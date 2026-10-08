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
** WebAssembly port: device creation, WebGL2 context management, back buffer,
** presentation and render target handling.
*/
#include "d3d8_internal.h"
#include "program.h"
#include "resources.h"
#include "shader.h"

#include <emscripten.h>
#include <emscripten/html5.h>

namespace webd3d8 {

#ifndef GL_UNMASKED_RENDERER_WEBGL
#define GL_UNMASKED_RENDERER_WEBGL 0x9246
#define GL_UNMASKED_VENDOR_WEBGL 0x9245
#endif
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

void PipelineState::Invalidate()
{
    memset(this, 0xFF, sizeof *this);
}

//------------------------------------------------------------------------------
// Creation
//------------------------------------------------------------------------------
Device *Device::Create(IDirect3D8 *d3d, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD behavior,
                       D3DPRESENT_PARAMETERS *pp)
{
    Device *dev = new Device();
    if (!dev->Initialize(d3d, adapter, type, focus, behavior, pp))
    {
        dev->m_refs = 0;
        delete dev;
        return nullptr;
    }
    return dev;
}

bool Device::CreateContext(const D3DPRESENT_PARAMETERS &pp)
{
    const Config &cfg = GetConfig();
    EmscriptenWebGLContextAttributes attr;
    emscripten_webgl_init_context_attributes(&attr);
    attr.alpha = false;
    attr.depth = false;
    attr.stencil = false;
    attr.antialias = false;
    attr.premultipliedAlpha = false;
    attr.preserveDrawingBuffer = false;
    attr.powerPreference = EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE;
    attr.failIfMajorPerformanceCaveat = false;
    attr.majorVersion = 2;
    attr.minorVersion = 0;
    attr.enableExtensionsByDefault = true;
    attr.explicitSwapControl = cfg.presentMode == WEBD3D8_PRESENT_EXPLICIT;

    // The drawing buffer has the size of the back buffer.
    if (pp.BackBufferWidth && pp.BackBufferHeight)
        emscripten_set_canvas_element_size(cfg.canvas.c_str(), (int)pp.BackBufferWidth, (int)pp.BackBufferHeight);

    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = emscripten_webgl_create_context(cfg.canvas.c_str(), &attr);
    if (ctx <= 0)
    {
        Log("could not create a WebGL2 context on '%s' (error %d)", cfg.canvas.c_str(), (int)ctx);
        return false;
    }
    if (emscripten_webgl_make_context_current(ctx) != EMSCRIPTEN_RESULT_SUCCESS)
    {
        Log("could not make the WebGL2 context current");
        emscripten_webgl_destroy_context(ctx);
        return false;
    }
    m_glContext = (int)ctx;
    emscripten_webgl_enable_extension(ctx, "WEBGL_compressed_texture_s3tc");
    emscripten_webgl_enable_extension(ctx, "EXT_texture_filter_anisotropic");
    emscripten_webgl_enable_extension(ctx, "WEBGL_debug_renderer_info");
    return true;
}

void Device::QueryGLCaps()
{
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m_glcaps.maxTextureSize);
    glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &m_glcaps.maxCubeSize);
    glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &m_glcaps.max3DSize);
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &m_glcaps.maxVertexAttribs);
    glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS, &m_glcaps.maxVertexUniformVectors);
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &m_glcaps.maxTextureUnits);
    GLfloat range[2] = {1, 1};
    glGetFloatv(GL_ALIASED_POINT_SIZE_RANGE, range);
    m_glcaps.maxPointSize = range[1];

    // Extensions are queried through the Emscripten helper (works on workers too).
    m_glcaps.s3tc = emscripten_webgl_enable_extension(m_glContext, "WEBGL_compressed_texture_s3tc") != 0;
    if (GetConfig().disableS3TC) m_glcaps.s3tc = false;
    m_glcaps.anisotropic = emscripten_webgl_enable_extension(m_glContext, "EXT_texture_filter_anisotropic") != 0;
    if (m_glcaps.anisotropic)
    {
        GLfloat a = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &a);
        m_glcaps.maxAnisotropy = a;
    }
    const char *v = (const char *)glGetString(GL_VERSION);
    m_glcaps.version = v ? v : "";
    const char *r = (const char *)glGetString(GL_RENDERER);
    const char *ven = (const char *)glGetString(GL_VENDOR);
    if (emscripten_webgl_enable_extension(m_glContext, "WEBGL_debug_renderer_info"))
    {
        const char *ur = (const char *)glGetString(GL_UNMASKED_RENDERER_WEBGL);
        const char *uv = (const char *)glGetString(GL_UNMASKED_VENDOR_WEBGL);
        if (ur && *ur) r = ur;
        if (uv && *uv) ven = uv;
    }
    m_glcaps.renderer = r ? r : "WebGL2";
    m_glcaps.vendor = ven ? ven : "WebGL";
    SetRendererString("WebGL2: " + m_glcaps.renderer);
    Log("WebGL2 renderer: %s / %s (%s), S3TC %s, max texture %d, anisotropy %.0f", m_glcaps.vendor.c_str(),
        m_glcaps.renderer.c_str(), m_glcaps.version.c_str(), m_glcaps.s3tc ? "yes" : "no (decoded on the CPU)",
        m_glcaps.maxTextureSize, m_glcaps.maxAnisotropy);
}

