#!/usr/bin/env node
// Automatic tuning of the Expert AI's settings (the ExpertSkill block of AIData) against the original Hard AI in mirror
// matches (expert:S against hard:S), with the native headless build. It plays the matches itself through aibench's
// library (lib/native.mjs, lib/plan.mjs, lib/stats.mjs) and passes every candidate's settings with -aiMatch aiini=<file>.
// A coordinate search: one setting at a time, screened on a few seeds, confirmed on more, the best checked on fresh seeds.
// Resumable: the matches played are kept in <out>/matches.jsonl and a rerun of the same command continues. See README.md,
// "Tuning the Expert".
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { NativeRunner, findNativeExecutable } from './lib/native.mjs';
import { playersArg, pool, runWithRetries, fnv1a } from './lib/plan.mjs';
import { wilson, pct } from './lib/stats.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(here, '..', '..');

const USAGE = `Usage: node scripts/aibench/tune.mjs --native PATH [options]

Searches the Expert AI's settings (ExpertSkill) for the most wins against the Hard AI in mirror matches, and writes a
report folder: summary.md, best.ini (pass it with aibench --aiini, or copy it into the code defaults), results.json.

Engine and data:
  --native PATH         the native headless build (zh_headless or its directory), required
  --zh DIR              the Zero Hour install (default $ZH_PATH)
  --generals DIR        the original Generals install, optional (default $GENERALS_PATH)
  --data starter        the starter pack instead (development only: checks the mechanism, never a gameplay result)
What to play:
  --map NAME            a 2-player map (default tournament desert; Ironwood Crossing with --data starter)
  --sides A,B,C         the mirrors expert:S against hard:S, sides as aibench takes them (default America,China,GLA)
  --timeout MIN         game minutes before a match is a timeout, which is not a win (default 30)
  --engine-arg ARG      an extra engine argument for every match (repeatable), e.g. cash=10000
The search:
  --params FILE         the settings to search (default scripts/aibench/tune_params.json): a JSON list of
                        { "name", "type": "real"|"int"|"bool", "values": [...], "default" }
  --seeds N             screening seeds per side (default 6; seeds 1..N)
  --confirm N           confirmation seeds per side (default 20; seeds 1001..)
  --holdout N           fresh seeds per side for the final check (default 30; seeds 100001..)
  --confirm-top N       candidates per setting that are confirmed (default 1: the best one on the screening seeds)
  --min-gain X          a change is kept when it raises the confirmed win rate by at least X (default 0.03) ...
  --max-side-drop X     ... and lowers no side's confirmed win rate by more than X (default 0.10)
  --passes N            passes over the settings at most (default 3); the search also ends when a pass changes nothing
  --target P            the win rate aimed at (default 0.8)
  --budget HOURS        wall time of this run (default 10); the search stops in time for the final check
How to run:
  --workers N           matches at the same time (default: cores - 1, at most 8; about 1 GB of memory each)
  --match-timeout SEC   wall seconds before a match is given up (default 2400)
  --retries N           repeats of a match that failed for technical reasons (default 1)
  --match-seconds S     wall seconds per match assumed by the plan before any was measured (default 150)
  --out DIR             report folder (default test-reports/<UTC date>-tune; without --out an unfinished or finished
                        folder of the same engine, data, map and time limit is continued)
  --dry-run             print the plan (matches, time) and exit
`;

function parseArgs(argv) {
	const o = {
		sides: null, map: null, seeds: 6, confirm: 20, holdout: 30, screenStart: 1, confirmStart: 1001, holdoutStart: 100001,
		minutes: 30, matchTimeout: 2400, retries: 1, params: path.join(here, 'tune_params.json'), target: 0.8, budget: 10,
		passes: 3, confirmTop: 1, minGain: 0.03, maxSideDrop: 0.10, engineArgs: [], matchSeconds: 150,
		workers: Math.max(1, Math.min(8, os.cpus().length - 1)),
	};
	const num = (a, v) => { const n = Number(v); if (!Number.isFinite(n)) throw new Error(`${a} needs a number, not "${v}"`); return n; };
	for (let i = 0; i < argv.length; ++i) {
		const a = argv[i];
		const next = () => { if (i + 1 >= argv.length) throw new Error(`${a} needs a value`); return argv[++i]; };
		switch (a) {
			case '--native': o.native = next(); break;
			case '--zh': o.zh = next(); break;
			case '--generals': o.generals = next(); break;
			case '--data': o.data = next(); if (o.data !== 'starter') throw new Error('--data takes only "starter" (use --zh for the game)'); break;
			case '--map': o.map = next(); break;
			case '--sides': o.sides = next().split(',').map((s) => s.trim()).filter(Boolean); break;
			case '--timeout': o.minutes = num(a, next()); break;
			case '--engine-arg': o.engineArgs.push(next()); break;
			case '--params': o.params = next(); break;
			case '--seeds': o.seeds = num(a, next()); break;
			case '--confirm': o.confirm = num(a, next()); break;
			case '--holdout': o.holdout = num(a, next()); break;
			case '--confirm-top': o.confirmTop = num(a, next()); break;
			case '--min-gain': o.minGain = num(a, next()); break;
			case '--max-side-drop': o.maxSideDrop = num(a, next()); break;
			case '--passes': o.passes = num(a, next()); break;
			case '--target': o.target = num(a, next()); break;
			case '--budget': o.budget = num(a, next()); break;
			case '--workers': o.workers = num(a, next()); break;
			case '--match-timeout': o.matchTimeout = num(a, next()); break;
			case '--retries': o.retries = num(a, next()); break;
			case '--match-seconds': o.matchSeconds = num(a, next()); break;
			case '--out': o.out = next(); break;
			case '--dry-run': o.dryRun = true; break;
			case '-h': case '--help': console.log(USAGE); process.exit(0); break;
			default: throw new Error(`unknown option ${a}\n\n${USAGE}`);
		}
	}
	if (!o.native) throw new Error(`--native is required\n\n${USAGE}`);
	o.starter = o.data === 'starter';
	if (!o.starter) {
		const zh = o.zh || process.env.ZH_PATH;
		const generals = o.generals || process.env.GENERALS_PATH;
		if (!zh && !o.dryRun) throw new Error('give the Zero Hour install with --zh or $ZH_PATH (or --data starter for a development run)');
		o.dataDirs = { zeroHour: zh ? path.resolve(zh) : null, generals: generals ? path.resolve(generals) : null };
		if (o.dataDirs.generals && !fs.existsSync(o.dataDirs.generals)) throw new Error(`${o.dataDirs.generals} does not exist (--generals / $GENERALS_PATH)`);
	}
	o.map ||= o.starter ? 'Ironwood Crossing' : 'tournament desert';
	o.sides ||= o.starter ? ['Ironwood'] : ['America', 'China', 'GLA'];
	if (o.seeds < 1 || o.confirm < 1 || o.holdout < 1) throw new Error('--seeds, --confirm and --holdout must be at least 1');
	// the three seed ranges must not overlap: the holdout is a fresh check
	if (o.screenStart + o.seeds > o.confirmStart || o.confirmStart + o.confirm > o.holdoutStart)
		throw new Error(`the seed ranges overlap: screening ${o.screenStart}.., confirm ${o.confirmStart}.., holdout ${o.holdoutStart}..`);
	return o;
}

