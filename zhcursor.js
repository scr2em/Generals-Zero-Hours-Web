// The game's mouse cursors in the browser.
//
// The game's cursors are Windows animated cursor files (Data\Cursors\*.ANI): a RIFF container of .cur images with a
// frame order and a frame rate. The engine loads them through LoadCursorFromFile() and selects them with SetCursor()
// like it does on Windows (WebCompat/WebPlatform, see WebPlatform.h). It hands the file bytes to window.zhCursor.load()
// and the id of the cursor to show to window.zhCursor.set(); this file turns them into CSS cursors (blob URLs of the
// frames, hot spots from the .cur headers) and plays the animation by switching the canvas's CSS cursor.
//
// set(id): id > 0 a loaded cursor, 0 no cursor (the game draws its own), -1 the system arrow.
// A cursor that cannot be decoded shows the system arrow.
(function () {
	'use strict';

	const MAX_CURSOR_SIZE = 128; // CSS cursors larger than this are ignored by the browser
	const JIFFY_MS = 1000 / 60;

	const cursors = new Map();   // id -> { ready, steps: [{ css, ms }] } | { failed }
	let wanted = -1;
	let timer = 0;

	function canvasElement() {
		return document.getElementById('canvas');
	}

	function u16(d, o) { return d[o] | (d[o + 1] << 8); }
	function u32(d, o) { return (d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (d[o + 3] << 24)) >>> 0; }
	function tag(d, o) { return String.fromCharCode(d[o], d[o + 1], d[o + 2], d[o + 3]); }

	// The chunks of a RIFF list body: [{ id, start, size }] (start is the offset of the data).
	function chunks(d, start, end) {
		const out = [];
		let pos = start;
		while (pos + 8 <= end) {
			const size = u32(d, pos + 4);
			const dataStart = pos + 8;
			const dataEnd = Math.min(dataStart + size, end);
			out.push({ id: tag(d, pos), start: dataStart, size: dataEnd - dataStart });
			pos = dataStart + size + (size & 1);
		}
		return out;
	}

	// An animated cursor (.ani) or a single static one (.cur): { frames: [Uint8Array of .cur files], steps: [{ frame, jiffies }] }
	function parse(d) {
		if (d.length >= 6 && u16(d, 0) === 0 && (u16(d, 2) === 2 || u16(d, 2) === 1)) {
			return { frames: [d], steps: [{ frame: 0, jiffies: 6 }] };
		}
		if (d.length < 12 || tag(d, 0) !== 'RIFF' || tag(d, 8) !== 'ACON') return null;
		let header = null, rates = null, seq = null;
		const frames = [];
		for (const c of chunks(d, 12, Math.min(d.length, 8 + u32(d, 4)))) {
			if (c.id === 'anih' && c.size >= 36) {
				header = { frames: u32(d, c.start + 4), steps: u32(d, c.start + 8), rate: u32(d, c.start + 28), flags: u32(d, c.start + 32) };
			} else if (c.id === 'rate') {
				rates = [];
				for (let i = 0; i + 4 <= c.size; i += 4) rates.push(u32(d, c.start + i));
			} else if (c.id === 'seq ') {
				seq = [];
				for (let i = 0; i + 4 <= c.size; i += 4) seq.push(u32(d, c.start + i));
			} else if (c.id === 'LIST' && tag(d, c.start) === 'fram') {
				for (const f of chunks(d, c.start + 4, c.start + c.size)) {
					if (f.id === 'icon') frames.push(d.subarray(f.start, f.start + f.size));
				}
			}
		}
		if (!header || !frames.length || !(header.flags & 1)) return null;
		const count = (header.flags & 2) && seq && seq.length ? seq.length : frames.length;
		const steps = [];
		for (let i = 0; i < count; ++i) {
			const frame = (header.flags & 2) && seq && seq.length ? seq[i] : i;
			const jiffies = rates && rates[i] ? rates[i] : (header.rate || 6);
			steps.push({ frame: Math.min(frame, frames.length - 1), jiffies: Math.max(1, jiffies) });
		}
		return { frames, steps };
	}

	// "url(...) x y, auto" for a .cur file; null if the browser cannot show it.
	async function frameCss(bytes) {
		const isCursor = bytes.length >= 22 && u16(bytes, 2) === 2;
		const w = bytes.length >= 8 ? (bytes[6] || 256) : 0;
		const h = bytes.length >= 8 ? (bytes[7] || 256) : 0;
		if (w > MAX_CURSOR_SIZE || h > MAX_CURSOR_SIZE) return null;
		const hotX = isCursor ? u16(bytes, 10) : 0;
		const hotY = isCursor ? u16(bytes, 12) : 0;
		const blob = new Blob([bytes], { type: 'image/x-icon' });
		try {
			const bitmap = await createImageBitmap(blob);	// proves that the browser decodes it
			bitmap.close();
		} catch (e) {
			return null;
		}
		return 'url("' + URL.createObjectURL(blob) + '") ' + hotX + ' ' + hotY + ', auto';
	}

	async function build(id, bytes) {
		let entry = { failed: true };
		try {
			const ani = parse(bytes);
			if (ani) {
				const css = [];
				for (const f of ani.frames) css.push(await frameCss(f));
				if (css.every((c) => c)) {
					entry = { ready: true, steps: ani.steps.map((s) => ({ css: css[s.frame], ms: s.jiffies * JIFFY_MS })) };
				}
			}
		} catch (e) {
			entry = { failed: true };
		}
		cursors.set(id, entry);
		if (wanted === id) apply();
	}

	function stop() {
		if (timer) clearTimeout(timer);
		timer = 0;
	}

	function apply() {
		stop();
		const el = canvasElement();
		if (!el) return;
		if (wanted === 0) { el.style.cursor = 'none'; return; }
		const entry = wanted > 0 ? cursors.get(wanted) : null;
		if (!entry || !entry.ready) { el.style.cursor = 'default'; return; }
		const steps = entry.steps;
		el.style.cursor = steps[0].css;
		if (steps.length < 2) return;
		let i = 0;
		const tick = () => {
			i = (i + 1) % steps.length;
			el.style.cursor = steps[i].css;
			timer = setTimeout(tick, steps[i].ms);
		};
		timer = setTimeout(tick, steps[0].ms);
	}

	window.zhCursor = {
		load(id, bytes) {
			cursors.set(id, { pending: true });
			build(id, bytes);
		},
		set(id) {
			if (id === wanted && (id <= 0 || (cursors.get(id) || {}).ready)) return;
			wanted = id;
			apply();
		},
		// For tests: what the canvas shows now.
		current() {
			const el = canvasElement();
			return { wanted, css: el ? el.style.cursor : '', loaded: [...cursors.entries()].map(([k, v]) => [k, v.ready ? v.steps.length : (v.failed ? 'failed' : 'pending')]) };
		},
	};
}());
