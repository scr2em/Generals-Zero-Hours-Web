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
** WebAssembly port: GLSL program generation and caching.
**
** A program is the combination of a vertex stage (generated from the
** fixed-function state of the draw call, or translated from a vs.1.1 shader)
** and a fragment stage (texture stage cascade, or a translated ps.1.x
** shader). Programs are cached by a key built from the render state bits that
** influence the generated code.
*/
#pragma once

#include "device.h"

namespace webd3d8 {

/// Per texture stage state that is baked into the generated shader code.
struct StageKey
{
    uint8_t colorOp, alphaOp;
    uint8_t colorArg0, colorArg1, colorArg2;
    uint8_t alphaArg0, alphaArg1, alphaArg2;
    uint8_t resultTemp;
    uint8_t texType;     ///< 0 = none, 1 = 2D, 2 = cube, 3 = volume
    uint8_t coordIndex;  ///< texture coordinate set (0..7)
    uint8_t texGen;      ///< 0 = pass through, 1 = camera normal, 2 = camera position, 3 = reflection
    uint8_t xformCount;  ///< D3DTTFF_COUNTn (0 = transform disabled)
    uint8_t projected;
    uint8_t lodBias;     ///< non-zero when a mip LOD bias is applied
    uint8_t borderMask;  ///< bit 0/1/2: U/V/W address mode is D3DTADDRESS_BORDER (emulated in the shader)
};

struct ProgramKey
{
    uint32_t vsHandle;   ///< 0 = fixed-function vertex processing
    uint32_t psHandle;   ///< 0 = fixed-function texture stages
    // Vertex format (fixed-function vertex stage).
    uint8_t rhw, hasNormal, hasDiffuse, hasSpecular, hasPSize;
    uint8_t tcDim[8];
    // Lighting.
    uint8_t lighting, specularEnable, localViewer, normalize, colorVertex;
    uint8_t srcDiffuse, srcSpecular, srcAmbient, srcEmissive;
    uint8_t lightCount;
    uint8_t lightType[MAX_SHADER_LIGHTS];
    // Fog / tests / misc.
    uint8_t fogEnable, fogVertexMode, fogTableMode, rangeFog;
    uint8_t alphaTest, alphaFunc;
    uint8_t clipMask;
    uint8_t pointSprite, pointScale, flatShade;
    uint8_t points;      ///< the draw is a point list (gl_PointSize is only written then)
    uint8_t stageCount;  ///< number of active stages (fixed-function fragment stage)
    uint8_t tcMask;      ///< stages whose texture coordinates are produced/consumed
    uint8_t pad[2];
    StageKey stage[MAX_STAGES];
};

enum UniformId
{
    U_XF,      ///< vec4[15]: wvp (0..3), wv (4..7), normal matrix columns (8..10), world (11..14)
    U_TM,      ///< mat4[8] texture transforms
    U_VIEW,    ///< vec4[2]: (1/width, 1/height, zmin, zrange), viewport (x, y, w, h)
    U_CLIP,    ///< vec4[6] user clip planes
    U_PT,      ///< vec4[2]: (size, min, max, viewport height), (scale a, b, c)
    U_MAT,     ///< vec4[5]: emissive, ambient, diffuse, specular, (power)
    U_LT,      ///< vec4[8*7]: per light position, direction, diffuse, specular, ambient, attenuation(+range), spot
    U_AMBIENT,
    U_FOGP,    ///< vec4[2]: (start, end, density, 1/(end-start)), color
    U_TFACTOR, U_ALPHAREF, U_LOD, U_BUMP, U_BUMPL,
    U_VSC, U_PSC, U_BORDER,
    U_COUNT
};



class Program
{
public:
    ~Program();
    GLuint id = 0;
    GLint loc[U_COUNT];
    uint32_t ver[G_COUNT] = {};   ///< version of each uniform group this program last received
    uint32_t usedGroups = 0;      ///< bit per UniformGroup: some uniform of the group exists in the program
    int xfCount = 4;              ///< vec4s of U_XF the program reads (4 = only the projection matrix)
    ProgramKey key;
    bool valid = false;
};

/// Builds (or fetches from the cache) the program for the key.
Program *CreateProgram(Device *dev, const ProgramKey &key, const std::string &vs, const std::string &fs);

std::string GenerateFixedFunctionVertexShader(const ProgramKey &key);
std::string GenerateFixedFunctionFragmentShader(const ProgramKey &key);

/// Declarations (and the macros that give the packed uniforms their readable names) shared by all
/// shader stages.
std::string PackedUniformDeclarations();

/// Fragment-stage prologue shared by FF and translated shaders: declarations
/// of the varyings both vertex stages produce.
std::string VaryingDeclarations(const ProgramKey &key, bool vertexStage, bool forTranslatedVS);

/// Epilogue common to every vertex stage: writes the varyings and gl_Position
/// from the D3D-convention outputs (oPos etc.).
const char *VertexEpilogue();

/// Fragment stage helpers shared with the translated pixel shaders.
std::string FragmentCommonUniforms();
/// Expression of the texture coordinates of stage `stage` (a vec4).
std::string TexCoordExpr(const ProgramKey &key, int stage);
/// Fog, alpha test and the write of `col` to fragColor; closes main().
std::string FragmentTail(const ProgramKey &key);
/// GLSL expression that samples stage `stage` at `coord` (a vec2 or vec3 expression; `lodArg` is the
/// optional ", bias" argument). Stages with D3DTADDRESS_BORDER return the border color outside
/// [0,1], which WebGL cannot do in the sampler.
std::string TextureFetch(const ProgramKey &key, int stage, const std::string &coord, const std::string &lodArg);

} // namespace webd3d8