// ------------------------------------------------------------------------------------------------------------------
// Settings: { "ExpertSkill.WaveSizeScale": 1.2, ... }; {} is the game's own (no aiini at all).

function loadParams(file) {
	const raw = JSON.parse(fs.readFileSync(file, 'utf8'));
	const list = Array.isArray(raw) ? raw : raw.params;
	if (!Array.isArray(list) || !list.length) throw new Error(`${file}: expected a JSON list of settings`);
	const seen = new Set();
	return list.map((p, i) => {
		if (!p || typeof p.name !== 'string' || !/^[A-Za-z]+(\.[A-Za-z]+)?$/.test(p.name)) throw new Error(`${file}: entry ${i + 1} has no valid "name"`);
		const name = p.name.includes('.') ? p.name : 'ExpertSkill.' + p.name;
		if (!/^(AIData|ExpertSkill)\./.test(name)) throw new Error(`${file}: ${p.name}: only AIData.<field> and ExpertSkill.<field> (or a bare ExpertSkill field)`);
		if (seen.has(name)) throw new Error(`${file}: ${name} is listed twice`);
		seen.add(name);
		if (!['real', 'int', 'bool'].includes(p.type)) throw new Error(`${file}: ${p.name}: "type" must be real, int or bool`);
		if (!Array.isArray(p.values) || !p.values.length) throw new Error(`${file}: ${p.name}: "values" must be a list`);
		for (const v of p.values) {
			if (p.type === 'bool' ? typeof v !== 'boolean' : typeof v !== 'number' || !Number.isFinite(v) || (p.type === 'int' && !Number.isInteger(v)))
				throw new Error(`${file}: ${p.name}: ${JSON.stringify(v)} is not a ${p.type}`);
		}
		return { name, short: name.replace(/^ExpertSkill\./, ''), type: p.type, values: p.values, default: p.default, about: p.about || '' };
	});
}

const sameValue = (a, b) => (typeof a === 'number' && typeof b === 'number'
	? Math.abs(a - b) <= 1e-5 * Math.max(1, Math.abs(a), Math.abs(b)) : a === b);
const valueText = (v) => (typeof v === 'boolean' ? (v ? 'Yes' : 'No') : String(v));
const sortedEntries = (settings) => Object.entries(settings).sort((a, b) => (a[0] < b[0] ? -1 : 1));
const configKey = (settings) => (Object.keys(settings).length ? 'c' + fnv1a(JSON.stringify(sortedEntries(settings))) : 'defaults');
const describe = (settings) => sortedEntries(settings).map(([k, v]) => `${k.replace(/^ExpertSkill\./, '')}=${valueText(v)}`).join(' ') || "the game's settings";

// The aiini file of some settings: an AIData block with the AIData fields and an ExpertSkill block.
function iniText(settings, comments = []) {
	const L = comments.map((c) => '; ' + c);
	const top = sortedEntries(settings).filter(([k]) => k.startsWith('AIData.'));
	const skill = sortedEntries(settings).filter(([k]) => k.startsWith('ExpertSkill.'));
	L.push('AIData');
	for (const [k, v] of top) L.push(`  ${k.slice(7)} = ${valueText(v)}`);
	L.push('  ExpertSkill');
	for (const [k, v] of skill) L.push(`    ${k.slice(12)} = ${valueText(v)}`);
	L.push('  End', 'End');
	return L.join('\n') + '\n';
}

// ------------------------------------------------------------------------------------------------------------------
// Score of some settings on some seeds: per side and over all sides, from the Expert's point of view.

