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
** WebAssembly port: ps.1.0 - ps.1.4 pixel shader token stream -> GLSL ES 3.00.
**
** The shader is parsed once into a list of instructions. The GLSL is generated
** per program key because the sampler types (2D / cube / volume) and the
** texture coordinate source depend on the textures bound at draw time.
*/
#include "shader_internal.h"

#include <cstdarg>
#include <map>

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

} // namespace

struct PSInst
{
    DWORD op = 0;
    bool coissue = false;
    int n = 0;
    Param p[4];
};

struct PixelShaderObject::Impl
{
    uint32_t version = 0;
    std::vector<PSInst> insts;
    std::map<int, std::array<float, 4>> defs;
};

PixelShaderObject::~PixelShaderObject() { delete impl; }

bool CreatePixelShaderObject(const DWORD *function, PixelShaderObject **out)
{
    const DWORD version = function[0];
    if ((version & 0xFFFF0000) != 0xFFFF0000 || ((version >> 8) & 0xFF) != 1 || (version & 0xFF) > 4)
    {
        Log("CreatePixelShader: unsupported shader version 0x%08x", (unsigned)version);
        return false;
    }
    PixelShaderObject *ps = new PixelShaderObject();
    ps->impl = new PixelShaderObject::Impl();
    ps->version = version & 0xFFFF;
    ps->impl->version = ps->version;

    size_t n = 1;
    while (n < 65536 && function[n] != 0x0000FFFF)
    {
        DWORD tok = function[n];
        if ((tok & 0xFFFF) == D3DSIO_COMMENT) n += 1 + ((tok >> 16) & 0x7FFF);
        else ++n;
    }
    ps->bytecode.assign(function, function + n + 1);

    const DWORD *code = ps->bytecode.data();
    size_t count = ps->bytecode.size();
    size_t i = 1;
    uint32_t texMask = 0;
    while (i < count)
    {
        DWORD tok = code[i++];
        DWORD op = tok & 0xFFFF;
        if (op == D3DSIO_COMMENT) { i += (tok >> 16) & 0x7FFF; continue; }
        if (op == D3DSIO_END) break;
        if (op == D3DSIO_NOP || op == D3DSIO_PHASE) continue;
        if (op == D3DSIO_DEF)
        {
            if (i + 5 > count) { delete ps; return false; }
            Param d = DecodeParam(code[i]);
            std::array<float, 4> v;
            for (int c = 0; c < 4; ++c) memcpy(&v[c], &code[i + 1 + c], 4);
            ps->impl->defs[d.reg] = v;
            i += 5;
            continue;
        }
        int nops = OperandCount(op, true, ps->version);
        if (nops < 0 || i + nops > count)
        {
            Log("CreatePixelShader: unsupported instruction %u", (unsigned)op);
            delete ps;
            return false;
        }
        PSInst in;
        in.op = op;
        in.coissue = (tok & 0x40000000) != 0;
        in.n = nops;
        for (int k = 0; k < nops; ++k) in.p[k] = DecodeParam(code[i + k]);
        i += nops;
        switch (op)
        {
        case D3DSIO_TEXDP3: case D3DSIO_TEXDP3TEX: case D3DSIO_TEXM3x3: case D3DSIO_TEXM3x2DEPTH:
        case D3DSIO_TEXDEPTH: case D3DSIO_TEXM3x3DIFF:
            Log("CreatePixelShader: instruction %s is not supported", OpcodeName(op));
            delete ps;
            return false;
        default: break;
        }
        // Stages whose coordinates / samplers the shader touches.
        if (op >= D3DSIO_TEXCOORD && op <= D3DSIO_TEXM3x3VSPEC)
        {
            if (ps->version >= 0x0104 && (op == D3DSIO_TEX || op == D3DSIO_TEXCOORD))
            {
                // texld rN, tM / texcrd rN, tM
                if (op == D3DSIO_TEX) texMask |= 1u << in.p[0].reg;
                if (in.p[1].type == RT_ADDR_TEX) texMask |= 1u << in.p[1].reg;
            }
            else
            {
                texMask |= 1u << in.p[0].reg;
                if (nops > 1 && in.p[1].type == RT_ADDR_TEX) texMask |= 1u << in.p[1].reg;
            }
        }
        for (int k = 1; k < nops; ++k)
            if (in.p[k].type == RT_ADDR_TEX) texMask |= 1u << in.p[k].reg;
        ps->impl->insts.push_back(in);
    }
    ps->textureStageMask = texMask;
    *out = ps;
    return true;
}

