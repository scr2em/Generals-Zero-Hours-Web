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
** WebAssembly port: GLSL ES 3.00 generation for the Direct3D 8 fixed-function
** pipeline (vertex lighting/transform and the texture stage cascade).
**
** Conventions (see device.h): matrices are uploaded in D3D memory order and
** therefore appear transposed to GLSL, which is exactly what makes
** `M * v` equal the D3D `v * M`. The vertex stage converts the D3D clip space
** (z in [0,w], y up, pixel centers on integers) into the flipped GL framebuffer
** convention.
*/
#include "program.h"

#include <cstdarg>

namespace webd3d8 {

namespace {

void Add(std::string &s, const char *fmt, ...)
{
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    s += buf;
}

const char *kSwizzle[] = {"x", "xy", "xyz", "xyzw"};

} // namespace

std::string VaryingDeclarations(const ProgramKey &key, bool vertexStage, bool forTranslatedVS)
{
    std::string s;
    const char *dir = vertexStage ? "out" : "in";
    const char *flat = key.flatShade ? "flat " : "";
    Add(s, "%s%s vec4 v_c0;\n%s%s vec4 v_c1;\n", flat, dir, flat, dir);
    Add(s, "%s float v_fog;\n%s float v_fz;\n", dir, dir);
    uint32_t mask = key.tcMask;
    if (vertexStage && forTranslatedVS) mask |= 0xF;
    for (int i = 0; i < MAX_STAGES; ++i)
        if (mask & (1u << i)) Add(s, "%s vec4 v_t%d;\n", dir, i);
    for (int i = 0; i < MAX_CLIP_PLANES; ++i)
        if (key.clipMask & (1u << i)) Add(s, "%s float v_clip%d;\n", dir, i);
    return s;
}

const char *VertexEpilogue()
{
    return
        "    gl_Position = vec4(oPos.x + oPos.w * u_pix.x, -oPos.y + oPos.w * u_pix.y, 2.0 * oPos.z - oPos.w, oPos.w);\n";
}

//------------------------------------------------------------------------------
// Vertex stage
//------------------------------------------------------------------------------
std::string GenerateFixedFunctionVertexShader(const ProgramKey &key)
{
    std::string s;
    s += "#version 300 es\nprecision highp float;\nprecision highp int;\n";
    s += "uniform mat4 u_wvp;\nuniform mat4 u_wv;\nuniform mat4 u_world;\nuniform mat3 u_nm;\n";
    s += "uniform mat4 u_tm[8];\nuniform vec2 u_pix;\nuniform vec4 u_vp;\n";
    s += "uniform vec4 u_point;\nuniform vec3 u_pointatt;\nuniform vec4 u_clip[6];\n";
    s += "uniform vec4 u_fog;\n";

    // Inputs (locations are the D3DVSDE register numbers).
    s += "layout(location=0) in vec4 a_pos;\n";
    if (key.hasNormal) s += "layout(location=3) in vec4 a_norm;\n";
    if (key.hasPSize) s += "layout(location=4) in vec4 a_psize;\n";
    if (key.hasDiffuse) s += "layout(location=5) in vec4 a_diff;\n";
    if (key.hasSpecular) s += "layout(location=6) in vec4 a_spec;\n";
    uint32_t setsNeeded = 0;
    for (int i = 0; i < MAX_STAGES; ++i)
        if ((key.tcMask & (1u << i)) && key.stage[i].texGen == 0)
            setsNeeded |= 1u << (key.stage[i].coordIndex & 7);
    for (int i = 0; i < 8; ++i)
        if (setsNeeded & (1u << i)) Add(s, "layout(location=%d) in vec4 a_t%d;\n", 7 + i, i);

    if (key.lighting && !key.rhw)
    {
        s += "uniform vec4 u_matE;\nuniform vec4 u_matA;\nuniform vec4 u_matD;\nuniform vec4 u_matS;\nuniform float u_matP;\n";
        s += "uniform vec4 u_ambient;\n";
        if (key.lightCount)
        {
            s += "uniform vec3 u_lpos[8];\nuniform vec3 u_ldir[8];\nuniform vec3 u_ldiff[8];\nuniform vec3 u_lspec[8];\n";
            s += "uniform vec3 u_lamb[8];\nuniform vec4 u_latt[8];\nuniform vec4 u_lspot[8];\n";
        }
    }
    s += VaryingDeclarations(key, true, false);

    s += "void main() {\n";
    s += "    vec4 oPos;\n";
    s += key.hasDiffuse ? "    vec4 vdiff = a_diff.bgra;\n" : "    vec4 vdiff = vec4(1.0);\n";
    s += key.hasSpecular ? "    vec4 vspec = a_spec.bgra;\n" : "    vec4 vspec = vec4(0.0);\n";

    if (key.rhw)
    {
        // Pre-transformed vertices: window coordinates (x, y, z in [0,1], 1/w).
        s += "    float rw = (a_pos.w != 0.0) ? 1.0 / a_pos.w : 1.0;\n";
        s += "    float nx = (a_pos.x + 0.5 - u_vp.x) / u_vp.z * 2.0 - 1.0;\n";
        s += "    float ny = (a_pos.y + 0.5 - u_vp.y) / u_vp.w * 2.0 - 1.0;\n";
        s += "    gl_Position = vec4(nx * rw, ny * rw, (a_pos.z * 2.0 - 1.0) * rw, rw);\n";
        s += "    vec4 eyePos = vec4(0.0);\n    vec3 eyeNormal = vec3(0.0, 0.0, 1.0);\n";
        s += "    v_c0 = vdiff;\n    v_c1 = vspec;\n";
        s += key.fogVertexMode ? "    v_fog = vspec.a;\n" : "    v_fog = 1.0;\n";
        s += "    v_fz = a_pos.z;\n";
    }
    else
    {
        s += "    vec4 pos4 = vec4(a_pos.xyz, 1.0);\n";
        s += "    oPos = u_wvp * pos4;\n";
        s += VertexEpilogue();
        s += "    vec4 eyePos = u_wv * pos4;\n";
        if (key.hasNormal)
        {
            s += "    vec3 eyeNormal = u_nm * a_norm.xyz;\n";
            if (key.normalize) s += "    eyeNormal = normalize(eyeNormal);\n";
        }
        else
            s += "    vec3 eyeNormal = vec3(0.0);\n";

        if (!key.lighting)
        {
            s += "    v_c0 = vdiff;\n    v_c1 = vspec;\n";
        }
        else
        {
            // Material color sources (0 = material, 1 = vertex diffuse, 2 = vertex specular).
            auto src = [&](uint8_t sel, const char *material) -> std::string {
                if (!key.colorVertex || sel == 0) return material;
                return sel == 1 ? "vdiff" : "vspec";
            };
            Add(s, "    vec4 mE = %s;\n    vec4 mA = %s;\n    vec4 mD = %s;\n    vec4 mS = %s;\n",
                src(key.srcEmissive, "u_matE").c_str(), src(key.srcAmbient, "u_matA").c_str(),
                src(key.srcDiffuse, "u_matD").c_str(), src(key.srcSpecular, "u_matS").c_str());
            s += "    vec3 lAmb = u_ambient.rgb;\n    vec3 lDiff = vec3(0.0);\n    vec3 lSpec = vec3(0.0);\n";
            if (key.lightCount)
            {
                if (key.specularEnable)
                    s += key.localViewer ? "    vec3 viewDir = normalize(-eyePos.xyz);\n" : "    vec3 viewDir = vec3(0.0, 0.0, -1.0);\n";
                for (int i = 0; i < key.lightCount; ++i)
                {
                    const int type = key.lightType[i]; // 0 directional, 1 point, 2 spot
                    Add(s, "    {\n        float att = 1.0;\n");
                    if (type == 0)
                        Add(s, "        vec3 L = -u_ldir[%d];\n", i);
                    else
                    {
                        Add(s, "        vec3 Lv = u_lpos[%d] - eyePos.xyz;\n        float dist = length(Lv);\n", i);
                        Add(s, "        vec3 L = Lv / max(dist, 1e-6);\n");
                        Add(s, "        att = (dist > u_latt[%d].w) ? 0.0 : 1.0 / (u_latt[%d].x + u_latt[%d].y * dist + u_latt[%d].z * dist * dist);\n", i, i, i, i);
                        if (type == 2)
                        {
                            Add(s, "        float rho = dot(-L, u_ldir[%d]);\n", i);
                            Add(s, "        float spot = (rho > u_lspot[%d].x) ? 1.0 : ((rho <= u_lspot[%d].y) ? 0.0 : pow((rho - u_lspot[%d].y) * u_lspot[%d].w, u_lspot[%d].z));\n",
                                i, i, i, i, i);
                            s += "        att *= spot;\n";
                        }
                    }
                    Add(s, "        lAmb += att * u_lamb[%d];\n", i);
                    s += "        float ndl = max(dot(eyeNormal, L), 0.0);\n";
                    Add(s, "        lDiff += att * ndl * u_ldiff[%d];\n", i);
                    if (key.specularEnable)
                    {
                        s += "        if (ndl > 0.0) {\n            vec3 H = normalize(L + viewDir);\n";
                        s += "            float sp = (u_matP > 0.0) ? pow(max(dot(eyeNormal, H), 0.0), u_matP) : 1.0;\n";
                        Add(s, "            lSpec += att * sp * u_lspec[%d];\n        }\n", i);
                    }
                    s += "    }\n";
                }
            }
            s += "    v_c0 = vec4(clamp(mE.rgb + mA.rgb * lAmb + mD.rgb * lDiff, 0.0, 1.0), clamp(mD.a, 0.0, 1.0));\n";
            if (key.specularEnable)
                s += "    v_c1 = vec4(clamp(mS.rgb * lSpec, 0.0, 1.0), 0.0);\n";
            else
                s += "    v_c1 = vec4(0.0);\n";
        }

        // Fog factor (1 = unfogged).
        s += key.rangeFog ? "    float fz = length(eyePos.xyz);\n" : "    float fz = abs(eyePos.z);\n";
        s += "    v_fz = fz;\n";
        switch (key.fogVertexMode)
        {
        case D3DFOG_LINEAR: s += "    v_fog = clamp((u_fog.y - fz) * u_fog.w, 0.0, 1.0);\n"; break;
        case D3DFOG_EXP: s += "    v_fog = clamp(exp(-u_fog.z * fz), 0.0, 1.0);\n"; break;
        case D3DFOG_EXP2: s += "    v_fog = clamp(exp(-(u_fog.z * fz) * (u_fog.z * fz)), 0.0, 1.0);\n"; break;
        default: s += "    v_fog = 1.0;\n"; break;
        }
    }

    // Texture coordinates.
    for (int i = 0; i < MAX_STAGES; ++i)
    {
        if (!(key.tcMask & (1u << i))) continue;
        const StageKey &st = key.stage[i];
        std::string expr;
        switch (st.texGen)
        {
        case 1: expr = "vec4(eyeNormal, 1.0)"; break;
        case 2: expr = "vec4(eyePos.xyz, 1.0)"; break;
        case 3: expr = "vec4(reflect(normalize(eyePos.xyz), normalize(eyeNormal)), 1.0)"; break;
        default:
        {
            char b[32];
            snprintf(b, sizeof b, "a_t%d", st.coordIndex & 7);
            expr = b;
            break;
        }
        }
        if (st.xformCount)
            Add(s, "    v_t%d = u_tm[%d] * %s;\n", i, i, expr.c_str());
        else
            Add(s, "    v_t%d = %s;\n", i, expr.c_str());
    }

    // User clip planes (world space plane equations).
    for (int i = 0; i < MAX_CLIP_PLANES; ++i)
        if (key.clipMask & (1u << i))
        {
            if (key.rhw) Add(s, "    v_clip%d = 1.0;\n", i);
            else Add(s, "    v_clip%d = dot(u_clip[%d], u_world * vec4(a_pos.xyz, 1.0));\n", i, i);
        }

    // Point size.
    if (key.hasPSize) s += "    float psz = a_psize.x;\n";
    else s += "    float psz = u_point.x;\n";
    if (key.pointScale && !key.rhw)
    {
        s += "    float pd = length(eyePos.xyz);\n";
        s += "    psz = u_point.w * psz * inversesqrt(max(u_pointatt.x + u_pointatt.y * pd + u_pointatt.z * pd * pd, 1e-12));\n";
    }
    s += "    gl_PointSize = clamp(psz, u_point.y, u_point.z);\n";
    s += "}\n";
    return s;
}

//------------------------------------------------------------------------------
// Fragment stage
//------------------------------------------------------------------------------
namespace {

std::string ArgExpr(uint8_t arg, bool colorPipe)
{
    std::string base;
    switch (arg & 0x0F)
    {
    case D3DTA_DIFFUSE: base = "diff"; break;
    case D3DTA_CURRENT: base = "cur"; break;
    case D3DTA_TEXTURE: base = "tex"; break;
    case D3DTA_TFACTOR: base = "u_tfactor"; break;
    case D3DTA_SPECULAR: base = "spec"; break;
    case D3DTA_TEMP: base = "tmp"; break;
    default: base = "cur"; break;
    }
    if ((arg & D3DTA_ALPHAREPLICATE) && colorPipe) base = "vec4(" + base + ".a)";
    if (arg & D3DTA_COMPLEMENT) base = "(vec4(1.0) - " + base + ")";
    return base;
}

/// Returns the expression of a texture operation on vec4 operands a0..a2.
std::string OpExpr(uint8_t op, const char *a0, const char *a1, const char *a2)
{
    char b[512];
    switch (op)
    {
    case D3DTOP_SELECTARG1: snprintf(b, sizeof b, "%s", a1); break;
    case D3DTOP_SELECTARG2: snprintf(b, sizeof b, "%s", a2); break;
    case D3DTOP_MODULATE: snprintf(b, sizeof b, "%s * %s", a1, a2); break;
    case D3DTOP_MODULATE2X: snprintf(b, sizeof b, "%s * %s * 2.0", a1, a2); break;
    case D3DTOP_MODULATE4X: snprintf(b, sizeof b, "%s * %s * 4.0", a1, a2); break;
    case D3DTOP_ADD: snprintf(b, sizeof b, "%s + %s", a1, a2); break;
    case D3DTOP_ADDSIGNED: snprintf(b, sizeof b, "%s + %s - 0.5", a1, a2); break;
    case D3DTOP_ADDSIGNED2X: snprintf(b, sizeof b, "(%s + %s - 0.5) * 2.0", a1, a2); break;
    case D3DTOP_SUBTRACT: snprintf(b, sizeof b, "%s - %s", a1, a2); break;
    case D3DTOP_ADDSMOOTH: snprintf(b, sizeof b, "%s + %s * (vec4(1.0) - %s)", a1, a2, a1); break;
    case D3DTOP_BLENDDIFFUSEALPHA: snprintf(b, sizeof b, "mix(%s, %s, diff.a)", a2, a1); break;
    case D3DTOP_BLENDTEXTUREALPHA: snprintf(b, sizeof b, "mix(%s, %s, tex.a)", a2, a1); break;
    case D3DTOP_BLENDFACTORALPHA: snprintf(b, sizeof b, "mix(%s, %s, u_tfactor.a)", a2, a1); break;
    case D3DTOP_BLENDTEXTUREALPHAPM: snprintf(b, sizeof b, "%s + %s * (1.0 - tex.a)", a1, a2); break;
    case D3DTOP_BLENDCURRENTALPHA: snprintf(b, sizeof b, "mix(%s, %s, cur.a)", a2, a1); break;
    case D3DTOP_PREMODULATE: snprintf(b, sizeof b, "%s", a1); break;
    case D3DTOP_MODULATEALPHA_ADDCOLOR: snprintf(b, sizeof b, "%s + vec4(%s.a) * %s", a1, a1, a2); break;
    case D3DTOP_MODULATECOLOR_ADDALPHA: snprintf(b, sizeof b, "%s * %s + vec4(%s.a)", a1, a2, a1); break;
    case D3DTOP_MODULATEINVALPHA_ADDCOLOR: snprintf(b, sizeof b, "vec4(1.0 - %s.a) * %s + %s", a1, a2, a1); break;
    case D3DTOP_MODULATEINVCOLOR_ADDALPHA: snprintf(b, sizeof b, "(vec4(1.0) - %s) * %s + vec4(%s.a)", a1, a2, a1); break;
    case D3DTOP_DOTPRODUCT3: snprintf(b, sizeof b, "vec4(dot(%s.rgb - 0.5, %s.rgb - 0.5) * 4.0)", a1, a2); break;
    case D3DTOP_MULTIPLYADD: snprintf(b, sizeof b, "%s + %s * %s", a0, a1, a2); break;
    case D3DTOP_LERP: snprintf(b, sizeof b, "mix(%s, %s, %s)", a2, a1, a0); break;
    case D3DTOP_BUMPENVMAP:
    case D3DTOP_BUMPENVMAPLUMINANCE:
    default: snprintf(b, sizeof b, "%s", a1); break;
    }
    return b;
}

} // namespace

std::string FragmentTail(const ProgramKey &key)
{
    std::string s;
    if (key.fogEnable)
    {
        if (key.fogTableMode)
        {
            switch (key.fogTableMode)
            {
            case D3DFOG_LINEAR: s += "    float ff = clamp((u_fog.y - v_fz) * u_fog.w, 0.0, 1.0);\n"; break;
            case D3DFOG_EXP: s += "    float ff = clamp(exp(-u_fog.z * v_fz), 0.0, 1.0);\n"; break;
            default: s += "    float ff = clamp(exp(-(u_fog.z * v_fz) * (u_fog.z * v_fz)), 0.0, 1.0);\n"; break;
            }
        }
        else
            s += "    float ff = clamp(v_fog, 0.0, 1.0);\n";
        s += "    col.rgb = mix(u_fogColor, col.rgb, ff);\n";
    }

    if (key.alphaTest)
    {
        s += "    float a8 = floor(col.a * 255.0 + 0.5);\n";
        const char *cond = nullptr;
        switch (key.alphaFunc)
        {
        case D3DCMP_NEVER: cond = "false"; break;
        case D3DCMP_LESS: cond = "a8 < u_alphaRef"; break;
        case D3DCMP_EQUAL: cond = "a8 == u_alphaRef"; break;
        case D3DCMP_LESSEQUAL: cond = "a8 <= u_alphaRef"; break;
        case D3DCMP_GREATER: cond = "a8 > u_alphaRef"; break;
        case D3DCMP_NOTEQUAL: cond = "a8 != u_alphaRef"; break;
        case D3DCMP_GREATEREQUAL: cond = "a8 >= u_alphaRef"; break;
        default: cond = nullptr; break; // ALWAYS
        }
        if (cond) Add(s, "    if (!(%s)) discard;\n", cond);
    }
    s += "    fragColor = col;\n}\n";
    return s;
}

std::string FragmentCommonUniforms()
{
    return "uniform vec4 u_tfactor;\nuniform float u_alphaRef;\nuniform vec4 u_fog;\nuniform vec3 u_fogColor;\nuniform float u_lod[8];\n";
}

std::string TexCoordExpr(const ProgramKey &key, int stage)
{
    if (key.pointSprite) return "vec4(gl_PointCoord.x, 1.0 - gl_PointCoord.y, 0.0, 1.0)";
    char b[16];
    snprintf(b, sizeof b, "v_t%d", key.vsHandle ? key.stage[stage].coordIndex : stage);
    return b;
}

std::string GenerateFixedFunctionFragmentShader(const ProgramKey &key)
{
    std::string s;
    s += "#version 300 es\nprecision highp float;\nprecision highp int;\n";
    s += "precision highp sampler2D;\nprecision highp samplerCube;\nprecision highp sampler3D;\n";
    s += VaryingDeclarations(key, false, false);
    s += FragmentCommonUniforms();
    for (int i = 0; i < key.stageCount; ++i)
    {
        const StageKey &st = key.stage[i];
        if (st.texType == 1) Add(s, "uniform sampler2D s%d;\n", i);
        else if (st.texType == 2) Add(s, "uniform samplerCube s%d;\n", i);
        else if (st.texType == 3) Add(s, "uniform sampler3D s%d;\n", i);
    }
    s += "out vec4 fragColor;\n";
    s += "void main() {\n";
    for (int i = 0; i < MAX_CLIP_PLANES; ++i)
        if (key.clipMask & (1u << i)) Add(s, "    if (v_clip%d < 0.0) discard;\n", i);
    s += "    vec4 diff = v_c0;\n    vec4 spec = v_c1;\n    vec4 cur = diff;\n    vec4 tmp = vec4(0.0);\n";

    for (int i = 0; i < key.stageCount; ++i)
    {
        const StageKey &st = key.stage[i];
        s += "    {\n";
        // Texture sample.
        const std::string tc = TexCoordExpr(key, i);
        if (st.texType)
        {
            std::string coord, bias;
            if (st.lodBias) { char b[32]; snprintf(b, sizeof b, ", u_lod[%d]", i); bias = b; }
            if (st.texType == 1)
            {
                if (st.projected && st.xformCount >= 2)
                {
                    const char comp = "xyzw"[Clamp<int>(st.xformCount - 1, 1, 3)];
                    coord = "(" + tc + ".xy / " + tc + "." + comp + ")";
                }
                else coord = tc + ".xy";
            }
            else
            {
                if (st.projected && st.xformCount >= 3)
                {
                    const char comp = "xyzw"[Clamp<int>(st.xformCount - 1, 2, 3)];
                    coord = "(" + tc + ".xyz / " + tc + "." + comp + ")";
                }
                else coord = tc + ".xyz";
            }
            Add(s, "        vec4 tex = texture(s%d, %s%s);\n", i, coord.c_str(), bias.c_str());
        }
        else
            s += "        vec4 tex = vec4(1.0);\n";

        auto emitArg = [&](const char *name, uint8_t arg, bool colorPipe) {
            Add(s, "        vec4 %s = %s;\n", name, ArgExpr(arg, colorPipe).c_str());
        };
        emitArg("ca0", st.colorArg0, true); emitArg("ca1", st.colorArg1, true); emitArg("ca2", st.colorArg2, true);
        std::string cexpr = OpExpr(st.colorOp, "ca0", "ca1", "ca2");
        Add(s, "        vec4 cres = %s;\n", cexpr.c_str());
        if (st.alphaOp == D3DTOP_DISABLE)
            s += "        float ares = cur.a;\n";
        else
        {
            emitArg("aa0", st.alphaArg0, false); emitArg("aa1", st.alphaArg1, false); emitArg("aa2", st.alphaArg2, false);
            std::string aexpr = OpExpr(st.alphaOp, "aa0", "aa1", "aa2");
            Add(s, "        float ares = (%s).a;\n", aexpr.c_str());
        }
        Add(s, "        %s = clamp(vec4(cres.rgb, ares), 0.0, 1.0);\n", st.resultTemp ? "tmp" : "cur");
        s += "    }\n";
    }

    s += "    vec4 col = cur;\n";
    if (key.specularEnable) s += "    col.rgb = clamp(col.rgb + spec.rgb, 0.0, 1.0);\n";

    s += FragmentTail(key);
    return s;
}
} // namespace webd3d8
