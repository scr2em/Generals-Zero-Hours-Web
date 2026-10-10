#!/usr/bin/env node
// Automated tuner for the Expert AI: searches the settings of the ExpertSkill block (space.json) by playing Expert
// against Hard with the real game data, one faction against the same faction (America, China, GLA), on 2-player maps.
// Each match is one run of scripts/aibench (native build). Search: an evolution strategy in a normalised space with
// antithetic samples and the same seeds for every candidate of a generation (common random numbers).
// The state is saved after every generation; run the same command again to continue.
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const AIBENCH = path.join(HERE, '..', 'aibench', 'aibench.mjs');

const USAGE = `Usage:
  node scripts/aitune/aitune.mjs tune   --native PATH --out DIR [options]
  node scripts/aitune/aitune.mjs verify --native PATH --out DIR --params FILE [--seeds N] [options]

tune: searches the Expert settings; continues from DIR/state.json when it exists.
verify: plays the tuned settings (FILE: best.json or state.json of a tune run) and the code defaults against Hard on
        fresh seeds, and reports both scores and Elo against Hard (Hard = 1000).

Options:
  --native PATH        native headless build (zh_headless or its directory). It is copied to DIR at the start, so a
                       rebuild during the run does not change the program under test
  --zh DIR             Zero Hour install (default $ZH_PATH); --generals DIR (default $GENERALS_PATH)
  --map NAME           repeatable; 2-player maps only (checked). Default "tournament desert"
  --sides LIST         default America,China,GLA (each plays against Hard of the same side)
  --seeds N            seeds per side and map per candidate (tune, default 4, even: half from each start; verify 20)
  --population N       candidates per generation besides the current mean (tune, default 6, even)
  --generations N      default 20
  --sigma X            initial step size in the normalised space (default 0.15)
  --sigma-decay X      step size factor per generation (default 0.95); --sigma-min X (default 0.04)
  --timeout MIN        game minutes per match (default 30)
  --workers N          matches at the same time (default: cores / 2)
  --space FILE         parameter space (default scripts/aitune/space.json)
`;

function parseArgs(argv) {
	const o = {
		cmd: argv[0], native: null, out: null, zh: process.env.ZH_PATH, generals: process.env.GENERALS_PATH,
		maps: [], sides: ['America', 'China', 'GLA'], seeds: null, population: 6, generations: 20, sigma: 0.15,
		sigmaDecay: 0.95, sigmaMin: 0.04, timeout: 30, workers: Math.max(1, Math.floor(os.cpus().length / 2)),
		space: path.join(HERE, 'space.json'), params: null,
	};
	for (let i = 1; i < argv.length; ++i) {
		const a = argv[i];
		const next = () => { if (i + 1 >= argv.length) throw new Error(`${a} needs a value`); return argv[++i]; };
		switch (a) {
			case '--native': o.native = next(); break;
			case '--out': o.out = path.resolve(next()); break;
			case '--zh': o.zh = next(); break;
			case '--generals': o.generals = next(); break;
			case '--map': o.maps.push(next()); break;
			case '--sides': o.sides = next().split(',').map((s) => s.trim()).filter(Boolean); break;
			case '--seeds': o.seeds = parseInt(next(), 10); break;
			case '--population': o.population = parseInt(next(), 10); break;
			case '--generations': o.generations = parseInt(next(), 10); break;
			case '--sigma': o.sigma = parseFloat(next()); break;
			case '--sigma-decay': o.sigmaDecay = parseFloat(next()); break;
			case '--sigma-min': o.sigmaMin = parseFloat(next()); break;
			case '--timeout': o.timeout = parseFloat(next()); break;
			case '--workers': o.workers = parseInt(next(), 10); break;
			case '--space': o.space = next(); break;
			case '--params': o.params = next(); break;
			case '-h': case '--help': console.log(USAGE); process.exit(0);
			default: throw new Error(`unknown option ${a}\n\n${USAGE}`);
		}
	}
	if (o.cmd !== 'tune' && o.cmd !== 'verify') throw new Error(USAGE);
	if (!o.native || !o.out) throw new Error(`--native and --out are required\n\n${USAGE}`);
	if (!o.zh) throw new Error('give the Zero Hour install with --zh or $ZH_PATH');
	if (o.cmd === 'verify' && !o.params) throw new Error('verify needs --params');
	if (!o.maps.length) o.maps = ['tournament desert'];
	if (o.seeds == null) o.seeds = o.cmd === 'tune' ? 4 : 20;
	if (o.seeds % 2 || o.population % 2) throw new Error('--seeds and --population must be even');
	return o;
}

