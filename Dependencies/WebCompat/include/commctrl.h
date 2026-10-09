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
** WebAssembly port: the common controls that the debug tools use. Controls do
** not exist in the page: the control messages go to windows that ignore them.
*/
#pragma once

#include "windows.h"

#define LVM_FIRST            0x1000
#define LVM_SETITEMA         (LVM_FIRST + 6)
#define LVM_INSERTITEMA      (LVM_FIRST + 7)
#define LVM_DELETEALLITEMS   (LVM_FIRST + 9)
#define LVM_INSERTCOLUMNA    (LVM_FIRST + 27)

#define LVIF_TEXT   0x0001
#define LVIF_IMAGE  0x0002
#define LVIF_PARAM  0x0004
#define LVIF_STATE  0x0008

#define LVCF_FMT    0x0001
#define LVCF_WIDTH  0x0002
#define LVCF_TEXT   0x0004
#define LVCF_SUBITEM 0x0008

#define LVCFMT_LEFT   0x0000
#define LVCFMT_RIGHT  0x0001
#define LVCFMT_CENTER 0x0002

typedef struct tagLVCOLUMNA
{
	UINT  mask;
	int   fmt;
	int   cx;
	LPSTR pszText;
	int   cchTextMax;
	int   iSubItem;
} LVCOLUMNA, LVCOLUMN, *LPLVCOLUMN;

typedef struct tagLVITEMA
{
	UINT   mask;
	int    iItem;
	int    iSubItem;
	UINT   state;
	UINT   stateMask;
	LPSTR  pszText;
	int    cchTextMax;
	int    iImage;
	LPARAM lParam;
} LVITEMA, LVITEM, *LPLVITEM;

#ifdef __cplusplus
extern "C" {
#endif
void WINAPI InitCommonControls(void);
#ifdef __cplusplus
}
#endif

#define ListView_InsertColumn(hwnd, iCol, pcol) (int)SendMessage((hwnd), LVM_INSERTCOLUMNA, (WPARAM)(int)(iCol), (LPARAM)(const LVCOLUMN *)(pcol))
#define ListView_InsertItem(hwnd, pitem) (int)SendMessage((hwnd), LVM_INSERTITEMA, 0, (LPARAM)(const LVITEM *)(pitem))
#define ListView_SetItem(hwnd, pitem) (BOOL)SendMessage((hwnd), LVM_SETITEMA, 0, (LPARAM)(const LVITEM *)(pitem))
#define ListView_DeleteAllItems(hwnd) (BOOL)SendMessage((hwnd), LVM_DELETEALLITEMS, 0, 0L)
