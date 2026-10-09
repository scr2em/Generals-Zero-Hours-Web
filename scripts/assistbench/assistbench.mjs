#!/usr/bin/env node
// Player assist test bench runner: plays scripted assist scenarios (-assistMatch) of the WebAssembly game in headless
// Chromium, in parallel, at full logic speed without rendering, and reports which passed. See README.md.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { startServer } from '../aibench/lib/server.mjs';
import { Browser } from '../aibench/lib/browser.mjs';

const HERE = path.dirname(new URL(import.meta.url).pathname);

const USAGE = `Usage: node assistbench.mjs --site DIR [options] [scenario.json | DIR ...]

  --site DIR              the web build to test (the directory with z_generals.html, e.g. build/web/GeneralsMD)
Game data (as aibench.mjs):
  --data starter|DIR      "starter" (default): the free starter content; DIR: a Zero Hour install or a folder with ZeroHour/
  --zh DIR                the Zero Hour install (default: $ZH_PATH); implies own game data
  --generals DIR          the original Generals install, optional (default: $GENERALS_PATH)
  --profile DIR           browser profile that keeps the imported game data (default ~/.cache/zh-aibench-profile)
Scenarios:
  scenario files or directories (default: scenarios/realdata with your own game data, scenarios/starter with the starter
  content; the starter ones only show that the bench runs, they say nothing about gameplay)
  --filter REGEX          only the scenarios whose name matches
How to run:
  --workers N             scenarios at the same time (default 2)
  --match-timeout SEC     wall seconds before a scenario is given up (default 300)
  --keep-logs             write every scenario's engine output to <out>/<name>.log (default: only those that did not pass)
  --engine-arg ARG        an extra engine argument for every scenario (repeatable)
  --out DIR               report directory (default ./assistbench-out/<time>)
  --port N                port of the local server (default 8947; keep it fixed with --profile)
  --chromium PATH         Chromium executable          --headful   show the browser
`;

function parseArgs(argv) {
	const o = { data: 'starter', workers: 2, matchTimeout: 300, engineArgs: [], port: 8947, scenarios: [],
		profile: path.join(os.homedir(), '.cache', 'zh-aibench-profile') };
	for (let i = 0; i < argv.length; ++i) {
		const a = argv[i];
		const next = () => { if (i + 1 >= argv.length) throw new Error(`${a} needs a value`); return argv[++i]; };
		switch (a) {
			case '--site': o.site = next(); break;
			case '--data': o.data = next(); o.dataGiven = true; break;
			case '--zh': o.zh = next(); break;
			case '--generals': o.generals = next(); break;
			case '--profile': o.profile = next(); break;
			case '--filter': o.filter = new RegExp(next()); break;
			case '--workers': o.workers = Number(next()); break;
			case '--match-timeout': o.matchTimeout = Number(next()); break;
			case '--keep-logs': o.keepLogs = true; break;
			case '--engine-arg': o.engineArgs.push(next()); break;
			case '--out': o.out = next(); break;
			case '--port': o.port = Number(next()); break;
			case '--chromium': o.chromium = next(); break;
			case '--headful': o.headful = true; break;
			case '-h': case '--help': console.log(USAGE); process.exit(0); break;
			default:
				if (a.startsWith('--')) throw new Error(`unknown option ${a}\n\n${USAGE}`);
				o.scenarios.push(a);
		}
	}
	if (!o.site) throw new Error(`--site is required\n\n${USAGE}`);
	o.site = path.resolve(o.site);
	if (!fs.existsSync(path.join(o.site, 'z_generals.html'))) throw new Error(`${o.site} has no z_generals.html`);
	// Own game data as { zeroHour, generals }, the same way aibench.mjs takes it.
	const zh = o.zh || (o.dataGiven ? null : process.env.ZH_PATH);
	const generals = o.generals || process.env.GENERALS_PATH;
	if (zh) {
		if (o.dataGiven) throw new Error('give the game data with --data or with --zh, not both');
		o.data = { zeroHour: path.resolve(zh), generals: generals ? path.resolve(generals) : null };
	} else if (o.data !== 'starter') {
		const dir = path.resolve(o.data);
		const isInstall = fs.existsSync(path.join(dir, 'INIZH.big'));
		o.data = {
			zeroHour: isInstall ? dir : path.join(dir, 'ZeroHour'),
			generals: o.generals ? path.resolve(o.generals) : isInstall ? (generals ? path.resolve(generals) : null) : path.join(dir, 'Generals'),
		};
	}
	if (!o.scenarios.length) o.scenarios.push(path.join(HERE, 'scenarios', o.data === 'starter' ? 'starter' : 'realdata'));
	o.out = path.resolve(o.out || path.join('assistbench-out', new Date().toISOString().replace(/[:.]/g, '-')));
	return o;
}