namespace {

struct PSGen
{
    const PixelShaderObject::Impl &impl;
    const ProgramKey &key;
    std::string s;
    std::string error;
    std::string pendingEval, pendingStore;
    bool m3Used = false;

    PSGen(const PixelShaderObject::Impl &i, const ProgramKey &k) : impl(i), key(k) {}

    bool is14() const { return impl.version >= 0x0104; }
    const char *Range() const { return is14() ? "8.0" : "1.0"; }

    std::string RegName(const Param &p) const
    {
        switch (p.type)
        {
        case RT_TEMP: return Fmt("r%d", p.reg);
        case RT_INPUT: return Fmt("v%d", p.reg);
        case RT_ADDR_TEX: return Fmt("t%d", p.reg);
        case RT_CONST:
            if (impl.defs.count(p.reg)) return Fmt("d%d", p.reg);
            return Fmt("pc[%d]", Clamp(p.reg, 0, 7));
        default: return "vec4(0.0)";
        }
    }

    std::string Src(const Param &p) const
    {
        std::string e = RegName(p);
        if (!IsIdentitySwizzle(p)) e += SwizzleString(p);
        switch (p.srcMod << 24)
        {
        case D3DSPSM_NEG: return "(-" + e + ")";
        case D3DSPSM_BIAS: return "(" + e + " - 0.5)";
        case D3DSPSM_BIASNEG: return "(-(" + e + " - 0.5))";
        case D3DSPSM_SIGN: return "((" + e + " - 0.5) * 2.0)";
        case D3DSPSM_SIGNNEG: return "(-((" + e + " - 0.5) * 2.0))";
        case D3DSPSM_COMP: return "(1.0 - " + e + ")";
        case D3DSPSM_X2: return "(" + e + " * 2.0)";
        case D3DSPSM_X2NEG: return "(-(" + e + " * 2.0))";
        default: return e;
        }
    }

    /// Texture coordinate of stage `stage` (vec4, projected when requested).
    std::string Coord(int stage) const
    {
        std::string tc = TexCoordExpr(key, stage);
        const StageKey &st = key.stage[stage];
        if (st.projected && st.xformCount >= 2 && !key.pointSprite)
        {
            const char comp = "xyzw"[Clamp<int>(st.xformCount - 1, 1, 3)];
            return Fmt("vec4(%s.xyz / %s.%c, 1.0)", tc.c_str(), tc.c_str(), comp);
        }
        return tc;
    }

    std::string Sample(int stage, const std::string &coord) const
    {
        const StageKey &st = key.stage[stage];
        if (!st.texType) return "vec4(1.0)";
        std::string bias = st.lodBias ? Fmt(", u_lod[%d]", stage) : "";
        if (st.texType == 1) return Fmt("texture(s%d, (%s).xy%s)", stage, coord.c_str(), bias.c_str());
        return Fmt("texture(s%d, (%s).xyz%s)", stage, coord.c_str(), bias.c_str());
    }

    void Flush()
    {
        s += pendingEval;
        s += pendingStore;
        pendingEval.clear();
        pendingStore.clear();
    }

