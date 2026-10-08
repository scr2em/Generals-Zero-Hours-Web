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
** WebAssembly port: IDirect3D8, adapter/mode enumeration and device caps.
**
** The caps describe a typical DX8 era card with hardware T&L: the point is to
** keep WW3D2 on its well-trodden fixed-function paths. They are static (the
** application queries them before a GL context exists) and refined with the
** real limits once a device has been created.
*/
#include "d3d8_internal.h"

#include <emscripten.h>

namespace webd3d8 {

static const GUID kAdapterGuid = {0x7e0bd8a5, 0x2a1c, 0x4e0f, {0x9a, 0x35, 0x77, 0x65, 0x62, 0x67, 0x6c, 0x32}};

struct DesktopSize { unsigned w, h; };

static DesktopSize QueryDesktopSize()
{
    const Config &cfg = GetConfig();
    if (cfg.desktopWidth && cfg.desktopHeight) return {cfg.desktopWidth, cfg.desktopHeight};
    int w = MAIN_THREAD_EM_ASM_INT({ return (typeof screen !== 'undefined') ? screen.width : 0; });
    int h = MAIN_THREAD_EM_ASM_INT({ return (typeof screen !== 'undefined') ? screen.height : 0; });
    if (w < 640 || h < 480) { w = 1280; h = 720; }
    return {(unsigned)w, (unsigned)h};
}

static std::string &RendererString()
{
    static std::string s = "WebGL2";
    return s;
}

void SetRendererString(const std::string &s) { RendererString() = s; }

void FillDeviceCaps(D3DCAPS8 *c, const GLCaps *gl)
{
    memset(c, 0, sizeof *c);
    c->DeviceType = D3DDEVTYPE_HAL;
    c->AdapterOrdinal = 0;
    c->Caps = 0;
    c->Caps2 = D3DCAPS2_FULLSCREENGAMMA | D3DCAPS2_CANRENDERWINDOWED | D3DCAPS2_DYNAMICTEXTURES | D3DCAPS2_CANMANAGERESOURCE;
    c->Caps3 = D3DCAPS3_ALPHA_FULLSCREEN_FLIP_OR_DISCARD;
    c->PresentationIntervals = D3DPRESENT_INTERVAL_DEFAULT | D3DPRESENT_INTERVAL_ONE | D3DPRESENT_INTERVAL_IMMEDIATE;
    c->CursorCaps = D3DCURSORCAPS_COLOR | D3DCURSORCAPS_LOWRES;
    c->DevCaps = D3DDEVCAPS_EXECUTESYSTEMMEMORY | D3DDEVCAPS_EXECUTEVIDEOMEMORY | D3DDEVCAPS_TLVERTEXSYSTEMMEMORY |
                 D3DDEVCAPS_TLVERTEXVIDEOMEMORY | D3DDEVCAPS_TEXTURESYSTEMMEMORY | D3DDEVCAPS_TEXTUREVIDEOMEMORY |
                 D3DDEVCAPS_DRAWPRIMTLVERTEX | D3DDEVCAPS_CANRENDERAFTERFLIP | D3DDEVCAPS_DRAWPRIMITIVES2 |
                 D3DDEVCAPS_DRAWPRIMITIVES2EX | D3DDEVCAPS_HWTRANSFORMANDLIGHT | D3DDEVCAPS_HWRASTERIZATION;
    c->PrimitiveMiscCaps = D3DPMISCCAPS_MASKZ | D3DPMISCCAPS_CULLNONE | D3DPMISCCAPS_CULLCW | D3DPMISCCAPS_CULLCCW |
                           D3DPMISCCAPS_COLORWRITEENABLE | D3DPMISCCAPS_CLIPPLANESCALEDPOINTS | D3DPMISCCAPS_CLIPTLVERTS |
                           D3DPMISCCAPS_TSSARGTEMP | D3DPMISCCAPS_BLENDOP;
    // No D3DPRASTERCAPS_ZBIAS on purpose: WW3D2 then biases depth through the
    // projection matrix, which behaves identically everywhere.
    c->RasterCaps = D3DPRASTERCAPS_DITHER | D3DPRASTERCAPS_ZTEST | D3DPRASTERCAPS_FOGVERTEX | D3DPRASTERCAPS_FOGTABLE |
                    D3DPRASTERCAPS_MIPMAPLODBIAS | D3DPRASTERCAPS_FOGRANGE | D3DPRASTERCAPS_ANISOTROPY |
                    D3DPRASTERCAPS_ZFOG | D3DPRASTERCAPS_COLORPERSPECTIVE;
    const DWORD cmp = D3DPCMPCAPS_NEVER | D3DPCMPCAPS_LESS | D3DPCMPCAPS_EQUAL | D3DPCMPCAPS_LESSEQUAL |
                      D3DPCMPCAPS_GREATER | D3DPCMPCAPS_NOTEQUAL | D3DPCMPCAPS_GREATEREQUAL | D3DPCMPCAPS_ALWAYS;
    c->ZCmpCaps = cmp;
    c->AlphaCmpCaps = cmp;
    const DWORD blend = D3DPBLENDCAPS_ZERO | D3DPBLENDCAPS_ONE | D3DPBLENDCAPS_SRCCOLOR | D3DPBLENDCAPS_INVSRCCOLOR |
                        D3DPBLENDCAPS_SRCALPHA | D3DPBLENDCAPS_INVSRCALPHA | D3DPBLENDCAPS_DESTALPHA |
                        D3DPBLENDCAPS_INVDESTALPHA | D3DPBLENDCAPS_DESTCOLOR | D3DPBLENDCAPS_INVDESTCOLOR |
                        D3DPBLENDCAPS_SRCALPHASAT | D3DPBLENDCAPS_BOTHSRCALPHA | D3DPBLENDCAPS_BOTHINVSRCALPHA;
    c->SrcBlendCaps = blend;
    c->DestBlendCaps = blend;
    c->ShadeCaps = D3DPSHADECAPS_COLORGOURAUDRGB | D3DPSHADECAPS_SPECULARGOURAUDRGB | D3DPSHADECAPS_ALPHAGOURAUDBLEND |
                   D3DPSHADECAPS_FOGGOURAUD;
    c->TextureCaps = D3DPTEXTURECAPS_PERSPECTIVE | D3DPTEXTURECAPS_ALPHA | D3DPTEXTURECAPS_PROJECTED |
                     D3DPTEXTURECAPS_CUBEMAP | D3DPTEXTURECAPS_VOLUMEMAP | D3DPTEXTURECAPS_MIPMAP |
                     D3DPTEXTURECAPS_MIPVOLUMEMAP | D3DPTEXTURECAPS_MIPCUBEMAP;
    const DWORD filt = D3DPTFILTERCAPS_MINFPOINT | D3DPTFILTERCAPS_MINFLINEAR | D3DPTFILTERCAPS_MINFANISOTROPIC |
                       D3DPTFILTERCAPS_MIPFPOINT | D3DPTFILTERCAPS_MIPFLINEAR | D3DPTFILTERCAPS_MAGFPOINT |
                       D3DPTFILTERCAPS_MAGFLINEAR | D3DPTFILTERCAPS_MAGFANISOTROPIC;
    c->TextureFilterCaps = filt;
    c->CubeTextureFilterCaps = filt;
    c->VolumeTextureFilterCaps = filt;
    const DWORD addr = D3DPTADDRESSCAPS_WRAP | D3DPTADDRESSCAPS_MIRROR | D3DPTADDRESSCAPS_CLAMP | D3DPTADDRESSCAPS_BORDER |
                       D3DPTADDRESSCAPS_INDEPENDENTUV | D3DPTADDRESSCAPS_MIRRORONCE;
    c->TextureAddressCaps = addr;
    c->VolumeTextureAddressCaps = addr;
    c->LineCaps = D3DLINECAPS_TEXTURE | D3DLINECAPS_ZTEST | D3DLINECAPS_BLEND | D3DLINECAPS_ALPHACMP | D3DLINECAPS_FOG;
    GLint maxTex = gl ? Min<GLint>(gl->maxTextureSize, 4096) : 4096;
    c->MaxTextureWidth = c->MaxTextureHeight = maxTex;
    c->MaxVolumeExtent = gl ? Min<GLint>(gl->max3DSize, 256) : 256;
    c->MaxTextureRepeat = 8192;
    c->MaxTextureAspectRatio = 0;
    c->MaxAnisotropy = gl ? (DWORD)Max(1.0f, gl->maxAnisotropy) : 16;
    c->MaxVertexW = 1.0e10f;
    c->GuardBandLeft = c->GuardBandTop = -8192.0f;
    c->GuardBandRight = c->GuardBandBottom = 8192.0f;
    c->ExtentsAdjust = 0.0f;
    c->StencilCaps = D3DSTENCILCAPS_KEEP | D3DSTENCILCAPS_ZERO | D3DSTENCILCAPS_REPLACE | D3DSTENCILCAPS_INCRSAT |
                     D3DSTENCILCAPS_DECRSAT | D3DSTENCILCAPS_INVERT | D3DSTENCILCAPS_INCR | D3DSTENCILCAPS_DECR;
    c->FVFCaps = 8 | D3DFVFCAPS_PSIZE;
    c->TextureOpCaps = D3DTEXOPCAPS_DISABLE | D3DTEXOPCAPS_SELECTARG1 | D3DTEXOPCAPS_SELECTARG2 | D3DTEXOPCAPS_MODULATE |
                       D3DTEXOPCAPS_MODULATE2X | D3DTEXOPCAPS_MODULATE4X | D3DTEXOPCAPS_ADD | D3DTEXOPCAPS_ADDSIGNED |
                       D3DTEXOPCAPS_ADDSIGNED2X | D3DTEXOPCAPS_SUBTRACT | D3DTEXOPCAPS_ADDSMOOTH |
                       D3DTEXOPCAPS_BLENDDIFFUSEALPHA | D3DTEXOPCAPS_BLENDTEXTUREALPHA | D3DTEXOPCAPS_BLENDFACTORALPHA |
                       D3DTEXOPCAPS_BLENDTEXTUREALPHAPM | D3DTEXOPCAPS_BLENDCURRENTALPHA | D3DTEXOPCAPS_PREMODULATE |
                       D3DTEXOPCAPS_MODULATEALPHA_ADDCOLOR | D3DTEXOPCAPS_MODULATECOLOR_ADDALPHA |
                       D3DTEXOPCAPS_MODULATEINVALPHA_ADDCOLOR | D3DTEXOPCAPS_MODULATEINVCOLOR_ADDALPHA |
                       D3DTEXOPCAPS_DOTPRODUCT3 | D3DTEXOPCAPS_MULTIPLYADD | D3DTEXOPCAPS_LERP;
    c->MaxTextureBlendStages = MAX_STAGES;
    c->MaxSimultaneousTextures = 4;
    c->VertexProcessingCaps = D3DVTXPCAPS_TEXGEN | D3DVTXPCAPS_MATERIALSOURCE7 | D3DVTXPCAPS_DIRECTIONALLIGHTS |
                              D3DVTXPCAPS_POSITIONALLIGHTS | D3DVTXPCAPS_LOCALVIEWER | D3DVTXPCAPS_NO_VSDT_UBYTE4 * 0;
    c->MaxActiveLights = MAX_SHADER_LIGHTS;
    c->MaxUserClipPlanes = MAX_CLIP_PLANES;
    c->MaxVertexBlendMatrices = 0;
    c->MaxVertexBlendMatrixIndex = 0;
    c->MaxPointSize = gl ? Min(gl->maxPointSize, 64.0f) : 64.0f;
    c->MaxPrimitiveCount = 0x000FFFFF;
    c->MaxVertexIndex = 0x000FFFFF;
    c->MaxStreams = MAX_STREAMS;
    c->MaxStreamStride = 255;
    const Config &cfg = GetConfig();
    c->VertexShaderVersion = cfg.vsVersion ? D3DVS_VERSION(cfg.vsVersion >> 8, cfg.vsVersion & 0xFF) : 0;
    c->PixelShaderVersion = cfg.psVersion ? D3DPS_VERSION(cfg.psVersion >> 8, cfg.psVersion & 0xFF) : 0;
    c->MaxVertexShaderConst = cfg.vsVersion ? VS_CONSTANTS : 0;
    c->MaxPixelShaderValue = cfg.psVersion ? (cfg.psVersion >= 0x0104 ? 8.0f : 1.0f) : 0.0f;
}

namespace {

const D3DFORMAT kDisplayFormats[] = {D3DFMT_X8R8G8B8, D3DFMT_R5G6B5};

class Direct3D8 final : public Unknown<IDirect3D8>
{
public:
    HRESULT STDMETHODCALLTYPE RegisterSoftwareDevice(void *) override { return D3DERR_NOTAVAILABLE; }
    UINT STDMETHODCALLTYPE GetAdapterCount() override { return 1; }

