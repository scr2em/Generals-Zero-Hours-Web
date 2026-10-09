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
#include <cstdlib>
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


static bool g_shaders = false;
static DWORD g_vs = 0, g_psMul = 0, g_psBem = 0, g_ps14 = 0;
static IDirect3DTexture8 *g_bump = nullptr;
static DWORD g_gameShaders[4] = {};

static DWORD AssemblePS(const char *src)
{
    ID3DXBuffer *code = nullptr, *errs = nullptr;
    HRESULT hr = D3DXAssembleShader(src, (UINT)strlen(src), 0, nullptr, &code, &errs);
    if (FAILED(hr))
    {
        printf("assemble failed: %s\n", errs ? (const char *)errs->GetBufferPointer() : "?");
        return 0;
    }
    DWORD h = 0;
    hr = g_dev->CreatePixelShader((const DWORD *)code->GetBufferPointer(), &h);
    if (FAILED(hr)) printf("CreatePixelShader failed 0x%x\n", (unsigned)hr);
    code->Release();
    return h;
}

static void CreateShaders()
{
    ID3DXBuffer *code = nullptr, *errs = nullptr;
    const char *vsSrc =
        "vs.1.1\n"
        "dp4 oPos.x, v0, c0\n"
        "dp4 oPos.y, v0, c1\n"
        "dp4 oPos.z, v0, c2\n"
        "dp4 oPos.w, v0, c3\n"
        "mul oD0, v5, c4\n"
        "mov oT0, v7\n"
        "mov oT1, v7\n";
    HRESULT hr = D3DXAssembleShader(vsSrc, (UINT)strlen(vsSrc), 0, nullptr, &code, &errs);
    if (FAILED(hr)) { printf("vs assemble failed: %s\n", errs ? (const char *)errs->GetBufferPointer() : "?"); return; }
    DWORD decl[] = {D3DVSD_STREAM(0), D3DVSD_REG(0, D3DVSDT_FLOAT3), D3DVSD_REG(5, D3DVSDT_D3DCOLOR),
                    D3DVSD_REG(7, D3DVSDT_FLOAT2), D3DVSD_END()};
    hr = g_dev->CreateVertexShader(decl, (const DWORD *)code->GetBufferPointer(), &g_vs, 0);
    printf("CreateVertexShader -> 0x%x handle %u\n", (unsigned)hr, (unsigned)g_vs);
    code->Release();

    g_psMul = AssemblePS("ps.1.1\ntex t0\ntex t1\nmul r0, v0, t0\nmad r0.rgb, t1, c0, r0\n");
    g_psBem = AssemblePS("ps.1.1\ntex t0\ntexbem t1, t0\nmov r0, t1\n");
    g_ps14 = AssemblePS("ps.1.4\ntexld r0, t0\nmul r0, r0, v0\n");
    printf("pixel shaders: %u %u %u\n", (unsigned)g_psMul, (unsigned)g_psBem, (unsigned)g_ps14);

    // The inline pixel shaders of the game (W3DWater.cpp, W3DProfilerFrameCapture.cpp), verbatim.
    {
        const char *river =
            "ps.1.1\n \
            tex t0 \n\
            tex t1	\n\
            tex t2	\n\
            tex t3\n\
            mul r0.rgb, v0, t0 ; blend vertex color into t0. \n\
            mov r0.a, t0 ; keep vertex alpha from fading the base water. \n\
            mul r1, t1, t2 ; mul\n\
            add r1.rgb, r1, t3\n\
            mul r1.rgb, r1, v0.a\n\
            +mul r0.a, r0, t3\n\
            add r0.rgb, r0, r1\n";
        const char *water =
            "ps.1.1\n \
            tex t0 \n\
            tex t1	\n\
            texbem t2, t1 ; use t1 as env map adjustment on t2.\n\
            mul r0,v0,t0 ; blend vertex color into t0. \n\
            mul r1.rgb,t2,c0 ; reduce t2 (environment mapped reflection) by constant\n\
            add r0.rgb, r0, r1";
        const char *trapezoid =
            "ps.1.1\n \
            tex t0 ;get water texture\n\
            tex t1 ;get white highlights on black background\n\
            tex t2 ;get white highlights with more tiling\n\
            tex t3	; get black shroud \n\
            mul r0,v0,t0 ; blend vertex color and alpha into base texture. \n\
            mad r0.rgb, t1, t2, r0	; blend sparkles and noise \n\
            mul r0.rgb, r0, t3 ; blend in black shroud \n\
            ;\n";
        const char *swizzle =
            "ps.1.4\n"
            "texld r0, t0\n"
            "mov r1.a, r0.r\n"
            "mov r2.a, r0.g\n"
            "mov r3.a, r0.b\n"
            "mul r0.rgb, r3.a, c0\n"
            "mad r0.rgb, r2.a, c1, r0\n"
            "mad r0.rgb, r1.a, c2, r0\n";
        g_gameShaders[0] = AssemblePS(river);
        g_gameShaders[1] = AssemblePS(water);
        g_gameShaders[2] = AssemblePS(trapezoid);
        g_gameShaders[3] = AssemblePS(swizzle);
        printf("game shader river: %u water: %u trapezoid: %u swizzle(ps1.4): %u\n", (unsigned)g_gameShaders[0],
               (unsigned)g_gameShaders[1], (unsigned)g_gameShaders[2], (unsigned)g_gameShaders[3]);
    }

    // Signed bump map: du/dv pattern.
    g_dev->CreateTexture(16, 16, 1, 0, D3DFMT_V8U8, D3DPOOL_MANAGED, &g_bump);
    if (g_bump)
    {
        D3DLOCKED_RECT lr;
        g_bump->LockRect(0, &lr, nullptr, 0);
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
            {
                signed char *p = (signed char *)((BYTE *)lr.pBits + y * lr.Pitch) + x * 2;
                p[0] = (signed char)((x < 8) ? 100 : -100); // U
                p[1] = 0;                                      // V
            }
        g_bump->UnlockRect(0);
    }
}

