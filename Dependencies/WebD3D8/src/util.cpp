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
** WebAssembly port: configuration, logging and matrix helpers.
*/
#include "common.h"

#include <cstdarg>
#include <set>

namespace webd3d8 {

Config &GetConfig()
{
    static Config config;
    return config;
}

void Log(const char *fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    fprintf(stderr, "[WebD3D8] %s\n", buf);
}

void LogUnsupported(const char *what)
{
    static std::set<std::string> reported;
    if (reported.insert(what).second)
        Log("unsupported: %s", what);
}

Mat4 Mat4::Identity()
{
    Mat4 r;
    memset(r.m, 0, sizeof r.m);
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

Mat4 Mat4::operator*(const Mat4 &o) const
{
    Mat4 r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
        {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k)
                s += m[i * 4 + k] * o.m[k * 4 + j];
            r.m[i * 4 + j] = s;
        }
    return r;
}

Mat4 Mat4::Transposed() const
{
    Mat4 r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            r.m[j * 4 + i] = m[i * 4 + j];
    return r;
}

bool Mat4::Inverse(Mat4 &out) const
{
    // Gauss-Jordan elimination with partial pivoting.
    double a[4][8];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
        {
            a[i][j] = m[i * 4 + j];
            a[i][4 + j] = (i == j) ? 1.0 : 0.0;
        }
    for (int c = 0; c < 4; ++c)
    {
        int piv = c;
        for (int r = c + 1; r < 4; ++r)
            if (std::fabs(a[r][c]) > std::fabs(a[piv][c])) piv = r;
        if (std::fabs(a[piv][c]) < 1e-30) return false;
        if (piv != c)
            for (int j = 0; j < 8; ++j) { double tmp_ = a[c][j]; a[c][j] = a[piv][j]; a[piv][j] = tmp_; }
        double d = 1.0 / a[c][c];
        for (int j = 0; j < 8; ++j) a[c][j] *= d;
        for (int r = 0; r < 4; ++r)
        {
            if (r == c) continue;
            double f = a[r][c];
            if (f == 0.0) continue;
            for (int j = 0; j < 8; ++j) a[r][j] -= f * a[c][j];
        }
    }
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            out.m[i * 4 + j] = (float)a[i][4 + j];
    return true;
}

/// Normal matrix of a row-major (row-vector) model-view matrix M, for upload
/// as a column-major GL mat3 that is applied as `normalMatrix * normal`.
/// Row vectors transform as n' = n * inverse(M3)^T, i.e. n'(column) =
/// inverse(M3) * n(column), so the GL matrix is inverse(M3) itself.
void NormalMatrix3(const Mat4 &mv, float out[9])
{
    double a = mv.m[0], b = mv.m[1], c = mv.m[2];
    double d = mv.m[4], e = mv.m[5], f = mv.m[6];
    double g = mv.m[8], h = mv.m[9], i = mv.m[10];
    double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (std::fabs(det) < 1e-30)
    {
        for (int k = 0; k < 9; ++k) out[k] = (k % 4 == 0) ? 1.0f : 0.0f;
        return;
    }
    double id = 1.0 / det;
    double inv[3][3] = {
        {(e * i - f * h) * id, (c * h - b * i) * id, (b * f - c * e) * id},
        {(f * g - d * i) * id, (a * i - c * g) * id, (c * d - a * f) * id},
        {(d * h - e * g) * id, (b * g - a * h) * id, (a * e - b * d) * id},
    };
    for (int r = 0; r < 3; ++r)
        for (int col = 0; col < 3; ++col)
            out[col * 3 + r] = (float)inv[r][col];
}

} // namespace webd3d8

extern "C" {

void WebD3D8_SetCanvas(const char *selector)
{
    if (selector && *selector) webd3d8::GetConfig().canvas = selector;
}

void WebD3D8_SetDesktopSize(unsigned width, unsigned height)
{
    webd3d8::GetConfig().desktopWidth = width;
    webd3d8::GetConfig().desktopHeight = height;
}

void WebD3D8_SetShaderModel(unsigned vs, unsigned ps)
{
    webd3d8::GetConfig().vsVersion = vs;
    webd3d8::GetConfig().psVersion = ps;
}

void WebD3D8_SetPlatformHooks(const WebD3D8_PlatformHooks *hooks)
{
    webd3d8::GetConfig().hooks = hooks ? *hooks : WebD3D8_PlatformHooks{};
}
void WebD3D8_SetPresentMode(int mode) { webd3d8::GetConfig().presentMode = mode; }
void WebD3D8_SetDisableS3TC(int disable) { webd3d8::GetConfig().disableS3TC = disable != 0; }
void WebD3D8_SetContextProxy(int mode) { webd3d8::GetConfig().contextProxy = mode; }
void WebD3D8_SetDebug(int flags)
{
    webd3d8::GetConfig().debug = (flags & 1) != 0;
    webd3d8::GetConfig().debugForce = flags >> 1;
}
void WebD3D8_SetReleaseTextureShadows(int enable) { webd3d8::GetConfig().releaseTextureShadows = enable != 0; }

} // extern "C"
