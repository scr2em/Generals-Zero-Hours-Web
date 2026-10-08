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
** WebAssembly port: smoke test of the Direct3D 8 on WebGL2 implementation.
**
** Renders a few scenes through the plain D3D8 API (fixed-function lighting,
** multi-texturing, fog, render-to-texture, DXT textures, pre-transformed 2D
** primitives, alpha test/blend) in a blocking loop on the proxied main
** thread, prints probe pixels read back through GetFrontBuffer() and reports
** TEST_DONE. tests/run_test.mjs drives it in headless Chromium.
**
** Query parameters of the test page (read from argv, see shell): none.
*/
#include "../src/d3d8_headers.h"
#include "WebD3D8/WebD3D8.h"

#include <emscripten.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

template <class T> static T Max(T a, T b) { return a > b ? a : b; }
static DWORD FloatToDword(float f) { DWORD v; memcpy(&v, &f, 4); return v; }

static IDirect3D8 *g_d3d;
static IDirect3DDevice8 *g_dev;

#define CHECK(expr)                                                              \
    do {                                                                         \
        HRESULT _hr = (expr);                                                    \
        if (FAILED(_hr)) { printf("FAILED %s -> 0x%08x (line %d)\n", #expr, (unsigned)_hr, __LINE__); } \
    } while (0)

struct LitVertex { float x, y, z, nx, ny, nz; float u, v; };
#define LIT_FVF (D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1)

struct ColVertex { float x, y, z; DWORD color; };
#define COL_FVF (D3DFVF_XYZ | D3DFVF_DIFFUSE)

struct TexVertex { float x, y, z; DWORD color; float u, v; };
#define TEX_FVF (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1)

struct Tex2Vertex { float x, y, z; DWORD color; float u0, v0, u1, v1; };
#define TEX2_FVF (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX2)

struct RhwVertex { float x, y, z, rhw; DWORD color; float u, v; };
#define RHW_FVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)

static IDirect3DTexture8 *MakeChecker(UINT size, DWORD c0, DWORD c1, bool mips)
{
    IDirect3DTexture8 *tex = nullptr;
    CHECK(g_dev->CreateTexture(size, size, mips ? 0 : 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex));
    if (!tex) return nullptr;
    for (UINT l = 0; l < tex->GetLevelCount(); ++l)
    {
        D3DSURFACE_DESC d;
        tex->GetLevelDesc(l, &d);
        D3DLOCKED_RECT lr;
        CHECK(tex->LockRect(l, &lr, nullptr, 0));
        for (UINT y = 0; y < d.Height; ++y)
        {
            DWORD *row = (DWORD *)((BYTE *)lr.pBits + y * lr.Pitch);
            for (UINT x = 0; x < d.Width; ++x)
            {
                UINT cell = Max<UINT>(1, d.Width / 8);
                row[x] = (((x / cell) + (y / cell)) & 1) ? c1 : c0;
            }
        }
        CHECK(tex->UnlockRect(l));
    }
    return tex;
}


static void SetCommonStates()
{
    g_dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    g_dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    g_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
    g_dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    g_dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    g_dev->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
    for (int s = 0; s < 4; ++s)
    {
        g_dev->SetTexture(s, nullptr);
        g_dev->SetTextureStageState(s, D3DTSS_COLOROP, s == 0 ? D3DTOP_MODULATE : D3DTOP_DISABLE);
        g_dev->SetTextureStageState(s, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        g_dev->SetTextureStageState(s, D3DTSS_COLORARG2, D3DTA_CURRENT);
        g_dev->SetTextureStageState(s, D3DTSS_ALPHAOP, s == 0 ? D3DTOP_SELECTARG1 : D3DTOP_DISABLE);
        g_dev->SetTextureStageState(s, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        g_dev->SetTextureStageState(s, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
        g_dev->SetTextureStageState(s, D3DTSS_TEXCOORDINDEX, s);
        g_dev->SetTextureStageState(s, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
        g_dev->SetTextureStageState(s, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
        g_dev->SetTextureStageState(s, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
        g_dev->SetTextureStageState(s, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
        g_dev->SetTextureStageState(s, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
        g_dev->SetTextureStageState(s, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
    }
}

static void SetViewport(DWORD x, DWORD y, DWORD w, DWORD h)
{
    D3DVIEWPORT8 vp = {x, y, w, h, 0.0f, 1.0f};
    CHECK(g_dev->SetViewport(&vp));
}

// Unit cube with per-face normals and UVs.
static void BuildCube(std::vector<LitVertex> &v, std::vector<WORD> &idx)
{
    static const float n[6][3] = {{0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    static const float u[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, -1}, {0, 0, 1}, {1, 0, 0}, {1, 0, 0}};
    static const float w[6][3] = {{0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    for (int f = 0; f < 6; ++f)
    {
        // Face vertices: corner = n*0.5 + (+-u + +-w)*0.5, ordered clockwise seen from outside (left-handed).
        static const int sx[4] = {-1, -1, 1, 1}, sy[4] = {-1, 1, 1, -1};
        for (int i = 0; i < 4; ++i)
        {
            LitVertex lv;
            lv.x = (n[f][0] + sx[i] * u[f][0] + sy[i] * w[f][0]) * 0.5f;
            lv.y = (n[f][1] + sx[i] * u[f][1] + sy[i] * w[f][1]) * 0.5f;
            lv.z = (n[f][2] + sx[i] * u[f][2] + sy[i] * w[f][2]) * 0.5f;
            lv.nx = n[f][0]; lv.ny = n[f][1]; lv.nz = n[f][2];
            lv.u = (sx[i] + 1) * 0.5f; lv.v = (1 - sy[i]) * 0.5f;
            v.push_back(lv);
        }
        WORD b = (WORD)(f * 4);
        idx.push_back(b); idx.push_back(b + 1); idx.push_back(b + 2);
        idx.push_back(b); idx.push_back(b + 2); idx.push_back(b + 3);
    }
}

static IDirect3DTexture8 *MakeDXT1()
{
    // 16x16 DXT1 with a 4x4 block pattern: red/green alternating plus a punch-through block.
    IDirect3DTexture8 *tex = nullptr;
    HRESULT hr = g_dev->CreateTexture(16, 16, 0, 0, D3DFMT_DXT1, D3DPOOL_MANAGED, &tex);
    if (FAILED(hr)) { printf("DXT1 creation failed 0x%x\n", (unsigned)hr); return nullptr; }
    for (UINT l = 0; l < tex->GetLevelCount(); ++l)
    {
        D3DSURFACE_DESC d;
        tex->GetLevelDesc(l, &d);
        D3DLOCKED_RECT lr;
        CHECK(tex->LockRect(l, &lr, nullptr, 0));
        UINT bw = (d.Width + 3) / 4, bh = (d.Height + 3) / 4;
        for (UINT by = 0; by < bh; ++by)
            for (UINT bx = 0; bx < bw; ++bx)
            {
                BYTE *blk = (BYTE *)lr.pBits + by * lr.Pitch + bx * 8;
                WORD c0 = ((bx + by) & 1) ? 0xF800 : 0x07E0; // red / green
                WORD c1 = ((bx + by) & 1) ? 0x001F : 0xFFE0; // blue / yellow
                memcpy(blk, &c0, 2);
                memcpy(blk + 2, &c1, 2);
                // indices: left half c0, right half c1, a gradient row at the bottom
                BYTE rows[4] = {0x50, 0x50, 0x50, 0xE4};
                memcpy(blk + 4, rows, 4);
            }
        CHECK(tex->UnlockRect(l));
    }
    return tex;
}

static void RenderFrame(float t, IDirect3DTexture8 *checker, IDirect3DTexture8 *gradient, IDirect3DTexture8 *dxt,
                        IDirect3DTexture8 *rtTex, IDirect3DSurface8 *rtDepth, IDirect3DSurface8 *bbSurface,
                        IDirect3DSurface8 *bbDepth, bool doRT)
{
    // Render-to-texture first.
    if (doRT)
    {
        IDirect3DSurface8 *rtSurf = nullptr;
        CHECK(rtTex->GetSurfaceLevel(0, &rtSurf));
        CHECK(g_dev->SetRenderTarget(rtSurf, rtDepth));
        SetViewport(0, 0, 128, 128);
        CHECK(g_dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF303060, 1.0f, 0));
        SetCommonStates();
        g_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        g_dev->SetVertexShader(COL_FVF);
        // Triangle: apex (red) at the top, green bottom-left, blue bottom-right.
        ColVertex tri[3] = {{0.0f, 0.8f, 0.5f, 0xFFFF0000}, {0.8f, -0.8f, 0.5f, 0xFF0000FF}, {-0.8f, -0.8f, 0.5f, 0xFF00FF00}};
        D3DXMATRIX id;
        D3DXMatrixIdentity(&id);
        g_dev->SetTransform(D3DTS_WORLD, &id);
        g_dev->SetTransform(D3DTS_VIEW, &id);
        g_dev->SetTransform(D3DTS_PROJECTION, &id);
        CHECK(g_dev->BeginScene());
        CHECK(g_dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, tri, sizeof(ColVertex)));
        CHECK(g_dev->EndScene());
        CHECK(g_dev->SetRenderTarget(bbSurface, bbDepth));
        rtSurf->Release();
    }

    SetViewport(0, 0, 640, 480);
    CHECK(g_dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0xFF203040, 1.0f, 0));
    CHECK(g_dev->BeginScene());

    //-------------------------------------------------------------------
    // Scene A (top-left): lit, textured, rotating cube (indexed, static VB/IB).
    //-------------------------------------------------------------------
    {
        SetCommonStates();
        SetViewport(0, 0, 320, 240);
        D3DXMATRIX world, rx, ry, view, proj;
        D3DXMatrixRotationY(&ry, 0.6f + t);
        D3DXMatrixRotationX(&rx, 0.5f);
        D3DXMatrixMultiply(&world, &ry, &rx);
        D3DXVECTOR3 eye(0, 1.0f, -3.2f), at(0, 0, 0), up(0, 1, 0);
        D3DXMatrixLookAtLH(&view, &eye, &at, &up);
        D3DXMatrixPerspectiveFovLH(&proj, 0.9f, 320.0f / 240.0f, 0.5f, 20.0f);
        g_dev->SetTransform(D3DTS_WORLD, &world);
        g_dev->SetTransform(D3DTS_VIEW, &view);
        g_dev->SetTransform(D3DTS_PROJECTION, &proj);

        D3DMATERIAL8 mat;
        memset(&mat, 0, sizeof mat);
        mat.Diffuse.r = 1.0f; mat.Diffuse.g = 0.85f; mat.Diffuse.b = 0.6f; mat.Diffuse.a = 1.0f;
        mat.Ambient = mat.Diffuse;
        g_dev->SetMaterial(&mat);
        D3DLIGHT8 light;
        memset(&light, 0, sizeof light);
        light.Type = D3DLIGHT_DIRECTIONAL;
        light.Diffuse.r = light.Diffuse.g = light.Diffuse.b = 1.0f;
        light.Direction.x = -0.5f; light.Direction.y = -0.6f; light.Direction.z = 0.7f;
        g_dev->SetLight(0, &light);
        g_dev->LightEnable(0, TRUE);
        g_dev->SetRenderState(D3DRS_LIGHTING, TRUE);
        g_dev->SetRenderState(D3DRS_AMBIENT, 0xFF404040);
        g_dev->SetRenderState(D3DRS_COLORVERTEX, FALSE);
        g_dev->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE);
        g_dev->SetTexture(0, checker);

        static IDirect3DVertexBuffer8 *vb = nullptr;
        static IDirect3DIndexBuffer8 *ib = nullptr;
        static int count = 0;
        if (!vb)
        {
            std::vector<LitVertex> v;
            std::vector<WORD> idx;
            BuildCube(v, idx);
            count = (int)idx.size() / 3;
            CHECK(g_dev->CreateVertexBuffer((UINT)(v.size() * sizeof(LitVertex)), D3DUSAGE_WRITEONLY, LIT_FVF, D3DPOOL_MANAGED, &vb));
            BYTE *p;
            CHECK(vb->Lock(0, 0, &p, 0));
            memcpy(p, v.data(), v.size() * sizeof(LitVertex));
            CHECK(vb->Unlock());
            CHECK(g_dev->CreateIndexBuffer((UINT)(idx.size() * 2), D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib));
            CHECK(ib->Lock(0, 0, &p, 0));
            memcpy(p, idx.data(), idx.size() * 2);
            CHECK(ib->Unlock());
        }
        CHECK(g_dev->SetStreamSource(0, vb, sizeof(LitVertex)));
        CHECK(g_dev->SetIndices(ib, 0));
        CHECK(g_dev->SetVertexShader(LIT_FVF));
        CHECK(g_dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 24, 0, count));
        g_dev->SetStreamSource(0, nullptr, 0);
        g_dev->SetIndices(nullptr, 0);
    }

    //-------------------------------------------------------------------
    // Scene B (top-right): ground plane with 2 texture stages, fog, TFACTOR.
    //-------------------------------------------------------------------
    {
        SetCommonStates();
        SetViewport(320, 0, 320, 240);
        D3DXMATRIX world, view, proj;
        D3DXMatrixIdentity(&world);
        D3DXVECTOR3 eye(0, 1.5f, -1.0f), at(0, 0, 4), up(0, 1, 0);
        D3DXMatrixLookAtLH(&view, &eye, &at, &up);
        D3DXMatrixPerspectiveFovLH(&proj, 1.0f, 320.0f / 240.0f, 0.3f, 50.0f);
        g_dev->SetTransform(D3DTS_WORLD, &world);
        g_dev->SetTransform(D3DTS_VIEW, &view);
        g_dev->SetTransform(D3DTS_PROJECTION, &proj);
        g_dev->SetRenderState(D3DRS_FOGENABLE, TRUE);
        g_dev->SetRenderState(D3DRS_FOGCOLOR, 0xFF8090A0);
        g_dev->SetRenderState(D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR);
        g_dev->SetRenderState(D3DRS_FOGSTART, FloatToDword(2.0f));
        g_dev->SetRenderState(D3DRS_FOGEND, FloatToDword(9.0f));
        g_dev->SetRenderState(D3DRS_TEXTUREFACTOR, 0xFFFFC0C0);
        g_dev->SetTexture(0, checker);
        g_dev->SetTexture(1, gradient);
        g_dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
        g_dev->SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        g_dev->SetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
        g_dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
        g_dev->SetTextureStageState(2, D3DTSS_COLOROP, D3DTOP_MODULATE);
        g_dev->SetTextureStageState(2, D3DTSS_COLORARG1, D3DTA_TFACTOR);
        g_dev->SetTextureStageState(2, D3DTSS_COLORARG2, D3DTA_CURRENT);
        g_dev->SetTextureStageState(2, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
        g_dev->SetTextureStageState(2, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
        Tex2Vertex quad[4] = {
            {-3, 0, 0, 0xFFFFFFFF, 0, 4, 0, 1},
            {-3, 0, 12, 0xFFFFFFFF, 0, 0, 0, 0},
            {3, 0, 12, 0xFFFFFFFF, 4, 0, 1, 0},
            {3, 0, 0, 0xFFFFFFFF, 4, 4, 1, 1},
        };
        WORD qi[6] = {0, 1, 2, 0, 2, 3};
        g_dev->SetVertexShader(TEX2_FVF);
        CHECK(g_dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 2, qi, D3DFMT_INDEX16, quad, sizeof(Tex2Vertex)));
    }

    //-------------------------------------------------------------------
    // Scene C (bottom-left): render target texture on a quad (triangle must point up).
    //-------------------------------------------------------------------
    {
        SetCommonStates();
        SetViewport(0, 240, 320, 240);
        D3DXMATRIX id, ortho;
        D3DXMatrixIdentity(&id);
        D3DXMatrixOrthoLH(&ortho, 4.0f, 3.0f, 0.0f, 10.0f);
        g_dev->SetTransform(D3DTS_WORLD, &id);
        g_dev->SetTransform(D3DTS_VIEW, &id);
        g_dev->SetTransform(D3DTS_PROJECTION, &ortho);
        g_dev->SetTexture(0, rtTex);
        TexVertex q[4] = {
            {-1.5f, -1.2f, 1, 0xFFFFFFFF, 0, 1}, {-1.5f, 1.2f, 1, 0xFFFFFFFF, 0, 0},
            {1.5f, 1.2f, 1, 0xFFFFFFFF, 1, 0}, {1.5f, -1.2f, 1, 0xFFFFFFFF, 1, 1},
        };
        WORD qi[6] = {0, 1, 2, 0, 2, 3};
        g_dev->SetVertexShader(TEX_FVF);
        CHECK(g_dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 2, qi, D3DFMT_INDEX16, q, sizeof(TexVertex)));
    }

    //-------------------------------------------------------------------
    // Scene D (bottom-right): pre-transformed primitives, DXT1, blending, alpha test, lines, points.
    //-------------------------------------------------------------------
    {
        SetCommonStates();
        SetViewport(320, 240, 320, 240);
        // DXT1 quad.
        g_dev->SetTexture(0, dxt);
        g_dev->SetVertexShader(RHW_FVF);
        g_dev->SetTextureStageState(0, D3DTSS_MIPFILTER, D3DTEXF_NONE);
        g_dev->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
        g_dev->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_POINT);
        RhwVertex dq[4] = {
            {10, 10, 0.5f, 1, 0xFFFFFFFF, 0, 0}, {138, 10, 0.5f, 1, 0xFFFFFFFF, 1, 0},
            {10, 138, 0.5f, 1, 0xFFFFFFFF, 0, 1}, {138, 138, 0.5f, 1, 0xFFFFFFFF, 1, 1},
        };
        // Viewport-relative: XYZRHW coordinates are in the viewport.
        for (auto &v : dq) { v.x += 320; v.y += 240; }
        CHECK(g_dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, dq, sizeof(RhwVertex)));

        // Alpha blended, vertex colored quad over the DXT texture.
        g_dev->SetTexture(0, nullptr);
        g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        g_dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
        g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        g_dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        g_dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        g_dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
        RhwVertex bq[4] = {
            {80, 60, 0.4f, 1, 0x80FFFFFF, 0, 0}, {200, 60, 0.4f, 1, 0x80FF0000, 1, 0},
            {80, 180, 0.4f, 1, 0x800000FF, 0, 1}, {200, 180, 0.4f, 1, 0x80FFFF00, 1, 1},
        };
        for (auto &v : bq) { v.x += 320; v.y += 240; }
        CHECK(g_dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, bq, sizeof(RhwVertex)));

        // Alpha-tested checker (alpha ref 128) with checker texture whose alpha toggles.
        g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        g_dev->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
        g_dev->SetRenderState(D3DRS_ALPHAREF, 0x80);
        g_dev->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
        g_dev->SetTexture(0, gradient);
        g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        g_dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        RhwVertex aq[4] = {
            {220, 20, 0.3f, 1, 0xFFFFFFFF, 0, 0}, {300, 20, 0.3f, 1, 0xFFFFFFFF, 1, 0},
            {220, 100, 0.3f, 1, 0xFFFFFFFF, 0, 1}, {300, 100, 0.3f, 1, 0xFFFFFFFF, 1, 1},
        };
        for (auto &v : aq) { v.x += 320; v.y += 240; }
        CHECK(g_dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, aq, sizeof(RhwVertex)));
        g_dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);

        // Lines and points.
        g_dev->SetTexture(0, nullptr);
        g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        struct PV { float x, y, z, rhw; DWORD c; };
        PV lines[4] = {{330, 440, 0.2f, 1, 0xFFFFFF00}, {440, 470, 0.2f, 1, 0xFFFFFF00}, {330, 470, 0.2f, 1, 0xFF00FFFF}, {440, 440, 0.2f, 1, 0xFF00FFFF}};
        g_dev->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        CHECK(g_dev->DrawPrimitiveUP(D3DPT_LINELIST, 2, lines, sizeof(PV)));
        g_dev->SetRenderState(D3DRS_POINTSIZE, FloatToDword(9.0f));
        PV pts[3] = {{470, 450, 0.2f, 1, 0xFFFF00FF}, {500, 450, 0.2f, 1, 0xFF00FF00}, {530, 450, 0.2f, 1, 0xFFFFFFFF}};
        CHECK(g_dev->DrawPrimitiveUP(D3DPT_POINTLIST, 3, pts, sizeof(PV)));
    }

    CHECK(g_dev->EndScene());
    SetViewport(0, 0, 640, 480);
    CHECK(g_dev->Present(nullptr, nullptr, nullptr, nullptr));
}

static DWORD g_front[640 * 480];

static void ReadBack()
{
    IDirect3DSurface8 *img = nullptr;
    CHECK(g_dev->CreateImageSurface(640, 480, D3DFMT_A8R8G8B8, &img));
    CHECK(g_dev->GetFrontBuffer(img));
    D3DLOCKED_RECT lr;
    CHECK(img->LockRect(&lr, nullptr, D3DLOCK_READONLY));
    for (int y = 0; y < 480; ++y) memcpy(g_front + y * 640, (BYTE *)lr.pBits + y * lr.Pitch, 640 * 4);
    CHECK(img->UnlockRect());
    img->Release();
}

static void Probe(const char *name, int x, int y)
{
    DWORD c = g_front[y * 640 + x];
    printf("PROBE %s (%d,%d) = %02x%02x%02x\n", name, x, y, (c >> 16) & 255, (c >> 8) & 255, c & 255);
}

int main(int argc, char **argv)
{
    bool blocking = true;
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--implicit")) WebD3D8_SetPresentMode(WEBD3D8_PRESENT_IMPLICIT);
        if (!strcmp(argv[i], "--no-s3tc")) WebD3D8_SetDisableS3TC(1);
        if (!strcmp(argv[i], "--shaders")) WebD3D8_SetShaderModel(0x0101, 0x0104);
        if (!strcmp(argv[i], "--debug")) WebD3D8_SetDebug(1);
    }
    (void)blocking;
    printf("web_d3d8_test starting\n");

    g_d3d = Direct3DCreate8(D3D_SDK_VERSION);
    if (!g_d3d) { printf("Direct3DCreate8 failed\n"); return 1; }
    D3DADAPTER_IDENTIFIER8 id;
    g_d3d->GetAdapterIdentifier(0, 0, &id);
    printf("adapter: %s / %s\n", id.Driver, id.Description);
    D3DDISPLAYMODE dm;
    g_d3d->GetAdapterDisplayMode(0, &dm);
    printf("desktop mode %ux%u fmt %d, %u modes\n", dm.Width, dm.Height, (int)dm.Format, g_d3d->GetAdapterModeCount(0));

    D3DPRESENT_PARAMETERS pp;
    memset(&pp, 0, sizeof pp);
    pp.BackBufferWidth = 640;
    pp.BackBufferHeight = 480;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.Windowed = TRUE;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    HRESULT hr = g_d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &g_dev);
    if (FAILED(hr)) { printf("CreateDevice failed 0x%08x\n", (unsigned)hr); return 1; }
    D3DCAPS8 caps;
    g_dev->GetDeviceCaps(&caps);
    printf("caps: max texture %u, max simultaneous textures %u, PS %x VS %x\n", caps.MaxTextureWidth,
           caps.MaxSimultaneousTextures, caps.PixelShaderVersion, caps.VertexShaderVersion);

    IDirect3DTexture8 *checker = MakeChecker(64, 0xFFF0F0F0, 0xFF2060C0, true);
    IDirect3DTexture8 *gradient = MakeChecker(32, 0xFFFFFFFF, 0x40404040, true); // alpha-less in practice, still a texture
    // Make the "gradient" texture alpha-varying: half opaque / half transparent (alpha test target).
    {
        D3DLOCKED_RECT lr;
        gradient->LockRect(0, &lr, nullptr, 0);
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x)
                ((DWORD *)((BYTE *)lr.pBits + y * lr.Pitch))[x] = (x < 16) ? 0xFFFFA020 : 0x20202020;
        gradient->UnlockRect(0);
    }
    IDirect3DTexture8 *dxt = MakeDXT1();

    IDirect3DTexture8 *rtTex = nullptr;
    CHECK(g_dev->CreateTexture(128, 128, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &rtTex));
    IDirect3DSurface8 *rtDepth = nullptr;
    CHECK(g_dev->CreateDepthStencilSurface(128, 128, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, &rtDepth));
    IDirect3DSurface8 *bb = nullptr, *bbDepth = nullptr;
    CHECK(g_dev->GetRenderTarget(&bb));
    CHECK(g_dev->GetDepthStencilSurface(&bbDepth));

    const int kFrames = 90;
    for (int f = 0; f < kFrames; ++f)
    {
        RenderFrame(f * 0.02f, checker, gradient, dxt, rtTex, rtDepth, bb, bbDepth, true);
        if (f == 0 || f == kFrames - 1)
        {
            ReadBack();
            printf("frame %d read back\n", f);
        }
        usleep(8000);
    }

    // Probes (frame kFrames-1 contents).
    Probe("clear-bg", 5, 235);
    Probe("scene-A-cube-center", 160, 120);
    Probe("scene-B-fog-far", 480, 12);
    Probe("scene-B-near", 480, 225);
    Probe("scene-C-rt-apex-top", 160, 330);
    Probe("scene-C-rt-bottom-left", 70, 440);
    Probe("scene-C-rt-bottom-right", 250, 440);
    Probe("scene-D-dxt-0", 335, 255);
    Probe("scene-D-dxt-1", 370, 255);
    Probe("scene-D-blend", 480, 340);
    Probe("scene-D-alphatest-kept", 560, 270);
    Probe("scene-D-alphatest-cut", 600, 270);
    Probe("scene-D-point", 470, 450);
    {
        // Checksum for regression comparisons.
        unsigned long long h = 1469598103934665603ull;
        for (int i = 0; i < 640 * 480; ++i) { h ^= g_front[i]; h *= 1099511628211ull; }
        printf("FRAME_HASH %016llx\n", h);
    }
    printf("TEST_DONE\n");
    fflush(stdout);
    return 0;
}
