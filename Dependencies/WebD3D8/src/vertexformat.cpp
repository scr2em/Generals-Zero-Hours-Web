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
** WebAssembly port: vertex formats. FVF codes and D3DVSD_* declarations are
** both turned into a list of attributes bound to the D3DVSDE_* register
** numbers, which double as GL attribute locations.
*/
#include "device.h"

namespace webd3d8 {

UINT FVFVertexSize(DWORD fvf)
{
    UINT size = 0;
    switch (fvf & D3DFVF_POSITION_MASK)
    {
    case D3DFVF_XYZ: size += 12; break;
    case D3DFVF_XYZRHW: size += 16; break;
    case D3DFVF_XYZB1: size += 16; break;
    case D3DFVF_XYZB2: size += 20; break;
    case D3DFVF_XYZB3: size += 24; break;
    case D3DFVF_XYZB4: size += 28; break;
    case D3DFVF_XYZB5: size += 32; break;
    }
    if (fvf & D3DFVF_NORMAL) size += 12;
    if (fvf & D3DFVF_PSIZE) size += 4;
    if (fvf & D3DFVF_DIFFUSE) size += 4;
    if (fvf & D3DFVF_SPECULAR) size += 4;
    const UINT texCount = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    for (UINT i = 0; i < texCount; ++i)
    {
        switch ((fvf >> (16 + i * 2)) & 3)
        {
        case D3DFVF_TEXTUREFORMAT1: size += 4; break;
        case D3DFVF_TEXTUREFORMAT2: size += 8; break;
        case D3DFVF_TEXTUREFORMAT3: size += 12; break;
        case D3DFVF_TEXTUREFORMAT4: size += 16; break;
        }
    }
    return size;
}

static uint32_t g_nextLayoutId = 1;

const VertexLayout *LayoutFromFVF(DWORD fvf)
{
    static std::unordered_map<DWORD, VertexLayout *> cache;
    auto it = cache.find(fvf);
    if (it != cache.end()) return it->second;

    VertexLayout *l = new VertexLayout();
    l->fvf = fvf;
    l->id = g_nextLayoutId++;
    uint16_t off = 0;
    auto add = [&](uint8_t reg, uint8_t size, uint8_t glType, uint8_t norm, uint8_t d3dColor, uint16_t bytes) {
        VertexElement &e = l->elems[l->count++];
        e.reg = reg; e.stream = 0; e.offset = off; e.size = size; e.glType = glType; e.normalized = norm; e.d3dColor = d3dColor;
        off += bytes;
    };
    switch (fvf & D3DFVF_POSITION_MASK)
    {
    case D3DFVF_XYZ: add(D3DVSDE_POSITION, 3, 0, 0, 0, 12); break;
    case D3DFVF_XYZRHW: add(D3DVSDE_POSITION, 4, 0, 0, 0, 16); break;
    case D3DFVF_XYZB1: case D3DFVF_XYZB2: case D3DFVF_XYZB3: case D3DFVF_XYZB4: case D3DFVF_XYZB5:
    {
        // Blend weights are skipped: skinning happens on the CPU in WW3D2.
        int weights = (int)(((fvf & D3DFVF_POSITION_MASK) - D3DFVF_XYZB1) / 2) + 1;
        add(D3DVSDE_POSITION, 3, 0, 0, 0, 12);
        off += (uint16_t)(weights * 4);
        break;
    }
    }
    if (fvf & D3DFVF_NORMAL) add(D3DVSDE_NORMAL, 3, 0, 0, 0, 12);
    if (fvf & D3DFVF_PSIZE) add(D3DVSDE_PSIZE, 1, 0, 0, 0, 4);
    if (fvf & D3DFVF_DIFFUSE) add(D3DVSDE_DIFFUSE, 4, 1, 1, 1, 4);
    if (fvf & D3DFVF_SPECULAR) add(D3DVSDE_SPECULAR, 4, 1, 1, 1, 4);
    const UINT texCount = Min<UINT>((fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT, 8);
    for (UINT i = 0; i < texCount; ++i)
    {
        switch ((fvf >> (16 + i * 2)) & 3)
        {
        case D3DFVF_TEXTUREFORMAT1: add((uint8_t)(D3DVSDE_TEXCOORD0 + i), 1, 0, 0, 0, 4); break;
        case D3DFVF_TEXTUREFORMAT2: add((uint8_t)(D3DVSDE_TEXCOORD0 + i), 2, 0, 0, 0, 8); break;
        case D3DFVF_TEXTUREFORMAT3: add((uint8_t)(D3DVSDE_TEXCOORD0 + i), 3, 0, 0, 0, 12); break;
        case D3DFVF_TEXTUREFORMAT4: add((uint8_t)(D3DVSDE_TEXCOORD0 + i), 4, 0, 0, 0, 16); break;
        }
    }
    l->stride[0] = off;
    cache[fvf] = l;
    return l;
}

bool ParseVertexDeclaration(const DWORD *decl, VertexLayout &out, std::vector<float> *constants)
{
    out = VertexLayout();
    out.id = g_nextLayoutId++;
    uint16_t offsets[MAX_STREAMS] = {};
    unsigned stream = 0;
    for (int guard = 0; guard < 512; ++guard)
    {
        DWORD t = decl[guard];
        if (t == D3DVSD_END()) return true;
        const DWORD type = (t & D3DVSD_TOKENTYPEMASK) >> D3DVSD_TOKENTYPESHIFT;
        switch (type)
        {
        case D3DVSD_TOKEN_NOP: break;
        case D3DVSD_TOKEN_STREAM:
            stream = t & D3DVSD_STREAMNUMBERMASK;
            if (stream >= MAX_STREAMS) return false;
            break;
        case D3DVSD_TOKEN_STREAMDATA:
            if (t & 0x10000000)
            {
                offsets[stream] += (uint16_t)(4 * ((t & D3DVSD_SKIPCOUNTMASK) >> D3DVSD_SKIPCOUNTSHIFT));
            }
            else
            {
                const DWORD reg = t & D3DVSD_VERTEXREGMASK;
                const DWORD dtype = (t & D3DVSD_DATATYPEMASK) >> D3DVSD_DATATYPESHIFT;
                if (out.count >= 16 || reg >= 16) return false;
                VertexElement &e = out.elems[out.count++];
                e.reg = (uint8_t)reg;
                e.stream = (uint8_t)stream;
                e.offset = offsets[stream];
                e.d3dColor = 0;
                e.normalized = 0;
                e.glType = 0;
                uint16_t bytes = 0;
                switch (dtype)
                {
                case D3DVSDT_FLOAT1: e.size = 1; bytes = 4; break;
                case D3DVSDT_FLOAT2: e.size = 2; bytes = 8; break;
                case D3DVSDT_FLOAT3: e.size = 3; bytes = 12; break;
                case D3DVSDT_FLOAT4: e.size = 4; bytes = 16; break;
                case D3DVSDT_D3DCOLOR: e.size = 4; e.glType = 1; e.normalized = 1; e.d3dColor = 1; bytes = 4; break;
                case D3DVSDT_UBYTE4: e.size = 4; e.glType = 1; bytes = 4; break;
                case D3DVSDT_SHORT2: e.size = 2; e.glType = 2; bytes = 4; break;
                case D3DVSDT_SHORT4: e.size = 4; e.glType = 2; bytes = 8; break;
                default: return false;
                }
                offsets[stream] += bytes;
            }
            break;
        case D3DVSD_TOKEN_CONSTMEM:
        {
            const DWORD count = (t & D3DVSD_CONSTCOUNTMASK) >> D3DVSD_CONSTCOUNTSHIFT;
            const DWORD addr = t & D3DVSD_CONSTADDRESSMASK;
            for (DWORD i = 0; i < count; ++i)
            {
                if (constants)
                {
                    constants->push_back((float)(addr + i));
                    for (int c = 0; c < 4; ++c) constants->push_back(DwordToFloat(decl[guard + 1 + i * 4 + c]));
                }
            }
            guard += (int)count * 4;
            break;
        }
        case D3DVSD_TOKEN_EXT:
            guard += (int)((t & D3DVSD_EXTCOUNTMASK) >> D3DVSD_EXTCOUNTSHIFT);
            break;
        default: break;
        }
    }
    return false;
}

} // namespace webd3d8
