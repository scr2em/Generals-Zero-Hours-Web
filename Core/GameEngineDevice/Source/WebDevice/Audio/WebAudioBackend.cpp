/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// FILE: WebAudioBackend.cpp //////////////////////////////////////////////////
//
// See WebAudioBackend.h for the design. This file has three parts:
//
//  1. The JavaScript side (EM_JS), which only ever runs on the main browser thread: an
//     interpreter of the command queue that owns the AudioContext, the voices and the buffers.
//  2. The command queue and the shared state block on the C++ side.
//  3. The stream decoder thread.
//
// Command encoding: a batch is an array of 32 bit words; floats are stored as their bit
// pattern. A command is the opcode followed by a fixed number of words (see the table in
// wa_js_install). Commands that carry PCM own a malloc'ed block, which the interpreter frees
// (through the C function passed to wa_js_install) after copying it.
//
///////////////////////////////////////////////////////////////////////////////

#include "WebDevice/Audio/WebAudioBackend.h"
#include "WebDevice/Audio/WebAudioDecoder.h"

#include <emscripten.h>
#include <emscripten/proxying.h>
#include <emscripten/threading.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string.h>
#include <thread>
#include <vector>

EM_JS_DEPS(web_audio_deps, "$getWasmTableEntry");

// Shared state ---------------------------------------------------------------------------------
//
// Written by the main thread (JavaScript), read by the engine and decoder threads. The layout is
// mirrored in wa_js_install: do not change one without the other.

namespace
{

struct VoiceShared
{
	int32_t segmentsFinished;	// number of segments that have ended (or were cancelled/dropped)
	int32_t loopsDone;
	int32_t generation;				// generation of the voice id the main thread has set up; 0 = not live
	int32_t reserved;
	double endWallMs;					// Date.now() time at which the queued segments end; 0 none, 1e18 paused
	double reserved2;
};

struct Shared
{
	int32_t contextState;
	int32_t sampleRate;
	int32_t reserved0;
	int32_t reserved1;
	double wallOffsetMs;			// Date.now() minus the context clock
	double contextTimeMs;
	VoiceShared voices[WEBAUDIO_MAX_VOICES];
};

static_assert(sizeof(VoiceShared) == 32, "mirrored in JavaScript");
static_assert(offsetof(Shared, voices) == 32, "mirrored in JavaScript");
static_assert(WEBAUDIO_MAX_VOICES <= 4096, "voice index has 12 bits");

alignas(8) Shared g_shared;

} // namespace

// ============================================================================================
// 1. The main thread side
// ============================================================================================

