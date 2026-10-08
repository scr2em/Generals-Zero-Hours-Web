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
** WebAssembly port: the Input Method Manager API used by the game's IME
** support. The browser delivers composed text as ordinary character input,
** so there is never an input context: see src/win32_imm.cpp.
*/
#pragma once

#include "windows.h"

#ifdef __cplusplus

typedef struct tagCANDIDATELIST {
	DWORD dwSize;
	DWORD dwStyle;
	DWORD dwCount;
	DWORD dwSelection;
	DWORD dwPageStart;
	DWORD dwPageSize;
	DWORD dwOffset[1];
} CANDIDATELIST, *LPCANDIDATELIST;

#define GCS_COMPREADSTR     0x0001
#define GCS_COMPREADATTR    0x0002
#define GCS_COMPREADCLAUSE  0x0004
#define GCS_COMPSTR         0x0008
#define GCS_COMPATTR        0x0010
#define GCS_COMPCLAUSE      0x0020
#define GCS_CURSORPOS       0x0080
#define GCS_DELTASTART      0x0100
#define GCS_RESULTREADSTR   0x0200
#define GCS_RESULTREADCLAUSE 0x0400
#define GCS_RESULTSTR       0x0800
#define GCS_RESULTCLAUSE    0x1000
#define CS_INSERTCHAR       0x2000
#define CS_NOMOVECARET      0x4000

#define IMN_CLOSESTATUSWINDOW   0x0001
#define IMN_OPENSTATUSWINDOW    0x0002
#define IMN_CHANGECANDIDATE     0x0003
#define IMN_CLOSECANDIDATE      0x0004
#define IMN_OPENCANDIDATE       0x0005
#define IMN_SETCONVERSIONMODE   0x0006
#define IMN_SETSENTENCEMODE     0x0007
#define IMN_SETOPENSTATUS       0x0008
#define IMN_SETCANDIDATEPOS     0x0009
#define IMN_SETCOMPOSITIONFONT  0x000A
#define IMN_SETCOMPOSITIONWINDOW 0x000B
#define IMN_SETSTATUSWINDOWPOS  0x000C
#define IMN_GUIDELINE           0x000D
#define IMN_PRIVATE             0x000E

#define IMC_GETCANDIDATEPOS     0x0007
#define IMC_SETCANDIDATEPOS     0x0008
#define IMC_GETCOMPOSITIONFONT  0x0009
#define IMC_SETCOMPOSITIONFONT  0x000A
#define IMC_GETCOMPOSITIONWINDOW 0x000B
#define IMC_SETCOMPOSITIONWINDOW 0x000C
#define IMC_GETSTATUSWINDOWPOS  0x000F
#define IMC_SETSTATUSWINDOWPOS  0x0010
#define IMC_CLOSESTATUSWINDOW   0x0021
#define IMC_OPENSTATUSWINDOW    0x0022

#define IMR_COMPOSITIONWINDOW   0x0001
#define IMR_CANDIDATEWINDOW     0x0002
#define IMR_COMPOSITIONFONT     0x0003
#define IMR_RECONVERTSTRING     0x0004
#define IMR_CONFIRMRECONVERTSTRING 0x0005

#define ISC_SHOWUICANDIDATEWINDOW 0x00000001
#define ISC_SHOWUICOMPOSITIONWINDOW 0x80000000
#define ISC_SHOWUIGUIDELINE     0x40000000
#define ISC_SHOWUIALLCANDIDATEWINDOW 0x0000000F
#define ISC_SHOWUIALL           0xC000000F

