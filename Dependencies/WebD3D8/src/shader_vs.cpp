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
** WebAssembly port: vs.1.x vertex shader token stream -> GLSL ES 3.00.
**
** The body is straight-line code over vec4 registers r0..r11, v0..v15,
** the constant array c[96] and the address register a0, producing oPos, oD0,
** oD1, oFog, oPts and oT0..oT3. BuildTranslatedVertexShader() wraps it with
** the declarations and the D3D -> GL clip space conversion (see device.h).
*/
#include "shader_internal.h"

#include <cstdarg>

namespace webd3d8 {

namespace {

std::string Fmt(const char *fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    return buf;
}

struct VSTranslator
{
    VertexShaderObject &vs;
    std::string body;
    std::string error;
    bool writesPts = false;
    bool writesFog = false;

    explicit VSTranslator(VertexShaderObject &o) : vs(o) {}

    std::string Base(const Param &p, int offset = 0)
    {
        switch (p.type)
        {
        case RT_TEMP: return Fmt("r%d", p.reg + offset);
        case RT_INPUT:
            vs.inputMask |= 1u << (p.reg + offset);
            return Fmt("v%d", p.reg + offset);
        case RT_CONST:
            if (p.relative) return Fmt("c[clamp(a0 + %d, 0, %d)]", p.reg + offset, VS_CONSTANTS - 1);
            return Fmt("c[%d]", Clamp(p.reg + offset, 0, VS_CONSTANTS - 1));
        default: error = "unsupported source register type"; return "vec4(0.0)";
        }
    }

    /// Source operand as a vec4 expression.
    std::string Src(const Param &p, int offset = 0)
    {
        std::string e = Base(p, offset);
        if (!IsIdentitySwizzle(p)) e += SwizzleString(p);
        if (p.srcMod == (D3DSPSM_NEG >> 24)) e = "(-" + e + ")";
        return e;
    }

    std::string DestName(const Param &p)
    {
        switch (p.type)
        {
        case RT_TEMP: return Fmt("r%d", p.reg);
        case RT_RASTOUT:
            if (p.reg == 0) return "oPos";
            if (p.reg == 1) { writesFog = true; return "oFog"; }
            writesPts = true;
            return "oPts";
        case RT_ATTROUT: return Fmt("oD%d", Clamp(p.reg, 0, 1));
        case RT_TEXCRDOUT: return Fmt("oT%d", Clamp(p.reg, 0, 3));
        default: error = "unsupported destination register type"; return "r0";
        }
    }

    void Assign(const Param &d, const std::string &expr)
    {
        if (d.type == RT_ADDR_TEX)
        {
            body += Fmt("    a0 = int(floor((%s).x));\n", expr.c_str());
            return;
        }
        std::string name = DestName(d);
        if (d.writeMask == 0xF)
            body += Fmt("    %s = %s;\n", name.c_str(), expr.c_str());
        else
        {
            std::string m = MaskString(d.writeMask);
            body += Fmt("    %s.%s = (%s).%s;\n", name.c_str(), m.c_str(), expr.c_str(), m.c_str());
        }
    }

    bool Run(const DWORD *code, size_t count)
    {
        size_t i = 1;
        while (i < count)
        {
            DWORD tok = code[i++];
            DWORD op = tok & 0xFFFF;
            if (op == D3DSIO_COMMENT) { i += (tok >> 16) & 0x7FFF; continue; }
            if (op == D3DSIO_END) break;
            if (op == D3DSIO_NOP) continue;
            if (op == D3DSIO_DEF)
            {
                if (i + 5 > count) { error = "truncated def"; return false; }
                Param d = DecodeParam(code[i]);
                int n = d.reg;
                if (d.type != RT_CONST || n >= VS_CONSTANTS) { error = "invalid def register"; return false; }
                for (int c = 0; c < 4; ++c) memcpy(&vs.defConstants[n][c], &code[i + 1 + c], 4);
                vs.defMask[n] = 1;
                i += 5;
                continue;
            }
            int n = OperandCount(op, false, 0x0101);
            if (n < 0) { error = Fmt("unsupported vertex shader instruction %u", (unsigned)op); return false; }
            if (i + n > count) { error = "truncated instruction"; return false; }
            Param p[4];
            for (int k = 0; k < n; ++k) p[k] = DecodeParam(code[i + k]);
            i += n;
            if (!Instruction(op, p, n)) return false;
        }
        return error.empty();
    }

