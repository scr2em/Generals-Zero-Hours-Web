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
** WebAssembly port: D3DX8 texture and surface functions used by WW3D2 and
** W3DDevice (texture creation, mip filtering, surface conversion/scaling and
** loading of DDS/TGA/BMP files).
*/
#include "resources.h"

#include <cstdio>

using namespace webd3d8;

#define D3DXERR_INVALIDDATA_ ((HRESULT)0x88760B54)

namespace {

constexpr UINT kDefault = 0xFFFFFFFFu; // D3DX_DEFAULT

Surface *AsSurface(IDirect3DSurface8 *s) { return static_cast<Surface *>(s); }

TextureBase *AsBase(IDirect3DBaseTexture8 *t, D3DRESOURCETYPE *type = nullptr)
{
    if (!t) return nullptr;
    D3DRESOURCETYPE ty = t->GetType();
    if (type) *type = ty;
    switch (ty)
    {
    case D3DRTYPE_TEXTURE: return static_cast<Texture2D *>(static_cast<IDirect3DTexture8 *>(t));
    case D3DRTYPE_CUBETEXTURE: return static_cast<CubeTexture *>(static_cast<IDirect3DCubeTexture8 *>(t));
    case D3DRTYPE_VOLUMETEXTURE: return static_cast<VolumeTexture *>(static_cast<IDirect3DVolumeTexture8 *>(t));
    default: return nullptr;
    }
}

bool FormatUsable(IDirect3DDevice8 *dev, D3DFORMAT f, DWORD usage)
{
    const FormatInfo *i = GetFormatInfo(f);
    if (!i || !i->texture) return false;
    if ((usage & D3DUSAGE_RENDERTARGET) && !i->renderTarget) return false;
    (void)dev;
    return true;
}

//------------------------------------------------------------------------------
// Scaling in A8R8G8B8 space
//------------------------------------------------------------------------------
struct Image
{
    uint32_t w = 0, h = 0;
    std::vector<uint8_t> px; // B,G,R,A, tightly packed
};

/// Resamples an ARGB image. Down-scaling averages the covered source area
/// (box/triangle), up-scaling is bilinear for the smoothing filters and
/// nearest for NONE/POINT.
Image Resample(const Image &src, uint32_t dw, uint32_t dh, DWORD filter)
{
    Image dst;
    dst.w = dw; dst.h = dh;
    dst.px.resize((size_t)dw * dh * 4);
    const DWORD f = filter & 0xFF;
    const bool smooth = f != D3DX_FILTER_NONE && f != D3DX_FILTER_POINT;
    if (dw == src.w && dh == src.h) { dst.px = src.px; return dst; }

    // Horizontal pass into floats, then vertical.
    std::vector<float> tmp((size_t)dw * src.h * 4);
    auto axis = [&](uint32_t srcN, uint32_t dstN, std::vector<std::vector<std::pair<uint32_t, float>>> &weights) {
        weights.resize(dstN);
        for (uint32_t i = 0; i < dstN; ++i)
        {
            auto &w = weights[i];
            if (!smooth)
            {
                uint32_t s = Min<uint32_t>((uint32_t)(((uint64_t)i * 2 + 1) * srcN / (2 * dstN)), srcN - 1);
                w.push_back({s, 1.0f});
            }
            else if (dstN < srcN)
            {
                // Area average.
                double a = (double)i * srcN / dstN, b = (double)(i + 1) * srcN / dstN;
                for (uint32_t s = (uint32_t)a; s < srcN && s < b; ++s)
                {
                    double lo = Max<double>(a, s), hi = Min<double>(b, s + 1);
                    if (hi > lo) w.push_back({s, (float)((hi - lo) / (b - a))});
                }
            }
            else
            {
                double c = ((double)i + 0.5) * srcN / dstN - 0.5;
                int s0 = (int)std::floor(c);
                float t = (float)(c - s0);
                int s1 = s0 + 1;
                s0 = Clamp(s0, 0, (int)srcN - 1);
                s1 = Clamp(s1, 0, (int)srcN - 1);
                w.push_back({(uint32_t)s0, 1.0f - t});
                w.push_back({(uint32_t)s1, t});
            }
        }
    };
    std::vector<std::vector<std::pair<uint32_t, float>>> wx, wy;
    axis(src.w, dw, wx);
    axis(src.h, dh, wy);
    for (uint32_t y = 0; y < src.h; ++y)
        for (uint32_t x = 0; x < dw; ++x)
        {
            float acc[4] = {0, 0, 0, 0};
            for (auto &p : wx[x])
                for (int c = 0; c < 4; ++c) acc[c] += src.px[((size_t)y * src.w + p.first) * 4 + c] * p.second;
            for (int c = 0; c < 4; ++c) tmp[((size_t)y * dw + x) * 4 + c] = acc[c];
        }
    for (uint32_t y = 0; y < dh; ++y)
        for (uint32_t x = 0; x < dw; ++x)
        {
            float acc[4] = {0, 0, 0, 0};
            for (auto &p : wy[y])
                for (int c = 0; c < 4; ++c) acc[c] += tmp[((size_t)p.first * dw + x) * 4 + c] * p.second;
            for (int c = 0; c < 4; ++c) dst.px[((size_t)y * dw + x) * 4 + c] = (uint8_t)Clamp(acc[c] + 0.5f, 0.0f, 255.0f);
        }
    return dst;
}

bool ToImage(D3DFORMAT fmt, const void *data, uint32_t pitch, uint32_t w, uint32_t h, Image &out)
{
    out.w = w; out.h = h;
    out.px.resize((size_t)w * h * 4);
    return ConvertPixels(fmt, data, pitch, D3DFMT_A8R8G8B8, out.px.data(), w * 4, w, h);
}

void ApplyColorKey(Image &img, D3DCOLOR key)
{
    if (!key) return;
    for (size_t i = 0; i < (size_t)img.w * img.h; ++i)
    {
        uint32_t c;
        memcpy(&c, &img.px[i * 4], 4);
        if ((c & 0x00FFFFFF) == (key & 0x00FFFFFF)) { c = 0; memcpy(&img.px[i * 4], &c, 4); }
    }
}

HRESULT LoadIntoSurface(IDirect3DSurface8 *pDst, const RECT *pDstRect, const void *src, D3DFORMAT srcFmt, UINT srcPitch,
                        uint32_t srcW, uint32_t srcH, const RECT *pSrcRect, DWORD filter, D3DCOLOR colorKey)
{
    if (!pDst || !src) return D3DERR_INVALIDCALL;
    Surface *dst = AsSurface(pDst);
    RECT sr = pSrcRect ? *pSrcRect : RECT{0, 0, (LONG)srcW, (LONG)srcH};
    RECT dr = pDstRect ? *pDstRect : RECT{0, 0, (LONG)dst->Width(), (LONG)dst->Height()};
    if (sr.left < 0 || sr.top < 0 || sr.right > (LONG)srcW || sr.bottom > (LONG)srcH || sr.right <= sr.left || sr.bottom <= sr.top)
        return D3DERR_INVALIDCALL;
    if (dr.left < 0 || dr.top < 0 || dr.right > (LONG)dst->Width() || dr.bottom > (LONG)dst->Height() || dr.right <= dr.left || dr.bottom <= dr.top)
        return D3DERR_INVALIDCALL;
    const uint32_t sw = sr.right - sr.left, sh = sr.bottom - sr.top, dw = dr.right - dr.left, dh = dr.bottom - dr.top;
    if (filter == kDefault || (filter & 0xFF) == 0) filter = D3DX_FILTER_TRIANGLE | D3DX_FILTER_DITHER;

    const FormatInfo *si = GetFormatInfo(srcFmt);
    if (!si) return D3DERR_INVALIDCALL;
    const uint8_t *base = static_cast<const uint8_t *>(src);
    const uint8_t *p;
    if (si->blockBytes)
    {
        sr.left &= ~3; sr.top &= ~3;
        p = base + (size_t)(sr.top / 4) * srcPitch + (size_t)(sr.left / 4) * si->blockBytes;
    }
    else
        p = base + (size_t)sr.top * srcPitch + (size_t)sr.left * (si->bits / 8);

    if (sw == dw && sh == dh && !colorKey)
    {
        uint32_t pitch = FormatPitch(dst->Format(), dw);
        std::vector<uint8_t> tmp((size_t)pitch * FormatRows(dst->Format(), dh));
        if (!ConvertPixels(srcFmt, p, srcPitch, dst->Format(), tmp.data(), pitch, dw, dh)) return D3DERR_INVALIDCALL;
        return dst->WritePixelsFrom(dr, dst->Format(), tmp.data(), pitch) ? D3D_OK : D3DERR_INVALIDCALL;
    }
    Image img;
    if (!ToImage(srcFmt, p, srcPitch, sw, sh, img)) return D3DERR_INVALIDCALL;
    ApplyColorKey(img, colorKey);
    Image out = Resample(img, dw, dh, filter);
    return dst->WritePixelsFrom(dr, D3DFMT_A8R8G8B8, out.px.data(), dw * 4) ? D3D_OK : D3DERR_INVALIDCALL;
}

//------------------------------------------------------------------------------
// Image files
//------------------------------------------------------------------------------
struct FileImage
{
    D3DXIMAGE_FILEFORMAT fileFormat = D3DXIFF_BMP;
    D3DRESOURCETYPE type = D3DRTYPE_TEXTURE;
    D3DFORMAT format = D3DFMT_A8R8G8B8;
    uint32_t w = 0, h = 0, depth = 1, levels = 1;
    struct Level { uint32_t w, h, pitch; std::vector<uint8_t> data; };
    std::vector<Level> mips;           // for the first face / slice
    std::vector<std::vector<Level>> faces; // cube maps: 6 face chains (mips = faces[0])
};

bool ParseDDS(const uint8_t *d, size_t n, FileImage &img)
{
    if (n < 128 || memcmp(d, "DDS ", 4) != 0) return false;
    auto rd = [&](size_t o) { uint32_t v; memcpy(&v, d + o, 4); return v; };
    const uint32_t flags = rd(8), height = rd(12), width = rd(16), mips = (flags & 0x20000) ? rd(28) : 1;
    const uint32_t pfFlags = rd(80), fourcc = rd(84), bits = rd(88), rm = rd(92), gm = rd(96), bm = rd(100), am = rd(104);
    const uint32_t caps2 = rd(112);
    D3DFORMAT fmt = D3DFMT_UNKNOWN;
    if (pfFlags & 4)
    {
        if (fourcc == MAKEFOURCC('D', 'X', 'T', '1')) fmt = D3DFMT_DXT1;
        else if (fourcc == MAKEFOURCC('D', 'X', 'T', '2')) fmt = D3DFMT_DXT2;
        else if (fourcc == MAKEFOURCC('D', 'X', 'T', '3')) fmt = D3DFMT_DXT3;
        else if (fourcc == MAKEFOURCC('D', 'X', 'T', '4')) fmt = D3DFMT_DXT4;
        else if (fourcc == MAKEFOURCC('D', 'X', 'T', '5')) fmt = D3DFMT_DXT5;
    }
    else if (pfFlags & 0x40)
    {
        if (bits == 32 && rm == 0xFF0000 && gm == 0xFF00 && bm == 0xFF) fmt = (pfFlags & 1) ? D3DFMT_A8R8G8B8 : D3DFMT_X8R8G8B8;
        else if (bits == 24 && rm == 0xFF0000) fmt = D3DFMT_R8G8B8;
        else if (bits == 16 && rm == 0xF800) fmt = D3DFMT_R5G6B5;
        else if (bits == 16 && rm == 0x7C00) fmt = (pfFlags & 1) ? D3DFMT_A1R5G5B5 : D3DFMT_X1R5G5B5;
        else if (bits == 16 && rm == 0x0F00) fmt = (pfFlags & 1) ? D3DFMT_A4R4G4B4 : D3DFMT_X4R4G4B4;
        (void)am;
    }
    else if ((pfFlags & 0x20000) && bits == 8) fmt = D3DFMT_L8;
    else if ((pfFlags & 0x20000) && bits == 16) fmt = D3DFMT_A8L8;
    else if ((pfFlags & 2) && bits == 8) fmt = D3DFMT_A8;
    if (fmt == D3DFMT_UNKNOWN) return false;

    img.fileFormat = D3DXIFF_DDS;
    img.format = fmt;
    img.w = width; img.h = height; img.levels = mips ? mips : 1;
    const bool cube = (caps2 & 0x200) != 0;
    const int faces = cube ? 6 : 1;
    img.type = cube ? D3DRTYPE_CUBETEXTURE : D3DRTYPE_TEXTURE;
    size_t off = 128;
    img.faces.resize(faces);
    for (int f = 0; f < faces; ++f)
    {
        uint32_t lw = width, lh = height;
        for (uint32_t l = 0; l < img.levels; ++l)
        {
            FileImage::Level lv;
            lv.w = lw; lv.h = lh;
            lv.pitch = FormatPitch(fmt, lw);
            size_t size = (size_t)lv.pitch * FormatRows(fmt, lh);
            if (off + size > n) return false;
            lv.data.assign(d + off, d + off + size);
            off += size;
            img.faces[f].push_back(std::move(lv));
            lw = Max<uint32_t>(1, lw >> 1);
            lh = Max<uint32_t>(1, lh >> 1);
        }
    }
    img.mips = img.faces[0];
    return true;
}

bool ParseTGA(const uint8_t *d, size_t n, FileImage &img)
{
    if (n < 18) return false;
    const uint8_t idLen = d[0], cmType = d[1], type = d[2];
    const uint16_t w = (uint16_t)(d[12] | (d[13] << 8)), h = (uint16_t)(d[14] | (d[15] << 8));
    const uint8_t bpp = d[16], desc = d[17];
    if (cmType != 0 || (type != 2 && type != 3 && type != 10 && type != 11) || !w || !h) return false;
    if (bpp != 8 && bpp != 24 && bpp != 32 && bpp != 16) return false;
    const uint32_t bytes = bpp / 8;
    size_t pos = 18 + idLen;
    std::vector<uint8_t> raw((size_t)w * h * bytes);
    if (type == 2 || type == 3)
    {
        if (pos + raw.size() > n) return false;
        memcpy(raw.data(), d + pos, raw.size());
    }
    else
    {
        size_t o = 0;
        while (o < raw.size() && pos < n)
        {
            uint8_t hdr = d[pos++];
            uint32_t count = (hdr & 0x7F) + 1;
            if (hdr & 0x80)
            {
                if (pos + bytes > n) return false;
                for (uint32_t i = 0; i < count && o < raw.size(); ++i, o += bytes) memcpy(&raw[o], d + pos, bytes);
                pos += bytes;
            }
            else
            {
                for (uint32_t i = 0; i < count && o < raw.size(); ++i, o += bytes, pos += bytes)
                {
                    if (pos + bytes > n) return false;
                    memcpy(&raw[o], d + pos, bytes);
                }
            }
        }
    }
    const bool topDown = (desc & 0x20) != 0;
    FileImage::Level lv;
    lv.w = w; lv.h = h;
    D3DFORMAT fmt = bpp == 32 ? D3DFMT_A8R8G8B8 : (bpp == 24 ? D3DFMT_X8R8G8B8 : (bpp == 16 ? D3DFMT_A1R5G5B5 : D3DFMT_L8));
    lv.pitch = w * (bpp == 24 ? 4 : bytes);
    lv.data.resize((size_t)lv.pitch * h);
    for (uint32_t y = 0; y < h; ++y)
    {
        const uint8_t *s = &raw[(size_t)(topDown ? y : h - 1 - y) * w * bytes];
        uint8_t *dd = &lv.data[(size_t)y * lv.pitch];
        for (uint32_t x = 0; x < w; ++x)
        {
            if (bpp == 24) { dd[x * 4] = s[x * 3]; dd[x * 4 + 1] = s[x * 3 + 1]; dd[x * 4 + 2] = s[x * 3 + 2]; dd[x * 4 + 3] = 255; }
            else memcpy(dd + x * bytes, s + x * bytes, bytes);
        }
    }
    img.fileFormat = D3DXIFF_TGA;
    img.format = fmt;
    img.w = w; img.h = h; img.levels = 1;
    img.mips.push_back(std::move(lv));
    img.faces.push_back(img.mips);
    return true;
}

bool ParseBMP(const uint8_t *d, size_t n, FileImage &img)
{
    if (n < 54 || d[0] != 'B' || d[1] != 'M') return false;
    auto rd32 = [&](size_t o) { uint32_t v; memcpy(&v, d + o, 4); return v; };
    uint32_t dataOff = rd32(10), hdr = rd32(14);
    if (hdr < 40) return false;
    int32_t w = (int32_t)rd32(18), h = (int32_t)rd32(22);
    uint16_t bpp = (uint16_t)(d[28] | (d[29] << 8));
    uint32_t comp = rd32(30);
    if (comp != 0 && comp != 3) return false;
    if (bpp != 24 && bpp != 32) return false;
    const bool topDown = h < 0;
    if (h < 0) h = -h;
    FileImage::Level lv;
    lv.w = w; lv.h = h; lv.pitch = w * 4;
    lv.data.resize((size_t)lv.pitch * h);
    const uint32_t srcPitch = ((w * bpp + 31) / 32) * 4;
    if (dataOff + (size_t)srcPitch * h > n) return false;
    for (int y = 0; y < h; ++y)
    {
        const uint8_t *s = d + dataOff + (size_t)(topDown ? y : h - 1 - y) * srcPitch;
        uint8_t *dd = &lv.data[(size_t)y * lv.pitch];
        for (int x = 0; x < w; ++x)
        {
            if (bpp == 24) { dd[x * 4] = s[x * 3]; dd[x * 4 + 1] = s[x * 3 + 1]; dd[x * 4 + 2] = s[x * 3 + 2]; dd[x * 4 + 3] = 255; }
            else { memcpy(dd + x * 4, s + x * 4, 4); dd[x * 4 + 3] = 255; }
        }
    }
    img.fileFormat = D3DXIFF_BMP;
    img.format = D3DFMT_X8R8G8B8;
    img.w = w; img.h = h; img.levels = 1;
    img.mips.push_back(std::move(lv));
    img.faces.push_back(img.mips);
    return true;
}

bool ParseImage(const void *data, size_t size, FileImage &img)
{
    const uint8_t *d = static_cast<const uint8_t *>(data);
    return ParseDDS(d, size, img) || ParseBMP(d, size, img) || ParseTGA(d, size, img);
}

bool ReadFile(const char *path, std::vector<uint8_t> &out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize((size_t)n);
    size_t got = n ? fread(out.data(), 1, (size_t)n, f) : 0;
    fclose(f);
    return got == (size_t)n;
}

std::string WideToNarrow(const WCHAR *w)
{
    std::string s;
    while (w && *w) s.push_back((char)*w++);
    return s;
}

void FillInfo(const FileImage &img, D3DXIMAGE_INFO *info)
{
    if (!info) return;
    info->Width = img.w; info->Height = img.h; info->Depth = img.depth; info->MipLevels = img.levels;
    info->Format = img.format; info->ResourceType = img.type; info->ImageFileFormat = img.fileFormat;
}

} // namespace

