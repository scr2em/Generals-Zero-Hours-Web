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
** WebAssembly port: shared definitions of the shader assembler and the
** bytecode -> GLSL translators.
*/
#pragma once

#include "shader.h"

namespace webd3d8 {

enum RegType
{
    RT_TEMP = 0,
    RT_INPUT = 1,
    RT_CONST = 2,
    RT_ADDR_TEX = 3, ///< a0 in vertex shaders, t# in pixel shaders
    RT_RASTOUT = 4,
    RT_ATTROUT = 5,
    RT_TEXCRDOUT = 6,
};

/// A decoded destination or source parameter token.
struct Param
{
    DWORD token = 0;
    int type = 0;
    int reg = 0;
    bool relative = false;
    // destination
    uint8_t writeMask = 0xF; ///< bit 0 = x ... bit 3 = w
    bool saturate = false;
    int shift = 0;           ///< result multiplier exponent: +1 = x2, +2 = x4, -1 = d2, ...
    // source
    uint8_t swizzle[4] = {0, 1, 2, 3};
    int srcMod = 0;          ///< D3DSPSM_* >> 24
};

inline Param DecodeParam(DWORD t)
{
    Param p;
    p.token = t;
    p.type = (int)((t & D3DSP_REGTYPE_MASK) >> D3DSP_REGTYPE_SHIFT);
    p.reg = (int)(t & D3DSP_REGNUM_MASK);
    p.relative = (t & D3DVS_ADDRESSMODE_MASK) != 0;
    p.writeMask = (uint8_t)((t >> 16) & 0xF);
    p.saturate = (t & D3DSPDM_SATURATE) != 0;
    int sh = (int)((t & D3DSP_DSTSHIFT_MASK) >> D3DSP_DSTSHIFT_SHIFT);
    p.shift = sh >= 8 ? sh - 16 : sh;
    for (int i = 0; i < 4; ++i) p.swizzle[i] = (uint8_t)((t >> (16 + 2 * i)) & 3);
    p.srcMod = (int)((t & D3DSP_SRCMOD_MASK) >> D3DSP_SRCMOD_SHIFT);
    return p;
}

inline bool IsIdentitySwizzle(const Param &p)
{
    return p.swizzle[0] == 0 && p.swizzle[1] == 1 && p.swizzle[2] == 2 && p.swizzle[3] == 3;
}

/// Number of parameter tokens (destination included) that follow an
/// instruction token. -1 for unknown opcodes.
int OperandCount(DWORD opcode, bool pixelShader, DWORD version);

const char *OpcodeName(DWORD opcode);

/// "xyzw" style swizzle suffix for GLSL, e.g. ".xxyz".
std::string SwizzleString(const Param &p);
std::string MaskString(uint8_t mask);

} // namespace webd3d8
