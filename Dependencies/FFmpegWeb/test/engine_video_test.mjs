// Plays a generated Bink video in the real game (z_generals) in headless Chromium, as the intro "EA logo" movie of
// the free starter content, and checks what the player does: the picture (screenshots during playback are matched
// to the frame they show), the sound (an analyser on the Web Audio master finds the tone and when it starts), the end
// of the movie, and that a missing, corrupt or truncated video is skipped without a trap.
//
//   node engine_video_test.mjs --site <build dir>/GeneralsMD [--scenario play|skip|nodevice|missing|corrupt|truncated|all] [--out dir]
//                              [--port 8951] [--keep] [--save-shots]
//
// Needs the starter pack next to the page (cmake --build <dir> --target starter_pack), Python 3 (the test video comes
// from make_test_bik.py; no copyrighted video is used or needed), and Playwright with a Chromium
// (NODE_PATH=/opt/node22/lib/node_modules, PLAYWRIGHT_BROWSERS_PATH=/opt/pw-browsers or CHROMIUM_PATH).
import { createRequire } from 'node:module';
import { spawn, spawnSync } from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';

const require = createRequire(import.meta.url);
let playwright;
try { playwright = require('playwright'); }
catch { playwright = require(path.join(process.env.NODE_PATH || '/opt/node22/lib/node_modules', 'playwright')); }

const here = path.dirname(new URL(import.meta.url).pathname);
const opt = { port: 8951, out: 'video-test-out', scenario: 'all', keep: false, saveShots: false };
{
	const a = process.argv.slice(2);
	for (let i = 0; i < a.length; ++i) {
		switch (a[i]) {
			case '--site': opt.site = path.resolve(a[++i]); break;
			case '--out': opt.out = path.resolve(a[++i]); break;
			case '--scenario': opt.scenario = a[++i]; break;
			case '--port': opt.port = Number(a[++i]); break;
			case '--keep': opt.keep = true; break;
			case '--save-shots': opt.saveShots = true; break;
			default: console.error('unknown option ' + a[i]); process.exit(2);
		}
	}
	if (!opt.site) { console.error('--site is required'); process.exit(2); }
}
fs.mkdirSync(opt.out, { recursive: true });

let failures = 0;
function check(ok, what) {
	console.log((ok ? 'ok    ' : 'FAIL  ') + what);
	if (!ok) ++failures;
}

// ---- the expected picture ----------------------------------------------------------------------------------------
// Must match block_value() in make_test_bik.py: 8x8 blocks of luma, one chroma value per frame.
const VIDEO = { width: 320, height: 240, frames: 190, fps: 30 };		// 190 < 200: every frame has its own luma pattern
const TONE = { start: 2.0, end: 3.5 };
function expectedMeanColor(n) {
	// The middle half of the picture: blocks 10..29 by 7..22 (of 40x30).
	let r = 0, g = 0, b = 0, count = 0;
	const u = 90 + (n * 2) % 100, v = 200 - (n * 2) % 100;
	for (let by = 7; by < 23; ++by) {
		for (let bx = 10; bx < 30; ++bx) {
			const y = 16 + ((bx * 7 + by * 5 + n * 3) % 200);
			const c = 1.164 * (y - 16);
			r += Math.max(0, Math.min(255, c + 1.596 * (v - 128)));
			g += Math.max(0, Math.min(255, c - 0.392 * (u - 128) - 0.813 * (v - 128)));
			b += Math.max(0, Math.min(255, c + 2.017 * (u - 128)));
			++count;
		}
	}
	return [r / count, g / count, b / count];
}
const EXPECTED = Array.from({ length: VIDEO.frames }, (_, n) => expectedMeanColor(n));

