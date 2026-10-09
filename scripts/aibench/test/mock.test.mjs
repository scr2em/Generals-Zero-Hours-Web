// End to end test of the runner against a mock engine page (no game needed):
//   node scripts/aibench/test/mock.test.mjs
// Needs Playwright + Chromium like the real runs. Checks the matchup/seed/rotation plan, the pool, the report and
// that the determinism check passes for a deterministic engine.
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import assert from 'node:assert/strict';

const here = path.dirname(new URL(import.meta.url).pathname);
const out = fs.mkdtempSync(path.join(os.tmpdir(), 'aibench-mock-'));
const r = spawnSync('node', [path.join(here, '..', 'aibench.mjs'), '--site', path.join(here, 'mocksite'), '--profile', path.join(out, 'profile'),
	'--port', '0', '--map', 'Mock', '--matchup', 'expert:Ironwood,hard:Ironwood', '--seeds', '20', '--workers', '4', '--determinism', '3',
	'--target', 'expert:Ironwood>hard:Ironwood=0.8', '--out', path.join(out, 'report')], { encoding: 'utf8', env: process.env });
console.log(r.stdout.split('\n').slice(-16).join('\n'));
assert.equal(r.status, 0, r.stderr);
const report = JSON.parse(fs.readFileSync(path.join(out, 'report', 'report.json'), 'utf8'));
const b = report.builds.candidate;
assert.equal(b.matches, 20);
assert.equal(b.outcomes.timeout, 2);                 // seeds 10 and 20
assert.equal(b.headToHead[0].played, 18 + 2);
assert.equal(report.determinism.checked, 3);
assert.equal(report.determinism.identical, 3);
assert.ok(b.headToHead[0].xWins + b.headToHead[0].yWins === 18);
assert.ok(fs.readFileSync(path.join(out, 'report', 'report.md'), 'utf8').includes('Head to head'));
console.log('mock test passed');