// Installs the interpreter. Runs on the main browser thread. Safe to call twice.
EM_JS(int, wa_js_install, (void *shared, int sampleRate, int freeFn), {
	if (globalThis.zhWebAudio && globalThis.zhWebAudio.ctx) {
		return 1;
	}
	var AC = globalThis.AudioContext || globalThis.webkitAudioContext;
	if (!AC) {
		return 0;
	}

	var MAX_VOICES = 512;
	var A = {
		ctx: null,
		master: null,
		shared: shared,
		freeFn: freeFn,
		voices: new Array(MAX_VOICES),
		buffers: new Map(),
		hrtf: false,
		unlockEvents: ['pointerdown', 'pointerup', 'mousedown', 'touchstart', 'touchend', 'keydown', 'click'],
		stats: { commandsRun: 0, buffersCreated: 0, sourcesStarted: 0, sourcesEnded: 0, voicesCreated: 0, streamChunks: 0, segmentsDropped: 0, errors: 0 },
		timer: 0
	};
	for (var vi = 0; vi < MAX_VOICES; ++vi) A.voices[vi] = null;

	var ctx;
	try {
		ctx = sampleRate > 0 ? new AC({ latencyHint: 'interactive', sampleRate: sampleRate }) : new AC({ latencyHint: 'interactive' });
	} catch (e) {
		try { ctx = new AC(); } catch (e2) { return 0; }
	}
	A.ctx = ctx;
	A.master = ctx.createGain();
	A.master.connect(ctx.destination);

	// Shared block accessors (the heap views are re-read on every use: they change when memory grows).
	var HDR_I = shared >> 2;
	var HDR_F = shared >> 3;
	function vI(idx) { return (shared >> 2) + 8 + idx * 8; }
	function vF(idx) { return (shared >> 3) + 4 + idx * 4; }

	function publishState() {
		var s = ctx.state === 'running' ? 2 : (ctx.state === 'closed' ? 3 : 1);
		Atomics.store(HEAP32, HDR_I, s);
		Atomics.store(HEAP32, HDR_I + 1, ctx.sampleRate | 0);
		HEAPF64[HDR_F + 2] = Date.now() - ctx.currentTime * 1000;
		HEAPF64[HDR_F + 3] = ctx.currentTime * 1000;
	}
	A.publishState = publishState;

	function publishVoice(v) {
		var now = ctx.currentTime;
		var end = 0;
		if (v.paused) end = 1e18;
		else if (v.endCtx > now) end = Date.now() - now * 1000 + v.endCtx * 1000;
		HEAPF64[vF(v.idx) + 1] = end;
	}
	A.publishVoice = publishVoice;

	function fail(where, e) {
		A.stats.errors++;
		if (A.stats.errors < 20) console.warn('zhWebAudio: ' + where + ': ' + e);
	}

	function freeBlock(p) {
		if (p) getWasmTableEntry(freeFn)(p);
	}

	// Context state and the autoplay unlock ----------------------------------------------------
	function onUnlock() {
		if (ctx.state === 'running') {
			removeUnlock();
			return;
		}
		var p = ctx.resume();
		if (p && p.catch) p.catch(function () {});
	}
	function removeUnlock() {
		if (typeof document === 'undefined') return;
		for (var i = 0; i < A.unlockEvents.length; ++i) document.removeEventListener(A.unlockEvents[i], onUnlock, true);
	}
	if (typeof document !== 'undefined') {
		for (var ei = 0; ei < A.unlockEvents.length; ++ei) document.addEventListener(A.unlockEvents[ei], onUnlock, true);
		document.addEventListener('visibilitychange', function () {
			// Safari "interrupted" state after a phone call or the like
			if (document.visibilityState === 'visible' && ctx.state !== 'running' && ctx.state !== 'closed') onUnlock();
		});
	}
	ctx.onstatechange = function () {
		publishState();
		if (ctx.state === 'running') removeUnlock();
	};
	A.resume = function () {
		var p = ctx.resume();
		if (p && p.catch) p.catch(function () {});
	};
	publishState();
	if (ctx.state !== 'running') {
		// Chrome lets a context created after a user gesture of the page run at once; others need resume() in a gesture.
		A.resume();
	}

	// Periodic refresh of the clock mapping and the end times (end times depend on it).
	A.tick = function () {
		publishState();
		for (var i = 0; i < MAX_VOICES; ++i) {
			var v = A.voices[i];
			if (v) publishVoice(v);
		}
	};
	A.timer = setInterval(A.tick, 20);

	// Voices -----------------------------------------------------------------------------------
	function createVoice(id, kind) {
		var idx = id & 0xFFF;
		var old = A.voices[idx];
		if (old) destroyVoice(old, 0.004);
		var v = {
			id: id, idx: idx, kind: kind, segs: [], pitch: 1, vol: 1, started: false, paused: false, dead: false,
			endCtx: 0, input: null, lp: null, panner: null, sp: null, gain: null, tail: null
		};
		v.input = ctx.createGain();
		v.gain = ctx.createGain();
		v.gain.gain.value = 1;
		if (kind === 1) {
			v.lp = ctx.createBiquadFilter();
			v.lp.type = 'lowpass';
			v.lp.frequency.value = ctx.sampleRate / 2;
			v.lp.Q.value = 0.7071;
			var p = ctx.createPanner();
			p.panningModel = A.hrtf ? 'HRTF' : 'equalpower';
			// The game attenuates by distance itself (AudioManager::getEffectiveVolume), the panner only pans.
			p.distanceModel = 'inverse';
			p.refDistance = 1;
			p.maxDistance = 1e9;
			p.rolloffFactor = 0;
			p.coneInnerAngle = 360;
			p.coneOuterAngle = 360;
			p.coneOuterGain = 1;
			v.panner = p;
			v.input.connect(v.lp);
			v.lp.connect(p);
			p.connect(v.gain);
		} else {
			v.input.connect(v.gain);
		}
		v.gain.connect(A.master);
		A.voices[idx] = v;
		// Reset the shared counters and tell the engine thread the voice exists (generation last).
		var i = vI(idx);
		Atomics.store(HEAP32, i, 0);
		Atomics.store(HEAP32, i + 1, 0);
		HEAPF64[vF(idx) + 1] = 0;
		Atomics.store(HEAP32, i + 2, id >>> 12);
		A.stats.voicesCreated++;
	}

	function getVoice(id) {
		var v = A.voices[id & 0xFFF];
		return (v && v.id === id) ? v : null;
	}

	function killSource(seg) {
		if (seg.src) {
			seg.src.onended = null;
			try { seg.src.stop(); } catch (e) {}
			try { seg.src.disconnect(); } catch (e) {}
			seg.src = null;
		}
	}

	function finishSegment(v, seg) {
		var k = v.segs.indexOf(seg);
		if (k >= 0) v.segs.splice(k, 1);
		var i = vI(v.idx);
		if (seg.flags & 1) Atomics.add(HEAP32, i + 1, 1);
		Atomics.add(HEAP32, i, 1);
		publishVoice(v);
	}

	function destroyVoice(v, fade) {
		if (v.dead) return;
		v.dead = true;
		if (A.voices[v.idx] === v) A.voices[v.idx] = null;
		Atomics.store(HEAP32, vI(v.idx) + 2, 0);
		var t = ctx.currentTime;
		fade = Math.max(fade, 0.004);
		try {
			v.gain.gain.cancelScheduledValues(t);
			v.gain.gain.setValueAtTime(v.gain.gain.value, t);
			v.gain.gain.linearRampToValueAtTime(0, t + fade);
		} catch (e) {}
		for (var i = 0; i < v.segs.length; ++i) {
			var s = v.segs[i];
			if (s.src) {
				s.src.onended = null;
				try { s.src.stop(t + fade + 0.002); } catch (e) {}
			}
		}
		v.segs = [];
		setTimeout(function () {
			try { v.input.disconnect(); } catch (e) {}
			try { v.gain.disconnect(); } catch (e) {}
			if (v.lp) try { v.lp.disconnect(); } catch (e) {}
			if (v.panner) try { v.panner.disconnect(); } catch (e) {}
			if (v.sp) try { v.sp.disconnect(); } catch (e) {}
		}, (fade + 0.1) * 1000);
	}

	function startSource(v, seg, when, offset) {
		var src = ctx.createBufferSource();
		src.buffer = seg.buf;
		src.playbackRate.value = v.pitch;
		src.connect(v.input);
		src.onended = function () {
			if (v.dead || seg.src !== src) return;
			seg.src = null;
			try { src.disconnect(); } catch (e) {}
			A.stats.sourcesEnded++;
			finishSegment(v, seg);
		};
		if (offset > 0) src.start(when, offset);
		else src.start(when);
		seg.src = src;
		seg.startAt = when;
		seg.offset = offset;
		seg.rate = v.pitch;
		seg.end = when + (seg.dur - offset) / v.pitch;
		A.stats.sourcesStarted++;
	}

	function queueSegment(v, buf, delay, flags, isStream) {
		var i = vI(v.idx);
		if (!isStream && ctx.state !== 'running') {
			// Sound effects triggered while the browser still blocks audio would play all at once on the first click.
			A.stats.segmentsDropped++;
			Atomics.add(HEAP32, i, 1);
			return;
		}
		var seg = { buf: buf, dur: buf.duration, flags: flags, src: null, startAt: 0, offset: 0, rate: 1, end: 0, delay: delay, begun: false };
		v.segs.push(seg);
		if (!v.paused) {
			var now = ctx.currentTime;
			var when;
			if (v.endCtx > now) when = v.endCtx;				// gapless behind the previous segment
			else when = now + (isStream ? 0.04 : 0.002);		// idle (streams get a little lead so the next chunk can follow in time)
			when += delay;
			startSource(v, seg, when, 0);
			v.endCtx = seg.end;
		} else {
			seg.end = 0;
		}
		v.started = true;
		publishVoice(v);
	}

	function pauseVoice(v, pause) {
		if (v.paused === pause) return;
		var now = ctx.currentTime;
		if (pause) {
			v.paused = true;
			var done = [];
			for (var i = 0; i < v.segs.length; ++i) {
				var s = v.segs[i];
				if (!s.src) continue;
				var progressed = s.offset + Math.max(0, now - s.startAt) * s.rate;
				var wait = Math.max(0, s.startAt - now);
				s.begun = s.startAt <= now;
				killSource(s);
				if (progressed >= s.dur - 0.0005) done.push(s);
				else { s.offset = progressed; s.delay = wait; }
			}
			for (var d = 0; d < done.length; ++d) {
				A.stats.sourcesEnded++;
				finishSegment(v, done[d]);
			}
			publishVoice(v);
		} else {
			v.paused = false;
			var t = now + 0.005;
			for (var j = 0; j < v.segs.length; ++j) {
				var sg = v.segs[j];
				var when = t + sg.delay;
				sg.delay = 0;
				startSource(v, sg, when, sg.offset);
				t = sg.end;
			}
			v.endCtx = v.segs.length ? v.segs[v.segs.length - 1].end : 0;
			publishVoice(v);
		}
	}

	function cancelPending(v) {
		var now = ctx.currentTime;
		var keep = [];
		var cancelled = [];
		for (var i = 0; i < v.segs.length; ++i) {
			var s = v.segs[i];
			var begun = s.src ? s.startAt <= now : s.begun;
			if (!begun) cancelled.push(s);
			else keep.push(s);
		}
		for (var c = 0; c < cancelled.length; ++c) {
			killSource(cancelled[c]);
			var k = v.segs.indexOf(cancelled[c]);
			if (k >= 0) v.segs.splice(k, 1);
			Atomics.add(HEAP32, vI(v.idx), 1);
		}
		v.endCtx = v.segs.length ? v.segs[v.segs.length - 1].end : 0;
		publishVoice(v);
	}

	function setPan(v, pan) {
		if (v.kind === 1 || !(pan === pan)) return;
		if (!v.sp) {
			if (pan === 0 || !ctx.createStereoPanner) return;
			v.sp = ctx.createStereoPanner();
			v.input.disconnect(v.gain);
			v.input.connect(v.sp);
			v.sp.connect(v.gain);
		}
		v.sp.pan.setTargetAtTime(Math.max(-1, Math.min(1, pan)), ctx.currentTime, 0.01);
	}

	function setParam(param, value, instant) {
		if (instant) param.value = value;
		else param.setTargetAtTime(value, ctx.currentTime, 0.01);
	}

	function makeBuffer(ptr, frames, ch, rate) {
		var b = ctx.createBuffer(ch, frames, rate);
		var base = ptr >> 2;
		for (var c = 0; c < ch; ++c) {
			b.getChannelData(c).set(HEAPF32.subarray(base + c * frames, base + (c + 1) * frames));
		}
		return b;
	}

	function setListener(a) {
		var L = ctx.listener;
		if (L.positionX) {
			L.positionX.value = a[0]; L.positionY.value = a[1]; L.positionZ.value = a[2];
			L.forwardX.value = a[3]; L.forwardY.value = a[4]; L.forwardZ.value = a[5];
			L.upX.value = a[6]; L.upY.value = a[7]; L.upZ.value = a[8];
		} else {
			L.setPosition(a[0], a[1], a[2]);
			L.setOrientation(a[3], a[4], a[5], a[6], a[7], a[8]);
		}
	}

	// The command interpreter ------------------------------------------------------------------
	// Opcode -> number of argument words.
	var ARGS = [0, 5, 1, 2, 2, 4, 6, 2, 2, 2, 4, 2, 2, 1, 9, 1, 1, 0];
	// 1 CREATE_BUFFER id ptr frames ch rate     2 DESTROY_BUFFER id        3 CREATE_VOICE id kind
	// 4 DESTROY_VOICE id fade(f)                5 QUEUE voice buffer delay(f) flags
	// 6 STREAM_CHUNK voice ptr frames ch rate flags     7 SET_GAIN voice g(f)   8 SET_PITCH voice r(f)
	// 9 SET_PAN voice p(f)    10 SET_POS voice x y z (f)    11 SET_LOWPASS voice hz(f)    12 PAUSE voice flag
	// 13 CANCEL_PENDING voice    14 LISTENER 9 floats    15 MASTER_GAIN g(f)    16 PANNING_MODEL m    17 RESUME
	A.exec = function (ptr, nwords) {
		var I = HEAP32, F = HEAPF32;
		var i = ptr >> 2, end = i + nwords;
		while (i < end) {
			var op = I[i++];
			var n = ARGS[op];
			if (n === undefined) { fail('exec', 'bad opcode ' + op); return; }
			var a = i;
			i += n;
			A.stats.commandsRun++;
			try {
				switch (op) {
					case 1: {	// CREATE_BUFFER
						var p = I[a + 1];
						try {
							var b = makeBuffer(p, I[a + 2], I[a + 3], I[a + 4]);
							A.buffers.set(I[a], b);
							A.stats.buffersCreated++;
						} finally { freeBlock(p); }
						break;
					}
					case 2: A.buffers.delete(I[a]); break;
					case 3: createVoice(I[a], I[a + 1]); break;
					case 4: { var v4 = getVoice(I[a]); if (v4) destroyVoice(v4, F[a + 1]); break; }
					case 5: {	// QUEUE
						var v5 = getVoice(I[a]);
						if (!v5) break;
						var b5 = A.buffers.get(I[a + 1]);
						if (!b5) { Atomics.add(HEAP32, vI(v5.idx), 1); break; }
						queueSegment(v5, b5, F[a + 2], I[a + 3], false);
						break;
					}
					case 6: {	// STREAM_CHUNK
						var p6 = I[a + 1];
						var v6 = getVoice(I[a]);
						try {
							if (v6) {
								var b6 = makeBuffer(p6, I[a + 2], I[a + 3], I[a + 4]);
								A.stats.streamChunks++;
								queueSegment(v6, b6, 0, I[a + 5], true);
							}
						} finally { freeBlock(p6); }
						break;
					}
					case 7: {
						var v7 = getVoice(I[a]);
						if (v7) { setParam(v7.gain.gain, F[a + 1], !v7.started); v7.vol = F[a + 1]; }
						break;
					}
					case 8: { var v8 = getVoice(I[a]); if (v8) v8.pitch = Math.max(0.01, F[a + 1]); break; }
					case 9: { var v9 = getVoice(I[a]); if (v9) setPan(v9, F[a + 1]); break; }
					case 10: {
						var v10 = getVoice(I[a]);
						if (v10 && v10.panner) {
							var P = v10.panner;
							if (P.positionX) { P.positionX.value = F[a + 1]; P.positionY.value = F[a + 2]; P.positionZ.value = F[a + 3]; }
							else P.setPosition(F[a + 1], F[a + 2], F[a + 3]);
						}
						break;
					}
					case 11: {
						var v11 = getVoice(I[a]);
						if (v11 && v11.lp) {
							var hz = F[a + 1];
							setParam(v11.lp.frequency, hz > 0 ? Math.min(hz, ctx.sampleRate / 2) : ctx.sampleRate / 2, false);
						}
						break;
					}
					case 12: { var v12 = getVoice(I[a]); if (v12) pauseVoice(v12, I[a + 1] !== 0); break; }
					case 13: { var v13 = getVoice(I[a]); if (v13) cancelPending(v13); break; }
					case 14: setListener([F[a], F[a + 1], F[a + 2], F[a + 3], F[a + 4], F[a + 5], F[a + 6], F[a + 7], F[a + 8]]); break;
					case 15: A.master.gain.setTargetAtTime(F[a], ctx.currentTime, 0.01); break;
					case 16: {
						A.hrtf = I[a] !== 0;
						for (var k = 0; k < MAX_VOICES; ++k) {
							var vk = A.voices[k];
							if (vk && vk.panner) vk.panner.panningModel = A.hrtf ? 'HRTF' : 'equalpower';
						}
						break;
					}
					case 17: A.resume(); break;
				}
			} catch (e) {
				fail('command ' + op, e);
			}
		}
	};

	A.shutdown = function () {
		clearInterval(A.timer);
		removeUnlock();
		for (var k = 0; k < MAX_VOICES; ++k) if (A.voices[k]) destroyVoice(A.voices[k], 0.01);
		A.buffers.clear();
		Atomics.store(HEAP32, HDR_I, 0);
		var c = ctx;
		setTimeout(function () { try { c.close(); } catch (e) {} }, 200);
		A.ctx = null;
	};

	globalThis.zhWebAudio = A;
	return 1;
});

