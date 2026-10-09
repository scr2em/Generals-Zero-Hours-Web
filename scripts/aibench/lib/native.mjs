// Runs matches with the native headless build of the game (zh_headless, the CMake preset "native-headless"): one
// process per match, reading the game data in place from a folder. Same interface as Browser in browser.mjs.
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

// The executable: the path itself, or zh_headless in the directory.
export function findNativeExecutable(p) {
	const full = path.resolve(p);
	const candidates = fs.existsSync(full) && fs.statSync(full).isDirectory()
		? [path.join(full, 'zh_headless'), path.join(full, 'GeneralsMD', 'zh_headless')]
		: [full];
	const exe = candidates.find((c) => fs.existsSync(c) && fs.statSync(c).isFile());
	if (!exe) throw new Error(`${p}: no zh_headless there (build it: cmake --preset native-headless && cmake --build build/native-headless --target zh_headless)`);
	return exe;
}

export class NativeRunner {
	// options: exe, data ('starter' or { zeroHour, generals }), starterDir (the starter content for 'starter')
	constructor(options) {
		this.o = options;
		if (options.data === 'starter') {
			this.zeroHour = options.starterDir;
			this.generals = null;
			if (!fs.existsSync(path.join(this.zeroHour, 'manifest.json')))
				throw new Error(`no starter content at ${this.zeroHour}: build it with the starter_pack target (cmake --build build/native-headless --target starter_pack)`);
		} else {
			this.zeroHour = options.data.zeroHour;
			this.generals = options.data.generals && fs.existsSync(options.data.generals) ? options.data.generals : null;
			if (!fs.existsSync(this.zeroHour)) throw new Error(`${this.zeroHour} does not exist: give the Zero Hour install with --zh (or $ZH_PATH)`);
		}
	}

	async open() {}
	async close() {}
	async prepareData(build, data, log) { log(`[${build}] native: game data read in place from ${this.zeroHour}${this.generals ? ' and ' + this.generals : ''}`); }
	async runBoot() { return { ok: false, error: '--boot needs a web build (the native build has no window)', log: [] }; }

	// Plays one match: { ok, result (parsed AIMATCH_RESULT), error, log (engine output lines), wallMs }.
	runMatch(build, args, timeoutMs) {
		const t0 = Date.now();
		const userData = fs.mkdtempSync(path.join(os.tmpdir(), 'aibench-native-'));
		const argv = ['--zh', this.zeroHour, '--userdata', userData];
		if (this.generals) argv.push('--generals', this.generals);
		argv.push(...args);
		return new Promise((resolve) => {
			const lines = [];
			let result = null, error = null, partial = '';
			const child = spawn(this.o.exe, argv, { stdio: ['ignore', 'pipe', 'pipe'], env: { ...process.env, ZH_PATH: '', GENERALS_PATH: '' } });
			const take = (chunk) => {
				const text = partial + chunk.toString('utf8');
				const parts = text.split('\n');
				partial = parts.pop();
				for (const l of parts) {
					lines.push(l);
					if (l.startsWith('AIMATCH_RESULT ')) { try { result = JSON.parse(l.slice(15)); } catch (e) { error = 'unreadable result: ' + e.message; } }
					else if (l.startsWith('AIMATCH_ERROR ')) error = error || l.slice(14);
				}
			};
			child.stdout.on('data', take);
			child.stderr.on('data', take);
			const timer = setTimeout(() => { error = error || `no result after ${Math.round(timeoutMs / 1000)} s (hang or too slow)`; child.kill('SIGKILL'); }, timeoutMs);
			child.on('error', (e) => { error = error || 'the engine exited without a result: ' + e.message; });
			child.on('close', (code, signal) => {
				clearTimeout(timer);
				if (partial) lines.push(partial);
				fs.rmSync(userData, { recursive: true, force: true });
				if (!result && !error) {
					const fatal = lines.find((l) => /Fatal error|Assertion failed/.test(l));
					error = fatal ? 'engine failure: ' + fatal.slice(0, 300)
						: signal ? `engine failure: the engine was killed by ${signal}` : `the engine exited without a result (code ${code})`;
				}
				if (result && result.result && result.result.outcome === 'error') { error = result.result.endReason; result = null; }
				resolve({ ok: !!result && !error, result, error, log: lines.filter((l) => !/^\s*$/.test(l)), wallMs: Date.now() - t0 });
			});
		});
	}
}
