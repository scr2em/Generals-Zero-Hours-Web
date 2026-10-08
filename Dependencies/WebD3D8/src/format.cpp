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
** WebAssembly port: D3DFORMAT description table and pixel conversions.
*/
#include "format.h"

namespace webd3d8 {

#ifndef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F0
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif

namespace {

// clang-format off
//                      d3d                     name                bits blk alpha tex    rt     depth  stenc  conv                  glInternal        glFormat           glType                        glBytes
const FormatInfo kFormats[] = {
    {D3DFMT_A8R8G8B8,  "A8R8G8B8",  32, 0, true,  true,  true,  false, false, UploadConv::BGRA8,    GL_RGBA8,          GL_RGBA,           GL_UNSIGNED_BYTE,             4},
    {D3DFMT_X8R8G8B8,  "X8R8G8B8",  32, 0, false, true,  true,  false, false, UploadConv::BGRX8,    GL_RGBA8,          GL_RGBA,           GL_UNSIGNED_BYTE,             4},
    {D3DFMT_R8G8B8,    "R8G8B8",    24, 0, false, true,  false, false, false, UploadConv::BGR8,     GL_RGB8,           GL_RGB,            GL_UNSIGNED_BYTE,             3},
    {D3DFMT_R5G6B5,    "R5G6B5",    16, 0, false, true,  true,  false, false, UploadConv::None,     GL_RGB565,         GL_RGB,            GL_UNSIGNED_SHORT_5_6_5,      2},
    {D3DFMT_A1R5G5B5,  "A1R5G5B5",  16, 0, true,  true,  true,  false, false, UploadConv::A1R5G5B5, GL_RGB5_A1,        GL_RGBA,           GL_UNSIGNED_SHORT_5_5_5_1,    2},
    {D3DFMT_X1R5G5B5,  "X1R5G5B5",  16, 0, false, true,  true,  false, false, UploadConv::X1R5G5B5, GL_RGB5_A1,        GL_RGBA,           GL_UNSIGNED_SHORT_5_5_5_1,    2},
    {D3DFMT_A4R4G4B4,  "A4R4G4B4",  16, 0, true,  true,  true,  false, false, UploadConv::A4R4G4B4, GL_RGBA4,          GL_RGBA,           GL_UNSIGNED_SHORT_4_4_4_4,    2},
    {D3DFMT_X4R4G4B4,  "X4R4G4B4",  16, 0, false, true,  false, false, false, UploadConv::X4R4G4B4, GL_RGBA4,          GL_RGBA,           GL_UNSIGNED_SHORT_4_4_4_4,    2},
    {D3DFMT_A8,        "A8",         8, 0, true,  true,  false, false, false, UploadConv::None,     GL_ALPHA,          GL_ALPHA,          GL_UNSIGNED_BYTE,             1},
    {D3DFMT_L8,        "L8",         8, 0, false, true,  false, false, false, UploadConv::None,     GL_LUMINANCE,      GL_LUMINANCE,      GL_UNSIGNED_BYTE,             1},
    {D3DFMT_A8L8,      "A8L8",      16, 0, true,  true,  false, false, false, UploadConv::None,     GL_LUMINANCE_ALPHA,GL_LUMINANCE_ALPHA,GL_UNSIGNED_BYTE,             2},
    {D3DFMT_A4L4,      "A4L4",       8, 0, true,  true,  false, false, false, UploadConv::A4L4,     GL_LUMINANCE_ALPHA,GL_LUMINANCE_ALPHA,GL_UNSIGNED_BYTE,             2},
    // Formats that exist for image surfaces / data only (not textures).
    {D3DFMT_R3G3B2,    "R3G3B2",     8, 0, false, false, false, false, false, UploadConv::None,     0, 0, 0, 0},
    {D3DFMT_A8R3G3B2,  "A8R3G3B2",  16, 0, true,  false, false, false, false, UploadConv::None,     0, 0, 0, 0},
    {D3DFMT_P8,        "P8",         8, 0, false, false, false, false, false, UploadConv::None,     0, 0, 0, 0},
    {D3DFMT_A8P8,      "A8P8",      16, 0, true,  false, false, false, false, UploadConv::None,     0, 0, 0, 0},
    {D3DFMT_V8U8,      "V8U8",      16, 0, false, false, false, false, false, UploadConv::None,     0, 0, 0, 0},
    {D3DFMT_L6V5U5,    "L6V5U5",    16, 0, false, false, false, false, false, UploadConv::None,     0, 0, 0, 0},
    {D3DFMT_X8L8V8U8,  "X8L8V8U8",  32, 0, false, false, false, false, false, UploadConv::None,     0, 0, 0, 0},
    {D3DFMT_Q8W8V8U8,  "Q8W8V8U8",  32, 0, true,  false, false, false, false, UploadConv::None,     0, 0, 0, 0},
    {D3DFMT_V16U16,    "V16U16",    32, 0, false, false, false, false, false, UploadConv::None,     0, 0, 0, 0},
    {D3DFMT_W11V11U10, "W11V11U10", 32, 0, false, false, false, false, false, UploadConv::None,     0, 0, 0, 0},
    {D3DFMT_UYVY,      "UYVY",      16, 0, false, false, false, false, false, UploadConv::None,     0, 0, 0, 0},
    {D3DFMT_YUY2,      "YUY2",      16, 0, false, false, false, false, false, UploadConv::None,     0, 0, 0, 0},
    // Block compressed.
    {D3DFMT_DXT1,      "DXT1",       0, 8,  true,  true,  false, false, false, UploadConv::DXT,     GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, GL_RGBA, GL_UNSIGNED_BYTE, 0},
    {D3DFMT_DXT2,      "DXT2",       0, 16, true,  true,  false, false, false, UploadConv::DXT,     GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, GL_RGBA, GL_UNSIGNED_BYTE, 0},
    {D3DFMT_DXT3,      "DXT3",       0, 16, true,  true,  false, false, false, UploadConv::DXT,     GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, GL_RGBA, GL_UNSIGNED_BYTE, 0},
    {D3DFMT_DXT4,      "DXT4",       0, 16, true,  true,  false, false, false, UploadConv::DXT,     GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, GL_RGBA, GL_UNSIGNED_BYTE, 0},
    {D3DFMT_DXT5,      "DXT5",       0, 16, true,  true,  false, false, false, UploadConv::DXT,     GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, GL_RGBA, GL_UNSIGNED_BYTE, 0},
    // Depth / stencil.
    {D3DFMT_D16_LOCKABLE,"D16_LOCKABLE",16,0,false,false,false,true, false, UploadConv::None,     0, 0, 0, 0},
    {D3DFMT_D32,       "D32",       32, 0, false, false, false, true,  false, UploadConv::None,     0, 0, 0, 0},
    {D3DFMT_D15S1,     "D15S1",     16, 0, false, false, false, true,  true,  UploadConv::None,     0, 0, 0, 0},
    {D3DFMT_D24S8,     "D24S8",     32, 0, false, false, false, true,  true,  UploadConv::None,     0, 0, 0, 0},
    {D3DFMT_D16,       "D16",       16, 0, false, false, false, true,  false, UploadConv::None,     0, 0, 0, 0},
    {D3DFMT_D24X8,     "D24X8",     32, 0, false, false, false, true,  false, UploadConv::None,     0, 0, 0, 0},
    {D3DFMT_D24X4S4,   "D24X4S4",   32, 0, false, false, false, true,  true,  UploadConv::None,     0, 0, 0, 0},
};
// clang-format on

inline uint32_t Expand(uint32_t v, int bits)
{
    // Replicates the top bits so that the maximum value maps to 255.
    switch (bits)
    {
    case 1: return v ? 255 : 0;
    case 2: return v * 85;
    case 3: return (v << 5) | (v << 2) | (v >> 1);
    case 4: return v * 17;
    case 5: return (v << 3) | (v >> 2);
    case 6: return (v << 2) | (v >> 4);
    default: return v;
    }
}

inline uint32_t Reduce(uint32_t v8, int bits)
{
    return (v8 * ((1u << bits) - 1) + 127) / 255;
}

} // namespace

const FormatInfo *GetFormatInfo(D3DFORMAT format)
{
    for (const FormatInfo &f : kFormats)
        if (f.d3d == format) return &f;
    return nullptr;
}

uint32_t FormatPitch(D3DFORMAT format, uint32_t width)
{
    const FormatInfo *i = GetFormatInfo(format);
    if (!i) return width * 4;
    if (i->blockBytes) return ((width + 3) / 4) * i->blockBytes;
    return (width * i->bits + 7) / 8;
}

uint32_t FormatRows(D3DFORMAT format, uint32_t height)
{
    const FormatInfo *i = GetFormatInfo(format);
    if (i && i->blockBytes) return (height + 3) / 4;
    return height;
}

uint32_t FullMipCount(uint32_t w, uint32_t h, uint32_t d)
{
    uint32_t m = Max(w, Max(h, d));
    uint32_t n = 1;
    while (m > 1) { m >>= 1; ++n; }
    return n;
}

uint32_t UnpackPixel(D3DFORMAT format, const uint8_t *p)
{
    switch (format)
    {
    case D3DFMT_A8R8G8B8: { uint32_t v; memcpy(&v, p, 4); return v; }
    case D3DFMT_X8R8G8B8: { uint32_t v; memcpy(&v, p, 4); return v | 0xFF000000u; }
    case D3DFMT_R8G8B8: return 0xFF000000u | (p[2] << 16) | (p[1] << 8) | p[0];
    case D3DFMT_R5G6B5:
    {
        uint32_t v = p[0] | (p[1] << 8);
        return 0xFF000000u | (Expand((v >> 11) & 31, 5) << 16) | (Expand((v >> 5) & 63, 6) << 8) | Expand(v & 31, 5);
    }
    case D3DFMT_A1R5G5B5: case D3DFMT_X1R5G5B5:
    {
        uint32_t v = p[0] | (p[1] << 8);
        uint32_t a = (format == D3DFMT_A1R5G5B5) ? Expand(v >> 15, 1) : 255;
        return (a << 24) | (Expand((v >> 10) & 31, 5) << 16) | (Expand((v >> 5) & 31, 5) << 8) | Expand(v & 31, 5);
    }
    case D3DFMT_A4R4G4B4: case D3DFMT_X4R4G4B4:
    {
        uint32_t v = p[0] | (p[1] << 8);
        uint32_t a = (format == D3DFMT_A4R4G4B4) ? Expand(v >> 12, 4) : 255;
        return (a << 24) | (Expand((v >> 8) & 15, 4) << 16) | (Expand((v >> 4) & 15, 4) << 8) | Expand(v & 15, 4);
    }
    case D3DFMT_R3G3B2:
    {
        uint32_t v = p[0];
        return 0xFF000000u | (Expand(v >> 5, 3) << 16) | (Expand((v >> 2) & 7, 3) << 8) | Expand(v & 3, 2);
    }
    case D3DFMT_A8R3G3B2:
    {
        uint32_t v = p[0] | (p[1] << 8);
        return ((v >> 8) << 24) | (Expand((v >> 5) & 7, 3) << 16) | (Expand((v >> 2) & 7, 3) << 8) | Expand(v & 3, 2);
    }
    case D3DFMT_A8: return (uint32_t)p[0] << 24;
    case D3DFMT_L8: case D3DFMT_P8: return 0xFF000000u | (p[0] * 0x010101u);
    case D3DFMT_A8L8: case D3DFMT_A8P8: return ((uint32_t)p[1] << 24) | (p[0] * 0x010101u);
    case D3DFMT_A4L4: { uint32_t l = Expand(p[0] & 15, 4), a = Expand(p[0] >> 4, 4); return (a << 24) | (l * 0x010101u); }
    default:
    {
        // Unknown/bump/depth formats: expose the low byte as gray.
        return 0xFF000000u | (p[0] * 0x010101u);
    }
    }
}

void PackPixel(D3DFORMAT format, uint32_t argb, uint8_t *p)
{
    uint32_t a = argb >> 24, r = (argb >> 16) & 255, g = (argb >> 8) & 255, b = argb & 255;
    switch (format)
    {
    case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8:
        if (format == D3DFMT_X8R8G8B8) argb |= 0xFF000000u;
        memcpy(p, &argb, 4);
        break;
    case D3DFMT_R8G8B8: p[0] = (uint8_t)b; p[1] = (uint8_t)g; p[2] = (uint8_t)r; break;
    case D3DFMT_R5G6B5:
    {
        uint32_t v = (Reduce(r, 5) << 11) | (Reduce(g, 6) << 5) | Reduce(b, 5);
        p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
        break;
    }
    case D3DFMT_A1R5G5B5: case D3DFMT_X1R5G5B5:
    {
        uint32_t v = ((format == D3DFMT_A1R5G5B5 ? (a >= 128) : 1u) << 15) | (Reduce(r, 5) << 10) | (Reduce(g, 5) << 5) | Reduce(b, 5);
        p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
        break;
    }
    case D3DFMT_A4R4G4B4: case D3DFMT_X4R4G4B4:
    {
        uint32_t v = ((format == D3DFMT_A4R4G4B4 ? Reduce(a, 4) : 15u) << 12) | (Reduce(r, 4) << 8) | (Reduce(g, 4) << 4) | Reduce(b, 4);
        p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
        break;
    }
    case D3DFMT_R3G3B2: p[0] = (uint8_t)((Reduce(r, 3) << 5) | (Reduce(g, 3) << 2) | Reduce(b, 2)); break;
    case D3DFMT_A8R3G3B2:
    {
        uint32_t v = (a << 8) | (Reduce(r, 3) << 5) | (Reduce(g, 3) << 2) | Reduce(b, 2);
        p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
        break;
    }
    case D3DFMT_A8: p[0] = (uint8_t)a; break;
    case D3DFMT_L8: case D3DFMT_P8: p[0] = (uint8_t)((r * 77 + g * 150 + b * 29) >> 8); break;
    case D3DFMT_A8L8: case D3DFMT_A8P8: p[0] = (uint8_t)((r * 77 + g * 150 + b * 29) >> 8); p[1] = (uint8_t)a; break;
    case D3DFMT_A4L4: p[0] = (uint8_t)((Reduce(a, 4) << 4) | Reduce((r * 77 + g * 150 + b * 29) >> 8, 4)); break;
    default: break;
    }
}

//------------------------------------------------------------------------------
// DXT
//------------------------------------------------------------------------------
namespace {

inline void Color565(uint32_t c, uint8_t out[3]) // R,G,B
{
    out[0] = (uint8_t)Expand((c >> 11) & 31, 5);
    out[1] = (uint8_t)Expand((c >> 5) & 63, 6);
    out[2] = (uint8_t)Expand(c & 31, 5);
}

void DecodeColorBlock(const uint8_t *b, bool dxt1, uint8_t rgba[16][4])
{
    uint32_t c0 = b[0] | (b[1] << 8), c1 = b[2] | (b[3] << 8);
    uint8_t pal[4][4];
    Color565(c0, pal[0]); Color565(c1, pal[1]);
    pal[0][3] = pal[1][3] = 255;
    if (c0 > c1 || !dxt1)
    {
        for (int i = 0; i < 3; ++i)
        {
            pal[2][i] = (uint8_t)((2 * pal[0][i] + pal[1][i]) / 3);
            pal[3][i] = (uint8_t)((pal[0][i] + 2 * pal[1][i]) / 3);
        }
        pal[2][3] = pal[3][3] = 255;
    }
    else
    {
        for (int i = 0; i < 3; ++i)
        {
            pal[2][i] = (uint8_t)((pal[0][i] + pal[1][i]) / 2);
            pal[3][i] = 0;
        }
        pal[2][3] = 255;
        pal[3][3] = 0;
    }
    uint32_t bits = b[4] | (b[5] << 8) | (b[6] << 16) | ((uint32_t)b[7] << 24);
    for (int i = 0; i < 16; ++i)
        memcpy(rgba[i], pal[(bits >> (2 * i)) & 3], 4);
}

} // namespace

void DecodeDXT(D3DFORMAT format, const uint8_t *src, uint32_t srcPitch, uint32_t w, uint32_t h,
               uint8_t *dst, uint32_t dstPitch, bool rgba)
{
    const FormatInfo *info = GetFormatInfo(format);
    const uint32_t blockBytes = info->blockBytes;
    const bool dxt1 = (format == D3DFMT_DXT1);
    const bool explicitAlpha = (format == D3DFMT_DXT2 || format == D3DFMT_DXT3);
    const uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4;
    for (uint32_t by = 0; by < bh; ++by)
    {
        const uint8_t *row = src + by * srcPitch;
        for (uint32_t bx = 0; bx < bw; ++bx)
        {
            const uint8_t *blk = row + bx * blockBytes;
            uint8_t px[16][4];
            DecodeColorBlock(blk + (dxt1 ? 0 : 8), dxt1, px);
            if (explicitAlpha)
            {
                for (int i = 0; i < 16; ++i)
                {
                    uint32_t a4 = (blk[i >> 1] >> ((i & 1) * 4)) & 15;
                    px[i][3] = (uint8_t)(a4 * 17);
                }
            }
            else if (!dxt1)
            {
                uint8_t al[8];
                al[0] = blk[0]; al[1] = blk[1];
                if (al[0] > al[1])
                    for (int i = 1; i < 7; ++i) al[i + 1] = (uint8_t)(((7 - i) * al[0] + i * al[1]) / 7);
                else
                {
                    for (int i = 1; i < 5; ++i) al[i + 1] = (uint8_t)(((5 - i) * al[0] + i * al[1]) / 5);
                    al[6] = 0; al[7] = 255;
                }
                uint64_t bits = 0;
                for (int i = 0; i < 6; ++i) bits |= (uint64_t)blk[2 + i] << (8 * i);
                for (int i = 0; i < 16; ++i) px[i][3] = al[(bits >> (3 * i)) & 7];
            }
            for (uint32_t y = 0; y < 4 && by * 4 + y < h; ++y)
                for (uint32_t x = 0; x < 4 && bx * 4 + x < w; ++x)
                {
                    uint8_t *d = dst + (by * 4 + y) * dstPitch + (bx * 4 + x) * 4;
                    const uint8_t *s = px[y * 4 + x];
                    if (rgba) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3]; }
                    else      { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = s[3]; }
                }
        }
    }
}