// A scenario file: { name, description, map, players, seed, steps: [...], maxFrames?, assists?, cash?, engineArgs?, log?, expect? }.
// log: lines the engine must print, as regular expressions ("ASSIST stance kite: unit \\d+ steps back") or
// { match, min, max } ({ match, max: 0 }: must not appear). expect: "pass" (default) or "fail" (a scenario that must fail:
// it checks the bench itself).
function loadScenarios(paths, filter) {
	const files = [];
	for (const p of paths) {
		if (fs.statSync(p).isDirectory()) files.push(...fs.readdirSync(p).filter((f) => f.endsWith('.json')).sort().map((f) => path.join(p, f)));
		else files.push(p);
	}
	const list = files.map((f) => {
		const s = JSON.parse(fs.readFileSync(f, 'utf8'));
		for (const k of ['map', 'players', 'steps']) if (!s[k]) throw new Error(`${f}: "${k}" is missing`);
		return { name: s.name || path.basename(f, '.json'), file: f, seed: 1, expect: 'pass', ...s };
	});
	return filter ? list.filter((s) => filter.test(s.name)) : list;
}

function argsOf(s, extra) {
	const steps = Array.isArray(s.steps) ? s.steps.join(';') : String(s.steps);
	const args = ['-assistMatch', `map=${s.map}`, `players=${s.players}`, `seed=${s.seed}`, `steps=${steps}`, `label=${s.name}`];
	if (s.maxFrames) args.push(`maxframes=${s.maxFrames}`);
	if (s.assists === false) args.push('assists=0');
	if (s.cash !== undefined) args.push(`cash=${s.cash}`);
	return [...args, ...(s.engineArgs || []), ...extra];
}

async function pool(items, n, fn) {
	let next = 0;
	const worker = async () => { for (;;) { const i = next++; if (i >= items.length) return; await fn(items[i], i); } };
	await Promise.all(Array.from({ length: Math.max(1, Math.min(n, items.length)) }, worker));
}

function summarize(rec) {
	const r = rec.result;
	if (!r) return `ERROR ${rec.error}`;
	const res = r.result || {};
	const bad = (r.checks || []).filter((c) => !c.pass);
	let text = `${res.outcome}: ${res.passed} passed, ${res.failed} failed, ${res.frames} frames`;
	if (r.perf) text += `, ${Math.round(r.perf.logicFps)} logic fps`;
	if (res.endReason) text += ` (${res.endReason})`;
	if (bad.length) text += `; first failure: step ${bad[0].step} "${bad[0].text}": ${bad[0].detail}`;
	return text;
}

