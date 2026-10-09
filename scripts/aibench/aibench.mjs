#!/usr/bin/env node
// AI-vs-AI test bench runner: plays batches of computer-vs-computer matches of the WebAssembly game in headless
// Chromium (or of the native headless build, --native), in parallel, and reports win rates, Elo, speed and a
// determinism check. See README.md.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { startServer } from './lib/server.mjs';
import { Browser } from './lib/browser.mjs';
import { NativeRunner, findNativeExecutable } from './lib/native.mjs';
import { loadOverlay, makeOverlaySite } from './lib/overlay.mjs';
import { compareRuns } from './lib/stats.mjs';
import { buildReport, renderMarkdown, summaryText } from './lib/report.mjs';

const USAGE = `Usage: node aibench.mjs [options]

Builds (the web build directories that contain z_generals.html):
  --site DIR              the build to test
  --baseline DIR          a second build to compare against (reported as "baseline"; --site is then "candidate")
  --build NAME=DIR        any number of named builds (repeatable); the first is the baseline of the comparison
  --native PATH           the native headless build (zh_headless, CMake preset "native-headless", or its directory): its
                          matches run as processes, without a browser (reported as "native"); can be combined with the above
Game data:
  --data starter|DIR      "starter" (default): the free starter content built next to the page (starterpack/);
                          DIR: a Zero Hour install (the folder with INIZH.big), or a folder holding ZeroHour/ (and optionally
                          Generals/) of an installed game
  --zh DIR                the Zero Hour install (default: $ZH_PATH); implies own game data
  --generals DIR          the original Generals install, optional (default: $GENERALS_PATH)
  --overlay NAME[,NAME]   play on the starter content edited by scripts/aibench/fixtures/NAME.json (bench-only data that exercises
                          behaviour the starter units cannot show); uses its own browser profile
  --profile DIR           browser profile that keeps the imported game data between runs
                          (default ~/.cache/zh-aibench-profile; use the same one every time)
What to play:
  --map NAME              map (repeatable): folder/file name, display name or map path
  --matchup SPEC          players of one match (repeatable): "difficulty:side[:variant][@team],difficulty:side[:variant][@team],..."
                          difficulty: easy, normal, hard, expert.  side: a faction ("Ironwood", "America", ...) or random.
                          Default with the starter content: hard:Ironwood,hard:Ironwood
  --seeds N               seeds per matchup and map (default 4)        --seed-start N   first seed (default 1)
  --starts 1,2,...        start positions to rotate the players through (default 1..number of players); every seed
                          shifts the players by one position, so with two players each side starts on both spots
  --no-rotate             do not rotate: the players keep the order of the matchup on the start positions
  --timeout MIN           game minutes after which a match is a timeout (default 20)
  --crc-interval N        logic frames between CRC samples (default 300)
  --keep-logs             write the engine output of every match to matches/<id>.log (default: only failed matches)
  --engine-arg ARG        an extra engine argument for every match (repeatable), e.g. loop=engine, cash=20000, sample=60
How to run:
  --workers N             matches at the same time (default 2)
  --retries N             repeats of a match that failed for technical reasons (default 1)
  --match-timeout SEC     wall seconds before a match is given up (default 900)
  --determinism N|all     matches replayed to check determinism (default 2; 0 turns it off)
  --target "A>B=P"        expected win rate of configuration A against B, e.g. "expert:Ironwood>hard:Ironwood=0.8"
  --out DIR               report directory (default ./aibench-out/<time>)
  --port N                port of the local server (default 8947; keep it fixed with --profile)
  --chromium PATH         Chromium executable (default: /opt/pw-browsers, or Playwright's own)
  --headful               show the browser
  --probe                 print the maps and sides of the game data and exit
  --boot SEC              start the game like a player (intro videos clicked away, main menu with its shell map), watch
                          it for SEC seconds and exit 1 on any engine failure; the log goes to <out>/boot.log
`;

