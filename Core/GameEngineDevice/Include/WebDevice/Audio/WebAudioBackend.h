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

// FILE: WebAudioBackend.h ////////////////////////////////////////////////////
//
// The browser sound device of the WebAssembly build: a thin layer over the Web Audio API that
// the WebAudioManager (and the tests) call from the engine thread.
//
// Why a thin layer of its own and not OpenAL (-lopenal) or miniaudio:
//
//  - A Web Audio AudioContext exists only on the main browser thread, the engine runs on a worker
//    (-sPROXY_TO_PTHREAD) that yields to the browser once per frame. Emscripten's OpenAL does work
//    from a worker, but by proxying every single AL call synchronously to the main thread: a
//    round trip measured at 60 to 120 microseconds in web_audio_test, against 0.14 microseconds
//    for queueing a command here. A busy frame sets positions, gains and polls states of dozens
//    of voices, hundreds of calls, which would cost the engine thread tens of milliseconds a
//    frame. Commands batched into one message per frame cost it nothing.
//  - Music has to keep playing while the engine thread is busy (loading a map): a decoder thread
//    of its own feeds the voices, instead of buffers queued from the engine's frame loop.
//  - The Web Audio graph does the mixing, resampling, panning and HRTF in native code on the audio
//    thread. miniaudio (whose Emscripten backends deliver to an AudioWorklet or a script
//    processor) would mix in WebAssembly instead: CPU on the cores of the engine for what the
//    browser does natively, a worklet next to the pthreads of the engine, and none of the
//    spatialisation of the browser.
//  - Gapless sequences (the attack, loop and decay of a sound), pausing and resuming a voice at
//    the same position and the browser's autoplay rules (sounds that are triggered while the
//    context is still blocked) are the game's needs, the OpenAL model of sources and queues does
//    not map onto them any better than the node graph.
//
// How it works:
//
//  - Calls on the engine thread (or any thread) append commands to a queue. WebAudio_Flush hands
//    the queued commands to the main browser thread in one asynchronous message, where a small
//    JavaScript interpreter turns them into Web Audio nodes (EM_JS in WebAudioBackend.cpp, no
//    extra link flags or JS files needed). Nothing here ever waits for the main thread, except
//    WebAudio_Init, WebAudio_Shutdown and WebAudio_GetStats.
//  - State that the engine thread needs to know (has a voice finished, how often did the music
//    loop, when does a voice end, is the context running) is written by the main thread into a
//    block of shared memory and read from there without any message.
//  - Sound effects are decoded to PCM by the caller (WebAudio::decodeAll), uploaded once as
//    a buffer (AudioBuffer) and then played by any number of voices.
//  - Music and speech are streams: a worker thread decodes the file in half second chunks and
//    pushes them to a voice, which schedules them back to back on the audio clock. The decoder
//    thread works without the engine thread, so music keeps playing while the engine is busy,
//    for instance loading a map. If a thread cannot be created WebAudio_PumpStreams decodes on
//    the caller's thread.
//
// A voice is a chain of Web Audio nodes: sources -> [low pass -> panner] -> gain -> master.
// It plays a queue of segments (buffers) back to back, which is what the engine's attack ->
// loop -> decay sequences need to be gapless, and it can be paused and resumed.
//
// All functions are thread safe, with the exception of the life cycle functions.
//
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t WebAudioBuffer;		// 0 is invalid
typedef uint32_t WebAudioVoice;			// 0 is invalid
typedef uint32_t WebAudioStream;		// 0 is invalid

enum
{
	WEBAUDIO_MAX_VOICES = 512,
};

typedef enum WebAudioContextState
{
	WEBAUDIO_STATE_NONE = 0,			// WebAudio_Init has not run or failed
	WEBAUDIO_STATE_SUSPENDED = 1,	// created, but the browser waits for a user gesture
	WEBAUDIO_STATE_RUNNING = 2,
	WEBAUDIO_STATE_CLOSED = 3,
} WebAudioContextState;

typedef enum WebAudioVoiceKind
{
	WEBAUDIO_VOICE_2D = 0,				// plays as it is (mono is copied to both speakers)
	WEBAUDIO_VOICE_3D = 1,				// positioned relative to the listener, input must be mono
} WebAudioVoiceKind;

enum
{
	// WebAudio_VoiceQueue flags
	WEBAUDIO_SEGMENT_ENDS_LOOP = 1,	// counts as one completed pass of looping music when it ends
};

// ---- life cycle (engine thread) --------------------------------------------------------------

// Creates the AudioContext and installs the main thread side. sampleRate is the rate of the
// context (the game's OutputRate); 0 lets the browser choose. Returns 1 if the Web Audio API is
// available. Calling it again is harmless. The context normally starts suspended unless the page
// had a user gesture before (the launcher's Play click); document level gesture handlers resume
// it on the first click or key press otherwise, see WebAudio_Resume.
int WebAudio_Init(int sampleRate);
void WebAudio_Shutdown(void);

