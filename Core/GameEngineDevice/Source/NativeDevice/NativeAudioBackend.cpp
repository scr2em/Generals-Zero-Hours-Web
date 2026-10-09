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

// FILE: NativeAudioBackend.cpp ///////////////////////////////////////////////
//
// The Web Audio backend (WebDevice/Audio/WebAudioBackend.h) of the native headless build: there is
// no sound device. The engine runs headless there, with WebAudioManagerDummy, which still decodes
// the audio files for their lengths (the scripts need them) but never plays anything; these
// functions are only here so that the Web Audio manager links. WebAudio_Init() fails, so even a
// full WebAudioManager would run without a device.
//
///////////////////////////////////////////////////////////////////////////////

#include "WebDevice/Audio/WebAudioBackend.h"

#include <string.h>

extern "C"
{

int WebAudio_Init(int) { return 0; }
void WebAudio_Shutdown(void) {}
void WebAudio_Resume(void) {}
int WebAudio_GetState(void) { return WEBAUDIO_STATE_NONE; }
int WebAudio_GetSampleRate(void) { return 0; }
void WebAudio_SetMasterVolume(float) {}
void WebAudio_SetPanningModel(int) {}
void WebAudio_SetListener(const float *, const float *, const float *) {}
void WebAudio_Flush(void) {}

WebAudioBuffer WebAudio_CreateBuffer(const int16_t *, uint32_t, uint32_t, uint32_t) { return 0; }
void WebAudio_DestroyBuffer(WebAudioBuffer) {}

WebAudioVoice WebAudio_CreateVoice(WebAudioVoiceKind) { return 0; }
void WebAudio_DestroyVoice(WebAudioVoice, float) {}
void WebAudio_VoiceQueue(WebAudioVoice, WebAudioBuffer, float, int) {}
void WebAudio_VoiceCancelPending(WebAudioVoice) {}
void WebAudio_VoiceSetGain(WebAudioVoice, float) {}
void WebAudio_VoiceSetPitch(WebAudioVoice, float) {}
void WebAudio_VoiceSetPan(WebAudioVoice, float) {}
void WebAudio_VoiceSetPosition(WebAudioVoice, float, float, float) {}
void WebAudio_VoiceSetLowPass(WebAudioVoice, float) {}
void WebAudio_VoicePause(WebAudioVoice, int) {}
uint32_t WebAudio_VoicePendingSegments(WebAudioVoice) { return 0; }
uint32_t WebAudio_VoiceLoopsDone(WebAudioVoice) { return 0; }
int32_t WebAudio_VoiceRemainingMs(WebAudioVoice) { return 0; }

WebAudioStream WebAudio_StreamOpen(const void *, uint32_t, WebAudioVoice, int) { return 0; }
void WebAudio_StreamClose(WebAudioStream) {}
int WebAudio_StreamIsFinished(WebAudioStream) { return 1; }
void WebAudio_PumpStreams(void) {}
int WebAudio_StreamsHaveThread(void) { return 0; }

int WebAudio_GetStats(WebAudioStats *stats)
{
	if (stats)
		memset(stats, 0, sizeof(*stats));
	return 0;
}

}