// ---- a PNG decoder, enough for the screenshots (8 bit RGB/RGBA, not interlaced) ------------------------------------
function decodePng(buf) {
	let pos = 8, width = 0, height = 0, channels = 0;
	const idat = [];
	while (pos < buf.length) {
		const len = buf.readUInt32BE(pos), type = buf.toString('ascii', pos + 4, pos + 8);
		const data = buf.subarray(pos + 8, pos + 8 + len);
		if (type === 'IHDR') { width = data.readUInt32BE(0); height = data.readUInt32BE(4); channels = data[9] === 6 ? 4 : 3; }
		if (type === 'IDAT') idat.push(data);
		pos += 12 + len;
	}
	const raw = zlib.inflateSync(Buffer.concat(idat));
	const stride = width * channels, out = Buffer.alloc(stride * height);
	for (let y = 0; y < height; ++y) {
		const filter = raw[y * (stride + 1)], line = raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1));
		for (let x = 0; x < stride; ++x) {
			const a = x >= channels ? out[y * stride + x - channels] : 0;
			const b = y > 0 ? out[(y - 1) * stride + x] : 0;
			const c = x >= channels && y > 0 ? out[(y - 1) * stride + x - channels] : 0;
			let v = line[x];
			switch (filter) {
				case 1: v += a; break;
				case 2: v += b; break;
				case 3: v += (a + b) >> 1; break;
				case 4: { const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c); v += pa <= pb && pa <= pc ? a : pb <= pc ? b : c; break; }
			}
			out[y * stride + x] = v & 255;
		}
	}
	return { width, height, channels, data: out };
}
function meanColor(png, x0, y0, x1, y1) {
	let r = 0, g = 0, b = 0, n = 0;
	for (let y = y0; y < y1; y += 2) for (let x = x0; x < x1; x += 2) {
		const i = (y * png.width + x) * png.channels;
		r += png.data[i]; g += png.data[i + 1]; b += png.data[i + 2]; ++n;
	}
	return [r / n, g / n, b / n];
}
function bestFrame(color) {
	let best = 0, bestD = 1e9;
	EXPECTED.forEach((e, n) => {
		const d = (e[0] - color[0]) ** 2 + (e[1] - color[1]) ** 2 + (e[2] - color[2]) ** 2;
		if (d < bestD) { bestD = d; best = n; }
	});
	return { frame: best, distance: Math.sqrt(bestD) };
}

// ---- the test site: the real site with the starter pack plus the test video ---------------------------------------------
function sha256(buf) { return crypto.createHash('sha256').update(buf).digest('hex'); }

function makeSite(name, movie) {
	const dir = path.join(opt.out, 'site-' + name);
	fs.rmSync(dir, { recursive: true, force: true });
	fs.mkdirSync(dir, { recursive: true });
	for (const entry of fs.readdirSync(opt.site)) {
		if (entry !== 'starterpack') fs.symlinkSync(path.join(opt.site, entry), path.join(dir, entry));
	}
	const pack = path.join(dir, 'starterpack');
	fs.cpSync(path.join(opt.site, 'starterpack'), pack, { recursive: true });

	const manifestPath = path.join(pack, 'manifest.json');
	const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
	const put = (rel, buf) => {
		const file = path.join(pack, rel);
		fs.mkdirSync(path.dirname(file), { recursive: true });
		fs.writeFileSync(file, buf);
		manifest.files = manifest.files.filter((f) => f.path !== rel);
		manifest.files.push({ path: rel, sha256: sha256(buf), size: buf.length });
	};
	// Both logo names (the engine picks one by the memory of the machine) play the test video.
	put('data/ini/video.ini', Buffer.from(
		'Video EALogoMovie\n  Filename = testlogo\n  Comment = test\nEnd\n' +
		'Video EALogoMovie640\n  Filename = testlogo\n  Comment = test\nEnd\n'));
	if (movie) put('data/movies/testlogo.bik', movie);
	// The starter pack turns the intro off (GameData.ini); the EA logo movie is the first part of it.
	put('data/ini/gamedata.ini', Buffer.concat([fs.readFileSync(path.join(pack, 'data/ini/gamedata.ini')),
		Buffer.from('\nGameData\n  PlayIntro = Yes\nEnd\n')]));
	manifest.totalSize = manifest.files.reduce((s, f) => s + f.size, 0);
	fs.writeFileSync(manifestPath, JSON.stringify(manifest));
	return dir;
}

function makeBik(name, extra = []) {
	const file = path.join(opt.out, name + '.bik');
	const r = spawnSync('python3', [path.join(here, 'make_test_bik.py'), file, '--width', String(VIDEO.width), '--height', String(VIDEO.height),
		'--frames', String(VIDEO.frames), '--fps', String(VIDEO.fps), ...extra], { encoding: 'utf8' });
	if (r.status !== 0) throw new Error('make_test_bik.py failed: ' + r.stderr);
	return fs.readFileSync(file);
}

