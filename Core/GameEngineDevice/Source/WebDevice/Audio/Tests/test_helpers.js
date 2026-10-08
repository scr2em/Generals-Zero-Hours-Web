// Page side helpers of web_audio_test (linked with --pre-js, runs on every thread but only
// defines anything on the main browser thread, where the Web Audio context lives).
//
// zhTest taps the master gain of the audio backend (globalThis.zhWebAudio) with a
// ScriptProcessor and keeps the last seconds of the output, so the C++ test can ask what the
// browser would have played: the strength of a tone per channel, the loudness, the longest
// silence. This is how the test "listens" in headless Chromium.
if (typeof document !== 'undefined') {
  globalThis.zhTest = (function () {
    var T = { rec: null, chunks: [], total: 0, rate: 0, maxFrames: 0 };

    // An AudioWorklet copies the rendered blocks to the page (a ScriptProcessor on the busy main
    // thread drops blocks, which shows up as clicks that are not in the audio).
    T.ready = 0;
    T.endFrame = 0;
    T.reset = function () { T.rec = null; T.chunks = []; T.total = 0; T.ready = 0; T.endFrame = 0; return 1; };
    T.startRecording = function () {
      var A = globalThis.zhWebAudio;
      if (!A || !A.ctx) return 0;
      if (T.rec) return 1;
      T.rate = A.ctx.sampleRate;
      T.maxFrames = T.rate * 30;
      T.rec = true;
      var code = 'class Tap extends AudioWorkletProcessor {' +
        ' constructor() { super(); this.n = 0; this.l = new Float32Array(1024); this.r = new Float32Array(1024); }' +
        ' process(inputs) {' +
        '   var i = inputs[0]; var a = i[0], b = i.length > 1 ? i[1] : i[0];' +
        '   if (a) { this.l.set(a, this.n); this.r.set(b, this.n); } else { this.l.fill(0, this.n, this.n + 128); this.r.fill(0, this.n, this.n + 128); }' +
        '   this.n += 128;' +
        '   if (this.n >= 1024) { this.port.postMessage([this.l.slice(), this.r.slice(), currentFrame + 128 - 1024]); this.n = 0; }' +
        '   return true; } }' +
        'registerProcessor("tap", Tap);';
      var url = URL.createObjectURL(new Blob([code], { type: 'application/javascript' }));
      A.ctx.audioWorklet.addModule(url).then(function () {
        var node = new AudioWorkletNode(A.ctx, 'tap', { numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2] });
        node.port.onmessage = function (e) {
          T.chunks.push(e.data);
          T.total += e.data[0].length;
          T.endFrame = e.data[2] + e.data[0].length;
          while (T.chunks.length > 1 && (T.chunks.length - 1) * e.data[0].length > T.maxFrames) T.chunks.shift();
        };
        var mute = A.ctx.createGain();
        mute.gain.value = 0;
        A.master.connect(node);
        node.connect(mute);
        mute.connect(A.ctx.destination);
        T.ready = 1;
      }, function (err) { console.error('tap failed ' + err); T.ready = -1; });
      return 1;
    };

    // The last ms milliseconds of channel ch.
    T.last = function (ch, ms) {
      var n = Math.round(T.rate * ms / 1000);
      var out = new Float32Array(n);
      var pos = n;
      for (var i = T.chunks.length - 1; i >= 0 && pos > 0; --i) {
        var c = T.chunks[i][ch];
        var take = Math.min(pos, c.length);
        out.set(c.subarray(c.length - take), pos - take);
        pos -= take;
      }
      return out;
    };

    // Amplitude of the sine of frequency f (Goertzel), 0.5 for a sine of 0.5.
    T.tone = function (ch, f, ms) {
      var s = T.last(ch, ms), w = 2 * Math.PI * f / T.rate, re = 0, im = 0;
      for (var i = 0; i < s.length; ++i) { re += s[i] * Math.cos(w * i); im += s[i] * Math.sin(w * i); }
      return 2 * Math.sqrt(re * re + im * im) / s.length;
    };

    T.rms = function (ch, ms) {
      var s = T.last(ch, ms), t = 0;
      for (var i = 0; i < s.length; ++i) t += s[i] * s[i];
      return Math.sqrt(t / s.length);
    };

    T.peak = function (ch, ms) {
      var s = T.last(ch, ms), p = 0;
      for (var i = 0; i < s.length; ++i) p = Math.max(p, Math.abs(s[i]));
      return p;
    };

    // Longest run, in milliseconds, of samples below the threshold in the last ms milliseconds.
    T.maxSilenceMs = function (ch, ms, threshold) {
      var s = T.last(ch, ms), run = 0, best = 0;
      for (var i = 0; i < s.length; ++i) {
        if (Math.abs(s[i]) < threshold) { ++run; if (run > best) best = run; } else run = 0;
      }
      return best * 1000 / T.rate;
    };

    // Where the longest silence starts, in ms from the start of the window.
    T.silenceStartMs = function (ch, ms, threshold) {
      var s = T.last(ch, ms), run = 0, best = 0, bestAt = 0;
      for (var i = 0; i < s.length; ++i) {
        if (Math.abs(s[i]) < threshold) { ++run; if (run > best) { best = run; bestAt = i - run + 1; } } else run = 0;
      }
      return bestAt * 1000 / T.rate;
    };

    // Largest sample to sample jump (clicks show up as jumps far above what the tone itself does).
    T.maxStep = function (ch, ms) {
      var s = T.last(ch, ms), best = 0;
      for (var i = 1; i < s.length; ++i) best = Math.max(best, Math.abs(s[i] - s[i - 1]));
      return best;
    };

    T.totalFrames = function () { return T.total; };
    // How many frames the data that reached the page lags behind the rendering (the worklet posts blocks of 1024 frames).
    T.behind = function () { return Math.round(globalThis.zhWebAudio.ctx.currentTime * T.rate) - T.endFrame; };

    // Introspection of the Web Audio graph.
    T.ctxState = function () { var A = globalThis.zhWebAudio; return A && A.ctx ? ({ suspended: 1, running: 2, closed: 3 }[A.ctx.state] || 1) : 0; };
    T.suspend = function () { globalThis.zhWebAudio.ctx.suspend(); return 1; };
    T.resume = function () { globalThis.zhWebAudio.ctx.resume(); return 1; };
    T.ctxRate = function () { return globalThis.zhWebAudio.ctx.sampleRate; };
    T.voice = function (idx) { return globalThis.zhWebAudio.voices[idx & 0xFFF]; };
    T.pannerCoord = function (idx, axis) {
      var v = T.voice(idx);
      return v && v.panner ? [v.panner.positionX.value, v.panner.positionY.value, v.panner.positionZ.value][axis] : NaN;
    };
    T.pannerModel = function (idx) { var v = T.voice(idx); return v && v.panner ? (v.panner.panningModel === 'HRTF' ? 1 : 0) : -1; };
    T.pannerRolloff = function (idx) { var v = T.voice(idx); return v && v.panner ? v.panner.rolloffFactor : -1; };
    T.listenerCoord = function (k) {
      var L = globalThis.zhWebAudio.ctx.listener;
      return [L.positionX.value, L.positionY.value, L.positionZ.value, L.forwardX.value, L.forwardY.value, L.forwardZ.value, L.upX.value, L.upY.value, L.upZ.value][k];
    };
    T.voiceGain = function (idx) { var v = T.voice(idx); return v ? v.gain.gain.value : -1; };
    T.lowpassHz = function (idx) { var v = T.voice(idx); return v && v.lp ? v.lp.frequency.value : -1; };
    T.liveVoices = function () { var n = 0, a = globalThis.zhWebAudio.voices; for (var i = 0; i < a.length; ++i) if (a[i]) ++n; return n; };
    T.liveBuffers = function () { return globalThis.zhWebAudio.buffers.size; };
    T.unlockListeners = function () { return typeof document !== 'undefined' ? 1 : 0; };

    return T;
  })();
}
