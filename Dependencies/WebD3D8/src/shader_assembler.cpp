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
** WebAssembly port: vs.1.x / ps.1.x assembler (the part of D3DXAssembleShader
** the game uses for its inline pixel shaders) and the opcode tables shared with
** the translators.
*/
#include "shader_internal.h"

#include <cctype>
#include <cstdlib>

namespace webd3d8 {

namespace {

struct OpInfo
{
    const char *name;
    DWORD op;
    int vsOperands;  ///< -1 = not a vertex shader instruction
    int psOperands;  ///< -1 = not a pixel shader instruction
};

const OpInfo kOps[] = {
    {"nop", D3DSIO_NOP, 0, 0},       {"mov", D3DSIO_MOV, 2, 2},       {"add", D3DSIO_ADD, 3, 3},
    {"sub", D3DSIO_SUB, 3, 3},      {"mad", D3DSIO_MAD, 4, 4},       {"mul", D3DSIO_MUL, 3, 3},
    {"rcp", D3DSIO_RCP, 2, -1},      {"rsq", D3DSIO_RSQ, 2, -1},      {"dp3", D3DSIO_DP3, 3, 3},
    {"dp4", D3DSIO_DP4, 3, 3},       {"min", D3DSIO_MIN, 3, -1},      {"max", D3DSIO_MAX, 3, -1},
    {"slt", D3DSIO_SLT, 3, -1},      {"sge", D3DSIO_SGE, 3, -1},      {"exp", D3DSIO_EXP, 2, -1},
    {"log", D3DSIO_LOG, 2, -1},      {"lit", D3DSIO_LIT, 2, -1},      {"dst", D3DSIO_DST, 3, -1},
    {"lrp", D3DSIO_LRP, -1, 4},      {"frc", D3DSIO_FRC, 2, -1},      {"m4x4", D3DSIO_M4x4, 3, -1},
    {"m4x3", D3DSIO_M4x3, 3, -1},    {"m3x4", D3DSIO_M3x4, 3, -1},    {"m3x3", D3DSIO_M3x3, 3, -1},
    {"m3x2", D3DSIO_M3x2, 3, -1},    {"expp", D3DSIO_EXPP, 2, -1},    {"logp", D3DSIO_LOGP, 2, -1},
    {"texcoord", D3DSIO_TEXCOORD, -1, 1}, {"texcrd", D3DSIO_TEXCOORD, -1, 2}, {"texkill", D3DSIO_TEXKILL, -1, 1},
    {"tex", D3DSIO_TEX, -1, 1},      {"texld", D3DSIO_TEX, -1, 2},    {"texbem", D3DSIO_TEXBEM, -1, 2},
    {"texbeml", D3DSIO_TEXBEML, -1, 2}, {"texreg2ar", D3DSIO_TEXREG2AR, -1, 2}, {"texreg2gb", D3DSIO_TEXREG2GB, -1, 2},
    {"texm3x2pad", D3DSIO_TEXM3x2PAD, -1, 2}, {"texm3x2tex", D3DSIO_TEXM3x2TEX, -1, 2},
    {"texm3x3pad", D3DSIO_TEXM3x3PAD, -1, 2}, {"texm3x3tex", D3DSIO_TEXM3x3TEX, -1, 2},
    {"texm3x3spec", D3DSIO_TEXM3x3SPEC, -1, 3}, {"texm3x3vspec", D3DSIO_TEXM3x3VSPEC, -1, 2},
    {"cnd", D3DSIO_CND, -1, 4},      {"def", D3DSIO_DEF, 5, 5},       {"texreg2rgb", D3DSIO_TEXREG2RGB, -1, 2},
    {"texdp3tex", D3DSIO_TEXDP3TEX, -1, 2}, {"texdp3", D3DSIO_TEXDP3, -1, 2}, {"texm3x3", D3DSIO_TEXM3x3, -1, 2},
    {"texdepth", D3DSIO_TEXDEPTH, -1, 1}, {"cmp", D3DSIO_CMP, -1, 4}, {"bem", D3DSIO_BEM, -1, 4},
    {"texm3x2depth", D3DSIO_TEXM3x2DEPTH, -1, 2}, {"texm3x3diff", D3DSIO_TEXM3x3DIFF, -1, 2},
    {"phase", D3DSIO_PHASE, 0, 0},
};

} // namespace

int OperandCount(DWORD opcode, bool pixelShader, DWORD version)
{
    for (const OpInfo &o : kOps)
    {
        if (o.op != opcode) continue;
        // texcoord/tex have different forms before/after ps.1.4.
        if (pixelShader && (opcode == D3DSIO_TEXCOORD || opcode == D3DSIO_TEX))
            return (version >= 0x0104) ? 2 : 1;
        return pixelShader ? o.psOperands : o.vsOperands;
    }
    return -1;
}

const char *OpcodeName(DWORD opcode)
{
    for (const OpInfo &o : kOps)
        if (o.op == opcode) return o.name;
    return "?";
}

std::string SwizzleString(const Param &p)
{
    static const char c[] = "xyzw";
    std::string s = ".";
    for (int i = 0; i < 4; ++i) s += c[p.swizzle[i]];
    return s;
}

std::string MaskString(uint8_t mask)
{
    static const char c[] = "xyzw";
    std::string s;
    for (int i = 0; i < 4; ++i)
        if (mask & (1 << i)) s += c[i];
    return s;
}

//------------------------------------------------------------------------------
// Assembler
//------------------------------------------------------------------------------
namespace {

struct Assembler
{
    std::vector<DWORD> &out;
    std::string &errors;
    int line = 0;
    bool isPS = false;
    DWORD version = 0;
    bool failed = false;