EM_JS(void, wa_js_exec, (int ptr, int nwords), {
	var A = globalThis.zhWebAudio;
	if (A && A.ctx) A.exec(ptr, nwords);
});

EM_JS(void, wa_js_shutdown, (), {
	var A = globalThis.zhWebAudio;
	if (A && A.ctx) A.shutdown();
	globalThis.zhWebAudio = null;
});

EM_JS(void, wa_js_stats, (uint32_t *out), {
	var A = globalThis.zhWebAudio;
	var s = A ? A.stats : null;
	var o = out >> 2;
	var keys = ['commandsRun', 'buffersCreated', 'sourcesStarted', 'sourcesEnded', 'voicesCreated', 'streamChunks', 'segmentsDropped', 'errors'];
	for (var i = 0; i < keys.length; ++i) HEAPU32[o + i] = s ? s[keys[i]] : 0;
});

// ============================================================================================
// 2. The command queue (C++ side)
// ============================================================================================

namespace
{

enum Op : int32_t
{
	OP_CREATE_BUFFER = 1,
	OP_DESTROY_BUFFER = 2,
	OP_CREATE_VOICE = 3,
	OP_DESTROY_VOICE = 4,
	OP_QUEUE = 5,
	OP_STREAM_CHUNK = 6,
	OP_SET_GAIN = 7,
	OP_SET_PITCH = 8,
	OP_SET_PAN = 9,
	OP_SET_POSITION = 10,
	OP_SET_LOWPASS = 11,
	OP_PAUSE = 12,
	OP_CANCEL_PENDING = 13,
	OP_LISTENER = 14,
	OP_MASTER_GAIN = 15,
	OP_PANNING_MODEL = 16,
	OP_RESUME = 17,
};

struct Backend
{
	std::mutex lock;								// guards the command buffer and the voice allocation
	std::vector<int32_t> commands;
	em_proxying_queue *queue = nullptr;
	std::atomic<bool> available{false};