//==============================================================================
extern "C" {

HRESULT WINAPI D3DXCheckTextureRequirements(LPDIRECT3DDEVICE8 dev, UINT *pw, UINT *ph, UINT *pLevels, DWORD usage, D3DFORMAT *pFmt, D3DPOOL pool)
{
    (void)pool;
    UINT w = pw ? *pw : kDefault, h = ph ? *ph : kDefault;
    if (w == kDefault || w == 0) w = 256;
    if (h == kDefault || h == 0) h = 256;
    D3DCAPS8 caps;
    if (dev && SUCCEEDED(dev->GetDeviceCaps(&caps)))
    {
        w = Min<UINT>(w, caps.MaxTextureWidth);
        h = Min<UINT>(h, caps.MaxTextureHeight);
    }
    D3DFORMAT fmt = pFmt ? *pFmt : D3DFMT_UNKNOWN;
    if (fmt == D3DFMT_UNKNOWN || fmt == (D3DFORMAT)kDefault) fmt = D3DFMT_A8R8G8B8;
    if (!FormatUsable(dev, fmt, usage)) fmt = D3DFMT_A8R8G8B8;
    if (IsCompressed(fmt)) { w = Max<UINT>(w, 4); h = Max<UINT>(h, 4); }
    UINT full = FullMipCount(w, h);
    UINT levels = pLevels ? *pLevels : 0;
    if (levels == 0 || levels == kDefault || levels > full) levels = full;
    if (pw) *pw = w;
    if (ph) *ph = h;
    if (pLevels) *pLevels = levels;
    if (pFmt) *pFmt = fmt;
    return D3D_OK;
}

HRESULT WINAPI D3DXCheckCubeTextureRequirements(LPDIRECT3DDEVICE8 dev, UINT *pSize, UINT *pLevels, DWORD usage, D3DFORMAT *pFmt, D3DPOOL pool)
{
    UINT w = pSize ? *pSize : kDefault;
    HRESULT hr = D3DXCheckTextureRequirements(dev, &w, &w, pLevels, usage, pFmt, pool);
    if (pSize) *pSize = w;
    return hr;
}

HRESULT WINAPI D3DXCheckVolumeTextureRequirements(LPDIRECT3DDEVICE8 dev, UINT *pw, UINT *ph, UINT *pd, UINT *pLevels, DWORD usage, D3DFORMAT *pFmt, D3DPOOL pool)
{
    UINT w = pw ? *pw : kDefault, h = ph ? *ph : kDefault, d = pd ? *pd : kDefault;
    if (d == kDefault || d == 0) d = 1;
    UINT levels = pLevels ? *pLevels : 0;
    UINT dummy = 0;
    HRESULT hr = D3DXCheckTextureRequirements(dev, &w, &h, &dummy, usage, pFmt, pool);
    UINT full = FullMipCount(w, h, d);
    if (levels == 0 || levels == kDefault || levels > full) levels = full;
    if (pw) *pw = w;
    if (ph) *ph = h;
    if (pd) *pd = d;
    if (pLevels) *pLevels = levels;
    return hr;
}

HRESULT WINAPI D3DXCreateTexture(LPDIRECT3DDEVICE8 dev, UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, LPDIRECT3DTEXTURE8 *pp)
{
    if (!dev || !pp) return D3DERR_INVALIDCALL;
    HRESULT hr = D3DXCheckTextureRequirements(dev, &w, &h, &levels, usage, &fmt, pool);
    if (FAILED(hr)) return hr;
    return dev->CreateTexture(w, h, levels, usage, fmt, pool, pp);
}

HRESULT WINAPI D3DXCreateCubeTexture(LPDIRECT3DDEVICE8 dev, UINT size, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, LPDIRECT3DCUBETEXTURE8 *pp)
{
    if (!dev || !pp) return D3DERR_INVALIDCALL;
    HRESULT hr = D3DXCheckCubeTextureRequirements(dev, &size, &levels, usage, &fmt, pool);
    if (FAILED(hr)) return hr;
    return dev->CreateCubeTexture(size, levels, usage, fmt, pool, pp);
}

HRESULT WINAPI D3DXCreateVolumeTexture(LPDIRECT3DDEVICE8 dev, UINT w, UINT h, UINT d, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, LPDIRECT3DVOLUMETEXTURE8 *pp)
{
    if (!dev || !pp) return D3DERR_INVALIDCALL;
    HRESULT hr = D3DXCheckVolumeTextureRequirements(dev, &w, &h, &d, &levels, usage, &fmt, pool);
    if (FAILED(hr)) return hr;
    return dev->CreateVolumeTexture(w, h, d, levels, usage, fmt, pool, pp);
}

//------------------------------------------------------------------------------
// Surfaces
//------------------------------------------------------------------------------
HRESULT WINAPI D3DXLoadSurfaceFromSurface(LPDIRECT3DSURFACE8 pDst, const PALETTEENTRY *, const RECT *pDstRect, LPDIRECT3DSURFACE8 pSrc,
                                          const PALETTEENTRY *, const RECT *pSrcRect, DWORD filter, D3DCOLOR colorKey)
{
    if (!pDst || !pSrc) return D3DERR_INVALIDCALL;
    Surface *src = AsSurface(pSrc), *dst = AsSurface(pDst);
    RECT sr = pSrcRect ? *pSrcRect : RECT{0, 0, (LONG)src->Width(), (LONG)src->Height()};
    RECT dr = pDstRect ? *pDstRect : RECT{0, 0, (LONG)dst->Width(), (LONG)dst->Height()};
    if (sr.left < 0 || sr.top < 0 || sr.right > (LONG)src->Width() || sr.bottom > (LONG)src->Height() || sr.right <= sr.left || sr.bottom <= sr.top)
        return D3DERR_INVALIDCALL;
    const uint32_t sw = sr.right - sr.left, sh = sr.bottom - sr.top, dw = dr.right - dr.left, dh = dr.bottom - dr.top;
    // Read the source rectangle in its own format when no scaling/keying is involved, ARGB otherwise.
    if (sw == dw && sh == dh && !colorKey)
    {
        const FormatInfo *di = GetFormatInfo(dst->Format());
        if (!di) return D3DERR_INVALIDCALL;
        RECT rd = sr;
        if (di->blockBytes) { rd.left &= ~3; rd.top &= ~3; }
        uint32_t pitch = FormatPitch(dst->Format(), dw);
        std::vector<uint8_t> tmp((size_t)pitch * FormatRows(dst->Format(), dh));
        if (!src->ReadPixelsTo(rd, dst->Format(), tmp.data(), pitch)) return D3DERR_INVALIDCALL;
        return dst->WritePixelsFrom(dr, dst->Format(), tmp.data(), pitch) ? D3D_OK : D3DERR_INVALIDCALL;
    }
    std::vector<uint8_t> tmp((size_t)sw * sh * 4);
    if (!src->ReadPixelsTo(sr, D3DFMT_A8R8G8B8, tmp.data(), sw * 4)) return D3DERR_INVALIDCALL;
    RECT full = {0, 0, (LONG)sw, (LONG)sh};
    return LoadIntoSurface(pDst, pDstRect, tmp.data(), D3DFMT_A8R8G8B8, sw * 4, sw, sh, &full, filter, colorKey);
}

HRESULT WINAPI D3DXLoadSurfaceFromMemory(LPDIRECT3DSURFACE8 pDst, const PALETTEENTRY *, const RECT *pDstRect, LPCVOID pSrc, D3DFORMAT srcFmt,
                                         UINT srcPitch, const PALETTEENTRY *, const RECT *pSrcRect, DWORD filter, D3DCOLOR colorKey)
{
    if (!pSrcRect) return D3DERR_INVALIDCALL; // D3DX requires the rectangle for raw memory
    return LoadIntoSurface(pDst, pDstRect, pSrc, srcFmt, srcPitch, pSrcRect->right, pSrcRect->bottom, pSrcRect, filter, colorKey);
}

HRESULT WINAPI D3DXGetImageInfoFromFileInMemory(LPCVOID data, UINT size, D3DXIMAGE_INFO *info)
{
    FileImage img;
    if (!data || !ParseImage(data, size, img)) return D3DXERR_INVALIDDATA_;
    FillInfo(img, info);
    return D3D_OK;
}

HRESULT WINAPI D3DXGetImageInfoFromFileA(LPCSTR file, D3DXIMAGE_INFO *info)
{
    std::vector<uint8_t> bytes;
    if (!file || !ReadFile(file, bytes)) return D3DXERR_INVALIDDATA_;
    return D3DXGetImageInfoFromFileInMemory(bytes.data(), (UINT)bytes.size(), info);
}

HRESULT WINAPI D3DXGetImageInfoFromFileW(LPCWSTR file, D3DXIMAGE_INFO *info)
{
    return D3DXGetImageInfoFromFileA(WideToNarrow(file).c_str(), info);
}

HRESULT WINAPI D3DXLoadSurfaceFromFileInMemory(LPDIRECT3DSURFACE8 pDst, const PALETTEENTRY *, const RECT *pDstRect, LPCVOID data, UINT size,
                                               const RECT *pSrcRect, DWORD filter, D3DCOLOR colorKey, D3DXIMAGE_INFO *info)
{
    FileImage img;
    if (!data || !ParseImage(data, size, img)) return D3DXERR_INVALIDDATA_;
    FillInfo(img, info);
    const FileImage::Level &lv = img.mips[0];
    return LoadIntoSurface(pDst, pDstRect, lv.data.data(), img.format, lv.pitch, lv.w, lv.h, pSrcRect, filter, colorKey);
}

HRESULT WINAPI D3DXLoadSurfaceFromFileA(LPDIRECT3DSURFACE8 pDst, const PALETTEENTRY *pal, const RECT *pDstRect, LPCSTR file, const RECT *pSrcRect,
                                        DWORD filter, D3DCOLOR colorKey, D3DXIMAGE_INFO *info)
{
    std::vector<uint8_t> bytes;
    if (!file || !ReadFile(file, bytes)) return D3DXERR_INVALIDDATA_;
    return D3DXLoadSurfaceFromFileInMemory(pDst, pal, pDstRect, bytes.data(), (UINT)bytes.size(), pSrcRect, filter, colorKey, info);
}

HRESULT WINAPI D3DXLoadSurfaceFromFileW(LPDIRECT3DSURFACE8 pDst, const PALETTEENTRY *pal, const RECT *pDstRect, LPCWSTR file, const RECT *pSrcRect,
                                        DWORD filter, D3DCOLOR colorKey, D3DXIMAGE_INFO *info)
{
    return D3DXLoadSurfaceFromFileA(pDst, pal, pDstRect, WideToNarrow(file).c_str(), pSrcRect, filter, colorKey, info);
}

//------------------------------------------------------------------------------
// Texture filtering
//------------------------------------------------------------------------------
HRESULT WINAPI D3DXFilterTexture(LPDIRECT3DBASETEXTURE8 pBase, const PALETTEENTRY *, UINT srcLevel, DWORD filter)
{
    D3DRESOURCETYPE type;
    TextureBase *tb = AsBase(pBase, &type);
    if (!tb || !tb->Valid()) return D3DERR_INVALIDCALL;
    if (srcLevel >= tb->m_levelCount) return D3DERR_INVALIDCALL;
    if (filter == kDefault) filter = D3DX_FILTER_BOX;
    const FormatInfo *fi = tb->m_info;

    if (type == D3DRTYPE_VOLUMETEXTURE)
    {
        for (UINT l = srcLevel; l + 1 < tb->m_levelCount; ++l)
        {
            LevelData &s = tb->LevelAt(0, l), &d = tb->LevelAt(0, l + 1);
            if (s.shadow.empty()) return D3DERR_INVALIDCALL;
            // Source slices to ARGB.
            std::vector<uint8_t> src((size_t)s.width * s.height * s.depth * 4);
            for (uint32_t z = 0; z < s.depth; ++z)
                ConvertPixels(tb->m_format, s.shadow.data() + (size_t)z * s.slicePitch, s.pitch, D3DFMT_A8R8G8B8,
                              &src[(size_t)z * s.width * s.height * 4], s.width * 4, s.width, s.height);
            std::vector<uint8_t> dst((size_t)d.width * d.height * d.depth * 4);
            for (uint32_t z = 0; z < d.depth; ++z)
                for (uint32_t y = 0; y < d.height; ++y)
                    for (uint32_t x = 0; x < d.width; ++x)
                    {
                        uint32_t acc[4] = {0, 0, 0, 0}, n = 0;
                        for (uint32_t dz = 0; dz < 2; ++dz)
                            for (uint32_t dy = 0; dy < 2; ++dy)
                                for (uint32_t dx = 0; dx < 2; ++dx)
                                {
                                    uint32_t sx = Min(x * 2 + dx, s.width - 1), sy = Min(y * 2 + dy, s.height - 1), sz = Min(z * 2 + dz, s.depth - 1);
                                    const uint8_t *p = &src[(((size_t)sz * s.height + sy) * s.width + sx) * 4];
                                    for (int c = 0; c < 4; ++c) acc[c] += p[c];
                                    ++n;
                                }
                        uint8_t *o = &dst[(((size_t)z * d.height + y) * d.width + x) * 4];
                        for (int c = 0; c < 4; ++c) o[c] = (uint8_t)((acc[c] + n / 2) / n);
                    }
            d.shadow.assign((size_t)d.slicePitch * d.depth, 0);
            for (uint32_t z = 0; z < d.depth; ++z)
                ConvertPixels(D3DFMT_A8R8G8B8, &dst[(size_t)z * d.width * d.height * 4], d.width * 4, tb->m_format,
                              d.shadow.data() + (size_t)z * d.slicePitch, d.pitch, d.width, d.height);
            d.shadowValid = true;
            tb->UploadWhole(0, l + 1);
        }
        return D3D_OK;
    }

    for (UINT f = 0; f < tb->m_faces; ++f)
        for (UINT l = srcLevel; l + 1 < tb->m_levelCount; ++l)
        {
            LevelData &s = tb->LevelAt(f, l), &d = tb->LevelAt(f, l + 1);
            std::vector<uint8_t> srcArgb((size_t)s.width * s.height * 4);
            RECT full = {0, 0, (LONG)s.width, (LONG)s.height};
            std::vector<uint8_t> own((size_t)s.pitch * FormatRows(tb->m_format, s.height));
            if (!tb->ReadRect(f, l, full, own.data(), s.pitch)) return D3DERR_INVALIDCALL;
            Image img;
            if (!ToImage(tb->m_format, own.data(), s.pitch, s.width, s.height, img)) return D3DERR_INVALIDCALL;
            Image out = Resample(img, d.width, d.height, filter | D3DX_FILTER_BOX);
            uint32_t pitch = FormatPitch(tb->m_format, d.width);
            std::vector<uint8_t> enc((size_t)pitch * FormatRows(tb->m_format, d.height));
            if (fi->blockBytes)
            {
                // Pad to whole blocks.
                uint32_t pw = (d.width + 3) & ~3u, ph = (d.height + 3) & ~3u;
                std::vector<uint8_t> padded((size_t)pw * ph * 4);
                for (uint32_t y = 0; y < ph; ++y)
                    for (uint32_t x = 0; x < pw; ++x)
                        memcpy(&padded[((size_t)y * pw + x) * 4], &out.px[((size_t)Min(y, d.height - 1) * d.width + Min(x, d.width - 1)) * 4], 4);
                if (!ConvertPixels(D3DFMT_A8R8G8B8, padded.data(), pw * 4, tb->m_format, enc.data(), pitch, pw, ph)) return D3DERR_INVALIDCALL;
            }
            else if (!ConvertPixels(D3DFMT_A8R8G8B8, out.px.data(), d.width * 4, tb->m_format, enc.data(), pitch, d.width, d.height))
                return D3DERR_INVALIDCALL;
            RECT dr = {0, 0, (LONG)d.width, (LONG)d.height};
            if (!tb->WriteRect(f, l + 1, dr, enc.data(), pitch)) return D3DERR_INVALIDCALL;
        }
    return D3D_OK;
}

HRESULT WINAPI D3DXFillTexture(LPDIRECT3DTEXTURE8 pTex, LPD3DXFILL2D fn, LPVOID data)
{
    if (!pTex || !fn) return D3DERR_INVALIDCALL;
    D3DSURFACE_DESC desc;
    for (UINT l = 0; l < pTex->GetLevelCount(); ++l)
    {
        pTex->GetLevelDesc(l, &desc);
        const FormatInfo *fi = GetFormatInfo(desc.Format);
        if (!fi || fi->blockBytes) return D3DERR_INVALIDCALL;
        D3DLOCKED_RECT lr;
        if (FAILED(pTex->LockRect(l, &lr, nullptr, 0))) return D3DERR_INVALIDCALL;
        D3DXVECTOR2 texel(1.0f / desc.Width, 1.0f / desc.Height);
        for (UINT y = 0; y < desc.Height; ++y)
            for (UINT x = 0; x < desc.Width; ++x)
            {
                D3DXVECTOR2 uv((x + 0.5f) * texel.x, (y + 0.5f) * texel.y);
                D3DXVECTOR4 c(0, 0, 0, 0);
                fn(&c, &uv, &texel, data);
                auto q = [](float v) { return (uint32_t)Clamp(v * 255.0f + 0.5f, 0.0f, 255.0f); };
                uint32_t argb = (q(c.w) << 24) | (q(c.x) << 16) | (q(c.y) << 8) | q(c.z);
                PackPixel(desc.Format, argb, (uint8_t *)lr.pBits + (size_t)y * lr.Pitch + (size_t)x * (fi->bits / 8));
            }
        pTex->UnlockRect(l);
    }
    return D3D_OK;
}

//------------------------------------------------------------------------------
// Texture creation from files
//------------------------------------------------------------------------------
HRESULT WINAPI D3DXCreateTextureFromFileInMemoryEx(LPDIRECT3DDEVICE8 dev, LPCVOID data, UINT size, UINT w, UINT h, UINT levels, DWORD usage,
                                                   D3DFORMAT fmt, D3DPOOL pool, DWORD filter, DWORD mipFilter, D3DCOLOR colorKey,
                                                   D3DXIMAGE_INFO *info, PALETTEENTRY *, LPDIRECT3DTEXTURE8 *pp)
{
    if (!dev || !data || !pp) return D3DERR_INVALIDCALL;
    FileImage img;
    if (!ParseImage(data, size, img)) return D3DXERR_INVALIDDATA_;
    FillInfo(img, info);
    if (w == kDefault || w == 0) w = img.w;
    if (h == kDefault || h == 0) h = img.h;
    if (fmt == D3DFMT_UNKNOWN) fmt = img.format;
    if (levels == kDefault) levels = 0;
    HRESULT hr = D3DXCheckTextureRequirements(dev, &w, &h, &levels, usage, &fmt, pool);
    if (FAILED(hr)) return hr;
    IDirect3DTexture8 *tex = nullptr;
    hr = dev->CreateTexture(w, h, levels, usage, fmt, pool, &tex);
    if (FAILED(hr)) return hr;

    // Level 0 from the file (scaled if required), the rest from the file's own chain when the sizes
    // match, generated otherwise.
    IDirect3DSurface8 *s0 = nullptr;
    tex->GetSurfaceLevel(0, &s0);
    const FileImage::Level &base = img.mips[0];
    hr = LoadIntoSurface(s0, nullptr, base.data.data(), img.format, base.pitch, base.w, base.h, nullptr, filter, colorKey);
    s0->Release();
    if (FAILED(hr)) { tex->Release(); return hr; }
    bool filePyramid = img.levels > 1 && base.w == w && base.h == h && fmt == img.format;
    if (filePyramid)
    {
        for (UINT l = 1; l < tex->GetLevelCount() && l < img.mips.size(); ++l)
        {
            IDirect3DSurface8 *s = nullptr;
            tex->GetSurfaceLevel(l, &s);
            const FileImage::Level &lv = img.mips[l];
            LoadIntoSurface(s, nullptr, lv.data.data(), img.format, lv.pitch, lv.w, lv.h, nullptr, D3DX_FILTER_NONE, 0);
            s->Release();
        }
    }
    else if (tex->GetLevelCount() > 1)
        D3DXFilterTexture(tex, nullptr, 0, mipFilter);
    *pp = tex;
    return D3D_OK;
}

HRESULT WINAPI D3DXCreateTextureFromFileInMemory(LPDIRECT3DDEVICE8 dev, LPCVOID data, UINT size, LPDIRECT3DTEXTURE8 *pp)
{
    return D3DXCreateTextureFromFileInMemoryEx(dev, data, size, kDefault, kDefault, kDefault, 0, D3DFMT_UNKNOWN, D3DPOOL_MANAGED,
                                               D3DX_FILTER_TRIANGLE, D3DX_FILTER_BOX, 0, nullptr, nullptr, pp);
}

HRESULT WINAPI D3DXCreateTextureFromFileExA(LPDIRECT3DDEVICE8 dev, LPCSTR file, UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT fmt,
                                            D3DPOOL pool, DWORD filter, DWORD mipFilter, D3DCOLOR colorKey, D3DXIMAGE_INFO *info,
                                            PALETTEENTRY *pal, LPDIRECT3DTEXTURE8 *pp)
{
    std::vector<uint8_t> bytes;
    if (!file || !ReadFile(file, bytes)) return D3DXERR_INVALIDDATA_;
    return D3DXCreateTextureFromFileInMemoryEx(dev, bytes.data(), (UINT)bytes.size(), w, h, levels, usage, fmt, pool, filter, mipFilter,
                                               colorKey, info, pal, pp);
}

HRESULT WINAPI D3DXCreateTextureFromFileExW(LPDIRECT3DDEVICE8 dev, LPCWSTR file, UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT fmt,
                                            D3DPOOL pool, DWORD filter, DWORD mipFilter, D3DCOLOR colorKey, D3DXIMAGE_INFO *info,
                                            PALETTEENTRY *pal, LPDIRECT3DTEXTURE8 *pp)
{
    return D3DXCreateTextureFromFileExA(dev, WideToNarrow(file).c_str(), w, h, levels, usage, fmt, pool, filter, mipFilter, colorKey, info, pal, pp);
}

HRESULT WINAPI D3DXCreateTextureFromFileA(LPDIRECT3DDEVICE8 dev, LPCSTR file, LPDIRECT3DTEXTURE8 *pp)
{
    return D3DXCreateTextureFromFileExA(dev, file, kDefault, kDefault, kDefault, 0, D3DFMT_UNKNOWN, D3DPOOL_MANAGED, D3DX_FILTER_TRIANGLE,
                                        D3DX_FILTER_BOX, 0, nullptr, nullptr, pp);
}

HRESULT WINAPI D3DXCreateTextureFromFileW(LPDIRECT3DDEVICE8 dev, LPCWSTR file, LPDIRECT3DTEXTURE8 *pp)
{
    return D3DXCreateTextureFromFileA(dev, WideToNarrow(file).c_str(), pp);
}

} // extern "C"
