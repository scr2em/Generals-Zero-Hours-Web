// Aggregation of match results: win-rate matrix, Elo, confidence intervals, determinism comparison.
// Pure functions on plain objects, so they can be tested without a game (test/stats.test.mjs).

// A match record: { id, build, kind, job: { map, seed, players: [{ label, difficulty, side, variant, team }] }, ok, error, result }
// where result is the engine's AIMATCH_RESULT object.

export function wilson(wins, n, z = 1.96) {
	if (n <= 0) return { p: 0, lo: 0, hi: 1 };
	const p = wins / n;
	const d = 1 + z * z / n;
	const centre = (p + z * z / (2 * n)) / d;
	const half = (z * Math.sqrt(p * (1 - p) / n + z * z / (4 * n * n))) / d;
	return { p, lo: Math.max(0, centre - half), hi: Math.min(1, centre + half) };
}

export const pct = (x) => (100 * x).toFixed(0) + '%';
export const mean = (xs) => (xs.length ? xs.reduce((a, b) => a + b, 0) / xs.length : 0);
export function median(xs) {
	if (!xs.length) return 0;
	const s = [...xs].sort((a, b) => a - b);
	const m = s.length >> 1;
	return s.length % 2 ? s[m] : (s[m - 1] + s[m]) / 2;
}

// The label of a configuration: what plays, independent of where it starts.
export function configLabel(p) {
	let l = `${p.difficulty}:${p.side}`;
	if (p.variant && p.variant !== p.difficulty) l += `:${p.variant}`;
	return l;
}

// Pairs of opposing players of one match with the score of the first one: 1 victory, 0 defeat, 0.5 for a timeout or a
// draw. `kind` says which. A match that is not a real victory never gives a win or a loss.
export function pairings(rec) {
	const out = [];
	if (!rec.ok) return out;
	const players = rec.result.players;
	for (let i = 0; i < players.length; ++i) {
		for (let j = i + 1; j < players.length; ++j) {
			const a = players[i], b = players[j];
			if (a.team >= 0 && a.team === b.team) continue;   // allies do not play each other
			let score, kind;
			if (a.outcome === 'won' && b.outcome === 'lost') { score = 1; kind = 'win'; }
			else if (a.outcome === 'lost' && b.outcome === 'won') { score = 0; kind = 'win'; }
			else if (a.outcome === 'timeout') { score = 0.5; kind = 'timeout'; }
			else { score = 0.5; kind = 'draw'; }
			out.push({ a: rec.job.players[i].label, b: rec.job.players[j].label, ai: i, bi: j, score, kind, rec });
		}
	}
	return out;
}

// matrix[a][b] = { wins, losses, timeouts, draws, n } from a's point of view, over all pairings.
export function winMatrix(records) {
	const m = {};
	const cell = (a, b) => ((m[a] ||= {})[b] ||= { wins: 0, losses: 0, timeouts: 0, draws: 0, n: 0 });
	for (const rec of records) {
		for (const p of pairings(rec)) {
			const x = cell(p.a, p.b), y = cell(p.b, p.a);
			x.n++; y.n++;
			if (p.kind === 'win') { if (p.score === 1) { x.wins++; y.losses++; } else { x.losses++; y.wins++; } }
			else if (p.kind === 'timeout') { x.timeouts++; y.timeouts++; }
			else { x.draws++; y.draws++; }
		}
	}
	return m;
}