    HRESULT STDMETHODCALLTYPE GetAdapterIdentifier(UINT adapter, DWORD, D3DADAPTER_IDENTIFIER8 *id) override
    {
        if (adapter != 0 || !id) return D3DERR_INVALIDCALL;
        memset(id, 0, sizeof *id);
        strncpy(id->Driver, "webgl2.dll", sizeof id->Driver - 1);
        strncpy(id->Description, RendererString().c_str(), sizeof id->Description - 1);
        // DriverVersion is a LARGE_INTEGER with _WIN32 and two DWORDs otherwise
        // (same layout: low part first); it directly precedes VendorId.
        DWORD *driverVersion = &id->VendorId - 2;
        driverVersion[0] = (10 << 16) | 4000; // LowPart
        driverVersion[1] = (6 << 16) | 14;    // HighPart
        id->VendorId = 0;
        id->DeviceId = 0;
        id->SubSysId = 0;
        id->Revision = 0;
        id->DeviceIdentifier = kAdapterGuid;
        id->WHQLLevel = 0;
        return D3D_OK;
    }

    static const std::vector<std::pair<unsigned, unsigned>> &Resolutions()
    {
        static std::vector<std::pair<unsigned, unsigned>> list;
        if (list.empty())
        {
            static const unsigned std[][2] = {{640, 480}, {800, 600}, {1024, 768}, {1152, 864}, {1280, 720}, {1280, 800},
                                              {1280, 960}, {1280, 1024}, {1360, 768}, {1366, 768}, {1440, 900},
                                              {1600, 900}, {1600, 1200}, {1680, 1050}, {1920, 1080}, {1920, 1200},
                                              {2560, 1440}};
            DesktopSize d = QueryDesktopSize();
            for (auto &r : std)
                if (r[0] <= d.w && r[1] <= d.h) list.push_back({r[0], r[1]});
            if (std::find(list.begin(), list.end(), std::make_pair(d.w, d.h)) == list.end()) list.push_back({d.w, d.h});
            std::sort(list.begin(), list.end());
        }
        return list;
    }

