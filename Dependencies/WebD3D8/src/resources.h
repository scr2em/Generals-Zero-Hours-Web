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
** WebAssembly port: Direct3D 8 resources (buffers, textures, surfaces).
*/
#pragma once

#include "device.h"

namespace webd3d8 {

/// Base of all objects that belong to a device. Holds a reference on the
/// device (as Direct3D 8 does) so GL objects can always be released safely.
class DeviceChild
{
public:
    /// \param takeRef  hold a reference on the device (all user-visible objects do)
    explicit DeviceChild(Device *dev, bool takeRef = true);
    virtual ~DeviceChild();
    /// For objects owned by the device itself: drops the device reference so
    /// there is no ownership cycle.
    void DropDeviceRef();
    Device *GetDeviceInternal() const { return m_device; }

protected:
    HRESULT GetDeviceCommon(IDirect3DDevice8 **ppDevice);
    HRESULT SetPrivateDataCommon(REFGUID guid, const void *data, DWORD size, DWORD flags);
    HRESULT GetPrivateDataCommon(REFGUID guid, void *data, DWORD *size);
    HRESULT FreePrivateDataCommon(REFGUID guid);

    Device *m_device;
    bool m_holdsRef;
    struct PrivateData { std::vector<uint8_t> bytes; };
    std::map<std::string, PrivateData> m_private;
};

/// IDirect3DResource8 methods on top of Unknown<Iface>.
template <class Iface>
class ResourceImpl : public Unknown<Iface>, public DeviceChild
{
public:
    explicit ResourceImpl(Device *dev) : DeviceChild(dev) {}
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice8 **p) override { return GetDeviceCommon(p); }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID g, const void *d, DWORD s, DWORD f) override { return SetPrivateDataCommon(g, d, s, f); }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID g, void *d, DWORD *s) override { return GetPrivateDataCommon(g, d, s); }
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID g) override { return FreePrivateDataCommon(g); }
    DWORD STDMETHODCALLTYPE SetPriority(DWORD p) override { DWORD o = m_priority; m_priority = p; return o; }
    DWORD STDMETHODCALLTYPE GetPriority() override { return m_priority; }
    void STDMETHODCALLTYPE PreLoad() override {}

protected:
    DWORD m_priority = 0;
};

//------------------------------------------------------------------------------
// Buffers
//------------------------------------------------------------------------------
/// Shared CPU-shadow + GL buffer logic of vertex and index buffers.
class BufferStorage : public GLObject
{
public:
    BufferStorage(Device *dev, GLenum target, UINT size, DWORD usage);
    ~BufferStorage() override;
    void OnContextLost() override { m_buf = 0; m_allocated = false; }
    void OnContextRestored() override;
    HRESULT Lock(UINT offset, UINT size, BYTE **ppData, DWORD flags);
    HRESULT Unlock();
    /// Uploads the pending dirty range to the GL buffer. Returns the GL buffer.
    GLuint Flush();
    GLuint Id() const { return m_buf; }
    UINT Size() const { return (UINT)m_data.size(); }
    const uint8_t *Data() const { return m_data.data(); }

private:
    Device *m_dev;
    GLenum m_target;
    GLuint m_buf = 0;
    std::vector<uint8_t> m_data;
    DWORD m_usage;
    bool m_locked = false;
    UINT m_lockOffset = 0, m_lockSize = 0;
    DWORD m_lockFlags = 0;
    UINT m_dirtyBegin = 0, m_dirtyEnd = 0; // [begin,end) pending upload
    bool m_allocated = false;
};

class VertexBuffer final : public ResourceImpl<IDirect3DVertexBuffer8>
{
public:
    VertexBuffer(Device *dev, UINT length, DWORD usage, DWORD fvf, D3DPOOL pool);
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_VERTEXBUFFER; }
    HRESULT STDMETHODCALLTYPE Lock(UINT o, UINT s, BYTE **pp, DWORD f) override { return m_storage.Lock(o, s, pp, f); }
    HRESULT STDMETHODCALLTYPE Unlock() override { return m_storage.Unlock(); }
    HRESULT STDMETHODCALLTYPE GetDesc(D3DVERTEXBUFFER_DESC *pDesc) override;
    GLuint Flush() { return m_storage.Flush(); }
    DWORD FVF() const { return m_fvf; }
    BufferStorage m_storage;
