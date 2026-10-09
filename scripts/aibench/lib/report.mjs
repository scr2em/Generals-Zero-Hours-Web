// Builds the JSON report from the match records and renders it as Markdown.
import { wilson, pct, elo, winMatrix, headToHead, startBias, configProfiles, speed, median, mean } from './stats.mjs';

const ci = (c) => `${pct(c.p)}, 95% CI ${pct(c.lo)}-${pct(c.hi)}`;

// "A>B=0.8"
function parseTarget(t) {
	const m = /^(.+?)>(.+?)=([0-9.]+)$/.exec(t.trim());
	if (!m) throw new Error(`bad --target "${t}" (A>B=P)`);
	return { a: m[1].trim().toLowerCase(), b: m[2].trim().toLowerCase(), p: Number(m[3]) };
}

function evaluateTarget(t, groups) {
	for (const g of groups) {
		const forward = g.x.toLowerCase() === t.a && g.y.toLowerCase() === t.b;
		const backward = g.x.toLowerCase() === t.b && g.y.toLowerCase() === t.a;
		if (!forward && !backward) continue;
		const wins = forward ? g.xWins : g.yWins;
		const c = wilson(wins, g.played);
		let verdict;
		if (c.p >= t.p) verdict = c.lo > t.p ? 'above the target (the whole interval is above it)' : 'at or above the target';
		else verdict = c.hi < t.p ? 'BELOW the target (the whole interval is below it)' : 'below the target, but the target is inside the interval (not significant)';
		return { target: t, found: true, wins, played: g.played, ci: c, verdict };
	}
	return { target: t, found: false };
}

export function buildReport({ options, builds, records, determinism, targets }) {
	const main = records.filter((r) => r.kind === 'main');
	const perBuild = {};
	for (const b of builds) {
		const recs = main.filter((r) => r.build === b);
		const groups = headToHead(recs);
		const outcomes = { victory: 0, timeout: 0, draw: 0, error: 0 };
		for (const r of recs) { if (!r.ok) outcomes.error++; else outcomes[r.result.result.outcome]++; }
		perBuild[b] = {
			matches: recs.length, outcomes,
			headToHead: groups,
			targets: targets.map(parseTarget).map((t) => evaluateTarget(t, groups)),
			matrix: winMatrix(recs),
			elo: elo(recs),
			profiles: configProfiles(recs),
			speed: speed(recs),
			startBias: startBias(recs),
			errors: recs.filter((r) => !r.ok).map((r) => ({ id: r.id, error: r.error, attempts: r.attempts })),
		};
	}
	const report = {
		schema: 'zh-aibench-report-1',
		generated: new Date().toISOString(),
		options: { builds: options.builds.map((b) => ({ name: b.name, dir: b.dir })), data: options.data === 'starter' ? 'starter' : 'own', maps: options.maps, matchups: options.matchups, seeds: options.seeds, seedStart: options.seedStart, minutes: options.minutes, workers: options.workers, rotate: options.rotate },
		builds: perBuild,
		determinism: { checked: determinism.length, identical: determinism.filter((d) => d.identical).length, runs: determinism },
	};
	if (builds.length >= 2) report.comparison = compareBuilds(builds[0], builds[1], perBuild);
	return report;
}

// Baseline (first) against candidate (second): the same matchups side by side.
function compareBuilds(a, b, perBuild) {
	const rows = [];
	const keyOf = (g) => [g.x, g.y].sort().join(' vs ');
	for (const ga of perBuild[a].headToHead) {
		const gb = perBuild[b].headToHead.find((g) => keyOf(g) === keyOf(ga));
		if (!gb) continue;
		// Express both from the point of view of ga.x.
		const side = (g) => (g.x === ga.x ? { wins: g.xWins, other: g.yWins } : { wins: g.yWins, other: g.xWins });
		rows.push({ matchup: `${ga.x} vs ${ga.y}`, first: ga.x, baseline: { played: ga.played, wins: side(ga).wins, timeouts: ga.timeouts, medianFrames: ga.medianFrames }, candidate: { played: gb.played, wins: side(gb).wins, timeouts: gb.timeouts, medianFrames: gb.medianFrames } });
	}
	const deltas = [];
	for (const label of Object.keys(perBuild[a].profiles)) {
		const pa = perBuild[a].profiles[label], pb = perBuild[b].profiles[label];
		if (!pb) continue;
		deltas.push({ config: label, baseline: pa, candidate: pb });
	}
	return { baseline: a, candidate: b, matchups: rows, profiles: deltas, speed: { baseline: perBuild[a].speed, candidate: perBuild[b].speed } };
}

