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
** WebAssembly port: draw calls, clear and the translation of the D3D device
** state to GL state (programs, uniforms, textures, vertex attributes,
** blend/depth/stencil/cull state).
*/
#include "d3d8_internal.h"
#include "program.h"
#include "resources.h"
#include "shader.h"

namespace webd3d8 {

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif

//------------------------------------------------------------------------------
// Enum mapping
//------------------------------------------------------------------------------
static GLenum MapCmp(DWORD f)
{
    switch (f)
    {
    case D3DCMP_NEVER: return GL_NEVER;
    case D3DCMP_LESS: return GL_LESS;
    case D3DCMP_EQUAL: return GL_EQUAL;
    case D3DCMP_LESSEQUAL: return GL_LEQUAL;
    case D3DCMP_GREATER: return GL_GREATER;
    case D3DCMP_NOTEQUAL: return GL_NOTEQUAL;
    case D3DCMP_GREATEREQUAL: return GL_GEQUAL;
    default: return GL_ALWAYS;
    }
}

static GLenum MapBlend(DWORD b, bool isSrc)
{
    switch (b)
    {
    case D3DBLEND_ZERO: return GL_ZERO;
    case D3DBLEND_ONE: return GL_ONE;
    case D3DBLEND_SRCCOLOR: return GL_SRC_COLOR;
    case D3DBLEND_INVSRCCOLOR: return GL_ONE_MINUS_SRC_COLOR;
    case D3DBLEND_SRCALPHA: return GL_SRC_ALPHA;
    case D3DBLEND_INVSRCALPHA: return GL_ONE_MINUS_SRC_ALPHA;
    case D3DBLEND_DESTALPHA: return GL_DST_ALPHA;
    case D3DBLEND_INVDESTALPHA: return GL_ONE_MINUS_DST_ALPHA;
    case D3DBLEND_DESTCOLOR: return GL_DST_COLOR;
    case D3DBLEND_INVDESTCOLOR: return GL_ONE_MINUS_DST_COLOR;
    case D3DBLEND_SRCALPHASAT: return GL_SRC_ALPHA_SATURATE;
    case D3DBLEND_BOTHSRCALPHA: return isSrc ? GL_SRC_ALPHA : GL_ONE_MINUS_SRC_ALPHA;
    case D3DBLEND_BOTHINVSRCALPHA: return isSrc ? GL_ONE_MINUS_SRC_ALPHA : GL_SRC_ALPHA;
    default: return isSrc ? GL_ONE : GL_ZERO;
    }
}

static GLenum MapBlendOp(DWORD op)
{
    switch (op)
    {
    case D3DBLENDOP_SUBTRACT: return GL_FUNC_SUBTRACT;
    case D3DBLENDOP_REVSUBTRACT: return GL_FUNC_REVERSE_SUBTRACT;
    case D3DBLENDOP_MIN: return GL_MIN;
    case D3DBLENDOP_MAX: return GL_MAX;
    default: return GL_FUNC_ADD;
    }
}

static GLenum MapStencilOp(DWORD op)
{
    switch (op)
    {
    case D3DSTENCILOP_ZERO: return GL_ZERO;
    case D3DSTENCILOP_REPLACE: return GL_REPLACE;
    case D3DSTENCILOP_INCRSAT: return GL_INCR;
    case D3DSTENCILOP_DECRSAT: return GL_DECR;
    case D3DSTENCILOP_INVERT: return GL_INVERT;
    case D3DSTENCILOP_INCR: return GL_INCR_WRAP;
    case D3DSTENCILOP_DECR: return GL_DECR_WRAP;
    default: return GL_KEEP;
    }
}

static GLenum MapAddress(DWORD a)
{
    switch (a)
    {
    case D3DTADDRESS_WRAP: return GL_REPEAT;
    case D3DTADDRESS_MIRROR: case D3DTADDRESS_MIRRORONCE: return GL_MIRRORED_REPEAT;
    default: return GL_CLAMP_TO_EDGE; // CLAMP, BORDER (no border color in WebGL)
    }
}

/// Debug aid: reports the first GL error after a stage of the draw setup.
#define GL_STAGE_CHECK(stage)                                                              \
    do {                                                                                   \
        if (GetConfig().debug) {                                                           \
            GLenum e_ = glGetError();                                                      \
            for (GLenum x_ = e_; x_ != GL_NO_ERROR; x_ = glGetError()) Log("  (drain 0x%x)", x_); \
            if (e_ != GL_NO_ERROR) Log("GL error 0x%x after %s (draw #%u, fvf 0x%x, rt %ux%u)", e_, stage, m_drawCounter, (unsigned)m_s.vertexShader, m_rtWidth, m_rtHeight);               \
        }                                                                                  \
    } while (0)

static inline void ColorToVec4(DWORD c, float *out)
{
    out[0] = ((c >> 16) & 255) / 255.0f;
    out[1] = ((c >> 8) & 255) / 255.0f;
    out[2] = (c & 255) / 255.0f;
    out[3] = ((c >> 24) & 255) / 255.0f;
}

//------------------------------------------------------------------------------
// Pipeline state
//------------------------------------------------------------------------------
PipelineState Device::DerivePipeline() const
{
    PipelineState p;
    memset(&p, 0, sizeof p);
    const DWORD *rs = m_s.rs;
    const Surface *ds = static_cast<const Surface *>(m_curDS.get());
    const FormatInfo *dsInfo = ds ? GetFormatInfo(ds->Format()) : nullptr;

    p.depthTest = ds && rs[D3DRS_ZENABLE] != D3DZB_FALSE;
    p.depthFunc = MapCmp(rs[D3DRS_ZFUNC]);
    p.depthMask = ds && rs[D3DRS_ZWRITEENABLE];
    p.depthNear = m_s.viewport.MinZ;
    p.depthFar = m_s.viewport.MaxZ;

    p.blend = rs[D3DRS_ALPHABLENDENABLE] != 0;
    p.srcBlend = MapBlend(rs[D3DRS_SRCBLEND], true);
    p.dstBlend = MapBlend(rs[D3DRS_DESTBLEND], false);
    p.blendOp = MapBlendOp(rs[D3DRS_BLENDOP]);

    // See the winding discussion in device.h: the framebuffer is flipped, so
    // D3D's default (cull counter-clockwise) keeps GL's default front face.
    switch (rs[D3DRS_CULLMODE])
    {
    case D3DCULL_NONE: p.cull = false; p.frontFace = GL_CCW; break;
    case D3DCULL_CW: p.cull = true; p.frontFace = GL_CW; break;
    default: p.cull = true; p.frontFace = GL_CCW; break;
    }

    p.stencilTest = dsInfo && dsInfo->stencil && rs[D3DRS_STENCILENABLE];
    p.stencilFunc = MapCmp(rs[D3DRS_STENCILFUNC]);
    p.stencilRef = (GLint)(rs[D3DRS_STENCILREF] & 0xFF);
    p.stencilMask = rs[D3DRS_STENCILMASK] & 0xFF;
    p.stencilWriteMask = rs[D3DRS_STENCILWRITEMASK] & 0xFF;
    p.stencilFail = MapStencilOp(rs[D3DRS_STENCILFAIL]);
    p.stencilZFail = MapStencilOp(rs[D3DRS_STENCILZFAIL]);
    p.stencilPass = MapStencilOp(rs[D3DRS_STENCILPASS]);

    p.colorMask = (uint8_t)(rs[D3DRS_COLORWRITEENABLE] & 0xF);

    const int zbias = (int)rs[D3DRS_ZBIAS];
    p.polyOffset = zbias != 0;
    p.polyFactor = -(float)zbias;
    p.polyUnits = -(float)zbias * 2.0f;

    p.vp[0] = (GLint)m_s.viewport.X;
    p.vp[1] = (GLint)m_s.viewport.Y;
    p.vp[2] = (GLint)m_s.viewport.Width;
    p.vp[3] = (GLint)m_s.viewport.Height;
    p.scissor = false;
    p.sc[0] = p.sc[1] = p.sc[2] = p.sc[3] = 0;
    return p;
}

void Device::ApplyPipeline()
{
    PipelineState want = DerivePipeline();
    PipelineState &have = m_applied;
    const bool all = !m_pipelineValid;
    auto enable = [](GLenum cap, bool on) { if (on) glEnable(cap); else glDisable(cap); };

    if (all || want.blend != have.blend) enable(GL_BLEND, want.blend);
    if (all || want.srcBlend != have.srcBlend || want.dstBlend != have.dstBlend) glBlendFunc(want.srcBlend, want.dstBlend);
    if (all || want.blendOp != have.blendOp) glBlendEquation(want.blendOp);
    if (all || want.depthTest != have.depthTest) enable(GL_DEPTH_TEST, want.depthTest);
    if (all || want.depthFunc != have.depthFunc) glDepthFunc(want.depthFunc);
    if (all || want.depthMask != have.depthMask) glDepthMask(want.depthMask ? GL_TRUE : GL_FALSE);
    if (all || want.depthNear != have.depthNear || want.depthFar != have.depthFar) glDepthRangef(want.depthNear, want.depthFar);
    if (all || want.cull != have.cull) enable(GL_CULL_FACE, want.cull);
    if (all || want.frontFace != have.frontFace) glFrontFace(want.frontFace);
    if (all) glCullFace(GL_BACK);
    if (all || want.stencilTest != have.stencilTest) enable(GL_STENCIL_TEST, want.stencilTest);
    if (all || want.stencilFunc != have.stencilFunc || want.stencilRef != have.stencilRef || want.stencilMask != have.stencilMask)
        glStencilFunc(want.stencilFunc, want.stencilRef, want.stencilMask);
    if (all || want.stencilFail != have.stencilFail || want.stencilZFail != have.stencilZFail || want.stencilPass != have.stencilPass)
        glStencilOp(want.stencilFail, want.stencilZFail, want.stencilPass);
    if (all || want.stencilWriteMask != have.stencilWriteMask) glStencilMask(want.stencilWriteMask);
    if (all || want.colorMask != have.colorMask)
        glColorMask((want.colorMask & 1) != 0, (want.colorMask & 2) != 0, (want.colorMask & 4) != 0, (want.colorMask & 8) != 0);
    if (all || want.polyOffset != have.polyOffset) enable(GL_POLYGON_OFFSET_FILL, want.polyOffset);
    if (all || want.polyFactor != have.polyFactor || want.polyUnits != have.polyUnits) glPolygonOffset(want.polyFactor, want.polyUnits);
    if (all || memcmp(want.vp, have.vp, sizeof want.vp) != 0) glViewport(want.vp[0], want.vp[1], want.vp[2], want.vp[3]);
    if (all || want.scissor != have.scissor) enable(GL_SCISSOR_TEST, want.scissor);

    have = want;
    m_pipelineValid = true;
}

//------------------------------------------------------------------------------
// Program selection
//------------------------------------------------------------------------------
static bool LayoutHas(const VertexLayout *l, int reg)
{
    for (int i = 0; i < l->count; ++i) if (l->elems[i].reg == reg) return true;
    return false;
}

static TextureBase *AsTextureBase(IDirect3DBaseTexture8 *t)
{
    if (!t) return nullptr;
    switch (t->GetType())
    {
    case D3DRTYPE_TEXTURE: return static_cast<Texture2D *>(static_cast<IDirect3DTexture8 *>(t));
    case D3DRTYPE_CUBETEXTURE: return static_cast<CubeTexture *>(static_cast<IDirect3DCubeTexture8 *>(t));
    case D3DRTYPE_VOLUMETEXTURE: return static_cast<VolumeTexture *>(static_cast<IDirect3DVolumeTexture8 *>(t));
    default: return nullptr;
    }
}

static uint8_t TexTypeOf(TextureBase *tb)
{
    if (!tb || !tb->Valid()) return 0;
    return tb->m_target == GL_TEXTURE_2D ? 1 : (tb->m_target == GL_TEXTURE_CUBE_MAP ? 2 : 3);
}

void Device::BuildProgramKey(ProgramKey &key)
{
    memset(&key, 0, sizeof key);
    const DWORD *rs = m_s.rs;
    const DWORD vsh = m_s.vertexShader;
    VertexShaderObject *vso = (vsh & 1) ? FindVertexShader(vsh) : nullptr;
    const bool customVS = vso && vso->hasFunction;
    PixelShaderObject *pso = m_s.pixelShader ? FindPixelShader(m_s.pixelShader) : nullptr;

    key.vsHandle = customVS ? vsh : 0;
    key.psHandle = pso ? m_s.pixelShader : 0;

    const VertexLayout *layout = vso ? &vso->layout : LayoutFromFVF(vsh);
    if (!customVS)
    {
        key.rhw = !vso && (vsh & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW;
        key.hasNormal = LayoutHas(layout, D3DVSDE_NORMAL);
        key.hasDiffuse = LayoutHas(layout, D3DVSDE_DIFFUSE);
        key.hasSpecular = LayoutHas(layout, D3DVSDE_SPECULAR);
        key.hasPSize = LayoutHas(layout, D3DVSDE_PSIZE);
        key.lighting = rs[D3DRS_LIGHTING] && !key.rhw;
        key.specularEnable = rs[D3DRS_SPECULARENABLE] != 0;
        key.localViewer = rs[D3DRS_LOCALVIEWER] != 0;
        key.normalize = rs[D3DRS_NORMALIZENORMALS] != 0;
        key.colorVertex = rs[D3DRS_COLORVERTEX] != 0;
        key.srcDiffuse = (uint8_t)rs[D3DRS_DIFFUSEMATERIALSOURCE];
        key.srcSpecular = (uint8_t)rs[D3DRS_SPECULARMATERIALSOURCE];
        key.srcAmbient = (uint8_t)rs[D3DRS_AMBIENTMATERIALSOURCE];
        key.srcEmissive = (uint8_t)rs[D3DRS_EMISSIVEMATERIALSOURCE];
        if (key.lighting)
        {
            int n = 0;
            for (const LightState &ls : m_s.lights)
            {
                if (!ls.defined || !ls.enabled) continue;
                if (n >= MAX_SHADER_LIGHTS) break;
                key.lightType[n++] = ls.light.Type == D3DLIGHT_DIRECTIONAL ? 0 : (ls.light.Type == D3DLIGHT_POINT ? 1 : 2);
            }
            key.lightCount = (uint8_t)n;
        }
    }
    if (rs[D3DRS_FOGENABLE])
    {
        key.fogEnable = 1;
        key.fogVertexMode = (uint8_t)rs[D3DRS_FOGVERTEXMODE];
        key.fogTableMode = (uint8_t)rs[D3DRS_FOGTABLEMODE];
        key.rangeFog = rs[D3DRS_RANGEFOGENABLE] != 0;
        if (!key.fogVertexMode && !key.fogTableMode) key.fogEnable = 0;
        // Table fog wins over vertex fog.
        if (key.fogTableMode) key.fogVertexMode = 0;
    }
    key.alphaTest = rs[D3DRS_ALPHATESTENABLE] != 0;
    key.alphaFunc = (uint8_t)rs[D3DRS_ALPHAFUNC];
    key.clipMask = (uint8_t)(rs[D3DRS_CLIPPLANEENABLE] & 0x3F);
    key.pointSprite = m_drawingPoints && rs[D3DRS_POINTSPRITEENABLE];
    key.pointScale = rs[D3DRS_POINTSCALEENABLE] != 0;
    key.flatShade = rs[D3DRS_SHADEMODE] == D3DSHADE_FLAT;
    if (!key.alphaTest) key.alphaFunc = 0;

    // Texture stages.
    int count = 0;
    if (pso)
    {
        uint32_t m = pso->textureStageMask;
        while (m) { ++count; m >>= 1; }
    }
    else
    {
        while (count < MAX_STAGES && m_s.tss[count][D3DTSS_COLOROP] != D3DTOP_DISABLE) ++count;
        key.specularEnable = key.specularEnable && true;
    }
    key.stageCount = (uint8_t)count;
    for (int s = 0; s < count; ++s)
    {
        const DWORD *t = m_s.tss[s];
        StageKey &st = key.stage[s];
        TextureBase *tb = AsTextureBase(m_s.textures[s].get());
        if (tb && tb->m_pool == D3DPOOL_SYSTEMMEM) tb->EnsureGL();
        st.texType = TexTypeOf(tb);
        if (tb && tb->m_tex && tb->m_tex == m_attachedColorTex) st.texType = TexTypeOf(tb); // feedback is handled at bind time
        st.colorOp = (uint8_t)t[D3DTSS_COLOROP];
        st.alphaOp = (uint8_t)t[D3DTSS_ALPHAOP];
        st.colorArg0 = (uint8_t)t[D3DTSS_COLORARG0];
        st.colorArg1 = (uint8_t)t[D3DTSS_COLORARG1];
        st.colorArg2 = (uint8_t)t[D3DTSS_COLORARG2];
        st.alphaArg0 = (uint8_t)t[D3DTSS_ALPHAARG0];
        st.alphaArg1 = (uint8_t)t[D3DTSS_ALPHAARG1];
        st.alphaArg2 = (uint8_t)t[D3DTSS_ALPHAARG2];
        st.resultTemp = (t[D3DTSS_RESULTARG] & 0xF) == D3DTA_TEMP;
        const DWORD tci = t[D3DTSS_TEXCOORDINDEX];
        st.coordIndex = (uint8_t)Min<DWORD>(tci & 0xFFFF, customVS ? 3u : 7u);
        st.texGen = (uint8_t)((tci >> 16) & 3);
        const DWORD ttf = t[D3DTSS_TEXTURETRANSFORMFLAGS];
        st.xformCount = (uint8_t)Min<DWORD>(ttf & 0xFF, 4u);
        st.projected = (ttf & D3DTTFF_PROJECTED) != 0;
        st.lodBias = DwordToFloat(t[D3DTSS_MIPMAPLODBIAS]) != 0.0f;
        const bool needsTexture = pso ? ((pso->textureStageMask >> s) & 1) : st.texType != 0;
        if (needsTexture && st.texType)
        {
            if (customVS) key.tcMask |= (uint8_t)(1u << st.coordIndex);
            else key.tcMask |= (uint8_t)(1u << s);
        }
        else if (pso && ((pso->textureStageMask >> s) & 1))
        {
            // Shader reads the coordinates of a stage without a texture (texcoord/texcrd).
            if (customVS) key.tcMask |= (uint8_t)(1u << st.coordIndex);
            else key.tcMask |= (uint8_t)(1u << s);
        }
    }
    if (pso) key.specularEnable = 0; // ps.1.x has no specular add stage
}

bool Device::SelectProgram()
{
    if (m_keyDirty || !m_curProgram || m_curProgramPoints != m_drawingPoints)
    {
        ProgramKey key;
        BuildProgramKey(key);
        std::string k(reinterpret_cast<const char *>(&key), sizeof key);
        auto it = m_programs.find(k);
        Program *prog;
        if (it != m_programs.end())
            prog = it->second;
        else
        {
            VertexShaderObject *vso = key.vsHandle ? FindVertexShader(key.vsHandle) : nullptr;
            PixelShaderObject *pso = key.psHandle ? FindPixelShader(key.psHandle) : nullptr;
            std::string vs = vso ? BuildTranslatedVertexShader(*vso, key) : GenerateFixedFunctionVertexShader(key);
            std::string fs = pso ? pso->Generate(key) : GenerateFixedFunctionFragmentShader(key);
            prog = CreateProgram(this, key, vs, fs);
            m_boundProgram = 0xFFFFFFFF; // creation changed the current program
            m_programs[k] = prog;
        }
        m_curProgram = prog;
        m_curProgramPoints = m_drawingPoints;
        m_keyDirty = false;
    }
    return m_curProgram && m_curProgram->valid;
}

//------------------------------------------------------------------------------
// Uniforms
//------------------------------------------------------------------------------
void Device::UpdateDerivedMatrices()
{
    if (!m_derivedDirty) return;
    m_wv = m_s.world[0] * m_s.view;
    m_wvp = m_wv * m_s.proj;
    NormalMatrix3(m_wv, m_nm);
    m_derivedDirty = false;
}

void Device::UploadUniforms()
{
    Program *p = m_curProgram;
    UpdateDerivedMatrices();
    const ProgramKey &key = p->key;
    const GLint *loc = p->loc;

    if (p->verTransform != m_verTransform)
    {
        p->verTransform = m_verTransform;
        if (loc[U_WVP] >= 0) glUniformMatrix4fv(loc[U_WVP], 1, GL_FALSE, m_wvp.m);
        if (loc[U_WV] >= 0) glUniformMatrix4fv(loc[U_WV], 1, GL_FALSE, m_wv.m);
        if (loc[U_WORLD] >= 0) glUniformMatrix4fv(loc[U_WORLD], 1, GL_FALSE, m_s.world[0].m);
        if (loc[U_NM] >= 0) glUniformMatrix3fv(loc[U_NM], 1, GL_FALSE, m_nm);
        if (loc[U_TM] >= 0)
        {
            float tm[8 * 16];
            for (int i = 0; i < 8; ++i) memcpy(tm + i * 16, m_s.tex[i].m, sizeof(float) * 16);
            glUniformMatrix4fv(loc[U_TM], 8, GL_FALSE, tm);
        }
        const float vw = Max<float>(1.0f, (float)m_s.viewport.Width), vh = Max<float>(1.0f, (float)m_s.viewport.Height);
        if (loc[U_PIX] >= 0) glUniform2f(loc[U_PIX], 1.0f / vw, 1.0f / vh);
        if (loc[U_VP] >= 0) glUniform4f(loc[U_VP], (float)m_s.viewport.X, (float)m_s.viewport.Y, vw, vh);
        if (loc[U_CLIP] >= 0) glUniform4fv(loc[U_CLIP], MAX_CLIP_PLANES, &m_s.clipPlanes[0][0]);
    }

    if (key.lighting && key.lightCount && p->verLights != m_verLights + (m_verTransform << 16))
    {
        p->verLights = m_verLights + (m_verTransform << 16);
        float pos[8][3] = {}, dir[8][3] = {}, diff[8][3] = {}, spec[8][3] = {}, amb[8][3] = {}, att[8][4] = {}, spot[8][4] = {};
        int n = 0;
        const Mat4 &v = m_s.view;
        for (const LightState &ls : m_s.lights)
        {
            if (!ls.defined || !ls.enabled) continue;
            if (n >= key.lightCount) break;
            const D3DLIGHT8 &l = ls.light;
            // World -> view (row vectors).
            pos[n][0] = l.Position.x * v.m[0] + l.Position.y * v.m[4] + l.Position.z * v.m[8] + v.m[12];
            pos[n][1] = l.Position.x * v.m[1] + l.Position.y * v.m[5] + l.Position.z * v.m[9] + v.m[13];
            pos[n][2] = l.Position.x * v.m[2] + l.Position.y * v.m[6] + l.Position.z * v.m[10] + v.m[14];
            float d[3] = {l.Direction.x * v.m[0] + l.Direction.y * v.m[4] + l.Direction.z * v.m[8],
                          l.Direction.x * v.m[1] + l.Direction.y * v.m[5] + l.Direction.z * v.m[9],
                          l.Direction.x * v.m[2] + l.Direction.y * v.m[6] + l.Direction.z * v.m[10]};
            float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (len > 0) { d[0] /= len; d[1] /= len; d[2] /= len; }
            memcpy(dir[n], d, sizeof d);
            diff[n][0] = l.Diffuse.r; diff[n][1] = l.Diffuse.g; diff[n][2] = l.Diffuse.b;
            spec[n][0] = l.Specular.r; spec[n][1] = l.Specular.g; spec[n][2] = l.Specular.b;
            amb[n][0] = l.Ambient.r; amb[n][1] = l.Ambient.g; amb[n][2] = l.Ambient.b;
            att[n][0] = l.Attenuation0; att[n][1] = l.Attenuation1; att[n][2] = l.Attenuation2;
            att[n][3] = Min(l.Range, 3.0e38f);
            const float ct = std::cos(l.Theta * 0.5f), cp = std::cos(l.Phi * 0.5f);
            spot[n][0] = ct; spot[n][1] = cp; spot[n][2] = l.Falloff;
            spot[n][3] = (ct - cp) != 0.0f ? 1.0f / (ct - cp) : 1.0f;
            ++n;
        }
        if (loc[U_LPOS] >= 0) glUniform3fv(loc[U_LPOS], 8, &pos[0][0]);
        if (loc[U_LDIR] >= 0) glUniform3fv(loc[U_LDIR], 8, &dir[0][0]);
        if (loc[U_LDIFF] >= 0) glUniform3fv(loc[U_LDIFF], 8, &diff[0][0]);
        if (loc[U_LSPEC] >= 0) glUniform3fv(loc[U_LSPEC], 8, &spec[0][0]);
        if (loc[U_LAMB] >= 0) glUniform3fv(loc[U_LAMB], 8, &amb[0][0]);
        if (loc[U_LATT] >= 0) glUniform4fv(loc[U_LATT], 8, &att[0][0]);
        if (loc[U_LSPOT] >= 0) glUniform4fv(loc[U_LSPOT], 8, &spot[0][0]);
    }

    if (p->verMaterial != m_verMaterial)
    {
        p->verMaterial = m_verMaterial;
        const D3DMATERIAL8 &m = m_s.material;
        if (loc[U_MAT_E] >= 0) glUniform4f(loc[U_MAT_E], m.Emissive.r, m.Emissive.g, m.Emissive.b, m.Emissive.a);
        if (loc[U_MAT_A] >= 0) glUniform4f(loc[U_MAT_A], m.Ambient.r, m.Ambient.g, m.Ambient.b, m.Ambient.a);
        if (loc[U_MAT_D] >= 0) glUniform4f(loc[U_MAT_D], m.Diffuse.r, m.Diffuse.g, m.Diffuse.b, m.Diffuse.a);
        if (loc[U_MAT_S] >= 0) glUniform4f(loc[U_MAT_S], m.Specular.r, m.Specular.g, m.Specular.b, m.Specular.a);
        if (loc[U_MAT_P] >= 0) glUniform1f(loc[U_MAT_P], m.Power);
    }

    if (p->verMisc != m_verMisc)
    {
        p->verMisc = m_verMisc;
        const DWORD *rs = m_s.rs;
        float c[4];
        if (loc[U_AMBIENT] >= 0) { ColorToVec4(rs[D3DRS_AMBIENT], c); glUniform4fv(loc[U_AMBIENT], 1, c); }
        if (loc[U_FOG] >= 0)
        {
            float start = DwordToFloat(rs[D3DRS_FOGSTART]), end = DwordToFloat(rs[D3DRS_FOGEND]);
            float range = end - start;
            glUniform4f(loc[U_FOG], start, end, DwordToFloat(rs[D3DRS_FOGDENSITY]), range != 0.0f ? 1.0f / range : 1.0f);
        }
        if (loc[U_FOGCOLOR] >= 0) { ColorToVec4(rs[D3DRS_FOGCOLOR], c); glUniform3fv(loc[U_FOGCOLOR], 1, c); }
        if (loc[U_TFACTOR] >= 0) { ColorToVec4(rs[D3DRS_TEXTUREFACTOR], c); glUniform4fv(loc[U_TFACTOR], 1, c); }
        if (loc[U_ALPHAREF] >= 0) glUniform1f(loc[U_ALPHAREF], (float)(rs[D3DRS_ALPHAREF] & 0xFF));
        if (loc[U_POINT] >= 0)
        {
            const float maxSize = Min(DwordToFloat(rs[D3DRS_POINTSIZE_MAX]), m_glcaps.maxPointSize);
            glUniform4f(loc[U_POINT], DwordToFloat(rs[D3DRS_POINTSIZE]), Min(DwordToFloat(rs[D3DRS_POINTSIZE_MIN]), maxSize),
                        maxSize, (float)m_s.viewport.Height);
        }
        if (loc[U_POINTATT] >= 0)
            glUniform3f(loc[U_POINTATT], DwordToFloat(rs[D3DRS_POINTSCALE_A]), DwordToFloat(rs[D3DRS_POINTSCALE_B]), DwordToFloat(rs[D3DRS_POINTSCALE_C]));
        if (loc[U_LOD] >= 0)
        {
            float lod[8];
            for (int i = 0; i < 8; ++i) lod[i] = DwordToFloat(m_s.tss[i][D3DTSS_MIPMAPLODBIAS]);
            glUniform1fv(loc[U_LOD], 8, lod);
        }
        if (loc[U_BUMP] >= 0)
        {
            float bump[8][4];
            for (int i = 0; i < 8; ++i)
                for (int k = 0; k < 4; ++k) bump[i][k] = DwordToFloat(m_s.tss[i][D3DTSS_BUMPENVMAT00 + k]);
            glUniform4fv(loc[U_BUMP], 8, &bump[0][0]);
        }
        if (loc[U_BUMPL] >= 0)
        {
            float bl[8][2];
            for (int i = 0; i < 8; ++i)
            {
                bl[i][0] = DwordToFloat(m_s.tss[i][D3DTSS_BUMPENVLSCALE]);
                bl[i][1] = DwordToFloat(m_s.tss[i][D3DTSS_BUMPENVLOFFSET]);
            }
            glUniform2fv(loc[U_BUMPL], 8, &bl[0][0]);
        }
    }

    if (loc[U_VSC] >= 0 && p->verVsConst != m_verVsConst)
    {
        p->verVsConst = m_verVsConst;
        glUniform4fv(loc[U_VSC], VS_CONSTANTS, &m_s.vsConst[0][0]);
    }
    if (loc[U_PSC] >= 0 && p->verPsConst != m_verPsConst)
    {
        p->verPsConst = m_verPsConst;
        glUniform4fv(loc[U_PSC], 8, &m_s.psConst[0][0]);
    }
}

//------------------------------------------------------------------------------
// Textures
//------------------------------------------------------------------------------
GLuint Device::SamplerFor(int stage, TextureBase *tb)
{
    const DWORD *t = m_s.tss[stage];
    DWORD minf = t[D3DTSS_MINFILTER], magf = t[D3DTSS_MAGFILTER], mipf = t[D3DTSS_MIPFILTER];
    const bool mipped = tb->m_levelCount > 1 && mipf != D3DTEXF_NONE;
    struct K { uint8_t minf, magf, mipf, mipped, au, av, aw, aniso; } k;
    k.minf = (uint8_t)minf; k.magf = (uint8_t)magf; k.mipf = (uint8_t)(mipped ? mipf : D3DTEXF_NONE); k.mipped = mipped;
    k.au = (uint8_t)t[D3DTSS_ADDRESSU]; k.av = (uint8_t)t[D3DTSS_ADDRESSV]; k.aw = (uint8_t)t[D3DTSS_ADDRESSW];
    const bool aniso = m_glcaps.anisotropic && (minf == D3DTEXF_ANISOTROPIC || magf == D3DTEXF_ANISOTROPIC);
    k.aniso = aniso ? (uint8_t)Clamp<DWORD>(t[D3DTSS_MAXANISOTROPY], 1, 16) : 1;
    std::string key(reinterpret_cast<const char *>(&k), sizeof k);
    auto it = m_samplers.find(key);
    if (it != m_samplers.end()) return it->second;

    GLuint s = 0;
    glGenSamplers(1, &s);
    GLenum gmin;
    const bool minLinear = minf != D3DTEXF_POINT && minf != D3DTEXF_NONE;
    if (!mipped) gmin = minLinear ? GL_LINEAR : GL_NEAREST;
    else if (mipf == D3DTEXF_POINT) gmin = minLinear ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST;
    else gmin = minLinear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR;
    glSamplerParameteri(s, GL_TEXTURE_MIN_FILTER, gmin);
    glSamplerParameteri(s, GL_TEXTURE_MAG_FILTER, (magf == D3DTEXF_POINT || magf == D3DTEXF_NONE) ? GL_NEAREST : GL_LINEAR);
    glSamplerParameteri(s, GL_TEXTURE_WRAP_S, MapAddress(k.au));
    glSamplerParameteri(s, GL_TEXTURE_WRAP_T, MapAddress(k.av));
    glSamplerParameteri(s, GL_TEXTURE_WRAP_R, MapAddress(k.aw));
    if (m_glcaps.anisotropic) glSamplerParameterf(s, GL_TEXTURE_MAX_ANISOTROPY_EXT, (float)k.aniso);
    m_samplers[key] = s;
    return s;
}

void Device::BindTextures()
{
    const ProgramKey &key = m_curProgram->key;
    for (int s = 0; s < key.stageCount; ++s)
    {
        const StageKey &st = key.stage[s];
        if (!st.texType) continue;
        TextureBase *tb = AsTextureBase(m_s.textures[s].get());
        GLenum target = st.texType == 1 ? GL_TEXTURE_2D : (st.texType == 2 ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_3D);
        const int slot = st.texType - 1;
        GLuint id = tb ? tb->m_tex : 0;
        // A texture that is also the current render target cannot be sampled (feedback loop).
        if (id && id == m_attachedColorTex) id = m_whiteTex[slot];
        if (!id) id = m_whiteTex[slot];
        if (m_activeUnit != s) { glActiveTexture(GL_TEXTURE0 + s); m_activeUnit = s; }
        if (m_boundTex[s][slot] != id) { glBindTexture(target, id); m_boundTex[s][slot] = id; }
        if (tb && id == tb->m_tex)
        {
            if (m_samplerDirtyMask & (1u << s))
            {
                m_stageSampler[s] = SamplerFor(s, tb);
                m_samplerDirtyMask &= ~(1u << s);
            }
            const GLuint sampler = m_stageSampler[s];
            if (m_boundSampler[s] != sampler) { glBindSampler(s, sampler); m_boundSampler[s] = sampler; }
            const GLint base = (GLint)Min<DWORD>(m_s.tss[s][D3DTSS_MAXMIPLEVEL], tb->m_levelCount - 1);
            if (tb->m_baseLevel != base)
            {
                glTexParameteri(target, GL_TEXTURE_BASE_LEVEL, base);
                tb->m_baseLevel = base;
            }
        }
        else
        {
            if (m_boundSampler[s] != m_whiteSampler)
            {
                if (!m_whiteSampler)
                {
                    glGenSamplers(1, &m_whiteSampler);
                    glSamplerParameteri(m_whiteSampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                    glSamplerParameteri(m_whiteSampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                }
                glBindSampler(s, m_whiteSampler);
                m_boundSampler[s] = m_whiteSampler;
            }
        }
    }
}

//------------------------------------------------------------------------------
// Vertex attributes
//------------------------------------------------------------------------------
void Device::FlushBuffers()
{
    for (int i = 0; i < MAX_STREAMS; ++i)
        if (m_s.streams[i].vb) static_cast<VertexBuffer *>(m_s.streams[i].vb.get())->Flush();
}

bool Device::BindAttributes(UINT baseVertex, GLuint userBuffer, size_t userOffset, UINT userStride)
{
    const DWORD vsh = m_s.vertexShader;
    VertexShaderObject *vso = (vsh & 1) ? FindVertexShader(vsh) : nullptr;
    const VertexLayout *layout = vso ? &vso->layout : LayoutFromFVF(vsh);
    if (!layout->count) return false;

    // Cheap signature of everything the pointers depend on.
    uint64_t sig = layout->id * 1000003ull + baseVertex;
    sig = sig * 31 + userBuffer;
    sig = sig * 31 + userOffset + userStride;
    GLuint bufs[MAX_STREAMS] = {};
    UINT strides[MAX_STREAMS] = {};
    for (int i = 0; i < layout->count; ++i)
    {
        const int st = layout->elems[i].stream;
        if (bufs[st]) continue;
        if (userBuffer && st == 0)
        {
            bufs[0] = userBuffer;
            strides[0] = userStride;
        }
        else
        {
            VertexBuffer *vb = static_cast<VertexBuffer *>(m_s.streams[st].vb.get());
            if (!vb) return false;
            bufs[st] = vb->Flush();
            strides[st] = m_s.streams[st].stride ? m_s.streams[st].stride : layout->stride[st];
        }
        sig = sig * 31 + bufs[st];
        sig = sig * 31 + strides[st];
    }
    if (sig == m_attribSig && !userBuffer) return true;
    m_attribSig = userBuffer ? 0 : sig;

    uint32_t enabled = 0;
    for (int i = 0; i < layout->count; ++i)
    {
        const VertexElement &e = layout->elems[i];
        const UINT stride = strides[e.stream];
        BindBuffer(GL_ARRAY_BUFFER, bufs[e.stream]);
        const size_t off = (e.stream == 0 && userBuffer ? userOffset : 0) + (size_t)e.offset + (size_t)baseVertex * stride;
        GLenum type = e.glType == 0 ? GL_FLOAT : (e.glType == 1 ? GL_UNSIGNED_BYTE : GL_SHORT);
        glVertexAttribPointer(e.reg, e.size, type, e.normalized ? GL_TRUE : GL_FALSE, stride, reinterpret_cast<const void *>(off));
        enabled |= 1u << e.reg;
    }
    const uint32_t changed = enabled ^ m_enabledAttribs;
    if (changed)
        for (int i = 0; i < 16; ++i)
            if (changed & (1u << i))
            {
                if (enabled & (1u << i)) glEnableVertexAttribArray(i);
                else glDisableVertexAttribArray(i);
            }
    m_enabledAttribs = enabled;
    return true;
}

//------------------------------------------------------------------------------
// Draw
//------------------------------------------------------------------------------
static bool PrimitiveInfo(D3DPRIMITIVETYPE type, UINT primCount, GLenum &mode, UINT &vertexCount)
{
    switch (type)
    {
    case D3DPT_POINTLIST: mode = GL_POINTS; vertexCount = primCount; return true;
    case D3DPT_LINELIST: mode = GL_LINES; vertexCount = primCount * 2; return true;
    case D3DPT_LINESTRIP: mode = GL_LINE_STRIP; vertexCount = primCount + 1; return true;
    case D3DPT_TRIANGLELIST: mode = GL_TRIANGLES; vertexCount = primCount * 3; return true;
    case D3DPT_TRIANGLESTRIP: mode = GL_TRIANGLE_STRIP; vertexCount = primCount + 2; return true;
    case D3DPT_TRIANGLEFAN: mode = GL_TRIANGLE_FAN; vertexCount = primCount + 2; return true;
    default: return false;
    }
}

bool Device::PrepareDraw(GLenum mode)
{
    m_drawingPoints = mode == GL_POINTS;
    ++m_drawCounter;
    if (m_contextLost) return false;
    GL_STAGE_CHECK("entry");
    if (!SelectProgram()) return false;
    GL_STAGE_CHECK("select program");
    ApplyRenderTargets();
    GL_STAGE_CHECK("render targets");
    m_attachedColorTex = 0;
    if (Surface *rt = static_cast<Surface *>(m_curRT.get()))
    {
        if (rt->GetKind() == Surface::BackBuffer) m_attachedColorTex = m_bbColor;
        else if (rt->GetKind() == Surface::Level) m_attachedColorTex = rt->Texture()->m_tex;
    }
    ApplyPipeline();
    GL_STAGE_CHECK("apply pipeline");
    if (m_boundProgram != m_curProgram->id)
    {
        glUseProgram(m_curProgram->id);
        m_boundProgram = m_curProgram->id;
    }
    GL_STAGE_CHECK("pipeline/program");
    UploadUniforms();
    GL_STAGE_CHECK("uniforms");
    BindTextures();
    GL_STAGE_CHECK("textures");
    return true;
}

uint32_t Device::UploadStream(const void *data, size_t size, bool index)
{
    GLuint &buf = index ? m_streamIB : m_streamVB;
    size_t &cap = index ? m_streamIBSize : m_streamVBSize;
    size_t &pos = index ? m_streamIBPos : m_streamVBPos;
    const GLenum target = index ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
    if (!buf) glGenBuffers(1, &buf);
    BindBuffer(target, buf);
    size = (size + 3) & ~size_t(3);
    if (size > cap || pos + size > cap)
    {
        if (size > cap) cap = Max<size_t>(size * 2, 4u << 20);
        glBufferData(target, (GLsizeiptr)cap, nullptr, GL_STREAM_DRAW); // orphan
        pos = 0;
    }
    glBufferSubData(target, (GLintptr)pos, (GLsizeiptr)size, data);
    uint32_t off = (uint32_t)pos;
    pos += size;
    return off;
}

bool Device::DrawCommon(D3DPRIMITIVETYPE type, UINT primCount, bool indexed, UINT start, UINT baseVertex,
                        const void *userVerts, UINT userStride, const void *userIndices, D3DFORMAT userIndexFmt, UINT userVertCount)
{
    GLenum mode;
    UINT count;
    if (!primCount || !PrimitiveInfo(type, primCount, mode, count)) return false;
    if (!PrepareDraw(mode)) return false;

    // Vertex data.
    const bool userData = userVerts != nullptr;
    GLuint ub = 0;
    size_t uoff = 0;
    if (userData)
    {
        size_t bytes = (size_t)userVertCount * userStride;
        uoff = UploadStream(userVerts, bytes, false);
        ub = m_streamVB;
    }
    else
        FlushBuffers();
    if (!BindAttributes(userData ? 0 : baseVertex, ub, uoff, userStride)) return false;
    GL_STAGE_CHECK("attributes");

    // Index data.
    GLenum indexType = GL_UNSIGNED_SHORT;
    size_t indexOffset = 0;
    GLuint ibuf = 0;
    const void *wireSrc = nullptr;
    if (indexed)
    {
        if (userData)
        {
            const bool wide = userIndexFmt == D3DFMT_INDEX32;
            indexType = wide ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
            const size_t bytes = (size_t)count * (wide ? 4 : 2);
            indexOffset = UploadStream(userIndices, bytes, true);
            GL_STAGE_CHECK("index upload");
            ibuf = m_streamIB;
            wireSrc = userIndices;
        }
        else
        {
            IndexBuffer *ib = static_cast<IndexBuffer *>(m_s.indices.get());
            if (!ib) return false;
            const bool wide = ib->Format() == D3DFMT_INDEX32;
            indexType = wide ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
            ibuf = ib->Flush();
            GL_STAGE_CHECK("index buffer flush");
            indexOffset = (size_t)start * (wide ? 4 : 2);
            wireSrc = ib->m_storage.Data() + indexOffset;
        }
        BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibuf);
        GL_STAGE_CHECK("element buffer bind");
    }

    const DWORD fill = m_s.rs[D3DRS_FILLMODE];
    const bool triangles = mode == GL_TRIANGLES || mode == GL_TRIANGLE_STRIP || mode == GL_TRIANGLE_FAN;
    if (triangles && (fill == D3DFILL_WIREFRAME || fill == D3DFILL_POINT))
    {
        // GL ES has no polygon mode: rebuild the primitive as lines/points.
        std::vector<uint32_t> tri;
        tri.reserve(count);
        auto fetch = [&](UINT i) -> uint32_t {
            if (!indexed) return start + i;
            return indexType == GL_UNSIGNED_INT ? static_cast<const uint32_t *>(wireSrc)[i] : static_cast<const uint16_t *>(wireSrc)[i];
        };
        for (UINT p = 0; p < primCount; ++p)
        {
            uint32_t a, b, c;
            if (mode == GL_TRIANGLES) { a = fetch(p * 3); b = fetch(p * 3 + 1); c = fetch(p * 3 + 2); }
            else if (mode == GL_TRIANGLE_STRIP) { a = fetch(p); b = fetch(p + 1 + (p & 1)); c = fetch(p + 2 - (p & 1)); }
            else { a = fetch(0); b = fetch(p + 1); c = fetch(p + 2); }
            tri.push_back(a); tri.push_back(b); tri.push_back(c);
        }
        std::vector<uint32_t> out;
        if (fill == D3DFILL_WIREFRAME)
            for (size_t i = 0; i + 2 < tri.size(); i += 3)
            { out.push_back(tri[i]); out.push_back(tri[i + 1]); out.push_back(tri[i + 1]); out.push_back(tri[i + 2]); out.push_back(tri[i + 2]); out.push_back(tri[i]); }
        else
            out = tri;
        size_t off = UploadStream(out.data(), out.size() * 4, true);
        BindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_streamIB);
        glDrawElements(fill == D3DFILL_WIREFRAME ? GL_LINES : GL_POINTS, (GLsizei)out.size(), GL_UNSIGNED_INT, reinterpret_cast<const void *>(off));
        return true;
    }

    if (indexed)
    {
        glDrawElements(mode, count, indexType, reinterpret_cast<const void *>(indexOffset));
        GL_STAGE_CHECK("glDrawElements");
    }
    else
        glDrawArrays(mode, userData ? 0 : start, count);

    if (GetConfig().debug)
    {
        GLenum err = glGetError();
        if (err != GL_NO_ERROR) Log("GL error 0x%x after draw #%u (fvf 0x%x, indexed %d)", err, m_drawCounter, (unsigned)m_s.vertexShader, (int)indexed);
    }
    return true;
}

HRESULT Device::DrawPrimitive(D3DPRIMITIVETYPE type, UINT start, UINT primCount)
{
    return DrawCommon(type, primCount, false, start, 0, nullptr, 0, nullptr, D3DFMT_UNKNOWN, 0) ? D3D_OK : D3DERR_INVALIDCALL;
}

HRESULT Device::DrawIndexedPrimitive(D3DPRIMITIVETYPE type, UINT, UINT, UINT startIndex, UINT primCount)
{
    return DrawCommon(type, primCount, true, startIndex, m_s.baseVertexIndex, nullptr, 0, nullptr, D3DFMT_UNKNOWN, 0)
               ? D3D_OK : D3DERR_INVALIDCALL;
}

HRESULT Device::DrawPrimitiveUP(D3DPRIMITIVETYPE type, UINT primCount, const void *verts, UINT stride)
{
    if (!verts || !stride) return D3DERR_INVALIDCALL;
    GLenum mode; UINT count;
    if (!PrimitiveInfo(type, primCount, mode, count)) return D3DERR_INVALIDCALL;
    return DrawCommon(type, primCount, false, 0, 0, verts, stride, nullptr, D3DFMT_UNKNOWN, count) ? D3D_OK : D3DERR_INVALIDCALL;
}

HRESULT Device::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE type, UINT minIndex, UINT numIndices, UINT primCount,
                                       const void *indices, D3DFORMAT fmt, const void *verts, UINT stride)
{
    if (!verts || !indices || !stride) return D3DERR_INVALIDCALL;
    return DrawCommon(type, primCount, true, 0, 0, verts, stride, indices, fmt, minIndex + numIndices) ? D3D_OK : D3DERR_INVALIDCALL;
}

HRESULT Device::ProcessVertices(UINT, UINT, UINT, IDirect3DVertexBuffer8 *, DWORD)
{
    WEBD3D8_UNSUPPORTED("ProcessVertices");
    return D3DERR_INVALIDCALL;
}

//------------------------------------------------------------------------------
// Clear
//------------------------------------------------------------------------------
HRESULT Device::Clear(DWORD count, const D3DRECT *rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil)
{
    if (m_contextLost) return D3DERR_DEVICELOST;
    if (count && !rects) return D3DERR_INVALIDCALL;
    ApplyRenderTargets();

    GLbitfield bits = 0;
    if (flags & D3DCLEAR_TARGET)
    {
        float c[4];
        ColorToVec4(color, c);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(c[0], c[1], c[2], c[3]);
        bits |= GL_COLOR_BUFFER_BIT;
    }
    const Surface *ds = static_cast<const Surface *>(m_curDS.get());
    if ((flags & D3DCLEAR_ZBUFFER) && ds)
    {
        glDepthMask(GL_TRUE);
        glClearDepthf(Clamp(z, 0.0f, 1.0f));
        bits |= GL_DEPTH_BUFFER_BIT;
    }
    const FormatInfo *dsInfo = ds ? GetFormatInfo(ds->Format()) : nullptr;
    if ((flags & D3DCLEAR_STENCIL) && dsInfo && dsInfo->stencil)
    {
        glStencilMask(0xFF);
        glClearStencil((GLint)stencil);
        bits |= GL_STENCIL_BUFFER_BIT;
    }
    if (bits)
    {
        glDisable(GL_STENCIL_TEST); // stencil test does not affect clears, but keep the state tidy
        glEnable(GL_SCISSOR_TEST);
        auto clearRect = [&](GLint x, GLint y, GLint w, GLint h) {
            glScissor(x, y, w, h);
            glClear(bits);
        };
        if (count)
        {
            for (DWORD i = 0; i < count; ++i)
            {
                const D3DRECT &r = rects[i];
                LONG x1 = Clamp<LONG>(r.x1, 0, m_rtWidth), x2 = Clamp<LONG>(r.x2, 0, m_rtWidth);
                LONG y1 = Clamp<LONG>(r.y1, 0, m_rtHeight), y2 = Clamp<LONG>(r.y2, 0, m_rtHeight);
                if (x2 > x1 && y2 > y1) clearRect(x1, y1, x2 - x1, y2 - y1);
            }
        }
        else
            clearRect((GLint)m_s.viewport.X, (GLint)m_s.viewport.Y, (GLint)m_s.viewport.Width, (GLint)m_s.viewport.Height);
        glDisable(GL_SCISSOR_TEST);
        // The cached masks/test state were touched.
        m_pipelineValid = false;
    }
    return D3D_OK;
}

} // namespace webd3d8