    Assembler(std::vector<DWORD> &o, std::string &e) : out(o), errors(e) {}

    void Error(const std::string &msg)
    {
        failed = true;
        char buf[64];
        snprintf(buf, sizeof buf, "(%d): error: ", line);
        errors += buf + msg + "\n";
    }

    static std::string Trim(const std::string &s)
    {
        size_t a = 0, b = s.size();
        while (a < b && isspace((unsigned char)s[a])) ++a;
        while (b > a && isspace((unsigned char)s[b - 1])) --b;
        return s.substr(a, b - a);
    }

    static std::string Lower(std::string s)
    {
        for (char &c : s) c = (char)tolower((unsigned char)c);
        return s;
    }

    /// Splits "a, b, c" on commas that are not inside brackets.
    static std::vector<std::string> SplitOperands(const std::string &s)
    {
        std::vector<std::string> r;
        int depth = 0;
        std::string cur;
        for (char c : s)
        {
            if (c == '[') ++depth;
            if (c == ']') --depth;
            if (c == ',' && depth == 0) { r.push_back(Trim(cur)); cur.clear(); }
            else cur += c;
        }
        if (!Trim(cur).empty() || !r.empty()) r.push_back(Trim(cur));
        return r;
    }

    bool ParseRegister(const std::string &name, int &type, int &reg)
    {
        std::string n = Lower(name);
        auto num = [&](size_t from, int &v) -> bool {
            if (from >= n.size()) return false;
            for (size_t i = from; i < n.size(); ++i) if (!isdigit((unsigned char)n[i])) return false;
            v = atoi(n.c_str() + from);
            return true;
        };
        if (n == "opos") { type = RT_RASTOUT; reg = 0; return !isPS; }
        if (n == "ofog") { type = RT_RASTOUT; reg = 1; return !isPS; }
        if (n == "opts") { type = RT_RASTOUT; reg = 2; return !isPS; }
        if (n.compare(0, 2, "od") == 0) { type = RT_ATTROUT; return num(2, reg); }
        if (n.compare(0, 2, "ot") == 0) { type = RT_TEXCRDOUT; return num(2, reg); }
        if (n == "a0") { type = RT_ADDR_TEX; reg = 0; return !isPS; }
        switch (n.empty() ? 0 : n[0])
        {
        case 'r': type = RT_TEMP; return num(1, reg);
        case 'v': type = RT_INPUT; return num(1, reg);
        case 'c': type = RT_CONST; return num(1, reg);
        case 't': type = RT_ADDR_TEX; return isPS && num(1, reg);
        }
        return false;
    }

