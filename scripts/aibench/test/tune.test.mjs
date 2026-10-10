// Tests of the tuner's search without a game: node --test scripts/aibench/test/tune.test.mjs
// A fake engine decides the matches from the seed and the aiini file, so the search, its rule, the final check, the
// report and the resume can be checked in a second.
import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { parseArgs, loadParams, iniText, configKey, Tuner } from '../tune.mjs';
import crypto from 'node:crypto';

// The game's values of the fake engine.
const GAME = { 'ExpertSkill.WaveSizeScale': '1', 'ExpertSkill.MinWaveValue': '2000' };

// Wins with a chance that depends on the settings: WaveSizeScale 0.8 is much better, FocusFire = No much worse,
// MinWaveValue 3000 better over both sides but much worse on one of them.
class FakeEngine {
	constructor() { this.calls = 0; }
	async runMatch(build, args) {
		++this.calls;
		const arg = (k) => (args.find((a) => a.startsWith(k + '=')) || '').slice(k.length + 1);
		const ini = arg('aiini') ? fs.readFileSync(arg('aiini'), 'utf8') : '';
		const get = (name) => (new RegExp(`^\\s*${name} = (\\S+)`, 'm').exec(ini) || [])[1];
		if (arg('maxframes')) {
			// the probe: report the change of every field in the file
			const log = [];
			for (const [k, v] of Object.entries(GAME)) if (get(k.split('.')[1]) !== undefined) log.push(`AIMATCH aiini ${k}: ${v} -> ${get(k.split('.')[1])}`);
			if (get('FocusFire') === 'Yes') { /* already Yes in the game: no line */ }
			return { ok: true, result: { result: { outcome: 'timeout', winners: [], frames: 2 } }, log, wallMs: 5 };
		}
		const side = arg('players').split(',')[0].split(':')[1];
		const seed = Number(arg('seed'));
		let p = 0.5;
		if (get('WaveSizeScale') === '0.8') p += 0.3;
		if (get('FocusFire') === 'No') p -= 0.3;
		if (get('MinWaveValue') === '3000') p += side === 'China' ? 0.4 : -0.2;
		const u = parseInt(crypto.createHash('sha1').update(`${side}/${seed}`).digest('hex').slice(0, 8), 16) / 0x100000000;
		const expertStart = Number(arg('players').split(',')[0].split(':')[3]);
		const outcome = u < p ? 'won' : u > 0.95 ? 'timeout' : 'lost';
		const winners = outcome === 'timeout' ? [] : [outcome === 'won' ? 0 : 1];
		return {
			ok: true, wallMs: 5, log: [`AIMATCH start ${expertStart}`, 'AISTRAT[p2 f30] WAVE launches'],
			result: { result: { outcome: outcome === 'timeout' ? 'timeout' : 'victory', winners, frames: 30000 } },
		};
	}
}

function setup(out, extra = []) {
	const params = path.join(out, 'params.json');
	fs.writeFileSync(params, JSON.stringify([
		{ name: 'MinWaveValue', type: 'real', default: 2000, values: [2000, 3000] },
		{ name: 'WaveSizeScale', type: 'real', default: 1, values: [0.8, 1, 1.2] },
		{ name: 'FocusFire', type: 'bool', default: true, values: [true, false] },
	]));
	const o = parseArgs(['--native', 'x', '--data', 'starter', '--sides', 'America,China', '--map', 'm', '--params', params,
		'--seeds', '12', '--confirm', '40', '--holdout', '40', '--out', out, ...extra]);
	o.out = out;
	const t = new Tuner(o, loadParams(params));
	t.log = () => {};
	t.fingerprint = { test: 1 };
	t.runner = new FakeEngine();
	return t;
}

async function run(t) {
	t.loadCache();
	await t.readCurrentValues();
	await t.search();
	await t.finalCheck();
	t.writeReport();
	t.saveState('done', t.best);
}

test('ini text and config keys', () => {
	assert.equal(configKey({}), 'defaults');
	assert.equal(configKey({ 'ExpertSkill.A': 1, 'ExpertSkill.B': true }), configKey({ 'ExpertSkill.B': true, 'ExpertSkill.A': 1 }));
	assert.equal(iniText({ 'ExpertSkill.FocusFire': false, 'AIData.Wealthy': 7, 'ExpertSkill.WaveSizeScale': 0.85 }),
		'AIData\n  Wealthy = 7\n  ExpertSkill\n    FocusFire = No\n    WaveSizeScale = 0.85\n  End\nEnd\n');
});