function emptyTally() { return { wins: 0, losses: 0, timeouts: 0, draws: 0, errors: 0, played: 0 }; }
function addTo(t, outcome) {
	if (outcome === 'error') { t.errors++; return; }
	t.played++;
	if (outcome === 'win') t.wins++; else if (outcome === 'loss') t.losses++; else if (outcome === 'timeout') t.timeouts++; else t.draws++;
}
const rate = (t) => (t.played ? t.wins / t.played : 0);
const ciText = (t) => { const c = wilson(t.wins, t.played); return `${t.wins}/${t.played} (${pct(c.p)}, 95% CI ${pct(c.lo)}-${pct(c.hi)})`; };
const seedRange = (start, n) => Array.from({ length: n }, (_, i) => start + i);

// ------------------------------------------------------------------------------------------------------------------

function sha1File(file) {
	const h = crypto.createHash('sha1');
	const fd = fs.openSync(file, 'r');
	const buf = Buffer.alloc(1 << 20);
	try { for (let n; (n = fs.readSync(fd, buf, 0, buf.length, null)) > 0;) h.update(buf.subarray(0, n)); } finally { fs.closeSync(fd); }
	return h.digest('hex');
}

function gitCommit() {
	const r = spawnSync('git', ['-C', ROOT, 'log', '-1', '--format=%h %s'], { encoding: 'utf8' });
	return r.status === 0 ? r.stdout.trim() : 'unknown';
}

const hours = (ms) => (ms / 3600e3).toFixed(1) + ' h';

class Tuner {
	constructor(o, params) {
		this.o = o;
		this.params = params;
		this.cache = new Map();      // match id -> record (matches.jsonl)
		this.errors = new Map();     // match id -> error of this run (not kept: a rerun tries again)
		this.playedNow = 0;
		this.started = Date.now();
		this.history = [];
		this.current = {};           // the game's value of every searched setting (from the engine)
	}

	log(m) { console.log(m); }

	// -- files ------------------------------------------------------------------------------------------------------
	loadCache() {
		const f = path.join(this.o.out, 'matches.jsonl');
		if (!fs.existsSync(f)) return;
		for (const line of fs.readFileSync(f, 'utf8').split('\n')) {
			if (!line.trim()) continue;
			try { const r = JSON.parse(line); this.cache.set(r.id, r); } catch { /* a line cut by a crash */ }
		}
	}

	iniPath(settings) {
		const key = configKey(settings);
		if (key === 'defaults') return null;
		const file = path.join(this.o.out, 'configs', key + '.ini');
		if (!fs.existsSync(file)) {
			fs.mkdirSync(path.dirname(file), { recursive: true });
			fs.writeFileSync(file, iniText(settings, [`written by scripts/aibench/tune.mjs: ${describe(settings)}`]));
		}
		return file;
	}

	// -- matches ----------------------------------------------------------------------------------------------------
	job(settings, side, seed, variant = '') {
		const key = configKey(settings);
		const id = `${key}-${side}-s${seed}${variant ? '-' + variant : ''}`;
		const first = seed % 2 === 1;     // the start positions alternate with the seed
		const players = [
			{ difficulty: 'expert', side, variant, team: null, start: first ? 1 : 2 },
			{ difficulty: 'hard', side, variant: '', team: null, start: first ? 2 : 1 },
		];
		const ini = this.iniPath(settings);
		const args = ['-aiMatch', `map=${this.o.map}`, `players=${playersArg(players)}`, `seed=${seed}`, `timeout=${this.o.minutes}`, `label=${id}`, ...this.o.engineArgs];
		if (ini) args.push(`aiini=${ini}`);
		return { id, key, side, seed, variant, args, settings };
	}

	jobs(settings, seeds, variant = '') {
		const out = [];
		for (const side of this.o.sides) for (const seed of seeds) out.push(this.job(settings, side, seed, variant));
		return out;
	}

	meanMatchMs() {
		const w = [...this.cache.values()].map((r) => r.wallMs).filter((x) => x > 0);
		return w.length ? w.reduce((a, b) => a + b, 0) / w.length : this.o.matchSeconds * 1000;
	}

	// Plays the jobs that have no result yet. keepLogs: write the Expert's decisions of losses and timeouts.
	async ensure(jobs, title, keepLogs = false) {
		const todo = [];
		const seen = new Set();
		for (const j of jobs) if (!this.cache.has(j.id) && !seen.has(j.id)) { seen.add(j.id); todo.push(j); }
		if (!todo.length) return;
		const eta = todo.length * this.meanMatchMs() / Math.min(this.o.workers, todo.length);
		this.log(`${title}: ${todo.length} match(es) to play, about ${(eta / 60e3).toFixed(0)} min`);
		let done = 0, failed = 0;
		await pool(todo, this.o.workers, async (j) => {
			const r = await runWithRetries(this.runner, 'native', j.args, this.o.matchTimeout * 1000, this.o.retries, (m) => this.log(m), j.id);
			++done;
			let outcome = 'error';
			if (r.ok) {
				const res = r.result.result;
				outcome = res.outcome === 'victory' ? (res.winners.includes(0) ? 'win' : 'loss') : res.outcome;
				const rec = { id: j.id, config: j.key, side: j.side, seed: j.seed, variant: j.variant, outcome, frames: res.frames, wallMs: r.wallMs };
				this.cache.set(j.id, rec);
				fs.appendFileSync(path.join(this.o.out, 'matches.jsonl'), JSON.stringify(rec) + '\n');
				this.errors.delete(j.id);
				++this.playedNow;
			} else {
				++failed;
				this.errors.set(j.id, r.error);
				fs.mkdirSync(path.join(this.o.out, 'errors'), { recursive: true });
				fs.writeFileSync(path.join(this.o.out, 'errors', j.id + '.log'), r.log.slice(-200).join('\n') + '\n');
			}
			if (keepLogs && (outcome === 'loss' || outcome === 'timeout' || outcome === 'error')) {
				fs.mkdirSync(path.join(this.o.out, 'holdout'), { recursive: true });
				fs.writeFileSync(path.join(this.o.out, 'holdout', j.id + '.trace.txt'),
					r.log.filter((l) => l.startsWith('AISTRAT[') || l.includes('AIMATCH')).join('\n') + '\n');
			}
			this.log(`  [${done}/${todo.length}] ${j.id}: ` + (r.ok ? `${outcome}, ${r.result.result.frames} frames, ${(r.wallMs / 1000).toFixed(0)} s` : `ERROR ${r.error}`));
		});
		// An engine that fails again and again would waste the night: stop.
		if (todo.length >= 4 && failed > todo.length / 4)
			throw new Error(`${failed} of ${todo.length} matches failed (see ${path.join(this.o.out, 'errors')}); stopping`);
	}