	uint32_t generation[WEBAUDIO_MAX_VOICES] = {};
	bool inUse[WEBAUDIO_MAX_VOICES] = {};
	std::atomic<int32_t> submitted[WEBAUDIO_MAX_VOICES] = {};	// segments queued on the voice
	uint32_t nextVoiceSearch = 0;
	std::atomic<uint32_t> nextBuffer{1};
};

Backend g_backend;

inline int32_t bits(float f)
{
	int32_t i;
	memcpy(&i, &f, 4);
	return i;
}

inline uint32_t voiceIndex(WebAudioVoice v) { return v & 0xFFFu; }
inline uint32_t voiceGeneration(WebAudioVoice v) { return v >> 12; }

/// Whether the main thread has set the voice up (and not destroyed it since).
inline bool voiceLive(WebAudioVoice v)
{
	if (v == 0 || voiceIndex(v) >= WEBAUDIO_MAX_VOICES)
		return false;
	return __atomic_load_n(&g_shared.voices[voiceIndex(v)].generation, __ATOMIC_ACQUIRE) == (int32_t)voiceGeneration(v);
}

/// Whether this id still names the voice slot (it is not destroyed or reused). Does not take the lock: only
/// meant for ids held by their owner.
inline bool voiceAllocated(WebAudioVoice v)
{
	if (v == 0 || voiceIndex(v) >= WEBAUDIO_MAX_VOICES)
		return false;
	return g_backend.inUse[voiceIndex(v)] && g_backend.generation[voiceIndex(v)] == voiceGeneration(v);
}

void push(std::initializer_list<int32_t> words)
{
	std::lock_guard<std::mutex> g(g_backend.lock);
	g_backend.commands.insert(g_backend.commands.end(), words.begin(), words.end());
}

void freeBlock(void *p)
{
	free(p);
}

void runBatch(void *arg)
{
	std::vector<int32_t> *batch = static_cast<std::vector<int32_t> *>(arg);
	wa_js_exec((int)(intptr_t)batch->data(), (int)batch->size());
	delete batch;
}

void flushLocked()
{
	if (g_backend.commands.empty() || !g_backend.queue)
		return;
	std::vector<int32_t> *batch = new std::vector<int32_t>();
	batch->swap(g_backend.commands);
	if (emscripten_is_main_runtime_thread())
	{
		runBatch(batch);
	}
	else if (!emscripten_proxy_async(g_backend.queue, emscripten_main_runtime_thread_id(), runBatch, batch))
	{
		delete batch;	// out of memory: the commands are lost
	}
}

} // namespace