    bool ParseSwizzleChars(const std::string &s, uint8_t sw[4], int &count)
    {
        count = 0;
        uint8_t tmp[4] = {0, 0, 0, 0};
        for (char c : s)
        {
            int v;
            switch (tolower(c))
            {
            case 'x': case 'r': v = 0; break;
            case 'y': case 'g': v = 1; break;
            case 'z': case 'b': v = 2; break;
            case 'w': case 'a': v = 3; break;
            default: return false;
            }
            if (count >= 4) return false;
            tmp[count++] = (uint8_t)v;
        }
        if (!count) return false;
        for (int i = 0; i < 4; ++i) sw[i] = tmp[i < count ? i : count - 1];
        return true;
    }

    bool ParseDest(const std::string &text, DWORD &token, std::string &modsOut)
    {
        (void)modsOut;
        std::string s = Trim(text);
        std::string swz;
        size_t dot = s.find('.');
        if (dot != std::string::npos) { swz = s.substr(dot + 1); s = s.substr(0, dot); }
        int type, reg;
        if (!ParseRegister(s, type, reg)) { Error("invalid destination register '" + text + "'"); return false; }
        uint32_t mask = 0xF;
        if (!swz.empty())
        {
            mask = 0;
            for (char c : swz)
            {
                switch (tolower(c))
                {
                case 'x': case 'r': mask |= 1; break;
                case 'y': case 'g': mask |= 2; break;
                case 'z': case 'b': mask |= 4; break;
                case 'w': case 'a': mask |= 8; break;
                default: Error("invalid write mask '" + swz + "'"); return false;
                }
            }
        }
        token = 0x80000000u | ((DWORD)type << D3DSP_REGTYPE_SHIFT) | (DWORD)reg | (mask << 16);
        return true;
    }

