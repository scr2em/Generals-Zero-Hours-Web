# Direct3D 8 on WebGL2: what the game uses and what is implemented

Source of the "game use" column: a grep of every Direct3D 8 identifier in `Core/Libraries/Source/WWVegas/WW3D2`,
`Core/GameEngineDevice/Source/W3DDevice` and `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice` (the Zero Hour build;
`Generals/` is not built for the web). Counts are occurrences in the code, not frequency per frame.

Status: **full** = implemented as Direct3D defines it; **approx** = implemented with a known difference (counted by
`-webd3d8report`); **ignored** = accepted and not acted on (counted when it is non-default); **unused** = the game never
calls it (proved by grep, see the notes); **n/a** = cannot exist in WebGL.

Tests: `T0` = `tests/web_d3d8_test.cpp` scenes (hash checked by `run_regression.mjs`), `F:<case>` =
`tests/web_d3d8_features.cpp` case (checked pixels, `run_features.mjs`), `G` = the game itself with the starter pack
(`GeneralsMD/Code/Main/web/test/starter_flow.mjs`).

## Running the checks

```
ninja -C build/em-d3d8 web_d3d8_test web_d3d8_features
cd Dependencies/WebD3D8/tests
node run_regression.mjs ../../../build/em-d3d8/Dependencies/WebD3D8    # 5 scenes, frame hashes
node run_features.mjs   ../../../build/em-d3d8/Dependencies/WebD3D8    # 7 modes, ~60 checks each
```

## Diagnostics: `-webd3d8report[=seconds]`

Add `?arg=-webd3d8report` (or `=5` for every 5 seconds) to the page URL. The console then gets, every interval: draw calls,
WebGL calls by kind and Direct3D calls per frame, uniform groups uploaded, programs compiled, context losses, and a table of
every feature the game hit that is **unsupported**, **approximated**, **failed** (a create or a compile), an **event**
(context lost/restored) or a **slow path** (GPU read-back, CPU DXT decode). Nothing is counted or formatted while it is off.
Related options: `-webd3d8shaders` (advertise vs.1.1/ps.1.4 so the game uses its shader paths, default off),
`-webd3d8loseafter=N[,M]` (drop the WebGL context after N frames and restore it M frames later, to test the device-lost path),
`-webd3d8debug[=flags]` (existing).

## Device methods

