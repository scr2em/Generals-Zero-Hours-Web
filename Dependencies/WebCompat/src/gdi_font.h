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
** WebAssembly port: the font side of the GDI text emulation. Keeps the fonts
** that the game registers (AddFontResource) and the bundled fallback fonts,
** maps a LOGFONT to one of them the way the Windows font mapper would
** (face name, weight, italic, size in pixels or cell height), derives the
** TEXTMETRIC values from the font tables and rasterises single glyphs with
** stb_truetype. See win32_gdi.cpp for the device context side.
*/
#pragma once

#include <windows.h>

#include <memory>
#include <stdint.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace WebCompat
{
namespace Gdi
{

// One face of a loaded font file (TrueType or OpenType with TrueType or CFF
// outlines). The definition is private to gdi_font.cpp.
struct Face;

// A LOGFONT resolved to a face and a pixel size, with the text metrics Windows
// would report for it. Immutable once resolved.
struct ScaledFont
{
	std::shared_ptr<const Face> face; // null: no font at all, text has metrics but draws nothing
	int ppem = 0;                     // em size in pixels
	float widthFactor = 1.0f;         // horizontal scale from lfWidth
	bool synthBold = false;           // the face is lighter than requested: smear the glyphs
	bool synthItalic = false;         // the face is upright but italic was requested: shear
	bool antialias = true;            // false: 1-bit output (NONANTIALIASED_QUALITY)
	bool underline = false;
	bool strikeOut = false;

	// TEXTMETRIC
	int height = 0;
	int ascent = 0;
	int descent = 0;
	int internalLeading = 0;
	int externalLeading = 0;
	int avgCharWidth = 0;
	int maxCharWidth = 0;
	int overhang = 0;
	int weight = FW_NORMAL;
	int firstChar = 0x20;
	int lastChar = 0xFF;
	int defaultChar = 0x7F;
	int breakChar = 0x20;
	BYTE pitchAndFamily = 0;
	BYTE charSet = ANSI_CHARSET;
	WCHAR faceName[LF_FACESIZE] = {};
	uint32_t generation = 0; // RegistryGeneration() when resolved
};

// A rendered glyph: 8-bit coverage, origin at the pen position on the baseline.
struct GlyphBitmap
{
	int left = 0;   // x of the first column relative to the pen
	int top = 0;    // y of the first row relative to the baseline (negative: above)
	int width = 0;
	int height = 0;
	int advance = 0;
	std::vector<uint8_t> coverage;
};

// The horizontal metrics of a glyph in whole pixels (the A, B and C widths of GetCharABCWidths).
struct GlyphAbc
{
	int a = 0;
	int b = 0;
	int c = 0;
	int advance = 0;
};

// Increases whenever the set of registered fonts changes; a ScaledFont resolved
// under an older generation is stale.
uint32_t RegistryGeneration();

// AddFontResource / RemoveFontResource. `key` identifies the file for the
// reference count (the Windows path as the game wrote it, upper-cased by the
// caller is not needed), `path` is the path in the file system. Add returns
// the number of faces the file holds, 0 if it cannot be read as a font.
int AddFontFile(const std::string &key, const std::string &path);
bool RemoveFontFile(const std::string &key);

// Directory searched for the bundled Liberation fonts (default /webfonts, or
// the WEBCOMPAT_FONT_DIR environment variable). Mainly for the tests.
void SetBundledFontDirectory(const std::string &directory);

// Maps a LOGFONTW to a face and computes its metrics. Always succeeds; without
// any usable face the result has `face == nullptr` and metrics derived from the size alone.
void ResolveFont(const LOGFONTW &lf, ScaledFont *out);

// Advance of a code point in whole pixels, without synthetic bold.
int GlyphAdvance(const ScaledFont &font, uint32_t codePoint);
bool GlyphMetrics(const ScaledFont &font, uint32_t codePoint, GlyphAbc *abc);
// Renders a code point. Returns false for blank glyphs (the bitmap is empty
// but `advance` is valid).
bool RenderGlyph(const ScaledFont &font, uint32_t codePoint, GlyphBitmap *out);

// The family name of the face that was chosen, for diagnostics and tests.
std::string FaceFamilyName(const ScaledFont &font);
// True if the chosen face (not a fallback) has a glyph for the code point.
bool FaceHasGlyph(const ScaledFont &font, uint32_t codePoint);

} // namespace Gdi
} // namespace WebCompat
