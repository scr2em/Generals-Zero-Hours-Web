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
** WebAssembly port: miscellaneous D3DX8 functions (error strings, ID3DXBuffer,
** FVF helpers, shader assembly front end).
*/
#include "shader.h"

using namespace webd3d8;

namespace {

class D3DXBuffer final : public Unknown<ID3DXBuffer>
{
public:
    explicit D3DXBuffer(const void *data, size_t size) : m_data((const uint8_t *)data, (const uint8_t *)data + size) {}
    LPVOID STDMETHODCALLTYPE GetBufferPointer() override { return m_data.data(); }
    DWORD STDMETHODCALLTYPE GetBufferSize() override { return (DWORD)m_data.size(); }

private:
    std::vector<uint8_t> m_data;
};

const char *ErrorText(HRESULT hr)
{
    switch ((unsigned)hr)
    {
    case 0: return "No error occurred.";
    case (unsigned)D3DERR_WRONGTEXTUREFORMAT: return "Wrong texture format.";
    case (unsigned)D3DERR_UNSUPPORTEDCOLOROPERATION: return "Unsupported color operation.";
    case (unsigned)D3DERR_UNSUPPORTEDCOLORARG: return "Unsupported color argument.";
    case (unsigned)D3DERR_UNSUPPORTEDALPHAOPERATION: return "Unsupported alpha operation.";
    case (unsigned)D3DERR_UNSUPPORTEDALPHAARG: return "Unsupported alpha argument.";
    case (unsigned)D3DERR_TOOMANYOPERATIONS: return "Too many operations.";
    case (unsigned)D3DERR_CONFLICTINGTEXTUREFILTER: return "Conflicting texture filter.";
    case (unsigned)D3DERR_UNSUPPORTEDFACTORVALUE: return "Unsupported factor value.";
    case (unsigned)D3DERR_CONFLICTINGRENDERSTATE: return "Conflicting render state.";
    case (unsigned)D3DERR_UNSUPPORTEDTEXTUREFILTER: return "Unsupported texture filter.";
    case (unsigned)D3DERR_CONFLICTINGTEXTUREPALETTE: return "Conflicting texture palette.";
    case (unsigned)D3DERR_DRIVERINTERNALERROR: return "Driver internal error.";
    case (unsigned)D3DERR_NOTFOUND: return "Not found.";
    case (unsigned)D3DERR_MOREDATA: return "More data.";
    case (unsigned)D3DERR_DEVICELOST: return "Device lost.";
    case (unsigned)D3DERR_DEVICENOTRESET: return "Device not reset.";
    case (unsigned)D3DERR_NOTAVAILABLE: return "Not available.";
    case (unsigned)D3DERR_OUTOFVIDEOMEMORY: return "Out of video memory.";
    case (unsigned)D3DERR_INVALIDDEVICE: return "Invalid device.";
    case (unsigned)D3DERR_INVALIDCALL: return "Invalid call.";
    case (unsigned)E_OUTOFMEMORY: return "Out of memory.";
    case (unsigned)E_FAIL: return "Unspecified error.";
    case (unsigned)E_NOTIMPL: return "Not implemented.";
    case 0x88760B54u: return "Invalid data.";
    default: return "Unknown error.";
    }
}

} // namespace

extern "C" {

HRESULT WINAPI D3DXGetErrorStringA(HRESULT hr, LPSTR buf, UINT len)
{
    if (!buf || !len) return D3DERR_INVALIDCALL;
    strncpy(buf, ErrorText(hr), len - 1);
    buf[len - 1] = 0;
    return D3D_OK;
}

HRESULT WINAPI D3DXGetErrorStringW(HRESULT hr, LPWSTR buf, UINT len)
{
    if (!buf || !len) return D3DERR_INVALIDCALL;
    const char *s = ErrorText(hr);
    UINT i = 0;
    for (; s[i] && i + 1 < len; ++i) buf[i] = (WCHAR)s[i];
    buf[i] = 0;
    return D3D_OK;
}

UINT WINAPI D3DXGetFVFVertexSize(DWORD fvf) { return FVFVertexSize(fvf); }

HRESULT WINAPI D3DXAssembleShader(LPCVOID src, UINT len, DWORD, LPD3DXBUFFER *ppConstants, LPD3DXBUFFER *ppCompiled, LPD3DXBUFFER *ppErrors)
{
    if (ppConstants) *ppConstants = nullptr;
    if (ppCompiled) *ppCompiled = nullptr;
    if (ppErrors) *ppErrors = nullptr;
    if (!src || !ppCompiled) return D3DERR_INVALIDCALL;
    std::vector<DWORD> code;
    std::string errors;
    if (!AssembleShader(static_cast<const char *>(src), len, code, errors))
    {
        if (ppErrors) *ppErrors = new D3DXBuffer(errors.c_str(), errors.size() + 1);
        return D3DERR_INVALIDCALL;
    }
    *ppCompiled = new D3DXBuffer(code.data(), code.size() * sizeof(DWORD));
    return D3D_OK;
}

HRESULT WINAPI D3DXAssembleShaderFromFileA(LPCSTR file, DWORD flags, LPD3DXBUFFER *c, LPD3DXBUFFER *out, LPD3DXBUFFER *err)
{
    FILE *f = file ? fopen(file, "rb") : nullptr;
    if (!f) return D3DERR_INVALIDCALL;
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
    fclose(f);
    return D3DXAssembleShader(text.data(), (UINT)text.size(), flags, c, out, err);
}

} // extern "C"
