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
** WebAssembly port: the COM declaration macros and base interfaces needed
** by the DirectX 8 headers and the few COM users in the game.
*/
#pragma once

#include "windows.h"

#ifndef interface
#define interface struct
#endif

#ifdef __cplusplus
#define STDMETHOD(method)        virtual HRESULT STDMETHODCALLTYPE method
#define STDMETHOD_(type, method) virtual type STDMETHODCALLTYPE method
#define STDMETHODV(method)       virtual HRESULT STDMETHODVCALLTYPE method
#define STDMETHODV_(type, method) virtual type STDMETHODVCALLTYPE method
#define PURE = 0
#define THIS_
#define THIS void
#define DECLARE_INTERFACE(iface)            interface iface
#define DECLARE_INTERFACE_(iface, baseiface) interface iface : public baseiface
#define DECLARE_INTERFACE_IID_(iface, baseiface, iid) interface iface : public baseiface
#endif

#define STDMETHODVCALLTYPE
#define STDMETHODIMP          HRESULT STDMETHODCALLTYPE
#define STDMETHODIMP_(type)   type STDMETHODCALLTYPE
#define STDAPI                extern "C" HRESULT STDAPICALLTYPE
#define STDAPI_(type)         extern "C" type STDAPICALLTYPE
#define WINOLEAPI             STDAPI
#define WINOLEAPI_(type)      STDAPI_(type)

#define CLSCTX_INPROC_SERVER  0x1
#define CLSCTX_INPROC_HANDLER 0x2
#define CLSCTX_LOCAL_SERVER   0x4
#define CLSCTX_SERVER         (CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER)
#define CLSCTX_ALL            (CLSCTX_INPROC_SERVER | CLSCTX_INPROC_HANDLER | CLSCTX_LOCAL_SERVER)
#define COINIT_APARTMENTTHREADED 0x2
#define COINIT_MULTITHREADED  0x0

#ifdef __cplusplus
inline bool operator==(const GUID &a, const GUID &b) { return memcmp(&a, &b, sizeof(GUID)) == 0; }
inline bool operator!=(const GUID &a, const GUID &b) { return !(a == b); }
#define IsEqualGUID(a, b) ((a) == (b))
#define IsEqualIID(a, b) ((a) == (b))
#define IsEqualCLSID(a, b) ((a) == (b))

#define __uuidof(x) IID_##x

extern "C" const IID IID_IUnknown;
extern "C" const IID IID_IClassFactory;
extern "C" const IID IID_IDispatch;

interface IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) = 0;
	virtual ULONG STDMETHODCALLTYPE AddRef(void) = 0;
	virtual ULONG STDMETHODCALLTYPE Release(void) = 0;
};
typedef IUnknown *LPUNKNOWN;

interface IClassFactory : public IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown *pUnkOuter, REFIID riid, void **ppvObject) = 0;
	virtual HRESULT STDMETHODCALLTYPE LockServer(BOOL fLock) = 0;
};
#endif

#ifdef __cplusplus
extern "C" {
#endif
HRESULT WINAPI CoInitialize(LPVOID pvReserved);
HRESULT WINAPI CoInitializeEx(LPVOID pvReserved, DWORD dwCoInit);
void    WINAPI CoUninitialize(void);
HRESULT WINAPI CoCreateInstance(REFCLSID rclsid, void *pUnkOuter, DWORD dwClsContext, REFIID riid, LPVOID *ppv);
LPVOID  WINAPI CoTaskMemAlloc(SIZE_T cb);
void    WINAPI CoTaskMemFree(LPVOID pv);
HRESULT WINAPI OleInitialize(LPVOID pvReserved);
void    WINAPI OleUninitialize(void);
#ifdef __cplusplus
}
#endif

#include "objidl.h"