    UINT STDMETHODCALLTYPE GetAdapterModeCount(UINT adapter) override
    {
        if (adapter != 0) return 0;
        return (UINT)(Resolutions().size() * 2);
    }

    HRESULT STDMETHODCALLTYPE EnumAdapterModes(UINT adapter, UINT mode, D3DDISPLAYMODE *m) override
    {
        if (adapter != 0 || !m || mode >= Resolutions().size() * 2) return D3DERR_INVALIDCALL;
        const auto &r = Resolutions()[mode / 2];
        m->Width = r.first;
        m->Height = r.second;
        m->RefreshRate = 60;
        m->Format = kDisplayFormats[mode % 2];
        return D3D_OK;
    }

    HRESULT STDMETHODCALLTYPE GetAdapterDisplayMode(UINT adapter, D3DDISPLAYMODE *m) override
    {
        if (adapter != 0 || !m) return D3DERR_INVALIDCALL;
        DesktopSize d = QueryDesktopSize();
        m->Width = d.w;
        m->Height = d.h;
        m->RefreshRate = 60;
        m->Format = D3DFMT_X8R8G8B8;
        return D3D_OK;
    }

    static bool IsBackBufferFormat(D3DFORMAT f)
    {
        return f == D3DFMT_A8R8G8B8 || f == D3DFMT_X8R8G8B8 || f == D3DFMT_R5G6B5 || f == D3DFMT_A1R5G5B5 || f == D3DFMT_X1R5G5B5;
    }