#define IME_CMODE_ALPHANUMERIC  0x0000
#define IME_CMODE_NATIVE        0x0001
#define IME_CMODE_CHINESE       IME_CMODE_NATIVE
#define IME_CMODE_HANGUL        IME_CMODE_NATIVE
#define IME_CMODE_JAPANESE      IME_CMODE_NATIVE
#define IME_CMODE_KATAKANA      0x0002
#define IME_CMODE_LANGUAGE      0x0003
#define IME_CMODE_FULLSHAPE     0x0008
#define IME_CMODE_ROMAN         0x0010
#define IME_CMODE_CHARCODE      0x0020
#define IME_CMODE_HANJACONVERT  0x0040
#define IME_CMODE_SOFTKBD       0x0080
#define IME_CMODE_NOCONVERSION  0x0100
#define IME_CMODE_EUDC          0x0200
#define IME_CMODE_SYMBOL        0x0400
#define IME_CMODE_FIXED         0x0800

#define IME_SMODE_NONE          0x0000
#define IME_SMODE_PLAURALCLAUSE 0x0001
#define IME_SMODE_SINGLECONVERT 0x0002
#define IME_SMODE_AUTOMATIC     0x0004
#define IME_SMODE_PHRASEPREDICT 0x0008
#define IME_SMODE_CONVERSATION  0x0010

#define IME_CAND_UNKNOWN        0x0000
#define IME_CAND_READ           0x0001
#define IME_CAND_CODE           0x0002
#define IME_CAND_MEANING        0x0003
#define IME_CAND_RADICAL        0x0004
#define IME_CAND_STROKE         0x0005

#define IGP_PROPERTY            0x00000004
#define IME_PROP_UNICODE        0x00080000

#ifndef WM_IME_STARTCOMPOSITION
#define WM_IME_STARTCOMPOSITION 0x010D
#define WM_IME_ENDCOMPOSITION   0x010E
#define WM_IME_COMPOSITION      0x010F
#define WM_IME_KEYLAST          0x010F
#endif
#ifndef WM_IME_SETCONTEXT
#define WM_IME_SETCONTEXT       0x0281
#endif
#ifndef WM_IME_NOTIFY
#define WM_IME_NOTIFY           0x0282
#define WM_IME_CONTROL          0x0283
#define WM_IME_COMPOSITIONFULL  0x0284
#define WM_IME_SELECT           0x0285
#define WM_IME_CHAR             0x0286
#define WM_IME_REQUEST          0x0288
#define WM_IME_KEYDOWN          0x0290
#define WM_IME_KEYUP            0x0291
#endif

extern "C" {
HIMC  WINAPI ImmGetContext(HWND hWnd);
BOOL  WINAPI ImmReleaseContext(HWND hWnd, HIMC hIMC);
HIMC  WINAPI ImmCreateContext(void);
BOOL  WINAPI ImmDestroyContext(HIMC hIMC);
HIMC  WINAPI ImmAssociateContext(HWND hWnd, HIMC hIMC);
HWND  WINAPI ImmGetDefaultIMEWnd(HWND hWnd);
BOOL  WINAPI ImmGetConversionStatus(HIMC hIMC, LPDWORD lpfdwConversion, LPDWORD lpfdwSentence);
DWORD WINAPI ImmGetProperty(HKL hKL, DWORD fdwIndex);
LONG  WINAPI ImmGetCompositionStringA(HIMC hIMC, DWORD dwIndex, LPVOID lpBuf, DWORD dwBufLen);
LONG  WINAPI ImmGetCompositionStringW(HIMC hIMC, DWORD dwIndex, LPVOID lpBuf, DWORD dwBufLen);
DWORD WINAPI ImmGetCandidateListCountA(HIMC hIMC, LPDWORD lpdwListCount);
DWORD WINAPI ImmGetCandidateListCountW(HIMC hIMC, LPDWORD lpdwListCount);
DWORD WINAPI ImmGetCandidateListA(HIMC hIMC, DWORD deIndex, LPCANDIDATELIST lpCandList, DWORD dwBufLen);
DWORD WINAPI ImmGetCandidateListW(HIMC hIMC, DWORD deIndex, LPCANDIDATELIST lpCandList, DWORD dwBufLen);
}
#define ImmGetCompositionString ImmGetCompositionStringA

#endif