namespace {

uint16_t To565(const uint8_t rgb[3])
{
    return (uint16_t)((Reduce(rgb[0], 5) << 11) | (Reduce(rgb[1], 6) << 5) | Reduce(rgb[2], 5));
}

void EncodeColorBlock(const uint8_t px[16][4], bool dxt1WithAlpha, uint8_t *out)
{
    // Endpoints from the bounding box diagonal, refined by the principal axis
    // of the covariance (good enough for the occasional runtime conversion).
    int minc[3] = {255, 255, 255}, maxc[3] = {0, 0, 0};
    bool anyTransparent = false;
    int count = 0;
    for (int i = 0; i < 16; ++i)
    {
        if (dxt1WithAlpha && px[i][3] < 128) { anyTransparent = true; continue; }
        ++count;
        for (int c = 0; c < 3; ++c)
        {
            minc[c] = Min<int>(minc[c], px[i][c]);
            maxc[c] = Max<int>(maxc[c], px[i][c]);
        }
    }
    if (count == 0) { minc[0] = minc[1] = minc[2] = maxc[0] = maxc[1] = maxc[2] = 0; }
    uint8_t lo[3], hi[3];
    for (int c = 0; c < 3; ++c) { lo[c] = (uint8_t)minc[c]; hi[c] = (uint8_t)maxc[c]; }
    uint16_t c0 = To565(hi), c1 = To565(lo);
    if (anyTransparent)
    {
        if (c0 > c1) std::swap(c0, c1); // 3-colour mode requires c0 <= c1
    }
    else if (c0 < c1) std::swap(c0, c1);
    else if (c0 == c1 && c1 > 0) { /* flat block: 4-colour mode with identical endpoints works */ }
    out[0] = (uint8_t)c0; out[1] = (uint8_t)(c0 >> 8);
    out[2] = (uint8_t)c1; out[3] = (uint8_t)(c1 >> 8);
    uint8_t pal[4][3];
    Color565(c0, pal[0]); Color565(c1, pal[1]);
    bool four = !(anyTransparent);
    if (four)
        for (int i = 0; i < 3; ++i) { pal[2][i] = (uint8_t)((2 * pal[0][i] + pal[1][i]) / 3); pal[3][i] = (uint8_t)((pal[0][i] + 2 * pal[1][i]) / 3); }
    else
        for (int i = 0; i < 3; ++i) { pal[2][i] = (uint8_t)((pal[0][i] + pal[1][i]) / 2); pal[3][i] = 0; }
    uint32_t bits = 0;
    for (int i = 0; i < 16; ++i)
    {
        int best = 0;
        if (!four && px[i][3] < 128) best = 3;
        else
        {
            int bestD = 1 << 30;
            for (int k = 0; k < (four ? 4 : 3); ++k)
            {
                int d = 0;
                for (int c = 0; c < 3; ++c) { int e = (int)px[i][c] - pal[k][c]; d += e * e; }
                if (d < bestD) { bestD = d; best = k; }
            }
        }
        bits |= (uint32_t)best << (2 * i);
    }
    out[4] = (uint8_t)bits; out[5] = (uint8_t)(bits >> 8); out[6] = (uint8_t)(bits >> 16); out[7] = (uint8_t)(bits >> 24);
}

} // namespace