static void RenderShaderScene(IDirect3DTexture8 *checker, IDirect3DTexture8 *gradient)
{
    struct SV { float x, y, z; DWORD color; float u, v; };
    SV q[4] = {{-0.9f, -0.9f, 0.5f, 0xFFFFFF80, 0, 1}, {-0.9f, 0.9f, 0.5f, 0xFF80FFFF, 0, 0},
               {0.9f, 0.9f, 0.5f, 0xFFFF80FF, 1, 0}, {0.9f, -0.9f, 0.5f, 0xFFFFFFFF, 1, 1}};
    WORD qi[6] = {0, 1, 2, 0, 2, 3};
    D3DXMATRIX id;
    D3DXMatrixIdentity(&id);

    // 1) Vertex shader (vs.1.1) with fixed-function texturing.
    SetCommonStates();
    g_dev->SetTransform(D3DTS_WORLD, &id);
    g_dev->SetTransform(D3DTS_VIEW, &id);
    g_dev->SetTransform(D3DTS_PROJECTION, &id);
    g_dev->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 0);
    SetViewport(0, 0, 100, 100);
    g_dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    g_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    g_dev->SetTexture(0, checker);
    float c0[4][4];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) c0[i][j] = (i == j) ? 1.0f : 0.0f;
    g_dev->SetVertexShaderConstant(0, c0, 4);
    float tint[4] = {1.0f, 0.6f, 0.6f, 1.0f};
    g_dev->SetVertexShaderConstant(4, tint, 1);
    DWORD decl;
    (void)decl;
    g_dev->SetVertexShader(g_vs);
    CHECK(g_dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 2, qi, D3DFMT_INDEX16, q, sizeof(SV)));
    g_dev->SetVertexShader(TEX_FVF);

    // 2) ps.1.1 with two textures and a constant.
    SetViewport(100, 0, 100, 100);
    g_dev->SetTexture(0, checker);
    g_dev->SetTexture(1, gradient);
    float k[4] = {0.0f, 0.5f, 0.0f, 0.0f};
    g_dev->SetPixelShaderConstant(0, k, 1);
    g_dev->SetPixelShader(g_psMul);
    CHECK(g_dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 2, qi, D3DFMT_INDEX16, q, sizeof(SV)));

    // 3) texbem: the checker is displaced horizontally by the signed bump map.
    SetViewport(200, 0, 100, 100);
    g_dev->SetTexture(0, g_bump);
    g_dev->SetTexture(1, checker);
    g_dev->SetTextureStageState(1, D3DTSS_BUMPENVMAT00, FloatToDword(0.25f));
    g_dev->SetTextureStageState(1, D3DTSS_BUMPENVMAT01, FloatToDword(0.0f));
    g_dev->SetTextureStageState(1, D3DTSS_BUMPENVMAT10, FloatToDword(0.0f));
    g_dev->SetTextureStageState(1, D3DTSS_BUMPENVMAT11, FloatToDword(0.25f));
    g_dev->SetPixelShader(g_psBem);
    CHECK(g_dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 2, qi, D3DFMT_INDEX16, q, sizeof(SV)));

    // 4) ps.1.4 texld.
    SetViewport(300, 0, 20, 100);
    g_dev->SetTexture(0, gradient);
    g_dev->SetPixelShader(g_ps14);
    CHECK(g_dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 2, qi, D3DFMT_INDEX16, q, sizeof(SV)));

    // 5) The game's own inline shaders, drawn tiny (compiles and links them).
    SetViewport(330, 100, 8, 8);
    for (int s = 0; s < 4; ++s)
    {
        g_dev->SetTexture(0, checker);
        g_dev->SetTexture(1, s == 1 ? g_bump : checker);
        g_dev->SetTexture(2, checker);
        g_dev->SetTexture(3, checker);
        for (int st = 0; st < 4; ++st) g_dev->SetTextureStageState(st, D3DTSS_TEXCOORDINDEX, 0);
        g_dev->SetPixelShader(g_gameShaders[s]);
        CHECK(g_dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 2, qi, D3DFMT_INDEX16, q, sizeof(SV)));
    }
    for (int st = 1; st < 4; ++st) g_dev->SetTexture(st, nullptr);

    g_dev->SetPixelShader(0);
    g_dev->SetTexture(1, nullptr);
    SetViewport(0, 0, 640, 480);
}