	score(settings, seeds, variant = '') {
		const perSide = Object.fromEntries(this.o.sides.map((s) => [s, emptyTally()]));
		const all = emptyTally();
		for (const j of this.jobs(settings, seeds, variant)) {
			const rec = this.cache.get(j.id);
			const outcome = rec ? rec.outcome : 'error';
			addTo(perSide[j.side], outcome);
			addTo(all, outcome);
		}
		return { all, perSide };
	}

	// -- the game's values of the searched settings ------------------------------------------------------------------
	// One short match whose aiini sets every searched setting to a value nobody uses: the engine prints each change
	// "AIMATCH aiini ExpertSkill.X: <old> -> <new>", which gives the game's value (the code default or the data's
	// AIData.ini). It also checks the setting names, the map and the side before a night of matches.
	async readCurrentValues() {
		const probe = {};
		for (const p of this.params) probe[p.name] = p.type === 'bool' ? true : p.type === 'int' ? 98765 : 98765.5;
		const file = path.join(this.o.out, 'configs', 'probe.ini');
		fs.mkdirSync(path.dirname(file), { recursive: true });
		fs.writeFileSync(file, iniText(probe, ['written by scripts/aibench/tune.mjs to read the game\'s values (a 2-frame match)']));
		const side = this.o.sides[0];
		const players = playersArg([{ difficulty: 'expert', side, variant: '', start: 1 }, { difficulty: 'hard', side, variant: '', start: 2 }]);
		const args = ['-aiMatch', `map=${this.o.map}`, `players=${players}`, 'seed=1', 'maxframes=2', 'label=probe', `aiini=${file}`, ...this.o.engineArgs];
		const r = await runWithRetries(this.runner, 'native', args, this.o.matchTimeout * 1000, this.o.retries);
		if (!r.ok) throw new Error(`the first check match failed: ${r.error}\n${r.log.slice(-15).join('\n')}`);
		const changed = new Map();
		for (const l of r.log) {
			const m = /^AIMATCH aiini ((?:AIData|ExpertSkill)\.\w+): (.*) -> (.*)$/.exec(l);
			if (m) changed.set(m[1], m[2]);
		}
		for (const p of this.params) {
			const old = changed.get(p.name);
			if (p.type === 'bool') this.current[p.name] = old === undefined ? true : old === 'Yes';
			else if (old === undefined) throw new Error(`${p.name}: the engine did not report its value (is it a ${p.type}?)`);
			else this.current[p.name] = Number(old);
			if (p.default !== undefined && !sameValue(p.default, this.current[p.name]))
				this.log(`note: ${p.short} is ${valueText(this.current[p.name])} in the game (the params file says ${valueText(p.default)}); the search starts from the game's value`);
		}
		this.log(`the game's values: ${this.params.map((p) => `${p.short}=${valueText(this.current[p.name])}`).join(' ')}`);
	}

	value(settings, p) { return p.name in settings ? settings[p.name] : this.current[p.name]; }

	with(settings, p, v) {
		const s = { ...settings, [p.name]: v };
		if (sameValue(v, this.current[p.name])) delete s[p.name];
		return s;
	}

	// -- the search -------------------------------------------------------------------------------------------------
	screenSeeds() { return seedRange(this.o.screenStart, this.o.seeds); }
	confirmSeeds() { return seedRange(this.o.confirmStart, this.o.confirm); }
	holdoutSeeds() { return seedRange(this.o.holdoutStart, this.o.holdout); }

	// Wall time left for the search: the budget minus what the final check (still to play) needs.
	outOfTime(nextMatches, best) {
		const perMatch = this.meanMatchMs() / this.o.workers;
		const holdout = [...this.jobs({}, this.holdoutSeeds(), 'trace'), ...this.jobs(best, this.holdoutSeeds(), 'trace')].filter((j) => !this.cache.has(j.id));
		const need = (nextMatches + new Set(holdout.map((j) => j.id)).size) * perMatch;
		return Date.now() - this.started + need > this.o.budget * 3600e3;
	}