bool EncodeDXT(D3DFORMAT format, const uint8_t *src, uint32_t srcPitch, uint32_t w, uint32_t h,
               uint8_t *dst, uint32_t dstPitch)
{
    if (format != D3DFMT_DXT1 && format != D3DFMT_DXT3 && format != D3DFMT_DXT5) return false;
    const FormatInfo *info = GetFormatInfo(format);
    const uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4;
    for (uint32_t by = 0; by < bh; ++by)
        for (uint32_t bx = 0; bx < bw; ++bx)
        {
            uint8_t px[16][4]; // R,G,B,A
            for (uint32_t y = 0; y < 4; ++y)
                for (uint32_t x = 0; x < 4; ++x)
                {
                    uint32_t sx = Min(bx * 4 + x, w - 1), sy = Min(by * 4 + y, h - 1);
                    const uint8_t *s = src + sy * srcPitch + sx * 4; // B,G,R,A
                    uint8_t *d = px[y * 4 + x];
                    d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = s[3];
                }
            uint8_t *out = dst + by * dstPitch + bx * info->blockBytes;
            if (format == D3DFMT_DXT1)
                EncodeColorBlock(px, true, out);
            else
            {
                if (format == D3DFMT_DXT3)
                {
                    for (int i = 0; i < 8; ++i)
                        out[i] = (uint8_t)(Reduce(px[2 * i][3], 4) | (Reduce(px[2 * i + 1][3], 4) << 4));
                }
                else
                {
                    int amin = 255, amax = 0;
                    for (int i = 0; i < 16; ++i) { amin = Min<int>(amin, px[i][3]); amax = Max<int>(amax, px[i][3]); }
                    uint8_t al[8];
                    al[0] = (uint8_t)amax; al[1] = (uint8_t)amin;
                    for (int i = 1; i < 7; ++i) al[i + 1] = (uint8_t)(((7 - i) * al[0] + i * al[1]) / 7);
                    uint64_t bits = 0;
                    for (int i = 0; i < 16; ++i)
                    {
                        int best = 0, bestD = 1 << 30;
                        for (int k = 0; k < 8; ++k)
                        {
                            int d = std::abs((int)px[i][3] - (int)al[k]);
                            if (d < bestD) { bestD = d; best = k; }
                        }
                        bits |= (uint64_t)best << (3 * i);
                    }
                    out[0] = al[0]; out[1] = al[1];
                    for (int i = 0; i < 6; ++i) out[2 + i] = (uint8_t)(bits >> (8 * i));
                }
                EncodeColorBlock(px, false, out + 8);
            }
        }
    return true;
}

