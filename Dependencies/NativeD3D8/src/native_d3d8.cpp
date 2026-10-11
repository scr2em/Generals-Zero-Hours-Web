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
** Native headless build: the D3DX functions the renderer links against, without a device.
**
** Headless mode never creates a Direct3D device (Direct3DCreate8 is looked up in d3d8.dll at run
** time, which never loads). The math functions are real (d3dx_math.cpp next to this file),
** because the game logic uses some of them (D3DXVec4Transform in BezierSegment, for one); the rest
** report that nothing can be created.
*/
#include <windows.h>
#include <objbase.h>

#include <d3d8.h>
#include <d3dx8.h>

#include <string.h>

namespace
{

UINT FVFVertexSize(DWORD fvf)
{
	UINT size = 0;
	switch (fvf & D3DFVF_POSITION_MASK)
	{
	case D3DFVF_XYZ: size += 12; break;
	case D3DFVF_XYZRHW: size += 16; break;
	case D3DFVF_XYZB1: size += 16; break;
	case D3DFVF_XYZB2: size += 20; break;
	case D3DFVF_XYZB3: size += 24; break;
	case D3DFVF_XYZB4: size += 28; break;
	case D3DFVF_XYZB5: size += 32; break;
	}
	if (fvf & D3DFVF_NORMAL) size += 12;
	if (fvf & D3DFVF_PSIZE) size += 4;
	if (fvf & D3DFVF_DIFFUSE) size += 4;
	if (fvf & D3DFVF_SPECULAR) size += 4;
	const UINT texCount = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
	for (UINT i = 0; i < texCount; ++i)
	{
		switch ((fvf >> (16 + i * 2)) & 3)
		{
		case D3DFVF_TEXTUREFORMAT1: size += 4; break;
		case D3DFVF_TEXTUREFORMAT2: size += 8; break;
		case D3DFVF_TEXTUREFORMAT3: size += 12; break;
		case D3DFVF_TEXTUREFORMAT4: size += 16; break;
		}
	}
	return size;
}

const char s_noDevice[] = "no Direct3D device (native headless build)";

} // namespace

extern "C" {

UINT WINAPI D3DXGetFVFVertexSize(DWORD fvf)
{
	return FVFVertexSize(fvf);
}

HRESULT WINAPI D3DXGetErrorStringA(HRESULT, LPSTR buf, UINT len)
{
	if (buf && len)
	{
		strncpy(buf, s_noDevice, len - 1);
		buf[len - 1] = '\0';
	}
	return S_OK;
}

HRESULT WINAPI D3DXAssembleShader(LPCVOID, UINT, DWORD, LPD3DXBUFFER *ppConstants, LPD3DXBUFFER *ppCompiled, LPD3DXBUFFER *ppErrors)
{
	if (ppConstants) *ppConstants = nullptr;
	if (ppCompiled) *ppCompiled = nullptr;
	if (ppErrors) *ppErrors = nullptr;
	return E_FAIL;
}

HRESULT WINAPI D3DXCreateTexture(LPDIRECT3DDEVICE8, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, LPDIRECT3DTEXTURE8 *ppTexture)
{
	if (ppTexture) *ppTexture = nullptr;
	return D3DERR_NOTAVAILABLE;
}

HRESULT WINAPI D3DXCreateCubeTexture(LPDIRECT3DDEVICE8, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, LPDIRECT3DCUBETEXTURE8 *ppCubeTexture)
{
	if (ppCubeTexture) *ppCubeTexture = nullptr;
	return D3DERR_NOTAVAILABLE;
}

HRESULT WINAPI D3DXCreateVolumeTexture(LPDIRECT3DDEVICE8, UINT, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, LPDIRECT3DVOLUMETEXTURE8 *ppVolumeTexture)
{
	if (ppVolumeTexture) *ppVolumeTexture = nullptr;
	return D3DERR_NOTAVAILABLE;
}

HRESULT WINAPI D3DXCreateTextureFromFileExA(LPDIRECT3DDEVICE8, LPCSTR, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, DWORD, DWORD,
	D3DCOLOR, D3DXIMAGE_INFO *, PALETTEENTRY *, LPDIRECT3DTEXTURE8 *ppTexture)
{
	if (ppTexture) *ppTexture = nullptr;
	return D3DERR_NOTAVAILABLE;
}

HRESULT WINAPI D3DXLoadSurfaceFromSurface(LPDIRECT3DSURFACE8, CONST PALETTEENTRY *, CONST RECT *, LPDIRECT3DSURFACE8, CONST PALETTEENTRY *,
	CONST RECT *, DWORD, D3DCOLOR)
{
	return D3DERR_NOTAVAILABLE;
}

HRESULT WINAPI D3DXFilterTexture(LPDIRECT3DBASETEXTURE8, CONST PALETTEENTRY *, UINT, DWORD)
{
	return D3DERR_NOTAVAILABLE;
}

} // extern "C"