//-------------------------------------------------------------------------------------------------
static void installOnMainThread(void *result)
{
	*static_cast<int *>(result) = wa_js_install(&g_shared, 0, 0);
}

extern "C" {

//-------------------------------------------------------------------------------------------------
int WebAudio_Init(int sampleRate)
{
	if (g_backend.available.load())
		return 1;

	if (!g_backend.queue)
		g_backend.queue = em_proxying_queue_create();

	// The interpreter needs the pointer of the C function that frees blocks, and the rate.
	struct Args { int rate; int result; } args = { sampleRate, 0 };
	auto install = [](void *p)
	{
		Args *a = static_cast<Args *>(p);
		a->result = wa_js_install(&g_shared, a->rate, (int)(intptr_t)&freeBlock);
	};
	if (emscripten_is_main_runtime_thread())
		install(&args);
	else
		emscripten_proxy_sync(g_backend.queue, emscripten_main_runtime_thread_id(), install, &args);

	if (!args.result)
		return 0;
	g_backend.available.store(true);
	return 1;
}

//-------------------------------------------------------------------------------------------------
static void stopStreamThread();

void WebAudio_Shutdown(void)
{
	if (!g_backend.available.load())
		return;
	stopStreamThread();
	{
		std::lock_guard<std::mutex> g(g_backend.lock);
		flushLocked();
	}
	g_backend.available.store(false);
	auto shut = [](void *) { wa_js_shutdown(); };
	if (emscripten_is_main_runtime_thread())
		shut(nullptr);
	else
		emscripten_proxy_sync(g_backend.queue, emscripten_main_runtime_thread_id(), shut, nullptr);
	std::lock_guard<std::mutex> g(g_backend.lock);
	for (uint32_t i = 0; i < WEBAUDIO_MAX_VOICES; ++i)
	{
		g_backend.inUse[i] = false;
		g_backend.submitted[i].store(0);
	}
}

//-------------------------------------------------------------------------------------------------
void WebAudio_Resume(void)
{
	if (!g_backend.available.load())
		return;
	push({ OP_RESUME });
	WebAudio_Flush();
}

//-------------------------------------------------------------------------------------------------
int WebAudio_GetState(void)
{
	if (!g_backend.available.load())
		return WEBAUDIO_STATE_NONE;
	return __atomic_load_n(&g_shared.contextState, __ATOMIC_ACQUIRE);
}

//-------------------------------------------------------------------------------------------------
int WebAudio_GetSampleRate(void)
{
	if (!g_backend.available.load())
		return 0;
	return __atomic_load_n(&g_shared.sampleRate, __ATOMIC_ACQUIRE);
}

//-------------------------------------------------------------------------------------------------
void WebAudio_SetMasterVolume(float volume)
{
	if (!g_backend.available.load())
		return;
	push({ OP_MASTER_GAIN, bits(volume) });
}

//-------------------------------------------------------------------------------------------------
void WebAudio_SetPanningModel(int hrtf)
{
	if (!g_backend.available.load())
		return;
	push({ OP_PANNING_MODEL, hrtf ? 1 : 0 });
}

//-------------------------------------------------------------------------------------------------
void WebAudio_SetListener(const float position[3], const float forward[3], const float up[3])
{
	if (!g_backend.available.load())
		return;
	push({ OP_LISTENER, bits(position[0]), bits(position[1]), bits(position[2]),
		bits(forward[0]), bits(forward[1]), bits(forward[2]), bits(up[0]), bits(up[1]), bits(up[2]) });
}

//-------------------------------------------------------------------------------------------------
void WebAudio_Flush(void)
{
	if (!g_backend.available.load())
		return;
	std::lock_guard<std::mutex> g(g_backend.lock);
	flushLocked();
}

//-------------------------------------------------------------------------------------------------
WebAudioBuffer WebAudio_CreateBuffer(const int16_t *interleaved, uint32_t frames, uint32_t channels, uint32_t sampleRate)
{
	if (!g_backend.available.load() || !interleaved || frames == 0 || channels == 0 || channels > 8)
		return 0;
	float *planar = static_cast<float *>(malloc((size_t)frames * channels * sizeof(float)));
	if (!planar)
		return 0;
	for (uint32_t c = 0; c < channels; ++c)
	{
		float *dst = planar + (size_t)c * frames;
		const int16_t *src = interleaved + c;
		for (uint32_t i = 0; i < frames; ++i)
			dst[i] = (float)src[(size_t)i * channels] * (1.0f / 32768.0f);
	}
	const WebAudioBuffer id = g_backend.nextBuffer.fetch_add(1);
	push({ OP_CREATE_BUFFER, (int32_t)id, (int32_t)(intptr_t)planar, (int32_t)frames, (int32_t)channels, (int32_t)sampleRate });
	return id;
}

//-------------------------------------------------------------------------------------------------
void WebAudio_DestroyBuffer(WebAudioBuffer buffer)
{
	if (!g_backend.available.load() || buffer == 0)
		return;
	push({ OP_DESTROY_BUFFER, (int32_t)buffer });
}

//-------------------------------------------------------------------------------------------------
WebAudioVoice WebAudio_CreateVoice(WebAudioVoiceKind kind)
{
	if (!g_backend.available.load())
		return 0;
	std::lock_guard<std::mutex> g(g_backend.lock);
	for (uint32_t n = 0; n < WEBAUDIO_MAX_VOICES; ++n)
	{
		const uint32_t idx = (g_backend.nextVoiceSearch + n) % WEBAUDIO_MAX_VOICES;
		if (g_backend.inUse[idx])
			continue;
		g_backend.nextVoiceSearch = idx + 1;
		g_backend.inUse[idx] = true;
		uint32_t gen = (g_backend.generation[idx] + 1) & 0xFFFFFu;
		if (gen == 0)
			gen = 1;
		g_backend.generation[idx] = gen;
		g_backend.submitted[idx].store(0);
		const WebAudioVoice id = (gen << 12) | idx;
		const int32_t words[] = { OP_CREATE_VOICE, (int32_t)id, (int32_t)kind };
		g_backend.commands.insert(g_backend.commands.end(), words, words + 3);
		return id;
	}
	return 0;
}

//-------------------------------------------------------------------------------------------------
void WebAudio_DestroyVoice(WebAudioVoice voice, float fadeOutSeconds)
{
	if (!g_backend.available.load() || !voiceAllocated(voice))
		return;
	std::lock_guard<std::mutex> g(g_backend.lock);
	const int32_t words[] = { OP_DESTROY_VOICE, (int32_t)voice, bits(fadeOutSeconds) };
	g_backend.commands.insert(g_backend.commands.end(), words, words + 3);
	g_backend.inUse[voiceIndex(voice)] = false;
}

//-------------------------------------------------------------------------------------------------
void WebAudio_VoiceQueue(WebAudioVoice voice, WebAudioBuffer buffer, float delaySeconds, int flags)
{
	if (!g_backend.available.load() || !voiceAllocated(voice) || buffer == 0)
		return;
	g_backend.submitted[voiceIndex(voice)].fetch_add(1);
	push({ OP_QUEUE, (int32_t)voice, (int32_t)buffer, bits(delaySeconds), flags & ~WEBAUDIO_SEGMENT_KEEP_IF_SUSPENDED });
}

//-------------------------------------------------------------------------------------------------
void WebAudio_VoiceCancelPending(WebAudioVoice voice)
{
	if (!g_backend.available.load() || !voiceAllocated(voice))
		return;
	push({ OP_CANCEL_PENDING, (int32_t)voice });
}

//-------------------------------------------------------------------------------------------------
void WebAudio_VoiceSetGain(WebAudioVoice voice, float gain)
{
	if (!g_backend.available.load() || !voiceAllocated(voice))
		return;
	push({ OP_SET_GAIN, (int32_t)voice, bits(gain) });
}

//-------------------------------------------------------------------------------------------------
void WebAudio_VoiceSetPitch(WebAudioVoice voice, float rate)
{
	if (!g_backend.available.load() || !voiceAllocated(voice))
		return;
	push({ OP_SET_PITCH, (int32_t)voice, bits(rate) });
}

//-------------------------------------------------------------------------------------------------
void WebAudio_VoiceSetPan(WebAudioVoice voice, float pan)
{
	if (!g_backend.available.load() || !voiceAllocated(voice))
		return;
	push({ OP_SET_PAN, (int32_t)voice, bits(pan) });
}

//-------------------------------------------------------------------------------------------------
void WebAudio_VoiceSetPosition(WebAudioVoice voice, float x, float y, float z)
{
	if (!g_backend.available.load() || !voiceAllocated(voice))
		return;
	push({ OP_SET_POSITION, (int32_t)voice, bits(x), bits(y), bits(z) });
}

//-------------------------------------------------------------------------------------------------
void WebAudio_VoiceSetLowPass(WebAudioVoice voice, float cutoffHz)
{
	if (!g_backend.available.load() || !voiceAllocated(voice))
		return;
	push({ OP_SET_LOWPASS, (int32_t)voice, bits(cutoffHz) });
}

//-------------------------------------------------------------------------------------------------
void WebAudio_VoicePause(WebAudioVoice voice, int paused)
{
	if (!g_backend.available.load() || !voiceAllocated(voice))
		return;
	push({ OP_PAUSE, (int32_t)voice, paused ? 1 : 0 });
}

//-------------------------------------------------------------------------------------------------
uint32_t WebAudio_VoicePendingSegments(WebAudioVoice voice)
{
	if (!voiceAllocated(voice))
		return 0;
	const uint32_t idx = voiceIndex(voice);
	const int32_t submitted = g_backend.submitted[idx].load();
	// Until the main thread has created the voice the counters of the shared block are those of
	// an earlier voice of the slot.
	const int32_t finished = voiceLive(voice) ? __atomic_load_n(&g_shared.voices[idx].segmentsFinished, __ATOMIC_ACQUIRE) : 0;
	return submitted > finished ? (uint32_t)(submitted - finished) : 0u;
}

//-------------------------------------------------------------------------------------------------
uint32_t WebAudio_VoiceLoopsDone(WebAudioVoice voice)
{
	if (!voiceLive(voice) || !voiceAllocated(voice))
		return 0;
	return (uint32_t)__atomic_load_n(&g_shared.voices[voiceIndex(voice)].loopsDone, __ATOMIC_ACQUIRE);
}

//-------------------------------------------------------------------------------------------------
int32_t WebAudio_VoiceRemainingMs(WebAudioVoice voice)
{
	if (!voiceLive(voice) || !voiceAllocated(voice))
		return 0;
	double end;
	const double* src = &g_shared.voices[voiceIndex(voice)].endWallMs;
	__atomic_load(src, &end, __ATOMIC_RELAXED);
	if (end <= 0.0)
		return 0;
	if (end >= 1e17)
		return INT32_MAX;
	const double left = end - emscripten_date_now();
	return left <= 0.0 ? 0 : (left >= 2e9 ? INT32_MAX : (int32_t)left);
}

//-------------------------------------------------------------------------------------------------
int WebAudio_GetStats(WebAudioStats *stats)
{
	if (!g_backend.available.load())
		return 0;
	static_assert(sizeof(WebAudioStats) == 8 * sizeof(uint32_t), "mirrored in wa_js_stats");
	auto read = [](void *p) { wa_js_stats(static_cast<uint32_t *>(p)); };
	WebAudio_Flush();
	if (emscripten_is_main_runtime_thread())
		read(stats);
	else
		emscripten_proxy_sync(g_backend.queue, emscripten_main_runtime_thread_id(), read, stats);
	return 1;
}

} // extern "C"

