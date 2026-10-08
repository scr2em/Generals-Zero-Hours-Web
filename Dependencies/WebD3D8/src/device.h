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
** WebAssembly port: the IDirect3DDevice8 implementation on WebGL2.
**
** Coordinate conventions used throughout (see also fixedfunction.cpp):
**  - Everything is rendered "flipped": row 0 of every framebuffer attachment is
**    the top row of the image, exactly like D3D surfaces, so that render
**    targets sampled as textures and surface read-backs need no vertical
**    flipping. The vertex stage negates clip-space Y to achieve that and the
**    winding order is adjusted accordingly. Present() flips the back buffer
**    when it is drawn to the canvas.
**  - Clip space depth is converted from D3D's [0,w] to GL's [-w,w] in the
**    vertex shader, and half a pixel is added to emulate D3D's pixel centers.
*/
#pragma once

#include "common.h"
#include "diag.h"
#include "format.h"

namespace webd3d8 {

class Device;
class Surface;
class TextureBase;
class VertexBuffer;
class IndexBuffer;
class Program;
class VertexShaderObject;
class PixelShaderObject;

enum
{
    MAX_STAGES = 8,
    MAX_STREAMS = 16,
    MAX_SHADER_LIGHTS = 8,
    MAX_CLIP_PLANES = 6,
    VS_CONSTANTS = 96,
    PS_CONSTANTS = 32,
    RS_COUNT = 256,
    TSS_COUNT = 33,
    UPLOAD_UNIT = 8, ///< texture unit used for resource uploads (stages use 0..7)
};

/// Base of everything that owns WebGL objects. The device keeps a list of them so that, when the
/// WebGL context is lost and a new one is created, every buffer, texture and renderbuffer can be
/// recreated from its system memory copy (see Device::RestoreContext()).
class GLObject
{
public:
    explicit GLObject(Device *dev);
    virtual ~GLObject();
    /// The context is gone: forget the GL names (the objects are invalid).
    virtual void OnContextLost() {}
    /// A new context is current: create the GL objects again and re-upload what is known.
    virtual void OnContextRestored() = 0;

private:
    friend class Device;
    Device *m_glDevice;
    GLObject *m_glPrev = nullptr, *m_glNext = nullptr;
};

/// Groups of uniforms that change together. The device keeps one version counter per group and
/// every program remembers the version it last received, so a state change re-uploads only the
/// uniforms that depend on it, and only into the programs that use them.
enum UniformGroup
{
    G_XFORM,    ///< world/view/projection derived matrices (wvp, wv, world, normal matrix)
    G_TEXMAT,   ///< texture transforms
    G_VIEWPORT, ///< pixel size, viewport, depth range
    G_CLIP,     ///< user clip planes
    G_LIGHTS,   ///< light parameters (view space)
    G_MATERIAL,
    G_AMBIENT,
    G_FOG,
    G_TFACTOR,
    G_ALPHAREF,
    G_POINT,    ///< point size parameters
    G_LOD,      ///< mip LOD biases
    G_BUMP,     ///< bump environment matrices and luminance scale/offset
    G_BORDER,   ///< texture border colors
    G_VSC,      ///< vertex shader constants
    G_PSC,      ///< pixel shader constants
    G_COUNT
};

/// Capabilities of the GL implementation.
struct GLCaps
{
    GLint maxTextureSize = 2048;
    GLint maxCubeSize = 2048;
    GLint max3DSize = 256;
    GLint maxVertexAttribs = 16;
    GLint maxVertexUniformVectors = 256;
    GLint maxTextureUnits = 16;
    float maxAnisotropy = 1.0f;
    float maxPointSize = 1.0f;
    GLint maxSamples = 1;
    GLint uniformAlignment = 256;
    bool s3tc = false;
    bool anisotropic = false;
    bool provokingVertex = false;   ///< WEBGL_provoking_vertex
    bool baseVertex = false;        ///< WEBGL_draw_instanced_base_vertex_base_instance
    std::string renderer = "WebGL2";
    std::string vendor = "WebGL";
    std::string version;
};

//------------------------------------------------------------------------------
// Vertex declarations
//------------------------------------------------------------------------------
struct VertexElement
{
    uint8_t reg;         ///< D3DVSDE_* register / attribute location
    uint8_t stream;
    uint16_t offset;
    uint8_t size;        ///< components
    uint8_t glType;      ///< 0 = float, 1 = ubyte, 2 = short
    uint8_t normalized;
    uint8_t d3dColor;    ///< D3DCOLOR: swizzled to RGBA by the vertex shader
};

struct VertexLayout
{
    VertexElement elems[16];
    int count = 0;
    uint16_t stride[MAX_STREAMS] = {};
    uint32_t id = 0;      ///< unique id for caching
    DWORD fvf = 0;        ///< non-zero when built from an FVF
};

const VertexLayout *LayoutFromFVF(DWORD fvf);
/// Parses a D3DVSD_* declaration. Returns false when invalid.
bool ParseVertexDeclaration(const DWORD *decl, VertexLayout &out, std::vector<float> *constants /* c0.. as (addr,x,y,z,w) */ = nullptr);
UINT FVFVertexSize(DWORD fvf);

//------------------------------------------------------------------------------
// State
//------------------------------------------------------------------------------
struct LightState
{
    bool defined = false;
    bool enabled = false;
    D3DLIGHT8 light = {};
};

struct StreamState
{
    Ref<IDirect3DVertexBuffer8> vb;
    UINT stride = 0;
};

/// Everything that state blocks capture.
struct DeviceState
{
    DWORD rs[RS_COUNT];
    DWORD tss[MAX_STAGES][TSS_COUNT];
    Mat4 world[4];
    Mat4 view, proj, tex[MAX_STAGES];
    D3DMATERIAL8 material;
    std::vector<LightState> lights;
    D3DVIEWPORT8 viewport;
    float clipPlanes[MAX_CLIP_PLANES][4];
    Ref<IDirect3DBaseTexture8> textures[MAX_STAGES];
    DWORD vertexShader = 0; ///< FVF code or shader handle
    DWORD pixelShader = 0;
    StreamState streams[MAX_STREAMS];
    Ref<IDirect3DIndexBuffer8> indices;
    UINT baseVertexIndex = 0;
    float vsConst[VS_CONSTANTS][4];
    float psConst[PS_CONSTANTS][4];
    DeviceState();
};

/// Vertex array objects are cached by layout, buffers and strides (not by the base vertex, see
/// Device::BindAttributes).
enum { VAO_KEY_STREAMS = 4, VAO_CACHE_LIMIT = 384 };
struct VaoKey
{
    uint32_t layout;
    uint32_t pointerBase;
    uint32_t buf[VAO_KEY_STREAMS];
    uint16_t stride[VAO_KEY_STREAMS];
    uint8_t stream[VAO_KEY_STREAMS];
};
struct VaoEntry
{
    VaoKey key;
    GLuint vao = 0;
    GLuint element = 0xFFFFFFFF; ///< element array buffer bound in this vertex array
    uint32_t enabled = 0;
    uint64_t lastUse = 0;
};

/// The GL pipeline state derived from the D3D render states.
struct PipelineState
{
    bool blend, depthTest, depthMask, cull, stencilTest, scissor, polyOffset;
    GLenum srcBlend, dstBlend, blendOp, depthFunc, frontFace, stencilFunc, stencilFail, stencilZFail, stencilPass;
    GLint stencilRef;
    GLuint stencilMask, stencilWriteMask;
    uint8_t colorMask; // bits r,g,b,a
    float depthNear, depthFar;
    float polyFactor, polyUnits;
    GLint vp[4];
    GLint sc[4];
    void Invalidate();
};

class Device final : public Unknown<IDirect3DDevice8>
{
public:
    static Device *Create(IDirect3D8 *d3d, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD behavior,
                          D3DPRESENT_PARAMETERS *pp);
    ~Device();