// The few lines that matter, for the terminal and the top of the Markdown.
export function summaryText(report) {
	const out = [];
	for (const [b, d] of Object.entries(report.builds)) {
		out.push(`== ${b}: ${d.matches} matches: ${d.outcomes.victory} victories, ${d.outcomes.timeout} timeouts, ${d.outcomes.draw} draws, ${d.outcomes.error} errors`);
		for (const g of d.headToHead) {
			out.push(`   ${g.x} vs ${g.y}: ${g.x} ${g.xWins}/${g.played} wins (${ci(g.xCI)}), ${g.y} ${g.yWins}/${g.played} (${ci(g.yCI)}), ${g.timeouts} timeouts, ${g.draws} draws` + (g.errors ? `, ${g.errors} errors not counted` : ''));
		}
		for (const t of d.targets) {
			out.push(t.found ? `   target ${t.target.a} > ${t.target.b} = ${pct(t.target.p)}: ${t.wins}/${t.played} (${ci(t.ci)}): ${t.verdict}` : `   target ${t.target.a} > ${t.target.b}: no such matchup was played`);
		}
		const e = Object.entries(d.elo).sort((x, y) => y[1].elo - x[1].elo);
		if (e.length) out.push('   Elo: ' + e.map(([l, v]) => `${l} ${v.elo}`).join(', '));
		out.push(`   speed: ${Math.round(d.speed.logicFpsMean)} logic frames/s on average (${Math.round(d.speed.logicFpsMin)} at the slowest; real time is 30)`);
	}
	const det = report.determinism;
	out.push(det.checked ? `== determinism: ${det.identical}/${det.checked} replayed matches identical` + (det.identical === det.checked ? '' : ' - DIVERGENCE: ' + det.runs.filter((r) => !r.identical).map((r) => `${r.id} (${r.divergence.kind}` + (r.divergence.atFrame != null ? ` between frames ${r.divergence.lastGoodFrame} and ${r.divergence.atFrame}` : '') + ')').join('; ')) : '== determinism: not checked');
	return out.join('\n');
}

const fmt = (x, d = 0) => (typeof x === 'number' ? x.toLocaleString('en-US', { maximumFractionDigits: d }) : String(x));