    /// Queues `dst = expr` with write mask / modifiers. Instruction results of a co-issued pair
    /// are all evaluated before any of them is stored.
    void Emit(const PSInst &in, const std::string &expr, bool applyRange = true)
    {
        const Param &d = in.p[0];
        const int id = tmpCounter++;
        std::string e = expr;
        if (d.shift > 0) e = Fmt("(%s) * %d.0", e.c_str(), 1 << d.shift);
        else if (d.shift < 0) e = Fmt("(%s) / %d.0", e.c_str(), 1 << -d.shift);
        if (d.saturate) e = "clamp(" + e + ", 0.0, 1.0)";
        else if (applyRange) e = Fmt("clamp(%s, -%s, %s)", e.c_str(), Range(), Range());
        const std::string name = RegName(d);
        if (!in.coissue) Flush();
        else if (pendingEval.empty()) { /* orphan co-issue: treat as plain */ }
        pendingEval += Fmt("    vec4 e%d = %s;\n", id, e.c_str());
        if (d.writeMask == 0xF) pendingStore += Fmt("    %s = e%d;\n", name.c_str(), id);
        else
        {
            std::string m = MaskString(d.writeMask);
            pendingStore += Fmt("    %s.%s = e%d.%s;\n", name.c_str(), m.c_str(), id, m.c_str());
        }
    }

    int tmpCounter = 0;

    bool Run()
    {
        for (const PSInst &in : impl.insts)
        {
            if (!Instruction(in)) return false;
        }
        Flush();
        return error.empty();
    }

