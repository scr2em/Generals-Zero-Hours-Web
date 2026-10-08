// The real launcher (z_generals.html) and the real game, reading a folder in place: picks the folder through the
// launcher's own "Select folder" button (<input webkitdirectory> stands in for showDirectoryPicker), presses Play,
// waits for frames and takes a screenshot; with --steps drives the menus like run_direct.mjs.
//
//   node launcher_direct.mjs --site <build/GeneralsMD> --folder <dir with ZeroHour/ [and Generals/]> --shot out.png
//        [--copy] [--steps "c:186,240 w:4 c:700,566 w:25 s:name"] [--out dir]
//
// --copy tests the same through the copy into browser storage, for a comparison of the start up times.
import { createRequire } from 'node:module';
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';

const require = createRequire(import.meta.url);
let playwright;
try { playwright = require('playwright'); }
catch { playwright = require(path.join(process.env.NODE_PATH || '/opt/node22/lib/node_modules', 'playwright')); }

const opt = { shot: 'launcher.png', out: '.', steps: '', copy: false };
{
	const a = process.argv.slice(2);
	for (let i = 0; i < a.length; ++i) {
		switch (a[i]) {
			case '--site': opt.site = a[++i]; break;
			case '--folder': opt.folder = a[++i]; break;
			case '--shot': opt.shot = a[++i]; break;
			case '--out': opt.out = a[++i]; break;
			case '--steps': opt.steps = a[++i]; break;
			case '--copy': opt.copy = true; break;
			default: console.error('unknown option ' + a[i]); process.exit(2);
		}
	}
	if (!opt.site || !opt.folder) { console.error('--site and --folder are required'); process.exit(2); }
}
fs.mkdirSync(opt.out, { recursive: true });
const server = spawn('python3', [path.join(opt.site, 'serve.py'), '--port', '0', '--dir', opt.site],
	{ env: { ...process.env, SERVE_QUIET: '1', PYTHONUNBUFFERED: '1' }, stdio: ['ignore', 'pipe', 'inherit'] });
process.on('exit', () => server.kill());
const port = await new Promise((resolve, reject) => {
	let text = '';
	server.stdout.on('data', (d) => { text += d; const m = /127\.0\.0\.1:(\d+)/.exec(text); if (m) resolve(Number(m[1])); });
	server.on('exit', () => reject(new Error('serve.py did not start')));
});
const exe = ['/opt/pw-browsers/chromium-1194/chrome-linux/chrome', '/opt/pw-browsers/chromium/chrome'].find((p) => fs.existsSync(p));
const browser = await playwright.chromium.launch({
	executablePath: exe,
	args: ['--no-sandbox', '--use-angle=swiftshader', '--use-gl=angle', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist', '--enable-webgl'],
});
const page = await (await browser.newContext({ viewport: { width: 1100, height: 800 } })).newPage();
const logs = [];
page.on('console', (m) => logs.push(m.text()));
page.on('worker', (w) => w.on('console', (m) => logs.push(m.text())));

await page.goto(`http://127.0.0.1:${port}/z_generals.html?picker=input${opt.copy ? '&copy=1' : ''}&arg=-webdirectstats`);
await page.waitForFunction(() => /Not imported/.test(document.getElementById('state-game').textContent));
const t0 = Date.now();
const [chooser] = await Promise.all([page.waitForEvent('filechooser'), page.click('#pick-game')]);
await chooser.setFiles(path.join(opt.folder, 'ZeroHour'));
await page.waitForFunction(() => /^Ready|does not|failed/.test(document.getElementById('state-game').textContent), null, { timeout: 120000 });
console.log('state: ' + await page.textContent('#state-game') + ` (${Date.now() - t0} ms)`);
await page.waitForFunction(() => !document.getElementById('play').disabled);
const tPlay = Date.now();
await page.click('#play');
await page.waitForFunction(() => window.__zhFrames > 30, null, { timeout: 90000 }).catch(() => console.log('no frames'));
console.log(`first frames ${((Date.now() - tPlay) / 1000).toFixed(1)} s after Play`);

async function point(x, y) {
	const box = await page.locator('#canvas').boundingBox();
	return { x: box.x + (x / 800) * box.width, y: box.y + (y / 600) * box.height };
}
for (const step of opt.steps.split(/\s+/).filter(Boolean)) {
	const [kind, arg = ''] = [step.split(':')[0], step.slice(step.indexOf(':') + 1)];
	const xy = () => arg.split(',').map(Number);
	if (kind === 'c') { const p = await point(...xy()); await page.mouse.move(p.x, p.y, { steps: 4 }); await page.waitForTimeout(150); await page.mouse.down(); await page.waitForTimeout(80); await page.mouse.up(); }
	else if (kind === 'w') await page.waitForTimeout(Number(arg) * 1000);
	else if (kind === 's') await page.screenshot({ path: path.join(opt.out, arg + '.png') });
}
await page.screenshot({ path: opt.shot });
logs.filter((l) => /direct file stats|direct mode|Fatal|Required|Cannot read/.test(l)).slice(0, 8).forEach((l) => console.log(l.slice(0, 250)));
const errors = await page.evaluate(() => ({ shown: !document.getElementById('errors').hidden, text: document.getElementById('errors-log').textContent.slice(0, 300) }));
console.log('error panel: ' + JSON.stringify(errors));
await browser.close();
process.exit(0);
