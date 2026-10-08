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
** WebAssembly port: configuration entry points of the Direct3D 8 on WebGL2
** implementation (Dependencies/WebD3D8).
**
** The Direct3D 8 interfaces themselves are used through the regular
** <d3d8.h> / <d3dx8.h> headers and Direct3DCreate8(); the functions below only
** let the platform layer steer the parts that have no Direct3D equivalent. All
** of them are optional and, unless noted, must be called before
** IDirect3D8::CreateDevice().
*/
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/// Selects the canvas the WebGL2 context is created on, as a CSS selector
/// ("#canvas" by default). When the device renders from a pthread, the canvas
/// has to be an OffscreenCanvas transferred to that thread
/// (-sOFFSCREENCANVAS_SUPPORT -sOFFSCREENCANVASES_TO_PTHREAD).
void WebD3D8_SetCanvas(const char *selector);

/// Size in pixels of the "desktop" that the display mode enumeration reports
/// (IDirect3D8::GetAdapterDisplayMode). The default is the browser screen size
/// when it can be queried, 1280x720 otherwise.
void WebD3D8_SetDesktopSize(unsigned width, unsigned height);

/// Vertex/pixel shader versions reported by IDirect3DDevice8::GetDeviceCaps(),
/// as (major << 8) | minor, e.g. 0x0101 for 1.1 and 0x0104 for ps 1.4.
/// 0 reports no programmable shader support, which keeps the game on its
/// fixed-function code paths. This is the default.
void WebD3D8_SetShaderModel(unsigned vertexShaderVersion, unsigned pixelShaderVersion);

/// How finished frames are handed to the browser by IDirect3DDevice8::Present().
enum WebD3D8_PresentMode {
    /// The browser presents the canvas when the calling task returns to the
    /// event loop (requestAnimationFrame / emscripten_set_main_loop style).
    WEBD3D8_PRESENT_IMPLICIT = 0,
    /// The context is created with explicitSwapControl and Present() calls
    /// emscripten_webgl_commit_frame(). Needed for OffscreenCanvas rendering
    /// from a thread that never yields to its event loop.
    WEBD3D8_PRESENT_EXPLICIT = 1,
};
void WebD3D8_SetPresentMode(int mode);

/// Enables extra GL error checking and a log of unsupported D3D features.
void WebD3D8_SetDebug(int enable);

/// Releases the memory of the system-memory shadow copies of managed textures
/// after they were uploaded, when enabled (default: disabled). Saves memory at
/// the price of a GPU read-back when such a level is locked again.
void WebD3D8_SetReleaseTextureShadows(int enable);

/// Info about the GL implementation that backs the device (valid after
/// CreateDevice()). Strings are owned by the library.
const char *WebD3D8_GetRendererString(void);

#ifdef __cplusplus
}
#endif
