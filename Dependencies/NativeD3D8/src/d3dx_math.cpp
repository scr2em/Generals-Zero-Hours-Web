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
** Native headless build: the non-inline D3DX8 math functions (vectors, matrices,
** quaternions, planes, colors). Left-handed, row-vector conventions exactly
** as in d3dx8math.h. (Formerly part of the web build's Direct3D 8 library,
** Dependencies/WebD3D8, which became dxWebGL2.)
*/
#include <windows.h>
#include <objbase.h>

#include <d3d8.h>
#include <d3dx8.h>

#include <cmath>

namespace {

inline float Len3(const D3DXVECTOR3 &v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
inline D3DXVECTOR3 Cross3(const D3DXVECTOR3 &a, const D3DXVECTOR3 &b)
{
    return D3DXVECTOR3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
inline float Dot3(const D3DXVECTOR3 &a, const D3DXVECTOR3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline D3DXVECTOR3 Norm3(const D3DXVECTOR3 &v)
{
    float l = Len3(v);
    return l > 0.0f ? D3DXVECTOR3(v.x / l, v.y / l, v.z / l) : D3DXVECTOR3(0, 0, 0);
}

D3DXMATRIX Mul(const D3DXMATRIX &a, const D3DXMATRIX &b)
{
    D3DXMATRIX r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j] + a.m[i][3] * b.m[3][j];
    return r;
}

D3DXMATRIX Identity()
{
    D3DXMATRIX m;
    memset(&m, 0, sizeof m);
    m._11 = m._22 = m._33 = m._44 = 1.0f;
    return m;
}

} // namespace

extern "C" {

//------------------------------------------------------------------------------
// 2D vectors
//------------------------------------------------------------------------------
D3DXVECTOR2 *WINAPI D3DXVec2Normalize(D3DXVECTOR2 *o, const D3DXVECTOR2 *v)
{
    float l = std::sqrt(v->x * v->x + v->y * v->y);
    if (l > 0.0f) { o->x = v->x / l; o->y = v->y / l; } else { o->x = o->y = 0.0f; }
    return o;
}

D3DXVECTOR2 *WINAPI D3DXVec2Hermite(D3DXVECTOR2 *o, const D3DXVECTOR2 *v1, const D3DXVECTOR2 *t1, const D3DXVECTOR2 *v2, const D3DXVECTOR2 *t2, float s)
{
    float s2 = s * s, s3 = s2 * s;
    float h1 = 2 * s3 - 3 * s2 + 1, h2 = -2 * s3 + 3 * s2, h3 = s3 - 2 * s2 + s, h4 = s3 - s2;
    o->x = h1 * v1->x + h2 * v2->x + h3 * t1->x + h4 * t2->x;
    o->y = h1 * v1->y + h2 * v2->y + h3 * t1->y + h4 * t2->y;
    return o;
}

D3DXVECTOR2 *WINAPI D3DXVec2CatmullRom(D3DXVECTOR2 *o, const D3DXVECTOR2 *v0, const D3DXVECTOR2 *v1, const D3DXVECTOR2 *v2, const D3DXVECTOR2 *v3, float s)
{
    float s2 = s * s, s3 = s2 * s;
    o->x = 0.5f * (2 * v1->x + (v2->x - v0->x) * s + (2 * v0->x - 5 * v1->x + 4 * v2->x - v3->x) * s2 + (3 * v1->x - v0->x - 3 * v2->x + v3->x) * s3);
    o->y = 0.5f * (2 * v1->y + (v2->y - v0->y) * s + (2 * v0->y - 5 * v1->y + 4 * v2->y - v3->y) * s2 + (3 * v1->y - v0->y - 3 * v2->y + v3->y) * s3);
    return o;
}

D3DXVECTOR2 *WINAPI D3DXVec2BaryCentric(D3DXVECTOR2 *o, const D3DXVECTOR2 *v1, const D3DXVECTOR2 *v2, const D3DXVECTOR2 *v3, float f, float g)
{
    o->x = v1->x + f * (v2->x - v1->x) + g * (v3->x - v1->x);
    o->y = v1->y + f * (v2->y - v1->y) + g * (v3->y - v1->y);
    return o;
}

D3DXVECTOR4 *WINAPI D3DXVec2Transform(D3DXVECTOR4 *o, const D3DXVECTOR2 *v, const D3DXMATRIX *m)
{
    D3DXVECTOR4 r(v->x * m->_11 + v->y * m->_21 + m->_41, v->x * m->_12 + v->y * m->_22 + m->_42,
                  v->x * m->_13 + v->y * m->_23 + m->_43, v->x * m->_14 + v->y * m->_24 + m->_44);
    *o = r;
    return o;
}

D3DXVECTOR2 *WINAPI D3DXVec2TransformCoord(D3DXVECTOR2 *o, const D3DXVECTOR2 *v, const D3DXMATRIX *m)
{
    D3DXVECTOR4 t;
    D3DXVec2Transform(&t, v, m);
    float w = t.w != 0.0f ? 1.0f / t.w : 1.0f;
    o->x = t.x * w; o->y = t.y * w;
    return o;
}

D3DXVECTOR2 *WINAPI D3DXVec2TransformNormal(D3DXVECTOR2 *o, const D3DXVECTOR2 *v, const D3DXMATRIX *m)
{
    D3DXVECTOR2 r(v->x * m->_11 + v->y * m->_21, v->x * m->_12 + v->y * m->_22);
    *o = r;
    return o;
}

//------------------------------------------------------------------------------
// 3D vectors
//------------------------------------------------------------------------------
D3DXVECTOR3 *WINAPI D3DXVec3Normalize(D3DXVECTOR3 *o, const D3DXVECTOR3 *v) { *o = Norm3(*v); return o; }

D3DXVECTOR3 *WINAPI D3DXVec3Hermite(D3DXVECTOR3 *o, const D3DXVECTOR3 *v1, const D3DXVECTOR3 *t1, const D3DXVECTOR3 *v2, const D3DXVECTOR3 *t2, float s)
{
    float s2 = s * s, s3 = s2 * s;
    float h1 = 2 * s3 - 3 * s2 + 1, h2 = -2 * s3 + 3 * s2, h3 = s3 - 2 * s2 + s, h4 = s3 - s2;
    o->x = h1 * v1->x + h2 * v2->x + h3 * t1->x + h4 * t2->x;
    o->y = h1 * v1->y + h2 * v2->y + h3 * t1->y + h4 * t2->y;
    o->z = h1 * v1->z + h2 * v2->z + h3 * t1->z + h4 * t2->z;
    return o;
}

D3DXVECTOR3 *WINAPI D3DXVec3CatmullRom(D3DXVECTOR3 *o, const D3DXVECTOR3 *v0, const D3DXVECTOR3 *v1, const D3DXVECTOR3 *v2, const D3DXVECTOR3 *v3, float s)
{
    float s2 = s * s, s3 = s2 * s;
    auto cr = [&](float a, float b, float c, float d) {
        return 0.5f * (2 * b + (c - a) * s + (2 * a - 5 * b + 4 * c - d) * s2 + (3 * b - a - 3 * c + d) * s3);
    };
    o->x = cr(v0->x, v1->x, v2->x, v3->x);
    o->y = cr(v0->y, v1->y, v2->y, v3->y);
    o->z = cr(v0->z, v1->z, v2->z, v3->z);
    return o;
}

D3DXVECTOR3 *WINAPI D3DXVec3BaryCentric(D3DXVECTOR3 *o, const D3DXVECTOR3 *v1, const D3DXVECTOR3 *v2, const D3DXVECTOR3 *v3, float f, float g)
{
    o->x = v1->x + f * (v2->x - v1->x) + g * (v3->x - v1->x);
    o->y = v1->y + f * (v2->y - v1->y) + g * (v3->y - v1->y);
    o->z = v1->z + f * (v2->z - v1->z) + g * (v3->z - v1->z);
    return o;
}

D3DXVECTOR4 *WINAPI D3DXVec3Transform(D3DXVECTOR4 *o, const D3DXVECTOR3 *v, const D3DXMATRIX *m)
{
    D3DXVECTOR4 r(v->x * m->_11 + v->y * m->_21 + v->z * m->_31 + m->_41,
                  v->x * m->_12 + v->y * m->_22 + v->z * m->_32 + m->_42,
                  v->x * m->_13 + v->y * m->_23 + v->z * m->_33 + m->_43,
                  v->x * m->_14 + v->y * m->_24 + v->z * m->_34 + m->_44);
    *o = r;
    return o;
}

D3DXVECTOR3 *WINAPI D3DXVec3TransformCoord(D3DXVECTOR3 *o, const D3DXVECTOR3 *v, const D3DXMATRIX *m)
{
    D3DXVECTOR4 t;
    D3DXVec3Transform(&t, v, m);
    float w = t.w != 0.0f ? 1.0f / t.w : 1.0f;
    o->x = t.x * w; o->y = t.y * w; o->z = t.z * w;
    return o;
}

D3DXVECTOR3 *WINAPI D3DXVec3TransformNormal(D3DXVECTOR3 *o, const D3DXVECTOR3 *v, const D3DXMATRIX *m)
{
    D3DXVECTOR3 r(v->x * m->_11 + v->y * m->_21 + v->z * m->_31,
                  v->x * m->_12 + v->y * m->_22 + v->z * m->_32,
                  v->x * m->_13 + v->y * m->_23 + v->z * m->_33);
    *o = r;
    return o;
}

D3DXVECTOR3 *WINAPI D3DXVec3Project(D3DXVECTOR3 *o, const D3DXVECTOR3 *v, const D3DVIEWPORT8 *vp, const D3DXMATRIX *proj, const D3DXMATRIX *view, const D3DXMATRIX *world)
{
    D3DXMATRIX m = Identity();
    if (world) m = Mul(m, *world);
    if (view) m = Mul(m, *view);
    if (proj) m = Mul(m, *proj);
    D3DXVECTOR3 p;
    D3DXVec3TransformCoord(&p, v, &m);
    if (vp)
    {
        p.x = vp->X + vp->Width * (1.0f + p.x) * 0.5f;
        p.y = vp->Y + vp->Height * (1.0f - p.y) * 0.5f;
        p.z = vp->MinZ + p.z * (vp->MaxZ - vp->MinZ);
    }
    *o = p;
    return o;
}

D3DXVECTOR3 *WINAPI D3DXVec3Unproject(D3DXVECTOR3 *o, const D3DXVECTOR3 *v, const D3DVIEWPORT8 *vp, const D3DXMATRIX *proj, const D3DXMATRIX *view, const D3DXMATRIX *world)
{
    D3DXMATRIX m = Identity();
    if (world) m = Mul(m, *world);
    if (view) m = Mul(m, *view);
    if (proj) m = Mul(m, *proj);
    D3DXMATRIX inv;
    if (!D3DXMatrixInverse(&inv, nullptr, &m)) return nullptr;
    D3DXVECTOR3 p = *v;
    if (vp)
    {
        p.x = 2.0f * (p.x - vp->X) / vp->Width - 1.0f;
        p.y = 1.0f - 2.0f * (p.y - vp->Y) / vp->Height;
        p.z = (p.z - vp->MinZ) / (vp->MaxZ - vp->MinZ);
    }
    D3DXVec3TransformCoord(o, &p, &inv);
    return o;
}

//------------------------------------------------------------------------------
// 4D vectors
//------------------------------------------------------------------------------
D3DXVECTOR4 *WINAPI D3DXVec4Cross(D3DXVECTOR4 *o, const D3DXVECTOR4 *a, const D3DXVECTOR4 *b, const D3DXVECTOR4 *c)
{
    D3DXVECTOR4 r;
    r.x = a->y * (b->z * c->w - c->z * b->w) - a->z * (b->y * c->w - c->y * b->w) + a->w * (b->y * c->z - b->z * c->y);
    r.y = -(a->x * (b->z * c->w - c->z * b->w) - a->z * (b->x * c->w - c->x * b->w) + a->w * (b->x * c->z - c->x * b->z));
    r.z = a->x * (b->y * c->w - c->y * b->w) - a->y * (b->x * c->w - c->x * b->w) + a->w * (b->x * c->y - c->x * b->y);
    r.w = -(a->x * (b->y * c->z - c->y * b->z) - a->y * (b->x * c->z - c->x * b->z) + a->z * (b->x * c->y - c->x * b->y));
    *o = r;
    return o;
}

D3DXVECTOR4 *WINAPI D3DXVec4Normalize(D3DXVECTOR4 *o, const D3DXVECTOR4 *v)
{
    float l = std::sqrt(v->x * v->x + v->y * v->y + v->z * v->z + v->w * v->w);
    if (l > 0.0f) { o->x = v->x / l; o->y = v->y / l; o->z = v->z / l; o->w = v->w / l; }
    else o->x = o->y = o->z = o->w = 0.0f;
    return o;
}

D3DXVECTOR4 *WINAPI D3DXVec4Hermite(D3DXVECTOR4 *o, const D3DXVECTOR4 *v1, const D3DXVECTOR4 *t1, const D3DXVECTOR4 *v2, const D3DXVECTOR4 *t2, float s)
{
    float s2 = s * s, s3 = s2 * s;
    float h1 = 2 * s3 - 3 * s2 + 1, h2 = -2 * s3 + 3 * s2, h3 = s3 - 2 * s2 + s, h4 = s3 - s2;
    o->x = h1 * v1->x + h2 * v2->x + h3 * t1->x + h4 * t2->x;
    o->y = h1 * v1->y + h2 * v2->y + h3 * t1->y + h4 * t2->y;
    o->z = h1 * v1->z + h2 * v2->z + h3 * t1->z + h4 * t2->z;
    o->w = h1 * v1->w + h2 * v2->w + h3 * t1->w + h4 * t2->w;
    return o;
}

D3DXVECTOR4 *WINAPI D3DXVec4CatmullRom(D3DXVECTOR4 *o, const D3DXVECTOR4 *v0, const D3DXVECTOR4 *v1, const D3DXVECTOR4 *v2, const D3DXVECTOR4 *v3, float s)
{
    float s2 = s * s, s3 = s2 * s;
    auto cr = [&](float a, float b, float c, float d) {
        return 0.5f * (2 * b + (c - a) * s + (2 * a - 5 * b + 4 * c - d) * s2 + (3 * b - a - 3 * c + d) * s3);
    };
    o->x = cr(v0->x, v1->x, v2->x, v3->x);
    o->y = cr(v0->y, v1->y, v2->y, v3->y);
    o->z = cr(v0->z, v1->z, v2->z, v3->z);
    o->w = cr(v0->w, v1->w, v2->w, v3->w);
    return o;
}

D3DXVECTOR4 *WINAPI D3DXVec4BaryCentric(D3DXVECTOR4 *o, const D3DXVECTOR4 *v1, const D3DXVECTOR4 *v2, const D3DXVECTOR4 *v3, float f, float g)
{
    o->x = v1->x + f * (v2->x - v1->x) + g * (v3->x - v1->x);
    o->y = v1->y + f * (v2->y - v1->y) + g * (v3->y - v1->y);
    o->z = v1->z + f * (v2->z - v1->z) + g * (v3->z - v1->z);
    o->w = v1->w + f * (v2->w - v1->w) + g * (v3->w - v1->w);
    return o;
}

D3DXVECTOR4 *WINAPI D3DXVec4Transform(D3DXVECTOR4 *o, const D3DXVECTOR4 *v, const D3DXMATRIX *m)
{
    D3DXVECTOR4 r(v->x * m->_11 + v->y * m->_21 + v->z * m->_31 + v->w * m->_41,
                  v->x * m->_12 + v->y * m->_22 + v->z * m->_32 + v->w * m->_42,
                  v->x * m->_13 + v->y * m->_23 + v->z * m->_33 + v->w * m->_43,
                  v->x * m->_14 + v->y * m->_24 + v->z * m->_34 + v->w * m->_44);
    *o = r;
    return o;
}

//------------------------------------------------------------------------------
// Matrices
//------------------------------------------------------------------------------
float WINAPI D3DXMatrixfDeterminant(const D3DXMATRIX *pm)
{
    const D3DXMATRIX &m = *pm;
    auto det3 = [](float a, float b, float c, float d, float e, float f, float g, float h, float i) {
        return a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    };
    return m._11 * det3(m._22, m._23, m._24, m._32, m._33, m._34, m._42, m._43, m._44) -
           m._12 * det3(m._21, m._23, m._24, m._31, m._33, m._34, m._41, m._43, m._44) +
           m._13 * det3(m._21, m._22, m._24, m._31, m._32, m._34, m._41, m._42, m._44) -
           m._14 * det3(m._21, m._22, m._23, m._31, m._32, m._33, m._41, m._42, m._43);
}

D3DXMATRIX *WINAPI D3DXMatrixTranspose(D3DXMATRIX *o, const D3DXMATRIX *pm)
{
    D3DXMATRIX t;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) t.m[i][j] = pm->m[j][i];
    *o = t;
    return o;
}

D3DXMATRIX *WINAPI D3DXMatrixMultiply(D3DXMATRIX *o, const D3DXMATRIX *a, const D3DXMATRIX *b)
{
    *o = Mul(*a, *b);
    return o;
}

D3DXMATRIX *WINAPI D3DXMatrixMultiplyTranspose(D3DXMATRIX *o, const D3DXMATRIX *a, const D3DXMATRIX *b)
{
    D3DXMATRIX t = Mul(*a, *b);
    return D3DXMatrixTranspose(o, &t);
}

D3DXMATRIX *WINAPI D3DXMatrixInverse(D3DXMATRIX *o, float *pDet, const D3DXMATRIX *pm)
{
    double a[4][8];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
        {
            a[i][j] = pm->m[i][j];
            a[i][4 + j] = (i == j) ? 1.0 : 0.0;
        }
    double det = 1.0;
    for (int c = 0; c < 4; ++c)
    {
        int piv = c;
        for (int r = c + 1; r < 4; ++r)
            if (std::fabs(a[r][c]) > std::fabs(a[piv][c])) piv = r;
        if (std::fabs(a[piv][c]) < 1e-300)
        {
            if (pDet) *pDet = 0.0f;
            return nullptr;
        }
        if (piv != c)
        {
            for (int j = 0; j < 8; ++j) { double tmp_ = a[c][j]; a[c][j] = a[piv][j]; a[piv][j] = tmp_; }
            det = -det;
        }
        det *= a[c][c];
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
    if (pDet) *pDet = (float)det;
    D3DXMATRIX r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) r.m[i][j] = (float)a[i][4 + j];
    *o = r;
    return o;
}

D3DXMATRIX *WINAPI D3DXMatrixScaling(D3DXMATRIX *o, float sx, float sy, float sz)
{
    *o = Identity();
    o->_11 = sx; o->_22 = sy; o->_33 = sz;
    return o;
}

D3DXMATRIX *WINAPI D3DXMatrixTranslation(D3DXMATRIX *o, float x, float y, float z)
{
    *o = Identity();
    o->_41 = x; o->_42 = y; o->_43 = z;
    return o;
}

D3DXMATRIX *WINAPI D3DXMatrixRotationX(D3DXMATRIX *o, float a)
{
    *o = Identity();
    float s = std::sin(a), c = std::cos(a);
    o->_22 = c; o->_23 = s; o->_32 = -s; o->_33 = c;
    return o;
}

D3DXMATRIX *WINAPI D3DXMatrixRotationY(D3DXMATRIX *o, float a)
{
    *o = Identity();
    float s = std::sin(a), c = std::cos(a);
    o->_11 = c; o->_13 = -s; o->_31 = s; o->_33 = c;
    return o;
}

D3DXMATRIX *WINAPI D3DXMatrixRotationZ(D3DXMATRIX *o, float a)
{
    *o = Identity();
    float s = std::sin(a), c = std::cos(a);
    o->_11 = c; o->_12 = s; o->_21 = -s; o->_22 = c;
    return o;
}

D3DXMATRIX *WINAPI D3DXMatrixRotationAxis(D3DXMATRIX *o, const D3DXVECTOR3 *axis, float angle)
{
    D3DXVECTOR3 v = Norm3(*axis);
    float s = std::sin(angle), c = std::cos(angle), t = 1.0f - c;
    *o = Identity();
    o->_11 = t * v.x * v.x + c;       o->_12 = t * v.x * v.y + s * v.z; o->_13 = t * v.x * v.z - s * v.y;
    o->_21 = t * v.x * v.y - s * v.z; o->_22 = t * v.y * v.y + c;       o->_23 = t * v.y * v.z + s * v.x;
    o->_31 = t * v.x * v.z + s * v.y; o->_32 = t * v.y * v.z - s * v.x; o->_33 = t * v.z * v.z + c;
    return o;
}

D3DXMATRIX *WINAPI D3DXMatrixRotationQuaternion(D3DXMATRIX *o, const D3DXQUATERNION *q)
{
    *o = Identity();
    o->_11 = 1.0f - 2.0f * (q->y * q->y + q->z * q->z);
    o->_12 = 2.0f * (q->x * q->y + q->z * q->w);
    o->_13 = 2.0f * (q->x * q->z - q->y * q->w);
    o->_21 = 2.0f * (q->x * q->y - q->z * q->w);
    o->_22 = 1.0f - 2.0f * (q->x * q->x + q->z * q->z);
    o->_23 = 2.0f * (q->y * q->z + q->x * q->w);
    o->_31 = 2.0f * (q->x * q->z + q->y * q->w);
    o->_32 = 2.0f * (q->y * q->z - q->x * q->w);
    o->_33 = 1.0f - 2.0f * (q->x * q->x + q->y * q->y);
    return o;
}

D3DXMATRIX *WINAPI D3DXMatrixRotationYawPitchRoll(D3DXMATRIX *o, float yaw, float pitch, float roll)
{
    D3DXMATRIX z, x, y;
    D3DXMatrixRotationZ(&z, roll);
    D3DXMatrixRotationX(&x, pitch);
    D3DXMatrixRotationY(&y, yaw);
    *o = Mul(Mul(z, x), y);
    return o;
}

D3DXMATRIX *WINAPI D3DXMatrixTransformation(D3DXMATRIX *o, const D3DXVECTOR3 *psc, const D3DXQUATERNION *psr, const D3DXVECTOR3 *ps,
                                            const D3DXVECTOR3 *prc, const D3DXQUATERNION *pr, const D3DXVECTOR3 *pt)
{
    D3DXMATRIX scC = Identity(), scCinv = Identity(), sr = Identity(), srInv = Identity(), s = Identity();
    D3DXMATRIX rc = Identity(), rcInv = Identity(), r = Identity(), t = Identity();
    if (psc) { D3DXMatrixTranslation(&scC, psc->x, psc->y, psc->z); D3DXMatrixTranslation(&scCinv, -psc->x, -psc->y, -psc->z); }
    if (psr) { D3DXMatrixRotationQuaternion(&sr, psr); D3DXMatrixTranspose(&srInv, &sr); }
    if (ps) D3DXMatrixScaling(&s, ps->x, ps->y, ps->z);
    if (prc) { D3DXMatrixTranslation(&rc, prc->x, prc->y, prc->z); D3DXMatrixTranslation(&rcInv, -prc->x, -prc->y, -prc->z); }
    if (pr) D3DXMatrixRotationQuaternion(&r, pr);
    if (pt) D3DXMatrixTranslation(&t, pt->x, pt->y, pt->z);
    D3DXMATRIX m = scCinv;
    m = Mul(m, srInv); m = Mul(m, s); m = Mul(m, sr); m = Mul(m, scC);
    m = Mul(m, rcInv); m = Mul(m, r); m = Mul(m, rc); m = Mul(m, t);
    *o = m;
    return o;
}

D3DXMATRIX *WINAPI D3DXMatrixAffineTransformation(D3DXMATRIX *o, float scaling, const D3DXVECTOR3 *prc, const D3DXQUATERNION *pr, const D3DXVECTOR3 *pt)
{
    D3DXMATRIX s, rc = Identity(), rcInv = Identity(), r = Identity(), t = Identity();
    D3DXMatrixScaling(&s, scaling, scaling, scaling);
    if (prc) { D3DXMatrixTranslation(&rc, prc->x, prc->y, prc->z); D3DXMatrixTranslation(&rcInv, -prc->x, -prc->y, -prc->z); }
    if (pr) D3DXMatrixRotationQuaternion(&r, pr);
    if (pt) D3DXMatrixTranslation(&t, pt->x, pt->y, pt->z);
    *o = Mul(Mul(Mul(Mul(s, rcInv), r), rc), t);
    return o;
}

static D3DXMATRIX *LookAt(D3DXMATRIX *o, const D3DXVECTOR3 *eye, const D3DXVECTOR3 *at, const D3DXVECTOR3 *up, bool rh)
{
    D3DXVECTOR3 z = rh ? Norm3(D3DXVECTOR3(eye->x - at->x, eye->y - at->y, eye->z - at->z))
                       : Norm3(D3DXVECTOR3(at->x - eye->x, at->y - eye->y, at->z - eye->z));
    D3DXVECTOR3 x = Norm3(Cross3(*up, z));
    D3DXVECTOR3 y = Cross3(z, x);
    *o = Identity();
    o->_11 = x.x; o->_12 = y.x; o->_13 = z.x;
    o->_21 = x.y; o->_22 = y.y; o->_23 = z.y;
    o->_31 = x.z; o->_32 = y.z; o->_33 = z.z;
    o->_41 = -Dot3(x, *eye); o->_42 = -Dot3(y, *eye); o->_43 = -Dot3(z, *eye);
    return o;
}

D3DXMATRIX *WINAPI D3DXMatrixLookAtRH(D3DXMATRIX *o, const D3DXVECTOR3 *e, const D3DXVECTOR3 *a, const D3DXVECTOR3 *u) { return LookAt(o, e, a, u, true); }
D3DXMATRIX *WINAPI D3DXMatrixLookAtLH(D3DXMATRIX *o, const D3DXVECTOR3 *e, const D3DXVECTOR3 *a, const D3DXVECTOR3 *u) { return LookAt(o, e, a, u, false); }

D3DXMATRIX *WINAPI D3DXMatrixPerspectiveRH(D3DXMATRIX *o, float w, float h, float zn, float zf)
{
    memset(o, 0, sizeof *o);
    o->_11 = 2 * zn / w; o->_22 = 2 * zn / h; o->_33 = zf / (zn - zf); o->_34 = -1.0f; o->_43 = zn * zf / (zn - zf);
    return o;
}

D3DXMATRIX *WINAPI D3DXMatrixPerspectiveLH(D3DXMATRIX *o, float w, float h, float zn, float zf)
{
    memset(o, 0, sizeof *o);
    o->_11 = 2 * zn / w; o->_22 = 2 * zn / h; o->_33 = zf / (zf - zn); o->_34 = 1.0f; o->_43 = zn * zf / (zn - zf);
    return o;
}

D3DXMATRIX *WINAPI D3DXMatrixPerspectiveFovRH(D3DXMATRIX *o, float fovy, float aspect, float zn, float zf)
{
    float ys = 1.0f / std::tan(fovy * 0.5f), xs = ys / aspect;
    memset(o, 0, sizeof *o);
    o->_11 = xs; o->_22 = ys; o->_33 = zf / (zn - zf); o->_34 = -1.0f; o->_43 = zn * zf / (zn - zf);
    return o;
}

D3DXMATRIX *WINAPI D3DXMatrixPerspectiveFovLH(D3DXMATRIX *o, float fovy, float aspect, float zn, float zf)
{
    float ys = 1.0f / std::tan(fovy * 0.5f), xs = ys / aspect;
    memset(o, 0, sizeof *o);
    o->_11 = xs; o->_22 = ys; o->_33 = zf / (zf - zn); o->_34 = 1.0f; o->_43 = -zn * zf / (zf - zn);
    return o;
}

D3DXMATRIX *WINAPI D3DXMatrixPerspectiveOffCenterRH(D3DXMATRIX *o, float l, float r, float b, float t, float zn, float zf)
{
    memset(o, 0, sizeof *o);
    o->_11 = 2 * zn / (r - l); o->_22 = 2 * zn / (t - b);
    o->_31 = (l + r) / (r - l); o->_32 = (t + b) / (t - b);
    o->_33 = zf / (zn - zf); o->_34 = -1.0f; o->_43 = zn * zf / (zn - zf);
    return o;
}

D3DXMATRIX *WINAPI D3DXMatrixPerspectiveOffCenterLH(D3DXMATRIX *o, float l, float r, float b, float t, float zn, float zf)
{
    memset(o, 0, sizeof *o);
    o->_11 = 2 * zn / (r - l); o->_22 = 2 * zn / (t - b);
    o->_31 = (l + r) / (l - r); o->_32 = (t + b) / (b - t);
    o->_33 = zf / (zf - zn); o->_34 = 1.0f; o->_43 = zn * zf / (zn - zf);
    return o;
}

D3DXMATRIX *WINAPI D3DXMatrixOrthoRH(D3DXMATRIX *o, float w, float h, float zn, float zf)
{
    *o = Identity();
    o->_11 = 2 / w; o->_22 = 2 / h; o->_33 = 1 / (zn - zf); o->_43 = zn / (zn - zf);
    return o;
}

D3DXMATRIX *WINAPI D3DXMatrixOrthoLH(D3DXMATRIX *o, float w, float h, float zn, float zf)
{
    *o = Identity();
    o->_11 = 2 / w; o->_22 = 2 / h; o->_33 = 1 / (zf - zn); o->_43 = zn / (zn - zf);
    return o;
}

D3DXMATRIX *WINAPI D3DXMatrixOrthoOffCenterRH(D3DXMATRIX *o, float l, float r, float b, float t, float zn, float zf)
{
    *o = Identity();
    o->_11 = 2 / (r - l); o->_22 = 2 / (t - b); o->_33 = 1 / (zn - zf);
    o->_41 = (l + r) / (l - r); o->_42 = (t + b) / (b - t); o->_43 = zn / (zn - zf);
    return o;
}

D3DXMATRIX *WINAPI D3DXMatrixOrthoOffCenterLH(D3DXMATRIX *o, float l, float r, float b, float t, float zn, float zf)
{
    *o = Identity();
    o->_11 = 2 / (r - l); o->_22 = 2 / (t - b); o->_33 = 1 / (zf - zn);
    o->_41 = (l + r) / (l - r); o->_42 = (t + b) / (b - t); o->_43 = zn / (zn - zf);
    return o;
}

D3DXMATRIX *WINAPI D3DXMatrixShadow(D3DXMATRIX *o, const D3DXVECTOR4 *light, const D3DXPLANE *plane)
{
    D3DXPLANE p;
    D3DXPlaneNormalize(&p, plane);
    float dot = p.a * light->x + p.b * light->y + p.c * light->z + p.d * light->w;
    o->_11 = p.a * -light->x + dot; o->_12 = p.a * -light->y;       o->_13 = p.a * -light->z;       o->_14 = p.a * -light->w;
    o->_21 = p.b * -light->x;       o->_22 = p.b * -light->y + dot; o->_23 = p.b * -light->z;       o->_24 = p.b * -light->w;
    o->_31 = p.c * -light->x;       o->_32 = p.c * -light->y;       o->_33 = p.c * -light->z + dot; o->_34 = p.c * -light->w;
    o->_41 = p.d * -light->x;       o->_42 = p.d * -light->y;       o->_43 = p.d * -light->z;       o->_44 = p.d * -light->w + dot;
    return o;
}

D3DXMATRIX *WINAPI D3DXMatrixReflect(D3DXMATRIX *o, const D3DXPLANE *plane)
{
    D3DXPLANE p;
    D3DXPlaneNormalize(&p, plane);
    *o = Identity();
    o->_11 = 1 - 2 * p.a * p.a; o->_12 = -2 * p.a * p.b;    o->_13 = -2 * p.a * p.c;
    o->_21 = -2 * p.a * p.b;    o->_22 = 1 - 2 * p.b * p.b; o->_23 = -2 * p.b * p.c;
    o->_31 = -2 * p.c * p.a;    o->_32 = -2 * p.c * p.b;    o->_33 = 1 - 2 * p.c * p.c;
    o->_41 = -2 * p.d * p.a;    o->_42 = -2 * p.d * p.b;    o->_43 = -2 * p.d * p.c;
    return o;
}

//------------------------------------------------------------------------------
// Quaternions
//------------------------------------------------------------------------------
void WINAPI D3DXQuaternionToAxisAngle(const D3DXQUATERNION *q, D3DXVECTOR3 *axis, float *angle)
{
    if (axis) { axis->x = q->x; axis->y = q->y; axis->z = q->z; }
    if (angle) *angle = 2.0f * std::acos(q->w);
}

D3DXQUATERNION *WINAPI D3DXQuaternionRotationMatrix(D3DXQUATERNION *o, const D3DXMATRIX *m)
{
    float trace = m->_11 + m->_22 + m->_33;
    D3DXQUATERNION q;
    if (trace > 0.0f)
    {
        float s = std::sqrt(trace + 1.0f);
        q.w = s * 0.5f;
        s = 0.5f / s;
        q.x = (m->_23 - m->_32) * s; q.y = (m->_31 - m->_13) * s; q.z = (m->_12 - m->_21) * s;
    }
    else
    {
        int i = 0;
        if (m->_22 > m->_11) i = 1;
        if (m->_33 > m->m[i][i]) i = 2;
        static const int nxt[3] = {1, 2, 0};
        int j = nxt[i], k = nxt[j];
        float s = std::sqrt(m->m[i][i] - m->m[j][j] - m->m[k][k] + 1.0f);
        float v[4];
        v[i] = s * 0.5f;
        s = 0.5f / s;
        v[3] = (m->m[j][k] - m->m[k][j]) * s;
        v[j] = (m->m[i][j] + m->m[j][i]) * s;
        v[k] = (m->m[i][k] + m->m[k][i]) * s;
        q.x = v[0]; q.y = v[1]; q.z = v[2]; q.w = v[3];
    }
    *o = q;
    return o;
}

D3DXQUATERNION *WINAPI D3DXQuaternionRotationAxis(D3DXQUATERNION *o, const D3DXVECTOR3 *axis, float angle)
{
    D3DXVECTOR3 v = Norm3(*axis);
    float s = std::sin(angle * 0.5f);
    o->x = v.x * s; o->y = v.y * s; o->z = v.z * s; o->w = std::cos(angle * 0.5f);
    return o;
}

D3DXQUATERNION *WINAPI D3DXQuaternionRotationYawPitchRoll(D3DXQUATERNION *o, float yaw, float pitch, float roll)
{
    D3DXMATRIX m;
    D3DXMatrixRotationYawPitchRoll(&m, yaw, pitch, roll);
    return D3DXQuaternionRotationMatrix(o, &m);
}

D3DXQUATERNION *WINAPI D3DXQuaternionMultiply(D3DXQUATERNION *o, const D3DXQUATERNION *q1, const D3DXQUATERNION *q2)
{
    D3DXQUATERNION r;
    r.x = q2->w * q1->x + q2->x * q1->w + q2->y * q1->z - q2->z * q1->y;
    r.y = q2->w * q1->y - q2->x * q1->z + q2->y * q1->w + q2->z * q1->x;
    r.z = q2->w * q1->z + q2->x * q1->y - q2->y * q1->x + q2->z * q1->w;
    r.w = q2->w * q1->w - q2->x * q1->x - q2->y * q1->y - q2->z * q1->z;
    *o = r;
    return o;
}

D3DXQUATERNION *WINAPI D3DXQuaternionNormalize(D3DXQUATERNION *o, const D3DXQUATERNION *q)
{
    float l = std::sqrt(q->x * q->x + q->y * q->y + q->z * q->z + q->w * q->w);
    if (l > 0.0f) { o->x = q->x / l; o->y = q->y / l; o->z = q->z / l; o->w = q->w / l; }
    else o->x = o->y = o->z = o->w = 0.0f;
    return o;
}

D3DXQUATERNION *WINAPI D3DXQuaternionInverse(D3DXQUATERNION *o, const D3DXQUATERNION *q)
{
    float l2 = q->x * q->x + q->y * q->y + q->z * q->z + q->w * q->w;
    if (l2 > 0.0f) { o->x = -q->x / l2; o->y = -q->y / l2; o->z = -q->z / l2; o->w = q->w / l2; }
    else o->x = o->y = o->z = o->w = 0.0f;
    return o;
}

D3DXQUATERNION *WINAPI D3DXQuaternionLn(D3DXQUATERNION *o, const D3DXQUATERNION *q)
{
    float t = (q->w >= 1.0f || q->w <= -1.0f) ? 0.0f : std::acos(q->w);
    float s = std::sin(t);
    float k = (s != 0.0f) ? t / s : 1.0f;
    o->x = q->x * k; o->y = q->y * k; o->z = q->z * k; o->w = 0.0f;
    return o;
}

D3DXQUATERNION *WINAPI D3DXQuaternionExp(D3DXQUATERNION *o, const D3DXQUATERNION *q)
{
    float t = std::sqrt(q->x * q->x + q->y * q->y + q->z * q->z);
    float s = std::sin(t);
    float k = (t != 0.0f) ? s / t : 1.0f;
    o->x = q->x * k; o->y = q->y * k; o->z = q->z * k; o->w = std::cos(t);
    return o;
}

D3DXQUATERNION *WINAPI D3DXQuaternionSlerp(D3DXQUATERNION *o, const D3DXQUATERNION *q1, const D3DXQUATERNION *q2, float t)
{
    float dot = q1->x * q2->x + q1->y * q2->y + q1->z * q2->z + q1->w * q2->w;
    float sign = dot < 0.0f ? -1.0f : 1.0f;
    dot = std::fabs(dot);
    float s1, s2;
    if (dot < 0.9999f)
    {
        float th = std::acos(dot), sn = std::sin(th);
        s1 = std::sin((1.0f - t) * th) / sn;
        s2 = std::sin(t * th) / sn;
    }
    else { s1 = 1.0f - t; s2 = t; }
    s2 *= sign;
    D3DXQUATERNION r(s1 * q1->x + s2 * q2->x, s1 * q1->y + s2 * q2->y, s1 * q1->z + s2 * q2->z, s1 * q1->w + s2 * q2->w);
    *o = r;
    return o;
}

D3DXQUATERNION *WINAPI D3DXQuaternionSquad(D3DXQUATERNION *o, const D3DXQUATERNION *q1, const D3DXQUATERNION *a, const D3DXQUATERNION *b, const D3DXQUATERNION *c, float t)
{
    D3DXQUATERNION t1, t2;
    D3DXQuaternionSlerp(&t1, q1, c, t);
    D3DXQuaternionSlerp(&t2, a, b, t);
    return D3DXQuaternionSlerp(o, &t1, &t2, 2.0f * t * (1.0f - t));
}

void WINAPI D3DXQuaternionSquadSetup(D3DXQUATERNION *aOut, D3DXQUATERNION *bOut, D3DXQUATERNION *cOut,
                                     const D3DXQUATERNION *q0, const D3DXQUATERNION *q1, const D3DXQUATERNION *q2, const D3DXQUATERNION *q3)
{
    auto dot = [](const D3DXQUATERNION &a, const D3DXQUATERNION &b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; };
    D3DXQUATERNION Q0 = *q0, Q2 = *q2, Q3 = *q3;
    if (dot(Q0, *q1) < 0.0f) { Q0.x = -Q0.x; Q0.y = -Q0.y; Q0.z = -Q0.z; Q0.w = -Q0.w; }
    if (dot(*q1, Q2) < 0.0f) { Q2.x = -Q2.x; Q2.y = -Q2.y; Q2.z = -Q2.z; Q2.w = -Q2.w; }
    if (dot(Q2, Q3) < 0.0f) { Q3.x = -Q3.x; Q3.y = -Q3.y; Q3.z = -Q3.z; Q3.w = -Q3.w; }
    auto control = [&](const D3DXQUATERNION &p, const D3DXQUATERNION &q, const D3DXQUATERNION &n, D3DXQUATERNION &out) {
        D3DXQUATERNION qi, ln1, ln2, sum, e;
        D3DXQuaternionInverse(&qi, &q);
        D3DXQUATERNION a, b;
        D3DXQuaternionMultiply(&a, &qi, &p); // q^-1 * p
        D3DXQuaternionMultiply(&b, &qi, &n);
        D3DXQuaternionLn(&ln1, &a);
        D3DXQuaternionLn(&ln2, &b);
        sum.x = -(ln1.x + ln2.x) * 0.25f; sum.y = -(ln1.y + ln2.y) * 0.25f; sum.z = -(ln1.z + ln2.z) * 0.25f; sum.w = 0;
        D3DXQuaternionExp(&e, &sum);
        D3DXQuaternionMultiply(&out, &e, &q);
    };
    control(Q0, *q1, Q2, *aOut);
    control(*q1, Q2, Q3, *bOut);
    *cOut = Q2;
}

D3DXQUATERNION *WINAPI D3DXQuaternionBaryCentric(D3DXQUATERNION *o, const D3DXQUATERNION *q1, const D3DXQUATERNION *q2, const D3DXQUATERNION *q3, float f, float g)
{
    D3DXQUATERNION a, b;
    D3DXQuaternionSlerp(&a, q1, q2, f + g);
    D3DXQuaternionSlerp(&b, q1, q3, f + g);
    return D3DXQuaternionSlerp(o, &a, &b, (f + g) != 0.0f ? g / (f + g) : 0.0f);
}

//------------------------------------------------------------------------------
// Planes
//------------------------------------------------------------------------------
D3DXPLANE *WINAPI D3DXPlaneNormalize(D3DXPLANE *o, const D3DXPLANE *p)
{
    float l = std::sqrt(p->a * p->a + p->b * p->b + p->c * p->c);
    if (l > 0.0f) { o->a = p->a / l; o->b = p->b / l; o->c = p->c / l; o->d = p->d / l; }
    else o->a = o->b = o->c = o->d = 0.0f;
    return o;
}

D3DXVECTOR3 *WINAPI D3DXPlaneIntersectLine(D3DXVECTOR3 *o, const D3DXPLANE *p, const D3DXVECTOR3 *v1, const D3DXVECTOR3 *v2)
{
    D3DXVECTOR3 d(v2->x - v1->x, v2->y - v1->y, v2->z - v1->z);
    float denom = p->a * d.x + p->b * d.y + p->c * d.z;
    if (denom == 0.0f) return nullptr;
    float t = -(p->a * v1->x + p->b * v1->y + p->c * v1->z + p->d) / denom;
    o->x = v1->x + t * d.x; o->y = v1->y + t * d.y; o->z = v1->z + t * d.z;
    return o;
}

D3DXPLANE *WINAPI D3DXPlaneFromPointNormal(D3DXPLANE *o, const D3DXVECTOR3 *pt, const D3DXVECTOR3 *n)
{
    o->a = n->x; o->b = n->y; o->c = n->z; o->d = -(pt->x * n->x + pt->y * n->y + pt->z * n->z);
    return o;
}

D3DXPLANE *WINAPI D3DXPlaneFromPoints(D3DXPLANE *o, const D3DXVECTOR3 *v1, const D3DXVECTOR3 *v2, const D3DXVECTOR3 *v3)
{
    D3DXVECTOR3 e1(v2->x - v1->x, v2->y - v1->y, v2->z - v1->z), e2(v3->x - v1->x, v3->y - v1->y, v3->z - v1->z);
    D3DXVECTOR3 n = Norm3(Cross3(e1, e2));
    return D3DXPlaneFromPointNormal(o, v1, &n);
}

D3DXPLANE *WINAPI D3DXPlaneTransform(D3DXPLANE *o, const D3DXPLANE *p, const D3DXMATRIX *m)
{
    D3DXPLANE r;
    r.a = p->a * m->_11 + p->b * m->_21 + p->c * m->_31 + p->d * m->_41;
    r.b = p->a * m->_12 + p->b * m->_22 + p->c * m->_32 + p->d * m->_42;
    r.c = p->a * m->_13 + p->b * m->_23 + p->c * m->_33 + p->d * m->_43;
    r.d = p->a * m->_14 + p->b * m->_24 + p->c * m->_34 + p->d * m->_44;
    *o = r;
    return o;
}

//------------------------------------------------------------------------------
// Colors / misc
//------------------------------------------------------------------------------
D3DXCOLOR *WINAPI D3DXColorAdjustSaturation(D3DXCOLOR *o, const D3DXCOLOR *c, float s)
{
    float grey = c->r * 0.2125f + c->g * 0.7154f + c->b * 0.0721f;
    o->r = grey + s * (c->r - grey);
    o->g = grey + s * (c->g - grey);
    o->b = grey + s * (c->b - grey);
    o->a = c->a;
    return o;
}

D3DXCOLOR *WINAPI D3DXColorAdjustContrast(D3DXCOLOR *o, const D3DXCOLOR *c, float k)
{
    o->r = 0.5f + k * (c->r - 0.5f);
    o->g = 0.5f + k * (c->g - 0.5f);
    o->b = 0.5f + k * (c->b - 0.5f);
    o->a = c->a;
    return o;
}

float WINAPI D3DXFresnelTerm(float cosTheta, float refractionIndex)
{
    float g = std::sqrt(refractionIndex * refractionIndex + cosTheta * cosTheta - 1.0f);
    float a = g + cosTheta, d = g - cosTheta;
    float r1 = (cosTheta * a - 1.0f) / (cosTheta * d + 1.0f);
    float r2 = (a * a) / (d * d);
    return 0.5f * r2 * (1.0f + r1 * r1);
}

} // extern "C"
