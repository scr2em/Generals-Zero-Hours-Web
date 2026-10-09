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
** WebAssembly port: GL program compilation and uniform lookup.
*/
#include "program.h"

namespace webd3d8 {

static const char *const kUniformNames[U_COUNT] = {
    "u_xf[0]", "u_tm[0]", "u_view[0]", "u_clip[0]", "u_pt[0]", "u_mat[0]", "u_lt[0]", "u_ambient", "u_fogp[0]",
    "u_tfactor", "u_alphaRef", "u_lod[0]", "u_bump[0]", "u_bumpl[0]", "c[0]", "pc[0]", "u_border[0]",
};

/// Which group each uniform belongs to.
static const uint8_t kUniformGroup[U_COUNT] = {
    G_XFORM, G_TEXMAT, G_VIEWPORT, G_CLIP, G_POINT, G_MATERIAL, G_LIGHTS, G_AMBIENT, G_FOG,
    G_TFACTOR, G_ALPHAREF, G_LOD, G_BUMP, G_BUMP, G_VSC, G_PSC, G_BORDER,
};

/// True when `name` occurs in `src` as a whole identifier.
static bool UsesIdentifier(const std::string &src, const char *name)
{
    const size_t n = strlen(name);
    // Only the code after the declarations counts (the macros that define the names mention them all).
    size_t from = src.find("// end of uniforms\n");
    from = from == std::string::npos ? 0 : from;
    for (size_t at = src.find(name, from); at != std::string::npos; at = src.find(name, at + 1))
    {
        const char after = at + n < src.size() ? src[at + n] : ' ';
        const char before = at ? src[at - 1] : ' ';
        if (!(isalnum((unsigned char)after) || after == '_') && !(isalnum((unsigned char)before) || before == '_')) return true;
    }
    return false;
}

Program::~Program()
{
    if (id) glDeleteProgram(id);
}

static GLuint CompileShader(GLenum type, const std::string &src)
{
    GLuint sh = glCreateShader(type);
    const char *p = src.c_str();
    glShaderSource(sh, 1, &p, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[2048];
        GLsizei n = 0;
        glGetShaderInfoLog(sh, sizeof log - 1, &n, log);
        log[n] = 0;
        Log("%s shader compile error: %s", type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        WD3D_HIT(Failed, "GLSL %s shader failed to compile", type == GL_VERTEX_SHADER ? "vertex" : "fragment");
        if (GetConfig().debug) Log("source:\n%s", src.c_str());
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

Program *CreateProgram(Device *, const ProgramKey &key, const std::string &vsSrc, const std::string &fsSrc)
{
    Program *prog = new Program();
    prog->key = key;
    ++g_d3d.programsCreated;
    GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fsSrc);
    if (!vs || !fs)
    {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        for (GLint &l : prog->loc) l = -1;
        return prog; // invalid, remembered so we do not retry every draw
    }
    prog->id = glCreateProgram();
    glAttachShader(prog->id, vs);
    glAttachShader(prog->id, fs);
    glLinkProgram(prog->id);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(prog->id, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[2048];
        GLsizei n = 0;
        glGetProgramInfoLog(prog->id, sizeof log - 1, &n, log);
        log[n] = 0;
        Log("program link error: %s", log);
        WD3D_HIT(Failed, "GLSL program failed to link");
        glDeleteProgram(prog->id);
        prog->id = 0;
        for (GLint &l : prog->loc) l = -1;
        return prog;
    }
    for (int i = 0; i < U_COUNT; ++i)
    {
        prog->loc[i] = glGetUniformLocation(prog->id, kUniformNames[i]);
        if (prog->loc[i] >= 0) prog->usedGroups |= 1u << kUniformGroup[i];
    }

    // How much of the packed transform block the program reads.
    {
        const bool wv = UsesIdentifier(vsSrc, "u_wv"), nm = UsesIdentifier(vsSrc, "u_nm"), world = UsesIdentifier(vsSrc, "u_world");
        prog->xfCount = world ? 15 : (wv || nm ? 11 : 4);
    }

    // Samplers are fixed: stage N always samples texture unit N.
    glUseProgram(prog->id);
    for (int i = 0; i < MAX_STAGES; ++i)
    {
        char name[8];
        snprintf(name, sizeof name, "s%d", i);
        GLint l = glGetUniformLocation(prog->id, name);
        if (l >= 0) glUniform1i(l, i);
    }
    prog->valid = true;
    return prog;
}

} // namespace webd3d8