async function main() {
	const o = parseArgs(process.argv.slice(2));
	const scenarios = loadScenarios(o.scenarios, o.filter);
	if (!scenarios.length) throw new Error('no scenarios');
	fs.mkdirSync(o.out, { recursive: true });
	const log = (m) => console.log(m);
	if (o.data === 'starter') log('Starter content: these runs only show that the bench works; gameplay is judged on the real game data (CLAUDE.md).');

	const { server, port } = await startServer({ site: o.site }, o.port).catch((e) => { throw new Error(`cannot listen on port ${o.port}: ${e.message}`); });
	const browser = new Browser({ port, profileDir: o.profile, chromium: o.chromium, headful: o.headful });
	await browser.open();
	const cleanup = async () => { await browser.close(); server.close(); };
	process.on('SIGINT', async () => { await cleanup(); process.exit(130); });

	const records = [];
	try {
		await browser.prepareData('site', o.data, log);
		log(`${scenarios.length} scenario(s), ${o.workers} at a time`);
		let done = 0;
		await pool(scenarios, o.workers, async (s) => {
			const r = await browser.runMatch('site', argsOf(s, o.engineArgs), o.matchTimeout * 1000, 'ASSISTMATCH');
			// a fatal error still prints the checks made before it
			let result = r.result;
			if (!result) {
				const line = r.log.find((l) => l.startsWith('ASSISTMATCH_RESULT '));
				if (line) try { result = JSON.parse(line.slice(19)); } catch { /* none */ }
			}
			// log checks: the decisions the assists print (-assistDebug lines), counted by the runner
			if (result && result.result && Array.isArray(s.log)) {
				for (const spec of s.log) {
					const c = typeof spec === 'string' ? { match: spec } : spec;
					const re = new RegExp(c.match);
					const count = r.log.filter((l) => re.test(l)).length;
					const min = c.min ?? (c.max === 0 ? 0 : 1);
					const pass = count >= min && (c.max === undefined || count <= c.max);
					result.checks.push({ step: 0, frame: result.result.frames, kind: 'log', text: `log /${c.match}/`, pass,
						detail: `${count} matching line(s), wanted ${min}${c.max !== undefined ? ' to ' + c.max : ' or more'}`, measured: { count } });
					result.result[pass ? 'passed' : 'failed'] += 1;
					if (!pass && result.result.outcome === 'pass') result.result.outcome = 'fail';
				}
			}
			const outcome = result && result.result ? result.result.outcome : 'error';
			const asExpected = outcome === s.expect;
			const rec = { name: s.name, file: path.relative(process.cwd(), s.file), description: s.description || '', expect: s.expect, outcome,
				asExpected, error: r.error || null, wallMs: r.wallMs, result };
			records.push(rec);
			if (!asExpected || o.keepLogs) fs.writeFileSync(path.join(o.out, s.name + '.log'), r.log.join('\n') + '\n');
			// the decisions of the assists and the steps, small enough to keep
			fs.writeFileSync(path.join(o.out, s.name + '.trace.txt'), r.log.filter((l) => /^(ASSIST|ASSISTMATCH|ASSISTTEST)/.test(l)).join('\n') + '\n');
			++done;
			log(`[${done}/${scenarios.length}] ${asExpected ? 'PASS' : 'FAIL'} ${s.name}${s.expect === 'fail' ? ' (must fail)' : ''}: ${summarize(rec)}, ${(r.wallMs / 1000).toFixed(1)} s`);
		});
	} finally {
		await cleanup();
	}

	records.sort((a, b) => (a.name < b.name ? -1 : 1));
	const passed = records.filter((r) => r.asExpected).length;
	fs.writeFileSync(path.join(o.out, 'report.json'), JSON.stringify({ data: o.data === 'starter' ? 'starter' : 'own', records }, null, 1));
	const md = [`# Player assist scenarios`, '', `${passed} of ${records.length} as expected.`, '',
		'| scenario | expected | outcome | checks passed / failed | frames | wall s | first failure |', '|---|---|---|---|---|---|---|'];
	for (const r of records) {
		const res = (r.result && r.result.result) || {};
		const bad = ((r.result && r.result.checks) || []).find((c) => !c.pass);
		const why = bad ? `step ${bad.step} \`${bad.text}\`: ${bad.detail}` : (r.error || res.endReason || '');
		md.push(`| ${r.asExpected ? '' : '**'}${r.name}${r.asExpected ? '' : '**'} | ${r.expect} | ${r.outcome} | ${res.passed ?? '-'} / ${res.failed ?? '-'} | ${res.frames ?? '-'} | ${(r.wallMs / 1000).toFixed(1)} | ${why.replace(/\|/g, '/')} |`);
	}
	md.push('', 'What each scenario checks:', '');
	for (const r of records) md.push(`- **${r.name}**: ${r.description}`);
	fs.writeFileSync(path.join(o.out, 'report.md'), md.join('\n') + '\n');
	log(`\n${passed} of ${records.length} scenario(s) as expected.\nReport: ${path.join(o.out, 'report.md')}`);
	process.exitCode = passed === records.length ? 0 : 1;
}

main().catch((e) => { console.error(e.message || e); process.exit(2); });
