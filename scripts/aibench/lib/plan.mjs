// Pieces of a match plan shared by aibench.mjs and tune.mjs: the player syntax, the worker pool, retries of matches that
// failed for technical reasons, and the hash the engine prints for an aiini= file.

// "hard:Ironwood" -> { difficulty, side, variant, label }; the label is the configuration's name in the reports.
// "expert:China:trace@1": the same on team 1 (players of a team are allies; without @ every player is on his own).
export function parsePlayer(text) {
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

// The players= value of the engine command line for players placed on start positions (p.start).
export function playersArg(placed) {
	return placed.map((p) => `${p.difficulty}:${p.side}:${p.team ?? ''}:${p.start}:${p.variant}`.replace(/:+$/, '')).join(',');
}

// Runs fn over items with at most `n` at a time.
export async function pool(items, n, fn) {
	let next = 0;
	const worker = async () => { for (;;) { const i = next++; if (i >= items.length) return; await fn(items[i], i); } };
	await Promise.all(Array.from({ length: Math.max(1, Math.min(n, items.length)) }, worker));
}

// A failure of the machinery (no result, a crash), not of the match: worth one more try.
export const technical = (e) => /^(no result|engine failure|browser|the engine exited|unreadable)/.test(e || '');

// Plays a match with `runner`, again up to `retries` times when it failed for technical reasons. The result carries
// the number of attempts.
export async function runWithRetries(runner, build, args, timeoutMs, retries, log = () => {}, label = '') {
	for (let attempt = 0; ; ++attempt) {
		const r = await runner.runMatch(build, args, timeoutMs);
		if (!r.ok && technical(r.error) && attempt < retries) { log(`  retry ${label}: ${r.error}`); continue; }
		return { ...r, attempts: attempt + 1 };
	}
}

// FNV-1a of a text's bytes as 8 hex digits: the "aiiniHash" the engine writes for an aiini= file.
export function fnv1a(data) {
	const bytes = typeof data === 'string' ? Buffer.from(data, 'utf8') : data;
	let h = 0x811c9dc5;
	for (const b of bytes) h = Math.imul(h ^ b, 0x01000193) >>> 0;
	return h.toString(16).padStart(8, '0');
}