//------------------------------------------------------------------------------
// Conversion
//------------------------------------------------------------------------------
bool ConvertPixels(D3DFORMAT sf, const void *srcv, uint32_t srcPitch,
                   D3DFORMAT df, void *dstv, uint32_t dstPitch, uint32_t w, uint32_t h)
{
    const FormatInfo *si = GetFormatInfo(sf), *di = GetFormatInfo(df);
    if (!si || !di) return false;
    const uint8_t *src = static_cast<const uint8_t *>(srcv);
    uint8_t *dst = static_cast<uint8_t *>(dstv);

    if (sf == df)
    {
        uint32_t rowBytes = FormatPitch(sf, w), rows = FormatRows(sf, h);
        for (uint32_t y = 0; y < rows; ++y) memcpy(dst + y * dstPitch, src + y * srcPitch, rowBytes);
        return true;
    }
    if (di->blockBytes)
    {
        // Compress via A8R8G8B8.
        std::vector<uint8_t> tmp((size_t)w * h * 4);
        if (!ConvertPixels(sf, src, srcPitch, D3DFMT_A8R8G8B8, tmp.data(), w * 4, w, h)) return false;
        return EncodeDXT(df, tmp.data(), w * 4, w, h, dst, dstPitch);
    }
    if (si->blockBytes)
    {
        if (df == D3DFMT_A8R8G8B8)
        {
            DecodeDXT(sf, src, srcPitch, w, h, dst, dstPitch, false);
            return true;
        }
        std::vector<uint8_t> tmp((size_t)w * h * 4);
        DecodeDXT(sf, src, srcPitch, w, h, tmp.data(), w * 4, false);
        return ConvertPixels(D3DFMT_A8R8G8B8, tmp.data(), w * 4, df, dst, dstPitch, w, h);
    }
    const uint32_t sb = si->bits / 8, db = di->bits / 8;
    for (uint32_t y = 0; y < h; ++y)
    {
        const uint8_t *s = src + y * srcPitch;
        uint8_t *d = dst + y * dstPitch;
        for (uint32_t x = 0; x < w; ++x, s += sb, d += db)
            PackPixel(df, UnpackPixel(sf, s), d);
    }
    return true;
}