bool Device::Initialize(IDirect3D8 *d3d, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD behavior,
                        D3DPRESENT_PARAMETERS *pp)
{
    m_d3d = d3d;
    m_d3d->AddRef();
    m_creation.AdapterOrdinal = adapter;
    m_creation.DeviceType = type;
    m_creation.hFocusWindow = focus;
    m_creation.BehaviorFlags = behavior;

    D3DPRESENT_PARAMETERS params = *pp;
    if (!CreateContext(params)) return false;
    QueryGLCaps();

    memset(m_boundTex, 0, sizeof m_boundTex);
    memset(m_boundSampler, 0, sizeof m_boundSampler);
    m_applied.Invalidate();

    glGenFramebuffers(1, &m_fbo);
    glGenFramebuffers(1, &m_scratchFbo);
    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);

    // Neutral defaults for all attributes (0,0,0,1) so that unbound inputs read what D3D expects.
    for (int i = 0; i < 16 && i < m_glcaps.maxVertexAttribs; ++i) glVertexAttrib4f(i, 0, 0, 0, 1);

    // 1x1 white textures bound for stages without a texture.
    {
        const uint8_t white[4] = {255, 255, 255, 255};
        const uint8_t white6[6][4] = {{255, 255, 255, 255}, {255, 255, 255, 255}, {255, 255, 255, 255},
                                      {255, 255, 255, 255}, {255, 255, 255, 255}, {255, 255, 255, 255}};
        glGenTextures(3, m_whiteTex);
        glActiveTexture(GL_TEXTURE0 + UPLOAD_UNIT);
        glBindTexture(GL_TEXTURE_2D, m_whiteTex[0]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
        glBindTexture(GL_TEXTURE_CUBE_MAP, m_whiteTex[1]);
        for (int f = 0; f < 6; ++f)
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white6[f]);
        glBindTexture(GL_TEXTURE_3D, m_whiteTex[2]);
        glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, 1, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
        m_activeUnit = UPLOAD_UNIT;
        for (int i = 0; i < 3; ++i) m_boundTex[UPLOAD_UNIT][i] = m_whiteTex[i];
    }

    m_pp = params;
    if (!CreateBackBuffer(params)) return false;
    CreatePresentProgram();
    ResetState();

    // Persist the corrected parameters for the caller.
    *pp = m_pp;
    return true;
}

Device::~Device() {}

void Device::OnZeroRefs()
{
    // Last reference: tear everything down while the GL context still exists.
    for (auto &p : m_programs) delete p.second;
    m_programs.clear();
    for (auto &s : m_vertexShaders) delete s.second;
    m_vertexShaders.clear();
    for (auto &s : m_pixelShaders) delete s.second;
    m_pixelShaders.clear();
    m_curRT = nullptr;
    m_curDS = nullptr;
    m_s = DeviceState();
    DestroyBackBuffer();
    if (m_glContext)
    {
        for (auto &s : m_samplers) glDeleteSamplers(1, &s.second);
        glDeleteFramebuffers(1, &m_fbo);
        glDeleteFramebuffers(1, &m_scratchFbo);
        glDeleteVertexArrays(1, &m_vao);
        glDeleteTextures(3, m_whiteTex);
        if (m_streamVB) glDeleteBuffers(1, &m_streamVB);
        if (m_streamIB) glDeleteBuffers(1, &m_streamIB);
        if (m_presentProgram) glDeleteProgram(m_presentProgram);
        if (m_gammaTex) glDeleteTextures(1, &m_gammaTex);
        emscripten_webgl_make_context_current(0);
        emscripten_webgl_destroy_context(m_glContext);
        m_glContext = 0;
    }
    if (m_d3d) { m_d3d->Release(); m_d3d = nullptr; }
    delete this;
}

