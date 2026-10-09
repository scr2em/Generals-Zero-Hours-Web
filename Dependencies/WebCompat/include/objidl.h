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
** WebAssembly port: the COM stream and storage interfaces. Only declarations,
** nothing in the game implements or creates these.
*/
#pragma once

#include "objbase.h"

#ifdef __cplusplus
typedef struct tagSTATSTG {
	LPWSTR         pwcsName;
	DWORD          type;
	ULARGE_INTEGER cbSize;
	FILETIME       mtime;
	FILETIME       ctime;
	FILETIME       atime;
	DWORD          grfMode;
	DWORD          grfLocksSupported;
	CLSID          clsid;
	DWORD          grfStateBits;
	DWORD          reserved;
} STATSTG;

interface ISequentialStream : public IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE Read(void *pv, ULONG cb, ULONG *pcbRead) = 0;
	virtual HRESULT STDMETHODCALLTYPE Write(const void *pv, ULONG cb, ULONG *pcbWritten) = 0;
};

interface IStream : public ISequentialStream
{
	virtual HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER dlibMove, DWORD dwOrigin, ULARGE_INTEGER *plibNewPosition) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER libNewSize) = 0;
	virtual HRESULT STDMETHODCALLTYPE CopyTo(IStream *pstm, ULARGE_INTEGER cb, ULARGE_INTEGER *pcbRead, ULARGE_INTEGER *pcbWritten) = 0;
	virtual HRESULT STDMETHODCALLTYPE Commit(DWORD grfCommitFlags) = 0;
	virtual HRESULT STDMETHODCALLTYPE Revert(void) = 0;
	virtual HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) = 0;
	virtual HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) = 0;
	virtual HRESULT STDMETHODCALLTYPE Stat(STATSTG *pstatstg, DWORD grfStatFlag) = 0;
	virtual HRESULT STDMETHODCALLTYPE Clone(IStream **ppstm) = 0;
};
typedef IStream *LPSTREAM;
#endif