	// Accept a candidate over the incumbent on the same confirmation matches?
	accept(cand, inc) {
		const gain = rate(cand.all) - rate(inc.all);
		if (gain < this.o.minGain) return `confirmed ${pct(rate(cand.all))} against ${pct(rate(inc.all))}: gain below ${pct(this.o.minGain)}`;
		for (const s of this.o.sides) {
			const drop = rate(inc.perSide[s]) - rate(cand.perSide[s]);
			if (drop > this.o.maxSideDrop) return `${s} drops ${pct(drop)} (more than ${pct(this.o.maxSideDrop)})`;
		}
		return null;
	}

	async search() {
		const S = this.screenSeeds(), C = this.confirmSeeds();
		let inc = {};
		await this.ensure([...this.jobs(inc, S), ...this.jobs(inc, C)], "the game's settings (start of the search)");
		this.baselineConfirm = this.score(inc, C);
		this.log(`start: ${ciText(this.baselineConfirm.all)} on the confirmation seeds`);
		this.stopReason = null;
		for (let pass = 1; pass <= this.o.passes && !this.stopReason; ++pass) {
			let changed = false;
			for (const p of this.params) {
				const values = p.values.filter((v) => !sameValue(v, this.value(inc, p)));
				if (!values.length) continue;
				const cands = values.map((v) => ({ v, settings: this.with(inc, p, v) }));
				const screenJobs = [...this.jobs(inc, S), ...cands.flatMap((c) => this.jobs(c.settings, S))];
				if (this.outOfTime(screenJobs.filter((j) => !this.cache.has(j.id)).length, inc)) { this.stopReason = 'the time budget'; break; }
				await this.ensure(screenJobs, `pass ${pass}, ${p.short}: screening ${values.map(valueText).join(', ')} (now ${valueText(this.value(inc, p))})`);
				const incScreen = this.score(inc, S);
				for (const c of cands) c.screen = this.score(c.settings, S);
				const better = cands.filter((c) => c.screen.all.wins > incScreen.all.wins && rate(c.screen.all) > rate(incScreen.all))
					.sort((a, b) => rate(b.screen.all) - rate(a.screen.all) || b.screen.all.wins - a.screen.all.wins);
				for (const c of cands) if (!better.includes(c)) this.record(pass, p, c, incScreen, null, null, 'not better on the screening seeds');
				let accepted = null;
				for (const [i, c] of better.entries()) {
					if (accepted || this.stopReason) { this.record(pass, p, c, incScreen, null, null, 'not confirmed (' + (accepted ? `${valueText(accepted.v)} was kept` : 'time budget') + ')'); continue; }
					if (i >= this.o.confirmTop) { this.record(pass, p, c, incScreen, null, null, 'not confirmed (another value was better on the screening seeds)'); continue; }
					const confirmJobs = [...this.jobs(inc, C), ...this.jobs(c.settings, C)];
					if (this.outOfTime(confirmJobs.filter((j) => !this.cache.has(j.id)).length, inc)) {
						this.stopReason = 'the time budget';
						this.record(pass, p, c, incScreen, null, null, 'not confirmed (time budget)');
						continue;
					}
					await this.ensure(confirmJobs, `pass ${pass}, ${p.short}=${valueText(c.v)}: confirming`);
					const incConfirm = this.score(inc, C), candConfirm = this.score(c.settings, C);
					const why = this.accept(candConfirm, incConfirm);
					this.record(pass, p, c, incScreen, candConfirm, incConfirm, why ? 'rejected: ' + why : 'ACCEPTED');
					this.log(`  ${p.short}=${valueText(c.v)}: confirmed ${ciText(candConfirm.all)} against ${ciText(incConfirm.all)}: ${why ? 'rejected (' + why + ')' : 'ACCEPTED'}`);
					if (!why) accepted = c;
				}
				if (accepted) { inc = accepted.settings; changed = true; }
				this.saveState('searching', inc);
				if (this.stopReason) break;
			}
			if (!changed && !this.stopReason) this.stopReason = pass === 1 ? 'no change beat the game\'s settings' : `pass ${pass} changed nothing`;
			else if (pass === this.o.passes && !this.stopReason) this.stopReason = `the last pass (--passes ${this.o.passes})`;
		}
		this.best = inc;
		this.bestConfirm = this.score(inc, C);
	}

	record(pass, p, c, incScreen, candConfirm, incConfirm, decision) {
		this.history.push({
			pass, setting: p.name, value: c.v, settings: c.settings,
			screen: { candidate: c.screen.all, incumbent: incScreen.all },
			confirm: candConfirm ? { candidate: candConfirm, incumbent: incConfirm } : null,
			decision,
		});
	}

	async finalCheck() {
		const H = this.holdoutSeeds();
		// both with the Expert's trace (its decisions in the log; it does not change the play): the logs of the best
		// settings' losses and timeouts are kept
		await this.ensure(this.jobs(this.best, H, 'trace'), 'final check of the best settings on fresh seeds', true);
		await this.ensure(this.jobs({}, H, 'trace'), "final check of the game's settings on the same seeds", configKey(this.best) === 'defaults');
		this.holdout = { baseline: this.score({}, H, 'trace'), best: this.score(this.best, H, 'trace') };
	}

