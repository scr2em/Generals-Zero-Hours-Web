#!/usr/bin/env python3
"""Generates test videos with make_test_bik.py, decodes them with bik_decode_test (the FFmpeg
built for WebAssembly, run with node, or a native build) and compares the result with what the
generator says the video contains.

  run_decode_test.py --runner "node /path/bik_decode_test.js"   [--keep DIR]

Exit status 0: all checks passed.
"""
import argparse
import json
import os
import shlex
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
failures = []


def check(cond, what):
    print(('ok    ' if cond else 'FAIL  ') + what)
    if not cond:
        failures.append(what)


def run_case(runner, workdir, name, gen_args, seekable):
    path = os.path.join(workdir, name + '.bik')
    subprocess.check_call([sys.executable, os.path.join(HERE, 'make_test_bik.py'), path] + gen_args,
                          stdout=subprocess.DEVNULL)
    expect = json.load(open(path + '.json'))
    cmd = shlex.split(runner) + [path] + (['seekable'] if seekable else [])
    out = subprocess.run(cmd, capture_output=True, text=True)
    kv = {}
    frames = {}
    for line in out.stdout.splitlines():
        parts = line.split()
        if not parts:
            continue
        if parts[0] == 'frame':
            frames[int(parts[1])] = {'y': [int(p) for p in parts[3:7]], 'u': int(parts[8]), 'v': int(parts[10])}
        else:
            key = parts[0] if parts[0] != 'stream' else 'stream' + parts[1]
            kv[key] = ' '.join(parts[1:])
    label = '%s%s' % (name, ' (seekable)' if seekable else '')
    check(out.returncode == 0 and kv.get('done') == '1', '%s: decoder ran to the end' % label)
    check(kv.get('format') == 'bink', '%s: probed as bink' % label)
    check(int(kv.get('width', 0)) == expect['width'] and int(kv.get('height', 0)) == expect['height'],
          '%s: size %sx%s' % (label, expect['width'], expect['height']))
    check(kv.get('pix_fmt') == 'yuv420p', '%s: pixel format yuv420p' % label)
    check(int(kv.get('video_frames', -1)) == expect['frames'], '%s: %d video frames (got %s)' % (
        label, expect['frames'], kv.get('video_frames')))
    check(int(kv.get('duration_frames', -1)) == expect['frames'], '%s: stream duration is the frame count' % label)
    bad = [b['n'] for b in expect['blocks'] if frames.get(b['n']) != {'y': b['y'], 'u': b['u'], 'v': b['v']}]
    check(not bad, '%s: all frames decode to the expected pixels%s' % (label, '' if not bad else ' (bad: %s)' % bad[:5]))
    if seekable:
        check(int(kv.get('rewind_frames', -1)) == expect['frames'] and int(kv.get('rewind_first_y', -1)) == expect['blocks'][0]['y'][0],
              '%s: after a rewind the video decodes again from the first frame' % label)
    a = expect['audio']
    if a['tracks']:
        check(int(kv.get('audio_rate', 0)) == a['rate'], '%s: audio rate %d' % (label, a['rate']))
        check(int(kv.get('audio_channels', 0)) == a['channels'], '%s: audio channels %d' % (label, a['channels']))
        check(a['codec'] in kv.get('stream1', ''), '%s: audio decoder %s' % (label, a['codec']))
        got = int(kv.get('audio_samples', 0))
        check(got == a['samples_per_channel'],
              '%s: %d audio samples per channel decoded (got %d)' % (label, a['samples_per_channel'], got))
        peak = float(kv.get('audio_peak', 0))
        if a['silent']:
            check(peak == 0.0, '%s: silent audio is silent' % label)
        else:
            check(0.3 < peak < 0.8, '%s: tone amplitude (peak %.3f)' % (label, peak))
            seconds = int(kv['audio_samples']) / float(a['rate'])
            if a['channels'] == 1:
                freq = int(kv['audio_crossings']) / 2.0 / seconds
                check(abs(freq - a['tone_hz']) / a['tone_hz'] < 0.06,
                      '%s: tone frequency %.0f Hz (expected %.0f)' % (label, freq, a['tone_hz']))
    else:
        check('audio_rate' not in kv, '%s: no audio track' % label)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--runner', required=True, help='command that runs bik_decode_test')
    ap.add_argument('--keep', help='directory to keep the generated files in')
    args = ap.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        workdir = args.keep or tmp
        os.makedirs(workdir, exist_ok=True)
        cases = [
            ('mono', ['--frames', '90'], False),
            ('mono', ['--frames', '90'], True),
            ('stereo_odd_size', ['--width', '322', '--height', '242', '--channels', '2', '--frames', '40'], False),
            ('silent_dct', ['--audio', 'dct', '--frames', '30'], False),
            ('no_audio', ['--audio', 'none', '--frames', '35', '--fps', '15'], False),
            ('vga', ['--width', '640', '--height', '480', '--frames', '40', '--rate', '44100'], False),
        ]
        for name, gen, seekable in cases:
            run_case(args.runner, workdir, name, gen, seekable)
    print('\n%d check(s) failed' % len(failures) if failures else '\nall checks passed')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