private:
    DWORD m_fvf, m_usage;
    D3DPOOL m_pool;
};

class IndexBuffer final : public ResourceImpl<IDirect3DIndexBuffer8>
{
public:
    IndexBuffer(Device *dev, UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool);
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_INDEXBUFFER; }
    HRESULT STDMETHODCALLTYPE Lock(UINT o, UINT s, BYTE **pp, DWORD f) override { return m_storage.Lock(o, s, pp, f); }
    HRESULT STDMETHODCALLTYPE Unlock() override { return m_storage.Unlock(); }
    HRESULT STDMETHODCALLTYPE GetDesc(D3DINDEXBUFFER_DESC *pDesc) override;
    GLuint Flush() { return m_storage.Flush(); }
    D3DFORMAT Format() const { return m_format; }
    BufferStorage m_storage;
private:
    D3DFORMAT m_format;
    DWORD m_usage;
    D3DPOOL m_pool;
};

//------------------------------------------------------------------------------
// Textures
//------------------------------------------------------------------------------
struct LevelData
{
    uint32_t width = 0, height = 0, depth = 1;
    uint32_t pitch = 0;      // bytes per row (or block row)
    uint32_t slicePitch = 0; // bytes per slice
    std::vector<uint8_t> shadow;
    bool shadowValid = false; ///< shadow holds the current contents
    bool glValid = false;     ///< something was uploaded to GL
    bool locked = false;
    DWORD lockFlags = 0;
    RECT lockRect = {};
    D3DBOX lockBox = {};
};

/// Storage common to 2D, cube and volume textures.
class TextureBase : public GLObject
{
public:
    TextureBase(Device *dev, D3DRESOURCETYPE type, UINT w, UINT h, UINT d, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool);
    virtual ~TextureBase();
    void OnContextLost() override { m_tex = 0; m_baseLevel = 0; /* m_glAllocated stays: the object had GL storage */ }
    void OnContextRestored() override;

    bool Valid() const { return m_valid; }
    bool EnsureGL();

    HRESULT LockLevel(UINT face, UINT level, D3DLOCKED_RECT *lr, D3DLOCKED_BOX *lb, const RECT *rect, const D3DBOX *box, DWORD flags);
    HRESULT UnlockLevel(UINT face, UINT level);
    /// Writes pixels already in this texture's format into a level (used by CopyRects etc.).
    bool WriteRect(UINT face, UINT level, const RECT &rc, const void *src, UINT srcPitch);
    /// Reads pixels in this texture's own format.
    bool ReadRect(UINT face, UINT level, const RECT &rc, void *dst, UINT dstPitch);
    void UploadWhole(UINT face, UINT level);
    GLenum FaceTarget(UINT face) const;
    LevelData &LevelAt(UINT face, UINT level) { return m_levels[face * m_levelCount + level]; }
    uint32_t LevelWidth(UINT level) const { return Max<uint32_t>(1, m_width >> level); }
    uint32_t LevelHeight(UINT level) const { return Max<uint32_t>(1, m_height >> level); }

    Device *m_dev;
    D3DRESOURCETYPE m_type;
    D3DFORMAT m_format;
    DWORD m_usage;
    D3DPOOL m_pool;
    uint32_t m_width, m_height, m_depth, m_levelCount, m_faces;
    GLenum m_target;
    GLuint m_tex = 0;
    const FormatInfo *m_info = nullptr;
    bool m_decode = false;       ///< DXT decoded to RGBA8 on upload
    bool m_isRT = false;
    DWORD m_lod = 0;
    GLint m_baseLevel = 0;
    uint32_t m_uniqueId;
    std::vector<LevelData> m_levels;

protected:
    bool m_valid = false;
    void UploadRegion(UINT face, UINT level, const RECT *rc, const D3DBOX *box);
    bool ReadbackLevel(UINT face, UINT level);
    void AllocateShadow(LevelData &lv);
    void ReleaseGL();
    std::vector<uint8_t> m_scratch;
    bool m_glAllocated = false;
    friend class Surface;
};