function parseArgs(argv) {
	const o = {
		builds: [], data: 'starter', maps: [], matchups: [], seeds: 4, seedStart: 1, workers: 2, retries: 1, minutes: 20,
		crcInterval: 300, engineArgs: [], matchTimeout: 900, determinism: '2', port: 8947, rotate: true, targets: [],
		profile: path.join(os.homedir(), '.cache', 'zh-aibench-profile'),
	};
	for (let i = 0; i < argv.length; ++i) {
		const a = argv[i];
		const next = () => { if (i + 1 >= argv.length) throw new Error(`${a} needs a value`); return argv[++i]; };
		switch (a) {
			case '--site': o.builds.push({ name: 'candidate', dir: next(), site: true }); break;
			case '--baseline': o.builds.unshift({ name: 'baseline', dir: next() }); break;
			case '--build': { const v = next(); const eq = v.indexOf('='); if (eq < 1) throw new Error('--build NAME=DIR'); o.builds.push({ name: v.slice(0, eq), dir: v.slice(eq + 1) }); break; }
			case '--native': o.builds.push({ name: 'native', dir: next(), native: true }); break;
			case '--data': o.data = next(); o.dataGiven = true; break;
			case '--zh': o.zh = next(); break;
			case '--generals': o.generals = next(); break;
			case '--profile': o.profile = next(); break;
			case '--overlay': o.overlay = next(); break;
			case '--map': o.maps.push(next()); break;
			case '--matchup': o.matchups.push(next()); break;
			case '--seeds': o.seeds = Number(next()); break;
			case '--seed-start': o.seedStart = Number(next()); break;
			case '--starts': o.starts = next().split(',').map(Number); break;
			case '--no-rotate': o.rotate = false; break;
			case '--timeout': o.minutes = Number(next()); break;
			case '--crc-interval': o.crcInterval = Number(next()); break;
			case '--keep-logs': o.keepLogs = true; break;
			case '--engine-arg': o.engineArgs.push(next()); break;
			case '--workers': o.workers = Number(next()); break;
			case '--retries': o.retries = Number(next()); break;
			case '--match-timeout': o.matchTimeout = Number(next()); break;
			case '--determinism': o.determinism = next(); break;
			case '--target': o.targets.push(next()); break;
			case '--out': o.out = next(); break;
			case '--port': o.port = Number(next()); break;
			case '--chromium': o.chromium = next(); break;
			case '--headful': o.headful = true; break;
			case '--probe': o.probe = true; break;
			case '--boot': o.boot = Number(next()); break;
			case '-h': case '--help': console.log(USAGE); process.exit(0); break;
			default: throw new Error(`unknown option ${a}\n\n${USAGE}`);
		}
	}
	if (!o.builds.length) throw new Error(`--site (or --baseline/--build/--native) is required\n\n${USAGE}`);
	const seen = new Set();
	for (const b of o.builds) {
		if (seen.has(b.name)) throw new Error(`two builds are called ${b.name}`);
		seen.add(b.name);
		if (b.native) {
			// the executable, and its directory, where the starter_pack target puts the starter content (starterpack/)
			b.exe = findNativeExecutable(b.dir);
			b.dir = path.dirname(b.exe);
			continue;
		}
		b.dir = path.resolve(b.dir);
		if (!fs.existsSync(path.join(b.dir, 'z_generals.html'))) throw new Error(`${b.dir} has no z_generals.html (is it the web build directory, e.g. build/bench/GeneralsMD?)`);
	}
	// Own game data as { zeroHour, generals }: from --zh/--generals ($ZH_PATH/$GENERALS_PATH), or from --data DIR.
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
	if (o.data !== 'starter' && (o.generals || process.env.GENERALS_PATH) && !fs.existsSync(o.data.generals)) throw new Error(`${o.data.generals} does not exist (--generals / $GENERALS_PATH)`);
	if (!o.maps.length && o.data === 'starter') o.maps.push('Ironwood Crossing');
	if (!o.matchups.length && o.data === 'starter') o.matchups.push('hard:Ironwood,hard:Ironwood');
	if (!o.probe && !o.boot && (!o.maps.length || !o.matchups.length)) throw new Error('--map and --matchup are required with your own game data');
	o.out = path.resolve(o.out || path.join('aibench-out', new Date().toISOString().replace(/[:.]/g, '-')));
	return o;
}

// "hard:Ironwood" -> { difficulty, side, variant, label }; the label is the configuration's name in the reports.
// "expert:China:trace@1": the same on team 1 (players of a team are allies; without @ every player is on his own).
function parsePlayer(text) {
	const at = text.trim().split('@');
	if (at.length > 2 || (at.length === 2 && !/^\d+$/.test(at[1]))) throw new Error(`player "${text}": the team after @ must be a number`);
	const team = at.length === 2 ? Number(at[1]) : null;
	const f = at[0].split(':');
	if (f.length < 2) throw new Error(`player "${text}" is not difficulty:side[:variant][@team]`);
	const difficulty = f[0].toLowerCase().replace(/^med(ium)?$/, 'normal').replace(/^brutal$/, 'hard');
	const variant = f[2] || '';
	const label = `${difficulty}:${f[1]}` + (variant && variant !== difficulty ? `:${variant}` : '');
	return { difficulty, side: f[1], variant, team, label };
}

