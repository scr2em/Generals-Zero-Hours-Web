// Plays a whole skirmish of the free starter content against the computer in the real game (headless Chromium) and
// checks that it ends on the score screen, with the right outcome, without an assertion, a missing string or a trap.
//
//   node starter_match.mjs --site <build/GeneralsMD> --outcome defeat|victory [--out <dir>] [--port 8961]
//                          [--ai easy|medium|hard] [--print] [-- <more starter_flow.mjs options>]
//
//   defeat    the player does nothing; the computer (Hard) builds a base, trains teams and destroys the headquarters
//   victory   the player builds a power plant and two barracks, trains rocketeers (cheap, good against buildings) and
//             attack-moves twelve of them into the computer's (Easy) base
//
// It writes the steps to <out>/<outcome>.steps and runs starter_flow.mjs on them (see its header for the step language),
// so every other option of starter_flow.mjs works too after a "--" (for instance "-- --profile dir").
// A match needs about 9000 (defeat) or 6000 (victory) logic frames; the game runs at 30 frames per second in real time,
// but a software rendered headless browser delivers 4 to 15 frames per second, so allow half an hour to an hour.
// The coordinates are those of the game at its default resolution (1024x768) in the default browser window of
// starter_flow.mjs (1100x800): the picture of the 3D view, and so every click on the map, depends on the resolution.
//
// Needs the same things as starter_flow.mjs (Playwright, a Chromium). Exit code 0 = the match ended as expected.
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';

const opt = { outcome: 'defeat', out: '.', port: 8961, ai: null, print: false, rest: [] };
{
	const a = process.argv.slice(2);
	for (let i = 0; i < a.length; ++i) {
		switch (a[i]) {
			case '--site': opt.site = a[++i]; break;
			case '--outcome': opt.outcome = a[++i]; break;
			case '--out': opt.out = a[++i]; break;
			case '--port': opt.port = Number(a[++i]); break;
			case '--ai': opt.ai = a[++i]; break;
			case '--print': opt.print = true; break;
			case '--': opt.rest = a.slice(i + 1); i = a.length; break;
			default: console.error('unknown option ' + a[i]); process.exit(2);
		}
	}
	if (!opt.site || !['defeat', 'victory'].includes(opt.outcome)) { console.error('usage: --site DIR --outcome defeat|victory'); process.exit(2); }
}
fs.mkdirSync(opt.out, { recursive: true });

// Design coordinates (800x600) of the starter content's screens; tools/wnd_pos.py prints the centres of buttons.
const MAIN_SKIRMISH = 'c:185,221';
const AI_SLOT_ARROW = 'c:181,197';                           // the combo box of the second slot
const AI_CHOICE = { easy: 'c:100,249', medium: 'c:100,264', hard: 'c:100,278' };   // rows of its list (Open, Closed, Easy, Medium, Hard)
const START_POSITION_1 = 'c:613,263';                        // the marker of the lower left start on the map preview: the player starts there
const CASH_ARROW = 'c:775,429', CASH_10000 = 'c:588,481';
const START = 'c:699,566';
const HOME = 'k:Home';                                       // centres the camera on the headquarters: a fixed picture
// With the camera on the headquarters (at 400,206): the power plant goes lower left, the barracks to the right.
const BUILD_POWER = 'c:202,486', BUILD_BARRACKS = 'c:258,486', TRAIN_1 = 'c:202,486', TRAIN_2 = 'c:258,486';
const NEXT_IDLE_WORKER = 'k:Period';
const RALLY_BUTTON = 'c:482,530', ATTACK_MOVE_BUTTON = 'c:202,486';

const ai = opt.ai || (opt.outcome === 'defeat' ? 'hard' : 'easy');
const steps = [];
const say = (...s) => steps.push(...s);

// ---- the setup screen and the start
say('w:3', 'm:300,300', 'w:1', MAIN_SKIRMISH, 'W:Shell:push\\(Menus/SkirmishGameOptionsMenu', 'w:3',
	AI_SLOT_ARROW, 'w:1', AI_CHOICE[ai], 'w:1');
if (opt.outcome === 'victory') say(CASH_ARROW, 'w:1', CASH_10000, 'w:1');
say(START_POSITION_1, 'w:1', 's:setup', START, 'W:Appended~CRC~on~frame~0', HOME, 'F:200', 's:start');