	saveState(status, incumbent) {
		const f = path.join(this.o.out, 'state.json');
		const prev = fs.existsSync(f) ? JSON.parse(fs.readFileSync(f, 'utf8')) : {};
		const runs = prev.runs || [];
		const now = { started: new Date(this.started).toISOString(), updated: new Date().toISOString(), matchesPlayed: this.playedNow };
		if (runs.length && runs[runs.length - 1].started === now.started) runs[runs.length - 1] = now; else runs.push(now);
		fs.writeFileSync(f, JSON.stringify({ schema: 'zh-aibench-tune-state-1', fingerprint: this.fingerprint, status, incumbent, runs, command: process.argv.slice(2) }, null, 1));
	}

	// -- the report -------------------------------------------------------------------------------------------------
	writeReport() {
		const o = this.o;
		const H = this.holdout;
		const verdict = (t) => {
			const c = wilson(t.wins, t.played);
			return { point: c.p, lo: c.lo, hi: c.hi, pointMet: c.p >= o.target, lowerBoundMet: c.lo >= o.target };
		};
		const target = { overall: verdict(H.best.all), perSide: Object.fromEntries(o.sides.map((s) => [s, verdict(H.best.perSide[s])])) };
		const changed = sortedEntries(this.best);
		const commit = gitCommit();
		const comments = [
			'Expert settings found by scripts/aibench/tune.mjs' + (o.starter ? ' ON THE STARTER PACK (a mechanism check, not a gameplay result)' : ''),
			`${new Date().toISOString()}, commit ${commit}, map ${o.map}, sides ${o.sides.join(', ')}, ${o.minutes} game minutes`,
			`holdout (${o.holdout} fresh seeds per side): best ${ciText(H.best.all)}, game's settings ${ciText(H.baseline.all)}`,
			'Use: node scripts/aibench/aibench.mjs --native ... --aiini best.ini; for good: the same values as ex.m_... defaults in AI.cpp',
		];
		if (!changed.length) comments.push('No change beat the game\'s settings: this file changes nothing.');
		fs.writeFileSync(path.join(o.out, 'best.ini'), iniText(this.best, comments));

		const results = {
			schema: 'zh-aibench-tune-1', generated: new Date().toISOString(), commit, starterPack: o.starter,
			options: { map: o.map, sides: o.sides, minutes: o.minutes, seeds: o.seeds, confirm: o.confirm, holdout: o.holdout, screenStart: o.screenStart,
				confirmStart: o.confirmStart, holdoutStart: o.holdoutStart, confirmTop: o.confirmTop, minGain: o.minGain, maxSideDrop: o.maxSideDrop,
				passes: o.passes, target: o.target, budgetHours: o.budget, workers: o.workers, engineArgs: o.engineArgs, params: o.params },
			fingerprint: this.fingerprint, gameValues: this.current, params: this.params,
			baseline: { settings: {}, confirm: this.baselineConfirm, holdout: H.baseline },
			best: { settings: this.best, key: configKey(this.best), confirm: this.bestConfirm, holdout: H.best },
			target, stopReason: this.stopReason, history: this.history,
			matches: { total: this.cache.size, playedThisRun: this.playedNow, errorsThisRun: [...this.errors].map(([id, error]) => ({ id, error })), meanWallSeconds: this.meanMatchMs() / 1000 },
		};
		fs.writeFileSync(path.join(o.out, 'results.json'), JSON.stringify(results, null, 1));

		const yes = (b) => (b ? 'met' : 'not met');
		const L = [];
		L.push('# Expert tuning: Expert against Hard in mirror matches', '');
		if (o.starter) L.push('**Starter pack: this run only checks that the tuner works. It is not a gameplay result.**', '');
		L.push(`${new Date().toISOString().slice(0, 16).replace('T', ' ')} UTC, commit ${commit}, ${os.type()} ${os.arch()}. Map ${o.map}; mirrors ${o.sides.map((s) => `expert:${s} against hard:${s}`).join(', ')}; ` +
			`${o.minutes} game minutes, after which a match is a timeout (not a win). Start positions alternate with the seed.`, '');
		L.push(`Seeds per side: screening ${o.screenStart}-${o.screenStart + o.seeds - 1}, confirmation ${o.confirmStart}-${o.confirmStart + o.confirm - 1}, ` +
			`final check ${o.holdoutStart}-${o.holdoutStart + o.holdout - 1} (fresh: never used in the search).`, '');
		L.push('## Result on the fresh seeds', '');
		L.push('| | game\'s settings | best settings |', '|---|---|---|');
		for (const s of o.sides) L.push(`| ${s} | ${ciText(H.baseline.perSide[s])} | ${ciText(H.best.perSide[s])} |`);
		L.push(`| **all** | **${ciText(H.baseline.all)}** | **${ciText(H.best.all)}** |`, '');
		L.push(`Target ${pct(o.target)} for the best settings: point estimate ${pct(target.overall.point)} (${yes(target.overall.pointMet)}), lower bound of the 95% interval ${pct(target.overall.lo)} (${yes(target.overall.lowerBoundMet)}). ` +
			'Per side: ' + o.sides.map((s) => `${s} ${pct(target.perSide[s].point)} (${yes(target.perSide[s].pointMet)}), lower bound ${pct(target.perSide[s].lo)} (${yes(target.perSide[s].lowerBoundMet)})`).join('; ') + '.', '');
		const to = (t) => `${t.timeouts} timeouts, ${t.draws} draws` + (t.errors ? `, ${t.errors} errors (not counted)` : '');
		L.push(`Not won: game's settings ${to(H.baseline.all)}; best settings ${to(H.best.all)}.`, '');
		L.push('## Best settings (best.ini)', '');
		if (!changed.length) L.push('No change beat the game\'s settings.', '');
		else {
			L.push('| setting | game\'s value | best |', '|---|---|---|');
			for (const [k, v] of changed) L.push(`| ${k} | ${valueText(this.current[k])} | **${valueText(v)}** |`);
			L.push('', 'Try them: `node scripts/aibench/aibench.mjs --native <zh_headless> --zh "$ZH_PATH" --aiini best.ini ...`. Make them the default: the same values in the `ex.m_...` lines of `AI::TAiData` in `GeneralsMD/Code/GameEngine/Source/GameLogic/AI/AI.cpp`, then run `scripts/gameplay/realdata_tests.sh mirror`.', '');
		}
		L.push('## Search', '');
		L.push(`Start: the game's settings won ${ciText(this.baselineConfirm.all)} on the confirmation seeds; the best settings ${ciText(this.bestConfirm.all)}. Search ended: ${this.stopReason}.`, '');
		L.push(`Rule: one setting at a time (coordinate search, in the order of the params file, up to ${o.passes} passes). Every other value of the setting is played on the ${o.seeds} screening seeds per side, together with the current settings. ` +
			`A value that wins more of these matches than the current settings is played on the ${o.confirm} confirmation seeds per side (the best ${o.confirmTop}). It is kept when its confirmed win rate over all sides is at least ${pct(o.minGain)} higher than that of the current settings on the same matches, ` +
			`and no side's confirmed win rate falls by more than ${pct(o.maxSideDrop)}. The kept settings are then played on fresh seeds (above), which the search never saw: the search picks among many candidates, so its own numbers are optimistic.`, '');
		if (this.history.length) {
			L.push('| pass | setting | value | screening: value / current | confirmation: value / current | per side (value / current) | decision |', '|---|---|---|---|---|---|---|');
			for (const h of this.history) {
				const sc = `${h.screen.candidate.wins}/${h.screen.candidate.played} / ${h.screen.incumbent.wins}/${h.screen.incumbent.played}`;
				const cf = h.confirm ? `${h.confirm.candidate.all.wins}/${h.confirm.candidate.all.played} / ${h.confirm.incumbent.all.wins}/${h.confirm.incumbent.all.played}` : '';
				const ps = h.confirm ? o.sides.map((s) => `${s} ${h.confirm.candidate.perSide[s].wins}/${h.confirm.incumbent.perSide[s].wins}`).join(', ') : '';
				L.push(`| ${h.pass} | ${h.setting.replace(/^ExpertSkill\./, '')} | ${valueText(h.value)} | ${sc} | ${cf} | ${ps} | ${h.decision} |`);
			}
			L.push('');
		}
		// the logs of the best settings; those of settings that were best in an earlier run of this folder go (size)
		const logDir = path.join(o.out, 'holdout');
		const all = fs.existsSync(logDir) ? fs.readdirSync(logDir) : [];
		for (const f of all) if (!f.startsWith(configKey(this.best) + '-')) fs.rmSync(path.join(logDir, f));
		const logs = all.filter((f) => f.startsWith(configKey(this.best) + '-')).sort();
		L.push('## Losses and timeouts of the best settings on the fresh seeds', '');
		if (!logs.length) L.push('None.', '');
		else {
			L.push('The Expert\'s decisions (its `trace`: `AISTRAT[...]` lines) and the match lines, one file per match:', '');
			for (const f of logs) {
				const rec = this.cache.get(f.replace(/\.trace\.txt$/, ''));
				L.push(`- [holdout/${f}](holdout/${f})` + (rec ? `: ${rec.side}, seed ${rec.seed}, ${rec.outcome} after ${(rec.frames / 1800).toFixed(0)} min` : ''));
			}
			L.push('');
		}
		const errs = [...this.errors];
		L.push('## Matches', '');
		L.push(`${this.cache.size} matches in matches.jsonl, ${this.playedNow} of them played in this run (${hours(Date.now() - this.started)}), about ${(this.meanMatchMs() / 1000).toFixed(0)} s per match with ${o.workers} at a time.` +
			(errs.length ? ` ${errs.length} failed and are not counted (errors/): ` + errs.slice(0, 10).map(([id, e]) => `${id}: ${e}`).join('; ') : ' No errors.'), '');
		L.push('Files: `best.ini` (the best settings as an aiini file), `results.json` (everything above), `matches.jsonl` (every match: the cache a rerun continues from), `configs/` (the aiini file of every candidate), `holdout/` (logs).', '');
		fs.writeFileSync(path.join(o.out, 'summary.md'), L.join('\n'));

		this.log('');
		this.log(`fresh seeds: game's settings ${ciText(H.baseline.all)}, best ${ciText(H.best.all)} (${describe(this.best)})`);
		for (const s of o.sides) this.log(`  ${s}: game's ${ciText(H.baseline.perSide[s])}, best ${ciText(H.best.perSide[s])}`);
		this.log(`target ${pct(o.target)}: point estimate ${yes(target.overall.pointMet)}, lower bound ${yes(target.overall.lowerBoundMet)}`);
		this.log(`\nReport: ${path.join(o.out, 'summary.md')}`);
	}
}

