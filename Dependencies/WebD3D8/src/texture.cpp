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
** WebAssembly port: textures, surfaces and volumes.
**
** Every texture level keeps a system-memory "shadow" copy in the D3D memory
** layout. LockRect hands out pointers into it and UnlockRect uploads the
** modified region to GL, converting to a GL-compatible layout (see
** PrepareForGL). Render targets and DEFAULT pool textures only materialize a
** shadow when they are locked, by reading the GL texture back.
*/
#include "resources.h"

namespace webd3d8 {

static uint32_t g_nextId = 1;

//------------------------------------------------------------------------------
// DeviceChild
//------------------------------------------------------------------------------
DeviceChild::DeviceChild(Device *dev, bool takeRef) : m_device(dev), m_holdsRef(takeRef)
{
    if (takeRef && dev) dev->AddRef();
}

DeviceChild::~DeviceChild()
{
    if (m_holdsRef && m_device) m_device->Release();
}

void DeviceChild::DropDeviceRef()
{
    if (m_holdsRef && m_device) m_device->Release();
    m_holdsRef = false;
}

HRESULT DeviceChild::GetDeviceCommon(IDirect3DDevice8 **ppDevice)
{
    if (!ppDevice) return D3DERR_INVALIDCALL;
    *ppDevice = m_device;
    m_device->AddRef();
    return D3D_OK;
}

static std::string GuidKey(REFGUID g) { return std::string(reinterpret_cast<const char *>(&g), sizeof(GUID)); }

HRESULT DeviceChild::SetPrivateDataCommon(REFGUID guid, const void *data, DWORD size, DWORD)
{
    PrivateData &pd = m_private[GuidKey(guid)];
    pd.bytes.assign(static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + size);
    return D3D_OK;
}

HRESULT DeviceChild::GetPrivateDataCommon(REFGUID guid, void *data, DWORD *size)
{
    auto it = m_private.find(GuidKey(guid));
    if (it == m_private.end() || !size) return D3DERR_NOTFOUND;
    if (!data || *size < it->second.bytes.size())
    {
        *size = (DWORD)it->second.bytes.size();
        return D3DERR_MOREDATA;
    }
    memcpy(data, it->second.bytes.data(), it->second.bytes.size());
    *size = (DWORD)it->second.bytes.size();
    return D3D_OK;
}

HRESULT DeviceChild::FreePrivateDataCommon(REFGUID guid)
{
    return m_private.erase(GuidKey(guid)) ? D3D_OK : D3DERR_NOTFOUND;
}

//------------------------------------------------------------------------------
// TextureBase
//------------------------------------------------------------------------------
TextureBase::TextureBase(Device *dev, D3DRESOURCETYPE type, UINT w, UINT h, UINT d, UINT levels,
                         DWORD usage, D3DFORMAT fmt, D3DPOOL pool)
    : m_dev(dev), m_type(type), m_format(fmt), m_usage(usage), m_pool(pool),
      m_width(w), m_height(h), m_depth(d), m_uniqueId(g_nextId++)
{
    m_info = GetFormatInfo(fmt);
    if (!m_info || !m_info->texture || w == 0 || h == 0 || d == 0) return;
    if (type == D3DRTYPE_CUBETEXTURE) { m_faces = 6; m_target = GL_TEXTURE_CUBE_MAP; }
    else if (type == D3DRTYPE_VOLUMETEXTURE) { m_faces = 1; m_target = GL_TEXTURE_3D; }
    else { m_faces = 1; m_target = GL_TEXTURE_2D; }
    if (type == D3DRTYPE_CUBETEXTURE) m_height = m_width;

    const GLCaps &caps = dev->Caps();
    GLint maxSize = (type == D3DRTYPE_CUBETEXTURE) ? caps.maxCubeSize : (type == D3DRTYPE_VOLUMETEXTURE ? caps.max3DSize : caps.maxTextureSize);
    if ((GLint)m_width > maxSize || (GLint)m_height > maxSize || (GLint)m_depth > caps.max3DSize) return;

    uint32_t full = FullMipCount(m_width, m_height, m_depth);
    m_levelCount = (levels == 0 || levels > full) ? full : levels;
    m_isRT = (usage & D3DUSAGE_RENDERTARGET) != 0;
    if (m_isRT && !m_info->renderTarget) return;
    m_decode = m_info->conv == UploadConv::DXT && dev->IsDecodingDXT();

    m_levels.resize(m_faces * m_levelCount);
    for (uint32_t f = 0; f < m_faces; ++f)
        for (uint32_t l = 0; l < m_levelCount; ++l)
        {
            LevelData &lv = LevelAt(f, l);
            lv.width = LevelWidth(l);
            lv.height = LevelHeight(l);
            lv.depth = Max<uint32_t>(1, m_depth >> l);
            lv.pitch = FormatPitch(fmt, lv.width);
            lv.slicePitch = lv.pitch * FormatRows(fmt, lv.height);
            lv.glValid = m_isRT;
        }
    m_valid = true;
    if (pool != D3DPOOL_SYSTEMMEM && pool != D3DPOOL_SCRATCH)
        m_valid = EnsureGL();
}

TextureBase::~TextureBase()
{
    ReleaseGL();
}

void TextureBase::ReleaseGL()
{
    if (m_tex)
    {
        m_dev->ForgetTexture(m_tex);
        glDeleteTextures(1, &m_tex);
        m_tex = 0;
    }
}

GLenum TextureBase::FaceTarget(UINT face) const
{
    return m_target == GL_TEXTURE_CUBE_MAP ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + face : m_target;
}

bool TextureBase::EnsureGL()
{
    if (m_tex) return true;
    if (!m_info) return false;
    glGenTextures(1, &m_tex);
    m_dev->BindForUpload(m_target, m_tex);

    const GLenum internal = m_decode ? GL_RGBA8 : m_info->glInternal;
    const bool legacy = internal == GL_ALPHA || internal == GL_LUMINANCE || internal == GL_LUMINANCE_ALPHA;
    if (!legacy)
    {
        if (m_target == GL_TEXTURE_3D)
            glTexStorage3D(m_target, m_levelCount, internal, m_width, m_height, m_depth);
        else
            glTexStorage2D(m_target, m_levelCount, internal, m_width, m_height);
    }
    else
    {
        for (uint32_t f = 0; f < m_faces; ++f)
            for (uint32_t l = 0; l < m_levelCount; ++l)
                glTexImage2D(FaceTarget(f), l, internal, LevelWidth(l), LevelHeight(l), 0, m_info->glFormat, m_info->glType, nullptr);
    }
    glTexParameteri(m_target, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(m_target, GL_TEXTURE_MAX_LEVEL, (GLint)m_levelCount - 1);
    m_baseLevel = 0;
    m_glAllocated = true;

    // Contents written while there was no GL texture (system memory pool).
    for (uint32_t f = 0; f < m_faces; ++f)
        for (uint32_t l = 0; l < m_levelCount; ++l)
            if (LevelAt(f, l).shadowValid) UploadRegion(f, l, nullptr, nullptr);
    return true;
}

void TextureBase::AllocateShadow(LevelData &lv)
{
    if (lv.shadow.empty())
    {
        lv.shadow.assign((size_t)lv.slicePitch * lv.depth, 0);
        lv.shadowValid = false;
    }
}

bool TextureBase::ReadbackLevel(UINT face, UINT level)
{
    LevelData &lv = LevelAt(face, level);
    AllocateShadow(lv);
    if (!m_tex || m_target == GL_TEXTURE_3D || m_info->blockBytes)
    {
        lv.shadowValid = true;
        return false;
    }
    std::vector<uint8_t> rgba((size_t)lv.width * lv.height * 4);
    RECT rc = {0, 0, (LONG)lv.width, (LONG)lv.height};
    if (!m_dev->ReadTextureLevel(m_tex, m_target, FaceTarget(face), level, lv.width, lv.height, rc, rgba.data()))
    {
        lv.shadowValid = true;
        return false;
    }
    // RGBA bytes -> A8R8G8B8 (B,G,R,A) -> this format.
    for (size_t i = 0; i < (size_t)lv.width * lv.height; ++i)
        std::swap(rgba[i * 4], rgba[i * 4 + 2]);
    ConvertPixels(D3DFMT_A8R8G8B8, rgba.data(), lv.width * 4, m_format, lv.shadow.data(), lv.pitch, lv.width, lv.height);
    lv.shadowValid = true;
    return true;
}

HRESULT TextureBase::LockLevel(UINT face, UINT level, D3DLOCKED_RECT *lr, D3DLOCKED_BOX *lb,
                               const RECT *rect, const D3DBOX *box, DWORD flags)
{
    if (!m_valid || face >= m_faces || level >= m_levelCount) return D3DERR_INVALIDCALL;
    LevelData &lv = LevelAt(face, level);
    if (lv.locked) return D3DERR_INVALIDCALL;

    RECT rc = {0, 0, (LONG)lv.width, (LONG)lv.height};
    D3DBOX bx = {0, 0, lv.width, lv.height, 0, lv.depth};
    if (rect)
    {
        if (rect->left < 0 || rect->top < 0 || rect->right > (LONG)lv.width || rect->bottom > (LONG)lv.height ||
            rect->left >= rect->right || rect->top >= rect->bottom)
            return D3DERR_INVALIDCALL;
        rc = *rect;
    }
    if (box)
    {
        if (box->Right > lv.width || box->Bottom > lv.height || box->Back > lv.depth ||
            box->Left >= box->Right || box->Top >= box->Bottom || box->Front >= box->Back)
            return D3DERR_INVALIDCALL;
        bx = *box;
    }
    if (m_info->blockBytes) { rc.left &= ~3; rc.top &= ~3; }

    AllocateShadow(lv);
    if (!lv.shadowValid)
    {
        if (lv.glValid && !(flags & D3DLOCK_DISCARD)) ReadbackLevel(face, level);
        lv.shadowValid = true;
    }

    uint8_t *base = lv.shadow.data();
    uint32_t bpp = m_info->blockBytes ? m_info->blockBytes : m_info->bits / 8;
    if (m_type == D3DRTYPE_VOLUMETEXTURE)
    {
        if (!lb) return D3DERR_INVALIDCALL;
        lb->RowPitch = lv.pitch;
        lb->SlicePitch = lv.slicePitch;
        lb->pBits = base + (size_t)bx.Front * lv.slicePitch + (size_t)bx.Top * lv.pitch + (size_t)bx.Left * bpp;
        lv.lockBox = bx;
    }
    else
    {
        if (!lr) return D3DERR_INVALIDCALL;
        size_t rowOffset = m_info->blockBytes ? (size_t)(rc.top / 4) * lv.pitch : (size_t)rc.top * lv.pitch;
        size_t colOffset = m_info->blockBytes ? (size_t)(rc.left / 4) * bpp : (size_t)rc.left * bpp;
        lr->Pitch = lv.pitch;
        lr->pBits = base + rowOffset + colOffset;
        lv.lockRect = rc;
    }
    lv.locked = true;
    lv.lockFlags = flags;
    return D3D_OK;
}

HRESULT TextureBase::UnlockLevel(UINT face, UINT level)
{
    if (!m_valid || face >= m_faces || level >= m_levelCount) return D3DERR_INVALIDCALL;
    LevelData &lv = LevelAt(face, level);
    if (!lv.locked) return D3DERR_INVALIDCALL;
    lv.locked = false;
    if (lv.lockFlags & D3DLOCK_READONLY) return D3D_OK;

    if (m_type == D3DRTYPE_VOLUMETEXTURE)
        UploadRegion(face, level, nullptr, &lv.lockBox);
    else
        UploadRegion(face, level, &lv.lockRect, nullptr);

    // DEFAULT pool contents live on the GPU only; managed ones optionally too.
    const bool full = lv.lockRect.left == 0 && lv.lockRect.top == 0 &&
                      (uint32_t)lv.lockRect.right == lv.width && (uint32_t)lv.lockRect.bottom == lv.height;
    const bool keepDefault = (m_usage & (D3DUSAGE_DYNAMIC | D3DUSAGE_RENDERTARGET)) != 0;
    if (m_tex && !m_info->blockBytes &&
        ((m_pool == D3DPOOL_DEFAULT && !keepDefault) || (m_pool == D3DPOOL_MANAGED && GetConfig().releaseTextureShadows)) &&
        (full || m_type == D3DRTYPE_VOLUMETEXTURE))
    {
        std::vector<uint8_t>().swap(lv.shadow);
        lv.shadowValid = false;
    }
    return D3D_OK;
}

void TextureBase::UploadRegion(UINT face, UINT level, const RECT *rcIn, const D3DBOX *)
{
    LevelData &lv = LevelAt(face, level);
    lv.shadowValid = true;
    if (!m_tex) return; // system memory textures are not on the GPU
    lv.glValid = true;

    m_dev->BindForUpload(m_target, m_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    if (m_target == GL_TEXTURE_3D)
    {
        const uint8_t *data = PrepareForGL(*m_info, lv.shadow.data(), lv.pitch, lv.width, lv.height * lv.depth, m_decode, m_scratch);
        const GLenum fmt = m_decode ? GL_RGBA : m_info->glFormat;
        const GLenum type = m_decode ? GL_UNSIGNED_BYTE : m_info->glType;
        if (m_info->blockBytes && !m_decode)
            glCompressedTexSubImage3D(m_target, level, 0, 0, 0, lv.width, lv.height, lv.depth, m_info->glInternal,
                                      (GLsizei)(lv.slicePitch * lv.depth), data);
        else
            glTexSubImage3D(m_target, level, 0, 0, 0, lv.width, lv.height, lv.depth, fmt, type, data);
        return;
    }

    RECT rc = {0, 0, (LONG)lv.width, (LONG)lv.height};
    if (rcIn) rc = *rcIn;
    uint32_t w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (m_info->blockBytes)
    {
        // Block aligned region.
        rc.left &= ~3; rc.top &= ~3;
        rc.right = Min<LONG>((rc.right + 3) & ~3, (LONG)lv.width);
        rc.bottom = Min<LONG>((rc.bottom + 3) & ~3, (LONG)lv.height);
        w = rc.right - rc.left; h = rc.bottom - rc.top;
    }

    const uint8_t *src;
    if (m_info->blockBytes)
        src = lv.shadow.data() + (size_t)(rc.top / 4) * lv.pitch + (size_t)(rc.left / 4) * m_info->blockBytes;
    else
        src = lv.shadow.data() + (size_t)rc.top * lv.pitch + (size_t)rc.left * (m_info->bits / 8);

    const uint8_t *data = PrepareForGL(*m_info, src, lv.pitch, w, h, m_decode, m_scratch);
    if (m_info->blockBytes && !m_decode)
    {
        uint32_t size = ((w + 3) / 4) * ((h + 3) / 4) * m_info->blockBytes;
        glCompressedTexSubImage2D(FaceTarget(face), level, rc.left, rc.top, w, h, m_info->glInternal, size, data);
    }
    else
    {
        const GLenum fmt = m_decode ? GL_RGBA : m_info->glFormat;
        const GLenum type = m_decode ? GL_UNSIGNED_BYTE : m_info->glType;
        glTexSubImage2D(FaceTarget(face), level, rc.left, rc.top, w, h, fmt, type, data);
    }
}

void TextureBase::UploadWhole(UINT face, UINT level)
{
    if (m_target == GL_TEXTURE_3D) { D3DBOX b = {}; UploadRegion(face, level, nullptr, &b); }
    else UploadRegion(face, level, nullptr, nullptr);
}

bool TextureBase::WriteRect(UINT face, UINT level, const RECT &rc, const void *src, UINT srcPitch)
{
    if (!m_valid || face >= m_faces || level >= m_levelCount || m_target == GL_TEXTURE_3D) return false;
    LevelData &lv = LevelAt(face, level);
    const bool full = rc.left == 0 && rc.top == 0 && (uint32_t)rc.right == lv.width && (uint32_t)rc.bottom == lv.height;
    AllocateShadow(lv);
    if (!lv.shadowValid)
    {
        if (lv.glValid && !full) ReadbackLevel(face, level);
        lv.shadowValid = true;
    }
    RECT r = rc;
    if (m_info->blockBytes) { r.left &= ~3; r.top &= ~3; }
    uint32_t bpp = m_info->blockBytes ? m_info->blockBytes : m_info->bits / 8;
    uint32_t rowBytes, rows;
    uint8_t *dst;
    if (m_info->blockBytes)
    {
        rowBytes = (((rc.right - r.left) + 3) / 4) * bpp;
        rows = ((rc.bottom - r.top) + 3) / 4;
        dst = lv.shadow.data() + (size_t)(r.top / 4) * lv.pitch + (size_t)(r.left / 4) * bpp;
    }
    else
    {
        rowBytes = (rc.right - rc.left) * bpp;
        rows = rc.bottom - rc.top;
        dst = lv.shadow.data() + (size_t)rc.top * lv.pitch + (size_t)rc.left * bpp;
    }
    const uint8_t *s = static_cast<const uint8_t *>(src);
    for (uint32_t y = 0; y < rows; ++y) memcpy(dst + (size_t)y * lv.pitch, s + (size_t)y * srcPitch, rowBytes);
    UploadRegion(face, level, &r, nullptr);

    const bool keepDefault = (m_usage & (D3DUSAGE_DYNAMIC | D3DUSAGE_RENDERTARGET)) != 0;
    if (m_tex && !m_info->blockBytes && full &&
        ((m_pool == D3DPOOL_DEFAULT && !keepDefault) || (m_pool == D3DPOOL_MANAGED && GetConfig().releaseTextureShadows)))
    {
        std::vector<uint8_t>().swap(lv.shadow);
        lv.shadowValid = false;
    }
    return true;
}

bool TextureBase::ReadRect(UINT face, UINT level, const RECT &rc, void *dst, UINT dstPitch)
{
    if (!m_valid || face >= m_faces || level >= m_levelCount || m_target == GL_TEXTURE_3D) return false;
    LevelData &lv = LevelAt(face, level);
    AllocateShadow(lv);
    if (!lv.shadowValid)
    {
        if (lv.glValid) ReadbackLevel(face, level);
        lv.shadowValid = true;
    }
    uint32_t bpp = m_info->blockBytes ? m_info->blockBytes : m_info->bits / 8;
    uint32_t rowBytes, rows;
    const uint8_t *s;
    if (m_info->blockBytes)
    {
        rowBytes = (((rc.right - (rc.left & ~3)) + 3) / 4) * bpp;
        rows = ((rc.bottom - (rc.top & ~3)) + 3) / 4;
        s = lv.shadow.data() + (size_t)((rc.top & ~3) / 4) * lv.pitch + (size_t)((rc.left & ~3) / 4) * bpp;
    }
    else
    {
        rowBytes = (rc.right - rc.left) * bpp;
        rows = rc.bottom - rc.top;
        s = lv.shadow.data() + (size_t)rc.top * lv.pitch + (size_t)rc.left * bpp;
    }
    uint8_t *d = static_cast<uint8_t *>(dst);
    for (uint32_t y = 0; y < rows; ++y) memcpy(d + (size_t)y * dstPitch, s + (size_t)y * lv.pitch, rowBytes);
    return true;
}

//------------------------------------------------------------------------------
// Texture2D
//------------------------------------------------------------------------------
Texture2D::Texture2D(Device *dev, UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool)
    : ResourceImpl<IDirect3DTexture8>(dev),
      TextureBase(dev, D3DRTYPE_TEXTURE, w, h, 1, levels, usage, fmt, pool)
{
    m_surfaces.assign(m_levelCount ? m_levelCount : 0, nullptr);
}

Texture2D::~Texture2D()
{
    for (Surface *s : m_surfaces) delete s;
}

Surface *Texture2D::LevelSurface(UINT level)
{
    if (level >= m_levelCount) return nullptr;
    if (!m_surfaces[level]) m_surfaces[level] = new Surface(this, static_cast<IDirect3DTexture8 *>(this), 0, level);
    return m_surfaces[level];
}

HRESULT Texture2D::GetLevelDesc(UINT Level, D3DSURFACE_DESC *d)
{
    if (!d || Level >= m_levelCount) return D3DERR_INVALIDCALL;
    d->Format = m_format;
    d->Type = D3DRTYPE_SURFACE;
    d->Usage = m_usage;
    d->Pool = m_pool;
    d->Size = LevelAt(0, Level).slicePitch;
    d->MultiSampleType = D3DMULTISAMPLE_NONE;
    d->Width = LevelWidth(Level);
    d->Height = LevelHeight(Level);
    return D3D_OK;
}

HRESULT Texture2D::GetSurfaceLevel(UINT level, IDirect3DSurface8 **pp)
{
    if (!pp) return D3DERR_INVALIDCALL;
    Surface *s = LevelSurface(level);
    if (!s) return D3DERR_INVALIDCALL;
    s->AddRef();
    *pp = s;
    return D3D_OK;
}

HRESULT Texture2D::LockRect(UINT Level, D3DLOCKED_RECT *lr, const RECT *rect, DWORD flags)
{
    return LockLevel(0, Level, lr, nullptr, rect, nullptr, flags);
}

HRESULT Texture2D::UnlockRect(UINT Level) { return UnlockLevel(0, Level); }

//------------------------------------------------------------------------------
// CubeTexture
//------------------------------------------------------------------------------
CubeTexture::CubeTexture(Device *dev, UINT edge, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool)
    : ResourceImpl<IDirect3DCubeTexture8>(dev),
      TextureBase(dev, D3DRTYPE_CUBETEXTURE, edge, edge, 1, levels, usage, fmt, pool)
{
    m_surfaces.assign(6 * m_levelCount, nullptr);
}

CubeTexture::~CubeTexture()
{
    for (Surface *s : m_surfaces) delete s;
}

Surface *CubeTexture::LevelSurface(UINT face, UINT level)
{
    if (face >= 6 || level >= m_levelCount) return nullptr;
    Surface *&s = m_surfaces[face * m_levelCount + level];
    if (!s) s = new Surface(this, static_cast<IDirect3DCubeTexture8 *>(this), face, level);
    return s;
}

HRESULT CubeTexture::GetLevelDesc(UINT Level, D3DSURFACE_DESC *d)
{
    if (!d || Level >= m_levelCount) return D3DERR_INVALIDCALL;
    d->Format = m_format;
    d->Type = D3DRTYPE_SURFACE;
    d->Usage = m_usage;
    d->Pool = m_pool;
    d->Size = LevelAt(0, Level).slicePitch;
    d->MultiSampleType = D3DMULTISAMPLE_NONE;
    d->Width = LevelWidth(Level);
    d->Height = LevelHeight(Level);
    return D3D_OK;
}

HRESULT CubeTexture::GetCubeMapSurface(D3DCUBEMAP_FACES face, UINT level, IDirect3DSurface8 **pp)
{
    if (!pp) return D3DERR_INVALIDCALL;
    Surface *s = LevelSurface((UINT)face, level);
    if (!s) return D3DERR_INVALIDCALL;
    s->AddRef();
    *pp = s;
    return D3D_OK;
}

HRESULT CubeTexture::LockRect(D3DCUBEMAP_FACES face, UINT Level, D3DLOCKED_RECT *lr, const RECT *rect, DWORD flags)
{
    return LockLevel((UINT)face, Level, lr, nullptr, rect, nullptr, flags);
}

HRESULT CubeTexture::UnlockRect(D3DCUBEMAP_FACES face, UINT Level) { return UnlockLevel((UINT)face, Level); }

//------------------------------------------------------------------------------
// VolumeTexture / Volume
//------------------------------------------------------------------------------
VolumeTexture::VolumeTexture(Device *dev, UINT w, UINT h, UINT d, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool)
    : ResourceImpl<IDirect3DVolumeTexture8>(dev),
      TextureBase(dev, D3DRTYPE_VOLUMETEXTURE, w, h, d, levels, usage, fmt, pool)
{
    m_volumes.assign(m_levelCount, nullptr);
}

VolumeTexture::~VolumeTexture()
{
    for (Volume *v : m_volumes) delete v;
}

HRESULT VolumeTexture::GetLevelDesc(UINT Level, D3DVOLUME_DESC *d)
{
    if (!d || Level >= m_levelCount) return D3DERR_INVALIDCALL;
    LevelData &lv = LevelAt(0, Level);
    d->Format = m_format;
    d->Type = D3DRTYPE_VOLUME;
    d->Usage = m_usage;
    d->Pool = m_pool;
    d->Size = lv.slicePitch * lv.depth;
    d->Width = lv.width;
    d->Height = lv.height;
    d->Depth = lv.depth;
    return D3D_OK;
}

HRESULT VolumeTexture::GetVolumeLevel(UINT Level, IDirect3DVolume8 **pp)
{
    if (!pp || Level >= m_levelCount) return D3DERR_INVALIDCALL;
    if (!m_volumes[Level]) m_volumes[Level] = new Volume(this, Level);
    m_volumes[Level]->AddRef();
    *pp = m_volumes[Level];
    return D3D_OK;
}

HRESULT VolumeTexture::LockBox(UINT Level, D3DLOCKED_BOX *lb, const D3DBOX *box, DWORD flags)
{
    return LockLevel(0, Level, nullptr, lb, nullptr, box, flags);
}

HRESULT VolumeTexture::UnlockBox(UINT Level) { return UnlockLevel(0, Level); }

Volume::Volume(VolumeTexture *parent, UINT level) : m_parent(parent), m_level(level) { m_refs = 0; }

ULONG Volume::AddRef()
{
    m_parent->AddRef();
    return ++m_refs;
}

ULONG Volume::Release()
{
    VolumeTexture *p = m_parent;
    ULONG r = --m_refs;
    p->Release();
    return r;
}

HRESULT Volume::GetDevice(IDirect3DDevice8 **pp)
{
    return m_parent->GetDevice(pp);
}

HRESULT Volume::GetContainer(REFIID, void **pp)
{
    if (!pp) return D3DERR_INVALIDCALL;
    *pp = static_cast<IDirect3DVolumeTexture8 *>(m_parent);
    m_parent->AddRef();
    return D3D_OK;
}

HRESULT Volume::GetDesc(D3DVOLUME_DESC *d) { return m_parent->GetLevelDesc(m_level, d); }
HRESULT Volume::LockBox(D3DLOCKED_BOX *lb, const D3DBOX *box, DWORD f) { return m_parent->LockBox(m_level, lb, box, f); }
HRESULT Volume::UnlockBox() { return m_parent->UnlockBox(m_level); }

//------------------------------------------------------------------------------
// Surface
//------------------------------------------------------------------------------
Surface::Surface(TextureBase *tex, IUnknown *owner, UINT face, UINT level)
    : DeviceChild(tex->m_dev, false), m_kind(Level), m_format(tex->m_format),
      m_width(tex->LevelWidth(level)), m_height(tex->LevelHeight(level)),
      m_pool(tex->m_pool), m_usage(tex->m_usage), m_tex(tex), m_owner(owner),
      m_face(face), m_level(level), m_id(g_nextId++)
{
    m_refs = 0;
}

Surface::Surface(Device *dev, Kind kind, UINT w, UINT h, D3DFORMAT fmt)
    : DeviceChild(dev, true), m_kind(kind), m_format(fmt),
      m_width(w), m_height(h), m_id(g_nextId++)
{
    const FormatInfo *info = GetFormatInfo(fmt);
    if (kind == Image)
    {
        m_pool = D3DPOOL_SYSTEMMEM;
        m_data.assign((size_t)FormatPitch(fmt, w) * FormatRows(fmt, h), 0);
    }
    else if (kind == RenderTarget)
    {
        m_usage = D3DUSAGE_RENDERTARGET;
        glGenRenderbuffers(1, &m_rb);
        glBindRenderbuffer(GL_RENDERBUFFER, m_rb);
        GLenum internal = info && info->glInternal && info->renderTarget ? info->glInternal : GL_RGBA8;
        glRenderbufferStorage(GL_RENDERBUFFER, internal, w, h);
    }
    else if (kind == DepthStencil)
    {
        m_usage = D3DUSAGE_DEPTHSTENCIL;
        glGenRenderbuffers(1, &m_rb);
        glBindRenderbuffer(GL_RENDERBUFFER, m_rb);
        glRenderbufferStorage(GL_RENDERBUFFER, DepthRenderbufferFormat(fmt), w, h);
    }
    else if (kind == BackBuffer)
    {
        m_usage = D3DUSAGE_RENDERTARGET;
    }
}

Surface::~Surface()
{
    if (m_rb) glDeleteRenderbuffers(1, &m_rb);
}

ULONG Surface::AddRef()
{
    if (m_owner) m_owner->AddRef();
    return ++m_refs;
}

ULONG Surface::Release()
{
    IUnknown *owner = m_owner;
    ULONG r = m_refs ? --m_refs : 0;
    if (owner)
        owner->Release(); // may destroy this surface
    else if (r == 0)
        OnZeroRefs();
    return r;
}

void Surface::OnZeroRefs() { delete this; }

HRESULT Surface::GetContainer(REFIID, void **pp)
{
    if (!pp) return D3DERR_INVALIDCALL;
    if (!m_owner) { *pp = nullptr; return D3DERR_INVALIDCALL; }
    m_owner->AddRef();
    *pp = m_owner;
    return D3D_OK;
}

HRESULT Surface::GetDesc(D3DSURFACE_DESC *d)
{
    if (!d) return D3DERR_INVALIDCALL;
    d->Format = m_format;
    d->Type = D3DRTYPE_SURFACE;
    d->Usage = m_usage;
    d->Pool = m_pool;
    d->Size = FormatLevelSize(m_format, m_width, m_height);
    d->MultiSampleType = D3DMULTISAMPLE_NONE;
    d->Width = m_width;
    d->Height = m_height;
    return D3D_OK;
}

HRESULT Surface::LockRect(D3DLOCKED_RECT *lr, const RECT *rect, DWORD flags)
{
    if (!lr) return D3DERR_INVALIDCALL;
    if (m_kind == Level)
        return m_tex->LockLevel(m_face, m_level, lr, nullptr, rect, nullptr, flags);
    if (m_locked) return D3DERR_INVALIDCALL;

    RECT rc = {0, 0, (LONG)m_width, (LONG)m_height};
    if (rect)
    {
        if (rect->left < 0 || rect->top < 0 || rect->right > (LONG)m_width || rect->bottom > (LONG)m_height ||
            rect->left >= rect->right || rect->top >= rect->bottom)
            return D3DERR_INVALIDCALL;
        rc = *rect;
    }
    const FormatInfo *info = GetFormatInfo(m_format);
    if (!info) return D3DERR_INVALIDCALL;

    if (m_kind == Image)
    {
        uint32_t pitch = FormatPitch(m_format, m_width);
        size_t off = info->blockBytes ? (size_t)(rc.top / 4) * pitch + (size_t)(rc.left / 4) * info->blockBytes
                                      : (size_t)rc.top * pitch + (size_t)rc.left * (info->bits / 8);
        lr->Pitch = pitch;
        lr->pBits = m_data.data() + off;
    }
    else if (m_kind == RenderTarget || m_kind == BackBuffer)
    {
        // Read the GPU contents back into a temporary in the surface format.
        uint32_t pitch = FormatPitch(m_format, m_width);
        m_lockTemp.assign((size_t)pitch * m_height, 0);
        RECT full = {0, 0, (LONG)m_width, (LONG)m_height};
        ReadPixelsTo(full, m_format, m_lockTemp.data(), pitch);
        lr->Pitch = pitch;
        lr->pBits = m_lockTemp.data() + (size_t)rc.top * pitch + (size_t)rc.left * (info->bits / 8);
    }
    else
        return D3DERR_INVALIDCALL;
    m_locked = true;
    m_lockFlags = flags;
    m_lockRect = rc;
    return D3D_OK;
}

HRESULT Surface::UnlockRect()
{
    if (m_kind == Level) return m_tex->UnlockLevel(m_face, m_level);
    if (!m_locked) return D3DERR_INVALIDCALL;
    m_locked = false;
    if ((m_kind == RenderTarget || m_kind == BackBuffer) && !(m_lockFlags & D3DLOCK_READONLY))
        WEBD3D8_UNSUPPORTED("writing to a render target surface through LockRect");
    if (m_kind == RenderTarget || m_kind == BackBuffer) std::vector<uint8_t>().swap(m_lockTemp);
    return D3D_OK;
}

void Surface::AttachColor()
{
    switch (m_kind)
    {
    case Level:
        m_tex->EnsureGL();
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, m_tex->FaceTarget(m_face), m_tex->m_tex, m_level);
        break;
    case RenderTarget:
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_rb);
        break;
    case BackBuffer:
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_device->BackBufferTexture(), 0);
        break;
    default: break;
    }
}

