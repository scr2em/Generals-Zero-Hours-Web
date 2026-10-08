// Runs the scenes of web_d3d8_test (default, --shaders, --scene2, --scene3) and compares the hash of the
// final frame with tests/reference_hashes.json, so that any change of the rendered pixels is noticed.
//
//   node run_regression.mjs <build-dir> [--update]
//
// The hashes are those of headless Chromium's SwiftShader; another software renderer or GPU may differ in
// the last bit. --update rewrites the reference file after an intended change (review the screenshots first).
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const args = process.argv.slice(2);
const buildDir = args.find((a) => !a.startsWith('--'));
const update = args.includes('--update');
if (!buildDir) { console.error('usage: node run_regression.mjs <build-dir> [--update]'); process.exit(2); }
const refFile = path.join(here, 'reference_hashes.json');
const refs = fs.existsSync(refFile) ? JSON.parse(fs.readFileSync(refFile, 'utf8')) : {};
const modes = { default: [], shaders: ['--shaders'], scene2: ['--scene2'], scene3: ['--scene3'], 'default-nos3tc': ['--no-s3tc'] };
let bad = 0;
for (const [name, a] of Object.entries(modes)) {
  const r = spawnSync('node', [path.join(here, 'run_test.mjs'), buildDir, `/tmp/webd3d8_${name}.png`, ...a], { encoding: 'utf8' });
  const m = /FRAME_HASH ([0-9a-f]+)/.exec(r.stdout);
  const hash = m ? m[1] : null;
  if (update) { refs[name] = hash; console.log(`${name}: ${hash} (updated)`); continue; }
  const ok = hash && refs[name] === hash;
  console.log(`${name}: ${hash} ${ok ? 'OK' : 'MISMATCH (reference ' + refs[name] + ')'}`);
  if (!ok) bad++;
}
if (update) fs.writeFileSync(refFile, JSON.stringify(refs, null, 2) + '\n');
console.log(bad ? `RESULT: ${bad} scene(s) differ` : 'RESULT: all scenes match');
process.exit(bad ? 1 : 0);