//------------------------------------------------------------------------------
// Back buffer
//------------------------------------------------------------------------------
bool Device::CreateBackBuffer(const D3DPRESENT_PARAMETERS &pp)
{
    UINT w = pp.BackBufferWidth, h = pp.BackBufferHeight;
    if (!w || !h)
    {
        int cw = 0, ch = 0;
        emscripten_get_canvas_element_size(GetConfig().canvas.c_str(), &cw, &ch);
        w = cw > 0 ? cw : 640;
        h = ch > 0 ? ch : 480;
    }
    m_pp.BackBufferWidth = w;
    m_pp.BackBufferHeight = h;
    if (m_pp.BackBufferFormat == D3DFMT_UNKNOWN) m_pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    if (m_pp.BackBufferCount == 0) m_pp.BackBufferCount = 1;
    m_pp.MultiSampleType = D3DMULTISAMPLE_NONE;
    emscripten_set_canvas_element_size(GetConfig().canvas.c_str(), (int)w, (int)h);

    glGenTextures(1, &m_bbColor);
    BindForUpload(GL_TEXTURE_2D, m_bbColor);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, w, h);

    // The device owns one reference of its default surfaces.
    m_backBuffer = new Surface(this, Surface::BackBuffer, w, h, m_pp.BackBufferFormat);
    m_backBuffer->DropDeviceRef();
    m_curRT = m_backBuffer;

    if (m_pp.EnableAutoDepthStencil)
    {
        D3DFORMAT zf = m_pp.AutoDepthStencilFormat;
        const FormatInfo *zi = GetFormatInfo(zf);
        if (!zi || !zi->depth) { zf = D3DFMT_D24S8; m_pp.AutoDepthStencilFormat = zf; }
        m_defaultDepth = new Surface(this, Surface::DepthStencil, w, h, zf);
        m_defaultDepth->DropDeviceRef();
        m_curDS = m_defaultDepth;
    }
    m_rtWidth = w;
    m_rtHeight = h;
    m_targetsDirty = true;
    return true;
}

void Device::DestroyBackBuffer()
{
    m_curRT = nullptr;
    m_curDS = nullptr;
    if (m_backBuffer) m_backBuffer->Release();
    if (m_defaultDepth) m_defaultDepth->Release();
    m_backBuffer = nullptr;
    m_defaultDepth = nullptr;
    if (m_bbColor)
    {
        ForgetTexture(m_bbColor);
        glDeleteTextures(1, &m_bbColor);
        m_bbColor = 0;
    }
    m_targetsDirty = true;
}

//------------------------------------------------------------------------------
// Present
//------------------------------------------------------------------------------
void Device::CreatePresentProgram()
{
    static const char *vs =
        "#version 300 es\n"
        "out vec2 uv;\n"
        "void main() {\n"
        "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
        "    uv = vec2(p.x, 1.0 - p.y);\n"
        "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
        "}\n";
    static const char *fs =
        "#version 300 es\nprecision highp float;\n"
        "in vec2 uv;\nuniform sampler2D s;\nuniform sampler2D g;\nuniform float doGamma;\n"
        "out vec4 o;\n"
        "void main() {\n"
        "    vec3 c = texture(s, uv).rgb;\n"
        "    if (doGamma > 0.5) {\n"
        "        c = vec3(texture(g, vec2(c.r * (255.0 / 256.0) + 0.5 / 256.0, 0.5)).r,\n"
        "                 texture(g, vec2(c.g * (255.0 / 256.0) + 0.5 / 256.0, 0.5)).g,\n"
        "                 texture(g, vec2(c.b * (255.0 / 256.0) + 0.5 / 256.0, 0.5)).b);\n"
        "    }\n"
        "    o = vec4(c, 1.0);\n"
        "}\n";
    ProgramKey key = {};
    Program *p = CreateProgram(this, key, vs, fs);
    m_presentProgram = p->id;
    p->id = 0; // keep the GL program, drop the wrapper
    delete p;
    glUseProgram(m_presentProgram);
    glUniform1i(glGetUniformLocation(m_presentProgram, "s"), 0);
    glUniform1i(glGetUniformLocation(m_presentProgram, "g"), 1);
    m_presentGammaLoc = glGetUniformLocation(m_presentProgram, "doGamma");
    m_boundProgram = 0xFFFFFFFF;
    glGenVertexArrays(1, &m_presentVao);
    memset(&m_gamma, 0, sizeof m_gamma);
    for (int i = 0; i < 256; ++i) m_gamma.red[i] = m_gamma.green[i] = m_gamma.blue[i] = (WORD)(i * 257);
}