class Texture2D final : public ResourceImpl<IDirect3DTexture8>, public TextureBase
{
public:
    Texture2D(Device *dev, UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool);
    ~Texture2D() override;
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_TEXTURE; }
    DWORD STDMETHODCALLTYPE SetLOD(DWORD l) override { DWORD o = m_lod; m_lod = l; return o; }
    DWORD STDMETHODCALLTYPE GetLOD() override { return m_lod; }
    DWORD STDMETHODCALLTYPE GetLevelCount() override { return m_levelCount; }
    HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT Level, D3DSURFACE_DESC *pDesc) override;
    HRESULT STDMETHODCALLTYPE GetSurfaceLevel(UINT Level, IDirect3DSurface8 **ppSurfaceLevel) override;
    HRESULT STDMETHODCALLTYPE LockRect(UINT Level, D3DLOCKED_RECT *pLockedRect, const RECT *pRect, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE UnlockRect(UINT Level) override;
    HRESULT STDMETHODCALLTYPE AddDirtyRect(const RECT *) override { return D3D_OK; }
    Surface *LevelSurface(UINT level);
private:
    std::vector<Surface *> m_surfaces;
};

class CubeTexture final : public ResourceImpl<IDirect3DCubeTexture8>, public TextureBase
{
public:
    CubeTexture(Device *dev, UINT edge, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool);
    ~CubeTexture() override;
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_CUBETEXTURE; }
    DWORD STDMETHODCALLTYPE SetLOD(DWORD l) override { DWORD o = m_lod; m_lod = l; return o; }
    DWORD STDMETHODCALLTYPE GetLOD() override { return m_lod; }
    DWORD STDMETHODCALLTYPE GetLevelCount() override { return m_levelCount; }
    HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT Level, D3DSURFACE_DESC *pDesc) override;
    HRESULT STDMETHODCALLTYPE GetCubeMapSurface(D3DCUBEMAP_FACES FaceType, UINT Level, IDirect3DSurface8 **ppCubeMapSurface) override;
    HRESULT STDMETHODCALLTYPE LockRect(D3DCUBEMAP_FACES FaceType, UINT Level, D3DLOCKED_RECT *pLockedRect, const RECT *pRect, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE UnlockRect(D3DCUBEMAP_FACES FaceType, UINT Level) override;
    HRESULT STDMETHODCALLTYPE AddDirtyRect(D3DCUBEMAP_FACES, const RECT *) override { return D3D_OK; }
    Surface *LevelSurface(UINT face, UINT level);
private:
    std::vector<Surface *> m_surfaces;
};

class Volume;
class VolumeTexture final : public ResourceImpl<IDirect3DVolumeTexture8>, public TextureBase
{
public:
    VolumeTexture(Device *dev, UINT w, UINT h, UINT d, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool);
    ~VolumeTexture() override;
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_VOLUMETEXTURE; }
    DWORD STDMETHODCALLTYPE SetLOD(DWORD l) override { DWORD o = m_lod; m_lod = l; return o; }
    DWORD STDMETHODCALLTYPE GetLOD() override { return m_lod; }
    DWORD STDMETHODCALLTYPE GetLevelCount() override { return m_levelCount; }
    HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT Level, D3DVOLUME_DESC *pDesc) override;
    HRESULT STDMETHODCALLTYPE GetVolumeLevel(UINT Level, IDirect3DVolume8 **ppVolumeLevel) override;
    HRESULT STDMETHODCALLTYPE LockBox(UINT Level, D3DLOCKED_BOX *pLockedVolume, const D3DBOX *pBox, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE UnlockBox(UINT Level) override;
    HRESULT STDMETHODCALLTYPE AddDirtyBox(const D3DBOX *) override { return D3D_OK; }
private:
    std::vector<Volume *> m_volumes;
};

