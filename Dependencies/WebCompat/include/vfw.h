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
** WebAssembly port: the part of Video for Windows that writes AVI files, used
** by the movie capture of the renderer (WW3D::Begin_Movie_Capture). The
** browser cannot write AVI files, so opening a file always fails and the
** capture is never started; see src/vfw_stub.cpp.
*/
#pragma once

#include "windows.h"
#include "mmsystem.h"

#ifdef __cplusplus

typedef struct IAVIFile  *PAVIFILE;
typedef struct IAVIStream *PAVISTREAM;

#define streamtypeVIDEO mmioFOURCC('v', 'i', 'd', 's')
#define streamtypeAUDIO mmioFOURCC('a', 'u', 'd', 's')

#define AVIERR_OK        0L
#define AVIERR_UNSUPPORTED ((HRESULT)0x80044065L)
#define AVIERR_FILEOPEN  ((HRESULT)0x80044069L)

typedef struct {
	DWORD fccType;
	DWORD fccHandler;
	DWORD dwFlags;
	DWORD dwCaps;
	WORD  wPriority;
	WORD  wLanguage;
	DWORD dwScale;
	DWORD dwRate;
	DWORD dwStart;
	DWORD dwLength;
	DWORD dwInitialFrames;
	DWORD dwSuggestedBufferSize;
	DWORD dwQuality;
	DWORD dwSampleSize;
	RECT  rcFrame;
	DWORD dwEditCount;
	DWORD dwFormatChangeCount;
	char  szName[64];
} AVISTREAMINFO;

#define AVIIF_KEYFRAME 0x00000010L

extern "C" {
void    WINAPI AVIFileInit(void);
void    WINAPI AVIFileExit(void);
HRESULT WINAPI AVIFileOpenA(PAVIFILE *ppfile, LPCSTR szFile, UINT mode, LPCLSID lpHandler);
HRESULT WINAPI AVIFileCreateStreamA(PAVIFILE pfile, PAVISTREAM *ppavi, AVISTREAMINFO *psi);
ULONG   WINAPI AVIFileRelease(PAVIFILE pfile);
HRESULT WINAPI AVIStreamSetFormat(PAVISTREAM pavi, LONG lPos, LPVOID lpFormat, LONG cbFormat);
HRESULT WINAPI AVIStreamWrite(PAVISTREAM pavi, LONG lStart, LONG lSamples, LPVOID lpBuffer, LONG cbBuffer, DWORD dwFlags, LONG *plSampWritten, LONG *plBytesWritten);
ULONG   WINAPI AVIStreamRelease(PAVISTREAM pavi);
}

#define AVIFileOpen AVIFileOpenA
#define AVIFileCreateStream AVIFileCreateStreamA

#endif