void Device::PresentToCanvas()
{
    // Gamma LUT texture.
    if (m_gammaActive && m_gammaDirty)
    {
        uint8_t lut[256 * 4];
        for (int i = 0; i < 256; ++i)
        {
            lut[i * 4 + 0] = (uint8_t)(m_gamma.red[i] >> 8);
            lut[i * 4 + 1] = (uint8_t)(m_gamma.green[i] >> 8);
            lut[i * 4 + 2] = (uint8_t)(m_gamma.blue[i] >> 8);
            lut[i * 4 + 3] = 255;
        }
        if (!m_gammaTex) glGenTextures(1, &m_gammaTex);
        glActiveTexture(GL_TEXTURE0 + UPLOAD_UNIT);
        glBindTexture(GL_TEXTURE_2D, m_gammaTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, lut);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_boundTex[UPLOAD_UNIT][0] = m_gammaTex;
        m_gammaDirty = false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_pp.BackBufferWidth, m_pp.BackBufferHeight);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glBindVertexArray(m_presentVao);
    glUseProgram(m_presentProgram);
    glUniform1f(m_presentGammaLoc, m_gammaActive ? 1.0f : 0.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_bbColor);
    glBindSampler(0, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (m_gammaActive)
    {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_gammaTex);
        glBindSampler(1, 0);
    }
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // Everything the main draw path caches has been disturbed.
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    memset(m_boundTex, 0, sizeof m_boundTex);
    memset(m_boundSampler, 0, sizeof m_boundSampler);
    m_activeUnit = 1;
    glBindVertexArray(m_vao);
    m_boundProgram = 0xFFFFFFFF;
    m_boundArrayBuffer = 0xFFFFFFFF;
    m_attribSig = 0;
    m_pipelineValid = false;
    m_targetsDirty = true;
    m_applied.Invalidate();

    if (GetConfig().presentMode == WEBD3D8_PRESENT_EXPLICIT)
        emscripten_webgl_commit_frame();
}

HRESULT Device::Present(const RECT *, const RECT *, HWND, const WebD3D8_RGNDATA *)
{
    if (m_contextLost) return D3DERR_DEVICELOST;
    PresentToCanvas();
    return D3D_OK;
}

void Device::SetGammaRamp(DWORD, const D3DGAMMARAMP *pRamp)
{
    if (!pRamp) return;
    m_gamma = *pRamp;
    bool identity = true;
    for (int i = 0; i < 256 && identity; ++i)
    {
        const int id = i * 257;
        if (std::abs((int)m_gamma.red[i] - id) > 256 || std::abs((int)m_gamma.green[i] - id) > 256 ||
            std::abs((int)m_gamma.blue[i] - id) > 256)
            identity = false;
    }
    m_gammaActive = !identity;
    m_gammaDirty = true;
}

void Device::GetGammaRamp(D3DGAMMARAMP *pRamp)
{
    if (pRamp) *pRamp = m_gamma;
}

//------------------------------------------------------------------------------
// Device level queries
//------------------------------------------------------------------------------
HRESULT Device::TestCooperativeLevel()
{
    if (emscripten_is_webgl_context_lost(m_glContext))
    {
        m_contextLost = true;
        return D3DERR_DEVICELOST;
    }
    return D3D_OK;
}

UINT Device::GetAvailableTextureMem() { return 256u * 1024u * 1024u; }
HRESULT Device::ResourceManagerDiscardBytes(DWORD) { return D3D_OK; }

HRESULT Device::GetDirect3D(IDirect3D8 **pp)
{
    if (!pp) return D3DERR_INVALIDCALL;
    *pp = m_d3d;
    m_d3d->AddRef();
    return D3D_OK;
}

HRESULT Device::GetDeviceCaps(D3DCAPS8 *caps)
{
    if (!caps) return D3DERR_INVALIDCALL;
    FillDeviceCaps(caps, &m_glcaps);
    return D3D_OK;
}

HRESULT Device::GetDisplayMode(D3DDISPLAYMODE *m)
{
    if (!m) return D3DERR_INVALIDCALL;
    m->Width = m_pp.BackBufferWidth;
    m->Height = m_pp.BackBufferHeight;
    m->RefreshRate = 60;
    m->Format = (m_pp.BackBufferFormat == D3DFMT_R5G6B5 || m_pp.BackBufferFormat == D3DFMT_X1R5G5B5) ? m_pp.BackBufferFormat : D3DFMT_X8R8G8B8;
    return D3D_OK;
}

HRESULT Device::GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS *p)
{
    if (!p) return D3DERR_INVALIDCALL;
    *p = m_creation;
    return D3D_OK;
}