// ============================================================================================
// 3. Streams
// ============================================================================================

namespace
{

enum
{
	STREAM_TARGET_CHUNKS = 6,			// chunks queued ahead of the playback
	STREAM_CHUNKS_PER_PASS = 3,		// decoded per stream before looking at the others
};

struct Stream
{
	WebAudioStream id = 0;
	WebAudioVoice voice = 0;
	bool loop = false;
	std::vector<uint8_t> data;
	std::unique_ptr<WebAudio::Decoder> decoder;
	uint32_t chunkFrames = 0;
	std::mutex decodeLock;				// one thread decodes a stream at a time
	std::atomic<bool> closed{false};
	std::atomic<bool> eof{false};	// the last chunk of a non looping file was queued

	// Decoder read-ahead: the chunk after the one being sent tells whether that one ends a pass.
	std::vector<int16_t> next;
	size_t nextFrames = 0;
	bool haveNext = false;
	std::vector<int16_t> current;
};

struct Streams
{
	std::mutex lock;
	std::vector<std::shared_ptr<Stream>> list;
	std::condition_variable wake;
	std::thread thread;
	std::atomic<bool> threadRunning{false};
	std::atomic<bool> threadFailed{false};
	bool quit = false;
	std::atomic<uint32_t> nextId{1};
};

Streams g_streams;

/// Decodes the next chunk of the stream. Returns false when there is nothing more to decode in
/// this pass; lastOfPass tells that the chunk returned is the last of the pass of the file.
bool fetchChunk(Stream &s, bool *lastOfPass)
{
	const size_t ch = s.decoder->info().channels;
	if (!s.haveNext)
	{
		s.next.resize((size_t)s.chunkFrames * ch);
		s.nextFrames = s.decoder->read(s.next.data(), s.chunkFrames);
		s.haveNext = true;
	}
	if (s.nextFrames == 0)
		return false;
	s.current.swap(s.next);
	const size_t frames = s.nextFrames;
	s.next.resize((size_t)s.chunkFrames * ch);
	s.nextFrames = s.decoder->read(s.next.data(), s.chunkFrames);
	s.current.resize(frames * ch);
	*lastOfPass = (s.nextFrames == 0);
	return true;
}

void pushChunk(Stream &s, int flags)
{
	const WebAudio::StreamInfo &info = s.decoder->info();
	const uint32_t ch = info.channels;
	const uint32_t frames = (uint32_t)(s.current.size() / ch);
	float *planar = static_cast<float *>(malloc((size_t)frames * ch * sizeof(float)));
	if (!planar)
		return;
	for (uint32_t c = 0; c < ch; ++c)
	{
		float *dst = planar + (size_t)c * frames;
		const int16_t *src = s.current.data() + c;
		for (uint32_t i = 0; i < frames; ++i)
			dst[i] = (float)src[(size_t)i * ch] * (1.0f / 32768.0f);
	}
	g_backend.submitted[voiceIndex(s.voice)].fetch_add(1);
	push({ OP_STREAM_CHUNK, (int32_t)s.voice, (int32_t)(intptr_t)planar, (int32_t)frames, (int32_t)ch, (int32_t)info.sampleRate, flags });
}

/// Decodes and queues chunks of one stream until it has enough ahead.
void pumpStream(Stream &s)
{
	std::unique_lock<std::mutex> decodeGuard(s.decodeLock, std::try_to_lock);
	if (!decodeGuard.owns_lock())
		return;

	for (int produced = 0; produced < STREAM_CHUNKS_PER_PASS; ++produced)
	{
		if (s.closed.load() || s.eof.load())
			return;
		if (WebAudio_VoicePendingSegments(s.voice) >= STREAM_TARGET_CHUNKS)
			return;

		bool lastOfPass = false;
		if (!fetchChunk(s, &lastOfPass))
		{
			// An empty pass (damaged file) must not loop forever.
			s.eof.store(true);
			return;
		}
		if (s.closed.load())
			return;

		pushChunk(s, lastOfPass ? WEBAUDIO_SEGMENT_ENDS_LOOP : 0);

		if (lastOfPass)
		{
			if (s.loop && s.decoder->rewind())
			{
				s.haveNext = false;
			}
			else
			{
				s.eof.store(true);
				return;
			}
		}
	}
}

void pumpAll()
{
	std::vector<std::shared_ptr<Stream>> snapshot;
	{
		std::lock_guard<std::mutex> g(g_streams.lock);
		// forget closed streams
		for (size_t i = 0; i < g_streams.list.size();)
		{
			if (g_streams.list[i]->closed.load())
				g_streams.list.erase(g_streams.list.begin() + (ptrdiff_t)i);
			else
				++i;
		}
		snapshot = g_streams.list;
	}
	for (auto &s : snapshot)
		pumpStream(*s);
	if (!snapshot.empty())
	{
		std::lock_guard<std::mutex> g(g_backend.lock);
		flushLocked();
	}
}

void streamThreadMain()
{
	std::unique_lock<std::mutex> lk(g_streams.lock);
	while (!g_streams.quit)
	{
		const bool busy = !g_streams.list.empty();
		g_streams.wake.wait_for(lk, std::chrono::milliseconds(busy ? 15 : 500));
		if (g_streams.quit)
			break;
		lk.unlock();
		pumpAll();
		// Also deliver commands that the engine thread has queued but not flushed (it may be busy loading).
		if (g_backend.available.load())
		{
			std::lock_guard<std::mutex> g(g_backend.lock);
			flushLocked();
		}
		lk.lock();
	}
}

void startStreamThread()
{
	if (g_streams.threadRunning.load() || g_streams.threadFailed.load())
		return;
	try
	{
		g_streams.quit = false;
		g_streams.thread = std::thread(streamThreadMain);
		g_streams.threadRunning.store(true);
	}
	catch (...)
	{
		g_streams.threadFailed.store(true);
	}
}

} // namespace