| Method | Game use | Status | Test |
|---|---|---|---|
| Direct3DCreate8, CheckDeviceType/Format/DepthStencilMatch, EnumAdapterModes, GetAdapterIdentifier | startup | full (one adapter, caps in `d3d8.cpp`) | G |
| CheckDeviceMultiSampleType | WW3D2 antialiasing option | full: 2/4/8 samples answered statically, the device clamps to `GL_MAX_SAMPLES` | F:msaa |
| CreateDevice / Reset | startup, device lost, resize | full (see Context loss) | G, F:contextloss |
| TestCooperativeLevel | 8 | full: DEVICELOST / DEVICENOTRESET / OK | F:contextloss, G (`-webd3d8loseafter`) |
| Present, Clear, BeginScene, EndScene | every frame | full | T0, G |
| SetRenderState/GetRenderState | 126 | see render state table | T0, F |
| SetTextureStageState | 89 | see texture stage table | T0, F |
| SetTexture, SetTransform, MultiplyTransform, SetViewport, SetMaterial, SetLight, LightEnable, SetClipPlane | many | full (8 lights, 6 clip planes) | T0 scene2 |
| SetStreamSource, SetIndices (with BaseVertexIndex), DrawPrimitive, DrawIndexedPrimitive, DrawPrimitiveUP, DrawIndexedPrimitiveUP | 11/8/1/9/12 | full: lists, strips, fans, points, lines | T0, G |
| CreateVertexBuffer / IndexBuffer (managed, default+dynamic), Lock/Unlock (DISCARD, NOOVERWRITE, READONLY) | 6/5 | full (system memory copy + dirty range upload) | T0, G |
| CreateTexture (all formats below), CreateCubeTexture, CreateVolumeTexture, LockRect/LockBox, GetSurfaceLevel, GetLevelDesc | 4 | full | T0 scene3, F |
| CreateImageSurface, CopyRects, UpdateTexture, GetFrontBuffer, GetBackBuffer | 2/2/5/1/1 | full | T0 scene3 |
| CreateRenderTarget / CreateDepthStencilSurface / SetRenderTarget / GetRenderTarget / GetDepthStencilSurface | 2/2/5/2/2 | full; a depth buffer larger than the render target (the water reflection) or with another sample count gets a private one, because WebGL refuses mismatching attachments | T0 scene C, F:msaa, G |
| CreateVertexShader / SetVertexShader / SetVertexShaderConstant / DeleteVertexShader | 1/22/11/2 | full for vs.1.1 (see shader table) | F:game_vshaders |
| CreatePixelShader / SetPixelShader / SetPixelShaderConstant / DeletePixelShader | 5/21/12/15 | full for ps.1.0-1.4 | F:ps_ops, F:game_shaders |
| SetGammaRamp, GetAvailableTextureMem, ResourceManagerDiscardBytes, ValidateDevice | 1/1/1/1 | full / constants | T0 |
| ShowCursor, SetCursorProperties, SetCursorPosition | 4/2/1 | ignored: the browser draws the cursor | |
| CreateAdditionalSwapChain | 1 (dx8wrapper, optional) | n/a: reports D3DERR_NOTAVAILABLE | |
| **ProcessVertices** | 2, both inside `#ifdef PRE_TRANSFORM_VERTEX`, which `BaseHeightMap.h` leaves undefined ("not a performance win") | **unused**; returns D3DERR_INVALIDCALL and is counted if it is ever called | |
| DrawRectPatch / DrawTriPatch / DeletePatch, SetPaletteEntries, SetCurrentTexturePalette | 0 | unused (no N-patch or palette caps are reported) | |
| GetInfo, SetClipStatus | 0 | unused | |
| State blocks (Create/Apply/Capture/DeleteStateBlock) | 0 | full (snapshots) | |
| Vertex tweening, vertex blending, indexed vertex blending | 0: `D3DRS_VERTEXBLEND`, `INDEXEDVERTEXBLENDENABLE` and `TWEENFACTOR` appear only in commented-out code and in the render-state name table; no `D3DFVF_XYZB*` vertex is ever created (`dx8fvf.cpp` only parses the format); skinning is done on the CPU by WW3D2; `MaxVertexBlendMatrices` is reported as 0 | **unused**, non-default values are counted | F:diagnostics |

## Context loss and restore

WebGL contexts are lost on GPU resets, driver crashes, sleep/wake and GPU switches. The device installs `webglcontextlost`
(with `preventDefault`, otherwise the browser never restores) and `webglcontextrestored` listeners on the canvas (works on
the OffscreenCanvas of the engine thread) and polls `isContextLost()` from `TestCooperativeLevel`/`Present`/`Clear`.

1. lost: `Present`, `Clear`, `TestCooperativeLevel` return `D3DERR_DEVICELOST`, draws are skipped, every GL object forgets
   its name. `TestCooperativeLevel` also yields to the browser through `OnFramePresented`, because WW3D2 stops calling
   `Present` while the device is lost and the restore event can only run when the thread yields.
2. restored: `TestCooperativeLevel` returns `D3DERR_DEVICENOTRESET`; WW3D2 runs `DX8Wrapper::Reset_Device`.
3. `Reset()` creates the GL objects again: every vertex/index buffer from its system memory copy, every managed texture
   from its shadow copy (default-pool textures come back empty, as in Direct3D), render targets and depth buffers empty,
   programs recompile lazily, the present program, the vertex array cache, samplers.

Verified end to end in the real game with `-webd3d8loseafter=200,25` (loss and restore in a running skirmish, picture
and 400 calls/frame afterwards) and by `F:contextloss` (two losses, a texture created while lost, render target,
pixel shader, managed buffers).

## Multisampling

`CreateDevice`/`Reset` with `MultiSampleType` 2..8 and `SWAPEFFECT_DISCARD` render into a multisampled renderbuffer (and
depth buffer) and resolve into the back buffer texture by `glBlitFramebuffer` before `Present`, `GetFrontBuffer`,
`CopyRects`, `LockRect` or a read-back. `D3DSURFACE_DESC::MultiSampleType` is reported, so the game's own check in
`W3DShaderManager::init` (it disables the render-to-texture screen filters under MSAA, as Direct3D 8 requires) keeps working.
When the context offers fewer samples the count is clamped (counted as approximated); when it offers none the device is
created without multisampling. `CreateRenderTarget`/`CreateDepthStencilSurface` accept sample counts too. Tests: `F:msaa`, `F:msaa-off`.