HRESULT Device::SetCursorProperties(UINT, UINT, IDirect3DSurface8 *)
{
    // The cursor is drawn by the browser / the game (RM_WINDOWS, RM_W3D, RM_POLYGON modes).
    return D3D_OK;
}

void Device::SetCursorPosition(UINT, UINT, DWORD) {}

BOOL Device::ShowCursor(BOOL bShow)
{
    BOOL old = m_cursorVisible;
    m_cursorVisible = bShow;
    return old;
}

HRESULT Device::CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS *, IDirect3DSwapChain8 **pp)
{
    if (pp) *pp = nullptr;
    WEBD3D8_UNSUPPORTED("CreateAdditionalSwapChain");
    return D3DERR_NOTAVAILABLE;
}

HRESULT Device::GetRasterStatus(D3DRASTER_STATUS *s)
{
    if (!s) return D3DERR_INVALIDCALL;
    s->InVBlank = FALSE;
    s->ScanLine = 0;
    return D3D_OK;
}

HRESULT Device::Reset(D3DPRESENT_PARAMETERS *pp)
{
    if (!pp) return D3DERR_INVALIDCALL;
    if (emscripten_is_webgl_context_lost(m_glContext)) return D3DERR_DEVICELOST;
    D3DPRESENT_PARAMETERS params = *pp;
    m_curRT = nullptr;
    m_curDS = nullptr;
    DestroyBackBuffer();
    m_pp = params;
    if (!CreateBackBuffer(params)) return D3DERR_INVALIDCALL;
    ResetState();
    *pp = m_pp;
    return D3D_OK;
}

HRESULT Device::GetBackBuffer(UINT index, D3DBACKBUFFER_TYPE, IDirect3DSurface8 **pp)
{
    if (!pp || index != 0) return D3DERR_INVALIDCALL;
    *pp = m_backBuffer;
    m_backBuffer->AddRef();
    return D3D_OK;
}

HRESULT Device::GetFrontBuffer(IDirect3DSurface8 *dest)
{
    if (!dest) return D3DERR_INVALIDCALL;
    Surface *dst = static_cast<Surface *>(dest);
    UINT w = Min(dst->Width(), m_backBuffer->Width()), h = Min(dst->Height(), m_backBuffer->Height());
    if (dst->Format() != D3DFMT_A8R8G8B8) return D3DERR_INVALIDCALL;
    std::vector<uint8_t> tmp((size_t)w * h * 4);
    RECT rc = {0, 0, (LONG)w, (LONG)h};
    if (!m_backBuffer->ReadPixelsTo(rc, D3DFMT_A8R8G8B8, tmp.data(), w * 4)) return D3DERR_INVALIDCALL;
    for (size_t i = 0; i < (size_t)w * h; ++i) tmp[i * 4 + 3] = 255;
    return dst->WritePixelsFrom(rc, D3DFMT_A8R8G8B8, tmp.data(), w * 4) ? D3D_OK : D3DERR_INVALIDCALL;
}

HRESULT Device::BeginScene()
{
    if (m_inScene) return D3DERR_INVALIDCALL;
    m_inScene = true;
    return D3D_OK;
}

HRESULT Device::EndScene()
{
    if (!m_inScene) return D3DERR_INVALIDCALL;
    m_inScene = false;
    return D3D_OK;
}

HRESULT Device::ValidateDevice(DWORD *pNumPasses)
{
    if (pNumPasses) *pNumPasses = 1;
    return D3D_OK;
}

HRESULT Device::GetInfo(DWORD, void *, DWORD) { return D3DERR_INVALIDCALL; }

HRESULT Device::SetPaletteEntries(UINT, const PALETTEENTRY *) { return D3DERR_INVALIDCALL; }
HRESULT Device::GetPaletteEntries(UINT, PALETTEENTRY *) { return D3DERR_INVALIDCALL; }
HRESULT Device::SetCurrentTexturePalette(UINT) { return D3DERR_INVALIDCALL; }
HRESULT Device::GetCurrentTexturePalette(UINT *) { return D3DERR_INVALIDCALL; }
HRESULT Device::SetClipStatus(const D3DCLIPSTATUS8 *) { return D3D_OK; }
HRESULT Device::GetClipStatus(D3DCLIPSTATUS8 *s) { if (s) memset(s, 0, sizeof *s); return D3D_OK; }
HRESULT Device::DrawRectPatch(UINT, const float *, const D3DRECTPATCH_INFO *) { return D3DERR_INVALIDCALL; }
HRESULT Device::DrawTriPatch(UINT, const float *, const D3DTRIPATCH_INFO *) { return D3DERR_INVALIDCALL; }
HRESULT Device::DeletePatch(UINT) { return D3DERR_INVALIDCALL; }

