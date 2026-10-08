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
** WebAssembly port: common definitions of the Direct3D 8 on WebGL2 library.
*/
#pragma once

#include "d3d8_headers.h"

#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "WebD3D8/WebD3D8.h"

namespace webd3d8 {

//------------------------------------------------------------------------------
// Global configuration (see WebD3D8.h)
//------------------------------------------------------------------------------
struct Config
{
    std::string canvas = "#canvas";
    unsigned desktopWidth = 0;
    unsigned desktopHeight = 0;
    unsigned vsVersion = 0;
    unsigned psVersion = 0;
    int presentMode = WEBD3D8_PRESENT_EXPLICIT;
    WebD3D8_PlatformHooks hooks = {};
    bool debug = false;
    bool releaseTextureShadows = false;
    bool disableS3TC = false;
};
Config &GetConfig();

//------------------------------------------------------------------------------
// Logging
//------------------------------------------------------------------------------
void Log(const char *fmt, ...);
/// Logs a call that is not (fully) supported. Each distinct message is only
/// reported once.
void LogUnsupported(const char *what);

#define WEBD3D8_UNSUPPORTED(what) ::webd3d8::LogUnsupported(what)

//------------------------------------------------------------------------------
// Small helpers
//------------------------------------------------------------------------------
template <class T> inline T Min(T a, T b) { return a < b ? a : b; }
template <class T> inline T Max(T a, T b) { return a > b ? a : b; }
template <class T> inline T Clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline float DwordToFloat(DWORD v) { float f; memcpy(&f, &v, sizeof f); return f; }
inline DWORD FloatToDword(float f) { DWORD v; memcpy(&v, &f, sizeof v); return v; }

inline uint32_t NextPow2(uint32_t v) { uint32_t p = 1; while (p < v) p <<= 1; return p; }

/// FNV-1a hash of a byte range.
inline uint64_t HashBytes(const void *data, size_t size, uint64_t h = 1469598103934665603ull)
{
    const uint8_t *p = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < size; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

/// Row-major 4x4 matrix helpers working on D3DMATRIX (row-vector convention).
struct Mat4
{
    float m[16]; // row-major, m[row*4+col]
    static Mat4 Identity();
    static Mat4 FromD3D(const D3DMATRIX &d) { Mat4 r; memcpy(r.m, &d, sizeof r.m); return r; }
    Mat4 operator*(const Mat4 &o) const; // this applied first, then o (D3D order)
    Mat4 Transposed() const;
    bool Inverse(Mat4 &out) const;
};
void NormalMatrix3(const Mat4 &modelView, float out[9]);

//------------------------------------------------------------------------------
// COM plumbing
//------------------------------------------------------------------------------
class Device;

/// IUnknown implementation shared by all objects (reference counting only;
/// QueryInterface hands out the object for any interface, as the game never
/// asks for anything but the interface it already has).
template <class Iface>
class Unknown : public Iface
{
public:
    virtual ~Unknown() = default;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **ppv) override
    {
        if (!ppv) return E_POINTER;
        *ppv = static_cast<Iface *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refs; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG r = --m_refs;
        if (r == 0) OnZeroRefs();
        return r;
    }

protected:
    virtual void OnZeroRefs() { delete this; }
    ULONG m_refs = 1;
};

/// Intrusive smart pointer.
template <class T>
class Ref
{
public:
    Ref() = default;
    Ref(T *p) : m_p(p) { if (m_p) m_p->AddRef(); }
    Ref(const Ref &o) : m_p(o.m_p) { if (m_p) m_p->AddRef(); }
    Ref(Ref &&o) noexcept : m_p(o.m_p) { o.m_p = nullptr; }
    ~Ref() { if (m_p) m_p->Release(); }
    Ref &operator=(const Ref &o) { Ref t(o); std::swap(m_p, t.m_p); return *this; }
    Ref &operator=(T *p) { Ref t(p); std::swap(m_p, t.m_p); return *this; }
    T *operator->() const { return m_p; }
    T *get() const { return m_p; }
    explicit operator bool() const { return m_p != nullptr; }
private:
    T *m_p = nullptr;
};

} // namespace webd3d8