// ---- parameter space: x in [0,1] per parameter ---------------------------------------------------------------------

const clamp01 = (x) => Math.min(1, Math.max(0, x));

function toUnit(p, v) {
	if (p.bool) return v ? 0.75 : 0.25;
	if (p.log) return clamp01((Math.log(v) - Math.log(p.min)) / (Math.log(p.max) - Math.log(p.min)));
	return clamp01((v - p.min) / (p.max - p.min));
}

function fromUnit(p, x) {
	x = clamp01(x);
	if (p.bool) return x >= 0.5;
	let v = p.log ? Math.exp(Math.log(p.min) + x * (Math.log(p.max) - Math.log(p.min))) : p.min + x * (p.max - p.min);
	if (p.int) return Math.round(v);
	return Number(v.toPrecision(3));
}

const valuesOf = (space, x) => Object.fromEntries(space.map((p, i) => [p.name, fromUnit(p, x[i])]));
const engineValue = (p, v) => (p.bool ? (v ? 'Yes' : 'No') : String(v));

// The ExpertSkill block for AIData.ini (only the settings that differ from the code defaults).
function iniBlock(space, values) {
	const lines = ['ExpertSkill'];
	for (const p of space) {
		const v = values[p.name];
		if (v === p.default) continue;
		lines.push(`  ${p.name} = ${p.bool ? (v ? 'Yes' : 'No') : p.percent ? v + '%' : v}`);
	}
	lines.push('End');
	return lines.join('\n') + '\n';
}