    //-- IDirect3DDevice8 --------------------------------------------------------
    HRESULT STDMETHODCALLTYPE TestCooperativeLevel() override;
    UINT STDMETHODCALLTYPE GetAvailableTextureMem() override;
    HRESULT STDMETHODCALLTYPE ResourceManagerDiscardBytes(DWORD Bytes) override;
    HRESULT STDMETHODCALLTYPE GetDirect3D(IDirect3D8 **ppD3D8) override;
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS8 *pCaps) override;
    HRESULT STDMETHODCALLTYPE GetDisplayMode(D3DDISPLAYMODE *pMode) override;
    HRESULT STDMETHODCALLTYPE GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS *pParameters) override;
    HRESULT STDMETHODCALLTYPE SetCursorProperties(UINT XHotSpot, UINT YHotSpot, IDirect3DSurface8 *pCursorBitmap) override;
    void STDMETHODCALLTYPE SetCursorPosition(UINT XScreenSpace, UINT YScreenSpace, DWORD Flags) override;
    BOOL STDMETHODCALLTYPE ShowCursor(BOOL bShow) override;
    HRESULT STDMETHODCALLTYPE CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS *pPresentationParameters, IDirect3DSwapChain8 **pSwapChain) override;
    HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS *pPresentationParameters) override;
    HRESULT STDMETHODCALLTYPE Present(const RECT *pSourceRect, const RECT *pDestRect, HWND hDestWindowOverride, const RGNDATA *pDirtyRegion) override;
    HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT BackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface8 **ppBackBuffer) override;
    HRESULT STDMETHODCALLTYPE GetRasterStatus(D3DRASTER_STATUS *pRasterStatus) override;
    void STDMETHODCALLTYPE SetGammaRamp(DWORD Flags, const D3DGAMMARAMP *pRamp) override;
    void STDMETHODCALLTYPE GetGammaRamp(D3DGAMMARAMP *pRamp) override;
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture8 **ppTexture) override;
    HRESULT STDMETHODCALLTYPE CreateVolumeTexture(UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DVolumeTexture8 **ppVolumeTexture) override;
    HRESULT STDMETHODCALLTYPE CreateCubeTexture(UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DCubeTexture8 **ppCubeTexture) override;
    HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer8 **ppVertexBuffer) override;
    HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DIndexBuffer8 **ppIndexBuffer) override;
    HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, BOOL Lockable, IDirect3DSurface8 **ppSurface) override;
    HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, IDirect3DSurface8 **ppSurface) override;
    HRESULT STDMETHODCALLTYPE CreateImageSurface(UINT Width, UINT Height, D3DFORMAT Format, IDirect3DSurface8 **ppSurface) override;
    HRESULT STDMETHODCALLTYPE CopyRects(IDirect3DSurface8 *pSourceSurface, const RECT *pSourceRectsArray, UINT cRects, IDirect3DSurface8 *pDestinationSurface, const POINT *pDestPointsArray) override;
    HRESULT STDMETHODCALLTYPE UpdateTexture(IDirect3DBaseTexture8 *pSourceTexture, IDirect3DBaseTexture8 *pDestinationTexture) override;
    HRESULT STDMETHODCALLTYPE GetFrontBuffer(IDirect3DSurface8 *pDestSurface) override;
    HRESULT STDMETHODCALLTYPE SetRenderTarget(IDirect3DSurface8 *pRenderTarget, IDirect3DSurface8 *pNewZStencil) override;
    HRESULT STDMETHODCALLTYPE GetRenderTarget(IDirect3DSurface8 **ppRenderTarget) override;
    HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface8 **ppZStencilSurface) override;
    HRESULT STDMETHODCALLTYPE BeginScene() override;
    HRESULT STDMETHODCALLTYPE EndScene() override;
    HRESULT STDMETHODCALLTYPE Clear(DWORD Count, const D3DRECT *pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil) override;
    HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX *pMatrix) override;
    HRESULT STDMETHODCALLTYPE GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX *pMatrix) override;
    HRESULT STDMETHODCALLTYPE MultiplyTransform(D3DTRANSFORMSTATETYPE, const D3DMATRIX *) override;
    HRESULT STDMETHODCALLTYPE SetViewport(const D3DVIEWPORT8 *pViewport) override;
    HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT8 *pViewport) override;
    HRESULT STDMETHODCALLTYPE SetMaterial(const D3DMATERIAL8 *pMaterial) override;
    HRESULT STDMETHODCALLTYPE GetMaterial(D3DMATERIAL8 *pMaterial) override;
    HRESULT STDMETHODCALLTYPE SetLight(DWORD Index, const D3DLIGHT8 *) override;
    HRESULT STDMETHODCALLTYPE GetLight(DWORD Index, D3DLIGHT8 *) override;
    HRESULT STDMETHODCALLTYPE LightEnable(DWORD Index, BOOL Enable) override;
    HRESULT STDMETHODCALLTYPE GetLightEnable(DWORD Index, BOOL *pEnable) override;
    HRESULT STDMETHODCALLTYPE SetClipPlane(DWORD Index, const float *pPlane) override;
    HRESULT STDMETHODCALLTYPE GetClipPlane(DWORD Index, float *pPlane) override;
    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE State, DWORD Value) override;
    HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE State, DWORD *pValue) override;
    HRESULT STDMETHODCALLTYPE BeginStateBlock() override;
    HRESULT STDMETHODCALLTYPE EndStateBlock(DWORD *pToken) override;
    HRESULT STDMETHODCALLTYPE ApplyStateBlock(DWORD Token) override;
    HRESULT STDMETHODCALLTYPE CaptureStateBlock(DWORD Token) override;
    HRESULT STDMETHODCALLTYPE DeleteStateBlock(DWORD Token) override;
    HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE Type, DWORD *pToken) override;
    HRESULT STDMETHODCALLTYPE SetClipStatus(const D3DCLIPSTATUS8 *pClipStatus) override;
    HRESULT STDMETHODCALLTYPE GetClipStatus(D3DCLIPSTATUS8 *pClipStatus) override;
    HRESULT STDMETHODCALLTYPE GetTexture(DWORD Stage, IDirect3DBaseTexture8 **ppTexture) override;
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD Stage, IDirect3DBaseTexture8 *pTexture) override;
    HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD *pValue) override;
    HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) override;
    HRESULT STDMETHODCALLTYPE ValidateDevice(DWORD *pNumPasses) override;
    HRESULT STDMETHODCALLTYPE GetInfo(DWORD DevInfoID, void *pDevInfoStruct, DWORD DevInfoStructSize) override;
    HRESULT STDMETHODCALLTYPE SetPaletteEntries(UINT PaletteNumber, const PALETTEENTRY *pEntries) override;
    HRESULT STDMETHODCALLTYPE GetPaletteEntries(UINT PaletteNumber, PALETTEENTRY *pEntries) override;
    HRESULT STDMETHODCALLTYPE SetCurrentTexturePalette(UINT PaletteNumber) override;
    HRESULT STDMETHODCALLTYPE GetCurrentTexturePalette(UINT *PaletteNumber) override;
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) override;
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE, UINT minIndex, UINT NumVertices, UINT startIndex, UINT primCount) override;
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, const void *pVertexStreamZeroData, UINT VertexStreamZeroStride) override;
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertexIndices, UINT PrimitiveCount, const void *pIndexData, D3DFORMAT IndexDataFormat, const void *pVertexStreamZeroData, UINT VertexStreamZeroStride) override;
    HRESULT STDMETHODCALLTYPE ProcessVertices(UINT SrcStartIndex, UINT DestIndex, UINT VertexCount, IDirect3DVertexBuffer8 *pDestBuffer, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD *pDeclaration, const DWORD *pFunction, DWORD *pHandle, DWORD Usage) override;
    HRESULT STDMETHODCALLTYPE SetVertexShader(DWORD Handle) override;
    HRESULT STDMETHODCALLTYPE GetVertexShader(DWORD *pHandle) override;
    HRESULT STDMETHODCALLTYPE DeleteVertexShader(DWORD Handle) override;
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstant(DWORD Register, const void *pConstantData, DWORD ConstantCount) override;
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstant(DWORD Register, void *pConstantData, DWORD ConstantCount) override;
    HRESULT STDMETHODCALLTYPE GetVertexShaderDeclaration(DWORD Handle, void *pData, DWORD *pSizeOfData) override;
    HRESULT STDMETHODCALLTYPE GetVertexShaderFunction(DWORD Handle, void *pData, DWORD *pSizeOfData) override;
    HRESULT STDMETHODCALLTYPE SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer8 *pStreamData, UINT Stride) override;
    HRESULT STDMETHODCALLTYPE GetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer8 **ppStreamData, UINT *pStride) override;
    HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer8 *pIndexData, UINT BaseVertexIndex) override;
    HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer8 **ppIndexData, UINT *pBaseVertexIndex) override;
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD *pFunction, DWORD *pHandle) override;
    HRESULT STDMETHODCALLTYPE SetPixelShader(DWORD Handle) override;
    HRESULT STDMETHODCALLTYPE GetPixelShader(DWORD *pHandle) override;
    HRESULT STDMETHODCALLTYPE DeletePixelShader(DWORD Handle) override;
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstant(DWORD Register, const void *pConstantData, DWORD ConstantCount) override;
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstant(DWORD Register, void *pConstantData, DWORD ConstantCount) override;
    HRESULT STDMETHODCALLTYPE GetPixelShaderFunction(DWORD Handle, void *pData, DWORD *pSizeOfData) override;
    HRESULT STDMETHODCALLTYPE DrawRectPatch(UINT Handle, const float *pNumSegs, const D3DRECTPATCH_INFO *pRectPatchInfo) override;
    HRESULT STDMETHODCALLTYPE DrawTriPatch(UINT Handle, const float *pNumSegs, const D3DTRIPATCH_INFO *pTriPatchInfo) override;
    HRESULT STDMETHODCALLTYPE DeletePatch(UINT Handle) override;

    //-- Services for the resource classes ----------------------------------------
    const GLCaps &Caps() const { return m_glcaps; }
    /// True while the WebGL context is lost (all GL calls are no-ops).
    bool IsContextLost() const { return m_contextLost; }
    /// Polls the context state (cheap); sets the lost / needs-reset state. Returns true when usable.
    bool CheckContext();
    void RegisterGL(GLObject *o);
    void UnregisterGL(GLObject *o);
    /// Sample count of the back buffer (0 = no multisampling).
    int BackBufferSamples() const { return m_samples; }
    GLuint BackBufferRenderbuffer() const { return m_bbMsRb; }
    /// Resolves the multisampled back buffer into BackBufferTexture() when it was drawn to.
    void ResolveBackBuffer();
    void MarkBackBufferDirty() { m_bbDirty = true; }
    /// Binds a texture to the dedicated upload unit.
    void BindForUpload(GLenum target, GLuint tex);
    /// Must be called before deleting a GL texture so cached bindings are dropped.
    void ForgetTexture(GLuint tex);
    void ForgetBuffer(GLuint buf);
    /// Reads the RGBA8 contents of a texture level (or of the current read framebuffer).
    bool ReadTextureLevel(GLuint tex, GLenum target, GLenum faceTarget, int level, uint32_t w, uint32_t h,
                          const RECT &rc, uint8_t *rgbaOut);
    bool ReadRenderbuffer(GLuint rb, uint32_t w, uint32_t h, const RECT &rc, uint8_t *rgbaOut);
    GLuint ScratchFramebuffer() const { return m_scratchFbo; }
    /// Binds a buffer to ARRAY_BUFFER / ELEMENT_ARRAY_BUFFER through the state cache.
    void BindBuffer(GLenum target, GLuint buf)
    {
        // The element array binding belongs to the vertex array object that is bound.
        GLuint &cached = target == GL_ARRAY_BUFFER ? m_boundArrayBuffer : (m_curVao ? m_curVao->element : m_boundElementBuffer);
        if (cached != buf) { glBindBuffer(target, buf); cached = buf; }
    }
    Surface *BackBufferSurface() const { return m_backBuffer; }
    bool IsDecodingDXT() const { return !m_glcaps.s3tc; }
    GLuint BackBufferTexture() const { return m_bbColor; }

    //-- Shader objects -----------------------------------------------------------
    VertexShaderObject *FindVertexShader(DWORD handle);
    PixelShaderObject *FindPixelShader(DWORD handle);

