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
** WebAssembly port: render state, texture stage state, transforms, lights,
** materials, viewport, shader constants and state blocks of the device.
*/
#include "d3d8_internal.h"
#include "program.h"
#include "resources.h"
#include "shader.h"

namespace webd3d8 {

//------------------------------------------------------------------------------
// Defaults
//------------------------------------------------------------------------------
static void SetDefaultRenderStates(DWORD *rs, bool depthEnabled)
{
    memset(rs, 0, sizeof(DWORD) * RS_COUNT);
    rs[D3DRS_ZENABLE] = depthEnabled ? D3DZB_TRUE : D3DZB_FALSE;
    rs[D3DRS_FILLMODE] = D3DFILL_SOLID;
    rs[D3DRS_SHADEMODE] = D3DSHADE_GOURAUD;
    rs[D3DRS_LINEPATTERN] = 0;
    rs[D3DRS_ZWRITEENABLE] = TRUE;
    rs[D3DRS_ALPHATESTENABLE] = FALSE;
    rs[D3DRS_LASTPIXEL] = TRUE;
    rs[D3DRS_SRCBLEND] = D3DBLEND_ONE;
    rs[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
    rs[D3DRS_CULLMODE] = D3DCULL_CCW;
    rs[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
    rs[D3DRS_ALPHAREF] = 0;
    rs[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS;
    rs[D3DRS_DITHERENABLE] = FALSE;
    rs[D3DRS_ALPHABLENDENABLE] = FALSE;
    rs[D3DRS_FOGENABLE] = FALSE;
    rs[D3DRS_SPECULARENABLE] = FALSE;
    rs[D3DRS_ZVISIBLE] = 0;
    rs[D3DRS_FOGCOLOR] = 0;
    rs[D3DRS_FOGTABLEMODE] = D3DFOG_NONE;
    rs[D3DRS_FOGSTART] = FloatToDword(0.0f);
    rs[D3DRS_FOGEND] = FloatToDword(1.0f);
    rs[D3DRS_FOGDENSITY] = FloatToDword(1.0f);
    rs[D3DRS_EDGEANTIALIAS] = FALSE;
    rs[D3DRS_ZBIAS] = 0;
    rs[D3DRS_RANGEFOGENABLE] = FALSE;
    rs[D3DRS_STENCILENABLE] = FALSE;
    rs[D3DRS_STENCILFAIL] = D3DSTENCILOP_KEEP;
    rs[D3DRS_STENCILZFAIL] = D3DSTENCILOP_KEEP;
    rs[D3DRS_STENCILPASS] = D3DSTENCILOP_KEEP;
    rs[D3DRS_STENCILFUNC] = D3DCMP_ALWAYS;
    rs[D3DRS_STENCILREF] = 0;
    rs[D3DRS_STENCILMASK] = 0xFFFFFFFF;
    rs[D3DRS_STENCILWRITEMASK] = 0xFFFFFFFF;
    rs[D3DRS_TEXTUREFACTOR] = 0xFFFFFFFF;
    for (int i = 0; i < 8; ++i) rs[D3DRS_WRAP0 + i] = 0;
    rs[D3DRS_CLIPPING] = TRUE;
    rs[D3DRS_LIGHTING] = TRUE;
    rs[D3DRS_AMBIENT] = 0;
    rs[D3DRS_FOGVERTEXMODE] = D3DFOG_NONE;
    rs[D3DRS_COLORVERTEX] = TRUE;
    rs[D3DRS_LOCALVIEWER] = TRUE;
    rs[D3DRS_NORMALIZENORMALS] = FALSE;
    rs[D3DRS_DIFFUSEMATERIALSOURCE] = D3DMCS_COLOR1;
    rs[D3DRS_SPECULARMATERIALSOURCE] = D3DMCS_COLOR2;
    rs[D3DRS_AMBIENTMATERIALSOURCE] = D3DMCS_COLOR2;
    rs[D3DRS_EMISSIVEMATERIALSOURCE] = D3DMCS_MATERIAL;
    rs[D3DRS_VERTEXBLEND] = D3DVBF_DISABLE;
    rs[D3DRS_CLIPPLANEENABLE] = 0;
    rs[D3DRS_SOFTWAREVERTEXPROCESSING] = FALSE;
    rs[D3DRS_POINTSIZE] = FloatToDword(1.0f);
    rs[D3DRS_POINTSIZE_MIN] = FloatToDword(1.0f);
    rs[D3DRS_POINTSPRITEENABLE] = FALSE;
    rs[D3DRS_POINTSCALEENABLE] = FALSE;
    rs[D3DRS_POINTSCALE_A] = FloatToDword(1.0f);
    rs[D3DRS_POINTSCALE_B] = FloatToDword(0.0f);
    rs[D3DRS_POINTSCALE_C] = FloatToDword(0.0f);
    rs[D3DRS_MULTISAMPLEANTIALIAS] = TRUE;
    rs[D3DRS_MULTISAMPLEMASK] = 0xFFFFFFFF;
    rs[D3DRS_PATCHEDGESTYLE] = D3DPATCHEDGE_DISCRETE;
    rs[D3DRS_PATCHSEGMENTS] = FloatToDword(1.0f);
    rs[D3DRS_POINTSIZE_MAX] = FloatToDword(64.0f);
    rs[D3DRS_INDEXEDVERTEXBLENDENABLE] = FALSE;
    rs[D3DRS_COLORWRITEENABLE] = 0x0000000F;
    rs[D3DRS_TWEENFACTOR] = FloatToDword(0.0f);
    rs[D3DRS_BLENDOP] = D3DBLENDOP_ADD;
    rs[D3DRS_POSITIONORDER] = D3DORDER_CUBIC;
    rs[D3DRS_NORMALORDER] = D3DORDER_LINEAR;
}

static void SetDefaultStageStates(DWORD tss[MAX_STAGES][TSS_COUNT])
{
    memset(tss, 0, sizeof(DWORD) * MAX_STAGES * TSS_COUNT);
    for (int s = 0; s < MAX_STAGES; ++s)
    {
        DWORD *t = tss[s];
        t[D3DTSS_COLOROP] = (s == 0) ? D3DTOP_MODULATE : D3DTOP_DISABLE;
        t[D3DTSS_COLORARG1] = D3DTA_TEXTURE;
        t[D3DTSS_COLORARG2] = D3DTA_CURRENT;
        t[D3DTSS_ALPHAOP] = (s == 0) ? D3DTOP_SELECTARG1 : D3DTOP_DISABLE;
        t[D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
        t[D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
        t[D3DTSS_BUMPENVMAT00] = FloatToDword(0.0f);
        t[D3DTSS_BUMPENVMAT01] = FloatToDword(0.0f);
        t[D3DTSS_BUMPENVMAT10] = FloatToDword(0.0f);
        t[D3DTSS_BUMPENVMAT11] = FloatToDword(0.0f);
        t[D3DTSS_TEXCOORDINDEX] = s;
        t[D3DTSS_ADDRESSU] = D3DTADDRESS_WRAP;
        t[D3DTSS_ADDRESSV] = D3DTADDRESS_WRAP;
        t[D3DTSS_ADDRESSW] = D3DTADDRESS_WRAP;
        t[D3DTSS_BORDERCOLOR] = 0;
        t[D3DTSS_MAGFILTER] = D3DTEXF_POINT;
        t[D3DTSS_MINFILTER] = D3DTEXF_POINT;
        t[D3DTSS_MIPFILTER] = D3DTEXF_NONE;
        t[D3DTSS_MIPMAPLODBIAS] = FloatToDword(0.0f);
        t[D3DTSS_MAXMIPLEVEL] = 0;
        t[D3DTSS_MAXANISOTROPY] = 1;
        t[D3DTSS_BUMPENVLSCALE] = FloatToDword(0.0f);
        t[D3DTSS_BUMPENVLOFFSET] = FloatToDword(0.0f);
        t[D3DTSS_TEXTURETRANSFORMFLAGS] = D3DTTFF_DISABLE;
        t[D3DTSS_COLORARG0] = D3DTA_CURRENT;
        t[D3DTSS_ALPHAARG0] = D3DTA_CURRENT;
        t[D3DTSS_RESULTARG] = D3DTA_CURRENT;
    }
}

DeviceState::DeviceState()
{
    SetDefaultRenderStates(rs, true);
    SetDefaultStageStates(tss);
    for (Mat4 &m : world) m = Mat4::Identity();
    view = proj = Mat4::Identity();
    for (Mat4 &m : tex) m = Mat4::Identity();
    memset(&material, 0, sizeof material);
    memset(&viewport, 0, sizeof viewport);
    viewport.MaxZ = 1.0f;
    memset(clipPlanes, 0, sizeof clipPlanes);
    memset(vsConst, 0, sizeof vsConst);
    memset(psConst, 0, sizeof psConst);
}

void Device::ResetState()
{
    const bool depth = m_curDS.get() != nullptr;
    m_s = DeviceState();
    SetDefaultRenderStates(m_s.rs, depth);
    m_s.viewport.X = 0;
    m_s.viewport.Y = 0;
    m_s.viewport.Width = m_pp.BackBufferWidth;
    m_s.viewport.Height = m_pp.BackBufferHeight;
    m_s.viewport.MinZ = 0.0f;
    m_s.viewport.MaxZ = 1.0f;
    m_keyDirty = true;
    m_derivedDirty = true;
    DirtyAllUniforms();
    m_pipelineValid = false;
    m_targetsDirty = true;
    m_attribSig = 0;
    for (int i = 0; i < MAX_STAGES; ++i) m_texLodSet[i] = 0;
}

//------------------------------------------------------------------------------
// Transforms
//------------------------------------------------------------------------------
static Mat4 *TransformSlot(DeviceState &s, D3DTRANSFORMSTATETYPE t)
{
    switch ((int)t)
    {
    case D3DTS_VIEW: return &s.view;
    case D3DTS_PROJECTION: return &s.proj;
    default: break;
    }
    int v = (int)t;
    if (v >= D3DTS_TEXTURE0 && v <= D3DTS_TEXTURE7) return &s.tex[v - D3DTS_TEXTURE0];
    if (v >= 256 && v < 260) return &s.world[v - 256];
    return nullptr;
}

/// Bumps the uniform groups that depend on a transform.
void Device::TransformChanged(D3DTRANSFORMSTATETYPE state)
{
    const int v = (int)state;
    if (v == D3DTS_VIEW) { Dirty(G_XFORM); Dirty(G_LIGHTS); m_derivedDirty = true; }
    else if (v == D3DTS_PROJECTION) { Dirty(G_XFORM); m_derivedDirty = true; }
    else if (v >= D3DTS_TEXTURE0 && v <= D3DTS_TEXTURE7) Dirty(G_TEXMAT);
    else if (v == D3DTS_WORLD) { Dirty(G_XFORM); m_derivedDirty = true; }
    // World matrices 1..3 only matter for vertex blending, which is not implemented.
}

HRESULT Device::SetTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX *m)
{
    if (!m) return D3DERR_INVALIDCALL;
    Mat4 *slot = TransformSlot(m_s, state);
    if (!slot) return D3DERR_INVALIDCALL;
    ++g_d3d.setTransform;
    const Mat4 nm = Mat4::FromD3D(*m);
    if (memcmp(nm.m, slot->m, sizeof nm.m) == 0) return D3D_OK;
    *slot = nm;
    TransformChanged(state);
    return D3D_OK;
}

HRESULT Device::GetTransform(D3DTRANSFORMSTATETYPE state, D3DMATRIX *m)
{
    if (!m) return D3DERR_INVALIDCALL;
    Mat4 *slot = TransformSlot(m_s, state);
    if (!slot) return D3DERR_INVALIDCALL;
    memcpy(m, slot->m, sizeof slot->m);
    return D3D_OK;
}

HRESULT Device::MultiplyTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX *m)
{
    if (!m) return D3DERR_INVALIDCALL;
    Mat4 *slot = TransformSlot(m_s, state);
    if (!slot) return D3DERR_INVALIDCALL;
    *slot = Mat4::FromD3D(*m) * *slot;
    TransformChanged(state);
    return D3D_OK;
}

HRESULT Device::SetViewport(const D3DVIEWPORT8 *vp)
{
    if (!vp) return D3DERR_INVALIDCALL;
    if (vp->X + vp->Width > m_rtWidth || vp->Y + vp->Height > m_rtHeight || !vp->Width || !vp->Height)
        return D3DERR_INVALIDCALL;
    m_s.viewport = *vp;
    Dirty(G_VIEWPORT);
    Dirty(G_POINT);
    return D3D_OK;
}

HRESULT Device::GetViewport(D3DVIEWPORT8 *vp)
{
    if (!vp) return D3DERR_INVALIDCALL;
    *vp = m_s.viewport;
    return D3D_OK;
}

HRESULT Device::SetMaterial(const D3DMATERIAL8 *m)
{
    if (!m) return D3DERR_INVALIDCALL;
    m_s.material = *m;
    Dirty(G_MATERIAL);
    return D3D_OK;
}

HRESULT Device::GetMaterial(D3DMATERIAL8 *m)
{
    if (!m) return D3DERR_INVALIDCALL;
    *m = m_s.material;
    return D3D_OK;
}

HRESULT Device::SetLight(DWORD index, const D3DLIGHT8 *l)
{
    if (!l || index >= 256) return D3DERR_INVALIDCALL;
    if (l->Type != D3DLIGHT_POINT && l->Type != D3DLIGHT_SPOT && l->Type != D3DLIGHT_DIRECTIONAL) return D3DERR_INVALIDCALL;
    if (m_s.lights.size() <= index) m_s.lights.resize(index + 1);
    m_s.lights[index].defined = true;
    m_s.lights[index].light = *l;
    Dirty(G_LIGHTS);
    m_keyDirty = true;
    return D3D_OK;
}

HRESULT Device::GetLight(DWORD index, D3DLIGHT8 *l)
{
    if (!l || index >= m_s.lights.size() || !m_s.lights[index].defined) return D3DERR_INVALIDCALL;
    *l = m_s.lights[index].light;
    return D3D_OK;
}

HRESULT Device::LightEnable(DWORD index, BOOL enable)
{
    if (index >= 256) return D3DERR_INVALIDCALL;
    if (m_s.lights.size() <= index) m_s.lights.resize(index + 1);
    LightState &ls = m_s.lights[index];
    if (!ls.defined)
    {
        // Enabling an undefined light creates a default one.
        ls.defined = true;
        memset(&ls.light, 0, sizeof ls.light);
        ls.light.Type = D3DLIGHT_DIRECTIONAL;
        ls.light.Diffuse.r = ls.light.Diffuse.g = ls.light.Diffuse.b = 1.0f;
        ls.light.Direction.z = 1.0f;
    }
    ls.enabled = enable != 0;
    Dirty(G_LIGHTS);
    m_keyDirty = true;
    return D3D_OK;
}

HRESULT Device::GetLightEnable(DWORD index, BOOL *e)
{
    if (!e || index >= m_s.lights.size() || !m_s.lights[index].defined) return D3DERR_INVALIDCALL;
    *e = m_s.lights[index].enabled;
    return D3D_OK;
}

HRESULT Device::SetClipPlane(DWORD index, const float *plane)
{
    if (!plane || index >= MAX_CLIP_PLANES) return D3DERR_INVALIDCALL;
    memcpy(m_s.clipPlanes[index], plane, 4 * sizeof(float));
    Dirty(G_CLIP);
    return D3D_OK;
}

HRESULT Device::GetClipPlane(DWORD index, float *plane)
{
    if (!plane || index >= MAX_CLIP_PLANES) return D3DERR_INVALIDCALL;
    memcpy(plane, m_s.clipPlanes[index], 4 * sizeof(float));
    return D3D_OK;
}

//------------------------------------------------------------------------------
// Render state / texture stage state
//------------------------------------------------------------------------------
HRESULT Device::SetRenderState(D3DRENDERSTATETYPE state, DWORD value)
{
    if ((unsigned)state >= RS_COUNT) return D3DERR_INVALIDCALL;
    ++g_d3d.setRenderState;
    DWORD &slot = m_s.rs[state];
    if (slot == value) return D3D_OK;
    slot = value;
    switch (state)
    {
    // States that change the generated shader code.
    case D3DRS_LIGHTING: case D3DRS_SPECULARENABLE: case D3DRS_FOGENABLE: case D3DRS_FOGTABLEMODE:
    case D3DRS_FOGVERTEXMODE: case D3DRS_RANGEFOGENABLE: case D3DRS_COLORVERTEX: case D3DRS_LOCALVIEWER:
    case D3DRS_NORMALIZENORMALS: case D3DRS_DIFFUSEMATERIALSOURCE: case D3DRS_SPECULARMATERIALSOURCE:
    case D3DRS_AMBIENTMATERIALSOURCE: case D3DRS_EMISSIVEMATERIALSOURCE: case D3DRS_ALPHATESTENABLE:
    case D3DRS_ALPHAFUNC: case D3DRS_CLIPPLANEENABLE: case D3DRS_POINTSPRITEENABLE: case D3DRS_POINTSCALEENABLE:
    case D3DRS_SHADEMODE:
        m_keyDirty = true;
        break;
    // States that are shader uniforms.
    case D3DRS_FOGSTART: case D3DRS_FOGEND: case D3DRS_FOGDENSITY: case D3DRS_FOGCOLOR:
        Dirty(G_FOG);
        break;
    case D3DRS_TEXTUREFACTOR: Dirty(G_TFACTOR); break;
    case D3DRS_ALPHAREF: Dirty(G_ALPHAREF); break;
    case D3DRS_AMBIENT: Dirty(G_AMBIENT); break;
    case D3DRS_POINTSIZE: case D3DRS_POINTSIZE_MIN: case D3DRS_POINTSIZE_MAX:
    case D3DRS_POINTSCALE_A: case D3DRS_POINTSCALE_B: case D3DRS_POINTSCALE_C:
        Dirty(G_POINT);
        break;
    default: break;
    }
    return D3D_OK;
}

HRESULT Device::GetRenderState(D3DRENDERSTATETYPE state, DWORD *value)
{
    if (!value || (unsigned)state >= RS_COUNT) return D3DERR_INVALIDCALL;
    *value = m_s.rs[state];
    return D3D_OK;
}

HRESULT Device::SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value)
{
    if (stage >= MAX_STAGES || (unsigned)type >= TSS_COUNT) return D3DERR_INVALIDCALL;
    ++g_d3d.setTextureStageState;
    DWORD &slot = m_s.tss[stage][type];
    if (slot == value) return D3D_OK;
    const DWORD old = slot;
    slot = value;
    switch (type)
    {
    case D3DTSS_COLOROP: case D3DTSS_COLORARG0: case D3DTSS_COLORARG1: case D3DTSS_COLORARG2:
    case D3DTSS_ALPHAOP: case D3DTSS_ALPHAARG0: case D3DTSS_ALPHAARG1: case D3DTSS_ALPHAARG2:
    case D3DTSS_RESULTARG: case D3DTSS_TEXCOORDINDEX: case D3DTSS_TEXTURETRANSFORMFLAGS:
        m_keyDirty = true;
        break;
    case D3DTSS_ADDRESSU: case D3DTSS_ADDRESSV: case D3DTSS_ADDRESSW:
        // BORDER addressing is emulated in the shader, so it is part of the program key.
        if (old == D3DTADDRESS_BORDER || value == D3DTADDRESS_BORDER) m_keyDirty = true;
        break;
    case D3DTSS_BORDERCOLOR:
        Dirty(G_BORDER);
        break;
    case D3DTSS_MIPMAPLODBIAS:
        m_keyDirty = true;
        Dirty(G_LOD);
        break;
    case D3DTSS_BUMPENVMAT00: case D3DTSS_BUMPENVMAT01: case D3DTSS_BUMPENVMAT10: case D3DTSS_BUMPENVMAT11:
    case D3DTSS_BUMPENVLSCALE: case D3DTSS_BUMPENVLOFFSET:
        Dirty(G_BUMP);
        break;
    default: break;
    }
    m_samplerDirtyMask |= 1u << stage;
    return D3D_OK;
}

HRESULT Device::GetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD *value)
{
    if (!value || stage >= MAX_STAGES || (unsigned)type >= TSS_COUNT) return D3DERR_INVALIDCALL;
    *value = m_s.tss[stage][type];
    return D3D_OK;
}

HRESULT Device::SetTexture(DWORD stage, IDirect3DBaseTexture8 *tex)
{
    if (stage >= MAX_STAGES) return D3DERR_INVALIDCALL;
    ++g_d3d.setTexture;
    if (m_s.textures[stage].get() == tex) return D3D_OK;
    m_s.textures[stage] = tex;
    m_keyDirty = true;
    m_samplerDirtyMask |= 1u << stage;
    return D3D_OK;
}

HRESULT Device::GetTexture(DWORD stage, IDirect3DBaseTexture8 **pp)
{
    if (!pp || stage >= MAX_STAGES) return D3DERR_INVALIDCALL;
    *pp = m_s.textures[stage].get();
    if (*pp) (*pp)->AddRef();
    return D3D_OK;
}

//------------------------------------------------------------------------------
// State blocks (full snapshots)
//------------------------------------------------------------------------------
HRESULT Device::BeginStateBlock()
{
    m_recordingBlock = true;
    return D3D_OK;
}

HRESULT Device::EndStateBlock(DWORD *pToken)
{
    if (!pToken || !m_recordingBlock) return D3DERR_INVALIDCALL;
    m_recordingBlock = false;
    return CreateStateBlock(D3DSBT_ALL, pToken);
}

HRESULT Device::CreateStateBlock(D3DSTATEBLOCKTYPE, DWORD *pToken)
{
    if (!pToken) return D3DERR_INVALIDCALL;
    for (size_t i = 0; i < m_stateBlocks.size(); ++i)
        if (!m_stateBlockValid[i])
        {
            m_stateBlocks[i] = m_s;
            m_stateBlockValid[i] = true;
            *pToken = (DWORD)i + 1;
            return D3D_OK;
        }
    m_stateBlocks.push_back(m_s);
    m_stateBlockValid.push_back(true);
    *pToken = (DWORD)m_stateBlocks.size();
    return D3D_OK;
}

HRESULT Device::ApplyStateBlock(DWORD token)
{
    if (token == 0 || token > m_stateBlocks.size() || !m_stateBlockValid[token - 1]) return D3DERR_INVALIDCALL;
    m_s = m_stateBlocks[token - 1];
    m_keyDirty = true;
    m_derivedDirty = true;
    m_samplerDirtyMask = 0xFF;
    DirtyAllUniforms();
    m_attribSig = 0;
    return D3D_OK;
}

HRESULT Device::CaptureStateBlock(DWORD token)
{
    if (token == 0 || token > m_stateBlocks.size() || !m_stateBlockValid[token - 1]) return D3DERR_INVALIDCALL;
    m_stateBlocks[token - 1] = m_s;
    return D3D_OK;
}

HRESULT Device::DeleteStateBlock(DWORD token)
{
    if (token == 0 || token > m_stateBlocks.size() || !m_stateBlockValid[token - 1]) return D3DERR_INVALIDCALL;
    m_stateBlocks[token - 1] = DeviceState();
    m_stateBlockValid[token - 1] = false;
    return D3D_OK;
}

//------------------------------------------------------------------------------
// Streams and indices
//------------------------------------------------------------------------------
HRESULT Device::SetStreamSource(UINT stream, IDirect3DVertexBuffer8 *vb, UINT stride)
{
    if (stream >= MAX_STREAMS) return D3DERR_INVALIDCALL;
    m_s.streams[stream].vb = vb;
    m_s.streams[stream].stride = stride;
    m_attribSig = 0;
    return D3D_OK;
}

HRESULT Device::GetStreamSource(UINT stream, IDirect3DVertexBuffer8 **pp, UINT *stride)
{
    if (stream >= MAX_STREAMS || !pp) return D3DERR_INVALIDCALL;
    *pp = m_s.streams[stream].vb.get();
    if (*pp) (*pp)->AddRef();
    if (stride) *stride = m_s.streams[stream].stride;
    return D3D_OK;
}

HRESULT Device::SetIndices(IDirect3DIndexBuffer8 *ib, UINT base)
{
    m_s.indices = ib;
    m_s.baseVertexIndex = base;
    return D3D_OK;
}

HRESULT Device::GetIndices(IDirect3DIndexBuffer8 **pp, UINT *base)
{
    if (!pp) return D3DERR_INVALIDCALL;
    *pp = m_s.indices.get();
    if (*pp) (*pp)->AddRef();
    if (base) *base = m_s.baseVertexIndex;
    return D3D_OK;
}

//------------------------------------------------------------------------------
// Shaders
//------------------------------------------------------------------------------
VertexShaderObject *Device::FindVertexShader(DWORD handle)
{
    auto it = m_vertexShaders.find(handle);
    return it == m_vertexShaders.end() ? nullptr : it->second;
}

PixelShaderObject *Device::FindPixelShader(DWORD handle)
{
    auto it = m_pixelShaders.find(handle);
    return it == m_pixelShaders.end() ? nullptr : it->second;
}

HRESULT Device::CreateVertexShader(const DWORD *decl, const DWORD *func, DWORD *pHandle, DWORD)
{
    if (!decl || !pHandle) return D3DERR_INVALIDCALL;
    if (func && GetConfig().vsVersion == 0)
    {
        // Shaders are only available when the caps advertised them.
        WEBD3D8_UNSUPPORTED("vertex shaders are disabled (WebD3D8_SetShaderModel)");
    }
    VertexShaderObject *vs = nullptr;
    if (!CreateVertexShaderObject(decl, func, &vs)) return D3DERR_INVALIDCALL;
    vs->handle = ((m_nextVertexShader++) << 1) | 1u;
    m_vertexShaders[vs->handle] = vs;
    *pHandle = vs->handle;
    return D3D_OK;
}

HRESULT Device::SetVertexShader(DWORD handle)
{
    if (m_s.vertexShader == handle) return D3D_OK;
    if ((handle & 1) && !FindVertexShader(handle)) return D3DERR_INVALIDCALL;
    m_s.vertexShader = handle;
    m_keyDirty = true;
    m_attribSig = 0;
    if (VertexShaderObject *vs = FindVertexShader(handle))
    {
        // `def` and D3DVSD_CONST constants are loaded into the register file when the shader is set.
        for (int i = 0; i < VS_CONSTANTS; ++i)
            if (vs->defMask[i]) { memcpy(m_s.vsConst[i], vs->defConstants[i], 16); Dirty(G_VSC); }
    }
    return D3D_OK;
}

HRESULT Device::GetVertexShader(DWORD *p)
{
    if (!p) return D3DERR_INVALIDCALL;
    *p = m_s.vertexShader;
    return D3D_OK;
}

HRESULT Device::DeleteVertexShader(DWORD handle)
{
    auto it = m_vertexShaders.find(handle);
    if (it == m_vertexShaders.end()) return D3DERR_INVALIDCALL;
    if (m_s.vertexShader == handle) { m_s.vertexShader = 0; m_keyDirty = true; }
    // Programs built on it keep their own compiled code; drop the cache entries.
    for (auto p = m_programs.begin(); p != m_programs.end();)
    {
        if (p->second->key.vsHandle == handle)
        {
            if (m_curProgram == p->second) m_curProgram = nullptr;
            delete p->second;
            p = m_programs.erase(p);
        }
        else ++p;
    }
    delete it->second;
    m_vertexShaders.erase(it);
    return D3D_OK;
}

HRESULT Device::SetVertexShaderConstant(DWORD reg, const void *data, DWORD count)
{
    if (!data || reg + count > VS_CONSTANTS) return D3DERR_INVALIDCALL;
    ++g_d3d.setShaderConstant;
    memcpy(m_s.vsConst[reg], data, count * 16);
    Dirty(G_VSC);
    return D3D_OK;
}

HRESULT Device::GetVertexShaderConstant(DWORD reg, void *data, DWORD count)
{
    if (!data || reg + count > VS_CONSTANTS) return D3DERR_INVALIDCALL;
    memcpy(data, m_s.vsConst[reg], count * 16);
    return D3D_OK;
}

HRESULT Device::GetVertexShaderDeclaration(DWORD handle, void *data, DWORD *size)
{
    VertexShaderObject *vs = FindVertexShader(handle);
    if (!vs || !size) return D3DERR_INVALIDCALL;
    DWORD bytes = (DWORD)(vs->declaration.size() * sizeof(DWORD));
    if (!data || *size < bytes) { *size = bytes; return D3DERR_MOREDATA; }
    memcpy(data, vs->declaration.data(), bytes);
    *size = bytes;
    return D3D_OK;
}

HRESULT Device::GetVertexShaderFunction(DWORD handle, void *data, DWORD *size)
{
    VertexShaderObject *vs = FindVertexShader(handle);
    if (!vs || !size) return D3DERR_INVALIDCALL;
    DWORD bytes = (DWORD)(vs->bytecode.size() * sizeof(DWORD));
    if (!data || *size < bytes) { *size = bytes; return D3DERR_MOREDATA; }
    memcpy(data, vs->bytecode.data(), bytes);
    *size = bytes;
    return D3D_OK;
}

HRESULT Device::CreatePixelShader(const DWORD *func, DWORD *pHandle)
{
    if (!func || !pHandle) return D3DERR_INVALIDCALL;
    PixelShaderObject *ps = nullptr;
    if (!CreatePixelShaderObject(func, &ps)) return D3DERR_INVALIDCALL;
    ps->handle = m_nextPixelShader++;
    m_pixelShaders[ps->handle] = ps;
    *pHandle = ps->handle;
    return D3D_OK;
}

HRESULT Device::SetPixelShader(DWORD handle)
{
    if (m_s.pixelShader == handle) return D3D_OK;
    if (handle && !FindPixelShader(handle)) return D3DERR_INVALIDCALL;
    m_s.pixelShader = handle;
    m_keyDirty = true;
    return D3D_OK;
}

HRESULT Device::GetPixelShader(DWORD *p)
{
    if (!p) return D3DERR_INVALIDCALL;
    *p = m_s.pixelShader;
    return D3D_OK;
}

HRESULT Device::DeletePixelShader(DWORD handle)
{
    auto it = m_pixelShaders.find(handle);
    if (it == m_pixelShaders.end()) return D3DERR_INVALIDCALL;
    if (m_s.pixelShader == handle) { m_s.pixelShader = 0; m_keyDirty = true; }
    for (auto p = m_programs.begin(); p != m_programs.end();)
    {
        if (p->second->key.psHandle == handle)
        {
            if (m_curProgram == p->second) m_curProgram = nullptr;
            delete p->second;
            p = m_programs.erase(p);
        }
        else ++p;
    }
    delete it->second;
    m_pixelShaders.erase(it);
    return D3D_OK;
}

HRESULT Device::SetPixelShaderConstant(DWORD reg, const void *data, DWORD count)
{
    if (!data || reg + count > PS_CONSTANTS) return D3DERR_INVALIDCALL;
    ++g_d3d.setShaderConstant;
    memcpy(m_s.psConst[reg], data, count * 16);
    Dirty(G_PSC);
    return D3D_OK;
}

HRESULT Device::GetPixelShaderConstant(DWORD reg, void *data, DWORD count)
{
    if (!data || reg + count > PS_CONSTANTS) return D3DERR_INVALIDCALL;
    memcpy(data, m_s.psConst[reg], count * 16);
    return D3D_OK;
}

HRESULT Device::GetPixelShaderFunction(DWORD handle, void *data, DWORD *size)
{
    PixelShaderObject *ps = FindPixelShader(handle);
    if (!ps || !size) return D3DERR_INVALIDCALL;
    DWORD bytes = (DWORD)(ps->bytecode.size() * sizeof(DWORD));
    if (!data || *size < bytes) { *size = bytes; return D3DERR_MOREDATA; }
    memcpy(data, ps->bytecode.data(), bytes);
    *size = bytes;
    return D3D_OK;
}

} // namespace webd3d8