// Resumes the context. Browsers only honour this inside a user gesture handler; call it from
// one (the launcher's click handler can use window.zhWebAudio.resume() once the module runs).
void WebAudio_Resume(void);

int WebAudio_GetState(void);					// WebAudioContextState
int WebAudio_GetSampleRate(void);			// rate of the context, 0 before the context exists

// Total volume, 0..1.
void WebAudio_SetMasterVolume(float volume);

// 0: equal power panning between the speakers, 1: HRTF binaural (for headphones).
void WebAudio_SetPanningModel(int hrtf);

// The listener, in game world units: position, the direction it looks at and its up vector.
void WebAudio_SetListener(const float position[3], const float forward[3], const float up[3]);

// Sends the queued commands to the browser thread. The WebAudioManager calls it once per update.
void WebAudio_Flush(void);

// ---- sample buffers --------------------------------------------------------------------------

// Uploads PCM (interleaved, 16 bit) as a playable buffer. The data is copied. Returns 0 if the
// device is not available.
WebAudioBuffer WebAudio_CreateBuffer(const int16_t *interleaved, uint32_t frames, uint32_t channels, uint32_t sampleRate);
void WebAudio_DestroyBuffer(WebAudioBuffer buffer);

// ---- voices ----------------------------------------------------------------------------------

// Returns 0 if all voices are in use or the device is not available.
WebAudioVoice WebAudio_CreateVoice(WebAudioVoiceKind kind);

// Stops the voice, with a short fade out to avoid a click, and frees it.
void WebAudio_DestroyVoice(WebAudioVoice voice, float fadeOutSeconds);

// Appends buffer to the segments of the voice: it starts when the segments queued before it have
// ended (gapless), or delaySeconds from now if the voice is idle. A voice that is idle when a
// segment is queued while the context is not running drops the segment, so that sounds triggered
// before the first user gesture do not all play at once later.
void WebAudio_VoiceQueue(WebAudioVoice voice, WebAudioBuffer buffer, float delaySeconds, int flags);

// Removes the queued segments that have not begun to play, the playing one continues.
void WebAudio_VoiceCancelPending(WebAudioVoice voice);

void WebAudio_VoiceSetGain(WebAudioVoice voice, float gain);						// smoothed
void WebAudio_VoiceSetPitch(WebAudioVoice voice, float rate);						// applies to segments queued afterwards
void WebAudio_VoiceSetPan(WebAudioVoice voice, float pan);							// 2D voices, -1 left .. 1 right
void WebAudio_VoiceSetPosition(WebAudioVoice voice, float x, float y, float z);	// 3D voices, world units
void WebAudio_VoiceSetLowPass(WebAudioVoice voice, float cutoffHz);				// 3D voices, 0 = off
void WebAudio_VoicePause(WebAudioVoice voice, int paused);

// Queries, valid for a voice that is not destroyed.
// Number of segments queued that have not finished. A voice is silent and done when this is 0
// and the owner has nothing more to queue. Segments that are still waiting for the main thread
// to see them are counted.
uint32_t WebAudio_VoicePendingSegments(WebAudioVoice voice);
// How many segments flagged WEBAUDIO_SEGMENT_ENDS_LOOP have ended.
uint32_t WebAudio_VoiceLoopsDone(WebAudioVoice voice);
// Milliseconds until the last queued segment ends; 0 if nothing is queued, INT32_MAX while paused.
int32_t WebAudio_VoiceRemainingMs(WebAudioVoice voice);

// ---- streams (music and speech) ---------------------------------------------------------------

// Decodes the file image (a copy is made) on the decoder thread and plays it on voice, which
// must be idle. loop != 0 restarts at the end forever; each pass counts for
// WebAudio_VoiceLoopsDone. Returns 0 if the data is not a supported file.
WebAudioStream WebAudio_StreamOpen(const void *fileData, uint32_t fileSize, WebAudioVoice voice, int loop);
void WebAudio_StreamClose(WebAudioStream stream);
// 1 when the decoder has reached the end of a non looping file and everything it decoded has
// been played (or the voice is gone).
int WebAudio_StreamIsFinished(WebAudioStream stream);
// Decodes and uploads what the streams need now on the calling thread. The decoder thread does
// this by itself; call this only if WebAudio_StreamsHaveThread is 0.
void WebAudio_PumpStreams(void);
int WebAudio_StreamsHaveThread(void);

// ---- diagnostics -----------------------------------------------------------------------------

typedef struct WebAudioStats
{
	uint32_t commandsRun;
	uint32_t buffersCreated;
	uint32_t sourcesStarted;
	uint32_t sourcesEnded;
	uint32_t voicesCreated;
	uint32_t streamChunks;
	uint32_t segmentsDropped;
	uint32_t errors;
} WebAudioStats;

// Reads the counters of the main thread interpreter (blocks until the main thread answers, for
// tests and the debug display only). Returns 0 if the device is not available.
int WebAudio_GetStats(WebAudioStats *stats);

#ifdef __cplusplus
}
#endif