function buildJobs(o) {
	const jobs = [];
	const matchups = o.matchups.map((m) => m.split(',').map(parsePlayer));
	const maxFramesArg = `timeout=${o.minutes}`;
	for (const build of o.builds) {
		for (const map of o.maps) {
			matchups.forEach((players, mi) => {
				for (let s = 0; s < o.seeds; ++s) {
					const seed = o.seedStart + s;
					const n = players.length;
					const starts = o.starts || Array.from({ length: n }, (_, i) => i + 1);
					const shift = o.rotate ? s % n : 0;
					const placed = players.map((p, i) => ({ ...p, start: starts[(i + shift) % starts.length] }));
					const id = `${build.name}-${map.replace(/\W+/g, '_')}-m${mi + 1}-s${seed}`;
					const spec = placed.map((p) => `${p.difficulty}:${p.side}:${p.team ?? ''}:${p.start}:${p.variant}`.replace(/:+$/, '')).join(',');
					jobs.push({
						id, build: build.name, map, matchup: mi + 1, seed, players: placed, kind: 'main',
						args: ['-aiMatch', `map=${map}`, `players=${spec}`, `seed=${seed}`, maxFramesArg, `crcinterval=${o.crcInterval}`, `label=${id}`, ...o.engineArgs],
					});
				}
			});
		}
	}
	return jobs;
}

// Runs fn over items with at most `n` at a time.
async function pool(items, n, fn) {
	let next = 0;
	const worker = async () => { for (;;) { const i = next++; if (i >= items.length) return; await fn(items[i], i); } };
	await Promise.all(Array.from({ length: Math.max(1, Math.min(n, items.length)) }, worker));
}

const technical = (e) => /^(no result|engine failure|browser|the engine exited|unreadable)/.test(e || '');