private:
    Device() = default;
    bool Initialize(IDirect3D8 *d3d, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD behavior, D3DPRESENT_PARAMETERS *pp);
    void OnZeroRefs() override;

    // device.cpp
    bool CreateContext(const D3DPRESENT_PARAMETERS &pp);
    void QueryGLCaps();
    void EnableExtensions();
    void CreateGLBaseObjects();
    void DestroyGLBaseObjects(bool contextAlive);
    bool CreateBackBuffer(const D3DPRESENT_PARAMETERS &pp);
    void DestroyBackBuffer();
    void ResetState();
    void PresentToCanvas();
    void CreatePresentProgram();
    void InstallContextListeners();
    void OnContextLostInternal();
    bool RestoreContext();
    void ResetGLCaches();
    void TickTestContextLoss();
public:
    /// Arms the test aid so that the next Present() drops the context (see WebD3D8_LoseContextNow).
    void StartTestContextLoss() { m_loseCountdown = 1; m_loseArmed = true; }
private:

    // state application (device_draw.cpp)
    bool PrepareDraw(GLenum mode);
    void DiagCheckDrawState(GLenum mode);
    void ApplyRenderTargets();
    void ApplyPipeline();
    void BindTextures();
    bool BindAttributes(UINT pointerBase, bool userData, UINT userStride);
    void SetAttributePointers(const VertexLayout *layout, const GLuint *bufs, const UINT *strides, UINT pointerBase,
                              uint32_t oldEnabled, uint32_t *newEnabled);
    void EvictVao();
    void DeleteVao(uint64_t hash, struct VaoEntry *e);
    void ClearVaoCache(bool contextAlive);
    bool SelectProgram();
    void UploadUniforms();
    size_t UploadStream(const void *data, size_t size, bool index, size_t align);
    PipelineState DerivePipeline() const;
    void FlushBuffers();
    GLuint SamplerFor(int stage, TextureBase *tex);
    void BuildProgramKey(struct ProgramKey &key);
    bool DrawCommon(D3DPRIMITIVETYPE type, UINT primCount, bool indexed, UINT start, UINT baseVertex,
                    const void *userVerts, UINT userStride, const void *userIndices, D3DFORMAT userIndexFmt, UINT userVertCount);

    // transforms/lights/derived data
    void UpdateDerivedMatrices();
    void TransformChanged(D3DTRANSFORMSTATETYPE state);

    friend class Surface;
    friend class TextureBase;
    friend class VertexBuffer;
    friend class IndexBuffer;
    friend struct ProgramUniformSetter;