void Surface::AttachDepth(bool &hasStencil)
{
    const FormatInfo *info = GetFormatInfo(m_format);
    hasStencil = info && info->stencil;
    if (m_kind != DepthStencil) return;
    // The renderbuffer is DEPTH24_STENCIL8 for stencil formats and a pure
    // depth format otherwise; attach to the matching point and detach the other.
    if (hasStencil)
    {
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_rb);
    }
    else
    {
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_rb);
    }
}

bool Surface::ReadPixelsTo(const RECT &rc, D3DFORMAT dstFormat, void *dst, UINT dstPitch)
{
    const FormatInfo *info = GetFormatInfo(m_format);
    if (!info) return false;
    const uint32_t w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w == 0 || h == 0) return true;

    if (m_kind == Image)
    {
        uint32_t pitch = FormatPitch(m_format, m_width);
        const uint8_t *src = info->blockBytes
            ? m_data.data() + (size_t)(rc.top / 4) * pitch + (size_t)(rc.left / 4) * info->blockBytes
            : m_data.data() + (size_t)rc.top * pitch + (size_t)rc.left * (info->bits / 8);
        return ConvertPixels(m_format, src, pitch, dstFormat, dst, dstPitch, w, h);
    }

    if (m_kind == Level)
    {
        // Texture level: use the shadow copy (reading back from GL if needed).
        if (dstFormat == m_format)
            return m_tex->ReadRect(m_face, m_level, rc, dst, dstPitch);
        std::vector<uint8_t> tmp;
        uint32_t pitch = info->blockBytes ? ((w + 3) / 4) * info->blockBytes : w * (info->bits / 8);
        tmp.resize((size_t)pitch * (info->blockBytes ? (h + 3) / 4 : h));
        if (!m_tex->ReadRect(m_face, m_level, rc, tmp.data(), pitch)) return false;
        return ConvertPixels(m_format, tmp.data(), pitch, dstFormat, dst, dstPitch, w, h);
    }

    if (m_kind == RenderTarget || m_kind == BackBuffer)
    {
        std::vector<uint8_t> rgba((size_t)w * h * 4);
        bool ok = (m_kind == BackBuffer)
            ? m_device->ReadTextureLevel(m_device->BackBufferTexture(), GL_TEXTURE_2D, GL_TEXTURE_2D, 0, m_width, m_height, rc, rgba.data())
            : m_device->ReadRenderbuffer(m_rb, m_width, m_height, rc, rgba.data());
        if (!ok) return false;
        for (size_t i = 0; i < (size_t)w * h; ++i) std::swap(rgba[i * 4], rgba[i * 4 + 2]); // -> BGRA
        if (m_kind == BackBuffer || !info->hasAlpha)
            for (size_t i = 0; i < (size_t)w * h; ++i) if (!info->hasAlpha) rgba[i * 4 + 3] = 255;
        return ConvertPixels(D3DFMT_A8R8G8B8, rgba.data(), w * 4, dstFormat, dst, dstPitch, w, h);
    }
    return false;
}