## Render states

| State | Game use | Status | Notes |
|---|---|---|---|
| ZENABLE, ZFUNC, ZWRITEENABLE | 16/35/17 | full | ZENABLE=D3DZB_USEW treated as TRUE (counted) |
| ALPHABLENDENABLE, SRCBLEND, DESTBLEND, BLENDOP | 56/57/54/8 | full | all 13 blend factors, ADD/SUBTRACT/REVSUBTRACT/MIN/MAX; BOTHSRCALPHA/BOTHINVSRCALPHA expand to a pair (counted) |
| ALPHATESTENABLE, ALPHAREF, ALPHAFUNC | 20/13/11 | full | baked into the fragment shader (8 bit compare) |
| CULLMODE | 24 | full | |
| FILLMODE | 43 | approx | WIREFRAME/POINT rebuild the primitive on the CPU (no polygon mode in WebGL2; `WEBGL_polygon_mode` is not used); counted |
| SHADEMODE | 12 | full | FLAT takes the first vertex: `WEBGL_provoking_vertex` when the browser has it, otherwise the triangles are reordered (lists and strips; fans use the wrong vertex, counted). The only flat-shaded draws of the game (`W3DVolumetricShadow`) use one colour. Tests: F:flat, F:flat_fallback |
| STENCILENABLE/FUNC/REF/MASK/WRITEMASK/FAIL/ZFAIL/PASS | 36/26/24/22/20/22/20/24 | full | all 8 operations; volumetric shadows (T0 scene2, G) |
| COLORWRITEENABLE | 38 | full | |
| ZBIAS | 35 | approx | polygon offset; the caps do not advertise `D3DPRASTERCAPS_ZBIAS`, so WW3D2 biases through the projection matrix instead |
| LIGHTING, AMBIENT, COLORVERTEX, material sources (DIFFUSE/SPECULAR/AMBIENT/EMISSIVE), SPECULARENABLE, LOCALVIEWER, NORMALIZENORMALS | 26/14/5/8,5,8,8/7/5/6 | full | up to 8 directional/point/spot lights; per-vertex lighting like the fixed-function pipeline |
| FOGENABLE, FOGCOLOR, FOGSTART/END/DENSITY, FOGTABLEMODE, FOGVERTEXMODE, RANGEFOGENABLE | 10/6/5,5,4/5/5/5 | full | vertex and table fog, LINEAR/EXP/EXP2, range fog |
| TEXTUREFACTOR | 14 | full | |
| CLIPPLANEENABLE | 6 | full | 6 user clip planes (fragment discard) |
| POINTSPRITEENABLE, POINTSCALEENABLE, POINTSIZE, POINTSIZE_MIN/MAX, POINTSCALE_A/B/C | 6/6/5/5/5/5 | full | limited to `ALIASED_POINT_SIZE_RANGE` |
| **WRAP0..WRAP7** | 6, all in `W3DWater.cpp` `drawSea` (`D3DWRAP_U|V` on stage 0 around the vertex-shader water patches) | **ignored** (counted when non-zero) | Cylindrical wrapping interpolates the shortest way around the 0..1 circle. The patch vertices carry `u = x * 42/14 = 3x`, so neighbours differ by exactly 3.0: any emulation either does nothing (whole numbers) or flattens the water texture, and GPUs/Wine/DXVK ignore it. No other draw sets WRAP |
| DITHERENABLE, LASTPIXEL, EDGEANTIALIAS, ZVISIBLE, LINEPATTERN, SOFTWAREVERTEXPROCESSING, CLIPPING, PATCH*, POSITIONORDER, NORMALORDER, MULTISAMPLEMASK, DEBUGMONITORTOKEN | 0-6, defaults or comments | ignored | non-default LINEPATTERN, EDGEANTIALIAS, CLIPPING=FALSE and MULTISAMPLEANTIALIAS=FALSE are counted |
| VERTEXBLEND, INDEXEDVERTEXBLENDENABLE, TWEENFACTOR | 0 | unused (see above) | counted |