//------------------------------------------------------------------------------
// Scene set 2: texture generation, projection, cube/volume maps, point sprites,
// lights, stencil, wireframe and clip planes (--scene2).
//------------------------------------------------------------------------------
static bool g_scene2 = false;

struct SphereVertex { float x, y, z, nx, ny, nz; };
#define SPHERE_FVF (D3DFVF_XYZ | D3DFVF_NORMAL)

static void BuildSphere(std::vector<SphereVertex> &v, std::vector<WORD> &idx, int n)
{
    for (int i = 0; i <= n; ++i)
        for (int j = 0; j <= n; ++j)
        {
            float th = 3.14159265f * i / n, ph = 2.0f * 3.14159265f * j / n;
            SphereVertex s;
            s.x = std::sin(th) * std::cos(ph); s.y = std::cos(th); s.z = std::sin(th) * std::sin(ph);
            s.nx = s.x; s.ny = s.y; s.nz = s.z;
            v.push_back(s);
        }
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
        {
            WORD a = (WORD)(i * (n + 1) + j), b = (WORD)(a + 1), c = (WORD)(a + n + 1), d = (WORD)(c + 1);
            idx.push_back(a); idx.push_back(b); idx.push_back(c);
            idx.push_back(b); idx.push_back(d); idx.push_back(c);
        }
}

static void Cell(int i)
{
    SetViewport((i % 4) * 160, (i / 4) * 160, 160, 160);
}

static void CameraTransforms(float dist = 3.0f)
{
    D3DXMATRIX world, view, proj;
    D3DXMatrixIdentity(&world);
    D3DXVECTOR3 eye(0, 0, -dist), at(0, 0, 0), up(0, 1, 0);
    D3DXMatrixLookAtLH(&view, &eye, &at, &up);
    D3DXMatrixPerspectiveFovLH(&proj, 0.8f, 1.0f, 0.5f, 30.0f);
    g_dev->SetTransform(D3DTS_WORLD, &world);
    g_dev->SetTransform(D3DTS_VIEW, &view);
    g_dev->SetTransform(D3DTS_PROJECTION, &proj);
}