const uint8_t *PrepareForGL(const FormatInfo &info, const uint8_t *src, uint32_t srcPitch,
                            uint32_t w, uint32_t h, bool decodeDxt, std::vector<uint8_t> &scratch)
{
    if (info.conv == UploadConv::DXT)
    {
        if (!decodeDxt)
        {
            // Compressed blocks go to GL as one contiguous run.
            uint32_t rowBytes = FormatPitch(info.d3d, w), rows = FormatRows(info.d3d, h);
            if (srcPitch == rowBytes) return src;
            scratch.resize((size_t)rowBytes * rows);
            for (uint32_t y = 0; y < rows; ++y) memcpy(&scratch[(size_t)y * rowBytes], src + (size_t)y * srcPitch, rowBytes);
            return scratch.data();
        }
        scratch.resize((size_t)w * h * 4);
        DecodeDXT(info.d3d, src, srcPitch, w, h, scratch.data(), w * 4, true);
        return scratch.data();
    }

    const uint32_t outBytes = info.glBytes;
    const uint32_t rowBytes = w * outBytes;
    if (info.conv == UploadConv::None)
    {
        if (srcPitch == rowBytes) return src;
        scratch.resize((size_t)rowBytes * h);
        for (uint32_t y = 0; y < h; ++y) memcpy(&scratch[(size_t)y * rowBytes], src + (size_t)y * srcPitch, rowBytes);
        return scratch.data();
    }

    scratch.resize((size_t)rowBytes * h);
    for (uint32_t y = 0; y < h; ++y)
    {
        const uint8_t *s = src + (size_t)y * srcPitch;
        uint8_t *d = &scratch[(size_t)y * rowBytes];
        switch (info.conv)
        {
        case UploadConv::BGRA8:
            for (uint32_t x = 0; x < w; ++x, s += 4, d += 4) { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = s[3]; }
            break;
        case UploadConv::BGRX8:
            for (uint32_t x = 0; x < w; ++x, s += 4, d += 4) { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = 255; }
            break;
        case UploadConv::BGR8:
            for (uint32_t x = 0; x < w; ++x, s += 3, d += 3) { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; }
            break;
        case UploadConv::A1R5G5B5: case UploadConv::X1R5G5B5:
            for (uint32_t x = 0; x < w; ++x, s += 2, d += 2)
            {
                uint16_t v = (uint16_t)(s[0] | (s[1] << 8));
                uint16_t o = (uint16_t)(((v & 0x7FFF) << 1) | (info.conv == UploadConv::X1R5G5B5 ? 1 : (v >> 15)));
                d[0] = (uint8_t)o; d[1] = (uint8_t)(o >> 8);
            }
            break;
        case UploadConv::A4R4G4B4: case UploadConv::X4R4G4B4:
            for (uint32_t x = 0; x < w; ++x, s += 2, d += 2)
            {
                uint16_t v = (uint16_t)(s[0] | (s[1] << 8));
                uint16_t a = (info.conv == UploadConv::X4R4G4B4) ? 15 : (v >> 12);
                uint16_t o = (uint16_t)(((v & 0x0FFF) << 4) | a);
                d[0] = (uint8_t)o; d[1] = (uint8_t)(o >> 8);
            }
            break;
        case UploadConv::A4L4:
            for (uint32_t x = 0; x < w; ++x, s += 1, d += 2) { d[0] = (uint8_t)Expand(s[0] & 15, 4); d[1] = (uint8_t)Expand(s[0] >> 4, 4); }
            break;
        default: break;
        }
    }
    return scratch.data();
}

GLenum DepthRenderbufferFormat(D3DFORMAT format)
{
    switch (format)
    {
    case D3DFMT_D16: case D3DFMT_D16_LOCKABLE: return GL_DEPTH_COMPONENT16;
    case D3DFMT_D32: case D3DFMT_D24X8: return GL_DEPTH_COMPONENT24;
    case D3DFMT_D24S8: case D3DFMT_D24X4S4: case D3DFMT_D15S1: return GL_DEPTH24_STENCIL8;
    default: return GL_DEPTH24_STENCIL8;
    }
}

} // namespace webd3d8