    bool ParseSource(const std::string &text, DWORD &token)
    {
        std::string s = Trim(text);
        int mod = 0;
        bool neg = false, comp = false;
        if (!s.empty() && s[0] == '-') { neg = true; s = Trim(s.substr(1)); }
        if (s.compare(0, 2, "1-") == 0) { comp = true; s = Trim(s.substr(2)); }

        bool relative = false;
        int type = 0, reg = 0;
        std::string rest;
        if (s.size() > 2 && tolower(s[0]) == 'c' && s[1] == '[')
        {
            // Constant with an index: c[3] or c[a0.x + 3].
            size_t close = s.find(']');
            if (close == std::string::npos) { Error("missing ']'"); return false; }
            std::string inner = Lower(Trim(s.substr(2, close - 2)));
            rest = s.substr(close + 1);
            type = RT_CONST;
            if (inner.compare(0, 2, "a0") == 0)
            {
                relative = true;
                size_t p = inner.find('+');
                reg = (p != std::string::npos) ? atoi(inner.c_str() + p + 1) : 0;
            }
            else reg = atoi(inner.c_str());
        }
        else
        {
            size_t n = 0;
            while (n < s.size() && isalnum((unsigned char)s[n])) ++n;
            std::string regName = s.substr(0, n);
            rest = s.substr(n);
            if (!ParseRegister(regName, type, reg)) { Error("invalid source register '" + regName + "'"); return false; }
        }

        std::string swz;
        while (!rest.empty())
        {
            if (rest[0] == '_')
            {
                size_t n = rest.find_first_of("._", 1);
                std::string m = Lower(rest.substr(1, n == std::string::npos ? std::string::npos : n - 1));
                rest = (n == std::string::npos) ? "" : rest.substr(n);
                if (m == "bias") mod = D3DSPSM_BIAS >> 24;
                else if (m == "bx2") mod = D3DSPSM_SIGN >> 24;
                else if (m == "x2") mod = D3DSPSM_X2 >> 24;
                else if (m == "dz") mod = D3DSPSM_DZ >> 24;
                else if (m == "dw") mod = D3DSPSM_DW >> 24;
                else { Error("unknown source modifier '_" + m + "'"); return false; }
            }
            else if (rest[0] == '.')
            {
                size_t n = rest.find('_', 1);
                swz = rest.substr(1, n == std::string::npos ? std::string::npos : n - 1);
                rest = (n == std::string::npos) ? "" : rest.substr(n);
            }
            else { Error("unexpected text '" + rest + "' in source operand"); return false; }
        }
        uint8_t sw[4] = {0, 1, 2, 3};
        if (!swz.empty())
        {
            int count;
            if (!ParseSwizzleChars(swz, sw, count)) { Error("invalid swizzle '" + swz + "'"); return false; }
        }
        if (comp) mod = D3DSPSM_COMP >> 24;
        if (neg)
        {
            switch (mod)
            {
            case 0: mod = D3DSPSM_NEG >> 24; break;
            case D3DSPSM_BIAS >> 24: mod = D3DSPSM_BIASNEG >> 24; break;
            case D3DSPSM_SIGN >> 24: mod = D3DSPSM_SIGNNEG >> 24; break;
            case D3DSPSM_X2 >> 24: mod = D3DSPSM_X2NEG >> 24; break;
            default: Error("negation cannot be combined with this modifier"); return false;
            }
        }
        token = 0x80000000u | ((DWORD)type << D3DSP_REGTYPE_SHIFT) | (DWORD)reg | (relative ? (DWORD)D3DVS_ADDRMODE_RELATIVE : 0u) |
                ((DWORD)sw[0] << 16) | ((DWORD)sw[1] << 18) | ((DWORD)sw[2] << 20) | ((DWORD)sw[3] << 22) | ((DWORD)mod << 24);
        return true;
    }