//-------------------------------------------------------------------------------------------------
static void stopStreamThread()
{
	if (g_streams.threadRunning.load())
	{
		{
			std::lock_guard<std::mutex> g(g_streams.lock);
			g_streams.quit = true;
		}
		g_streams.wake.notify_all();
		g_streams.thread.join();
		g_streams.threadRunning.store(false);
	}
	std::lock_guard<std::mutex> g(g_streams.lock);
	for (auto &s : g_streams.list)
		s->closed.store(true);
	g_streams.list.clear();
}

extern "C" {

//-------------------------------------------------------------------------------------------------
WebAudioStream WebAudio_StreamOpen(const void *fileData, uint32_t fileSize, WebAudioVoice voice, int loop)
{
	if (!g_backend.available.load() || !fileData || fileSize == 0 || !voiceAllocated(voice))
		return 0;

	std::shared_ptr<Stream> s = std::make_shared<Stream>();
	s->data.assign(static_cast<const uint8_t *>(fileData), static_cast<const uint8_t *>(fileData) + fileSize);
	s->decoder = WebAudio::Decoder::create(s->data.data(), s->data.size());
	if (!s->decoder)
		return 0;
	s->voice = voice;
	s->loop = loop != 0;
	s->chunkFrames = s->decoder->info().sampleRate / 2;	// half a second
	if (s->chunkFrames < 1024)
		s->chunkFrames = 1024;
	s->id = g_streams.nextId.fetch_add(1);

	// The voice creation (and its gain) must reach the main thread before the first chunk.
	WebAudio_Flush();

	{
		std::lock_guard<std::mutex> g(g_streams.lock);
		g_streams.list.push_back(s);
	}
	startStreamThread();
	if (g_streams.threadRunning.load())
		g_streams.wake.notify_all();
	else
		WebAudio_PumpStreams();	// no thread: the caller keeps pumping, start with the first chunk now
	return s->id;
}

//-------------------------------------------------------------------------------------------------
void WebAudio_StreamClose(WebAudioStream stream)
{
	std::lock_guard<std::mutex> g(g_streams.lock);
	for (size_t i = 0; i < g_streams.list.size(); ++i)
	{
		if (g_streams.list[i]->id == stream)
		{
			g_streams.list[i]->closed.store(true);
			g_streams.list.erase(g_streams.list.begin() + (ptrdiff_t)i);
			return;
		}
	}
}

//-------------------------------------------------------------------------------------------------
int WebAudio_StreamIsFinished(WebAudioStream stream)
{
	std::shared_ptr<Stream> s;
	{
		std::lock_guard<std::mutex> g(g_streams.lock);
		for (auto &it : g_streams.list)
		{
			if (it->id == stream)
			{
				s = it;
				break;
			}
		}
	}
	if (!s)
		return 1;
	if (!s->eof.load())
		return 0;
	return WebAudio_VoicePendingSegments(s->voice) == 0 ? 1 : 0;
}

//-------------------------------------------------------------------------------------------------
void WebAudio_PumpStreams(void)
{
	if (!g_backend.available.load())
		return;
	pumpAll();
}

//-------------------------------------------------------------------------------------------------
int WebAudio_StreamsHaveThread(void)
{
	return g_streams.threadRunning.load() ? 1 : 0;
}

} // extern "C"