static void RenderScene2(IDirect3DTexture8 *checker)
{
    static std::vector<SphereVertex> sv;
    static std::vector<WORD> si;
    if (sv.empty()) BuildSphere(sv, si, 24);
    const UINT ns = (UINT)sv.size(), nt = (UINT)si.size() / 3;

    static IDirect3DCubeTexture8 *cube = nullptr;
    static IDirect3DVolumeTexture8 *vol = nullptr;
    if (!cube)
    {
        CHECK(g_dev->CreateCubeTexture(32, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &cube));
        static const DWORD faceColor[6] = {0xFFFF2020, 0xFF802020, 0xFF20FF20, 0xFF208020, 0xFF2020FF, 0xFF202080};
        for (int f = 0; f < 6; ++f)
        {
            D3DLOCKED_RECT lr;
            CHECK(cube->LockRect((D3DCUBEMAP_FACES)f, 0, &lr, nullptr, 0));
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x)
                    ((DWORD *)((BYTE *)lr.pBits + y * lr.Pitch))[x] = ((x / 8 + y / 8) & 1) ? faceColor[f] : (faceColor[f] | 0x00606060);
            CHECK(cube->UnlockRect((D3DCUBEMAP_FACES)f, 0));
        }
        CHECK(g_dev->CreateVolumeTexture(8, 8, 8, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &vol));
        D3DLOCKED_BOX lb;
        CHECK(vol->LockBox(0, &lb, nullptr, 0));
        for (int z = 0; z < 8; ++z)
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x)
                    ((DWORD *)((BYTE *)lb.pBits + z * lb.SlicePitch + y * lb.RowPitch))[x] = 0xFF000000 | ((x * 36) << 16) | ((y * 36) << 8) | (z * 36);
        CHECK(vol->UnlockBox(0));
    }

    // Cell 0: environment mapped sphere (camera space reflection vector -> cube map).
    {
        SetCommonStates();
        Cell(0);
        CameraTransforms();
        g_dev->SetTexture(0, cube);
        g_dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR);
        g_dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT3);
        D3DXMATRIX invView, view;
        g_dev->GetTransform(D3DTS_VIEW, &view);
        D3DXMatrixInverse(&invView, nullptr, &view);
        g_dev->SetTransform(D3DTS_TEXTURE0, &invView);
        g_dev->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE);
        g_dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        g_dev->SetVertexShader(SPHERE_FVF);
        CHECK(g_dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, ns, nt, si.data(), D3DFMT_INDEX16, sv.data(), sizeof(SphereVertex)));
    }

    // Cell 1: projected texture on a ground plane (camera space position + projective transform).
    {
        SetCommonStates();
        Cell(1);
        D3DXMATRIX world, view, proj;
        D3DXMatrixIdentity(&world);
        D3DXVECTOR3 eye(0, 2.2f, -3.0f), at(0, -1, 1), up(0, 1, 0);
        D3DXMatrixLookAtLH(&view, &eye, &at, &up);
        D3DXMatrixPerspectiveFovLH(&proj, 0.9f, 1.0f, 0.5f, 30.0f);
        g_dev->SetTransform(D3DTS_WORLD, &world);
        g_dev->SetTransform(D3DTS_VIEW, &view);
        g_dev->SetTransform(D3DTS_PROJECTION, &proj);
        // Projector: looks straight down from above (0,3,1), orthographic 2x2 world units.
        D3DXMATRIX pView, pProj, bias, invView, m;
        D3DXVECTOR3 pe(0, 3, 1), pa(0, -1, 1), pu(0, 0, 1);
        D3DXMatrixLookAtLH(&pView, &pe, &pa, &pu);
        D3DXMatrixOrthoLH(&pProj, 2.0f, 2.0f, 0.1f, 10.0f);
        D3DXMatrixIdentity(&bias);
        bias._11 = 0.5f; bias._22 = -0.5f; bias._33 = 1.0f; bias._41 = 0.5f; bias._42 = 0.5f;
        D3DXMatrixInverse(&invView, nullptr, &view);
        D3DXMatrixMultiply(&m, &invView, &pView);
        D3DXMatrixMultiply(&m, &m, &pProj);
        D3DXMatrixMultiply(&m, &m, &bias);
        g_dev->SetTransform(D3DTS_TEXTURE0, &m);
        g_dev->SetTexture(0, checker);
        g_dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
        g_dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT3 | D3DTTFF_PROJECTED);
        g_dev->SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
        g_dev->SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
        g_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        ColVertex gv[4] = {{-3, -1, -1, 0xFF804020}, {-3, -1, 4, 0xFF804020}, {3, -1, 4, 0xFF804020}, {3, -1, -1, 0xFF804020}};
        WORD gi[6] = {0, 1, 2, 0, 2, 3};
        g_dev->SetVertexShader(COL_FVF);
        CHECK(g_dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 2, gi, D3DFMT_INDEX16, gv, sizeof(ColVertex)));
    }

    // Cell 2: point sprites (textured, distance scaled).
    {
        SetCommonStates();
        Cell(2);
        CameraTransforms(4.0f);
        g_dev->SetTexture(0, checker);
        g_dev->SetRenderState(D3DRS_POINTSPRITEENABLE, TRUE);
        g_dev->SetRenderState(D3DRS_POINTSCALEENABLE, TRUE);
        g_dev->SetRenderState(D3DRS_POINTSIZE, FloatToDword(0.35f));
        g_dev->SetRenderState(D3DRS_POINTSIZE_MIN, FloatToDword(1.0f));
        g_dev->SetRenderState(D3DRS_POINTSCALE_A, FloatToDword(0.0f));
        g_dev->SetRenderState(D3DRS_POINTSCALE_B, FloatToDword(0.0f));
        g_dev->SetRenderState(D3DRS_POINTSCALE_C, FloatToDword(0.05f));
        ColVertex pts[4] = {{-1, -1, 0, 0xFFFF8080}, {1, -1, 1, 0xFF80FF80}, {-1, 1, 2, 0xFF8080FF}, {1, 1, 3, 0xFFFFFF80}};
        g_dev->SetVertexShader(COL_FVF);
        g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        CHECK(g_dev->DrawPrimitiveUP(D3DPT_POINTLIST, 4, pts, sizeof(ColVertex)));
        g_dev->SetRenderState(D3DRS_POINTSPRITEENABLE, FALSE);
        g_dev->SetRenderState(D3DRS_POINTSCALEENABLE, FALSE);
    }

    // Cell 3: volume texture.
    {
        SetCommonStates();
        Cell(3);
        CameraTransforms();
        g_dev->SetTexture(0, vol);
        g_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        struct VV { float x, y, z; DWORD c; float u, v, w; } vq[4] = {
            {-1, -1, 0, 0xFFFFFFFF, 0, 1, 0.1f}, {-1, 1, 0, 0xFFFFFFFF, 0, 0, 0.5f}, {1, 1, 0, 0xFFFFFFFF, 1, 0, 0.9f}, {1, -1, 0, 0xFFFFFFFF, 1, 1, 0.5f}};
        WORD vi[6] = {0, 1, 2, 0, 2, 3};
        g_dev->SetVertexShader(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE3(0));
        g_dev->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_POINT);
        g_dev->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
        CHECK(g_dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 2, vi, D3DFMT_INDEX16, vq, sizeof(VV)));
    }

    // Cell 4: point + spot lights with specular on a sphere.
    {
        SetCommonStates();
        Cell(4);
        CameraTransforms();
        D3DMATERIAL8 mat;
        memset(&mat, 0, sizeof mat);
        mat.Diffuse.r = mat.Diffuse.g = mat.Diffuse.b = mat.Diffuse.a = 0.8f;
        mat.Ambient = mat.Diffuse;
        mat.Specular.r = mat.Specular.g = mat.Specular.b = 1.0f;
        mat.Power = 25.0f;
        g_dev->SetMaterial(&mat);
        D3DLIGHT8 l0, l1;
        memset(&l0, 0, sizeof l0);
        l0.Type = D3DLIGHT_POINT;
        l0.Diffuse.r = 1.0f; l0.Diffuse.g = 0.3f; l0.Diffuse.b = 0.3f;
        l0.Specular.r = l0.Specular.g = l0.Specular.b = 1.0f;
        l0.Position.x = 2.0f; l0.Position.y = 1.0f; l0.Position.z = -2.0f;
        l0.Range = 20.0f; l0.Attenuation1 = 0.15f;
        memset(&l1, 0, sizeof l1);
        l1.Type = D3DLIGHT_SPOT;
        l1.Diffuse.r = 0.2f; l1.Diffuse.g = 0.4f; l1.Diffuse.b = 1.0f;
        l1.Position.x = -2.5f; l1.Position.y = 0.5f; l1.Position.z = -2.5f;
        l1.Direction.x = 2.5f; l1.Direction.y = -0.5f; l1.Direction.z = 2.0f;
        l1.Range = 20.0f; l1.Attenuation0 = 1.0f; l1.Theta = 0.5f; l1.Phi = 1.0f; l1.Falloff = 1.0f;
        g_dev->SetLight(0, &l0);
        g_dev->SetLight(1, &l1);
        g_dev->LightEnable(0, TRUE);
        g_dev->LightEnable(1, TRUE);
        g_dev->SetRenderState(D3DRS_LIGHTING, TRUE);
        g_dev->SetRenderState(D3DRS_SPECULARENABLE, TRUE);
        g_dev->SetRenderState(D3DRS_COLORVERTEX, FALSE);
        g_dev->SetRenderState(D3DRS_AMBIENT, 0xFF202020);
        g_dev->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE);
        g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        g_dev->SetVertexShader(SPHERE_FVF);
        CHECK(g_dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, ns, nt, si.data(), D3DFMT_INDEX16, sv.data(), sizeof(SphereVertex)));
    }

    // Cell 5: stencil (diamond mask) then a colored quad drawn only inside it.
    {
        SetCommonStates();
        Cell(5);
        CameraTransforms();
        g_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        g_dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
        g_dev->SetRenderState(D3DRS_STENCILENABLE, TRUE);
        g_dev->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
        g_dev->SetRenderState(D3DRS_STENCILREF, 1);
        g_dev->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_REPLACE);
        g_dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
        ColVertex diamond[4] = {{0, -1, 0, 0xFFFFFFFF}, {-1, 0, 0, 0xFFFFFFFF}, {0, 1, 0, 0xFFFFFFFF}, {1, 0, 0, 0xFFFFFFFF}};
        g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        g_dev->SetVertexShader(COL_FVF);
        CHECK(g_dev->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, diamond, sizeof(ColVertex)));
        g_dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
        g_dev->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_EQUAL);
        g_dev->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
        ColVertex big[4] = {{-1.4f, -1.4f, 0, 0xFFFF4040}, {-1.4f, 1.4f, 0, 0xFF40FF40}, {1.4f, 1.4f, 0, 0xFF4040FF}, {1.4f, -1.4f, 0, 0xFFFFFF40}};
        CHECK(g_dev->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, big, sizeof(ColVertex)));
        g_dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    }

    // Cell 6: wireframe sphere.
    {
        SetCommonStates();
        Cell(6);
        CameraTransforms();
        g_dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_WIREFRAME);
        g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        g_dev->SetRenderState(D3DRS_LIGHTING, FALSE);
        g_dev->SetVertexShader(SPHERE_FVF);
        CHECK(g_dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, ns, nt, si.data(), D3DFMT_INDEX16, sv.data(), sizeof(SphereVertex)));
        g_dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    }

    // Cell 7: user clip plane (keeps x < 0.2) over a gradient quad, with alpha-blended + additive passes.
    {
        SetCommonStates();
        Cell(7);
        CameraTransforms();
        g_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        float plane[4] = {-1.0f, 0.0f, 0.0f, 0.2f};
        g_dev->SetClipPlane(0, plane);
        g_dev->SetRenderState(D3DRS_CLIPPLANEENABLE, 1);
        g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        ColVertex cq[4] = {{-1.4f, -1.4f, 0, 0xFFFF2020}, {-1.4f, 1.4f, 0, 0xFFFFFF20}, {1.4f, 1.4f, 0, 0xFF20FF20}, {1.4f, -1.4f, 0, 0xFF2020FF}};
        g_dev->SetVertexShader(COL_FVF);
        CHECK(g_dev->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, cq, sizeof(ColVertex)));
        g_dev->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
        // Additive overlay.
        g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        g_dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
        g_dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
        ColVertex ov[4] = {{-0.5f, -0.5f, 0, 0xFF404040}, {-0.5f, 0.5f, 0, 0xFF404040}, {0.5f, 0.5f, 0, 0xFF404040}, {0.5f, -0.5f, 0, 0xFF404040}};
        CHECK(g_dev->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, ov, sizeof(ColVertex)));
    }
}