    bool Instruction(const PSInst &in)
    {
        const Param *p = in.p;
        const DWORD op = in.op;
        const bool ps14 = is14();
        std::string s0, s1, s2;
        auto src = [&](int i) { return Src(p[i]); };

        switch (op)
        {
        case D3DSIO_MOV: Emit(in, src(1)); return true;
        case D3DSIO_ADD: Emit(in, src(1) + " + " + src(2)); return true;
        case D3DSIO_SUB: Emit(in, src(1) + " - " + src(2)); return true;
        case D3DSIO_MUL: Emit(in, src(1) + " * " + src(2)); return true;
        case D3DSIO_MAD: Emit(in, src(1) + " * " + src(2) + " + " + src(3)); return true;
        case D3DSIO_LRP: Emit(in, Fmt("mix(%s, %s, %s)", src(3).c_str(), src(2).c_str(), src(1).c_str())); return true;
        case D3DSIO_DP3: Emit(in, Fmt("vec4(dot((%s).xyz, (%s).xyz))", src(1).c_str(), src(2).c_str())); return true;
        case D3DSIO_DP4: Emit(in, Fmt("vec4(dot(%s, %s))", src(1).c_str(), src(2).c_str())); return true;
        case D3DSIO_CND:
            Emit(in, Fmt("mix(%s, %s, vec4(greaterThan(%s, vec4(0.5))))", src(3).c_str(), src(2).c_str(), src(1).c_str()));
            return true;
        case D3DSIO_CMP:
            Emit(in, Fmt("mix(%s, %s, vec4(greaterThanEqual(%s, vec4(0.0))))", src(3).c_str(), src(2).c_str(), src(1).c_str()));
            return true;
        case D3DSIO_BEM:
        {
            int stage = p[0].reg;
            std::string a = src(1), b = src(2);
            Emit(in, Fmt("vec4((%s).x + u_bump[%d].x * (%s).x + u_bump[%d].z * (%s).y, (%s).y + u_bump[%d].y * (%s).x + u_bump[%d].w * (%s).y, 0.0, 0.0)",
                         a.c_str(), stage, b.c_str(), stage, b.c_str(), a.c_str(), stage, b.c_str(), stage, b.c_str()));
            return true;
        }
        default: break;
        }

        // Texture addressing instructions.
        Flush();
        if (ps14)
        {
            if (op == D3DSIO_TEX)
            {
                // texld rN, tM|rM: sampler N, coordinates from the source register.
                const int stage = p[0].reg;
                std::string coord = RegName(p[1]);
                if (!IsIdentitySwizzle(p[1])) coord += SwizzleString(p[1]);
                if (p[1].type == RT_ADDR_TEX) coord = Coord(p[1].reg);
                if ((p[1].srcMod << 24) == D3DSPSM_DZ) coord = Fmt("vec4((%s).xy / (%s).z, 0.0, 1.0)", coord.c_str(), coord.c_str());
                else if ((p[1].srcMod << 24) == D3DSPSM_DW) coord = Fmt("vec4((%s).xy / (%s).w, 0.0, 1.0)", coord.c_str(), coord.c_str());
                PSInst tmp = in;
                tmp.coissue = false;
                EmitTex(tmp, Sample(stage, coord));
                return true;
            }
            if (op == D3DSIO_TEXCOORD)
            {
                PSInst tmp = in;
                std::string coord = Coord(p[1].reg);
                EmitTex(tmp, Fmt("vec4((%s).xyz, 1.0)", coord.c_str()));
                return true;
            }
            error = Fmt("instruction %s is not valid in ps.1.4", OpcodeName(op));
            return false;
        }

        const int n = p[0].reg;
        const std::string t = Fmt("t%d", n);
        switch (op)
        {
        case D3DSIO_TEX:
            s += Fmt("    %s = %s;\n", t.c_str(), Sample(n, Coord(n)).c_str());
            return true;
        case D3DSIO_TEXCOORD:
            s += Fmt("    %s = vec4(clamp((%s).xyz, 0.0, 1.0), 1.0);\n", t.c_str(), Coord(n).c_str());
            return true;
        case D3DSIO_TEXKILL:
            s += Fmt("    if (any(lessThan(%s.xyz, vec3(0.0)))) discard;\n", t.c_str());
            return true;
        case D3DSIO_TEXBEM: case D3DSIO_TEXBEML:
        {
            const std::string m = Fmt("t%d", p[1].reg);
            s += Fmt("    { vec2 uv = (%s).xy + vec2(u_bump[%d].x * %s.r + u_bump[%d].z * %s.g, u_bump[%d].y * %s.r + u_bump[%d].w * %s.g);\n",
                     Coord(n).c_str(), n, m.c_str(), n, m.c_str(), n, m.c_str(), n, m.c_str());
            s += Fmt("      %s = %s;\n", t.c_str(), Sample(n, "vec4(uv, 0.0, 1.0)").c_str());
            if (op == D3DSIO_TEXBEML)
                s += Fmt("      %s.rgb *= clamp(%s.b * u_bumpl[%d].x + u_bumpl[%d].y, 0.0, 1.0); }\n", t.c_str(), m.c_str(), n, n);
            else
                s += "    }\n";
            return true;
        }
        case D3DSIO_TEXREG2AR:
            s += Fmt("    %s = %s;\n", t.c_str(), Sample(n, Fmt("vec4(t%d.a, t%d.r, 0.0, 1.0)", p[1].reg, p[1].reg)).c_str());
            return true;
        case D3DSIO_TEXREG2GB:
            s += Fmt("    %s = %s;\n", t.c_str(), Sample(n, Fmt("vec4(t%d.g, t%d.b, 0.0, 1.0)", p[1].reg, p[1].reg)).c_str());
            return true;
        case D3DSIO_TEXREG2RGB:
            s += Fmt("    %s = %s;\n", t.c_str(), Sample(n, Fmt("vec4(t%d.rgb, 1.0)", p[1].reg)).c_str());
            return true;
        case D3DSIO_TEXM3x2PAD:
            m3Used = true;
            s += Fmt("    m3d0 = dot(%s.xyz, t%d.xyz);\n", Coord(n).c_str(), p[1].reg);
            return true;
        case D3DSIO_TEXM3x2TEX:
            m3Used = true;
            s += Fmt("    %s = %s;\n", t.c_str(),
                     Sample(n, Fmt("vec4(m3d0, dot(%s.xyz, t%d.xyz), 0.0, 1.0)", Coord(n).c_str(), p[1].reg)).c_str());
            return true;
        case D3DSIO_TEXM3x3PAD:
            m3Used = true;
            s += Fmt("    if (m3count == 0) { m3d0 = dot(%s.xyz, t%d.xyz); m3w0 = %s.w; m3count = 1; } else { m3d1 = dot(%s.xyz, t%d.xyz); m3w1 = %s.w; }\n",
                     Coord(n).c_str(), p[1].reg, Coord(n).c_str(), Coord(n).c_str(), p[1].reg, Coord(n).c_str());
            return true;
        case D3DSIO_TEXM3x3TEX:
            m3Used = true;
            s += Fmt("    %s = %s;\n", t.c_str(),
                     Sample(n, Fmt("vec4(m3d0, m3d1, dot(%s.xyz, t%d.xyz), 1.0)", Coord(n).c_str(), p[1].reg)).c_str());
            return true;
        case D3DSIO_TEXM3x3SPEC: case D3DSIO_TEXM3x3VSPEC:
        {
            m3Used = true;
            std::string eye = (op == D3DSIO_TEXM3x3SPEC) ? Fmt("(%s).xyz", Src(p[2]).c_str()) : Fmt("vec3(m3w0, m3w1, %s.w)", Coord(n).c_str());
            s += Fmt("    { vec3 nn = vec3(m3d0, m3d1, dot(%s.xyz, t%d.xyz)); vec3 ee = %s;\n", Coord(n).c_str(), p[1].reg, eye.c_str());
            s += "      vec3 rr = 2.0 * (dot(nn, ee) / max(dot(nn, nn), 1e-12)) * nn - ee;\n";
            s += Fmt("      %s = %s; }\n", t.c_str(), Sample(n, "vec4(rr, 1.0)").c_str());
            return true;
        }
        default:
            error = Fmt("unsupported pixel shader instruction %s", OpcodeName(op));
            return false;
        }
    }