bool Surface::WritePixelsFrom(const RECT &rc, D3DFORMAT srcFormat, const void *src, UINT srcPitch)
{
    const FormatInfo *info = GetFormatInfo(m_format);
    if (!info) return false;
    const uint32_t w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w == 0 || h == 0) return true;

    if (m_kind == Image)
    {
        uint32_t pitch = FormatPitch(m_format, m_width);
        uint8_t *dst = info->blockBytes
            ? m_data.data() + (size_t)(rc.top / 4) * pitch + (size_t)(rc.left / 4) * info->blockBytes
            : m_data.data() + (size_t)rc.top * pitch + (size_t)rc.left * (info->bits / 8);
        return ConvertPixels(srcFormat, src, srcPitch, m_format, dst, pitch, w, h);
    }
    if (m_kind == Level)
    {
        if (srcFormat == m_format)
            return m_tex->WriteRect(m_face, m_level, rc, src, srcPitch);
        uint32_t pitch = info->blockBytes ? ((w + 3) / 4) * info->blockBytes : w * (info->bits / 8);
        std::vector<uint8_t> tmp((size_t)pitch * (info->blockBytes ? (h + 3) / 4 : h));
        if (!ConvertPixels(srcFormat, src, srcPitch, m_format, tmp.data(), pitch, w, h)) return false;
        return m_tex->WriteRect(m_face, m_level, rc, tmp.data(), pitch);
    }
    WEBD3D8_UNSUPPORTED("copying pixels into a render target surface");
    return false;
}

} // namespace webd3d8