//------------------------------------------------------------------------------
// Scene set 3: texture formats, UpdateTexture, CopyRects and D3DX (--scene3).
// Every cell fills a texture with a known color and probes the rendered result.
//------------------------------------------------------------------------------
static bool g_scene3 = false;

struct FmtCase { D3DFORMAT fmt; const char *name; DWORD argb; DWORD expect; };

static void RenderScene3()
{
    static const FmtCase cases[] = {
        {D3DFMT_A8R8G8B8, "A8R8G8B8", 0xFF3070C0, 0x3070C0},
        {D3DFMT_X8R8G8B8, "X8R8G8B8", 0x003070C0, 0x3070C0},
        {D3DFMT_R5G6B5, "R5G6B5", 0xFF3070C0, 0x316DC6},
        {D3DFMT_A1R5G5B5, "A1R5G5B5", 0xFF3070C0, 0x316BC6},
        {D3DFMT_A4R4G4B4, "A4R4G4B4", 0xFF3070C0, 0x3377CC},
        {D3DFMT_L8, "L8", 0xFF808080, 0x808080},
        {D3DFMT_A8, "A8 (rgb 0)", 0x80FFFFFF, 0x000000},
        {D3DFMT_A8L8, "A8L8", 0xFF404040, 0x404040},
        {D3DFMT_DXT1, "DXT1", 0, 0x00FF00},
    };
    D3DXMATRIX id;
    D3DXMatrixIdentity(&id);
    struct QV { float x, y, z, rhw; DWORD c; float u, v; };
    int cell = 0;
    for (const FmtCase &fc : cases)
    {
        SetCommonStates();
        const int cx = (cell % 4) * 160, cy = (cell / 4) * 160;
        ++cell;
        SetViewport(cx, cy, 160, 160);
        g_dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
        g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        IDirect3DTexture8 *sys = nullptr, *tex = nullptr;
        HRESULT hr = g_dev->CreateTexture(16, 16, 1, 0, fc.fmt, D3DPOOL_SYSTEMMEM, &sys);
        if (FAILED(hr)) { printf("CASE %s: create failed 0x%x\n", fc.name, (unsigned)hr); continue; }
        D3DLOCKED_RECT lr;
        sys->LockRect(0, &lr, nullptr, 0);
        if (fc.fmt == D3DFMT_DXT1)
        {
            for (int by = 0; by < 4; ++by)
                for (int bx = 0; bx < 4; ++bx)
                {
                    BYTE *blk = (BYTE *)lr.pBits + by * lr.Pitch + bx * 8;
                    WORD c0 = 0x07E0, c1 = 0x07E0;
                    memcpy(blk, &c0, 2); memcpy(blk + 2, &c1, 2);
                    memset(blk + 4, 0, 4);
                }
        }
        else
        {
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x)
                {
                    BYTE *p = (BYTE *)lr.pBits + y * lr.Pitch;
                    DWORD a = fc.argb >> 24, r = (fc.argb >> 16) & 255, g = (fc.argb >> 8) & 255, b = fc.argb & 255;
                    switch (fc.fmt)
                    {
                    case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: ((DWORD *)p)[x] = fc.argb; break;
                    case D3DFMT_R5G6B5: ((WORD *)p)[x] = (WORD)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); break;
                    case D3DFMT_A1R5G5B5: ((WORD *)p)[x] = (WORD)((a ? 0x8000 : 0) | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)); break;
                    case D3DFMT_A4R4G4B4: ((WORD *)p)[x] = (WORD)(((a >> 4) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)); break;
                    case D3DFMT_L8: p[x] = (BYTE)r; break;
                    case D3DFMT_A8: p[x] = (BYTE)a; break;
                    case D3DFMT_A8L8: ((WORD *)p)[x] = (WORD)((a << 8) | r); break;
                    default: break;
                    }
                }
        }
        sys->UnlockRect(0);
        hr = g_dev->CreateTexture(16, 16, 1, 0, fc.fmt, D3DPOOL_DEFAULT, &tex);
        if (FAILED(hr)) { printf("CASE %s: default create failed 0x%x\n", fc.name, (unsigned)hr); sys->Release(); continue; }
        CHECK(g_dev->UpdateTexture(sys, tex));
        sys->Release();
        g_dev->SetTexture(0, tex);
        g_dev->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_POINT);
        g_dev->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
        g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        g_dev->SetVertexShader(RHW_FVF);
        QV q[4] = {{(float)cx + 10, (float)cy + 10, 0.5f, 1, 0xFFFFFFFF, 0, 0}, {(float)cx + 150, (float)cy + 10, 0.5f, 1, 0xFFFFFFFF, 1, 0},
                   {(float)cx + 10, (float)cy + 150, 0.5f, 1, 0xFFFFFFFF, 0, 1}, {(float)cx + 150, (float)cy + 150, 0.5f, 1, 0xFFFFFFFF, 1, 1}};
        CHECK(g_dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(QV)));
        tex->Release();
    }
    // Lighting checks: a quad facing the camera, lit head-on by a directional light.
    // Cell 9: identity world. Cell 10: rotated world (the normal matrix must follow).
    // Both must come out as diffuse * 1.0 = (127, 64, 191).
    for (int k = 0; k < 2; ++k)
    {
        SetCommonStates();
        const int c = 9 + k;
        SetViewport((c % 4) * 160, (c / 4) * 160, 160, 160);
        g_dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
        g_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        D3DXMATRIX world, view, proj, rot;
        D3DXVECTOR3 eye(0, 0, -3), at(0, 0, 0), up(0, 1, 0);
        D3DXMatrixLookAtLH(&view, &eye, &at, &up);
        D3DXMatrixPerspectiveFovLH(&proj, 0.8f, 1.0f, 0.5f, 30.0f);
        // Rotating the world about Y by 0.5 rad turns the quad normal (0,0,-1) to (-sin, 0, -cos).
        D3DXMatrixRotationY(&world, k ? 0.5f : 0.0f);
        g_dev->SetTransform(D3DTS_WORLD, &world);
        g_dev->SetTransform(D3DTS_VIEW, &view);
        g_dev->SetTransform(D3DTS_PROJECTION, &proj);
        D3DMATERIAL8 mat;
        memset(&mat, 0, sizeof mat);
        mat.Diffuse.r = 0.5f; mat.Diffuse.g = 0.25f; mat.Diffuse.b = 0.75f; mat.Diffuse.a = 1.0f;
        g_dev->SetMaterial(&mat);
        D3DLIGHT8 l;
        memset(&l, 0, sizeof l);
        l.Type = D3DLIGHT_DIRECTIONAL;
        l.Diffuse.r = l.Diffuse.g = l.Diffuse.b = 1.0f;
        // Light travels along +normal-opposite: direction = -(rotated normal) in WORLD space.
        const float a = k ? 0.5f : 0.0f;
        l.Direction.x = std::sin(a); l.Direction.y = 0.0f; l.Direction.z = std::cos(a);
        g_dev->SetLight(0, &l);
        g_dev->LightEnable(0, TRUE);
        g_dev->SetRenderState(D3DRS_LIGHTING, TRUE);
        g_dev->SetRenderState(D3DRS_COLORVERTEX, FALSE);
        g_dev->SetRenderState(D3DRS_AMBIENT, 0);
        g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        struct LV { float x, y, z, nx, ny, nz; } lq[4] = {{-1, -1, 0, 0, 0, -1}, {-1, 1, 0, 0, 0, -1}, {1, 1, 0, 0, 0, -1}, {1, -1, 0, 0, 0, -1}};
        g_dev->SetVertexShader(D3DFVF_XYZ | D3DFVF_NORMAL);
        CHECK(g_dev->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, lq, sizeof(LV)));
    }
    // World light direction for cell 10 above is (sin a, 0, cos a) = the light travelling toward +z/+x,
    // i.e. hitting a surface whose normal is (-sin a, 0, -cos a): exactly the rotated quad normal.
    SetViewport(0, 0, 640, 480);
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
    if (g_scene3)
    {
        RenderScene3();
        CHECK(g_dev->EndScene());
        SetViewport(0, 0, 640, 480);
        CHECK(g_dev->Present(nullptr, nullptr, nullptr, nullptr));
        return;
    }
    if (g_scene2)
    {
        RenderScene2(checker);
        CHECK(g_dev->EndScene());
        SetViewport(0, 0, 640, 480);
        CHECK(g_dev->Present(nullptr, nullptr, nullptr, nullptr));
        return;
    }

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

    if (g_shaders) RenderShaderScene(checker, gradient);

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
        if (!strcmp(argv[i], "--shaders")) { WebD3D8_SetShaderModel(0x0101, 0x0104); g_shaders = true; }
        if (!strcmp(argv[i], "--debug")) WebD3D8_SetDebug(1);
        if (!strcmp(argv[i], "--scene2")) g_scene2 = true;
        if (!strcmp(argv[i], "--scene3")) g_scene3 = true;
    }
    (void)blocking;
    WebD3D8_PlatformHooks hooks = {};
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--yield")) hooks.OnFramePresented = [] { emscripten_sleep(0); };
        if (!strcmp(argv[i], "--proxy")) WebD3D8_SetContextProxy(1);
    }
    WebD3D8_SetPlatformHooks(&hooks);
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
    if (g_shaders) CreateShaders();

    IDirect3DTexture8 *rtTex = nullptr;
    CHECK(g_dev->CreateTexture(128, 128, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &rtTex));
    IDirect3DSurface8 *rtDepth = nullptr;
    CHECK(g_dev->CreateDepthStencilSurface(128, 128, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, &rtDepth));
    IDirect3DSurface8 *bb = nullptr, *bbDepth = nullptr;
    CHECK(g_dev->GetRenderTarget(&bb));
    CHECK(g_dev->GetDepthStencilSurface(&bbDepth));

    bool doReset = false;
    for (int i = 1; i < argc; ++i) if (!strcmp(argv[i], "--reset")) doReset = true;
    int kFrames = 90;
    for (int i = 1; i < argc; ++i)
        if (!strncmp(argv[i], "--frames=", 9)) kFrames = atoi(argv[i] + 9);
    for (int f = 0; f < kFrames; ++f)
    {
        if (doReset && f == 40)
        {
            // Back buffer references must be dropped before Reset(), as with real D3D8.
            bb->Release(); bbDepth->Release();
            pp.BackBufferWidth = 640; pp.BackBufferHeight = 480;
            HRESULT rhr = g_dev->Reset(&pp);
            printf("Reset -> 0x%x\n", (unsigned)rhr);
            g_dev->GetRenderTarget(&bb);
            g_dev->GetDepthStencilSurface(&bbDepth);
        }
        RenderFrame(f * 0.02f, checker, gradient, dxt, rtTex, rtDepth, bb, bbDepth, true);
        if (f == 0 || f == kFrames - 1)
        {
            ReadBack();
            printf("frame %d read back\n", f);
        }
        usleep(16000);
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
    if (g_scene3)
        for (int i = 0; i < 11; ++i) Probe("scene3-cell", (i % 4) * 160 + 80, (i / 4) * 160 + 80);
    if (g_shaders)
    {
        Probe("vs-quad", 50, 50);
        Probe("ps11-quad", 150, 50);
        Probe("texbem-left", 215, 50);
        Probe("texbem-right", 285, 50);
        Probe("ps14-quad", 310, 50);
    }
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