    void EmitTex(const PSInst &in, const std::string &expr)
    {
        // Texture loads write the register without range clamping.
        Emit(in, expr, false);
    }
};

} // namespace

std::string PixelShaderObject::Generate(const ProgramKey &key) const
{
    PSGen gen(*impl, key);
    if (!gen.Run())
    {
        Log("pixel shader translation failed: %s", gen.error.c_str());
        return "";
    }
    std::string s;
    s += "#version 300 es\nprecision highp float;\nprecision highp int;\n";
    s += "precision highp sampler2D;\nprecision highp samplerCube;\nprecision highp sampler3D;\n";
    s += VaryingDeclarations(key, false, false);
    s += FragmentCommonUniforms();
    s += "uniform vec4 pc[8];\nuniform vec4 u_bump[8];\nuniform vec2 u_bumpl[8];\n";
    for (int i = 0; i < MAX_STAGES; ++i)
    {
        const StageKey &st = key.stage[i];
        if (!(textureStageMask & (1u << i)) || !st.texType) continue;
        if (st.texType == 1) s += Fmt("uniform sampler2D s%d;\n", i);
        else if (st.texType == 2) s += Fmt("uniform samplerCube s%d;\n", i);
        else s += Fmt("uniform sampler3D s%d;\n", i);
    }
    s += "out vec4 fragColor;\nvoid main() {\n";
    for (int i = 0; i < MAX_CLIP_PLANES; ++i)
        if (key.clipMask & (1u << i)) s += Fmt("    if (v_clip%d < 0.0) discard;\n", i);
    for (const auto &d : impl->defs)
        s += Fmt("    const vec4 d%d = vec4(%.9g, %.9g, %.9g, %.9g);\n", d.first, d.second[0], d.second[1], d.second[2], d.second[3]);
    s += "    vec4 v0 = v_c0;\n    vec4 v1 = v_c1;\n";
    s += "    vec4 r0 = vec4(0.0), r1 = vec4(0.0), r2 = vec4(0.0), r3 = vec4(0.0), r4 = vec4(0.0), r5 = vec4(0.0);\n";
    for (int i = 0; i < 6; ++i)
    {
        // Texture registers start out as the interpolated coordinates.
        if ((textureStageMask & (1u << i)) && i < MAX_STAGES)
            s += Fmt("    vec4 t%d = %s;\n", i, gen.Coord(i).c_str());
        else
            s += Fmt("    vec4 t%d = vec4(0.0);\n", i);
    }
    if (gen.m3Used)
        s += "    float m3d0 = 0.0, m3d1 = 0.0, m3w0 = 0.0, m3w1 = 0.0; int m3count = 0;\n";
    s += gen.s;
    s += "    vec4 col = clamp(r0, 0.0, 1.0);\n";
    s += FragmentTail(key);
    return s;
}

} // namespace webd3d8
