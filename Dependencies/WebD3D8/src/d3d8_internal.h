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
** WebAssembly port: declarations shared between d3d8.cpp and device.cpp.
*/
#pragma once

#include "device.h"

namespace webd3d8 {

/// Fills D3DCAPS8; `gl` may be null before a context exists.
void FillDeviceCaps(D3DCAPS8 *caps, const GLCaps *gl);
void SetRendererString(const std::string &s);
IDirect3D8 *CreateDirect3D8();

} // namespace webd3d8