// ------------------------------------------------------------------------------------------------------------------
// The plan: how many matches a pass takes (from the game's values in the params file).

function plan(o, params, meanMs) {
	const n = o.sides.length;
	const rows = params.map((p) => {
		const alt = p.values.filter((v) => p.default === undefined || !sameValue(v, p.default)).length;
		return { name: p.short, values: p.values.map(valueText).join(', '), screening: alt * o.seeds * n };
	});
	const start = (o.seeds + o.confirm) * n;
	const screening = rows.reduce((a, r) => a + r.screening, 0);
	const confirmMax = params.length * Math.min(o.confirmTop, 99) * o.confirm * n;
	const holdout = 2 * o.holdout * n;
	const lo = start + screening + holdout, hi = lo + confirmMax;
	const t = (m) => hours(m * meanMs / o.workers);
	const L = [];
	L.push(`Plan: map ${o.map}, mirrors ${o.sides.map((s) => `expert:${s}-hard:${s}`).join(', ')}, ${o.minutes} game minutes, ${o.workers} at a time, budget ${o.budget} h`);
	L.push(`  start (the game's settings on screening + confirmation seeds): ${start} matches`);
	for (const r of rows) L.push(`  ${r.name.padEnd(18)} values ${r.values.padEnd(30)} screening ${r.screening} matches`);
	L.push(`  one pass: ${screening} screening matches + up to ${confirmMax} for confirmations (${o.confirm * n} per confirmed value)`);
	L.push(`  final check on fresh seeds: ${holdout} matches (the game's settings and the best, ${o.holdout} seeds per side)`);
	L.push(`  total for one pass: ${lo} to ${hi} matches, about ${t(lo)} to ${t(hi)} at ${(meanMs / 1000).toFixed(0)} s per match` +
		` (${o.workers} at a time); every further pass adds about ${screening} matches (${t(screening)}) plus its confirmations`);
	return L.join('\n');
}