test('seed ranges must not overlap', () => {
	assert.throws(() => parseArgs(['--native', 'x', '--data', 'starter', '--seeds', '2000']), /overlap/);
});

test('the search keeps what helps, rejects what hurts a side, and resumes from its cache', async () => {
	const out = fs.mkdtempSync(path.join(os.tmpdir(), 'tune-test-'));
	const t = setup(out);
	await run(t);
	assert.deepEqual(t.current, { 'ExpertSkill.WaveSizeScale': 1, 'ExpertSkill.MinWaveValue': 2000, 'ExpertSkill.FocusFire': true });
	assert.deepEqual(t.best, { 'ExpertSkill.WaveSizeScale': 0.8 });
	const decisions = t.history.map((h) => `${h.setting.split('.')[1]}=${h.value}: ${h.decision.split(':')[0]}`);
	assert.ok(decisions.includes('WaveSizeScale=0.8: ACCEPTED'), decisions.join('\n'));
	assert.ok(!decisions.some((d) => d.startsWith('FocusFire=false: ACCEPTED')), decisions.join('\n'));
	assert.ok(t.history.some((h) => h.setting === 'ExpertSkill.MinWaveValue' && /^rejected: America drops/.test(h.decision)), decisions.join('\n'));
	assert.ok(t.holdout.best.all.wins > t.holdout.baseline.all.wins);
	const summary = fs.readFileSync(path.join(out, 'summary.md'), 'utf8');
	assert.match(summary, /Result on the fresh seeds/);
	assert.match(summary, /\| ExpertSkill.WaveSizeScale \| 1 \| \*\*0.8\*\* \|/);
	assert.match(fs.readFileSync(path.join(out, 'best.ini'), 'utf8'), /^    WaveSizeScale = 0\.8$/m);
	const results = JSON.parse(fs.readFileSync(path.join(out, 'results.json'), 'utf8'));
	assert.equal(results.best.key, configKey({ 'ExpertSkill.WaveSizeScale': 0.8 }));
	// losses and timeouts of the best settings on the fresh seeds keep their trace
	const logs = fs.readdirSync(path.join(out, 'holdout'));
	assert.equal(logs.length, t.holdout.best.all.played - t.holdout.best.all.wins);
	assert.ok(logs.every((f) => f.startsWith(results.best.key + '-') && f.endsWith('-trace.trace.txt')));
	// seeds never mix: the final check uses its own
	const lines = fs.readFileSync(path.join(out, 'matches.jsonl'), 'utf8').trim().split('\n').map((l) => JSON.parse(l));
	assert.ok(lines.filter((r) => r.variant === 'trace').every((r) => r.seed >= 100001));
	assert.ok(lines.filter((r) => r.variant !== 'trace').every((r) => r.seed < 100001));

	// The same command again: everything comes from the cache (only the probe runs), the same result.
	const again = setup(out);
	await run(again);
	assert.equal(again.runner.calls, 1);
	assert.deepEqual(again.best, t.best);

	// A run cut short: the cache of the first two thirds, then the rest.
	const out2 = fs.mkdtempSync(path.join(os.tmpdir(), 'tune-test-'));
	fs.writeFileSync(path.join(out2, 'matches.jsonl'), lines.slice(0, Math.floor(lines.length * 2 / 3)).map((l) => JSON.stringify(l)).join('\n') + '\n{"id":"cut');
	fs.copyFileSync(path.join(out, 'params.json'), path.join(out2, 'params.json'));
	const resumed = setup(out2);
	await run(resumed);
	assert.deepEqual(resumed.best, t.best);
	assert.ok(resumed.runner.calls > 1 && resumed.runner.calls < t.runner.calls);
});

test('the time budget ends the search in time for the final check', async () => {
	const out = fs.mkdtempSync(path.join(os.tmpdir(), 'tune-test-'));
	const t = setup(out, ['--budget', '0']);
	await run(t);
	assert.equal(t.stopReason, 'the time budget');
	assert.deepEqual(t.best, {});
	assert.equal(t.holdout.best.all.played, 80);
});