/// A level of a volume texture.
class Volume final : public Unknown<IDirect3DVolume8>
{
public:
    Volume(VolumeTexture *parent, UINT level);
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice8 **pp) override;
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void *, DWORD, DWORD) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void *, DWORD *) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE GetContainer(REFIID, void **pp) override;
    HRESULT STDMETHODCALLTYPE GetDesc(D3DVOLUME_DESC *pDesc) override;
    HRESULT STDMETHODCALLTYPE LockBox(D3DLOCKED_BOX *pLockedVolume, const D3DBOX *pBox, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE UnlockBox() override;
protected:
    void OnZeroRefs() override {}
private:
    VolumeTexture *m_parent;
    UINT m_level;
};

//------------------------------------------------------------------------------
// Surfaces
//------------------------------------------------------------------------------
class Surface final : public Unknown<IDirect3DSurface8>, public DeviceChild, public GLObject
{
public:
    enum Kind { Level, Image, RenderTarget, DepthStencil, BackBuffer };

    /// Surface of a texture level; lifetime is tied to the texture.
    Surface(TextureBase *tex, IUnknown *owner, UINT face, UINT level);
    /// Standalone surface. `samples` > 1 makes a multisampled render target / depth buffer.
    Surface(Device *dev, Kind kind, UINT w, UINT h, D3DFORMAT fmt, int samples = 0);
    ~Surface() override;
    void OnContextLost() override { m_rb = 0; }
    void OnContextRestored() override;

    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice8 **p) override { return GetDeviceCommon(p); }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID g, const void *d, DWORD s, DWORD f) override { return SetPrivateDataCommon(g, d, s, f); }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID g, void *d, DWORD *s) override { return GetPrivateDataCommon(g, d, s); }
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID g) override { return FreePrivateDataCommon(g); }
    HRESULT STDMETHODCALLTYPE GetContainer(REFIID riid, void **ppContainer) override;
    HRESULT STDMETHODCALLTYPE GetDesc(D3DSURFACE_DESC *pDesc) override;
    HRESULT STDMETHODCALLTYPE LockRect(D3DLOCKED_RECT *pLockedRect, const RECT *pRect, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE UnlockRect() override;

    // Internal
    Kind GetKind() const { return m_kind; }
    D3DFORMAT Format() const { return m_format; }
    UINT Width() const { return m_width; }
    UINT Height() const { return m_height; }
    bool IsRenderable() const { return m_kind == RenderTarget || m_kind == BackBuffer || (m_kind == Level && m_tex && m_tex->m_isRT); }
    uint32_t UniqueId() const { return m_id; }
    TextureBase *Texture() const { return m_tex; }
    UINT Face() const { return m_face; }
    UINT MipLevel() const { return m_level; }
    GLuint Renderbuffer() const { return m_rb; }
    /// Samples per pixel (0 = not multisampled).
    int Samples() const;
    /// Attaches this surface to the currently bound framebuffer unless `cachedKey` (the value returned
    /// by the previous call for that framebuffer) says it already is. Returns the new key.
    uint64_t AttachColor(uint64_t cachedKey);
    uint64_t AttachDepth(bool &hasStencil, uint64_t cachedKey);
    /// Reads a rectangle into `dst` converted to `dstFormat`.
    bool ReadPixelsTo(const RECT &rc, D3DFORMAT dstFormat, void *dst, UINT dstPitch);
    /// Writes a rectangle from `src` (in `srcFormat`) at the position of rc.
    bool WritePixelsFrom(const RECT &rc, D3DFORMAT srcFormat, const void *src, UINT srcPitch);
    void SetPool(D3DPOOL p) { m_pool = p; }
    void SetUsage(DWORD u) { m_usage = u; }

protected:
    void OnZeroRefs() override;

private:
    Kind m_kind;
    D3DFORMAT m_format;
    UINT m_width, m_height;
    D3DPOOL m_pool = D3DPOOL_DEFAULT;
    DWORD m_usage = 0;
    TextureBase *m_tex = nullptr;
    IUnknown *m_owner = nullptr;
    UINT m_face = 0, m_level = 0;
    std::vector<uint8_t> m_data; // Image surfaces
    GLuint m_rb = 0;             // RenderTarget/DepthStencil
    int m_samples = 0;
    bool m_locked = false;
    DWORD m_lockFlags = 0;
    RECT m_lockRect = {};
    std::vector<uint8_t> m_lockTemp; // render-target read-back
    uint32_t m_id;
};

} // namespace webd3d8