// Elo by maximum likelihood (Bradley-Terry), so the order of the games does not matter. A draw is half a win each.
// A phantom opponent rated `anchor` that every configuration drew once against keeps ratings finite when a
// configuration has won (or lost) everything.
export function elo(records, anchor = 1000) {
	const wins = new Map(), games = new Map();
	const labels = new Set();
	const pairs = [];
	for (const rec of records) for (const p of pairings(rec)) { if (p.a === p.b) continue; pairs.push(p); labels.add(p.a); labels.add(p.b); }
	if (!labels.size) return {};
	const key = (a, b) => (a < b ? a + '\u0000' + b : b + '\u0000' + a);
	const n = new Map();
	for (const l of labels) { wins.set(l, 0.5); }
	for (const p of pairs) {
		wins.set(p.a, wins.get(p.a) + p.score);
		wins.set(p.b, wins.get(p.b) + 1 - p.score);
		n.set(key(p.a, p.b), (n.get(key(p.a, p.b)) || 0) + 1);
		games.set(p.a, (games.get(p.a) || 0) + 1);
		games.set(p.b, (games.get(p.b) || 0) + 1);
	}
	const strength = new Map([...labels].map((l) => [l, 1]));
	for (let it = 0; it < 2000; ++it) {
		let change = 0;
		const next = new Map();
		for (const l of labels) {
			let denom = 1 / (strength.get(l) + 1);   // the phantom, strength 1
			for (const o of labels) {
				if (o === l) continue;
				const c = n.get(key(l, o));
				if (c) denom += c / (strength.get(l) + strength.get(o));
			}
			const s = wins.get(l) / denom;
			next.set(l, s);
			change = Math.max(change, Math.abs(Math.log(s / strength.get(l))));
		}
		for (const [l, s] of next) strength.set(l, s);
		if (change < 1e-9) break;
	}
	const out = {};
	for (const l of labels) out[l] = { elo: Math.round(anchor + 400 * Math.log10(strength.get(l))), games: games.get(l) || 0 };
	return out;
}

// Headline numbers for every matchup between two different configurations: wins of the first one.
// A matchup is identified by the sorted list of configurations in it.
export function headToHead(records) {
	const groups = new Map();
	for (const rec of records) {
		const labels = rec.job.players.map((p) => p.label);
		const distinct = [...new Set(labels)];
		if (distinct.length !== 2 || labels.length !== 2) continue;   // 1v1 between two different configurations
		const k = [...distinct].sort().join(' vs ');
		const [x, y] = groups.has(k) ? [groups.get(k).x, groups.get(k).y] : labels;   // in the order the matchup was written
		if (!groups.has(k)) groups.set(k, { x, y, played: 0, xWins: 0, yWins: 0, timeouts: 0, draws: 0, errors: 0, frames: [] });
		const g = groups.get(k);
		if (!rec.ok) { g.errors++; continue; }
		g.played++;
		const r = rec.result.result;
		if (r.outcome === 'timeout') g.timeouts++;
		else if (r.outcome === 'draw') g.draws++;
		else {
			const winner = rec.job.players[r.winners[0]].label;
			if (winner === x) g.xWins++; else g.yWins++;
		}
		g.frames.push(r.frames);
	}
	return [...groups.values()].map((g) => ({ ...g, xCI: wilson(g.xWins, g.played), yCI: wilson(g.yWins, g.played), medianFrames: median(g.frames) }));
}

// Win rate of the first/second start position over decided 1v1 matches of one configuration against itself or not:
// a map or engine bias shows here. Returns { n, firstWins, secondWins }.
export function startBias(records) {
	const out = { n: 0, byStart: {} };
	for (const rec of records) {
		if (!rec.ok || rec.result.result.outcome !== 'victory') continue;
		for (const p of rec.result.players) {
			const s = String(p.startPos);
			out.byStart[s] ||= { games: 0, wins: 0 };
			out.byStart[s].games++;
			if (p.outcome === 'won') out.byStart[s].wins++;
		}
		out.n++;
	}
	return out;
}