//------------------------------------------------------------------------------
// Resource creation
//------------------------------------------------------------------------------
HRESULT Device::CreateTexture(UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DTexture8 **pp)
{
    if (!pp) return D3DERR_INVALIDCALL;
    *pp = nullptr;
    const FormatInfo *info = GetFormatInfo(fmt);
    if (!info || !info->texture) return D3DERR_INVALIDCALL;
    Texture2D *t = new Texture2D(this, w, h, levels, usage, fmt, pool);
    if (!t->Valid())
    {
        t->Release();
        return D3DERR_INVALIDCALL;
    }
    *pp = t;
    return D3D_OK;
}

HRESULT Device::CreateCubeTexture(UINT edge, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DCubeTexture8 **pp)
{
    if (!pp) return D3DERR_INVALIDCALL;
    *pp = nullptr;
    CubeTexture *t = new CubeTexture(this, edge, levels, usage, fmt, pool);
    if (!t->Valid())
    {
        t->Release();
        return D3DERR_INVALIDCALL;
    }
    *pp = t;
    return D3D_OK;
}

HRESULT Device::CreateVolumeTexture(UINT w, UINT h, UINT d, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DVolumeTexture8 **pp)
{
    if (!pp) return D3DERR_INVALIDCALL;
    *pp = nullptr;
    VolumeTexture *t = new VolumeTexture(this, w, h, d, levels, usage, fmt, pool);
    if (!t->Valid())
    {
        t->Release();
        return D3DERR_INVALIDCALL;
    }
    *pp = t;
    return D3D_OK;
}

HRESULT Device::CreateVertexBuffer(UINT length, DWORD usage, DWORD fvf, D3DPOOL pool, IDirect3DVertexBuffer8 **pp)
{
    if (!pp || length == 0) return D3DERR_INVALIDCALL;
    *pp = new VertexBuffer(this, length, usage, fvf, pool);
    return D3D_OK;
}

HRESULT Device::CreateIndexBuffer(UINT length, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DIndexBuffer8 **pp)
{
    if (!pp || length == 0) return D3DERR_INVALIDCALL;
    if (fmt != D3DFMT_INDEX16 && fmt != D3DFMT_INDEX32) return D3DERR_INVALIDCALL;
    *pp = new IndexBuffer(this, length, usage, fmt, pool);
    return D3D_OK;
}

HRESULT Device::CreateRenderTarget(UINT w, UINT h, D3DFORMAT fmt, D3DMULTISAMPLE_TYPE, BOOL, IDirect3DSurface8 **pp)
{
    if (!pp) return D3DERR_INVALIDCALL;
    const FormatInfo *info = GetFormatInfo(fmt);
    if (!info || !info->renderTarget) return D3DERR_INVALIDCALL;
    *pp = new Surface(this, Surface::RenderTarget, w, h, fmt);
    return D3D_OK;
}

HRESULT Device::CreateDepthStencilSurface(UINT w, UINT h, D3DFORMAT fmt, D3DMULTISAMPLE_TYPE, IDirect3DSurface8 **pp)
{
    if (!pp) return D3DERR_INVALIDCALL;
    const FormatInfo *info = GetFormatInfo(fmt);
    if (!info || !info->depth) return D3DERR_INVALIDCALL;
    *pp = new Surface(this, Surface::DepthStencil, w, h, fmt);
    return D3D_OK;
}

HRESULT Device::CreateImageSurface(UINT w, UINT h, D3DFORMAT fmt, IDirect3DSurface8 **pp)
{
    if (!pp || !w || !h) return D3DERR_INVALIDCALL;
    const FormatInfo *info = GetFormatInfo(fmt);
    if (!info || info->depth) return D3DERR_INVALIDCALL;
    *pp = new Surface(this, Surface::Image, w, h, fmt);
    return D3D_OK;
}