function fingerprintOf(o, exe) {
	return {
		engine: sha1File(exe).slice(0, 16), data: o.starter ? 'starter' : o.dataDirs.zeroHour, generals: o.starter ? null : o.dataDirs.generals,
		map: o.map, minutes: o.minutes, engineArgs: o.engineArgs,
	};
}

// Without --out: continue the latest folder of the same engine, data, map and time limit, or start a new one.
function chooseOut(o, fp) {
	if (o.out) return path.resolve(o.out);
	const base = path.join(ROOT, 'test-reports');
	const same = [];
	if (fs.existsSync(base)) {
		for (const d of fs.readdirSync(base)) {
			const f = path.join(base, d, 'state.json');
			if (!/-tune(-\d+)?$/.test(d) || !fs.existsSync(f)) continue;
			try { if (JSON.stringify(JSON.parse(fs.readFileSync(f, 'utf8')).fingerprint) === JSON.stringify(fp)) same.push({ d: path.join(base, d), t: fs.statSync(f).mtimeMs }); } catch { /* not ours */ }
		}
	}
	if (same.length) return same.sort((a, b) => b.t - a.t)[0].d;
	const day = new Date().toISOString().slice(0, 10).replace(/-/g, '');
	let dir = path.join(base, `${day}-tune`);
	for (let i = 2; fs.existsSync(dir); ++i) dir = path.join(base, `${day}-tune-${i}`);
	return dir;
}

async function main() {
	const o = parseArgs(process.argv.slice(2));
	const params = loadParams(path.resolve(o.params));
	o.params = path.resolve(o.params);
	if (o.dryRun) {
		console.log(plan(o, params, o.matchSeconds * 1000));
		console.log(`  (the time assumes ${o.matchSeconds} s per match, --match-seconds; a run measures it and prints its own estimates)`);
		console.log(`Report folder: ${o.out ? path.resolve(o.out) : 'test-reports/<UTC date>-tune (or the folder of an earlier run of the same engine and data)'}`);
		return;
	}
	const exe = findNativeExecutable(o.native);
	const fp = fingerprintOf(o, exe);
	o.out = chooseOut(o, fp);
	const stateFile = path.join(o.out, 'state.json');
	if (fs.existsSync(stateFile)) {
		const prev = JSON.parse(fs.readFileSync(stateFile, 'utf8'));
		if (JSON.stringify(prev.fingerprint) !== JSON.stringify(fp))
			throw new Error(`${o.out} holds a tuning run of another engine, data, map or time limit:\n  then ${JSON.stringify(prev.fingerprint)}\n  now  ${JSON.stringify(fp)}\nGive another --out.`);
	}
	fs.mkdirSync(o.out, { recursive: true });

	const t = new Tuner(o, params);
	t.fingerprint = fp;
	t.loadCache();
	console.log(`Tuning into ${o.out}` + (t.cache.size ? ` (continuing: ${t.cache.size} matches already played)` : ''));
	if (o.starter) console.log('Starter pack: a check of the tuner only, never a gameplay result.');
	t.runner = new NativeRunner({ exe, data: o.starter ? 'starter' : o.dataDirs, starterDir: path.join(path.dirname(exe), 'starterpack') });
	await t.runner.prepareData('native', o.starter ? 'starter' : o.dataDirs, (m) => console.log(m));
	console.log(plan(o, params, t.meanMatchMs()));
	t.saveState('starting', {});
	await t.readCurrentValues();
	await t.search();
	console.log(`search ended (${t.stopReason}): best ${describe(t.best)}, confirmed ${ciText(t.bestConfirm.all)}`);
	t.saveState('final check', t.best);
	await t.finalCheck();
	t.writeReport();
	t.saveState('done', t.best);
}

export { parseArgs, loadParams, iniText, configKey, plan, Tuner };

// Run as a program (not when a test imports it).
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url))
	main().catch((e) => { console.error(e.message || e); process.exit(2); });