async function main() {
	const o = parseArgs(process.argv.slice(2));
	fs.mkdirSync(path.join(o.out, 'matches'), { recursive: true });
	const log = (m) => console.log(m);

	// Where each build's starter content is: its own directory, or an edited copy (--overlay).
	let mounts = Object.fromEntries(o.builds.map((b) => [b.name, b.dir]));
	let profileDir = o.profile;
	if (o.overlay) {
		if (o.data !== 'starter') throw new Error('--overlay edits the starter content: use it with --data starter');
		// several overlays can be combined: --overlay towers,strictfight
		const parts = o.overlay.split(',').map((n) => loadOverlay(path.join(path.dirname(new URL(import.meta.url).pathname), 'fixtures'), n.trim()));
		const overlay = { name: parts.map((p) => p.name).join('+'), description: parts.map((p) => p.description || p.name).join(' / '), edits: parts.flatMap((p) => p.edits) };
		log(`overlay ${overlay.name}: ${overlay.description || ''}`);
		mounts = Object.fromEntries(o.builds.map((b) => [b.name, makeOverlaySite(b.dir, overlay, o.out, { packOnly: !!b.native })]));
		profileDir = o.profile + '-overlay-' + overlay.name;
	}
	// The web builds play in the browser, the native ones as processes.
	const runners = {};
	const webBuilds = o.builds.filter((b) => !b.native);
	let server = null, browser = null;
	if (webBuilds.length) {
		const webMounts = Object.fromEntries(webBuilds.map((b) => [b.name, mounts[b.name]]));
		const started = await startServer(webMounts, o.port).catch((e) => { throw new Error(`cannot listen on port ${o.port}: ${e.message}`); });
		server = started.server;
		browser = new Browser({ port: started.port, profileDir, chromium: o.chromium, headful: o.headful });
		await browser.open();
		for (const b of webBuilds) runners[b.name] = browser;
	}
	for (const b of o.builds.filter((x) => x.native))
		runners[b.name] = new NativeRunner({ exe: b.exe, data: o.data, starterDir: path.join(mounts[b.name], 'starterpack') });
	const cleanup = async () => { if (browser) await browser.close(); if (server) server.close(); };
	process.on('SIGINT', async () => { await cleanup(); process.exit(130); });

	try {
		for (const b of o.builds) await runners[b.name].prepareData(b.name, o.data, log);

		if (o.boot) {
			const r = await runners[o.builds[0].name].runBoot(o.builds[0].name, o.boot);
			fs.writeFileSync(path.join(o.out, 'boot.log'), r.log.join('\n') + '\n');
			log(r.ok ? `boot: PASS (${o.boot} s without an engine failure)` : `boot: FAIL ${r.error}`);
			process.exitCode = r.ok ? 0 : 1;
			return;
		}

		if (o.probe) {
			const b = o.builds[0].name;
			const run = async (args) => (await runners[b].runMatch(b, ['-aiMatch', ...args], 120000)).error || '';
			log('Maps:  ' + ((await run(['map=__none__', 'players=hard:random,hard:random', 'seed=1'])).replace(/^.*Maps: /, '') || '(none)'));
			const maps = o.maps.length ? o.maps[0] : '__none__';
			log('Sides: ' + ((await run([`map=${maps}`, 'players=hard:__none__,hard:random', 'seed=1'])).replace(/^.*Sides: /, '') || '(give --map to see the sides)'));
			return;
		}

		const jobs = buildJobs(o);
		log(`${jobs.length} matches (${o.builds.map((b) => b.name).join(', ')} x ${o.maps.length} map(s) x ${o.matchups.length} matchup(s) x ${o.seeds} seeds), ${o.workers} at a time`);
		const records = [];
		let done = 0;
		const runJob = async (job, attempt = 0) => {
			const r = await runners[job.build].runMatch(job.build, job.args, o.matchTimeout * 1000);
			if (!r.ok && technical(r.error) && attempt < o.retries) { log(`  retry ${job.id}: ${r.error}`); return runJob(job, attempt + 1); }
			return { ...r, attempts: attempt + 1 };
		};
		const finish = (job, r, total) => {
			const rec = { id: job.id, build: job.build, kind: job.kind, job, ok: r.ok, error: r.error || null, result: r.result, wallMs: r.wallMs, attempts: r.attempts };
			fs.writeFileSync(path.join(o.out, 'matches', job.id + '.json'), JSON.stringify({ job, ok: r.ok, error: r.error, wallMs: r.wallMs, result: r.result }, null, 1));
			if (!r.ok || o.keepLogs) fs.writeFileSync(path.join(o.out, 'matches', job.id + '.log'), r.log.join('\n') + '\n');
			++done;
			const res = r.ok ? r.result.result : null;
			const winner = res && res.winners.length ? job.players[res.winners[0]].label : '-';
			log(`[${done}/${total}] ${job.id}: ` + (r.ok
				? `${res.outcome}${res.outcome === 'victory' ? ' (' + winner + ')' : ''}, ${res.frames} frames, ${Math.round(r.result.perf.logicFps)} logic fps, ${(r.wallMs / 1000).toFixed(1)} s`
				: `ERROR ${r.error}`));
			return rec;
		};
		await pool(jobs, o.workers, async (job) => { records.push(finish(job, await runJob(job), jobs.length + 0)); });
		records.sort((a, b) => (a.id < b.id ? -1 : 1));

		// Determinism: the same match again, same build. Two runs must agree on every CRC.
		const determinism = [];
		const okRecords = records.filter((r) => r.ok);
		const want = o.determinism === 'all' ? okRecords.length : Math.min(okRecords.length, Number(o.determinism) || 0);
		if (want > 0) {
			// spread over builds and matchups
			const picks = [];
			const stride = okRecords.length / want;
			for (let i = 0; i < want; ++i) picks.push(okRecords[Math.floor(i * stride)]);
			log(`determinism: replaying ${picks.length} match(es)`);
			await pool(picks, o.workers, async (orig) => {
				const job = { ...orig.job, id: orig.job.id + '-replay', kind: 'determinism', args: orig.job.args.map((a) => (a.startsWith('label=') ? 'label=' + orig.job.id + '-replay' : a)) };
				const r = await runJob(job);
				const cmp = compareRuns({ ok: true, result: orig.result }, r);
				if (!r.ok) fs.writeFileSync(path.join(o.out, 'matches', job.id + '.log'), r.log.join('\n') + '\n');
				fs.writeFileSync(path.join(o.out, 'matches', job.id + '.json'), JSON.stringify({ job, ok: r.ok, error: r.error, result: r.result }, null, 1));
				determinism.push({ id: orig.id, build: orig.build, seed: orig.job.seed, identical: cmp.identical, divergence: cmp.divergence, frames: orig.result.result.frames, crcSamples: orig.result.crcTimeline.crc.length });
				log(`  ${orig.id}: ` + (cmp.identical ? 'identical' : `DIVERGES ${JSON.stringify(cmp.divergence)}`));
			});
			determinism.sort((a, b) => (a.id < b.id ? -1 : 1));
		}

		const report = buildReport({ options: o, builds: o.builds.map((b) => b.name), records, determinism, targets: o.targets });
		fs.writeFileSync(path.join(o.out, 'report.json'), JSON.stringify(report, null, 1));
		fs.writeFileSync(path.join(o.out, 'report.md'), renderMarkdown(report));
		log('\n' + summaryText(report));
		log(`\nReport: ${path.join(o.out, 'report.md')}\n        ${path.join(o.out, 'report.json')}`);
		const bad = records.some((r) => !r.ok) || determinism.some((d) => !d.identical);
		process.exitCode = bad ? 1 : 0;
	} finally {
		await cleanup();
	}
}

main().catch((e) => { console.error(e.message || e); process.exit(2); });