    HRESULT STDMETHODCALLTYPE CheckDeviceType(UINT adapter, D3DDEVTYPE type, D3DFORMAT display, D3DFORMAT backBuffer, BOOL) override
    {
        if (adapter != 0) return D3DERR_INVALIDCALL;
        if (type != D3DDEVTYPE_HAL) return D3DERR_NOTAVAILABLE;
        if (!IsBackBufferFormat(display) || !IsBackBufferFormat(backBuffer)) return D3DERR_NOTAVAILABLE;
        return D3D_OK;
    }

    HRESULT STDMETHODCALLTYPE CheckDeviceFormat(UINT adapter, D3DDEVTYPE type, D3DFORMAT, DWORD usage,
                                                D3DRESOURCETYPE rtype, D3DFORMAT format) override
    {
        if (adapter != 0) return D3DERR_INVALIDCALL;
        if (type != D3DDEVTYPE_HAL) return D3DERR_NOTAVAILABLE;
        const FormatInfo *info = GetFormatInfo(format);
        if (!info) return D3DERR_NOTAVAILABLE;
        if (usage & D3DUSAGE_DEPTHSTENCIL)
            return (rtype == D3DRTYPE_SURFACE && info->depth) ? D3D_OK : D3DERR_NOTAVAILABLE;
        if (info->depth) return D3DERR_NOTAVAILABLE;
        if (usage & D3DUSAGE_RENDERTARGET)
        {
            if (!info->renderTarget) return D3DERR_NOTAVAILABLE;
            if (rtype != D3DRTYPE_TEXTURE && rtype != D3DRTYPE_SURFACE && rtype != D3DRTYPE_CUBETEXTURE) return D3DERR_NOTAVAILABLE;
            return D3D_OK;
        }
        switch (rtype)
        {
        case D3DRTYPE_SURFACE: return D3D_OK;
        case D3DRTYPE_TEXTURE:
        case D3DRTYPE_CUBETEXTURE:
        case D3DRTYPE_VOLUMETEXTURE:
            return info->texture ? D3D_OK : D3DERR_NOTAVAILABLE;
        default: return D3DERR_NOTAVAILABLE;
        }
    }