// Standard normal numbers from a seeded generator, so a resumed run samples the same candidates.
function rng(seed) {
	let s = seed >>> 0;
	const u = () => { s = (s + 0x6d2b79f5) >>> 0; let t = s; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
	return () => Math.sqrt(-2 * Math.log(u() + 1e-12)) * Math.cos(2 * Math.PI * u());
}

// ---- matches -------------------------------------------------------------------------------------------------------

function run(cmd, args) {
	return new Promise((resolve) => {
		const child = spawn(cmd, args, { stdio: ['ignore', 'pipe', 'pipe'] });
		let out = '';
		child.stdout.on('data', (d) => (out += d));
		child.stderr.on('data', (d) => (out += d));
		child.on('close', (code) => resolve({ code, out }));
	});
}

async function pool(items, n, fn) {
	let next = 0;
	const worker = async () => { for (;;) { const i = next++; if (i >= items.length) return; await fn(items[i], i); } };
	await Promise.all(Array.from({ length: Math.max(1, Math.min(n, items.length)) }, worker));
}

// Score of the Expert in one match: a win 1, a loss 0, a timeout between 0.1 and 0.9 by its share of the army and
// structure value left on the map (money in the bank does not count). `pure` is the score for Elo (timeout = 0.5).
function scoreMatch(result) {
	const players = result.players || [];
	const ex = players.find((p) => p.difficulty === 'expert');
	const hd = players.find((p) => p.difficulty !== 'expert');
	if (!ex || !hd) return null;
	if (ex.outcome === 'won') return { score: 1, pure: 1, outcome: 'won' };
	if (ex.outcome === 'lost') return { score: 0, pure: 0, outcome: 'lost' };
	const mat = (p) => (p.final?.armyValue || 0) + (p.final?.structureValue || 0);
	const total = mat(ex) + mat(hd);
	const share = total > 0 ? mat(ex) / total : 0.5;
	return { score: 0.1 + 0.8 * share, pure: 0.5, outcome: 'timeout', share };
}

class Bench {
	constructor(o) {
		this.o = o;
		this.exe = path.join(o.out, 'zh_headless');
		this.done = 0;
	}

	// Copies the program once (a resumed run keeps its copy) and checks that every map is a 2-player map.
	async prepare() {
		fs.mkdirSync(this.o.out, { recursive: true });
		if (!fs.existsSync(this.exe)) {
			const src = fs.statSync(this.o.native).isDirectory()
				? [path.join(this.o.native, 'zh_headless'), path.join(this.o.native, 'GeneralsMD', 'zh_headless')].find((f) => fs.existsSync(f))
				: this.o.native;
			if (!src) throw new Error(`${this.o.native}: no zh_headless there`);
			fs.copyFileSync(src, this.exe);
			fs.chmodSync(this.exe, 0o755);
		}
		const ud = fs.mkdtempSync(path.join(os.tmpdir(), 'aitune-'));
		for (const map of this.o.maps) {
			const args = ['--zh', this.o.zh, '--userdata', ud];
			if (this.o.generals) args.push('--generals', this.o.generals);
			args.push('-aiMatch', `map=${map}`, 'players=hard:random,hard:random,hard:random', 'seed=1', 'maxframes=2');
			const { out } = await run(this.exe, args);
			if (!/has room for 2 players/.test(out)) {
				const why = (out.match(/AIMATCH_ERROR (.*)/) || [])[1];
				throw new Error(`map "${map}" is not a 2-player map${why ? ': ' + why : ' (it has room for more players)'}`);
			}
		}
		fs.rmSync(ud, { recursive: true, force: true });
	}

	// The matches of one candidate: every side and map, seeds `seedStart...`, half of them from each start.
	jobsFor(tag, values, seedStart) {
		const jobs = [];
		for (const map of this.o.maps)
			for (const side of this.o.sides)
				for (let s = 0; s < this.o.seeds; ++s)
					jobs.push({ tag, values, map, side, seed: seedStart + s, starts: s % 2 ? '2,1' : '1,2' });
		return jobs;
	}

	async play(job, space, dir) {
		const id = `${job.tag}-${job.map.replace(/\W+/g, '_')}-${job.side}-s${job.seed}`;
		const out = path.join(dir, id);
		const file = () => { const m = path.join(out, 'matches'); return fs.existsSync(m) ? fs.readdirSync(m).find((f) => f.endsWith('.json')) : null; };
		if (!file()) {
			const args = [AIBENCH, '--native', this.exe, '--zh', this.o.zh, '--map', job.map,
				'--matchup', `expert:${job.side},hard:${job.side}`, '--seeds', '1', '--seed-start', String(job.seed),
				'--no-rotate', '--starts', job.starts, '--timeout', String(this.o.timeout), '--determinism', '0',
				'--workers', '1', '--out', out];
			if (this.o.generals) args.push('--generals', this.o.generals);
			for (const p of space) if (job.values) args.push('--engine-arg', `skill=${p.name}:${engineValue(p, job.values[p.name])}`);
			const r = await run(process.execPath, args);
			if (!file()) { fs.mkdirSync(out, { recursive: true }); fs.writeFileSync(path.join(out, 'error.log'), r.out); }
		}
		const f = file();
		const rec = f ? JSON.parse(fs.readFileSync(path.join(out, 'matches', f), 'utf8')) : null;
		const s = rec && rec.ok && rec.result ? scoreMatch(rec.result) : null;
		++this.done;
		if (!s) console.log(`  ${id}: no result (${f ? rec.error : 'see ' + path.join(out, 'error.log')})`);
		return s ? { ...s, side: job.side, map: job.map, seed: job.seed } : null;
	}

	// Plays a list of candidates ({ tag, values }) on the same seeds; returns the match scores of each.
	async evaluate(cands, space, dir, seedStart) {
		const jobs = cands.flatMap((c) => this.jobsFor(c.tag, c.values, seedStart));
		const results = Object.fromEntries(cands.map((c) => [c.tag, []]));
		const t0 = Date.now();
		let n = 0;
		await pool(jobs, this.o.workers, async (job) => {
			const r = await this.play(job, space, dir);
			if (r) results[job.tag].push(r);
			if (++n % 10 === 0 || n === jobs.length) console.log(`  ${n}/${jobs.length} matches, ${((Date.now() - t0) / 60000).toFixed(1)} min`);
		});
		return results;
	}
}

// ---- statistics ----------------------------------------------------------------------------------------------------

const mean = (a) => (a.length ? a.reduce((x, y) => x + y, 0) / a.length : NaN);
const eloVsHard = (p) => Math.round(1000 + 400 * Math.log10(Math.min(0.99, Math.max(0.01, p)) / (1 - Math.min(0.99, Math.max(0.01, p)))));

function summary(rs) {
	const count = (k) => rs.filter((r) => r.outcome === k).length;
	const pure = mean(rs.map((r) => r.pure));
	return { games: rs.length, score: mean(rs.map((r) => r.score)), pure, elo: eloVsHard(pure), won: count('won'), lost: count('lost'), timeout: count('timeout') };
}

const fmt = (s) => `score ${s.score.toFixed(3)}, ${s.won}W ${s.lost}L ${s.timeout}T of ${s.games}, Elo vs Hard ${s.elo}`;

function bySide(rs, sides) {
	return sides.map((side) => { const s = summary(rs.filter((r) => r.side === side)); return `${side} ${s.score.toFixed(2)} (${s.won}W ${s.lost}L ${s.timeout}T)`; }).join(', ');
}

// ---- tune ----------------------------------------------------------------------------------------------------------

async function tune(o, space) {
	const bench = new Bench(o);
	await bench.prepare();
	const stateFile = path.join(o.out, 'state.json');
	let st = fs.existsSync(stateFile) ? JSON.parse(fs.readFileSync(stateFile, 'utf8')) : null;
	if (st && st.space.join() !== space.map((p) => p.name).join()) throw new Error(`${stateFile}: made with another parameter space`);
	if (!st) {
		st = { space: space.map((p) => p.name), options: { maps: o.maps, sides: o.sides, seeds: o.seeds, population: o.population, timeout: o.timeout },
			mean: space.map((p) => toUnit(p, p.default)), sigma: o.sigma, generation: 0, history: [] };
		console.log(`aitune: ${space.length} settings, ${o.population} candidates + mean per generation, ${o.sides.length * o.maps.length * o.seeds} matches each, ${o.workers} at a time`);
	} else console.log(`aitune: continuing at generation ${st.generation + 1}, sigma ${st.sigma.toFixed(3)}`);

	const weightsRaw = Array.from({ length: o.population / 2 }, (_, i) => Math.log(o.population / 2 + 0.5) - Math.log(i + 1));
	const weights = weightsRaw.map((w) => w / weightsRaw.reduce((a, b) => a + b, 0));

	while (st.generation < o.generations) {
		const g = st.generation + 1;
		const normal = rng(0x5eed + g * 7919);
		const cands = [{ tag: `g${g}-mean`, x: st.mean }];
		for (let k = 0; k < o.population / 2; ++k) {
			const z = space.map(() => normal());
			cands.push({ tag: `g${g}-c${2 * k + 1}`, x: st.mean.map((m, i) => clamp01(m + st.sigma * z[i])) });
			cands.push({ tag: `g${g}-c${2 * k + 2}`, x: st.mean.map((m, i) => clamp01(m - st.sigma * z[i])) });
		}
		for (const c of cands) c.values = valuesOf(space, c.x);
		const seedStart = 1000 * g;
		console.log(`\n== generation ${g}/${o.generations}: sigma ${st.sigma.toFixed(3)}, seeds ${seedStart}-${seedStart + o.seeds - 1}`);
		const results = await bench.evaluate(cands, space, path.join(o.out, 'matches'), seedStart);

		const rows = cands.map((c) => ({ ...c, sum: summary(results[c.tag]) })).filter((c) => c.sum.games > 0);
		for (const c of rows) console.log(`  ${c.tag.padEnd(10)} ${fmt(c.sum)}  [${bySide(results[c.tag], o.sides)}]`);
		const ranked = rows.filter((c) => !c.tag.endsWith('-mean')).sort((a, b) => b.sum.score - a.sum.score);
		if (ranked.length >= weights.length) {
			st.mean = space.map((_, i) => ranked.slice(0, weights.length).reduce((acc, c, r) => acc + weights[r] * c.x[i], 0));
		} else console.log('  too few results: the mean stays');
		const meanRow = rows.find((c) => c.tag.endsWith('-mean'));
		st.history.push({ generation: g, sigma: st.sigma, seedStart, mean: meanRow ? { values: meanRow.values, ...meanRow.sum } : null,
			best: ranked[0] ? { tag: ranked[0].tag, values: ranked[0].values, ...ranked[0].sum } : null });
		st.sigma = Math.max(o.sigmaMin, st.sigma * o.sigmaDecay);
		st.generation = g;
		fs.writeFileSync(stateFile, JSON.stringify(st, null, 1));
		writeBest(o, space, st);
	}
	console.log(`\ndone. Next: verify the result on fresh seeds:\n  node scripts/aitune/aitune.mjs verify --native ${o.native} --out ${o.out}/verify --params ${path.join(o.out, 'best.json')}`);
}

// The recommendation is the current mean of the search (it is what the search converges to; the single best
// candidate of a generation is lucky more often than good).
function writeBest(o, space, st) {
	const values = valuesOf(space, st.mean);
	fs.writeFileSync(path.join(o.out, 'best.json'), JSON.stringify({ generation: st.generation, values }, null, 1));
	fs.writeFileSync(path.join(o.out, 'best.ini'), iniBlock(space, values));
	const L = ['# aitune progress', '', `Maps: ${o.maps.join(', ')}. Sides: ${o.sides.join(', ')} (Expert against Hard of the same side). ` +
		`Score: win 1, loss 0, timeout 0.1-0.9 by the share of army and structure value left. Elo against Hard counts a timeout as half a game.`, '',
		'| gen | sigma | mean: score | W/L/T | Elo vs Hard | best candidate: score | Elo |', '|---|---|---|---|---|---|---|'];
	for (const h of st.history) {
		const m = h.mean, b = h.best;
		L.push(`| ${h.generation} | ${h.sigma.toFixed(3)} | ${m ? m.score.toFixed(3) : '-'} | ${m ? `${m.won}/${m.lost}/${m.timeout}` : '-'} | ${m ? m.elo : '-'} | ${b ? b.score.toFixed(3) : '-'} | ${b ? b.elo : '-'} |`);
	}
	L.push('', '## Current mean (the recommendation)', '', '```', iniBlock(space, values).trimEnd(), '```', '');
	fs.writeFileSync(path.join(o.out, 'progress.md'), L.join('\n'));
}

// ---- verify --------------------------------------------------------------------------------------------------------

async function verify(o, space) {
	const bench = new Bench(o);
	await bench.prepare();
	const p = JSON.parse(fs.readFileSync(o.params, 'utf8'));
	const tuned = p.values || (p.mean && valuesOf(space, p.mean));
	if (!tuned) throw new Error(`${o.params}: no values (give best.json or state.json of a tune run)`);
	const defaults = Object.fromEntries(space.map((q) => [q.name, q.default]));
	const seedStart = 900000;
	console.log(`verify: tuned and default settings, ${o.sides.length * o.maps.length * o.seeds} matches each, seeds ${seedStart}-${seedStart + o.seeds - 1}`);
	const results = await bench.evaluate([{ tag: 'tuned', values: tuned }, { tag: 'default', values: defaults }], space, path.join(o.out, 'matches'), seedStart);
	const L = ['# aitune verify', '', `Maps: ${o.maps.join(', ')}; ${o.seeds} seeds per side (half from each start), fresh seeds ${seedStart}+.`, ''];
	for (const tag of ['tuned', 'default']) {
		const s = summary(results[tag]);
		const line = `${tag}: ${fmt(s)}  [${bySide(results[tag], o.sides)}]`;
		console.log(line);
		L.push(`- ${line}`);
	}
	L.push('', 'Settings:', '', '```', iniBlock(space, tuned).trimEnd(), '```', '');
	fs.writeFileSync(path.join(o.out, 'verify.md'), L.join('\n'));
	console.log(`report: ${path.join(o.out, 'verify.md')}`);
}

async function main() {
	const o = parseArgs(process.argv.slice(2));
	const space = JSON.parse(fs.readFileSync(o.space, 'utf8')).params;
	if (o.cmd === 'tune') await tune(o, space);
	else await verify(o, space);
}

main().catch((e) => { console.error(e.message || e); process.exit(1); });
