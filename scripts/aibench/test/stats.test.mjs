// Tests of the statistics without a game: node --test scripts/aibench/test/stats.test.mjs
import test from 'node:test';
import assert from 'node:assert/strict';
import { wilson, elo, winMatrix, headToHead, compareRuns } from '../lib/stats.mjs';
import { buildReport, renderMarkdown, summaryText } from '../lib/report.mjs';

const player = (label, outcome, slot, extra = {}) => ({
	slot, outcome, team: -1, startPos: slot + 1, defeatedFrame: outcome === 'lost' ? 3000 : null,
	money: { gathered: 5000, spent: 4000 }, peak: { armyValue: 2000 }, final: { armyValue: 1000 },
	production: { idleFraction: 0.1 }, totals: { unitsBuilt: 10, structuresBuilt: 4, unitsLost: 3, objectsKilled: 5 }, ...extra,
});
function match(id, a, b, winner, outcome = 'victory', frames = 9000) {
	const players = [player(a, winner === 0 ? 'won' : winner === 1 ? 'lost' : outcome, 0), player(b, winner === 1 ? 'won' : winner === 0 ? 'lost' : outcome, 1)];
	const dummy = { frames: [0, 300], crc: ['AAAA0001', 'BBBB0002'] };
	return {
		id, build: 'x', kind: 'main', ok: true, wallMs: 1000,
		job: { players: [{ label: a }, { label: b }], seed: 1 },
		result: { result: { outcome, winners: winner === null ? [] : [winner], frames, finalCRC: 'BBBB0002' }, players, crcTimeline: dummy, timeline: {}, perf: { logicFps: 900, setupMs: 100 } },
	};
}

test('wilson interval', () => {
	const c = wilson(16, 20);
	assert.ok(Math.abs(c.p - 0.8) < 1e-9);
	assert.ok(c.lo > 0.58 && c.lo < 0.60, c.lo);
	assert.ok(c.hi > 0.91 && c.hi < 0.93, c.hi);
	assert.equal(wilson(0, 0).hi, 1);
});

test('timeouts are not wins; mixed difficulties are separate configurations', () => {
	const recs = [];
	for (let i = 0; i < 8; ++i) recs.push(match('a' + i, 'expert:Ironwood', 'hard:Ironwood', 0));
	recs.push(match('t1', 'expert:Ironwood', 'hard:Ironwood', null, 'timeout'));
	recs.push(match('l1', 'expert:Ironwood', 'hard:Ironwood', 1));
	const [g] = headToHead(recs);
	assert.equal(g.x, 'expert:Ironwood');
	assert.equal(g.xWins, 8); assert.equal(g.yWins, 1); assert.equal(g.timeouts, 1); assert.equal(g.played, 10);
	const m = winMatrix(recs);
	assert.equal(m['expert:Ironwood']['hard:Ironwood'].wins, 8);
	assert.equal(m['expert:Ironwood']['hard:Ironwood'].timeouts, 1);
	const e = elo(recs);
	assert.ok(e['expert:Ironwood'].elo > e['hard:Ironwood'].elo + 100);
});

test('elo of a perfect record stays finite', () => {
	const recs = [match('a', 'a:X', 'b:X', 0), match('b', 'a:X', 'b:X', 0)];
	const e = elo(recs);
	assert.ok(Number.isFinite(e['a:X'].elo) && e['a:X'].elo > e['b:X'].elo);
});

test('determinism comparison reports the divergence', () => {
	const a = match('a', 'a:X', 'b:X', 0), b = match('a', 'a:X', 'b:X', 0);
	assert.ok(compareRuns(a, b).identical);
	b.result.crcTimeline.crc[1] = 'CCCC0003';
	const c = compareRuns(a, b);
	assert.equal(c.identical, false);
	assert.equal(c.divergence.atFrame, 300); assert.equal(c.divergence.lastGoodFrame, 0);
});

test('report and target', () => {
	const recs = [];
	for (let i = 0; i < 10; ++i) recs.push(match('a' + i, 'expert:Ironwood', 'hard:Ironwood', i < 8 ? 0 : 1));
	const options = { builds: [{ name: 'x', dir: '/x' }], data: 'starter', maps: ['m'], matchups: ['e,h'], seeds: 10, seedStart: 1, minutes: 20, workers: 1, rotate: true };
	const report = buildReport({ options, builds: ['x'], records: recs, determinism: [], targets: ['expert:Ironwood>hard:Ironwood=0.8'] });
	const text = summaryText(report);
	assert.match(text, /expert:Ironwood 8\/10 wins \(80%, 95% CI 49%-94%\)/);
	assert.match(text, /target expert:ironwood > hard:ironwood = 80%/);
	assert.match(renderMarkdown(report), /Head to head/);
});