// ---- in the game: Escape opens the in-game menu and closes it again
say('k:Escape', 'w:4', 's:escape-menu', 'k:Escape', 'w:3');

if (opt.outcome === 'victory') {
	// With the camera on the headquarters (k:Home, then wait for the camera): the headquarters is at 400,206. "Next idle
	// worker" (the period key) also moves the camera to that worker, so every order starts with Home and a pause.
	const settle = ['k:Home', 'F:200'];
	const place = (button, x, y) => [NEXT_IDLE_WORKER, ...settle, button, 'w:1', 'm:' + x + ',' + y, 'w:1', 'c:' + x + ',' + y, 'F:100'];
	const queue6 = Array(6).fill(TRAIN_2);          // a barracks queues six units
	const barracksA = 'c:602,315', barracksB = 'c:437,390';
	// the rally point of both barracks: open ground south of the headquarters, inside the picture; the units gather there
	const rally = [RALLY_BUTTON, 'w:1', 'c:470,330', 'w:1'];
	const armyBox = 'r:380,250,580,400';
	say(...place(BUILD_POWER, 200, 330), 's:v-power');
	say('f:1000', ...place(BUILD_BARRACKS, 600, 330), ...place(BUILD_BARRACKS, 440, 400), 's:v-barracks');
	// both barracks stand at about frame 2200: rally point, six rocketeers each, ready at about frame 3900
	say('f:2300', ...settle, barracksA, 'w:1', ...rally, ...queue6, 's:v-queue-a', barracksB, 'w:1', ...rally, ...queue6, 's:v-queue-b');
	// the first twelve attack, six more from each barracks (the computer attacks at about two minutes) gather at the rally
	// point behind them. Box select the army, attack move to the open ground in front of the computer's base (the opposite
	// corner of the minimap; a destination inside a building cannot be reached).
	const attack = (shot) => [...settle, armyBox, 'w:1', 's:' + shot + '-selected', ATTACK_MOVE_BUTTON, 'w:1', 'c:108,497', 'w:2', 's:' + shot];
	// Once they are there: a click on the minimap puts the camera on the computer's base, box select what stands in the
	// picture and order an attack on the headquarters (a right click on it). Units that stand idle near buildings shoot at
	// them by themselves (Attack_Buildings in their AI module), so the rest of the base falls too.
	const assault = (shot) => ['c:122,483', 'F:200', 's:' + shot + '-view', 'r:20,40,780,420', 'w:1', 'R:400,215', 'F:300', 's:' + shot];
	say('f:3900', ...settle, barracksA, 'w:1', ...queue6, barracksB, 'w:1', ...queue6, 's:v-army');
	say('f:4200', ...attack('v-attack'));
	say('f:6000', ...assault('v-assault'));
	say('f:7500', ...assault('v-assault-2'));
	say('f:9000', ...assault('v-assault-3'));
	}

// ---- the end: the banner, then the score screen
const endWord = opt.outcome === 'victory' ? 'victory' : 'defeat';
say('W:Shell:push\\(Menus/ScoreScreen', 'w:12', 's:score-' + endWord, 'w:20', 's:score-' + endWord + '-2');
// the match must have been clean
say('N:ASSERTION~FAILURE', 'N:MISSING:~\'', 'N:Assertion~failed',
	'e:window.zhWebAudio.stats.errors===0&&window.zhWebAudio.stats.sourcesStarted>0&&window.zhWebAudio.stats.streamChunks>0',
	'v:JSON.stringify(window.zhWebAudio.stats)');

const stepsFile = path.join(opt.out, opt.outcome + '.steps');
fs.writeFileSync(stepsFile, steps.join('\n') + '\n');
if (opt.print) { console.log(steps.join('\n')); process.exit(0); }

const here = path.dirname(new URL(import.meta.url).pathname);
const child = spawn('node', [path.join(here, 'starter_flow.mjs'), '--site', opt.site, '--port', String(opt.port), '--out', opt.out,
	'--log', path.join(opt.out, opt.outcome + '.log'),
	'--steps-file', stepsFile, ...opt.rest], { stdio: 'inherit' });
child.on('exit', (code) => process.exit(code === null ? 1 : code));