public:
    //-- Data (public for the helpers in the same library) --------------------------
    IDirect3D8 *m_d3d = nullptr;
    D3DDEVICE_CREATION_PARAMETERS m_creation = {};
    D3DPRESENT_PARAMETERS m_pp = {};
    int m_glContext = 0;
    bool m_contextLost = false;
    bool m_needsReset = false;          ///< a new context exists; the application has to Reset() the device
    volatile int m_ctxEvent = 0;        ///< set by the canvas event listeners: 1 = lost, 2 = restored
    GLObject *m_glObjects = nullptr;    ///< every object that owns WebGL names (see GLObject)
    int m_loseCountdown = 0, m_restoreCountdown = 0; ///< test aid (WebD3D8_LoseContextAfterFrames)
    bool m_loseArmed = false;
    int m_samples = 0;                  ///< multisampling of the back buffer (0 = off)
    GLuint m_bbMsRb = 0;                ///< multisampled colour renderbuffer (when m_samples)
    GLuint m_resolveRead = 0, m_resolveDraw = 0;
    bool m_bbDirty = false;
    struct SubDepth { GLuint rb; uint32_t w, h; int samples; bool stencil; };
    std::vector<SubDepth> m_subDepth;   ///< depth buffers standing in for ones whose sample count mismatches the target
    unsigned m_presentCounter = 0;	// Present calls, for the debug log
    bool m_explicitSwap = false;
    GLCaps m_glcaps;
    bool m_inScene = false;

    DeviceState m_s;
    std::vector<DeviceState> m_stateBlocks; // token = index + 1; empty slots have valid=false
    std::vector<bool> m_stateBlockValid;
    bool m_recordingBlock = false;

    // Targets.
    Surface *m_backBuffer = nullptr;
    Surface *m_defaultDepth = nullptr;
    Ref<IDirect3DSurface8> m_curRT, m_curDS;
    GLuint m_bbColor = 0;
    GLuint m_fbo = 0, m_scratchFbo = 0;
    bool m_targetsDirty = true;
    uint64_t m_fboColorKey = 0, m_fboDepthKey = 0;   ///< what m_fbo currently has attached (see Surface::AttachColor)
    GLuint m_boundFbo = 0xFFFFFFFF;                  ///< framebuffer bound for drawing
    uint32_t m_rtWidth = 0, m_rtHeight = 0;

    // Present
    GLuint m_presentProgram = 0, m_presentVao = 0, m_gammaTex = 0;
    GLint m_presentGammaLoc = -1, m_presentTexLoc = -1;
    D3DGAMMARAMP m_gamma;
    bool m_gammaActive = false;
    bool m_gammaDirty = false;

    // GL state caches.
    GLuint m_vao = 0;               ///< scratch vertex array (uncached draws)
    std::unordered_multimap<uint64_t, VaoEntry *> m_vaoCache;
    VaoEntry *m_curVao = nullptr;   ///< bound cached vertex array, null when the scratch one is bound
    std::unordered_map<uint64_t, uint32_t> m_vaoSeen; ///< keys with a base vertex seen once (promoted on the second sighting)
    uint64_t m_vaoClock = 0;
    GLuint m_boundProgram = 0xFFFFFFFF;
    GLuint m_boundArrayBuffer = 0xFFFFFFFF;
    GLuint m_boundElementBuffer = 0xFFFFFFFF;
    GLuint m_boundTex[MAX_STAGES + 1][3];
    GLuint m_boundSampler[MAX_STAGES];
    int m_activeUnit = -1;
    GLuint m_whiteTex[3] = {};
    uint32_t m_enabledAttribs = 0;  ///< arrays enabled in the scratch vertex array
    PipelineState m_applied;
    bool m_pipelineValid = false;
    uint64_t m_attribSig = 0;

    // Programs.
    std::unordered_map<std::string, Program *> m_programs;
    Program *m_curProgram = nullptr;
    bool m_keyDirty = true;
    std::unordered_map<std::string, GLuint> m_samplers;

    // Versions of the uniform groups (see UniformGroup); a program re-uploads a group when its own
    // copy of the version differs.
    uint32_t m_ver[G_COUNT] = {};
    void Dirty(UniformGroup g) { ++m_ver[g]; }
    void DirtyAllUniforms() { for (uint32_t &v : m_ver) ++v; }

    // Derived per-draw data.
    Mat4 m_wv, m_wvp, m_normalMat3Src;
    float m_nm[9];
    bool m_derivedDirty = true;

    // Streaming buffers for *UP draws.
    GLuint m_streamVB = 0, m_streamIB = 0;
    size_t m_streamVBSize = 0, m_streamIBSize = 0, m_streamVBPos = 0, m_streamIBPos = 0;

    // Shaders.
    std::unordered_map<DWORD, VertexShaderObject *> m_vertexShaders;
    std::unordered_map<DWORD, PixelShaderObject *> m_pixelShaders;
    DWORD m_nextVertexShader = 1, m_nextPixelShader = 1;

    // Scratch.
    std::vector<uint8_t> m_scratch;
    std::vector<uint16_t> m_wireIndices;

    // Cursor (not shown; the browser draws the cursor).
    BOOL m_cursorVisible = FALSE;

    DWORD m_texLodSet[MAX_STAGES] = {};
    uint32_t m_samplerDirtyMask = 0xFF;
    GLuint m_stageSampler[MAX_STAGES] = {};
    bool m_drawingPoints = false;
    bool m_curProgramPoints = false;
    GLuint m_attachedColorTex = 0;
    GLuint m_whiteSampler = 0;
    float m_presentGammaValue = -1.0f;
    float m_clearColor[4] = {};
    float m_clearDepth = 0;
    GLint m_clearStencil = 0;
    bool m_clearColorValid = false, m_clearDepthValid = false, m_clearStencilValid = false;
    unsigned m_drawCounter = 0;
};

} // namespace webd3d8