HRESULT Device::CopyRects(IDirect3DSurface8 *pSrc, const RECT *srcRects, UINT cRects, IDirect3DSurface8 *pDst, const POINT *dstPoints)
{
    if (!pSrc || !pDst) return D3DERR_INVALIDCALL;
    Surface *src = static_cast<Surface *>(pSrc), *dst = static_cast<Surface *>(pDst);
    RECT whole = {0, 0, (LONG)src->Width(), (LONG)src->Height()};
    UINT n = cRects ? cRects : 1;
    for (UINT i = 0; i < n; ++i)
    {
        RECT r = (cRects && srcRects) ? srcRects[i] : whole;
        POINT p = (cRects && dstPoints) ? dstPoints[i] : POINT{r.left, r.top};
        if (!(cRects && srcRects)) { p.x = 0; p.y = 0; }
        r.left = Clamp<LONG>(r.left, 0, src->Width());
        r.right = Clamp<LONG>(r.right, 0, src->Width());
        r.top = Clamp<LONG>(r.top, 0, src->Height());
        r.bottom = Clamp<LONG>(r.bottom, 0, src->Height());
        LONG w = r.right - r.left, h = r.bottom - r.top;
        if (w <= 0 || h <= 0) continue;
        // Clip against the destination.
        w = Min<LONG>(w, (LONG)dst->Width() - p.x);
        h = Min<LONG>(h, (LONG)dst->Height() - p.y);
        if (w <= 0 || h <= 0 || p.x < 0 || p.y < 0) return D3DERR_INVALIDCALL;
        r.right = r.left + w; r.bottom = r.top + h;
        RECT dr = {p.x, p.y, p.x + w, p.y + h};

        const FormatInfo *fi = GetFormatInfo(dst->Format());
        uint32_t pitch = FormatPitch(dst->Format(), w);
        std::vector<uint8_t> tmp((size_t)pitch * FormatRows(dst->Format(), h));
        if (fi && fi->blockBytes) { r.left &= ~3; r.top &= ~3; }
        if (!src->ReadPixelsTo(r, dst->Format(), tmp.data(), pitch)) return D3DERR_INVALIDCALL;
        if (!dst->WritePixelsFrom(dr, dst->Format(), tmp.data(), pitch)) return D3DERR_INVALIDCALL;
    }
    return D3D_OK;
}

HRESULT Device::UpdateTexture(IDirect3DBaseTexture8 *pSrc, IDirect3DBaseTexture8 *pDst)
{
    if (!pSrc || !pDst) return D3DERR_INVALIDCALL;
    if (pSrc->GetType() != pDst->GetType()) return D3DERR_INVALIDCALL;
    TextureBase *src, *dst;
    switch (pSrc->GetType())
    {
    case D3DRTYPE_TEXTURE: src = static_cast<Texture2D *>(static_cast<IDirect3DTexture8 *>(pSrc)); dst = static_cast<Texture2D *>(static_cast<IDirect3DTexture8 *>(pDst)); break;
    case D3DRTYPE_CUBETEXTURE: src = static_cast<CubeTexture *>(static_cast<IDirect3DCubeTexture8 *>(pSrc)); dst = static_cast<CubeTexture *>(static_cast<IDirect3DCubeTexture8 *>(pDst)); break;
    case D3DRTYPE_VOLUMETEXTURE:
    {
        // Volumes: copy the shadow level by level.
        VolumeTexture *s = static_cast<VolumeTexture *>(static_cast<IDirect3DVolumeTexture8 *>(pSrc));
        VolumeTexture *d = static_cast<VolumeTexture *>(static_cast<IDirect3DVolumeTexture8 *>(pDst));
        if (s->m_format != d->m_format) return D3DERR_INVALIDCALL;
        d->EnsureGL();
        UINT skip = s->m_levelCount > d->m_levelCount ? s->m_levelCount - d->m_levelCount : 0;
        for (UINT l = 0; l < d->m_levelCount && l + skip < s->m_levelCount; ++l)
        {
            LevelData &sl = s->LevelAt(0, l + skip), &dl = d->LevelAt(0, l);
            if (sl.shadow.empty() || sl.slicePitch * sl.depth != dl.slicePitch * dl.depth) continue;
            dl.shadow = sl.shadow;
            dl.shadowValid = true;
            d->UploadWhole(0, l);
        }
        return D3D_OK;
    }
    default: return D3DERR_INVALIDCALL;
    }
    if (src->m_format != dst->m_format) return D3DERR_INVALIDCALL;
    UINT skip = src->m_levelCount > dst->m_levelCount ? src->m_levelCount - dst->m_levelCount : 0;
    for (UINT f = 0; f < dst->m_faces; ++f)
        for (UINT l = 0; l < dst->m_levelCount && l + skip < src->m_levelCount; ++l)
        {
            LevelData &sl = src->LevelAt(f, l + skip);
            LevelData &dl = dst->LevelAt(f, l);
            if (sl.width != dl.width || sl.height != dl.height || sl.shadow.empty()) continue;
            RECT rc = {0, 0, (LONG)dl.width, (LONG)dl.height};
            dst->WriteRect(f, l, rc, sl.shadow.data(), sl.pitch);
        }
    return D3D_OK;
}

