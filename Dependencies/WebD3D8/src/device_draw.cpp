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

    if (all || memcmp(&want, &have, sizeof want) != 0) ++g_d3d.pipelineChanges;
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
    key.points = m_drawingPoints ? 1 : 0;
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
        st.borderMask = (uint8_t)((t[D3DTSS_ADDRESSU] == D3DTADDRESS_BORDER ? 1 : 0) | (t[D3DTSS_ADDRESSV] == D3DTADDRESS_BORDER ? 2 : 0) |
                                  (t[D3DTSS_ADDRESSW] == D3DTADDRESS_BORDER && st.texType == 3 ? 4 : 0));
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
    const ProgramKey &key = p->key;
    const GLint *loc = p->loc;

    // Only the groups some uniform of this program belongs to, and only when a state of the group
    // changed since this program last received it.
    uint32_t pending = 0;
    for (int g = 0; g < G_COUNT; ++g)
        if ((p->usedGroups & (1u << g)) && p->ver[g] != m_ver[g]) pending |= 1u << g;
    if (!pending) return;

    for (int g = 0; g < G_COUNT; ++g)
    {
        if (!(pending & (1u << g))) continue;
        p->ver[g] = m_ver[g];
        ++g_d3d.uniformUploads;
        ++g_d3d.groupUploads[g];
        switch (g)
        {
        case G_XFORM:
        {
            UpdateDerivedMatrices();
            // wvp, wv, normal matrix (3 columns), world: the program reads a prefix of this block.
            float xf[15 * 4];
            memcpy(xf, m_wvp.m, 64);
            if (p->xfCount > 4)
            {
                memcpy(xf + 16, m_wv.m, 64);
                for (int c = 0; c < 3; ++c) { memcpy(xf + 32 + c * 4, m_nm + c * 3, 12); xf[32 + c * 4 + 3] = 0.0f; }
                memcpy(xf + 44, m_s.world[0].m, 64);
            }
            glUniform4fv(loc[U_XF], p->xfCount, xf);
            break;
        }
        case G_TEXMAT:
        {
            float tm[8 * 16];
            for (int i = 0; i < 8; ++i) memcpy(tm + i * 16, m_s.tex[i].m, sizeof(float) * 16);
            glUniformMatrix4fv(loc[U_TM], 8, GL_FALSE, tm);
            break;
        }
        case G_VIEWPORT:
        {
            const float vw = Max<float>(1.0f, (float)m_s.viewport.Width), vh = Max<float>(1.0f, (float)m_s.viewport.Height);
            const float v[8] = {1.0f / vw, 1.0f / vh, m_s.viewport.MinZ, m_s.viewport.MaxZ - m_s.viewport.MinZ,
                                (float)m_s.viewport.X, (float)m_s.viewport.Y, vw, vh};
            glUniform4fv(loc[U_VIEW], 2, v);
            break;
        }
        case G_CLIP:
            glUniform4fv(loc[U_CLIP], MAX_CLIP_PLANES, &m_s.clipPlanes[0][0]);
            break;
        case G_LIGHTS:
        {
            if (!(key.lighting && key.lightCount)) break;
            float lt[8 * 7][4] = {};
            int n = 0;
            const Mat4 &v = m_s.view;
            for (const LightState &ls : m_s.lights)
            {
                if (!ls.defined || !ls.enabled) continue;
                if (n >= key.lightCount) break;
                const D3DLIGHT8 &l = ls.light;
                float (*o)[4] = lt + n * 7;
                // World -> view (row vectors).
                o[0][0] = l.Position.x * v.m[0] + l.Position.y * v.m[4] + l.Position.z * v.m[8] + v.m[12];
                o[0][1] = l.Position.x * v.m[1] + l.Position.y * v.m[5] + l.Position.z * v.m[9] + v.m[13];
                o[0][2] = l.Position.x * v.m[2] + l.Position.y * v.m[6] + l.Position.z * v.m[10] + v.m[14];
                float d[3] = {l.Direction.x * v.m[0] + l.Direction.y * v.m[4] + l.Direction.z * v.m[8],
                              l.Direction.x * v.m[1] + l.Direction.y * v.m[5] + l.Direction.z * v.m[9],
                              l.Direction.x * v.m[2] + l.Direction.y * v.m[6] + l.Direction.z * v.m[10]};
                float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                if (len > 0) { d[0] /= len; d[1] /= len; d[2] /= len; }
                memcpy(o[1], d, sizeof d);
                o[2][0] = l.Diffuse.r; o[2][1] = l.Diffuse.g; o[2][2] = l.Diffuse.b;
                o[3][0] = l.Specular.r; o[3][1] = l.Specular.g; o[3][2] = l.Specular.b;
                o[4][0] = l.Ambient.r; o[4][1] = l.Ambient.g; o[4][2] = l.Ambient.b;
                o[5][0] = l.Attenuation0; o[5][1] = l.Attenuation1; o[5][2] = l.Attenuation2;
                o[5][3] = Min(l.Range, 3.0e38f);
                const float ct = std::cos(l.Theta * 0.5f), cp = std::cos(l.Phi * 0.5f);
                o[6][0] = ct; o[6][1] = cp; o[6][2] = l.Falloff;
                o[6][3] = (ct - cp) != 0.0f ? 1.0f / (ct - cp) : 1.0f;
                ++n;
            }
            // Only the lights the program was generated for are sent.
            glUniform4fv(loc[U_LT], key.lightCount * 7, &lt[0][0]);
            break;
        }
        case G_MATERIAL:
        {
            const D3DMATERIAL8 &m = m_s.material;
            const float mat[5][4] = {{m.Emissive.r, m.Emissive.g, m.Emissive.b, m.Emissive.a},
                                     {m.Ambient.r, m.Ambient.g, m.Ambient.b, m.Ambient.a},
                                     {m.Diffuse.r, m.Diffuse.g, m.Diffuse.b, m.Diffuse.a},
                                     {m.Specular.r, m.Specular.g, m.Specular.b, m.Specular.a},
                                     {m.Power, 0, 0, 0}};
            glUniform4fv(loc[U_MAT], 5, &mat[0][0]);
            break;
        }
        case G_AMBIENT:
        {
            float c[4];
            ColorToVec4(m_s.rs[D3DRS_AMBIENT], c);
            glUniform4fv(loc[U_AMBIENT], 1, c);
            break;
        }
        case G_FOG:
        {
            const DWORD *rs = m_s.rs;
            const float start = DwordToFloat(rs[D3DRS_FOGSTART]), end = DwordToFloat(rs[D3DRS_FOGEND]);
            const float range = end - start;
            float f[8] = {start, end, DwordToFloat(rs[D3DRS_FOGDENSITY]), range != 0.0f ? 1.0f / range : 1.0f, 0, 0, 0, 0};
            ColorToVec4(rs[D3DRS_FOGCOLOR], f + 4);
            glUniform4fv(loc[U_FOGP], 2, f);
            break;
        }
        case G_TFACTOR:
        {
            float c[4];
            ColorToVec4(m_s.rs[D3DRS_TEXTUREFACTOR], c);
            glUniform4fv(loc[U_TFACTOR], 1, c);
            break;
        }
        case G_ALPHAREF:
            glUniform1f(loc[U_ALPHAREF], (float)(m_s.rs[D3DRS_ALPHAREF] & 0xFF));
            break;
        case G_POINT:
        {
            const DWORD *rs = m_s.rs;
            const float maxSize = Min(DwordToFloat(rs[D3DRS_POINTSIZE_MAX]), m_glcaps.maxPointSize);
            const float pt[8] = {DwordToFloat(rs[D3DRS_POINTSIZE]), Min(DwordToFloat(rs[D3DRS_POINTSIZE_MIN]), maxSize), maxSize,
                                 (float)m_s.viewport.Height,
                                 DwordToFloat(rs[D3DRS_POINTSCALE_A]), DwordToFloat(rs[D3DRS_POINTSCALE_B]), DwordToFloat(rs[D3DRS_POINTSCALE_C]), 0};
            glUniform4fv(loc[U_PT], 2, pt);
            break;
        }
        case G_LOD:
        {
            float lod[8];
            for (int i = 0; i < 8; ++i) lod[i] = DwordToFloat(m_s.tss[i][D3DTSS_MIPMAPLODBIAS]);
            glUniform1fv(loc[U_LOD], 8, lod);
            break;
        }
        case G_BUMP:
        {
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
            break;
        }
        case G_BORDER:
        {
            float b[8][4];
            for (int i = 0; i < 8; ++i) ColorToVec4(m_s.tss[i][D3DTSS_BORDERCOLOR], b[i]);
            glUniform4fv(loc[U_BORDER], 8, &b[0][0]);
            break;
        }
        case G_VSC:
            glUniform4fv(loc[U_VSC], VS_CONSTANTS, &m_s.vsConst[0][0]);
            break;
        case G_PSC:
            glUniform4fv(loc[U_PSC], 8, &m_s.psConst[0][0]);
            break;
        }
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
        if (m_boundTex[s][slot] != id) { glBindTexture(target, id); m_boundTex[s][slot] = id; ++g_d3d.textureBinds; }
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

/// Sets up the vertex attributes of the current layout and streams and makes the vertex array object
/// that holds them current. Layouts, buffers and strides that were drawn with before are served from a
/// cache of vertex array objects (one GL call to bind instead of one per attribute); the cache is keyed
/// without the base vertex because indexed draws pass it to the draw call
/// (WEBGL_draw_instanced_base_vertex_base_instance) and streamed data is placed at multiples of the
/// vertex size. `pointerBase` is a vertex offset applied to the attribute pointers instead, for browsers
/// without that extension; such draws use the uncached scratch vertex array.
bool Device::BindAttributes(UINT pointerBase, bool userData, UINT userStride)
{
    const DWORD vsh = m_s.vertexShader;
    VertexShaderObject *vso = (vsh & 1) ? FindVertexShader(vsh) : nullptr;
    const VertexLayout *layout = vso ? &vso->layout : LayoutFromFVF(vsh);
    if (!layout->count) return false;

    GLuint bufs[MAX_STREAMS] = {};
    UINT strides[MAX_STREAMS] = {};
    VaoKey key;
    memset(&key, 0, sizeof key);
    key.layout = layout->id;
    key.pointerBase = pointerBase;
    int streamsUsed = 0;
    for (int i = 0; i < layout->count; ++i)
    {
        const int st = layout->elems[i].stream;
        if (bufs[st]) continue;
        if (userData && st == 0)
        {
            bufs[0] = m_streamVB;
            strides[0] = userStride;
        }
        else
        {
            VertexBuffer *vb = static_cast<VertexBuffer *>(m_s.streams[st].vb.get());
            if (!vb) return false;
            bufs[st] = vb->Flush();
            strides[st] = m_s.streams[st].stride ? m_s.streams[st].stride : layout->stride[st];
        }
        if (streamsUsed < VAO_KEY_STREAMS)
        {
            key.buf[streamsUsed] = bufs[st];
            key.stride[streamsUsed] = (uint16_t)strides[st];
            key.stream[streamsUsed] = (uint8_t)st;
        }
        ++streamsUsed;
    }

    bool cacheable = streamsUsed <= VAO_KEY_STREAMS && !(userData && pointerBase != 0);
    VaoEntry *entry = nullptr;
    if (cacheable)
    {
        const uint64_t h = HashBytes(&key, sizeof key);
        auto range = m_vaoCache.equal_range(h);
        for (auto it = range.first; it != range.second; ++it)
            if (memcmp(&it->second->key, &key, sizeof key) == 0) { entry = it->second; break; }
        if (!entry && pointerBase != 0)
        {
            // A base vertex in the key (browsers without the base vertex extension): only geometry that
            // keeps coming back is worth a vertex array object; dynamic buffers move their data around.
            uint32_t &seen = m_vaoSeen[h];
            if (seen == 0) { seen = 1; cacheable = false; }
        }
        if (cacheable && !entry)
        {
            if (m_vaoCache.size() >= VAO_CACHE_LIMIT) EvictVao();
            entry = new VaoEntry();
            entry->key = key;
            glGenVertexArrays(1, &entry->vao);
            m_vaoCache.insert({h, entry});
            glBindVertexArray(entry->vao);
            m_curVao = entry;
            SetAttributePointers(layout, bufs, strides, pointerBase, 0, &entry->enabled);
            ++g_d3d.vaoCreated;
        }
        if (entry)
        {
            entry->lastUse = ++m_vaoClock;
            if (m_curVao != entry)
            {
                glBindVertexArray(entry->vao);
                m_curVao = entry;
            }
            return true;
        }
    }

    // Uncached: the scratch vertex array, reconfigured when anything it depends on changed.
    uint64_t sig = layout->id * 1000003ull + pointerBase;
    for (int i = 0; i < MAX_STREAMS; ++i)
        if (bufs[i]) { sig = sig * 31 + bufs[i]; sig = sig * 31 + strides[i]; }
    if (m_curVao)
    {
        glBindVertexArray(m_vao); // keeps the state it was last configured with
        m_curVao = nullptr;
    }
    if (sig == m_attribSig) return true;
    m_attribSig = sig;
    SetAttributePointers(layout, bufs, strides, pointerBase, m_enabledAttribs, &m_enabledAttribs);
    return true;
}

/// Points the attributes of `layout` at the buffers and enables exactly those arrays (the vertex array
/// object that is bound keeps the state). `oldEnabled` is the mask of arrays currently enabled in it.
void Device::SetAttributePointers(const VertexLayout *layout, const GLuint *bufs, const UINT *strides, UINT pointerBase,
                                  uint32_t oldEnabled, uint32_t *newEnabled)
{
    uint32_t enabled = 0;
    for (int i = 0; i < layout->count; ++i)
    {
        const VertexElement &e = layout->elems[i];
        const UINT stride = strides[e.stream];
        BindBuffer(GL_ARRAY_BUFFER, bufs[e.stream]);
        const size_t off = (size_t)e.offset + (size_t)pointerBase * stride;
        GLenum type = e.glType == 0 ? GL_FLOAT : (e.glType == 1 ? GL_UNSIGNED_BYTE : GL_SHORT);
        glVertexAttribPointer(e.reg, e.size, type, e.normalized ? GL_TRUE : GL_FALSE, stride, reinterpret_cast<const void *>(off));
        enabled |= 1u << e.reg;
    }
    const uint32_t changed = enabled ^ oldEnabled;
    if (changed)
        for (int i = 0; i < 16; ++i)
            if (changed & (1u << i))
            {
                if (enabled & (1u << i)) glEnableVertexAttribArray(i);
                else glDisableVertexAttribArray(i);
            }
    *newEnabled = enabled;
}

/// Deletes the least recently used vertex array object (and the ones nothing can use any more).
void Device::EvictVao()
{
    uint64_t oldest = ~0ull;
    VaoEntry *victim = nullptr;
    uint64_t victimHash = 0;
    for (auto &kv : m_vaoCache)
        if (kv.second != m_curVao && kv.second->lastUse < oldest) { oldest = kv.second->lastUse; victim = kv.second; victimHash = kv.first; }
    if (!victim) return;
    DeleteVao(victimHash, victim);
}

void Device::DeleteVao(uint64_t hash, VaoEntry *e)
{
    auto range = m_vaoCache.equal_range(hash);
    for (auto it = range.first; it != range.second; ++it)
        if (it->second == e) { m_vaoCache.erase(it); break; }
    if (m_curVao == e) { glBindVertexArray(m_vao); m_curVao = nullptr; m_attribSig = 0; }
    if (e->vao) glDeleteVertexArrays(1, &e->vao);
    delete e;
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

/// Diagnostics (-webd3d8report): records the Direct3D 8 features of the current draw that are ignored
/// or emulated approximately. Only called while the report is on.
void Device::DiagCheckDrawState(GLenum mode)
{
    const DWORD *rs = m_s.rs;
    const ProgramKey &key = m_curProgram->key;
    for (int i = 0; i < 8; ++i)
        if (rs[D3DRS_WRAP0 + i]) WD3D_HIT(Unsupported, "rs WRAP%d (cylindrical texture wrapping) is ignored", i);
    if (rs[D3DRS_VERTEXBLEND] != D3DVBF_DISABLE) WD3D_HIT(Unsupported, "rs VERTEXBLEND (vertex blending) is ignored");
    if (rs[D3DRS_INDEXEDVERTEXBLENDENABLE]) WD3D_HIT(Unsupported, "rs INDEXEDVERTEXBLENDENABLE is ignored");
    if (DwordToFloat(rs[D3DRS_TWEENFACTOR]) != 0.0f) WD3D_HIT(Unsupported, "rs TWEENFACTOR (vertex tweening) is ignored");
    if (rs[D3DRS_ZBIAS]) WD3D_HIT(Approximated, "rs ZBIAS=%u emulated with polygon offset", (unsigned)rs[D3DRS_ZBIAS]);
    if (rs[D3DRS_LINEPATTERN]) WD3D_HIT(Unsupported, "rs LINEPATTERN is ignored");
    if (rs[D3DRS_EDGEANTIALIAS]) WD3D_HIT(Unsupported, "rs EDGEANTIALIAS is ignored");
    if (!rs[D3DRS_CLIPPING]) WD3D_HIT(Approximated, "rs CLIPPING=FALSE (clipping is always on)");
    if (!rs[D3DRS_MULTISAMPLEANTIALIAS] && m_samples) WD3D_HIT(Unsupported, "rs MULTISAMPLEANTIALIAS=FALSE on a multisampled target is ignored");
    if (rs[D3DRS_ZENABLE] != D3DZB_TRUE && rs[D3DRS_ZENABLE] != D3DZB_FALSE) WD3D_HIT(Approximated, "rs ZENABLE=%u (w-buffering) uses z", (unsigned)rs[D3DRS_ZENABLE]);
    if (rs[D3DRS_FILLMODE] == D3DFILL_POINT && mode != GL_POINTS) WD3D_HIT(Approximated, "rs FILLMODE=POINT");
    if (rs[D3DRS_BLENDOP] > D3DBLENDOP_MAX) WD3D_HIT(Unsupported, "rs BLENDOP=%u", (unsigned)rs[D3DRS_BLENDOP]);
    if (rs[D3DRS_SRCBLEND] == D3DBLEND_BOTHSRCALPHA || rs[D3DRS_SRCBLEND] == D3DBLEND_BOTHINVSRCALPHA)
        WD3D_HIT(Approximated, "rs SRCBLEND=BOTHSRCALPHA/BOTHINVSRCALPHA expanded to a blend pair");
    if (rs[D3DRS_FOGENABLE] && !rs[D3DRS_FOGVERTEXMODE] && !rs[D3DRS_FOGTABLEMODE]) WD3D_HIT(Approximated, "fog enabled with no fog mode (ignored)");
    if (rs[D3DRS_FOGENABLE] && rs[D3DRS_FOGVERTEXMODE] && !rs[D3DRS_FOGTABLEMODE] && key.vsHandle)
        WD3D_HIT(Approximated, "vertex fog with a vertex shader uses oFog");
    if (rs[D3DRS_SHADEMODE] == D3DSHADE_FLAT && !m_glcaps.provokingVertex) WD3D_HIT(Approximated, "flat shading without WEBGL_provoking_vertex");
    if (rs[D3DRS_SHADEMODE] != D3DSHADE_FLAT && rs[D3DRS_SHADEMODE] != D3DSHADE_GOURAUD) WD3D_HIT(Unsupported, "rs SHADEMODE=%u", (unsigned)rs[D3DRS_SHADEMODE]);
    for (int s = 0; s < key.stageCount; ++s)
    {
        const StageKey &st = key.stage[s];
        const DWORD *t = m_s.tss[s];
        if (!m_curProgram->key.psHandle)
        {
            if (st.colorOp == D3DTOP_BUMPENVMAP || st.colorOp == D3DTOP_BUMPENVMAPLUMINANCE)
                WD3D_HIT(Unsupported, "tss COLOROP=BUMPENVMAP[LUMINANCE] in the fixed-function pipeline");
            if (st.colorOp == D3DTOP_PREMODULATE || st.alphaOp == D3DTOP_PREMODULATE)
                WD3D_HIT(Approximated, "tss OP=PREMODULATE (treated as SELECTARG1)");
        }
        if (t[D3DTSS_ADDRESSU] == D3DTADDRESS_MIRRORONCE || t[D3DTSS_ADDRESSV] == D3DTADDRESS_MIRRORONCE)
            WD3D_HIT(Approximated, "tss ADDRESS=MIRRORONCE (treated as MIRROR)");
        if (t[D3DTSS_ADDRESSU] == D3DTADDRESS_BORDER || t[D3DTSS_ADDRESSV] == D3DTADDRESS_BORDER)
            WD3D_HIT(Approximated, "tss ADDRESS=BORDER (hard cut, no filtering at the border)");
        if (t[D3DTSS_MAGFILTER] == D3DTEXF_FLATCUBIC || t[D3DTSS_MAGFILTER] == D3DTEXF_GAUSSIANCUBIC)
            WD3D_HIT(Approximated, "tss MAGFILTER=CUBIC (treated as LINEAR)");
        if ((t[D3DTSS_TEXTURETRANSFORMFLAGS] & 0xFF) > 4) WD3D_HIT(Unsupported, "tss TEXTURETRANSFORMFLAGS count %u", (unsigned)(t[D3DTSS_TEXTURETRANSFORMFLAGS] & 0xFF));
    }
    const DWORD vsh = m_s.vertexShader;
    if (!(vsh & 1) && vsh)
    {
        const DWORD pos = vsh & D3DFVF_POSITION_MASK;
        if (pos >= D3DFVF_XYZB1 && pos <= D3DFVF_XYZB5) WD3D_HIT(Unsupported, "FVF with vertex blend weights (weights are skipped)");
        if (vsh & D3DFVF_LASTBETA_UBYTE4) WD3D_HIT(Unsupported, "FVF LASTBETA_UBYTE4");
    }
}

bool Device::PrepareDraw(GLenum mode)
{
    m_drawingPoints = mode == GL_POINTS;
    ++m_drawCounter;
    if (m_ctxEvent) CheckContext();
    if (m_contextLost || m_needsReset) return false;
    if (GetConfig().debug && m_presentCounter % 120 == 0)
        Log("draw #%u: mode 0x%x, fvf 0x%x, vs %u, alphablend %u, src %u dst %u, zenable %u, cull %u, tex0 %d, rt %ux%u", m_drawCounter, (unsigned)mode,
            (unsigned)m_s.vertexShader, (unsigned)m_s.vertexShader, m_s.rs[D3DRS_ALPHABLENDENABLE], m_s.rs[D3DRS_SRCBLEND], m_s.rs[D3DRS_DESTBLEND],
            m_s.rs[D3DRS_ZENABLE], m_s.rs[D3DRS_CULLMODE], m_s.textures[0] ? 1 : 0, m_rtWidth, m_rtHeight);
    if (GetConfig().debug && m_presentCounter % 120 == 0)
    {
        const Mat4 &w = m_s.world[0], &v = m_s.view, &p = m_s.proj;
        Log("  viewport %u,%u %ux%u z %.2f-%.2f", (unsigned)m_s.viewport.X, (unsigned)m_s.viewport.Y, (unsigned)m_s.viewport.Width, (unsigned)m_s.viewport.Height, m_s.viewport.MinZ, m_s.viewport.MaxZ);
        Log("  world %g %g %g %g / %g %g %g %g / %g %g %g %g / %g %g %g %g", w.m[0], w.m[1], w.m[2], w.m[3], w.m[4], w.m[5], w.m[6], w.m[7], w.m[8], w.m[9], w.m[10], w.m[11], w.m[12], w.m[13], w.m[14], w.m[15]);
        Log("  view  %g %g %g %g / %g %g %g %g / %g %g %g %g / %g %g %g %g", v.m[0], v.m[1], v.m[2], v.m[3], v.m[4], v.m[5], v.m[6], v.m[7], v.m[8], v.m[9], v.m[10], v.m[11], v.m[12], v.m[13], v.m[14], v.m[15]);
        Log("  lighting %u, alphatest %u func %u ref %u, colorwrite 0x%x, fog %u, stage0 colorop %u arg1 %u arg2 %u alphaop %u aarg1 %u aarg2 %u; stage1 colorop %u",
            m_s.rs[D3DRS_LIGHTING], m_s.rs[D3DRS_ALPHATESTENABLE], m_s.rs[D3DRS_ALPHAFUNC], m_s.rs[D3DRS_ALPHAREF], m_s.rs[D3DRS_COLORWRITEENABLE], m_s.rs[D3DRS_FOGENABLE],
            m_s.tss[0][D3DTSS_COLOROP], m_s.tss[0][D3DTSS_COLORARG1], m_s.tss[0][D3DTSS_COLORARG2], m_s.tss[0][D3DTSS_ALPHAOP], m_s.tss[0][D3DTSS_ALPHAARG1], m_s.tss[0][D3DTSS_ALPHAARG2], m_s.tss[1][D3DTSS_COLOROP]);
        Log("  proj  %g %g %g %g / %g %g %g %g / %g %g %g %g / %g %g %g %g", p.m[0], p.m[1], p.m[2], p.m[3], p.m[4], p.m[5], p.m[6], p.m[7], p.m[8], p.m[9], p.m[10], p.m[11], p.m[12], p.m[13], p.m[14], p.m[15]);
    }
    GL_STAGE_CHECK("entry");
    if (!SelectProgram()) return false;
    if (g_diagOn) DiagCheckDrawState(mode);
    GL_STAGE_CHECK("select program");
    ApplyRenderTargets();
    GL_STAGE_CHECK("render targets");
    m_attachedColorTex = 0;
    if (Surface *rt = static_cast<Surface *>(m_curRT.get()))
    {
        if (rt->GetKind() == Surface::BackBuffer) { m_attachedColorTex = m_samples ? 0 : m_bbColor; m_bbDirty = true; }
        else if (rt->GetKind() == Surface::Level) m_attachedColorTex = rt->Texture()->m_tex;
    }
    ApplyPipeline();
    GL_STAGE_CHECK("apply pipeline");
    if (m_boundProgram != m_curProgram->id)
    {
        glUseProgram(m_curProgram->id);
        m_boundProgram = m_curProgram->id;
        ++g_d3d.programSwitches;
    }
    GL_STAGE_CHECK("pipeline/program");
    UploadUniforms();
    GL_STAGE_CHECK("uniforms");
    BindTextures();
    GL_STAGE_CHECK("textures");
    if (GetConfig().debugForce)
    {
        if (GetConfig().debugForce & 1) glDisable(GL_CULL_FACE);
        if (GetConfig().debugForce & 2) glDisable(GL_BLEND);
        if (GetConfig().debugForce & 4) glDisable(GL_DEPTH_TEST);
        m_applied.Invalidate();
    }
    return true;
}

/// Appends data to the streaming buffer used for draws from user memory. Offsets are multiples of
/// `align` (the vertex size for vertex data, so that the offset is a whole number of vertices and can
/// be passed as a base vertex). The buffer is orphaned when it is full.
size_t Device::UploadStream(const void *data, size_t size, bool index, size_t align)
{
    GLuint &buf = index ? m_streamIB : m_streamVB;
    size_t &cap = index ? m_streamIBSize : m_streamVBSize;
    size_t &pos = index ? m_streamIBPos : m_streamVBPos;
    const GLenum target = index ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
    if (!buf) glGenBuffers(1, &buf);
    BindBuffer(target, buf);
    if (align < 4) align = 4;
    size = (size + 3) & ~size_t(3);
    size_t at = (pos + align - 1) / align * align;
    if (size > cap || at + size > cap)
    {
        if (size + align > cap) cap = Max<size_t>((size + align) * 2, 4u << 20);
        glBufferData(target, (GLsizeiptr)cap, nullptr, GL_STREAM_DRAW); // orphan
        g_gl.uploadBytes += 0;
        at = 0;
    }
    glBufferSubData(target, (GLintptr)at, (GLsizeiptr)size, data);
    g_gl.uploadBytes += (uint32_t)size;
    pos = at + size;
    return at;
}

bool Device::DrawCommon(D3DPRIMITIVETYPE type, UINT primCount, bool indexed, UINT start, UINT baseVertex,
                        const void *userVerts, UINT userStride, const void *userIndices, D3DFORMAT userIndexFmt, UINT userVertCount)
{
    GLenum mode;
    UINT count;
    if (!primCount || !PrimitiveInfo(type, primCount, mode, count)) return false;
    if (!PrepareDraw(mode)) return false;
    ++g_d3d.draws;
    g_d3d.primitives += primCount;
    if (userVerts) ++g_d3d.drawsUP;
    else if (indexed) ++g_d3d.drawsIndexed;

    const bool userData = userVerts != nullptr;
    const DWORD fill = m_s.rs[D3DRS_FILLMODE];
    const bool triangles = mode == GL_TRIANGLES || mode == GL_TRIANGLE_STRIP || mode == GL_TRIANGLE_FAN;
    const bool wire = triangles && (fill == D3DFILL_WIREFRAME || fill == D3DFILL_POINT);
    // Flat shading takes the color of the triangle's first vertex in Direct3D and of the last one in
    // WebGL unless WEBGL_provoking_vertex switches the convention; otherwise the triangles are
    // reordered so that the first vertex comes last.
    const bool flatFallback = triangles && !wire && m_curProgram->key.flatShade && !m_glcaps.provokingVertex && mode != GL_TRIANGLE_FAN;
    const bool rebuild = wire || flatFallback;

    // Vertex data. User data is appended to the streaming buffer at a multiple of the vertex size, so
    // that its position is a vertex number ("vertexBase") like the BaseVertexIndex of SetIndices.
    UINT vertexBase = 0;
    if (userData)
    {
        const size_t bytes = (size_t)userVertCount * userStride;
        vertexBase = (UINT)(UploadStream(userVerts, bytes, false, userStride) / userStride);
    }
    else
        FlushBuffers();
    // Where the base vertex is applied: by the draw call (extension), or by the attribute pointers.
    const UINT totalBase = userData ? vertexBase : (indexed ? baseVertex : 0);
    const bool baseInDraw = indexed && m_glcaps.baseVertex && !rebuild;
    if (!BindAttributes(baseInDraw || !indexed || rebuild ? 0 : totalBase, userData, userStride)) return false;
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
            indexOffset = UploadStream(userIndices, bytes, true, 4);
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

    if (rebuild)
    {
        // GL ES has no polygon mode: rebuild the primitive as lines/points (or rotate the triangles).
        // The indices of the rebuilt list include the base vertex, so the attribute pointers stay put.
        std::vector<uint32_t> tri;
        tri.reserve(count);
        auto fetch = [&](UINT i) -> uint32_t {
            if (!indexed) return (userData ? vertexBase : start) + i;
            const uint32_t raw = indexType == GL_UNSIGNED_INT ? static_cast<const uint32_t *>(wireSrc)[i] : static_cast<const uint16_t *>(wireSrc)[i];
            return raw + totalBase;
        };
        for (UINT p = 0; p < primCount; ++p)
        {
            uint32_t a, b, c;
            if (mode == GL_TRIANGLES) { a = fetch(p * 3); b = fetch(p * 3 + 1); c = fetch(p * 3 + 2); }
            else if (mode == GL_TRIANGLE_STRIP) { a = fetch(p); b = fetch(p + 1 + (p & 1)); c = fetch(p + 2 - (p & 1)); }
            else { a = fetch(0); b = fetch(p + 1); c = fetch(p + 2); }
            if (flatFallback)
            {
                // (a,b,c) keeps its winding as (b,c,a); the vertex D3D shades with ends up last.
                tri.push_back(b); tri.push_back(c); tri.push_back(a);
            }
            else { tri.push_back(a); tri.push_back(b); tri.push_back(c); }
        }
        std::vector<uint32_t> out;
        GLenum outMode = GL_TRIANGLES;
        if (wire && fill == D3DFILL_WIREFRAME)
        {
            outMode = GL_LINES;
            for (size_t i = 0; i + 2 < tri.size(); i += 3)
            { out.push_back(tri[i]); out.push_back(tri[i + 1]); out.push_back(tri[i + 1]); out.push_back(tri[i + 2]); out.push_back(tri[i + 2]); out.push_back(tri[i]); }
        }
        else
        {
            if (wire) outMode = GL_POINTS;
            out = tri;
        }
        size_t off = UploadStream(out.data(), out.size() * 4, true, 4);
        BindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_streamIB);
        glDrawElements(outMode, (GLsizei)out.size(), GL_UNSIGNED_INT, reinterpret_cast<const void *>(off));
        if (g_diagOn)
        {
            if (wire) WD3D_HIT(Approximated, "fill mode %s rebuilt from the triangle list on the CPU", fill == D3DFILL_WIREFRAME ? "WIREFRAME" : "POINT");
            else WD3D_HIT(Approximated, "flat shading by reordering triangles (no WEBGL_provoking_vertex)");
        }
        return true;
    }
    if (g_diagOn && triangles && m_curProgram->key.flatShade && mode == GL_TRIANGLE_FAN)
        WD3D_HIT(Approximated, "flat shaded triangle fan uses the wrong provoking vertex");

    if (indexed)
    {
        if (baseInDraw && totalBase)
            glDrawElementsInstancedBaseVertexBaseInstanceWEBGL(mode, count, indexType, reinterpret_cast<const void *>(indexOffset), 1, (GLint)totalBase, 0);
        else
            glDrawElements(mode, count, indexType, reinterpret_cast<const void *>(indexOffset));
        GL_STAGE_CHECK("glDrawElements");
    }
    else
        glDrawArrays(mode, userData ? vertexBase : start, count);

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
    if (m_ctxEvent) CheckContext();
    if (m_contextLost || m_needsReset) return D3DERR_DEVICELOST;
    if (count && !rects) return D3DERR_INVALIDCALL;
    ApplyRenderTargets();
    ++g_d3d.clears;
    if (m_samples && m_curRT.get() && static_cast<Surface *>(m_curRT.get())->GetKind() == Surface::BackBuffer) m_bbDirty = true;

    // The masks a clear needs are set through the pipeline cache so that the next draw only re-issues
    // what really differs.
    const bool known = m_pipelineValid;
    GLbitfield bits = 0;
    if (flags & D3DCLEAR_TARGET)
    {
        float c[4];
        ColorToVec4(color, c);
        if (!known || m_applied.colorMask != 0xF) glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        m_applied.colorMask = 0xF;
        if (c[0] != m_clearColor[0] || c[1] != m_clearColor[1] || c[2] != m_clearColor[2] || c[3] != m_clearColor[3] || !m_clearColorValid)
        {
            glClearColor(c[0], c[1], c[2], c[3]);
            memcpy(m_clearColor, c, sizeof c);
            m_clearColorValid = true;
        }
        bits |= GL_COLOR_BUFFER_BIT;
    }
    const Surface *ds = static_cast<const Surface *>(m_curDS.get());
    if ((flags & D3DCLEAR_ZBUFFER) && ds)
    {
        if (!known || !m_applied.depthMask) glDepthMask(GL_TRUE);
        m_applied.depthMask = true;
        const float zc = Clamp(z, 0.0f, 1.0f);
        if (zc != m_clearDepth || !m_clearDepthValid) { glClearDepthf(zc); m_clearDepth = zc; m_clearDepthValid = true; }
        bits |= GL_DEPTH_BUFFER_BIT;
    }
    const FormatInfo *dsInfo = ds ? GetFormatInfo(ds->Format()) : nullptr;
    if ((flags & D3DCLEAR_STENCIL) && dsInfo && dsInfo->stencil)
    {
        if (!known || m_applied.stencilWriteMask != 0xFF) glStencilMask(0xFF);
        m_applied.stencilWriteMask = 0xFF;
        if ((GLint)stencil != m_clearStencil || !m_clearStencilValid) { glClearStencil((GLint)stencil); m_clearStencil = (GLint)stencil; m_clearStencilValid = true; }
        bits |= GL_STENCIL_BUFFER_BIT;
    }
    if (bits)
    {
        // The scissor test restricts a clear to the rectangles (the stencil test does not affect it). It
        // stays enabled afterwards; the next draw switches it off when needed (PipelineState::scissor).
        if (!known || !m_applied.scissor) glEnable(GL_SCISSOR_TEST);
        m_applied.scissor = true;
        auto clearRect = [&](GLint x, GLint y, GLint w, GLint h) {
            const GLint sc[4] = {x, y, w, h};
            if (memcmp(sc, m_applied.sc, sizeof sc) != 0 || !known) { glScissor(x, y, w, h); memcpy(m_applied.sc, sc, sizeof sc); }
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
        // Anything that was not known (m_pipelineValid false) is applied in full by the next draw.
    }
    return D3D_OK;
}

} // namespace webd3d8