// ---- running the game ---------------------------------------------------------------------------------------------------
const chromium = [process.env.CHROMIUM_PATH, '/opt/pw-browsers/chromium-1194/chrome-linux/chrome'].find((p) => p && fs.existsSync(p));

async function runGame(name, siteDir, { seconds, shots, analyser, aliveSeconds = 8, noAudioContext = false, skipAfterMs = 0 }) {
	const server = spawn('python3', [path.join(here, '..', '..', '..', 'GeneralsMD', 'Code', 'Main', 'web', 'serve.py'), '--port', String(opt.port), '--dir', siteDir],
		{ env: { ...process.env, SERVE_QUIET: '1' }, stdio: 'inherit' });
	await new Promise((r) => setTimeout(r, 800));
	const browser = await playwright.chromium.launch({
		executablePath: chromium,
		args: ['--no-sandbox', '--use-angle=swiftshader', '--use-gl=angle', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist',
			'--enable-webgl', '--enable-features=SharedArrayBuffer', '--autoplay-policy=no-user-gesture-required'],
	});
	const result = { logs: [], shots: [], audio: [], errors: [], trapped: false, frames: 0 };
	try {
		const page = await (await browser.newContext({ viewport: { width: 1100, height: 800 } })).newPage();
		if (noAudioContext) await page.addInitScript(() => { delete globalThis.AudioContext; delete globalThis.webkitAudioContext; });
		const log = (text) => result.logs.push({ t: Date.now(), text });
		page.on('console', (m) => log(m.text()));
		page.on('pageerror', (e) => { log('PAGEERROR ' + e.message); result.errors.push(e.message); });
		page.on('worker', (w) => w.on('console', (m) => log('[worker] ' + m.text())));

		await page.goto(`http://127.0.0.1:${opt.port}/z_generals.html?picker=input`);
		await page.waitForFunction(() => document.getElementById('download-starter'), null, { timeout: 30000 });
		await page.click('#download-starter');
		await page.waitForFunction(() => /^(Ready|Download failed|Could not|This server)/.test(document.getElementById('state-starter').textContent), null, { timeout: 180000 });
		await page.waitForFunction(() => !document.getElementById('play').disabled, null, { timeout: 30000 });
		result.t0 = Date.now();
		await page.click('#play');

		if (analyser) {
			// An analyser on the master gain of the audio device, sampled in the page.
			await page.waitForFunction(() => globalThis.zhWebAudio && globalThis.zhWebAudio.ctx && globalThis.zhWebAudio.master, null, { timeout: 60000 }).catch(() => {});
			await page.evaluate(() => {
				const A = globalThis.zhWebAudio;
				if (!A || !A.master) return;
				const an = A.ctx.createAnalyser();
				an.fftSize = 2048;
				A.master.connect(an);
				const td = new Float32Array(an.fftSize), fd = new Float32Array(an.frequencyBinCount);
				window.__audio = [];
				setInterval(() => {
					an.getFloatTimeDomainData(td);
					let s = 0;
					for (let i = 0; i < td.length; ++i) s += td[i] * td[i];
					an.getFloatFrequencyData(fd);
					let peak = 0;
					for (let i = 1; i < fd.length; ++i) if (fd[i] > fd[peak]) peak = i;
					window.__audio.push([Date.now(), Math.sqrt(s / td.length), peak * A.ctx.sampleRate / an.fftSize]);
				}, 30);
			});
		}

		const canvasBox = async () => page.locator('#canvas').boundingBox();
		const until = Date.now() + seconds * 1000;
		let closed = false, skipped = false;
		while (Date.now() < until && !closed) {
			if (skipAfterMs && !skipped) {
				const opened = result.logs.find((l) => /WebVideoStream: 320x240/.test(l.text));
				if (opened && Date.now() - opened.t > skipAfterMs) {
					skipped = true;
					result.skippedAt = Date.now();
					await page.keyboard.press('Escape');
				}
			}
			if (shots) {
				const box = await canvasBox();
				const t = Date.now();
				const raw = await page.screenshot({ type: 'png' });
				if (opt.saveShots) fs.writeFileSync(path.join(opt.out, `${name}-shot${String(result.shots.length).padStart(2, '0')}.png`), raw);
				const png = decodePng(raw);
				if (box) {
					const m = meanColor(png, Math.round(box.x + box.width * 0.25), Math.round(box.y + box.height * 0.25),
						Math.round(box.x + box.width * 0.75), Math.round(box.y + box.height * 0.75));
					result.shots.push({ t, color: m, best: bestFrame(m) });
				}
			} else {
				await page.waitForTimeout(200);
			}
			closed = result.logs.some((l) => /WebVideoStream: closed/.test(l.text));
		}
		if (closed) await page.waitForTimeout(500);
		// The page learns the frame count of the engine every 30 frames.
		result.frames = await page.evaluate(() => window.__zhFrames || 0).catch(() => 0);
		await page.waitForTimeout(aliveSeconds * 1000);
		result.framesLater = await page.evaluate(() => window.__zhFrames || 0).catch(() => 0);
		if (analyser) {
			result.audio = await page.evaluate(() => window.__audio || []).catch(() => []);
			result.audioStats = await page.evaluate(() => {
				const A = globalThis.zhWebAudio;
				return A ? { stats: A.stats, state: A.ctx.state, rate: A.ctx.sampleRate } : null;
			}).catch(() => null);
		}
		await page.screenshot({ path: path.join(opt.out, name + '-end.png') });
	} finally {
		await browser.close();
		server.kill();
		await new Promise((r) => setTimeout(r, 300));
	}
	result.trapped = result.logs.some((l) => /RuntimeError|unreachable|memory access out of bounds|Aborted\(|abort\(/.test(l.text));
	fs.writeFileSync(path.join(opt.out, name + '.log'), result.logs.map((l) => ((l.t - (result.t0 || l.t)) / 1000).toFixed(2).padStart(7) + ' ' + l.text).join('\n') + '\n');
	return result;
}

const videoLogs = (r) => r.logs.filter((l) => /WebVideoStream/.test(l.text));

async function scenarioPlay() {
	console.log(`--- play: a ${(VIDEO.frames / VIDEO.fps).toFixed(1)} s video (${VIDEO.frames} frames, ${VIDEO.fps} fps) with a tone from ${TONE.start} s to ${TONE.end} s`);
	const bik = makeBik('play', ['--tone-window', `${TONE.start}:${TONE.end}`]);
	const r = await runGame('play', makeSite('play', bik), { seconds: 90, shots: true, analyser: true });
	check(!r.trapped && r.errors.length === 0, 'no trap, no page error');
	const open = videoLogs(r).find((l) => new RegExp(`320x240, ${VIDEO.frames} frames at 30/1 fps`).test(l.text));
	check(!!open, 'the movie opened: ' + (open ? open.text.replace('[worker] ', '') : '(no log line)'));
	check(!!open && /sound: yes/.test(open.text), 'the movie has sound on a voice of the Web Audio device');
	const close = videoLogs(r).find((l) => /WebVideoStream: closed/.test(l.text));
	check(!!close, 'the movie ended: ' + (close ? close.text.replace('[worker] ', '') : '(no log line)'));
	if (open && close) {
		const seconds = (close.t - open.t) / 1000;
		const length = VIDEO.frames / VIDEO.fps;
		check(seconds > length * 0.8 && seconds < length * 1.8, `the movie took ${seconds.toFixed(2)} s (${length.toFixed(1)} s of video)`);
		const m = /closed at frame (\d+) of (\d+): (\d+) shown, (\d+) skipped, sound (\d+) chunks, (\d+) resyncs/.exec(close.text);
		if (m) {
			check(Number(m[1]) === VIDEO.frames, `played to the last frame (${m[1]} of ${m[2]})`);
			check(Number(m[3]) >= 10 && Number(m[3]) + Number(m[4]) >= VIDEO.frames - 5, `${m[3]} frames shown, ${m[4]} skipped because the game was late`);
			check(Number(m[5]) >= 10, `${m[5]} chunks of sound queued`);
			console.log(`      (${m[6]} audio resyncs)`);
		}
	}
	// Picture: which frame is on the screen when, against the time since the first shot that shows the video.
	const matched = r.shots.map((s) => ({ t: s.t, ...s.best })).filter((s) => s.distance < 8);
	console.log('      screenshots (seconds since the movie opened: frame): ' + matched.map((m) => `${open ? ((m.t - open.t) / 1000).toFixed(1) : '?'}:${m.frame}`).join(' '));
	check(matched.length >= 6, `${matched.length} of ${r.shots.length} screenshots show a frame of the video`);
	if (matched.length >= 6) {
		matched.sort((a, b) => a.t - b.t);
		const frames = matched.map((m) => m.frame);
		let increasing = 0;
		for (let i = 1; i < frames.length; ++i) if (frames[i] >= frames[i - 1] || frames[i] < 10) ++increasing;
		check(increasing >= frames.length - 3, 'the frames advance: ' + frames.join(' '));
		// Least squares frame = rate * (t - t0), over the part of the shots that did not wrap around.
		const pts = matched.filter((m, i) => i === 0 || m.frame >= matched[0].frame);
		const n = pts.length, mt = pts.reduce((s, p) => s + p.t, 0) / n, mf = pts.reduce((s, p) => s + p.frame, 0) / n;
		const slope = pts.reduce((s, p) => s + (p.t - mt) * (p.frame - mf), 0) / Math.max(1, pts.reduce((s, p) => s + (p.t - mt) ** 2, 0)) * 1000;
		check(slope > 24 && slope < 36, `the video runs at ${slope.toFixed(1)} frames per second (30)`);
	}
	// Sound: the tone, when it starts and how long it lasts, against the start of the movie.
	const loud = r.audio.filter((a) => a[1] > 0.02);
	check(loud.length >= 10, `the analyser hears the tone (${loud.length} of ${r.audio.length} samples above the noise)`);
	if (loud.length >= 10 && open) {
		const start = (loud[0][0] - open.t) / 1000, end = (loud[loud.length - 1][0] - open.t) / 1000;
		check(start > TONE.start - 0.3 && start < TONE.start + 0.6, `the tone starts ${start.toFixed(2)} s after the movie opened (${TONE.start} s + the start-up of the movie)`);
		check(Math.abs(end - start - (TONE.end - TONE.start)) < 0.4, `the tone lasts ${(end - start).toFixed(2)} s (${TONE.end - TONE.start} s)`);
		const freqs = loud.map((a) => a[2]).sort((x, y) => x - y);
		const median = freqs[Math.floor(freqs.length / 2)];
		check(Math.abs(median - 517) < 45, `the tone is ${median.toFixed(0)} Hz (517)`);
	}
	if (r.audioStats) {
		check(r.audioStats.stats.errors === 0 && r.audioStats.stats.segmentsDropped === 0,
			`the audio device had no errors and dropped no sound: ${JSON.stringify(r.audioStats.stats)}`);
	}
	check(r.framesLater > r.frames, `the engine goes on after the movie (${r.frames} -> ${r.framesLater} frames)`);
}

async function scenarioNoStream(name, what, movie) {
	console.log(`--- ${name}: ${what}`);
	const r = await runGame(name, makeSite(name, movie), { seconds: 25, shots: false, analyser: false });
	check(!r.trapped && r.errors.length === 0, 'no trap, no page error');
	check(r.framesLater > r.frames && r.framesLater > 100, `the engine keeps rendering (${r.frames} -> ${r.framesLater} frames)`);
	return r;
}

async function scenarioSkip() {
	console.log('--- skip: Escape pressed one second into the movie (an NTSC frame rate: 30000/1001)');
	const bik = makeBik('skip', ['--tone-window', '0:6', '--fps', '30000', '--fps-den', '1001']);
	const r = await runGame('skip', makeSite('skip', bik), { seconds: 60, shots: false, analyser: true, skipAfterMs: 1000 });
	check(!r.trapped && r.errors.length === 0, 'no trap, no page error');
	const open = videoLogs(r).find((l) => /WebVideoStream: 320x240/.test(l.text));
	check(!!open && new RegExp(`${VIDEO.frames} frames at 30000/1001 fps`).test(open.text), 'the frame count is right for a fractional frame rate: ' + (open ? open.text.replace('[worker] ', '') : '(no log line)'));
	const close = videoLogs(r).find((l) => /closed at frame (\d+) of (\d+)/.test(l.text));
	const m = close && /closed at frame (\d+) of (\d+)/.exec(close.text);
	check(!!m && Number(m[1]) < VIDEO.frames * 0.6, 'the movie stopped early: ' + (close ? close.text.replace('[worker] ', '') : '(no log line)'));
	if (close && r.skippedAt) {
		check(close.t - r.skippedAt < 1500, `... ${close.t - r.skippedAt} ms after the key`);
		// The sound stops with it (the voice fades out in 50 ms; the analyser looks at what is left 0.5 s later).
		const late = r.audio.filter((a) => a[0] > close.t + 500);
		check(late.length > 5 && late.every((a) => a[1] < 0.005), `the sound is gone after the movie (${late.length} samples, loudest ${Math.max(0, ...late.map((a) => a[1])).toFixed(4)})`);
	}
	check(r.framesLater > r.frames, `the engine goes on after the movie (${r.frames} -> ${r.framesLater} frames)`);
}

async function scenarioNoDevice() {
	console.log('--- nodevice: the browser has no Web Audio: the movie plays without sound');
	const bik = makeBik('nodevice', ['--tone-window', '0:3']);
	const r = await runGame('nodevice', makeSite('nodevice', bik), { seconds: 90, shots: false, analyser: false, noAudioContext: true });
	check(!r.trapped && r.errors.length === 0, 'no trap, no page error');
	const open = videoLogs(r).find((l) => /WebVideoStream: 320x240/.test(l.text));
	check(!!open && /sound: no device/.test(open.text), 'the movie opened without sound: ' + (open ? open.text.replace('[worker] ', '') : '(no log line)'));
	const close = videoLogs(r).find((l) => /closed at frame (\d+) of (\d+)/.test(l.text));
	const m = close && /closed at frame (\d+) of (\d+)/.exec(close.text);
	check(!!m && Number(m[1]) === VIDEO.frames, 'the movie played to the last frame: ' + (close ? close.text.replace('[worker] ', '') : '(no log line)'));
	if (open && close) {
		const seconds = (close.t - open.t) / 1000, length = VIDEO.frames / VIDEO.fps;
		check(seconds > length * 0.8 && seconds < length * 1.8, `the movie took ${seconds.toFixed(2)} s (${length.toFixed(1)} s of video)`);
	}
	check(r.framesLater > r.frames, `the engine goes on after the movie (${r.frames} -> ${r.framesLater} frames)`);
}

const wanted = (s) => opt.scenario === 'all' || opt.scenario === s;
if (!chromium) { console.error('no Chromium found (set CHROMIUM_PATH)'); process.exit(2); }
if (wanted('play')) await scenarioPlay();
if (wanted('skip')) await scenarioSkip();
if (wanted('nodevice')) await scenarioNoDevice();
if (wanted('missing')) await scenarioNoStream('missing', 'Video.ini names a movie that is not there (the starter pack has no videos)', null);
if (wanted('corrupt')) {
	const r = await scenarioNoStream('corrupt', 'the movie is garbage', crypto.randomBytes(100000));
	check(!videoLogs(r).some((l) => /closed/.test(l.text)), 'no stream was created');
}
if (wanted('truncated')) {
	const bik = makeBik('trunc', ['--tone-window', '0:3']);
	const r = await scenarioNoStream('truncated', 'the movie file ends after 60 percent', bik.subarray(0, Math.floor(bik.length * 0.6)));
	const close = videoLogs(r).find((l) => /closed/.test(l.text));
	check(!!close, 'the movie ended anyway: ' + (close ? close.text.replace('[worker] ', '') : '(no log line)'));
}
console.log(failures ? `\n${failures} check(s) failed` : '\nall checks passed');
if (!opt.keep) for (const e of fs.readdirSync(opt.out)) if (e.startsWith('site-')) fs.rmSync(path.join(opt.out, e), { recursive: true, force: true });
process.exit(failures ? 1 : 0);
