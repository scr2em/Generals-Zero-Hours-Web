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

// FILE: web_audio_test.cpp ///////////////////////////////////////////////////
//
// Test of the browser sound device (WebAudioBackend) as the engine uses it: from a worker
// thread (-sPROXY_TO_PTHREAD) that blocks, with the browser's main thread idle. The page
// (run_test.mjs, headless Chromium) taps the output of the Web Audio graph (test_helpers.js),
// so the checks listen to what is actually rendered: tone frequency and strength per channel,
// panning, silence, clicks and gaps.
//
// The last scenario runs the pattern of the real engine: one update per frame from
// emscripten_set_main_loop on this thread.
//
///////////////////////////////////////////////////////////////////////////////

#include "WebDevice/Audio/WebAudioBackend.h"
#include "WebDevice/Audio/WebAudioDecoder.h"
#include "WebDevice/Audio/WebAudioMix.h"

#include <emscripten.h>
#include <emscripten/threading.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

static bool g_allScenarios = true;
static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, ...) \
	do { \
		++g_checks; \
		if (cond) { printf("  ok   %s\n", #cond); } \
		else { ++g_failures; printf("  FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } \
	} while (0)

static double nowMs() { return emscripten_get_now(); }
static void sleepMs(double ms) { emscripten_thread_sleep(ms); }

// Blocks the thread the way a busy engine does: without yielding, without any call.
static void spinMs(double ms)
{
	const double end = nowMs() + ms;
	while (nowMs() < end) {}
}

// Page helpers ---------------------------------------------------------------------------------

// The recording reaches the page in blocks, a little after it was rendered: wait until it has caught up
// with the rendering, so that "the last 200 ms" means the last 200 ms.
static void settle()
{
	const double end = nowMs() + 500;
	while (nowMs() < end && MAIN_THREAD_EM_ASM_INT({ return zhTest.behind() > 1600 ? 1 : 0; }))
		sleepMs(2);
}

static double tone(int ch, double freq, double ms)
{
	settle();
	return MAIN_THREAD_EM_ASM_DOUBLE({ return zhTest.tone($0, $1, $2); }, ch, freq, ms);
}
static double rms(int ch, double ms) { settle(); return MAIN_THREAD_EM_ASM_DOUBLE({ return zhTest.rms($0, $1); }, ch, ms); }
static double peak(int ch, double ms) { settle(); return MAIN_THREAD_EM_ASM_DOUBLE({ return zhTest.peak($0, $1); }, ch, ms); }
static double maxSilenceMs(int ch, double ms, double threshold)
{
	settle();
	return MAIN_THREAD_EM_ASM_DOUBLE({ return zhTest.maxSilenceMs($0, $1, $2); }, ch, ms, threshold);
}
static double silenceStartMs(int ch, double ms, double threshold)
{
	settle();
	return MAIN_THREAD_EM_ASM_DOUBLE({ return zhTest.silenceStartMs($0, $1, $2); }, ch, ms, threshold);
}
static double maxStep(int ch, double ms) { settle(); return MAIN_THREAD_EM_ASM_DOUBLE({ return zhTest.maxStep($0, $1); }, ch, ms); }
static double pannerCoord(WebAudioVoice v, int axis) { return MAIN_THREAD_EM_ASM_DOUBLE({ return zhTest.pannerCoord($0, $1); }, v, axis); }
static double listenerCoord(int k) { return MAIN_THREAD_EM_ASM_DOUBLE({ return zhTest.listenerCoord($0); }, k); }
static double voiceGain(WebAudioVoice v) { return MAIN_THREAD_EM_ASM_DOUBLE({ return zhTest.voiceGain($0); }, v); }
static int liveVoices() { return MAIN_THREAD_EM_ASM_INT({ return zhTest.liveVoices(); }); }
static int liveBuffers() { return MAIN_THREAD_EM_ASM_INT({ return zhTest.liveBuffers(); }); }

// Files and buffers ----------------------------------------------------------------------------

static std::vector<uint8_t> readFile(const char *name)
{
	std::string path = std::string("/data/") + name;
	std::vector<uint8_t> v;
	FILE *f = fopen(path.c_str(), "rb");
	if (!f)
		return v;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	v.resize((size_t)n);
	if (fread(v.data(), 1, v.size(), f) != v.size())
		v.clear();
	fclose(f);
	return v;
}

struct Sound
{
	WebAudioBuffer buffer = 0;
	WebAudio::StreamInfo info;
};

static Sound loadSound(const char *name)
{
	Sound s;
	std::vector<uint8_t> file = readFile(name);
	std::vector<int16_t> pcm;
	if (file.empty() || !WebAudio::decodeAll(file.data(), file.size(), &pcm, &s.info))
		return s;
	s.buffer = WebAudio_CreateBuffer(pcm.data(), (uint32_t)s.info.totalFrames, s.info.channels, s.info.sampleRate);
	return s;
}

static Sound makeTones(double f1, double f2, double seconds, uint32_t rate)
{
	Sound s;
	const uint32_t n = (uint32_t)(seconds * rate);
	std::vector<int16_t> pcm(n);
	for (uint32_t i = 0; i < n; ++i)
		pcm[i] = (int16_t)(8000.0 * sin(2 * M_PI * f1 * i / rate) + (f2 > 0 ? 8000.0 * sin(2 * M_PI * f2 * i / rate) : 0.0));
	s.info.sampleRate = rate;
	s.info.channels = 1;
	s.info.totalFrames = n;
	s.buffer = WebAudio_CreateBuffer(pcm.data(), n, 1, rate);
	return s;
}

static bool waitFor(bool (*pred)(void *), void *arg, double timeoutMs)
{
	const double end = nowMs() + timeoutMs;
	while (nowMs() < end)
	{
		if (pred(arg))
			return true;
		sleepMs(5);
	}
	return pred(arg);
}

static bool voiceDone(void *p)
{
	return WebAudio_VoicePendingSegments(*static_cast<WebAudioVoice *>(p)) == 0;
}

static void resetListener()
{
	const float pos[3] = { 0, 0, 0 }, fwd[3] = { 0, 1, 0 }, up[3] = { 0, 0, 1 };
	WebAudio_SetListener(pos, fwd, up);
	WebAudio_Flush();
}

// Scenarios ------------------------------------------------------------------------------------

// Decoding in the browser build (the same code is unit tested natively with the sanitizers) ---

static double goertzel(const std::vector<int16_t> &pcm, uint32_t channels, uint32_t ch, double freq, double rate)
{
	const size_t n = pcm.size() / channels;
	double re = 0, im = 0;
	for (size_t i = 0; i < n; ++i)
	{
		const double s = pcm[i * channels + ch] / 32768.0;
		re += s * cos(2 * M_PI * freq * (double)i / rate);
		im += s * sin(2 * M_PI * freq * (double)i / rate);
	}
	return 2.0 * sqrt(re * re + im * im) / (double)n;
}

static void testDecoders()
{
	printf("== decoders (WebAssembly build)\n");
	struct { const char *name; WebAudio::Codec codec; uint32_t channels, rate; uint64_t frames; double fl, fr; } files[] = {
		{ "pcm16_mono_44100_440.wav", WebAudio::Codec::Pcm, 1, 44100, 44100, 440, 0 },
		{ "pcm8_mono_22050_550.wav", WebAudio::Codec::Pcm, 1, 22050, 11025, 550, 0 },
		{ "adpcm_mono_22050_880.wav", WebAudio::Codec::ImaAdpcm, 1, 22050, 33075, 880, 0 },
		{ "adpcm_stereo_44100_660.wav", WebAudio::Codec::ImaAdpcm, 2, 44100, 44100, 660, 990 },
		{ "music_stereo_44100_1320.mp3", WebAudio::Codec::Mp3, 2, 44100, 176400, 1320, 1760 },
		{ "music_mono_22050_1100.mp3", WebAudio::Codec::Mp3, 1, 22050, 44100, 1100, 0 },
	};
	for (auto &f : files)
	{
		std::vector<uint8_t> file = readFile(f.name);
		std::vector<int16_t> pcm;
		WebAudio::StreamInfo info;
		const double t0 = nowMs();
		const bool ok = WebAudio::decodeAll(file.data(), file.size(), &pcm, &info);
		const double dt = nowMs() - t0;
		CHECK(ok, "%s decodes", f.name);
		CHECK(info.codec == f.codec && info.channels == f.channels && info.sampleRate == f.rate, "%s: format", f.name);
		CHECK(info.totalFrames == f.frames, "%s: %llu frames (want %llu)", f.name, (unsigned long long)info.totalFrames, (unsigned long long)f.frames);
		CHECK(fabs(goertzel(pcm, f.channels, 0, f.fl, f.rate) - 0.5) < 0.03, "%s: tone", f.name);
		if (f.channels == 2)
			CHECK(fabs(goertzel(pcm, 2, 1, f.fr, f.rate) - 0.5) < 0.03, "%s: right tone", f.name);
		printf("  %s: %.1f ms for %.2f s of sound (%.0fx real time)\n", f.name, dt, (double)info.totalFrames / f.rate, (double)info.totalFrames / f.rate * 1000.0 / (dt > 0.01 ? dt : 0.01));
	}
}

static void testContext()
{
	printf("== context\n");
	CHECK(WebAudio_Init(44100) == 1, "Web Audio is not available");
	CHECK(WebAudio_Init(44100) == 1, "second init");
	CHECK(MAIN_THREAD_EM_ASM_INT({ return zhTest.startRecording(); }) == 1, "recording tap");
	CHECK(waitFor([](void *) { return MAIN_THREAD_EM_ASM_INT({ return zhTest.ready; }) == 1; }, nullptr, 3000), "the recording worklet is up");
	const bool running = waitFor([](void *) { return WebAudio_GetState() == WEBAUDIO_STATE_RUNNING; }, nullptr, 3000);
	CHECK(running, "AudioContext is not running, state %d", WebAudio_GetState());
	CHECK(WebAudio_GetSampleRate() == 44100, "sample rate %d", WebAudio_GetSampleRate());
	// the context clock must advance (this is a real audio clock, not a stalled one)
	const double t0 = MAIN_THREAD_EM_ASM_DOUBLE({ return globalThis.zhWebAudio.ctx.currentTime; });
	sleepMs(300);
	const double t1 = MAIN_THREAD_EM_ASM_DOUBLE({ return globalThis.zhWebAudio.ctx.currentTime; });
	CHECK(t1 - t0 > 0.2 && t1 - t0 < 0.45, "context time advanced by %f s in 0.3 s", t1 - t0);
	CHECK(MAIN_THREAD_EM_ASM_INT({ return zhTest.totalFrames(); }) > 0, "the tap has not seen any audio block");
	resetListener();
}

static void testPcmAndAdpcm()
{
	printf("== PCM and IMA ADPCM sound effects\n");
	WebAudioStats before, after;
	WebAudio_GetStats(&before);

	Sound pcm = loadSound("pcm16_mono_44100_440.wav");
	Sound pcm8 = loadSound("pcm8_mono_22050_550.wav");
	Sound adpcm = loadSound("adpcm_mono_22050_880.wav");
	Sound adpcmSt = loadSound("adpcm_stereo_44100_660.wav");
	CHECK(pcm.buffer && pcm8.buffer && adpcm.buffer && adpcmSt.buffer, "loading the sounds");

	struct { const Sound *s; double f; double f2; } cases[] = { { &pcm, 440, 0 }, { &pcm8, 550, 0 }, { &adpcm, 880, 0 } };
	for (auto &c : cases)
	{
		WebAudioVoice v = WebAudio_CreateVoice(WEBAUDIO_VOICE_2D);
		CHECK(v != 0, "voice");
		WebAudio_VoiceSetGain(v, 0.5f);
		WebAudio_VoiceQueue(v, c.s->buffer, 0.0f, 0);
		WebAudio_Flush();
		sleepMs(c.s->info.durationMs() > 400 ? 330 : 280);
		// sine of 0.5 (the files) * 0.5 (voice gain); a mono sound plays on both speakers
		const double l = tone(0, c.f, 200), r = tone(1, c.f, 200);
		CHECK(l > 0.2 && l < 0.3, "%.0f Hz left %f", c.f, l);
		CHECK(r > 0.2 && r < 0.3, "%.0f Hz right %f", c.f, r);
		CHECK(tone(0, c.f * 1.5, 200) < 0.02, "%.0f Hz: no other tone expected, %f", c.f, tone(0, c.f * 1.5, 200));
		CHECK(waitFor(voiceDone, &v, 2500), "the sound never ended");
		WebAudio_DestroyVoice(v, 0.0f);
		WebAudio_Flush();
		sleepMs(150);
	}

	// stereo ADPCM: 660 Hz on the left, 990 Hz on the right
	{
		WebAudioVoice v = WebAudio_CreateVoice(WEBAUDIO_VOICE_2D);
		WebAudio_VoiceSetGain(v, 1.0f);
		WebAudio_VoiceQueue(v, adpcmSt.buffer, 0.0f, 0);
		WebAudio_Flush();
		sleepMs(350);
		CHECK(tone(0, 660, 200) > 0.4 && tone(0, 990, 200) < 0.03, "left %f / %f", tone(0, 660, 200), tone(0, 990, 200));
		CHECK(tone(1, 990, 200) > 0.4 && tone(1, 660, 200) < 0.03, "right %f / %f", tone(1, 990, 200), tone(1, 660, 200));
		CHECK(waitFor(voiceDone, &v, 2000), "stereo sound never ended");
		WebAudio_DestroyVoice(v, 0.0f);
	}
	WebAudio_Flush();
	sleepMs(150);

	// every voice that plays creates a source node, and an AudioBuffer was made per sound
	WebAudio_GetStats(&after);
	CHECK(after.buffersCreated - before.buffersCreated >= 4, "buffers created %u", after.buffersCreated - before.buffersCreated);
	CHECK(after.sourcesStarted - before.sourcesStarted >= 4, "sources started %u", after.sourcesStarted - before.sourcesStarted);
	CHECK(after.sourcesEnded - before.sourcesEnded >= 4, "sources ended %u", after.sourcesEnded - before.sourcesEnded);
	CHECK(after.errors == before.errors, "backend errors %u", after.errors);
	CHECK(liveBuffers() >= 4, "buffers alive %d", liveBuffers());

	WebAudio_DestroyBuffer(pcm.buffer);
	WebAudio_DestroyBuffer(pcm8.buffer);
	WebAudio_DestroyBuffer(adpcm.buffer);
	WebAudio_DestroyBuffer(adpcmSt.buffer);
	WebAudio_Flush();
	sleepMs(50);
	CHECK(liveBuffers() == 0, "buffers alive after destroy %d", liveBuffers());
}

static void testVolumeCategories()
{
	printf("== volume categories\n");
	using namespace WebAudio;
	MixSettings mix;
	mix.musicVolume = 0.8f;
	mix.speechVolume = 0.6f;
	mix.soundVolume = 0.5f;
	mix.sound3DVolume = 0.4f;
	mix.use3DSoundRangeVolumeFade = true;
	mix.fadeExponent = 4.0f;

	// the formula
	CHECK(fabsf(effectiveVolume(MixCategory::Music, 1.0f, mix) - 0.8f) < 1e-6f, "music");
	CHECK(fabsf(effectiveVolume(MixCategory::Speech, 0.5f, mix) - 0.3f) < 1e-6f, "speech");
	CHECK(fabsf(effectiveVolume(MixCategory::Sound, 1.0f, mix) - 0.5f) < 1e-6f, "sound");
	CHECK(fabsf(effectiveVolume(MixCategory::Sound3D, 1.0f, mix, true, 50, 100, 500) - 0.4f) < 1e-6f, "3D inside the minimum distance");
	CHECK(fabsf(effectiveVolume(MixCategory::Sound3D, 1.0f, mix, true, 300, 100, 500) - 0.4f * (1.0f - powf(0.5f, 4))) < 1e-5f, "3D fade");
	CHECK(effectiveVolume(MixCategory::Sound3D, 1.0f, mix, true, 500, 100, 500) == 0.0f, "3D at the maximum distance");
	CHECK(effectiveVolume(MixCategory::Sound3D, 1.0f, mix, true, 900, 100, 500) == 0.0f, "3D beyond the maximum distance");
	mix.use3DSoundRangeVolumeFade = false;
	CHECK(fabsf(effectiveVolume(MixCategory::Sound3D, 1.0f, mix, true, 300, 100, 500) - 0.4f) < 1e-6f, "3D without fade");
	mix.use3DSoundRangeVolumeFade = true;

	// and what comes out: the same tone through the four category volumes
	Sound s = makeTones(500, 0, 1.2, 44100);
	struct { MixCategory cat; bool pos; float dist; float expect; } cases[] = {
		{ MixCategory::Music, false, 0, 0.8f },
		{ MixCategory::Speech, false, 0, 0.6f },
		{ MixCategory::Sound, false, 0, 0.5f },
		{ MixCategory::Sound3D, true, 300, 0.4f * (1.0f - powf(0.5f, 4)) },
	};
	double base = 0;
	{
		WebAudioVoice v = WebAudio_CreateVoice(WEBAUDIO_VOICE_2D);
		WebAudio_VoiceSetGain(v, 1.0f);
		WebAudio_VoiceQueue(v, s.buffer, 0, 0);
		WebAudio_Flush();
		sleepMs(450);
		base = tone(0, 500, 250);
		WebAudio_DestroyVoice(v, 0.0f);
		WebAudio_Flush();
		sleepMs(150);
	}
	CHECK(base > 0.22 && base < 0.27, "reference level %f", base);
	for (auto &c : cases)
	{
		const float g = effectiveVolume(c.cat, 1.0f, mix, c.pos, c.dist, 100, 500);
		WebAudioVoice v = WebAudio_CreateVoice(WEBAUDIO_VOICE_2D);
		WebAudio_VoiceSetGain(v, g);
		WebAudio_VoiceQueue(v, s.buffer, 0, 0);
		WebAudio_Flush();
		sleepMs(450);
		const double level = tone(0, 500, 250);
		CHECK(fabs(level / base - c.expect) < 0.04, "category %d: level %f of %f is %f, expected %f", (int)c.cat, level, base, level / base, c.expect);
		WebAudio_DestroyVoice(v, 0.0f);
		WebAudio_Flush();
		sleepMs(150);
	}

	// gain changes while playing are smooth and reach the target (no clicks, no zipper)
	{
		WebAudioVoice v = WebAudio_CreateVoice(WEBAUDIO_VOICE_2D);
		WebAudio_VoiceSetGain(v, 1.0f);
		WebAudio_VoiceQueue(v, s.buffer, 0, 0);
		WebAudio_Flush();
		sleepMs(300);
		WebAudio_VoiceSetGain(v, 0.25f);
		WebAudio_Flush();
		sleepMs(250);
		const double level = tone(0, 500, 150);
		CHECK(fabs(level / base - 0.25) < 0.03, "after the gain change %f", level / base);
		CHECK(maxStep(0, 100) < 0.2, "step %f", maxStep(0, 100));
		WebAudio_DestroyVoice(v, 0.0f);
	}
	WebAudio_DestroyBuffer(s.buffer);
	WebAudio_Flush();
	sleepMs(150);
}

static void testPositional()
{
	printf("== 3D positional audio\n");
	Sound s = makeTones(440, 0, 4.0, 44100);

	struct Case { const char *name; float fwd[3]; float pos[3]; int dominant; }; // dominant: 0 left, 1 right, 2 centre
	const Case cases[] = {
		{ "source to the right, looking north", { 0, 1, 0 }, { 100, 0, 0 }, 1 },
		{ "source to the left, looking north", { 0, 1, 0 }, { -100, 0, 0 }, 0 },
		{ "source ahead, looking north", { 0, 1, 0 }, { 0, 100, 0 }, 2 },
		{ "source to the north, looking east: on the left", { 1, 0, 0 }, { 0, 100, 0 }, 0 },
		{ "source to the north, looking west: on the right", { -1, 0, 0 }, { 0, 100, 0 }, 1 },
		{ "source to the east, looking south: on the left", { 0, -1, 0 }, { 100, 0, 0 }, 0 },
	};
	for (const Case &c : cases)
	{
		const float lpos[3] = { 0, 0, 0 }, up[3] = { 0, 0, 1 };
		WebAudio_SetListener(lpos, c.fwd, up);
		WebAudioVoice v = WebAudio_CreateVoice(WEBAUDIO_VOICE_3D);
		WebAudio_VoiceSetGain(v, 1.0f);
		WebAudio_VoiceSetPosition(v, c.pos[0], c.pos[1], c.pos[2]);
		WebAudio_VoiceQueue(v, s.buffer, 0, 0);
		WebAudio_Flush();
		sleepMs(450);
		const double l = tone(0, 440, 250), r = tone(1, 440, 250);
		printf("  %s: left %.3f right %.3f\n", c.name, l, r);
		if (c.dominant == 1)
			CHECK(r > 0.2 && l < r * 0.1, "%s", c.name);
		else if (c.dominant == 0)
			CHECK(l > 0.2 && r < l * 0.1, "%s", c.name);
		else
			CHECK(fabs(l - r) < 0.02 && fabs(l - 0.173) < 0.02, "%s (equal power: -3 dB per ear)", c.name);
		// the panner and listener carry the world coordinates
		CHECK(fabs(pannerCoord(v, 0) - c.pos[0]) < 1e-3 && fabs(pannerCoord(v, 1) - c.pos[1]) < 1e-3 && fabs(pannerCoord(v, 2) - c.pos[2]) < 1e-3,
			"panner position %f %f %f", pannerCoord(v, 0), pannerCoord(v, 1), pannerCoord(v, 2));
		CHECK(fabs(listenerCoord(3) - c.fwd[0]) < 1e-3 && fabs(listenerCoord(4) - c.fwd[1]) < 1e-3 && fabs(listenerCoord(8) - 1.0) < 1e-3, "listener orientation");
		WebAudio_DestroyVoice(v, 0.0f);
		WebAudio_Flush();
		sleepMs(150);
	}

	// the listener moves: a source at the origin is heard on the left when the listener stands east of it, looking north
	{
		const float lpos[3] = { 100, 0, 0 }, fwd[3] = { 0, 1, 0 }, up[3] = { 0, 0, 1 };
		WebAudio_SetListener(lpos, fwd, up);
		WebAudioVoice v = WebAudio_CreateVoice(WEBAUDIO_VOICE_3D);
		WebAudio_VoiceSetGain(v, 1.0f);
		WebAudio_VoiceSetPosition(v, 0, 0, 0);
		WebAudio_VoiceQueue(v, s.buffer, 0, 0);
		WebAudio_Flush();
		sleepMs(450);
		CHECK(tone(0, 440, 250) > 0.2 && tone(1, 440, 250) < 0.03, "listener east of the source: left %f right %f", tone(0, 440, 250), tone(1, 440, 250));
		CHECK(fabs(listenerCoord(0) - 100) < 1e-3, "listener position");
		WebAudio_DestroyVoice(v, 0.0f);
	}
	// the panner does not attenuate with distance, the game does that through the gain
	{
		resetListener();
		WebAudioVoice v = WebAudio_CreateVoice(WEBAUDIO_VOICE_3D);
		WebAudio_VoiceSetGain(v, 1.0f);
		WebAudio_VoiceSetPosition(v, 0, 3000, 0);
		WebAudio_VoiceQueue(v, s.buffer, 0, 0);
		WebAudio_Flush();
		sleepMs(450);
		CHECK(fabs(tone(0, 440, 250) - 0.173) < 0.02, "a distant source keeps its level, %f", tone(0, 440, 250));
		CHECK(MAIN_THREAD_EM_ASM_DOUBLE({ return zhTest.pannerRolloff($0); }, v) == 0.0, "rolloff factor");
		WebAudio_DestroyVoice(v, 0.0f);
	}
	// HRTF panning model for headphones
	{
		resetListener();
		WebAudio_SetPanningModel(1);
		WebAudio_Flush();
		sleepMs(900);	// the browser loads the HRTF database in the background; the backend warms it up on this switch
		WebAudioVoice v = WebAudio_CreateVoice(WEBAUDIO_VOICE_3D);
		WebAudio_VoiceSetPosition(v, 100, 0, 0);
		WebAudio_VoiceQueue(v, s.buffer, 0, 0);
		WebAudio_Flush();
		sleepMs(450);
		CHECK(MAIN_THREAD_EM_ASM_INT({ return zhTest.pannerModel($0); }, v) == 1, "HRTF");
		CHECK(tone(1, 440, 250) > tone(0, 440, 250) * 1.4, "HRTF still pans right: left %f right %f", tone(0, 440, 250), tone(1, 440, 250));
		WebAudio_SetPanningModel(0);
		WebAudio_Flush();
		sleepMs(50);
		CHECK(MAIN_THREAD_EM_ASM_INT({ return zhTest.pannerModel($0); }, v) == 0, "back to equal power");
		WebAudio_DestroyVoice(v, 0.0f);
	}
	WebAudio_DestroyBuffer(s.buffer);
	WebAudio_Flush();
	sleepMs(150);
	resetListener();
}

static void testLowPass()
{
	printf("== occlusion low pass\n");
	Sound s = makeTones(500, 10000, 3.0, 44100);
	WebAudioVoice v = WebAudio_CreateVoice(WEBAUDIO_VOICE_3D);
	WebAudio_VoiceSetGain(v, 1.0f);
	WebAudio_VoiceSetPosition(v, 0, 100, 0);
	WebAudio_VoiceQueue(v, s.buffer, 0, 0);
	WebAudio_Flush();
	sleepMs(450);
	const double lo0 = tone(0, 500, 200), hi0 = tone(0, 10000, 200);
	CHECK(lo0 > 0.1 && hi0 > 0.1, "both tones audible: %f %f", lo0, hi0);
	WebAudio_VoiceSetLowPass(v, 1000.0f);
	WebAudio_Flush();
	sleepMs(500);
	const double lo1 = tone(0, 500, 200), hi1 = tone(0, 10000, 200);
	CHECK(hi1 < hi0 * 0.1, "10 kHz drops by more than 20 dB: %f -> %f", hi0, hi1);
	CHECK(fabs(lo1 / lo0 - 1.0) < 0.15, "500 Hz stays: %f -> %f", lo0, lo1);
	WebAudio_VoiceSetLowPass(v, 0.0f);
	WebAudio_Flush();
	sleepMs(500);
	CHECK(tone(0, 10000, 200) > hi0 * 0.9, "low pass removed again: %f", tone(0, 10000, 200));
	WebAudio_DestroyVoice(v, 0.0f);
	WebAudio_DestroyBuffer(s.buffer);
	WebAudio_Flush();
	sleepMs(150);
}

static void testLatency()
{
	printf("== latency from queueing a sound to the first rendered sample\n");
	Sound s = makeTones(440, 0, 1.0, 44100);
	double worst = 0, sum = 0;
	const int runs = 6;
	for (int i = 0; i < runs; ++i)
	{
		WebAudioVoice v = WebAudio_CreateVoice(WEBAUDIO_VOICE_2D);
		WebAudio_VoiceSetGain(v, 1.0f);
		sleepMs(300);
		const double t0 = nowMs();
		WebAudio_VoiceQueue(v, s.buffer, 0, 0);
		WebAudio_Flush();
		double dt = -1;
		while (nowMs() - t0 < 1000)
		{
			if (peak(0, 15) > 0.02)
			{
				dt = nowMs() - t0;
				break;
			}
			sleepMs(1);
		}
		printf("  run %d: %.0f ms\n", i, dt);
		worst = dt > worst ? dt : worst;
		sum += dt;
		WebAudio_DestroyVoice(v, 0.0f);
		WebAudio_Flush();
		sleepMs(200);
	}
	CHECK(worst > 0 && worst < 150, "latency up to %f ms (average %f)", worst, sum / runs);
	WebAudio_DestroyBuffer(s.buffer);
	WebAudio_Flush();
}

static void testChainingPauseAndLifetime()
{
	printf("== gapless chaining, pause/resume, stop\n");
	Sound s = makeTones(440, 0, 1.0, 44100);

	// four segments queued at once, played back to back
	{
		WebAudioVoice v = WebAudio_CreateVoice(WEBAUDIO_VOICE_2D);
		WebAudio_VoiceSetGain(v, 1.0f);
		for (int i = 0; i < 4; ++i)
			WebAudio_VoiceQueue(v, s.buffer, 0, 0);
		WebAudio_Flush();
		CHECK(WebAudio_VoicePendingSegments(v) == 4, "pending %u", WebAudio_VoicePendingSegments(v));
		sleepMs(200);
		const int32_t rem = WebAudio_VoiceRemainingMs(v);
		CHECK(rem > 3500 && rem <= 4000, "remaining %d ms", (int)rem);
		sleepMs(3500);
		// 3.5 s of a pure tone: no silence, no click at the seams
		CHECK(maxSilenceMs(0, 3400, 1e-3) < 0.3, "gap of %f ms", maxSilenceMs(0, 3400, 1e-3));
		CHECK(maxStep(0, 3400) < 0.08, "step %f", maxStep(0, 3400));
		CHECK(waitFor(voiceDone, &v, 1500), "chain never ended");
		CHECK(WebAudio_VoiceRemainingMs(v) == 0, "remaining after the end %d", (int)WebAudio_VoiceRemainingMs(v));
		WebAudio_DestroyVoice(v, 0.0f);
		WebAudio_Flush();
		sleepMs(150);
	}

	// the engine's pattern: the next portion is queued a little before the current one ends
	{
		WebAudioVoice v = WebAudio_CreateVoice(WEBAUDIO_VOICE_2D);
		WebAudio_VoiceSetGain(v, 1.0f);
		WebAudio_VoiceQueue(v, s.buffer, 0, 0);
		WebAudio_Flush();
		const double t0 = nowMs();
		sleepMs(60);
		while (WebAudio_VoiceRemainingMs(v) > 250 && nowMs() - t0 < 1500)
			sleepMs(5);
		const double queuedAt = nowMs();
		WebAudio_VoiceQueue(v, s.buffer, 0, 0);
		WebAudio_VoiceQueue(v, s.buffer, 0, 0);
		WebAudio_Flush();
		sleepMs(1500);
		CHECK(maxSilenceMs(0, 1700, 1e-3) < 0.3, "late queued segments leave a gap of %f ms at %f of 1700 (queued at %f ms)", maxSilenceMs(0, 1700, 1e-3), silenceStartMs(0, 1700, 1e-3), queuedAt - t0);
		CHECK(queuedAt - t0 > 600 && queuedAt - t0 < 900, "queued %f ms after the start", queuedAt - t0);
		CHECK(waitFor(voiceDone, &v, 2500), "second chain never ended");
		WebAudio_DestroyVoice(v, 0.0f);
		WebAudio_Flush();
		sleepMs(150);
	}

	// pause keeps the position, resume continues there
	{
		WebAudioVoice v = WebAudio_CreateVoice(WEBAUDIO_VOICE_2D);
		WebAudio_VoiceSetGain(v, 1.0f);
		for (int i = 0; i < 3; ++i)
			WebAudio_VoiceQueue(v, s.buffer, 0, 0);
		WebAudio_Flush();
		const double start = nowMs();
		sleepMs(1300);	// into the second segment
		const double pauseStart = nowMs();
		WebAudio_VoicePause(v, 1);
		WebAudio_Flush();
		sleepMs(150);
		CHECK(rms(0, 100) < 0.002 && rms(1, 100) < 0.002, "paused voice is silent: %f", rms(0, 100));
		CHECK(WebAudio_VoiceRemainingMs(v) == INT32_MAX, "paused voice has no end time");
		CHECK(WebAudio_VoicePendingSegments(v) == 2, "pending while paused %u", WebAudio_VoicePendingSegments(v));
		sleepMs(700);
		const double pausedFor = nowMs() - pauseStart;
		const double remainingBefore = 3000.0 - (pauseStart - start);
		WebAudio_VoicePause(v, 0);
		WebAudio_Flush();
		sleepMs(150);
		printf("  paused at %.0f ms for %.0f ms, %.0f ms of sound were left, remaining reported now: %d ms\n", pauseStart - start, pausedFor, remainingBefore, (int)WebAudio_VoiceRemainingMs(v));
		CHECK(tone(0, 440, 100) > 0.2, "resumed: %f", tone(0, 440, 100));
		CHECK(waitFor(voiceDone, &v, 4000), "paused chain never ended");
		const double total = nowMs() - start - pausedFor;
		CHECK(fabs(total - 3000.0) < 250.0, "3 s of sound plus the pause took %f ms of play time", total);
		WebAudio_DestroyVoice(v, 0.0f);
		WebAudio_Flush();
		sleepMs(150);
	}

	// cancelling the pending segments lets the playing one finish
	{
		WebAudioVoice v = WebAudio_CreateVoice(WEBAUDIO_VOICE_2D);
		WebAudio_VoiceSetGain(v, 1.0f);
		for (int i = 0; i < 3; ++i)
			WebAudio_VoiceQueue(v, s.buffer, 0, 0);
		WebAudio_Flush();
		sleepMs(400);
		WebAudio_VoiceCancelPending(v);
		WebAudio_Flush();
		sleepMs(100);
		CHECK(WebAudio_VoicePendingSegments(v) == 1, "pending after cancel %u", WebAudio_VoicePendingSegments(v));
		CHECK(waitFor(voiceDone, &v, 1500), "cancelled chain never ended");
		sleepMs(300);
		CHECK(rms(0, 200) < 0.002, "silence after the playing segment: %f", rms(0, 200));
		WebAudio_DestroyVoice(v, 0.0f);
		WebAudio_Flush();
		sleepMs(150);
	}

	// stopping a playing voice is quick and does not click
	{
		const int voicesBefore = liveVoices();
		WebAudioVoice v = WebAudio_CreateVoice(WEBAUDIO_VOICE_2D);
		WebAudio_VoiceSetGain(v, 1.0f);
		WebAudio_VoiceQueue(v, s.buffer, 0, 0);
		WebAudio_Flush();
		sleepMs(300);
		CHECK(liveVoices() == voicesBefore + 1, "voices alive %d", liveVoices());
		WebAudio_DestroyVoice(v, 0.01f);
		WebAudio_Flush();
		sleepMs(150);
		CHECK(rms(0, 100) < 0.002, "silent after stopping: %f", rms(0, 100));
		CHECK(liveVoices() == voicesBefore, "voices alive after stop %d", liveVoices());
		CHECK(WebAudio_VoicePendingSegments(v) == 0, "destroyed voice has no pending segments");
		// the slot is reused with a new generation; the old id must not reach the new voice
		WebAudioVoice w = WebAudio_CreateVoice(WEBAUDIO_VOICE_2D);
		CHECK(w != v, "ids are not reused immediately");
		WebAudio_VoiceSetGain(v, 0.0f);
		WebAudio_DestroyVoice(v, 0.0f);	// stale id: ignored
		WebAudio_VoiceQueue(w, s.buffer, 0, 0);
		WebAudio_Flush();
		sleepMs(300);
		CHECK(tone(0, 440, 150) > 0.2, "the stale id left the new voice alone: %f", tone(0, 440, 150));
		WebAudio_DestroyVoice(w, 0.0f);
	}

	// a delayed start
	{
		WebAudioVoice v = WebAudio_CreateVoice(WEBAUDIO_VOICE_2D);
		WebAudio_VoiceSetGain(v, 1.0f);
		WebAudio_VoiceQueue(v, s.buffer, 0.5f, 0);
		WebAudio_Flush();
		sleepMs(300);
		CHECK(rms(0, 150) < 0.002, "still silent during the delay: %f", rms(0, 150));
		sleepMs(450);
		CHECK(tone(0, 440, 150) > 0.2, "plays after the delay: %f", tone(0, 440, 150));
		WebAudio_DestroyVoice(v, 0.0f);
	}

	// pitch
	{
		WebAudioVoice v = WebAudio_CreateVoice(WEBAUDIO_VOICE_2D);
		WebAudio_VoiceSetGain(v, 1.0f);
		WebAudio_VoiceSetPitch(v, 1.5f);
		WebAudio_VoiceQueue(v, s.buffer, 0, 0);
		WebAudio_Flush();
		sleepMs(350);
		CHECK(tone(0, 660, 200) > 0.2 && tone(0, 440, 200) < 0.03, "pitch shifted to 660 Hz: %f / %f", tone(0, 660, 200), tone(0, 440, 200));
		CHECK(WebAudio_VoiceRemainingMs(v) < 700, "pitched voice is shorter: %d", (int)WebAudio_VoiceRemainingMs(v));
		WebAudio_DestroyVoice(v, 0.0f);
	}
	WebAudio_DestroyBuffer(s.buffer);
	WebAudio_Flush();
	sleepMs(150);
}

static void testSuspendedContext()
{
	printf("== sounds while the browser blocks audio\n");
	Sound s = makeTones(440, 0, 0.5, 44100);
	WebAudioStats before, after;
	MAIN_THREAD_EM_ASM({ zhTest.suspend(); });
	sleepMs(150);
	CHECK(WebAudio_GetState() == WEBAUDIO_STATE_SUSPENDED, "state %d", WebAudio_GetState());

	// A sound effect that is triggered now waits for the context for a moment, then it is dropped.
	WebAudio_GetStats(&before);
	WebAudioVoice v = WebAudio_CreateVoice(WEBAUDIO_VOICE_2D);
	WebAudio_VoiceSetGain(v, 1.0f);
	WebAudio_VoiceQueue(v, s.buffer, 0, 0);
	WebAudio_Flush();
	sleepMs(100);
	CHECK(WebAudio_VoicePendingSegments(v) == 1, "the sound waits for the context: %u", WebAudio_VoicePendingSegments(v));
	sleepMs(450);
	WebAudio_GetStats(&after);
	CHECK(after.segmentsDropped == before.segmentsDropped + 1, "stale sound effect dropped (%u -> %u)", before.segmentsDropped, after.segmentsDropped);
	CHECK(WebAudio_VoicePendingSegments(v) == 0, "a dropped sound is finished: %u", WebAudio_VoicePendingSegments(v));
	WebAudio_DestroyVoice(v, 0.0f);

	// A sound triggered just before the context runs (the click that unlocks the audio) plays.
	v = WebAudio_CreateVoice(WEBAUDIO_VOICE_2D);
	WebAudio_VoiceSetGain(v, 1.0f);
	WebAudio_VoiceQueue(v, s.buffer, 0, 0);
	WebAudio_Flush();
	sleepMs(60);
	WebAudio_Resume();
	MAIN_THREAD_EM_ASM({ zhTest.resume(); });
	CHECK(waitFor([](void *) { return WebAudio_GetState() == WEBAUDIO_STATE_RUNNING; }, nullptr, 3000), "context resumes");
	sleepMs(200);
	CHECK(tone(0, 440, 100) > 0.2, "the waiting sound plays once the context runs: %f", tone(0, 440, 100));
	CHECK(waitFor(voiceDone, &v, 1500), "it ends");
	WebAudio_DestroyVoice(v, 0.0f);

	// A stream is not dropped: it starts when the context runs.
	MAIN_THREAD_EM_ASM({ zhTest.suspend(); });
	sleepMs(150);
	std::vector<uint8_t> mp3 = readFile("music_mono_22050_1100.mp3");
	WebAudioVoice m = WebAudio_CreateVoice(WEBAUDIO_VOICE_2D);
	WebAudio_VoiceSetGain(m, 1.0f);
	WebAudioStream st = WebAudio_StreamOpen(mp3.data(), (uint32_t)mp3.size(), m, 1);
	CHECK(st != 0, "stream open");
	sleepMs(600);
	CHECK(WebAudio_VoicePendingSegments(m) > 0, "stream chunks wait for the context: %u", WebAudio_VoicePendingSegments(m));
	WebAudio_Resume();
	MAIN_THREAD_EM_ASM({ zhTest.resume(); });
	CHECK(waitFor([](void *) { return WebAudio_GetState() == WEBAUDIO_STATE_RUNNING; }, nullptr, 3000), "context resumes");
	sleepMs(400);
	CHECK(tone(0, 1100, 250) > 0.4, "the waiting stream plays after the resume: %f", tone(0, 1100, 250));
	WebAudio_StreamClose(st);
	WebAudio_DestroyVoice(m, 0.0f);
	WebAudio_DestroyBuffer(s.buffer);
	WebAudio_Flush();
	sleepMs(200);
}

// The heap grows while the audio runs (the game loads hundreds of megabytes): the main thread side reads
// commands and PCM blocks from the heap, which may have moved into a larger memory since the last command.
static void testHeapGrowth()
{
	printf("== heap growth while playing\n");
	Sound s = makeTones(440, 0, 4.0, 44100);
	WebAudioVoice v = WebAudio_CreateVoice(WEBAUDIO_VOICE_3D);
	WebAudio_VoiceSetGain(v, 1.0f);
	WebAudio_VoiceSetPosition(v, 50, 100, 0);
	WebAudio_VoiceQueue(v, s.buffer, 0, 0);
	WebAudio_Flush();
	sleepMs(300);
	const double before = MAIN_THREAD_EM_ASM_DOUBLE({ return HEAPU8.length; });
	std::vector<uint8_t *> blocks;
	for (int i = 0; i < 6; ++i)
	{
		uint8_t *p = static_cast<uint8_t *>(malloc(64u << 20));
		if (!p)
			break;
		memset(p, i, 64u << 20);	// touch it
		blocks.push_back(p);
		WebAudio_VoiceSetPosition(v, 50.0f + (float)i, 100, 0);
		WebAudio_Flush();
		// new sounds uploaded from the grown heap
		Sound extra = makeTones(500 + 100 * i, 0, 0.3, 22050);
		WebAudioVoice w = WebAudio_CreateVoice(WEBAUDIO_VOICE_2D);
		WebAudio_VoiceSetGain(w, 0.2f);
		WebAudio_VoiceQueue(w, extra.buffer, 0, 0);
		WebAudio_Flush();
		sleepMs(60);
		WebAudio_DestroyVoice(w, 0.0f);
		WebAudio_DestroyBuffer(extra.buffer);
	}
	const double after = MAIN_THREAD_EM_ASM_DOUBLE({ return HEAPU8.length; });
	printf("  heap %.0f MB -> %.0f MB\n", before / 1048576.0, after / 1048576.0);
	CHECK(after > before, "the heap did not grow");
	sleepMs(300);
	WebAudioStats stats;
	WebAudio_GetStats(&stats);
	CHECK(stats.errors == 0, "interpreter errors %u", stats.errors);
	CHECK(tone(0, 440, 150) + tone(1, 440, 150) > 0.1, "still playing: %f %f", tone(0, 440, 150), tone(1, 440, 150));
	CHECK(fabs(pannerCoord(v, 0) - 55.0) < 1e-3, "the last position arrived: %f", pannerCoord(v, 0));
	// and a stream decoded on its thread while the heap grows
	std::vector<uint8_t> mp3 = readFile("music_stereo_44100_1320.mp3");
	WebAudioVoice m = WebAudio_CreateVoice(WEBAUDIO_VOICE_2D);
	WebAudio_VoiceSetGain(m, 0.5f);
	WebAudioStream st = WebAudio_StreamOpen(mp3.data(), (uint32_t)mp3.size(), m, 1);
	for (int i = 0; i < 4; ++i)
	{
		uint8_t *p = static_cast<uint8_t *>(malloc(48u << 20));
		if (p)
		{
			memset(p, 1, 48u << 20);
			blocks.push_back(p);
		}
		sleepMs(150);
	}
	CHECK(st != 0 && tone(0, 1320, 250) > 0.15, "music during growth: %f", tone(0, 1320, 250));
	WebAudio_GetStats(&stats);
	CHECK(stats.errors == 0, "interpreter errors %u", stats.errors);
	WebAudio_StreamClose(st);
	WebAudio_DestroyVoice(m, 0.0f);
	WebAudio_DestroyVoice(v, 0.0f);
	WebAudio_DestroyBuffer(s.buffer);
	WebAudio_Flush();
	for (uint8_t *p : blocks)
		free(p);
	sleepMs(150);
}

static void testStreams()
{
	printf("== streamed music and speech (decoder thread)\n");
	std::vector<uint8_t> mp3 = readFile("music_stereo_44100_1320.mp3");
	std::vector<uint8_t> speech = readFile("music_mono_22050_1100.mp3");
	CHECK(!mp3.empty() && !speech.empty(), "files");

	// Looping stereo music, with the engine thread blocked for a while.
	WebAudioVoice m = WebAudio_CreateVoice(WEBAUDIO_VOICE_2D);
	WebAudio_VoiceSetGain(m, 0.5f);
	WebAudioStream st = WebAudio_StreamOpen(mp3.data(), (uint32_t)mp3.size(), m, 1);
	CHECK(st != 0, "stream open");
	CHECK(WebAudio_StreamsHaveThread() == 1, "decoder thread");
	CHECK(WebAudio_StreamOpen(nullptr, 0, m, 0) == 0, "empty file");
	const uint8_t junk[64] = { 1, 2, 3 };
	CHECK(WebAudio_StreamOpen(junk, sizeof(junk), m, 0) == 0, "garbage file");
	sleepMs(600);
	CHECK(fabs(tone(0, 1320, 300) - 0.25) < 0.04, "music left %f", tone(0, 1320, 300));
	CHECK(fabs(tone(1, 1760, 300) - 0.25) < 0.04, "music right %f", tone(1, 1760, 300));
	CHECK(tone(0, 1760, 300) < 0.02, "channels are not mixed up");
	CHECK(WebAudio_VoiceLoopsDone(m) == 0, "no loop yet");

	// Block the engine thread for 2.5 s, no flush, no update: the music must not stop
	// (6 chunks of half a second were queued ahead, and the decoder thread tops them up).
	spinMs(2500);
	CHECK(maxSilenceMs(0, 2400, 1e-3) < 0.5, "music is interrupted for %f ms while the engine is busy", maxSilenceMs(0, 2400, 1e-3));
	CHECK(tone(0, 1320, 300) > 0.2, "music still plays after the stall: %f", tone(0, 1320, 300));

	// The 4 s track has looped by now (0.6 + 2.5 s elapsed, so keep going until it did).
	sleepMs(1500);
	CHECK(WebAudio_VoiceLoopsDone(m) >= 1, "loops done %u", WebAudio_VoiceLoopsDone(m));
	// The loop point is seamless: a pure tone has no hole and no jump there
	// (the window of 4.2 s contains a loop seam, whichever loop it is).
	CHECK(maxSilenceMs(0, 4200, 1e-3) < 0.5, "seam gap %f ms", maxSilenceMs(0, 4200, 1e-3));
	CHECK(maxStep(0, 4200) < 0.2, "seam step %f", maxStep(0, 4200));
	CHECK(tone(0, 1320, 300) > 0.2 && tone(1, 1760, 300) > 0.2, "music after the loop");
	CHECK(WebAudio_StreamIsFinished(st) == 0, "looping music never finishes");
	CHECK(WebAudio_VoicePendingSegments(m) <= 7, "stream keeps only a few chunks queued: %u", WebAudio_VoicePendingSegments(m));
	const uint32_t loops = WebAudio_VoiceLoopsDone(m);
	sleepMs(4200);
	CHECK(WebAudio_VoiceLoopsDone(m) >= loops + 1, "another loop: %u -> %u", loops, WebAudio_VoiceLoopsDone(m));

	// pausing music pauses the stream
	WebAudio_VoicePause(m, 1);
	WebAudio_Flush();
	sleepMs(200);
	CHECK(rms(0, 150) < 0.002, "paused music is silent");
	WebAudio_VoicePause(m, 0);
	WebAudio_Flush();
	sleepMs(300);
	CHECK(tone(0, 1320, 150) > 0.2, "music resumes");

	// track change: close the first, start another on a new voice
	WebAudio_StreamClose(st);
	WebAudio_DestroyVoice(m, 0.02f);
	WebAudioVoice sp = WebAudio_CreateVoice(WEBAUDIO_VOICE_2D);
	WebAudio_VoiceSetGain(sp, 1.0f);
	WebAudioStream st2 = WebAudio_StreamOpen(speech.data(), (uint32_t)speech.size(), sp, 0);
	CHECK(st2 != 0, "speech stream");
	sleepMs(500);
	CHECK(tone(0, 1100, 250) > 0.4 && tone(0, 1320, 250) < 0.03, "the new track plays alone: %f / %f", tone(0, 1100, 250), tone(0, 1320, 250));
	CHECK(WebAudio_StreamIsFinished(st2) == 0, "not finished yet");
	const double t0 = nowMs();
	bool fin = false;
	while (nowMs() - t0 < 4000)
	{
		if (WebAudio_StreamIsFinished(st2))
		{
			fin = true;
			break;
		}
		sleepMs(10);
	}
	CHECK(fin, "speech did not finish");
	// the file is 2 s long (the LAME tag trims the encoder delay): it ended 2 s after it started
	CHECK(fabs((nowMs() - t0) - 1500.0) < 400.0, "speech took %f ms more", nowMs() - t0);
	CHECK(WebAudio_VoiceLoopsDone(sp) == 1, "the end counts as a pass: %u", WebAudio_VoiceLoopsDone(sp));
	WebAudio_StreamClose(st2);
	WebAudio_DestroyVoice(sp, 0.0f);
	WebAudio_Flush();
	sleepMs(200);
	CHECK(WebAudio_StreamIsFinished(st2) == 1, "closed streams are finished");

	// no thread mode: the caller pumps
	{
		WebAudioVoice v = WebAudio_CreateVoice(WEBAUDIO_VOICE_2D);
		WebAudio_VoiceSetGain(v, 1.0f);
		WebAudioStream s3 = WebAudio_StreamOpen(speech.data(), (uint32_t)speech.size(), v, 0);
		CHECK(s3 != 0, "stream");
		for (int i = 0; i < 100; ++i)
		{
			WebAudio_PumpStreams();
			sleepMs(10);
		}
		CHECK(WebAudio_VoicePendingSegments(v) > 0 || WebAudio_StreamIsFinished(s3), "pumping from the caller works");
		WebAudio_StreamClose(s3);
		WebAudio_DestroyVoice(v, 0.0f);
		WebAudio_Flush();
	}
}

// The device can be shut down and started again (the engine does that when the options change the audio device).
static void testRestart()
{
	printf("== shutdown and restart\n");
	WebAudio_Shutdown();
	sleepMs(300);
	CHECK(WebAudio_GetState() == WEBAUDIO_STATE_NONE, "state after shutdown %d", WebAudio_GetState());
	CHECK(WebAudio_CreateVoice(WEBAUDIO_VOICE_2D) == 0, "no voices without a device");
	CHECK(WebAudio_CreateBuffer(nullptr, 0, 0, 0) == 0, "no buffers without a device");
	WebAudio_Flush();	// harmless

	MAIN_THREAD_EM_ASM({ zhTest.reset(); });
	CHECK(WebAudio_Init(48000) == 1, "init again");
	CHECK(MAIN_THREAD_EM_ASM_INT({ return zhTest.startRecording(); }) == 1, "recording tap");
	CHECK(waitFor([](void *) { return WebAudio_GetState() == WEBAUDIO_STATE_RUNNING && MAIN_THREAD_EM_ASM_INT({ return zhTest.ready; }) == 1; }, nullptr, 3000), "running again");
	CHECK(WebAudio_GetSampleRate() == 48000, "new sample rate %d", WebAudio_GetSampleRate());
	Sound s = makeTones(440, 0, 1.0, 44100);	// a 44.1 kHz sound on a 48 kHz context
	WebAudioVoice v = WebAudio_CreateVoice(WEBAUDIO_VOICE_2D);
	WebAudio_VoiceSetGain(v, 1.0f);
	WebAudio_VoiceQueue(v, s.buffer, 0, 0);
	WebAudio_Flush();
	sleepMs(400);
	CHECK(fabs(tone(0, 440, 200) - 0.244) < 0.02, "plays at the right pitch and level: %f", tone(0, 440, 200));
	CHECK(waitFor(voiceDone, &v, 1500), "ends");
	WebAudio_DestroyVoice(v, 0.0f);
	WebAudio_DestroyBuffer(s.buffer);
	WebAudio_Flush();
	sleepMs(100);
}

// The engine pattern: a frame loop that yields once per frame ---------------------------------

struct FrameLoop
{
	WebAudioVoice voice = 0;
	Sound sound;
	int frame = 0;
	double start = 0;
	double l[16] = {}, r[16] = {};
	int sampled = 0;
	bool started = false;
};

static FrameLoop g_loop;

static void finishTest()
{
	printf("%d checks, %d failures\n", g_checks, g_failures);
	printf(g_failures ? "RESULT: FAIL\n" : "RESULT: PASS\n");
	printf("TEST_DONE\n");

	// The runner inspects the live Web Audio graph now (it sets zhTestAck when it is done), then
	// the device is shut down.
	for (int i = 0; i < 100 && !MAIN_THREAD_EM_ASM_INT({ return globalThis.zhTestAck ? 1 : 0; }); ++i)
		sleepMs(50);
	WebAudio_Shutdown();
	sleepMs(300);
	printf("SHUTDOWN: %s\n", (WebAudio_GetState() == WEBAUDIO_STATE_NONE &&
		MAIN_THREAD_EM_ASM_INT({ return globalThis.zhWebAudio ? 1 : 0; }) == 0) ? "ok" : "FAIL");
}

static void finishFrameLoop();

static void frameStep()
{
	FrameLoop &f = g_loop;
	if (!f.started)
	{
		f.started = true;
		f.start = nowMs();
		WebAudio_VoiceQueue(f.voice, f.sound.buffer, 0, 0);
	}
	const double t = (nowMs() - f.start) / 1000.0;	// seconds

	// "update": a source flies from the left of the listener to the right
	const float x = -200.0f + 400.0f * (float)(t / 1.6);
	WebAudio_VoiceSetPosition(f.voice, x, 100.0f, 0.0f);
	const float lpos[3] = { 0, 0, 0 }, fwd[3] = { 0, 1, 0 }, up[3] = { 0, 0, 1 };
	WebAudio_SetListener(lpos, fwd, up);
	WebAudio_Flush();

	// sample the output every 100 ms (cheap enough to do in the frame, like the real thing would query the voices)
	if (t >= 0.2 && f.sampled < 14 && t >= 0.2 + 0.1 * f.sampled)
	{
		f.l[f.sampled] = tone(0, 440, 60);
		f.r[f.sampled] = tone(1, 440, 60);
		++f.sampled;
	}
	++f.frame;
	if (t > 1.7)
	{
		emscripten_cancel_main_loop();
		finishFrameLoop();
	}
}

static void finishFrameLoop()
{
	FrameLoop &f = g_loop;
	printf("== frame loop (60 Hz, yields every frame)\n");
	const double elapsed = nowMs() - f.start;
	printf("  %d frames in %.0f ms\n", f.frame, elapsed);
	CHECK(f.frame > 40, "only %d frames", f.frame);
	CHECK(f.sampled >= 12, "sampled %d", f.sampled);
	// left dominant at first, right at the end, and the ratio only moves one way (a little noise allowed)
	double firstDiff = f.l[0] - f.r[0], lastDiff = f.l[f.sampled - 1] - f.r[f.sampled - 1];
	CHECK(firstDiff > 0.08, "starts on the left: %f %f", f.l[0], f.r[0]);
	CHECK(lastDiff < -0.08, "ends on the right: %f %f", f.l[f.sampled - 1], f.r[f.sampled - 1]);
	int reversals = 0;
	for (int i = 1; i < f.sampled; ++i)
		if ((f.l[i] - f.r[i]) > (f.l[i - 1] - f.r[i - 1]) + 0.06)
			++reversals;
	CHECK(reversals == 0, "panning sweep is monotonic, %d reversals", reversals);
	CHECK(maxStep(0, 1000) < 0.3, "no clicks while the source moves: %f", maxStep(0, 1000));

	WebAudio_DestroyVoice(f.voice, 0.0f);
	WebAudio_DestroyBuffer(f.sound.buffer);
	WebAudio_Flush();

	WebAudioStats stats;
	WebAudio_GetStats(&stats);
	printf("  backend: %u commands, %u buffers, %u sources started / %u ended, %u voices, %u stream chunks, %u dropped, %u errors\n",
		stats.commandsRun, stats.buffersCreated, stats.sourcesStarted, stats.sourcesEnded, stats.voicesCreated, stats.streamChunks, stats.segmentsDropped, stats.errors);
	CHECK(stats.errors == 0, "the interpreter reported %u errors", stats.errors);
	if (g_allScenarios)
		CHECK(stats.voicesCreated > 15 && stats.streamChunks > 10, "counters");
	CHECK(WebAudio_GetState() == WEBAUDIO_STATE_RUNNING, "still running at the end");
	finishTest();
}

static void startFrameLoop()
{
	g_loop.sound = makeTones(440, 0, 4.0, 44100);
	g_loop.voice = WebAudio_CreateVoice(WEBAUDIO_VOICE_3D);
	WebAudio_VoiceSetGain(g_loop.voice, 1.0f);
	WebAudio_VoiceSetPosition(g_loop.voice, -200, 100, 0);
	WebAudio_Flush();
	emscripten_set_main_loop(frameStep, 0, 0);
	emscripten_set_main_loop_timing(EM_TIMING_SETTIMEOUT, 16);
}

// Without --autoplay-policy=no-user-gesture-required (run_test.mjs gesture --policy) a page that had no
// user gesture gets a suspended context; the first click must resume it.
static void testGesture()
{
	printf("== autoplay policy and user gesture\n");
	CHECK(WebAudio_Init(44100) == 1, "Web Audio is not available");
	CHECK(MAIN_THREAD_EM_ASM_INT({ return zhTest.startRecording(); }) == 1, "recording tap");
	const int initial = WebAudio_GetState();
	printf("INITIAL_STATE %d\n", initial);
	if (initial != WEBAUDIO_STATE_RUNNING)
	{
		// the sound that the click itself triggers must not be lost
		Sound s = makeTones(440, 0, 0.5, 44100);
		WebAudioVoice v = WebAudio_CreateVoice(WEBAUDIO_VOICE_2D);
		WebAudio_VoiceSetGain(v, 1.0f);
		printf("WAITING_FOR_GESTURE\n");
		const bool resumed = waitFor([](void *) { return WebAudio_GetState() == WEBAUDIO_STATE_RUNNING; }, nullptr, 30000);
		CHECK(resumed, "the first user gesture resumes the context");
		if (resumed)
		{
			WebAudio_VoiceQueue(v, s.buffer, 0, 0);
			WebAudio_Flush();
			sleepMs(300);
			CHECK(waitFor([](void *) { return MAIN_THREAD_EM_ASM_INT({ return zhTest.ready; }) == 1; }, nullptr, 3000), "tap");
			sleepMs(100);
			CHECK(tone(0, 440, 150) > 0.2, "sound plays after the gesture: %f", tone(0, 440, 150));
		}
		WebAudio_DestroyVoice(v, 0.0f);
		WebAudio_DestroyBuffer(s.buffer);
	}
	else
	{
		printf("  (the browser did not block audio)\n");
	}
	finishTest();
}

static bool wanted(int argc, char **argv, const char *name)
{
	if (argc <= 1)
		return true;
	for (int i = 1; i < argc; ++i)
		if (strcmp(argv[i], name) == 0)
			return true;
	return false;
}

// Optional arguments (?args=pause,streams in the page URL) select scenarios.
int main(int argc, char **argv)
{
	g_allScenarios = argc <= 1;
	if (argc > 1 && strcmp(argv[1], "gesture") == 0)
	{
		testGesture();
		return 0;
	}
	printf("web_audio_test: engine thread is %s\n", emscripten_is_main_runtime_thread() ? "the main thread" : "a worker (as in the game)");
	testContext();
	if (WebAudio_GetState() == WEBAUDIO_STATE_RUNNING)
	{
		if (wanted(argc, argv, "decoders")) testDecoders();
		if (wanted(argc, argv, "decode")) testPcmAndAdpcm();
		if (wanted(argc, argv, "volume")) testVolumeCategories();
		if (wanted(argc, argv, "positional")) testPositional();
		if (wanted(argc, argv, "lowpass")) testLowPass();
		if (wanted(argc, argv, "latency")) testLatency();
		if (wanted(argc, argv, "chain")) testChainingPauseAndLifetime();
		if (wanted(argc, argv, "suspend")) testSuspendedContext();
		if (wanted(argc, argv, "streams")) testStreams();
		if (wanted(argc, argv, "heap")) testHeapGrowth();
		if (wanted(argc, argv, "restart")) testRestart();
		startFrameLoop();	// finishes the test from the loop
		return 0;
	}
	printf("%d checks, %d failures\n", g_checks, g_failures);
	printf("RESULT: FAIL\nTEST_DONE\n");
	return 1;
}