## Texture stage states and operations

| Item | Game use | Status | Notes |
|---|---|---|---|
| COLOROP/ALPHAOP: DISABLE, SELECTARG1/2, MODULATE/2X/4X, ADD, ADDSIGNED/2X, SUBTRACT, ADDSMOOTH, BLEND{DIFFUSE,TEXTURE,FACTOR,CURRENT}ALPHA, BLENDTEXTUREALPHAPM, PREMODULATE, MODULATEALPHA_ADDCOLOR, MODULATECOLOR_ADDALPHA, MODULATEINVALPHA_ADDCOLOR, MODULATEINVCOLOR_ADDALPHA, DOTPRODUCT3, MULTIPLYADD, LERP | all used | full except PREMODULATE (approx: SELECTARG1, counted) | T0, G |
| BUMPENVMAP, BUMPENVMAPLUMINANCE | caps queries only (`dx8caps.cpp`); the caps do not advertise them, so no stage is ever set to them | unused in the fixed-function pipeline (counted); bump mapping goes through ps `texbem`/`texbeml`, full | F:game_shaders (wave.nvp) |
| COLORARG0/1/2, ALPHAARG0/1/2, RESULTARG (TEMP) | all | full | TA_DIFFUSE/CURRENT/TEXTURE/TFACTOR/SPECULAR/TEMP, COMPLEMENT, ALPHAREPLICATE |
| TEXCOORDINDEX incl. CAMERASPACENORMAL/POSITION/REFLECTIONVECTOR | 125/8/30/7 | full | T0 scene2 |
| TEXTURETRANSFORMFLAGS COUNT1..4, PROJECTED | 79 | full | |
| ADDRESSU/V/W: WRAP, CLAMP, MIRROR | 55/48/2 | full | |
| ADDRESS BORDER + BORDERCOLOR | `dx8wrapper.cpp` only sets BORDERCOLOR to 0 and `D3DTADDRESS_BORDER` appears in a name table: the game never selects it | full by emulation: WebGL has no border, so the shader returns the border colour outside [0,1] (a hard cut: no filtering across the edge, counted as approx) | F:border |
| ADDRESS MIRRORONCE | 2 (name tables) | approx (MIRROR, counted) | |
| MIN/MAG/MIPFILTER POINT, LINEAR, NONE, ANISOTROPIC; MAXANISOTROPY, MIPMAPLODBIAS, MAXMIPLEVEL | all | full | `EXT_texture_filter_anisotropic` |
| FLATCUBIC, GAUSSIANCUBIC | 2 (name tables) | approx (LINEAR, counted) | |
| BUMPENVMAT00..11, BUMPENVLSCALE, BUMPENVLOFFSET | 7/7 | full (texbem, texbeml, bem) | T0 --shaders |

## Formats, pools, usage

| Item | Status |
|---|---|
| A8R8G8B8, X8R8G8B8, R5G6B5, A1R5G5B5, X1R5G5B5, A4R4G4B4, X4R4G4B4, R8G8B8, A8, L8, A8L8, A4L4 textures and surfaces | full (converted on upload where WebGL has no equivalent) |
| DXT1-DXT5 | full: `WEBGL_compressed_texture_s3tc`, or decoded on the CPU when missing (counted; tested with `--no-s3tc`) |
| V8U8, Q8W8V8U8 (bump) | full (SNORM). L6V5U5, X8L8V8U8, V16U16, W11V11U10: image surfaces only; CheckDeviceFormat refuses them as textures, the game picks another format |
| P8, A8P8, R3G3B2, A8R3G3B2, UYVY, YUY2 | image surfaces / conversion only; not textures (refused by CheckDeviceFormat; creating one is counted as failed) |
| D16, D16_LOCKABLE, D24S8, D24X8, D24X4S4, D32, D15S1 | full as renderbuffers (stencil formats get DEPTH24_STENCIL8) |
| INDEX16, INDEX32 | full |
| Pools DEFAULT/MANAGED/SYSTEMMEM, usage RENDERTARGET/DYNAMIC/WRITEONLY/POINTS/NPATCHES/SOFTWAREPROCESSING | full / accepted. WW3D2 builds with `USE_MANAGED_TEXTURES`, so file textures are managed and keep a system copy |
| Lock flags DISCARD, NOOVERWRITE, READONLY, NO_DIRTY_UPDATE, NOSYSLOCK | full |
| Render target textures, back buffer read-back, `LockRect` on a render target | full (GPU read-back, counted as slow path) |

