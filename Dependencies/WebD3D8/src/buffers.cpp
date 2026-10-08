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
** WebAssembly port: vertex and index buffers.
**
** The application writes into a system-memory copy through Lock(); the
** modified range is pushed to the GL buffer lazily, right before a draw call
** reads from it, so several locks between two draws cost a single upload.
*/
#include "resources.h"

namespace webd3d8 {

BufferStorage::BufferStorage(Device *dev, GLenum target, UINT size, DWORD usage)
    : m_dev(dev), m_target(target), m_data(size, 0), m_usage(usage)
{
    glGenBuffers(1, &m_buf);
}

BufferStorage::~BufferStorage()
{
    if (m_buf)
    {
        m_dev->ForgetBuffer(m_buf);
        glDeleteBuffers(1, &m_buf);
    }
}

HRESULT BufferStorage::Lock(UINT offset, UINT size, BYTE **ppData, DWORD flags)
{
    if (!ppData) return D3DERR_INVALIDCALL;
    if (m_locked) return D3DERR_INVALIDCALL;
    if (offset > m_data.size()) return D3DERR_INVALIDCALL;
    if (size == 0) size = (UINT)m_data.size() - offset;
    if ((uint64_t)offset + size > m_data.size()) return D3DERR_INVALIDCALL;
    m_locked = true;
    m_lockOffset = offset;
    m_lockSize = size;
    m_lockFlags = flags;
    *ppData = m_data.data() + offset;
    return D3D_OK;
}

HRESULT BufferStorage::Unlock()
{
    if (!m_locked) return D3DERR_INVALIDCALL;
    m_locked = false;
    if (m_lockFlags & D3DLOCK_READONLY) return D3D_OK;
    UINT b = m_lockOffset, e = m_lockOffset + m_lockSize;
    if (m_dirtyBegin == m_dirtyEnd) { m_dirtyBegin = b; m_dirtyEnd = e; }
    else { m_dirtyBegin = Min(m_dirtyBegin, b); m_dirtyEnd = Max(m_dirtyEnd, e); }
    return D3D_OK;
}

GLuint BufferStorage::Flush()
{
    if (!m_allocated)
    {
        glBindBuffer(GL_COPY_WRITE_BUFFER, m_buf);
        glBufferData(GL_COPY_WRITE_BUFFER, (GLsizeiptr)m_data.size(), m_data.data(), GL_DYNAMIC_DRAW);
        m_dev->MarkBufferBindingDirty();
        m_allocated = true;
        m_dirtyBegin = m_dirtyEnd = 0;
    }
    else if (m_dirtyBegin != m_dirtyEnd)
    {
        glBindBuffer(GL_COPY_WRITE_BUFFER, m_buf);
        glBufferSubData(GL_COPY_WRITE_BUFFER, m_dirtyBegin, m_dirtyEnd - m_dirtyBegin, m_data.data() + m_dirtyBegin);
        m_dev->MarkBufferBindingDirty();
        m_dirtyBegin = m_dirtyEnd = 0;
    }
    return m_buf;
}

VertexBuffer::VertexBuffer(Device *dev, UINT length, DWORD usage, DWORD fvf, D3DPOOL pool)
    : ResourceImpl<IDirect3DVertexBuffer8>(dev), m_storage(dev, GL_ARRAY_BUFFER, length, usage),
      m_fvf(fvf), m_usage(usage), m_pool(pool)
{
}

HRESULT VertexBuffer::GetDesc(D3DVERTEXBUFFER_DESC *d)
{
    if (!d) return D3DERR_INVALIDCALL;
    d->Format = D3DFMT_VERTEXDATA;
    d->Type = D3DRTYPE_VERTEXBUFFER;
    d->Usage = m_usage;
    d->Pool = m_pool;
    d->Size = m_storage.Size();
    d->FVF = m_fvf;
    return D3D_OK;
}

IndexBuffer::IndexBuffer(Device *dev, UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool)
    : ResourceImpl<IDirect3DIndexBuffer8>(dev), m_storage(dev, GL_ELEMENT_ARRAY_BUFFER, length, usage),
      m_format(format), m_usage(usage), m_pool(pool)
{
}

HRESULT IndexBuffer::GetDesc(D3DINDEXBUFFER_DESC *d)
{
    if (!d) return D3DERR_INVALIDCALL;
    d->Format = m_format;
    d->Type = D3DRTYPE_INDEXBUFFER;
    d->Usage = m_usage;
    d->Pool = m_pool;
    d->Size = m_storage.Size();
    return D3D_OK;
}

} // namespace webd3d8