    void Instruction(std::string text)
    {
        bool coissue = false;
        text = Trim(text);
        if (!text.empty() && text[0] == '+') { coissue = true; text = Trim(text.substr(1)); }
        size_t sp = text.find_first_of(" \t");
        std::string mnemonic = Lower(sp == std::string::npos ? text : text.substr(0, sp));
        std::string operands = sp == std::string::npos ? "" : Trim(text.substr(sp));

        // Instruction modifiers (_x2, _sat, ...).
        int shift = 0;
        bool sat = false;
        for (;;)
        {
            size_t us = mnemonic.rfind('_');
            if (us == std::string::npos) break;
            std::string m = mnemonic.substr(us + 1);
            if (m == "sat") sat = true;
            else if (m == "x2") shift = 1;
            else if (m == "x4") shift = 2;
            else if (m == "x8") shift = 3;
            else if (m == "d2") shift = -1;
            else if (m == "d4") shift = -2;
            else if (m == "d8") shift = -3;
            else break;
            mnemonic = mnemonic.substr(0, us);
        }
        const OpInfo *info = nullptr;
        for (const OpInfo &o : kOps)
            if (mnemonic == o.name) { info = &o; break; }
        if (!info) { Error("unknown instruction '" + mnemonic + "'"); return; }

        if (info->op == D3DSIO_PHASE) { out.push_back(D3DSIO_PHASE); return; }
        if (info->op == D3DSIO_NOP) { out.push_back(D3DSIO_NOP); return; }

        std::vector<std::string> ops = SplitOperands(operands);
        if (info->op == D3DSIO_DEF)
        {
            if (ops.size() != 5) { Error("def requires a register and four values"); return; }
            DWORD dst;
            std::string mods;
            if (!ParseDest(ops[0], dst, mods)) return;
            out.push_back(D3DSIO_DEF);
            out.push_back(dst | D3DSP_WRITEMASK_ALL);
            for (int i = 1; i < 5; ++i)
            {
                float f = (float)atof(ops[i].c_str());
                DWORD d;
                memcpy(&d, &f, 4);
                out.push_back(d);
            }
            return;
        }

        const int expected = OperandCount(info->op, isPS, version);
        // texld/texcrd (ps.1.4) vs tex/texcoord (<1.4) are the same opcodes.
        if (expected >= 0 && (int)ops.size() != expected &&
            !(isPS && (info->op == D3DSIO_TEX || info->op == D3DSIO_TEXCOORD)))
        {
            Error(std::string("wrong number of operands for ") + mnemonic);
            return;
        }
        if (expected < 0 && !(isPS && (info->op == D3DSIO_TEX || info->op == D3DSIO_TEXCOORD)))
        {
            Error(std::string("instruction '") + mnemonic + "' is not available in this shader version");
            return;
        }

        // vs.1.1 has no subtract instruction: "sub d, a, b" is the assembler macro "add d, a, -b".
        const bool vsSub = !isPS && info->op == D3DSIO_SUB;
        out.push_back((vsSub ? (DWORD)D3DSIO_ADD : info->op) | (coissue ? 0x40000000u : 0u));
        for (size_t i = 0; i < ops.size(); ++i)
        {
            DWORD t;
            if (i == 0)
            {
                std::string mods;
                if (!ParseDest(ops[0], t, mods)) return;
                t |= (sat ? D3DSPDM_SATURATE : 0u);
                t |= ((DWORD)(shift & 0xF) << D3DSP_DSTSHIFT_SHIFT);
            }
            else if (!ParseSource(ops[i], t))
                return;
            else if (vsSub && i == 2)
            {
                if ((t & D3DSP_SRCMOD_MASK) != 0) { Error("sub cannot negate a source that has a modifier"); return; }
                t |= D3DSPSM_NEG;
            }
            out.push_back(t);
        }
    }
};

} // namespace

bool AssembleShader(const char *source, size_t length, std::vector<DWORD> &out, std::string &errors)
{
    out.clear();
    Assembler as(out, errors);
    std::string text(source, length);
    size_t pos = 0;
    bool haveVersion = false;
    while (pos <= text.size())
    {
        size_t nl = text.find('\n', pos);
        std::string ln = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
        ++as.line;
        // Strip comments.
        size_t c = ln.find(';');
        if (c != std::string::npos) ln.erase(c);
        c = ln.find("//");
        if (c != std::string::npos) ln.erase(c);
        ln = Assembler::Trim(ln);
        if (ln.empty()) continue;

        // The game's inline shaders use "\\\n" continuations inside string literals, producing
        // several instructions on one line separated by spaces only when a newline was
        // swallowed; also accept ';'-less multi-statement lines split on '\r'.
        if (!haveVersion)
        {
            std::string l = Assembler::Lower(ln);
            unsigned major, minor;
            if (sscanf(l.c_str(), "vs.%u.%u", &major, &minor) == 2 || sscanf(l.c_str(), "vs_%u_%u", &major, &minor) == 2)
                { as.isPS = false; out.push_back(D3DVS_VERSION(major, minor)); }
            else if (sscanf(l.c_str(), "ps.%u.%u", &major, &minor) == 2 || sscanf(l.c_str(), "ps_%u_%u", &major, &minor) == 2)
                { as.isPS = true; out.push_back(D3DPS_VERSION(major, minor)); }
            else { as.Error("expected a version directive (vs.1.1 / ps.1.1)"); return false; }
            as.version = (major << 8) | minor;
            haveVersion = true;
            continue;
        }
        as.Instruction(ln);
        if (as.failed) return false;
    }
    if (!haveVersion) { errors = "no version directive\n"; return false; }
    out.push_back(D3DPS_END());
    return !as.failed;
}

} // namespace webd3d8