    bool Instruction(DWORD op, Param *p, int n)
    {
        (void)n;
        const Param &d = p[0];
        std::string s0, s1, s2;
        if (n > 1) s0 = Src(p[1]);
        if (n > 2) s1 = Src(p[2]);
        if (n > 3) s2 = Src(p[3]);
        std::string e;
        switch (op)
        {
        case D3DSIO_MOV: e = s0; break;
        case D3DSIO_ADD: e = Fmt("%s + %s", s0.c_str(), s1.c_str()); break;
        case D3DSIO_SUB: e = Fmt("%s - %s", s0.c_str(), s1.c_str()); break;
        case D3DSIO_MUL: e = Fmt("%s * %s", s0.c_str(), s1.c_str()); break;
        case D3DSIO_MAD: e = Fmt("%s * %s + %s", s0.c_str(), s1.c_str(), s2.c_str()); break;
        case D3DSIO_RCP: e = Fmt("vec4(d3d_rcp((%s).x))", s0.c_str()); break;
        case D3DSIO_RSQ: e = Fmt("vec4(d3d_rsq((%s).x))", s0.c_str()); break;
        case D3DSIO_DP3: e = Fmt("vec4(dot((%s).xyz, (%s).xyz))", s0.c_str(), s1.c_str()); break;
        case D3DSIO_DP4: e = Fmt("vec4(dot(%s, %s))", s0.c_str(), s1.c_str()); break;
        case D3DSIO_MIN: e = Fmt("min(%s, %s)", s0.c_str(), s1.c_str()); break;
        case D3DSIO_MAX: e = Fmt("max(%s, %s)", s0.c_str(), s1.c_str()); break;
        case D3DSIO_SLT: e = Fmt("vec4(lessThan(%s, %s))", s0.c_str(), s1.c_str()); break;
        case D3DSIO_SGE: e = Fmt("vec4(greaterThanEqual(%s, %s))", s0.c_str(), s1.c_str()); break;
        case D3DSIO_EXP: case D3DSIO_EXPP:
            e = Fmt("d3d_exp((%s).x)", s0.c_str());
            break;
        case D3DSIO_LOG: case D3DSIO_LOGP:
            e = Fmt("d3d_log((%s).x)", s0.c_str());
            break;
        case D3DSIO_LIT: e = Fmt("d3d_lit(%s)", s0.c_str()); break;
        case D3DSIO_DST: e = Fmt("vec4(1.0, (%s).y * (%s).y, (%s).z, (%s).w)", s0.c_str(), s1.c_str(), s0.c_str(), s1.c_str()); break;
        case D3DSIO_FRC: e = Fmt("fract(%s)", s0.c_str()); break;
        case D3DSIO_M4x4: case D3DSIO_M4x3: case D3DSIO_M3x4: case D3DSIO_M3x3: case D3DSIO_M3x2:
        {
            const int rows = (op == D3DSIO_M4x4 || op == D3DSIO_M3x4) ? 4 : (op == D3DSIO_M4x3 || op == D3DSIO_M3x3 ? 3 : 2);
            const bool four = (op == D3DSIO_M4x4 || op == D3DSIO_M4x3);
            std::string v = four ? s0 : "(" + s0 + ").xyz";
            std::string comp[4] = {"0.0", "0.0", "0.0", "1.0"};
            for (int r = 0; r < rows; ++r)
            {
                std::string row = Base(p[2], r);
                if (!four) row += ".xyz";
                comp[r] = Fmt("dot(%s, %s)", v.c_str(), row.c_str());
            }
            e = Fmt("vec4(%s, %s, %s, %s)", comp[0].c_str(), comp[1].c_str(), comp[2].c_str(), comp[3].c_str());
            break;
        }
        default:
            error = Fmt("unsupported vertex shader instruction %s", OpcodeName(op));
            return false;
        }
        Assign(d, e);
        return true;
    }
};

const char *kVSHelpers =
    "float d3d_rcp(float x) { return x == 0.0 ? 3.402823e38 : 1.0 / x; }\n"
    "float d3d_rsq(float x) { x = abs(x); return x == 0.0 ? 3.402823e38 : inversesqrt(x); }\n"
    "vec4 d3d_exp(float x) { float f = floor(x); return vec4(exp2(f), x - f, exp2(x), 1.0); }\n"
    "vec4 d3d_log(float x) { float a = abs(x); float l = (a == 0.0) ? -3.402823e38 : log2(a); float f = floor(l);\n"
    "    return vec4(f, a / exp2(f), l, 1.0); }\n"
    "vec4 d3d_lit(vec4 s) {\n"
    "    float d = max(s.x, 0.0);\n"
    "    float sp = (s.x > 0.0) ? pow(max(s.y, 0.0), clamp(s.w, -127.9961, 127.9961)) : 0.0;\n"
    "    return vec4(1.0, d, sp, 1.0); }\n";

} // namespace

bool CreateVertexShaderObject(const DWORD *declaration, const DWORD *function, VertexShaderObject **out)
{
    VertexShaderObject *vs = new VertexShaderObject();
    memset(vs->defConstants, 0, sizeof vs->defConstants);
    memset(vs->defMask, 0, sizeof vs->defMask);
    std::vector<float> consts;
    if (!ParseVertexDeclaration(declaration, vs->layout, &consts))
    {
        Log("CreateVertexShader: invalid declaration");
        delete vs;
        return false;
    }
    // Keep a copy of the declaration (up to and including the END token).
    for (size_t i = 0; i < 1024; ++i)
    {
        vs->declaration.push_back(declaration[i]);
        if (declaration[i] == D3DVSD_END()) break;
    }
    for (size_t i = 0; i + 4 < consts.size(); i += 5)
    {
        int reg = (int)consts[i];
        if (reg >= 0 && reg < VS_CONSTANTS)
        {
            for (int c = 0; c < 4; ++c) vs->defConstants[reg][c] = consts[i + 1 + c];
            vs->defMask[reg] = 1;
        }
    }
    for (int i = 0; i < vs->layout.count; ++i)
        if (vs->layout.elems[i].d3dColor) vs->d3dColorMask |= 1u << vs->layout.elems[i].reg;

    if (function)
    {
        const DWORD version = function[0];
        if ((version & 0xFFFF0000) != 0xFFFE0000 || ((version >> 8) & 0xFF) != 1)
        {
            Log("CreateVertexShader: unsupported shader version 0x%08x", (unsigned)version);
            delete vs;
            return false;
        }
        size_t n = 1;
        while (n < 65536 && function[n] != 0x0000FFFF)
        {
            DWORD tok = function[n];
            if ((tok & 0xFFFF) == D3DSIO_COMMENT) n += 1 + ((tok >> 16) & 0x7FFF);
            else ++n;
        }
        vs->bytecode.assign(function, function + n + 1);
        VSTranslator tr(*vs);
        if (!tr.Run(vs->bytecode.data(), vs->bytecode.size()))
        {
            Log("CreateVertexShader: %s", tr.error.c_str());
            delete vs;
            return false;
        }
        vs->hasFunction = true;
        vs->body = tr.body;
        vs->writesPointSize = tr.writesPts;
        vs->writesFog = tr.writesFog;
    }
    *out = vs;
    return true;
}

std::string BuildTranslatedVertexShader(const VertexShaderObject &vs, const ProgramKey &key)
{
    std::string s;
    s += "#version 300 es\nprecision highp float;\nprecision highp int;\n";
    s += "uniform vec4 c[96];\nuniform vec2 u_pix;\nuniform vec4 u_point;\nuniform vec4 u_clip[6];\n";
    for (int i = 0; i < 16; ++i)
        if (vs.inputMask & (1u << i)) s += Fmt("layout(location=%d) in vec4 in_v%d;\n", i, i);
    s += VaryingDeclarations(key, true, true);
    s += kVSHelpers;
    s += "void main() {\n";
    for (int i = 0; i < 16; ++i)
        if (vs.inputMask & (1u << i))
            s += Fmt("    vec4 v%d = in_v%d%s;\n", i, i, (vs.d3dColorMask & (1u << i)) ? ".bgra" : "");
    s += "    vec4 r0 = vec4(0.0), r1 = vec4(0.0), r2 = vec4(0.0), r3 = vec4(0.0), r4 = vec4(0.0), r5 = vec4(0.0);\n";
    s += "    vec4 r6 = vec4(0.0), r7 = vec4(0.0), r8 = vec4(0.0), r9 = vec4(0.0), r10 = vec4(0.0), r11 = vec4(0.0);\n";
    s += "    int a0 = 0;\n";
    s += "    vec4 oPos = vec4(0.0, 0.0, 0.0, 1.0), oD0 = vec4(0.0), oD1 = vec4(0.0), oFog = vec4(1.0), oPts = vec4(1.0);\n";
    s += "    vec4 oT0 = vec4(0.0), oT1 = vec4(0.0), oT2 = vec4(0.0), oT3 = vec4(0.0);\n";
    s += vs.body;
    s += VertexEpilogue();
    s += "    v_c0 = clamp(oD0, 0.0, 1.0);\n    v_c1 = clamp(oD1, 0.0, 1.0);\n";
    s += vs.writesFog ? "    v_fog = clamp(oFog.x, 0.0, 1.0);\n" : "    v_fog = 1.0;\n";
    s += "    v_fz = oPos.w;\n";
    for (int i = 0; i < 4; ++i)
        s += Fmt("    v_t%d = oT%d;\n", i, i);
    for (int i = 0; i < MAX_CLIP_PLANES; ++i)
        if (key.clipMask & (1u << i)) s += Fmt("    v_clip%d = dot(u_clip[%d], oPos);\n", i, i);
    if (vs.writesPointSize) s += "    gl_PointSize = clamp(oPts.x, u_point.y, u_point.z);\n";
    else s += "    gl_PointSize = clamp(u_point.x, u_point.y, u_point.z);\n";
    s += "}\n";
    return s;
}

} // namespace webd3d8
