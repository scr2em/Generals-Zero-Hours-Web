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
** WebAssembly port: the part of ATL that the embedded browser glue code is
** declared with. There is no COM in the browser, so none of the machinery
** (module, class factories, interface maps, aggregation) does anything: the
** classes only have to exist so that the code compiles, and nothing creates
** objects of them at run time.
*/
#pragma once

#include <windows.h>
#include <objbase.h>
#include <oaidl.h>
#include <ocidl.h>

#ifdef __cplusplus

class CComSingleThreadModel {};
class CComMultiThreadModel {};
typedef CComSingleThreadModel CComObjectThreadModel;

class CComModule
{
public:
	HRESULT Init(void *pObjMap, HINSTANCE hInst, const GUID *plibid = nullptr) { return S_OK; }
	void Term() {}
};

template <class ThreadModel>
class CComObjectRootEx {};

template <class T, const CLSID *pclsid = nullptr>
class CComCoClass {};

// ATL's CComObject derives from the class the game declares and supplies
// IUnknown for it; the game's classes implement IUnknown themselves.
template <class Base>
class CComObject : public Base
{
public:
	typedef Base _BaseClass;
	CComObject() {}
};

// Smart pointer that queries for an interface.
template <class T>
class CComQIPtr
{
public:
	CComQIPtr(IUnknown *lp) : p(nullptr)
	{
		if (lp != nullptr)
			lp->QueryInterface(__uuidof(T), reinterpret_cast<void **>(&p));
	}
	~CComQIPtr() { if (p != nullptr) p->Release(); }
	operator T *() const { return p; }
	T *operator->() const { return p; }
	T *p;
private:
	CComQIPtr(const CComQIPtr &);
	CComQIPtr &operator=(const CComQIPtr &);
};

// The same, with the interface named by its IID (MinGW's spelling).
#define I_ID(Itype) Itype, &IID_##Itype
template <class T, const IID *piid>
class CComQIIDPtr
{
public:
	CComQIIDPtr(IUnknown *lp) : p(nullptr)
	{
		if (lp != nullptr)
			lp->QueryInterface(*piid, reinterpret_cast<void **>(&p));
	}
	~CComQIIDPtr() { if (p != nullptr) p->Release(); }
	operator T *() const { return p; }
	T *operator->() const { return p; }
	T *p;
private:
	CComQIIDPtr(const CComQIIDPtr &);
	CComQIIDPtr &operator=(const CComQIIDPtr &);
};

#endif
