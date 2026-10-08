/*
**	Command & Conquer Generals Zero Hour(tm)
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
/*
** WebAssembly port: the multimedia timer API.
*/
#pragma once

#include "windows.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef UINT MMRESULT;
#define TIMERR_NOERROR 0
#define TIMERR_NOCANDO 97
#define MMSYSERR_NOERROR 0
#define TIME_ONESHOT  0x0000
#define TIME_PERIODIC 0x0001

typedef struct timecaps_tag { UINT wPeriodMin; UINT wPeriodMax; } TIMECAPS, *LPTIMECAPS;
typedef void (CALLBACK *LPTIMECALLBACK)(UINT uTimerID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2);

DWORD    WINAPI timeGetTime(void);
MMRESULT WINAPI timeBeginPeriod(UINT uPeriod);
MMRESULT WINAPI timeEndPeriod(UINT uPeriod);
MMRESULT WINAPI timeGetDevCaps(LPTIMECAPS ptc, UINT cbtc);
MMRESULT WINAPI timeSetEvent(UINT uDelay, UINT uResolution, LPTIMECALLBACK fptc, DWORD_PTR dwUser, UINT fuEvent);
MMRESULT WINAPI timeKillEvent(UINT uTimerID);

typedef struct tWAVEFORMATEX {
	WORD  wFormatTag;
	WORD  nChannels;
	DWORD nSamplesPerSec;
	DWORD nAvgBytesPerSec;
	WORD  nBlockAlign;
	WORD  wBitsPerSample;
	WORD  cbSize;
} WAVEFORMATEX, *PWAVEFORMATEX, *LPWAVEFORMATEX;
typedef struct waveformat_tag {
	WORD  wFormatTag;
	WORD  nChannels;
	DWORD nSamplesPerSec;
	DWORD nAvgBytesPerSec;
	WORD  nBlockAlign;
} WAVEFORMAT;
typedef struct pcmwaveformat_tag { WAVEFORMAT wf; WORD wBitsPerSample; } PCMWAVEFORMAT;
#define WAVE_FORMAT_PCM 1
#define WAVE_FORMAT_ADPCM 2
#define WAVE_FORMAT_IMA_ADPCM 0x11

typedef DWORD FOURCC;
#define mmioFOURCC(c0, c1, c2, c3) \
	((DWORD)(BYTE)(c0) | ((DWORD)(BYTE)(c1) << 8) | ((DWORD)(BYTE)(c2) << 16) | ((DWORD)(BYTE)(c3) << 24))
#define MAKEFOURCC mmioFOURCC

#ifdef __cplusplus
}
#endif