export function renderMarkdown(report) {
	const L = [];
	L.push('# AI test bench report', '', `Generated ${report.generated}. Data: ${report.options.data}. Maps: ${report.options.maps.join(', ')}. Seeds per matchup: ${report.options.seeds} (from ${report.options.seedStart}). Time limit ${report.options.minutes} game minutes. Start positions ${report.options.rotate ? 'rotate with the seed' : 'fixed'}.`, '');
	L.push('## Summary', '', '```', summaryText(report), '```', '');

	for (const [b, d] of Object.entries(report.builds)) {
		L.push(`## Build: ${b}`, '');
		L.push(`${d.matches} matches: ${d.outcomes.victory} victories, ${d.outcomes.timeout} timeouts (no winner, never counted as a win), ${d.outcomes.draw} draws, ${d.outcomes.error} errors.`, '');
		if (d.headToHead.length) {
			L.push('### Head to head', '', '| matchup | first wins | 95% CI | second wins | timeouts | draws | median length |', '|---|---|---|---|---|---|---|');
			for (const g of d.headToHead) L.push(`| ${g.x} vs ${g.y} | ${g.xWins}/${g.played} (${pct(g.xCI.p)}) | ${pct(g.xCI.lo)}-${pct(g.xCI.hi)} | ${g.yWins}/${g.played} (${pct(g.yCI.p)}) | ${g.timeouts} | ${g.draws} | ${fmt(g.medianFrames / 30 / 60, 1)} min |`);
			L.push('');
		}
		for (const t of d.targets) L.push(t.found ? `Target ${t.target.a} > ${t.target.b} = ${pct(t.target.p)}: **${t.wins}/${t.played}** (${ci(t.ci)}) - ${t.verdict}.` : `Target ${t.target.a} > ${t.target.b}: not played.`, '');

		const labels = Object.keys(d.matrix).sort();
		if (labels.length) {
			L.push('### Win-rate matrix', '', 'Row configuration against column configuration: wins / games (timeouts and draws are games without a win).', '', '| | ' + labels.join(' | ') + ' |', '|---|' + labels.map(() => '---').join('|') + '|');
			for (const a of labels) L.push(`| **${a}** | ` + labels.map((c) => { const x = d.matrix[a][c]; return x ? `${x.wins}/${x.n}` + (x.timeouts + x.draws ? ` (${x.timeouts + x.draws} no result)` : '') : ''; }).join(' | ') + ' |');
			L.push('');
		}
		const e = Object.entries(d.elo).sort((x, y) => y[1].elo - x[1].elo);
		if (e.length) {
			L.push('### Elo', '', 'Maximum likelihood over all games, 1000 = a phantom opponent every configuration drew once against. Timeouts and draws count as half a win. Few games give wide error bars: read it together with the intervals above.', '', '| configuration | Elo | games |', '|---|---|---|');
			for (const [l, v] of e) L.push(`| ${l} | ${v.elo} | ${v.games} |`);
			L.push('');
		}
		const bias = Object.entries(d.startBias.byStart);
		if (bias.length > 1) {
			L.push('### Start position bias', '', 'Victories by start position over decided matches; a big difference points at the map, not at the AI.', '', '| start | wins / games |', '|---|---|');
			for (const [s, v] of bias.sort()) L.push(`| ${s} | ${v.wins}/${v.games} |`);
			L.push('');
		}
		const prof = Object.entries(d.profiles);
		if (prof.length) {
			L.push('### Averages per configuration', '', '| configuration | games | money gathered | money spent | peak army value | final army value | idle production | units built | units lost | objects destroyed |', '|---|---|---|---|---|---|---|---|---|---|');
			for (const [l, p] of prof) L.push(`| ${l} | ${p.games} | ${fmt(p.moneyGathered)} | ${fmt(p.moneySpent)} | ${fmt(p.peakArmyValue)} | ${fmt(p.finalArmyValue)} | ${pct(p.idleFraction)} | ${fmt(p.unitsBuilt, 1)} | ${fmt(p.unitsLost, 1)} | ${fmt(p.objectsKilled, 1)} |`);
			L.push('');
		}
		const sp = d.speed;
		L.push('### Speed', '', `${sp.matches} matches: ${fmt(sp.logicFpsMean)} logic frames per second on average (median ${fmt(sp.logicFpsMedian)}, slowest ${fmt(sp.logicFpsMin)}; the game runs at 30), mean ${fmt(sp.framesMean)} frames per match, map load and match setup ${fmt(sp.setupMsMean / 1000, 1)} s, ${fmt(sp.wallMsMean / 1000, 1)} s per match including the browser.`, '');
		if (d.errors.length) {
			L.push('### Errors', '');
			for (const e2 of d.errors) L.push(`- ${e2.id} (${e2.attempts} attempt${e2.attempts > 1 ? 's' : ''}): ${e2.error}`);
			L.push('');
		}
	}

	if (report.comparison) {
		const c = report.comparison;
		L.push(`## Comparison: ${c.baseline} (baseline) vs ${c.candidate} (candidate)`, '');
		if (c.matchups.length) {
			L.push('Wins of the first configuration of each matchup in each build:', '', '| matchup | baseline | candidate | timeouts b/c | median minutes b/c |', '|---|---|---|---|---|');
			for (const r of c.matchups) {
				const bw = wilson(r.baseline.wins, r.baseline.played), cw = wilson(r.candidate.wins, r.candidate.played);
				L.push(`| ${r.matchup} | ${r.baseline.wins}/${r.baseline.played} (${pct(bw.p)}, ${pct(bw.lo)}-${pct(bw.hi)}) | ${r.candidate.wins}/${r.candidate.played} (${pct(cw.p)}, ${pct(cw.lo)}-${pct(cw.hi)}) | ${r.baseline.timeouts}/${r.candidate.timeouts} | ${fmt(r.baseline.medianFrames / 1800, 1)} / ${fmt(r.candidate.medianFrames / 1800, 1)} |`);
			}
			L.push('');
		}
		if (c.profiles.length) {
			L.push('Averages per configuration (baseline -> candidate):', '', '| configuration | money gathered | money spent | peak army value | idle production | units built | objects destroyed |', '|---|---|---|---|---|---|---|');
			const arrow = (x, y, d = 0) => `${fmt(x, d)} -> ${fmt(y, d)}`;
			for (const p of c.profiles) L.push(`| ${p.config} | ${arrow(p.baseline.moneyGathered, p.candidate.moneyGathered)} | ${arrow(p.baseline.moneySpent, p.candidate.moneySpent)} | ${arrow(p.baseline.peakArmyValue, p.candidate.peakArmyValue)} | ${pct(p.baseline.idleFraction)} -> ${pct(p.candidate.idleFraction)} | ${arrow(p.baseline.unitsBuilt, p.candidate.unitsBuilt, 1)} | ${arrow(p.baseline.objectsKilled, p.candidate.objectsKilled, 1)} |`);
			L.push('');
		}
		L.push(`Speed: ${fmt(c.speed.baseline.logicFpsMean)} -> ${fmt(c.speed.candidate.logicFpsMean)} logic frames per second.`, '');
	}

	const det = report.determinism;
	L.push('## Determinism', '');
	if (!det.checked) L.push('Not checked.', '');
	else {
		L.push(`${det.identical}/${det.checked} replayed matches reproduced the same CRC at every sample (and the same statistics).`, '');
		L.push('| match | frames | CRC samples | result | divergence |', '|---|---|---|---|---|');
		for (const r of det.runs) L.push(`| ${r.id} | ${fmt(r.frames)} | ${r.crcSamples} | ${r.identical ? 'identical' : '**DIVERGED**'} | ${r.divergence ? `${r.divergence.kind}: first differs between frames ${r.divergence.lastGoodFrame} and ${r.divergence.atFrame}` + (r.divergence.a != null ? ` (${r.divergence.a} vs ${r.divergence.b})` : '') : ''} |`);
		L.push('');
	}
	return L.join('\n') + '\n';
}
