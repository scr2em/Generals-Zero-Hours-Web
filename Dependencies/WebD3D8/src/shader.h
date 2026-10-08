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
** WebAssembly port: Direct3D 8 vertex/pixel shader objects.
**
** vs.1.1 and ps.1.0-1.4 token streams are parsed and translated to GLSL ES
** 3.00 (shader_translate.cpp); text shaders are assembled to token streams by
** D3DXAssembleShader (shader_assembler.cpp).
*/
#pragma once

#include "device.h"
#include "program.h"

namespace webd3d8 {

class VertexShaderObject
{
public:
    VertexLayout layout;            ///< from the declaration
    bool hasFunction = false;       ///< false: declaration only, fixed-function processing
    std::string body;               ///< GLSL statements of main() computing oPos, oD0, oD1, oFog, oPts, oT[0..3]
    uint32_t inputMask = 0;         ///< v# registers read by the program
    uint32_t d3dColorMask = 0;      ///< registers declared as D3DCOLOR (need a BGRA swizzle)
    std::vector<DWORD> bytecode;
    std::vector<DWORD> declaration;
    float defConstants[VS_CONSTANTS][4]; ///< values of `def c#` and D3DVSD_CONST
    uint8_t defMask[VS_CONSTANTS];
    bool writesPointSize = false;
    bool writesFog = false;
    DWORD handle = 0;
};

class PixelShaderObject
{
public:
    std::vector<DWORD> bytecode;
    uint32_t version = 0;           ///< major<<8 | minor
    uint32_t textureStageMask = 0;  ///< stages sampled / whose coordinates are read
    uint32_t sampledMask = 0;
    struct Impl;
    Impl *impl = nullptr;
    ~PixelShaderObject();
    /// Generates the fragment shader for the sampler types in `key`.
    std::string Generate(const ProgramKey &key) const;
    DWORD handle = 0;
};

bool CreateVertexShaderObject(const DWORD *declaration, const DWORD *function, VertexShaderObject **out);
bool CreatePixelShaderObject(const DWORD *function, PixelShaderObject **out);

/// Assembles vs.1.x / ps.1.x text to a token stream. Returns false on error
/// (message in `errors`).
bool AssembleShader(const char *source, size_t length, std::vector<DWORD> &out, std::string &errors);

/// Wraps the translated vertex shader body into a complete GLSL vertex shader.
std::string BuildTranslatedVertexShader(const VertexShaderObject &vs, const ProgramKey &key);

} // namespace webd3d8
