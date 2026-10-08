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
** WebAssembly port: the OLE Automation declarations (BSTR, IDispatch and the
** type library interfaces) that the embedded browser glue code is written
** against. The browser has no OLE: type libraries can never be loaded and no
** dispatch object can be created, see src/ole_automation.cpp.
*/
#pragma once

#include "objbase.h"

#ifdef __cplusplus

typedef WCHAR       OLECHAR;
typedef OLECHAR    *LPOLESTR;
typedef const OLECHAR *LPCOLESTR;
typedef OLECHAR    *BSTR;
typedef LONG        DISPID;
typedef DISPID      MEMBERID;
typedef DWORD       HREFTYPE;
typedef unsigned short VARTYPE;
typedef short       VARIANT_BOOL;

#define VARIANT_TRUE  ((VARIANT_BOOL)-1)
#define VARIANT_FALSE ((VARIANT_BOOL)0)

#define DISPID_UNKNOWN (-1)
#define DISPID_VALUE   0

#define DISP_E_MEMBERNOTFOUND ((HRESULT)0x80020003L)
#define TYPE_E_CANTLOADLIBRARY ((HRESULT)0x80029C4AL)

typedef struct tagVARIANT {
	VARTYPE vt;
	WORD    wReserved1;
	WORD    wReserved2;
	WORD    wReserved3;
	union {
		LONG         lVal;
		BYTE         bVal;
		short        iVal;
		float        fltVal;
		double       dblVal;
		VARIANT_BOOL boolVal;
		BSTR         bstrVal;
		IUnknown    *punkVal;
		void        *byref;
	};
} VARIANT, VARIANTARG;

typedef struct tagDISPPARAMS {
	VARIANTARG *rgvarg;
	DISPID     *rgdispidNamedArgs;
	UINT        cArgs;
	UINT        cNamedArgs;
} DISPPARAMS;

typedef struct tagEXCEPINFO {
	WORD   wCode;
	WORD   wReserved;
	BSTR   bstrSource;
	BSTR   bstrDescription;
	BSTR   bstrHelpFile;
	DWORD  dwHelpContext;
	void  *pvReserved;
	HRESULT (*pfnDeferredFillIn)(struct tagEXCEPINFO *);
	LONG   scode;
} EXCEPINFO;

typedef struct tagTYPEATTR TYPEATTR;
typedef struct tagTLIBATTR TLIBATTR;
typedef struct tagTYPEDESC TYPEDESC;

interface ITypeInfo;
interface ITypeLib;

interface IDispatch : public IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT *pctinfo) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT iTInfo, LCID lcid, ITypeInfo **ppTInfo) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID riid, LPOLESTR *rgszNames, UINT cNames, LCID lcid, DISPID *rgDispId) = 0;
	virtual HRESULT STDMETHODCALLTYPE Invoke(DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr) = 0;
};
typedef IDispatch *LPDISPATCH;

/* The two type information interfaces are only ever used through pointers
** that the (always failing) LoadTypeLib hands out. */
interface ITypeInfo : public IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE GetTypeAttr(TYPEATTR **ppTypeAttr) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetTypeComp(void **ppTComp) = 0;
};

interface ITypeLib : public IUnknown
{
	virtual UINT STDMETHODCALLTYPE GetTypeInfoCount(void) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT index, ITypeInfo **ppTInfo) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetTypeInfoOfGuid(REFGUID guid, ITypeInfo **ppTInfo) = 0;
};

extern "C" {
BSTR    WINAPI SysAllocString(const OLECHAR *psz);
BSTR    WINAPI SysAllocStringLen(const OLECHAR *pch, UINT cch);
void    WINAPI SysFreeString(BSTR bstrString);
UINT    WINAPI SysStringLen(BSTR bstr);
HRESULT WINAPI LoadTypeLib(const OLECHAR *szFile, ITypeLib **pptlib);
HRESULT WINAPI CreateStdDispatch(IUnknown *punkOuter, void *pvThis, ITypeInfo *ptinfo, IUnknown **ppunkStdDisp);
}

#endif