    HRESULT STDMETHODCALLTYPE CheckDeviceMultiSampleType(UINT adapter, D3DDEVTYPE, D3DFORMAT, BOOL, D3DMULTISAMPLE_TYPE type) override
    {
        if (adapter != 0) return D3DERR_INVALIDCALL;
        return type == D3DMULTISAMPLE_NONE ? D3D_OK : D3DERR_NOTAVAILABLE;
    }

    HRESULT STDMETHODCALLTYPE CheckDepthStencilMatch(UINT adapter, D3DDEVTYPE, D3DFORMAT, D3DFORMAT, D3DFORMAT depth) override
    {
        if (adapter != 0) return D3DERR_INVALIDCALL;
        const FormatInfo *info = GetFormatInfo(depth);
        return (info && info->depth) ? D3D_OK : D3DERR_NOTAVAILABLE;
    }

    HRESULT STDMETHODCALLTYPE GetDeviceCaps(UINT adapter, D3DDEVTYPE type, D3DCAPS8 *caps) override
    {
        if (adapter != 0 || !caps) return D3DERR_INVALIDCALL;
        if (type != D3DDEVTYPE_HAL) return D3DERR_NOTAVAILABLE;
        FillDeviceCaps(caps, nullptr);
        return D3D_OK;
    }

    HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT) override { return reinterpret_cast<HMONITOR>(1); }

    HRESULT STDMETHODCALLTYPE CreateDevice(UINT adapter, D3DDEVTYPE type, HWND focus, DWORD behavior,
                                           D3DPRESENT_PARAMETERS *pp, IDirect3DDevice8 **ppDevice) override
    {
        if (!ppDevice || !pp) return D3DERR_INVALIDCALL;
        *ppDevice = nullptr;
        if (adapter != 0) return D3DERR_INVALIDCALL;
        if (type != D3DDEVTYPE_HAL) return D3DERR_NOTAVAILABLE;
        Device *dev = Device::Create(this, adapter, type, focus, behavior, pp);
        if (!dev) return D3DERR_NOTAVAILABLE;
        *ppDevice = dev;
        return D3D_OK;
    }
};

} // namespace

IDirect3D8 *CreateDirect3D8() { return new Direct3D8(); }

} // namespace webd3d8

extern "C" IDirect3D8 *WINAPI Direct3DCreate8(UINT sdkVersion)
{
    (void)sdkVersion; // the headers are the ones this library implements
    return webd3d8::CreateDirect3D8();
}

extern "C" const char *WebD3D8_GetRendererString(void)
{
    return webd3d8::RendererString().c_str();
}