// Mean of per-player numbers for each configuration over the matches of a record list.
export function configProfiles(records) {
	const acc = new Map();
	for (const rec of records) {
		if (!rec.ok) continue;
		rec.result.players.forEach((p, i) => {
			const l = rec.job.players[i].label;
			if (!acc.has(l)) acc.set(l, { games: 0, gathered: [], spent: [], peakArmy: [], finalArmy: [], idle: [], unitsBuilt: [], structuresBuilt: [], unitsLost: [], killed: [], survivedFrames: [] });
			const a = acc.get(l);
			a.games++;
			a.gathered.push(p.money.gathered); a.spent.push(p.money.spent);
			a.peakArmy.push(p.peak.armyValue); a.finalArmy.push(p.final.armyValue);
			a.idle.push(p.production.idleFraction);
			a.unitsBuilt.push(p.totals.unitsBuilt); a.structuresBuilt.push(p.totals.structuresBuilt);
			a.unitsLost.push(p.totals.unitsLost); a.killed.push(p.totals.objectsKilled);
			a.survivedFrames.push(p.defeatedFrame == null ? rec.result.result.frames : p.defeatedFrame);
		});
	}
	const out = {};
	for (const [l, a] of acc) {
		out[l] = {
			games: a.games, moneyGathered: mean(a.gathered), moneySpent: mean(a.spent), peakArmyValue: mean(a.peakArmy),
			finalArmyValue: mean(a.finalArmy), idleFraction: mean(a.idle), unitsBuilt: mean(a.unitsBuilt),
			structuresBuilt: mean(a.structuresBuilt), unitsLost: mean(a.unitsLost), objectsKilled: mean(a.killed),
			survivedFrames: mean(a.survivedFrames),
		};
	}
	return out;
}

// Speed: logic frames per second over the matches (wall time of the simulation only, without start-up).
export function speed(records) {
	const fps = [], setup = [], wall = [], frames = [];
	for (const rec of records) {
		if (!rec.ok) continue;
		fps.push(rec.result.perf.logicFps); setup.push(rec.result.perf.loadMs); wall.push(rec.wallMs); frames.push(rec.result.result.frames);
	}
	return {
		matches: fps.length, logicFpsMean: mean(fps), logicFpsMedian: median(fps), logicFpsMin: fps.length ? Math.min(...fps) : 0,
		setupMsMean: mean(setup), wallMsMean: mean(wall), framesMean: mean(frames),
	};
}

// Compares two runs of the same match. Returns { identical, divergence }:
// divergence = { kind, atFrame, lastGoodFrame, a, b } where the game states first differed between lastGoodFrame
// (exclusive) and atFrame (inclusive).
export function compareRuns(a, b) {
	if (!a.ok || !b.ok) return { identical: false, divergence: { kind: 'run-failed', detail: !a.ok ? a.error : b.error } };
	const ca = a.result.crcTimeline, cb = b.result.crcTimeline;
	const n = Math.min(ca.crc.length, cb.crc.length);
	for (let i = 0; i < n; ++i) {
		if (ca.frames[i] !== cb.frames[i]) return { identical: false, divergence: { kind: 'frames', atFrame: Math.min(ca.frames[i], cb.frames[i]), lastGoodFrame: i ? ca.frames[i - 1] : 0, a: ca.frames[i], b: cb.frames[i] } };
		if (ca.crc[i] !== cb.crc[i]) return { identical: false, divergence: { kind: 'crc', atFrame: ca.frames[i], lastGoodFrame: i ? ca.frames[i - 1] : 0, a: ca.crc[i], b: cb.crc[i] } };
	}
	if (ca.crc.length !== cb.crc.length || a.result.result.frames !== b.result.result.frames) {
		return { identical: false, divergence: { kind: 'length', atFrame: n ? ca.frames[n - 1] : 0, lastGoodFrame: n ? ca.frames[n - 1] : 0, a: a.result.result.frames, b: b.result.result.frames } };
	}
	if (a.result.result.finalCRC !== b.result.result.finalCRC) {
		return { identical: false, divergence: { kind: 'finalCRC', atFrame: a.result.result.frames, lastGoodFrame: a.result.result.frames, a: a.result.result.finalCRC, b: b.result.result.finalCRC } };
	}
	// The game states agree; the statistics must, too (they only read the state).
	const strip = (r) => JSON.stringify({ result: r.result, players: r.players, timeline: r.timeline });
	if (strip(a.result) !== strip(b.result)) return { identical: false, divergence: { kind: 'statistics', atFrame: a.result.result.frames, lastGoodFrame: a.result.result.frames } };
	return { identical: true, divergence: null };
}