## Vertex formats

FVF: XYZ, XYZRHW, NORMAL, PSIZE (point size), DIFFUSE, SPECULAR, TEX1..TEX8 with 1-4 components: full. XYZB1..5: positions
accepted, weights skipped (unused by the game, counted). `D3DVSD_*` declarations: FLOAT1-4, D3DCOLOR, UBYTE4, SHORT2/4,
multiple streams, `D3DVSD_CONST`, skip tokens: full (the game uses `D3DVSD_STREAM`, `REG`, `END`).

## Shaders

The default caps report no shader support (the game then keeps to its fixed-function paths, which is what is played today).
`-webd3d8shaders` reports vs.1.1/ps.1.4 so that W3DShaderManager and the water use their shader paths; with the real
`ShadersZH.big` the `.pso`/`.vso` files load through `CreatePixelShader`/`CreateVertexShader`, with the starter pack they do not exist.

| Shader | Source in the repo | Status | Test |
|---|---|---|---|
| vs.1.1: mov add sub(macro for add with a negated source) mad mul rcp rsq dp3 dp4 min max slt sge exp expp log logp lit dst frc m3x2 m3x3 m3x4 m4x3 m4x4, `a0.x` and `c[a0.x+n]`, `def`, all output registers | `Trees.nvv`, `wave.nvv`, `MotionBlur.nvv` | full | F:game_vshaders (Trees.nvv transform, colour scale and the wave skew through `c[a0.x+8]`; wave.nvv dp4/rcp/mad) |
| ps.1.0-1.3: tex texcoord texkill texbem texbeml texreg2ar texreg2gb texreg2rgb texm3x2pad texm3x2tex texm3x3pad texm3x3tex texm3x3spec texm3x3vspec, add sub mul mad lrp dp3 cnd mov, source modifiers (`1-`, `-`, `_bias`, `_bx2`, `_x2`), destination modifiers (`_x2/_x4/_x8/_d2.._sat`), write masks, co-issue (`+`) | `terrain*.nvp`, `fterrain*.nvp`, `roadnoise2.nvp`, `monochrome.nvp`, `invmonochrome.nvp`, `Trees.nvp`, `motionblur.nvp`, `wave.nvp`, the four inline shaders of `W3DWater.cpp`/`W3DProfilerFrameCapture.cpp` | full | F:game_shaders: every `.nvp` source assembles, links and the arithmetic ones are compared with hand computed values; T0 --shaders runs the inline ones |
| **ps.1.2/1.3: texdp3, texdp3tex, texm3x3, texm3x2depth** (new) | none (no game shader uses them) | full | F:ps_ops |
| **ps.1.4: texdepth** (new), texld, texcrd, cmp, bem, phase, `def` | the swizzle shader of `W3DProfilerFrameCapture.cpp` | full (depth written through `gl_FragDepth` mapped to the viewport depth range) | F:ps_ops, T0 --shaders |
| texm3x3diff | none | refused at creation and counted | |

The NVASM sources use `#define`/`#ifdef`; the test preprocesses them (the shipped `.pso`/`.vso` binaries are plain
token streams, which is what `CreatePixelShader`/`CreateVertexShader` parse: version token, instructions, comments, `def`,
co-issue bit, `0x0000FFFF`).

## D3DX

