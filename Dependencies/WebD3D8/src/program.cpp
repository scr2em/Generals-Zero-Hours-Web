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
    "u_wvp", "u_wv", "u_world", "u_nm", "u_tm[0]", "u_pix", "u_vp", "u_clip[0]", "u_point", "u_pointatt",
    "u_matE", "u_matA", "u_matD", "u_matS", "u_matP",
    "u_lpos[0]", "u_ldir[0]", "u_ldiff[0]", "u_lspec[0]", "u_lamb[0]", "u_latt[0]", "u_lspot[0]",
    "u_ambient", "u_fog", "u_fogColor", "u_tfactor", "u_alphaRef", "u_lod[0]",
    "c[0]", "pc[0]",
};

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
        glDeleteProgram(prog->id);
        prog->id = 0;
        for (GLint &l : prog->loc) l = -1;
        return prog;
    }
    for (int i = 0; i < U_COUNT; ++i)
        prog->loc[i] = glGetUniformLocation(prog->id, kUniformNames[i]);

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