//------------------------------------------------------------------------------
// Render targets
//------------------------------------------------------------------------------
HRESULT Device::SetRenderTarget(IDirect3DSurface8 *rt, IDirect3DSurface8 *zs)
{
    if (rt)
    {
        Surface *s = static_cast<Surface *>(rt);
        if (!s->IsRenderable()) return D3DERR_INVALIDCALL;
        m_curRT = rt;
        m_s.viewport.X = 0; m_s.viewport.Y = 0;
        m_s.viewport.Width = s->Width(); m_s.viewport.Height = s->Height();
        m_s.viewport.MinZ = 0.0f; m_s.viewport.MaxZ = 1.0f;
        m_rtWidth = s->Width(); m_rtHeight = s->Height();
        ++m_verTransform;
    }
    if (zs)
    {
        Surface *s = static_cast<Surface *>(zs);
        if (s->GetKind() != Surface::DepthStencil) return D3DERR_INVALIDCALL;
        m_curDS = zs;
    }
    m_targetsDirty = true;
    return D3D_OK;
}

HRESULT Device::GetRenderTarget(IDirect3DSurface8 **pp)
{
    if (!pp) return D3DERR_INVALIDCALL;
    *pp = m_curRT.get();
    if (*pp) (*pp)->AddRef();
    return *pp ? D3D_OK : D3DERR_NOTFOUND;
}

HRESULT Device::GetDepthStencilSurface(IDirect3DSurface8 **pp)
{
    if (!pp) return D3DERR_INVALIDCALL;
    *pp = m_curDS.get();
    if (*pp) (*pp)->AddRef();
    return *pp ? D3D_OK : D3DERR_NOTFOUND;
}

void Device::ApplyRenderTargets()
{
    if (!m_targetsDirty) return;
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    Surface *rt = static_cast<Surface *>(m_curRT.get());
    if (rt) rt->AttachColor();
    bool stencil = false;
    Surface *ds = static_cast<Surface *>(m_curDS.get());
    if (ds)
        ds->AttachDepth(stencil);
    else
    {
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
    }
    if (GetConfig().debug)
    {
        GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (st != GL_FRAMEBUFFER_COMPLETE) Log("framebuffer incomplete: 0x%x", st);
    }
    m_targetsDirty = false;
}

//------------------------------------------------------------------------------
// GL helpers for the resources
//------------------------------------------------------------------------------
void Device::BindForUpload(GLenum target, GLuint tex)
{
    if (m_activeUnit != UPLOAD_UNIT)
    {
        glActiveTexture(GL_TEXTURE0 + UPLOAD_UNIT);
        m_activeUnit = UPLOAD_UNIT;
    }
    int slot = target == GL_TEXTURE_2D ? 0 : (target == GL_TEXTURE_CUBE_MAP ? 1 : 2);
    if (m_boundTex[UPLOAD_UNIT][slot] != tex)
    {
        glBindTexture(target, tex);
        m_boundTex[UPLOAD_UNIT][slot] = tex;
    }
}

void Device::ForgetTexture(GLuint tex)
{
    // Deleting a texture unbinds it everywhere; mirror that in the cache.
    for (auto &unit : m_boundTex)
        for (GLuint &t : unit)
            if (t == tex) t = 0;
    m_targetsDirty = true;
}

void Device::ForgetBuffer(GLuint buf)
{
    if (m_boundArrayBuffer == buf) m_boundArrayBuffer = 0xFFFFFFFF;
    if (m_boundElementBuffer == buf) m_boundElementBuffer = 0xFFFFFFFF;
    m_attribSig = 0;
}

bool Device::ReadTextureLevel(GLuint tex, GLenum target, GLenum faceTarget, int level, uint32_t, uint32_t,
                              const RECT &rc, uint8_t *rgbaOut)
{
    if (target == GL_TEXTURE_3D) return false;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_scratchFbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, faceTarget, tex, level);
    bool ok = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (ok)
    {
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glReadPixels(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, GL_RGBA, GL_UNSIGNED_BYTE, rgbaOut);
    }
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, faceTarget, 0, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    return ok;
}

bool Device::ReadRenderbuffer(GLuint rb, uint32_t, uint32_t, const RECT &rc, uint8_t *rgbaOut)
{
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_scratchFbo);
    glFramebufferRenderbuffer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
    bool ok = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (ok)
    {
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glReadPixels(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, GL_RGBA, GL_UNSIGNED_BYTE, rgbaOut);
    }
    glFramebufferRenderbuffer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    return ok;
}

} // namespace webd3d8