| Function | Game use | Status |
|---|---|---|
| D3DXMatrix* (Inverse, Multiply, Scaling, Translation, Transpose, RotationZ, Identity), D3DXVec3/4Transform, D3DXVec4Dot, D3DXGetFVFVertexSize, D3DXGetErrorStringA | many | full (the whole math library is implemented) |
| D3DXAssembleShader | 4 | full for vs/ps text (see above) |
| D3DXFilterTexture | 6, always `D3DX_FILTER_BOX` | full: **all filter types** now behave as documented: POINT (nearest texel), LINEAR (four nearest texels), TRIANGLE (every covered texel contributes equally), BOX (area/2x2 average, an enlarging box falls back to linear), NONE (copy the overlap, transparent black elsewhere), MIRROR_U/V flags; DITHER is ignored (8 bit targets). F:d3dx_filters |
| D3DXLoadSurfaceFromSurface | 4 with BOX, NONE, TRIANGLE | full (same filters; a NONE copy between different sizes no longer scales) |
| D3DXCreateTexture, CreateCubeTexture, CreateVolumeTexture, CreateTextureFromFileExA, LoadSurfaceFromFile | 4/4/2/1/1 | full (DDS, TGA, BMP) |
| D3DXCreateFont | 0 (comments) | not implemented |

## Rendering features of the game (what draws through the device)

| Feature | Device features it needs | Status |
|---|---|---|
| Terrain (`BaseHeightMap`, `HeightMap`, `TerrainTex`): multitexture blends, 2 pass, cloud and noise stages, `D3DXFilterTexture` on built tiles, roads/bridges (`W3DRoadBuffer`, `W3DBridgeBuffer`), scorch decals | stages 0-3, MODULATE/SELECTARG/BLEND*ALPHA, dynamic vertex buffers, alpha blend + test, texture transforms, pixel shaders when enabled | full (G: terrain, roads in T0) |
| Water (`W3DWater`): sea patches with bump env map, reflection render target + 256x256 depth, river/trapezoid shaders, mesh water | render-to-texture with the large back buffer depth (now works), texbem, vertex shader with relative addressing (not used), BUMPENV constants | full; WRAP0 ignored (see above) |
| Shadows: projected (`W3DProjectedShadow`, render target + texgen projection), volumetric stencil (`W3DVolumetricShadow`: incr/decr stencil, cull CW, colour write off, full screen quad with DESTCOLOR blend) | stencil ops, render target with its own depth, CAMERASPACEPOSITION projected texture coordinates, flat shading | full |
| Particles, streaks (`pointgr`, `streak`, `W3DParticleSys`) | point sprites with distance scale, dynamic vertex buffers, additive/alpha blending | full (T0 scene2 points, G) |
| Shroud (`W3DShroud`): A4R4G4B4/R5G6B5 default pool texture written each frame | `LockRect` on default-pool textures, texture transform | full |
| Radar (`W3DRadar`), UI, text, video (`W3DVideoBuffer`) | pre-transformed vertices, `UpdateTexture`, dynamic textures, A8/L8 formats | full (G menus and control bar) |
| Weather (`W3DSnow`): point sprites with `D3DUSAGE_POINTS` | POINTS usage accepted | full |
| Smudges/heat haze (`W3DSmudge`): copy of the back buffer into a default-pool texture | `CopyRects`/`GetBackBuffer`, with MSAA a resolve first | full (read-back) |
| Screen filters (`W3DShaderManager`: black&white, motion blur, crossfade) | render target the size of the back buffer, drawn back with texture stages or ps | full without MSAA; the game turns them off under MSAA |
| Pixel shader screen filter, trees (`Trees.nvv`) | see shaders | full when enabled |
| Frame capture/gamma | `SetGammaRamp` | full (LUT in the present pass) |

## Performance (starter pack skirmish, calls per frame)

Measured with `-webd3d8report` in `starter_flow.mjs` (about 80 draws per frame; counts, not fps):

| | before | after |
|---|---|---|
| WebGL calls per frame | ~850 | ~400 |
| vertex attribute calls | 317 | 0 |
| uniform calls | 183 | 80 |
| state calls | 64 | 39 |
| synchronous calls | 3.3 | 0 |

What changed: per-uniform-group version counters with packed uniform arrays (a state change costs one call per program that
uses it), a cache of vertex array objects keyed by layout/buffers/strides (base vertex passed to the draw call when
`WEBGL_draw_instanced_base_vertex_base_instance` exists, otherwise promoted after the second sighting), attachment caching for
framebuffers, `Clear`/`Present` keeping the pipeline cache valid instead of invalidating it, point size code only in point
programs, redundant `SetTransform` filtering. `F:state_cache` guards it (at most 3 calls per repeated draw).
