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
** WebAssembly port: the GDI subset the game uses to draw text, implemented in
** memory. The game rasterises its fonts through GDI (WW3D2 render2dsentence.cpp):
** it creates a font, selects it and a 24 bit DIB section into a memory DC, draws one
** character with ExtTextOutW, measures it with GetTextExtentPoint32W and reads the
** pixels from the DIB. This file keeps the device contexts, DIB sections, fonts
** and brushes; gdi_font.cpp supplies the font mapping, the TEXTMETRIC values
** and the glyph rasteriser.
**
** Supported: memory DCs; DIB sections of 1, 4, 8, 16, 24 and 32 bits (top-down or
** bottom-up) and compatible bitmaps (32 bit); solid brushes; FillRect, BitBlt
** (SRCCOPY, BLACKNESS, WHITENESS, PATCOPY), GetPixel/SetPixel; fonts by face name,
** weight, italic, underline, strike-out, width, cell or em height, quality
** (1 bit or anti-aliased); ExtTextOut/TextOut/DrawText in the A and W variants;
** text and background colour, background mode, text alignment; text extents, text
** metrics, character widths and ABC widths; AddFontResource(Ex)/RemoveFontResource(Ex).
** Not supported: rotated text (lfEscapement), ClearType colour fringes (output is
** grey anti-aliased), pens, regions and clipping other than ETO_CLIPPED.
*/
#include "webcompat_internal.h"

#include "charset.h"
#include "gdi_font.h"

#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

using namespace WebCompat;

namespace
{

// ---------------------------------------------------------------------------
// Objects
// ---------------------------------------------------------------------------

enum GdiKind : uint32_t { GDI_STOCK = 1, GDI_FONT, GDI_BITMAP, GDI_BRUSH, GDI_DC };

struct GdiObject
{
	static const uint32_t MAGIC = 0x47444F42; // "GDOB"

	explicit GdiObject(GdiKind k) : magic(MAGIC), kind(k) {}
	virtual ~GdiObject() { magic = 0; }

	uint32_t magic;
	GdiKind kind;
	bool stock = false;         // shared stock object, never deleted
	int selectCount = 0;
};

struct FontObject : GdiObject
{
	FontObject() : GdiObject(GDI_FONT) {}

	LOGFONTW lf = {};
	Gdi::ScaledFont resolved;
	bool resolvedValid = false;
	std::unordered_map<uint32_t, Gdi::GlyphBitmap> glyphs; // rasterised glyphs of the resolved font
};

struct BitmapObject : GdiObject
{
	BitmapObject() : GdiObject(GDI_BITMAP) {}
	~BitmapObject() override { free(bits); }

	int width = 0;
	int height = 0;
	int bitsPerPixel = 32;
	size_t stride = 0;
	bool topDown = true;
	bool rgb565 = false;
	std::vector<COLORREF> palette; // bitsPerPixel <= 8
	uint8_t *bits = nullptr;
};

struct BrushObject : GdiObject
{
	BrushObject() : GdiObject(GDI_BRUSH) {}

	COLORREF color = 0;
	bool hollow = false;
};

struct DcObject : GdiObject
{
	DcObject() : GdiObject(GDI_DC) {}

	BitmapObject *bitmap = nullptr;
	FontObject *font = nullptr;
	BrushObject *brush = nullptr;
	COLORREF textColor = RGB(0, 0, 0);
	COLORREF bkColor = RGB(255, 255, 255);
	int bkMode = OPAQUE;
	UINT textAlign = TA_LEFT | TA_TOP;
	int positionX = 0;
	int positionY = 0;
};

template <class T>
T *As(HANDLE handle, GdiKind kind)
{
	GdiObject *object = reinterpret_cast<GdiObject *>(handle);
	if (!object || object->magic != GdiObject::MAGIC || object->kind != kind)
		return nullptr;
	return static_cast<T *>(object);
}

// Stock objects ---------------------------------------------------------------

struct StockObjects
{
	BitmapObject defaultBitmap; // the 1x1 monochrome bitmap every new DC starts with
	FontObject fonts[8];        // OEM_FIXED_FONT .. DEFAULT_GUI_FONT (10..17)
	BrushObject brushes[6];     // WHITE_BRUSH .. NULL_BRUSH
	GdiObject other;            // pens and the default palette

	StockObjects() : other(GDI_STOCK)
	{
		defaultBitmap.stock = true;
		defaultBitmap.width = defaultBitmap.height = 1;
		defaultBitmap.bitsPerPixel = 1;
		defaultBitmap.stride = 4;
		defaultBitmap.bits = static_cast<uint8_t *>(calloc(1, 4));
		defaultBitmap.palette = { RGB(0, 0, 0), RGB(255, 255, 255) };

		struct FontSpec { const char *face; int height; int weight; BYTE pitchAndFamily; };
		static const FontSpec specs[8] = {
			{ "Terminal", -12, FW_NORMAL, FIXED_PITCH | FF_MODERN },     // OEM_FIXED_FONT
			{ "Courier New", -13, FW_NORMAL, FIXED_PITCH | FF_MODERN },  // ANSI_FIXED_FONT
			{ "MS Sans Serif", -13, FW_NORMAL, VARIABLE_PITCH | FF_SWISS }, // ANSI_VAR_FONT
			{ "System", -16, FW_BOLD, VARIABLE_PITCH | FF_SWISS },       // SYSTEM_FONT
			{ "System", -16, FW_NORMAL, VARIABLE_PITCH | FF_SWISS },     // DEVICE_DEFAULT_FONT
			{ "", 0, FW_NORMAL, 0 },                                     // DEFAULT_PALETTE slot, unused
			{ "Fixedsys", -16, FW_NORMAL, FIXED_PITCH | FF_MODERN },     // SYSTEM_FIXED_FONT
			{ "MS Shell Dlg", -11, FW_NORMAL, VARIABLE_PITCH | FF_SWISS }, // DEFAULT_GUI_FONT
		};
		for (int i = 0; i < 8; ++i)
		{
			fonts[i].stock = true;
			fonts[i].lf.lfHeight = specs[i].height;
			fonts[i].lf.lfWeight = specs[i].weight;
			fonts[i].lf.lfCharSet = DEFAULT_CHARSET;
			fonts[i].lf.lfQuality = ANTIALIASED_QUALITY;
			fonts[i].lf.lfPitchAndFamily = specs[i].pitchAndFamily;
			for (int k = 0; specs[i].face[k] && k < LF_FACESIZE - 1; ++k)
				fonts[i].lf.lfFaceName[k] = (WCHAR)specs[i].face[k];
		}
		static const COLORREF colors[5] = { RGB(255, 255, 255), RGB(192, 192, 192), RGB(128, 128, 128), RGB(64, 64, 64), RGB(0, 0, 0) };
		for (int i = 0; i < 6; ++i)
		{
			brushes[i].stock = true;
			brushes[i].color = i < 5 ? colors[i] : 0;
			brushes[i].hollow = i == 5;
		}
		other.stock = true;
	}
};

StockObjects &Stock()
{
	static StockObjects stock;
	return stock;
}

FontObject *DefaultFont()
{
	return &Stock().fonts[SYSTEM_FONT - OEM_FIXED_FONT];
}

// ---------------------------------------------------------------------------
// Pixels
// ---------------------------------------------------------------------------

inline int Red(COLORREF c) { return (int)(c & 0xFF); }
inline int Green(COLORREF c) { return (int)((c >> 8) & 0xFF); }
inline int Blue(COLORREF c) { return (int)((c >> 16) & 0xFF); }
inline COLORREF Rgb(int r, int g, int b) { return (COLORREF)(r | (g << 8) | (b << 16)); }

inline uint8_t *RowPointer(const BitmapObject *bitmap, int y)
{
	return bitmap->bits + (size_t)(bitmap->topDown ? y : bitmap->height - 1 - y) * bitmap->stride;
}

int NearestPaletteIndex(const BitmapObject *bitmap, COLORREF color)
{
	int best = 0;
	int bestDistance = 0x7FFFFFFF;
	for (size_t i = 0; i < bitmap->palette.size(); ++i)
	{
		const COLORREF p = bitmap->palette[i];
		const int dr = Red(p) - Red(color), dg = Green(p) - Green(color), db = Blue(p) - Blue(color);
		const int distance = dr * dr + dg * dg + db * db;
		if (distance < bestDistance)
		{
			bestDistance = distance;
			best = (int)i;
		}
	}
	return best;
}

COLORREF GetPixelValue(const BitmapObject *bitmap, int x, int y)
{
	const uint8_t *row = RowPointer(bitmap, y);
	switch (bitmap->bitsPerPixel)
	{
	case 32:
	case 24:
	{
		const uint8_t *p = row + (size_t)x * (bitmap->bitsPerPixel / 8);
		return Rgb(p[2], p[1], p[0]);
	}
	case 16:
	{
		const unsigned v = row[x * 2] | (row[x * 2 + 1] << 8);
		if (bitmap->rgb565)
			return Rgb(((v >> 11) & 31) * 255 / 31, ((v >> 5) & 63) * 255 / 63, (v & 31) * 255 / 31);
		return Rgb(((v >> 10) & 31) * 255 / 31, ((v >> 5) & 31) * 255 / 31, (v & 31) * 255 / 31);
	}
	case 8:
		return row[x] < bitmap->palette.size() ? bitmap->palette[row[x]] : 0;
	case 4:
	{
		const unsigned index = (x & 1) ? (row[x / 2] & 15) : (row[x / 2] >> 4);
		return index < bitmap->palette.size() ? bitmap->palette[index] : 0;
	}
	case 1:
	{
		const unsigned index = (row[x / 8] >> (7 - (x & 7))) & 1;
		return index < bitmap->palette.size() ? bitmap->palette[index] : 0;
	}
	}
	return 0;
}

void SetPixelValue(BitmapObject *bitmap, int x, int y, COLORREF color)
{
	uint8_t *row = RowPointer(bitmap, y);
	switch (bitmap->bitsPerPixel)
	{
	case 32:
	case 24:
	{
		uint8_t *p = row + (size_t)x * (bitmap->bitsPerPixel / 8);
		p[0] = (uint8_t)Blue(color);
		p[1] = (uint8_t)Green(color);
		p[2] = (uint8_t)Red(color);
		break;
	}
	case 16:
	{
		unsigned v;
		if (bitmap->rgb565)
			v = ((Red(color) >> 3) << 11) | ((Green(color) >> 2) << 5) | (Blue(color) >> 3);
		else
			v = ((Red(color) >> 3) << 10) | ((Green(color) >> 3) << 5) | (Blue(color) >> 3);
		row[x * 2] = (uint8_t)v;
		row[x * 2 + 1] = (uint8_t)(v >> 8);
		break;
	}
	case 8:
		row[x] = (uint8_t)NearestPaletteIndex(bitmap, color);
		break;
	case 4:
	{
		const unsigned index = (unsigned)NearestPaletteIndex(bitmap, color);
		uint8_t &b = row[x / 2];
		b = (x & 1) ? (uint8_t)((b & 0xF0) | index) : (uint8_t)((b & 0x0F) | (index << 4));
		break;
	}
	case 1:
	{
		const unsigned index = (unsigned)NearestPaletteIndex(bitmap, color);
		const uint8_t mask = (uint8_t)(0x80 >> (x & 7));
		uint8_t &b = row[x / 8];
		b = index ? (uint8_t)(b | mask) : (uint8_t)(b & ~mask);
		break;
	}
	}
}

inline int Mix(int destination, int source, int alpha)
{
	return (destination * (255 - alpha) + source * alpha + 127) / 255;
}

// Blends a text colour into a pixel with an 8 bit coverage.
inline void BlendPixel(BitmapObject *bitmap, int x, int y, COLORREF color, int alpha)
{
	if (alpha >= 255)
	{
		SetPixelValue(bitmap, x, y, color);
		return;
	}
	if (bitmap->bitsPerPixel == 24 || bitmap->bitsPerPixel == 32)
	{
		uint8_t *p = RowPointer(bitmap, y) + (size_t)x * (bitmap->bitsPerPixel / 8);
		p[0] = (uint8_t)Mix(p[0], Blue(color), alpha);
		p[1] = (uint8_t)Mix(p[1], Green(color), alpha);
		p[2] = (uint8_t)Mix(p[2], Red(color), alpha);
		return;
	}
	const COLORREF d = GetPixelValue(bitmap, x, y);
	SetPixelValue(bitmap, x, y, Rgb(Mix(Red(d), Red(color), alpha), Mix(Green(d), Green(color), alpha), Mix(Blue(d), Blue(color), alpha)));
}

void FillPixels(BitmapObject *bitmap, RECT rect, COLORREF color)
{
	rect.left = std::max<LONG>(rect.left, 0);
	rect.top = std::max<LONG>(rect.top, 0);
	rect.right = std::min<LONG>(rect.right, bitmap->width);
	rect.bottom = std::min<LONG>(rect.bottom, bitmap->height);
	for (int y = rect.top; y < rect.bottom; ++y)
	{
		for (int x = rect.left; x < rect.right; ++x)
			SetPixelValue(bitmap, x, y, color);
	}
}

// ---------------------------------------------------------------------------
// Fonts
// ---------------------------------------------------------------------------

void AnsiToWide(const char *s, WCHAR *out, size_t capacity)
{
	size_t i = 0;
	for (; s && s[i] && i + 1 < capacity; ++i)
		out[i] = (WCHAR)Cp1252ToUtf16((unsigned char)s[i]);
	out[i] = 0;
}

void WideToAnsi(const WCHAR *s, char *out, size_t capacity)
{
	size_t i = 0;
	for (; s && s[i] && i + 1 < capacity; ++i)
	{
		const int c = Utf16ToCp1252((uint16_t)s[i]);
		out[i] = (char)(c < 0 ? '?' : c);
	}
	out[i] = 0;
}

// The resolved font of a font object, resolved again when fonts were added or removed.
const Gdi::ScaledFont &Resolved(FontObject *font)
{
	if (!font->resolvedValid || font->resolved.generation != Gdi::RegistryGeneration())
	{
		Gdi::ResolveFont(font->lf, &font->resolved);
		font->resolvedValid = true;
		font->glyphs.clear();
	}
	return font->resolved;
}

FontObject *ActiveFont(DcObject *dc)
{
	return dc && dc->font ? dc->font : DefaultFont();
}

const Gdi::GlyphBitmap &CachedGlyph(FontObject *font, uint32_t codePoint)
{
	std::unordered_map<uint32_t, Gdi::GlyphBitmap>::iterator found = font->glyphs.find(codePoint);
	if (found != font->glyphs.end())
		return found->second;
	if (font->glyphs.size() > 4096)
		font->glyphs.clear();
	Gdi::GlyphBitmap &glyph = font->glyphs[codePoint];
	Gdi::RenderGlyph(font->resolved, codePoint, &glyph);
	return glyph;
}

HFONT MakeFont(const LOGFONTW &lf)
{
	FontObject *font = new FontObject();
	font->lf = lf;
	font->lf.lfFaceName[LF_FACESIZE - 1] = 0;
	return reinterpret_cast<HFONT>(font);
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

// A string as code points (UTF-16 surrogates joined) with the index of the first
// UTF-16 unit of each, which is what the lpDx array of ExtTextOut is indexed by.
struct Text
{
	std::vector<uint32_t> codePoints;
	std::vector<int> unitIndex;
};

void ToText(const WCHAR *s, int count, Text *text)
{
	for (int i = 0; i < count; ++i)
	{
		uint32_t c = (uint16_t)s[i];
		const int start = i;
		if (IsHighSurrogate(c) && i + 1 < count && IsLowSurrogate((uint16_t)s[i + 1]))
		{
			c = 0x10000 + ((c - 0xD800) << 10) + ((uint16_t)s[i + 1] - 0xDC00);
			++i;
		}
		text->codePoints.push_back(c);
		text->unitIndex.push_back(start);
	}
}

void ToText(const char *s, int count, Text *text)
{
	for (int i = 0; i < count; ++i)
	{
		text->codePoints.push_back(Cp1252ToUtf16((unsigned char)s[i]));
		text->unitIndex.push_back(i);
	}
}

int CountOf(const char *s, int c) { return c < 0 ? (int)strlen(s) : c; }
int CountOf(const WCHAR *s, int c)
{
	if (c >= 0)
		return c;
	int n = 0;
	while (s[n])
		++n;
	return n;
}

// The advances of the characters of a text and their sum (the pen movement, without the overhang).
int Advances(FontObject *font, const Text &text, const INT *dx, std::vector<int> *advances)
{
	const Gdi::ScaledFont &resolved = Resolved(font);
	int total = 0;
	advances->resize(text.codePoints.size());
	for (size_t i = 0; i < text.codePoints.size(); ++i)
	{
		const int advance = dx ? dx[text.unitIndex[i]] : Gdi::GlyphAdvance(resolved, text.codePoints[i]);
		(*advances)[i] = advance;
		total += advance;
	}
	return total;
}

BOOL TextExtent(HDC hdc, const Text &text, LPSIZE size)
{
	if (!size)
		return FALSE;
	FontObject *font = ActiveFont(As<DcObject>(hdc, GDI_DC));
	std::vector<int> advances;
	const int width = Advances(font, text, nullptr, &advances);
	const Gdi::ScaledFont &resolved = Resolved(font);
	size->cx = width + (width > 0 ? resolved.overhang : 0);
	size->cy = resolved.height;
	return TRUE;
}

BOOL DrawTextRun(HDC hdc, int x, int y, UINT options, const RECT *rect, const Text &text, const INT *dx)
{
	DcObject *dc = As<DcObject>(hdc, GDI_DC);
	if (!dc)
		return FALSE;
	FontObject *font = ActiveFont(dc);
	const Gdi::ScaledFont &resolved = Resolved(font);
	std::vector<int> advances;
	const int penWidth = Advances(font, text, dx, &advances);
	const int width = penWidth + (penWidth > 0 ? resolved.overhang : 0);

	if (dc->textAlign & TA_UPDATECP)
	{
		x = dc->positionX;
		y = dc->positionY;
	}
	switch (dc->textAlign & TA_CENTER) // TA_CENTER (6) contains the TA_RIGHT bit
	{
	case TA_CENTER: x -= width / 2; break;
	case TA_RIGHT: x -= width; break;
	}
	int top = y;
	switch (dc->textAlign & TA_BASELINE) // TA_BASELINE (24) contains the TA_BOTTOM bit
	{
	case TA_BASELINE: top = y - resolved.ascent; break;
	case TA_BOTTOM: top = y - resolved.height; break;
	}

	BitmapObject *bitmap = dc->bitmap;
	if (bitmap && bitmap->bits)
	{
		// Background.
		if ((options & ETO_OPAQUE) && rect)
			FillPixels(bitmap, *rect, dc->bkColor);
		else if (dc->bkMode == OPAQUE && width > 0)
		{
			RECT cell = { x, top, x + width, top + resolved.height };
			FillPixels(bitmap, cell, dc->bkColor);
		}

		// Clip box: the bitmap, narrowed by ETO_CLIPPED.
		RECT clip = { 0, 0, bitmap->width, bitmap->height };
		if ((options & ETO_CLIPPED) && rect)
		{
			clip.left = std::max(clip.left, rect->left);
			clip.top = std::max(clip.top, rect->top);
			clip.right = std::min(clip.right, rect->right);
			clip.bottom = std::min(clip.bottom, rect->bottom);
		}

		const int baseline = top + resolved.ascent;
		int pen = x;
		for (size_t i = 0; i < text.codePoints.size(); ++i)
		{
			const Gdi::GlyphBitmap &glyph = CachedGlyph(font, text.codePoints[i]);
			if (glyph.width > 0)
			{
				const int originX = pen + glyph.left;
				const int originY = baseline + glyph.top;
				const int x0 = std::max<int>(0, clip.left - originX);
				const int x1 = std::min<int>(glyph.width, clip.right - originX);
				const int y0 = std::max<int>(0, clip.top - originY);
				const int y1 = std::min<int>(glyph.height, clip.bottom - originY);
				for (int row = y0; row < y1; ++row)
				{
					const uint8_t *coverage = &glyph.coverage[(size_t)row * glyph.width];
					for (int col = x0; col < x1; ++col)
					{
						if (coverage[col])
							BlendPixel(bitmap, originX + col, originY + row, dc->textColor, coverage[col]);
					}
				}
			}
			pen += advances[i];
		}

		// Underline and strike-out.
		if ((resolved.underline || resolved.strikeOut) && width > 0)
		{
			const int thickness = std::max(1, (resolved.ppem + 8) / 14);
			if (resolved.underline)
			{
				const int lineTop = baseline + std::max(1, (resolved.ppem + 5) / 10);
				RECT line = { std::max<LONG>(x, clip.left), std::max<LONG>(lineTop, clip.top), std::min<LONG>(x + width, clip.right), std::min<LONG>(lineTop + thickness, clip.bottom) };
				FillPixels(bitmap, line, dc->textColor);
			}
			if (resolved.strikeOut)
			{
				const int lineTop = baseline - (resolved.ppem * 3 + 5) / 10;
				RECT line = { std::max<LONG>(x, clip.left), std::max<LONG>(lineTop, clip.top), std::min<LONG>(x + width, clip.right), std::min<LONG>(lineTop + thickness, clip.bottom) };
				FillPixels(bitmap, line, dc->textColor);
			}
		}
	}

	if (dc->textAlign & TA_UPDATECP)
		dc->positionX = x + width;
	return TRUE;
}

template <class Char>
BOOL ExtTextOutImpl(HDC hdc, int x, int y, UINT options, const RECT *rect, const Char *string, UINT c, const INT *dx)
{
	Text text;
	if (string)
		ToText(string, (int)c, &text);
	return DrawTextRun(hdc, x, y, options, rect, text, dx);
}

// DrawText: lines split at '\n', optional word breaks, alignment inside the rectangle.
template <class Char>
int DrawTextImpl(HDC hdc, const Char *string, int count, LPRECT rect, UINT format)
{
	DcObject *dc = As<DcObject>(hdc, GDI_DC);
	if (!dc || !string || !rect)
		return 0;
	FontObject *font = ActiveFont(dc);
	const Gdi::ScaledFont &resolved = Resolved(font);
	Text all;
	ToText(string, CountOf(string, count), &all);

	// Prefix characters: "&&" is an ampersand, a single '&' marks a mnemonic and is dropped.
	Text clean;
	for (size_t i = 0; i < all.codePoints.size(); ++i)
	{
		if (!(format & DT_NOPREFIX) && all.codePoints[i] == '&')
		{
			if (i + 1 < all.codePoints.size() && all.codePoints[i + 1] == '&')
				++i;
			else
				continue;
		}
		clean.codePoints.push_back(all.codePoints[i]);
		clean.unitIndex.push_back((int)clean.codePoints.size() - 1);
	}

	const int maxWidth = rect->right - rect->left;
	std::vector<Text> lines(1);
	std::vector<int> lineWidths(1, 0);
	size_t lastBreak = (size_t)-1; // index in the current line after the last space
	for (size_t i = 0; i < clean.codePoints.size(); ++i)
	{
		const uint32_t c = clean.codePoints[i];
		if (!(format & DT_SINGLELINE) && (c == '\n' || c == '\r'))
		{
			if (c == '\r' && i + 1 < clean.codePoints.size() && clean.codePoints[i + 1] == '\n')
				++i;
			lines.emplace_back();
			lineWidths.push_back(0);
			lastBreak = (size_t)-1;
			continue;
		}
		if (c == '\t' || c == '\n' || c == '\r')
		{
			// A tab is eight average characters wide when DT_EXPANDTABS, else a space.
			const int advance = (format & DT_EXPANDTABS) && c == '\t' ? resolved.avgCharWidth * 8 : Gdi::GlyphAdvance(resolved, ' ');
			lineWidths.back() += advance;
			lines.back().codePoints.push_back(' ');
			lines.back().unitIndex.push_back((int)lines.back().codePoints.size() - 1);
			continue;
		}
		const int advance = Gdi::GlyphAdvance(resolved, c);
		if ((format & DT_WORDBREAK) && !(format & DT_SINGLELINE) && lineWidths.back() + advance > maxWidth && !lines.back().codePoints.empty())
		{
			// Break at the last space if there is one, else here.
			Text carried;
			int carriedWidth = 0;
			if (lastBreak != (size_t)-1 && lastBreak < lines.back().codePoints.size())
			{
				for (size_t k = lastBreak; k < lines.back().codePoints.size(); ++k)
				{
					carried.codePoints.push_back(lines.back().codePoints[k]);
					carried.unitIndex.push_back((int)carried.codePoints.size() - 1);
					carriedWidth += Gdi::GlyphAdvance(resolved, lines.back().codePoints[k]);
				}
				lines.back().codePoints.resize(lastBreak);
				lines.back().unitIndex.resize(lastBreak);
				lineWidths.back() -= carriedWidth;
			}
			// Trailing space is not part of the line.
			while (!lines.back().codePoints.empty() && lines.back().codePoints.back() == ' ')
			{
				lineWidths.back() -= Gdi::GlyphAdvance(resolved, ' ');
				lines.back().codePoints.pop_back();
				lines.back().unitIndex.pop_back();
			}
			lines.push_back(carried);
			lineWidths.push_back(carriedWidth);
			lastBreak = (size_t)-1;
		}
		lines.back().codePoints.push_back(c);
		lines.back().unitIndex.push_back((int)lines.back().codePoints.size() - 1);
		lineWidths.back() += advance;
		if (c == ' ')
			lastBreak = lines.back().codePoints.size();
	}

	const int textHeight = (int)lines.size() * resolved.height;
	if (format & DT_CALCRECT)
	{
		int widest = 0;
		for (size_t i = 0; i < lineWidths.size(); ++i)
			widest = std::max(widest, lineWidths[i]);
		if (!(format & DT_WORDBREAK) || widest < maxWidth)
			rect->right = rect->left + widest;
		rect->bottom = rect->top + textHeight;
		return textHeight;
	}

	int top = rect->top;
	if ((format & DT_SINGLELINE) && (format & DT_VCENTER))
		top = rect->top + ((rect->bottom - rect->top) - textHeight) / 2;
	else if ((format & DT_SINGLELINE) && (format & DT_BOTTOM))
		top = rect->bottom - textHeight;

	const UINT savedAlign = dc->textAlign;
	dc->textAlign = TA_LEFT | TA_TOP;
	const int savedMode = dc->bkMode;
	for (size_t i = 0; i < lines.size(); ++i)
	{
		int x = rect->left;
		if ((format & 3) == DT_CENTER)
			x = rect->left + (maxWidth - lineWidths[i]) / 2;
		else if ((format & 3) == DT_RIGHT)
			x = rect->right - lineWidths[i];
		// DrawText never paints a background for text drawn in transparent mode; in opaque mode it
		// does, like TextOut.
		UINT options = (format & DT_NOCLIP) ? 0 : ETO_CLIPPED;
		DrawTextRun(hdc, x, top + (int)i * resolved.height, options, rect, lines[i], nullptr);
	}
	dc->textAlign = savedAlign;
	dc->bkMode = savedMode;
	return textHeight;
}

// Char widths / ABC widths over a code point range.
template <class Fn>
BOOL ForEachChar(HDC hdc, UINT first, UINT last, Fn fn)
{
	DcObject *dc = As<DcObject>(hdc, GDI_DC);
	if (!dc || first > last || last - first > 0xFFFF)
		return FALSE;
	const Gdi::ScaledFont &resolved = Resolved(ActiveFont(dc));
	for (UINT c = first; c <= last; ++c)
		fn(resolved, c, c - first);
	return TRUE;
}

// ---------------------------------------------------------------------------
// Font files
// ---------------------------------------------------------------------------

// Registers a font file the game lists in its language INI. The path is a Windows path
// relative to the game directory.
int AddFontPath(const std::string &path)
{
	if (path.empty())
		return 0;
	char resolved[4096];
	std::string key = path;
	for (size_t i = 0; i < key.size(); ++i)
	{
		if (key[i] == '\\')
			key[i] = '/';
	}
	bool exists = ResolvePath(path.c_str(), resolved, sizeof(resolved));
	if (!exists && key.find('/') == std::string::npos)
	{
		// A bare file name: GDI looks in the Fonts folder, here the bundled fonts.
		const char *directory = getenv("WEBCOMPAT_FONT_DIR");
		const std::string candidate = std::string(directory && *directory ? directory : "/webfonts") + "/" + key;
		exists = ResolvePath(candidate.c_str(), resolved, sizeof(resolved));
	}
	if (!exists)
		return 0;
	return Gdi::AddFontFile(key, resolved);
}

bool RemoveFontPath(const std::string &path)
{
	std::string key = path;
	for (size_t i = 0; i < key.size(); ++i)
	{
		if (key[i] == '\\')
			key[i] = '/';
	}
	return Gdi::RemoveFontFile(key);
}

void AppendUtf8(std::string *s, uint32_t c)
{
	unsigned char buffer[4];
	const size_t n = EncodeUtf8(c, buffer);
	s->append((const char *)buffer, n);
}

// The ANSI file names of the A functions are Windows-1252; the file system wants UTF-8.
std::string AnsiToUtf8(LPCSTR s)
{
	std::string result;
	for (; *s; ++s)
		AppendUtf8(&result, Cp1252ToUtf16((unsigned char)*s));
	return result;
}

std::string WideToUtf8(LPCWSTR s)
{
	std::string result;
	for (size_t i = 0; s && s[i]; ++i)
	{
		uint32_t c = (uint16_t)s[i];
		if (IsHighSurrogate(c) && IsLowSurrogate((uint16_t)s[i + 1]))
		{
			c = 0x10000 + ((c - 0xD800) << 10) + ((uint16_t)s[i + 1] - 0xDC00);
			++i;
		}
		AppendUtf8(&result, c);
	}
	return result;
}

void ReleaseSelection(GdiObject *object)
{
	if (object && !object->stock && object->selectCount > 0)
		--object->selectCount;
}

} // namespace

// Cursors, icons and images are opaque tokens.
namespace WebCompat
{
HANDLE NewStockHandle()
{
	return reinterpret_cast<HANDLE>(new GdiObject(GDI_STOCK));
}
} // namespace WebCompat

extern "C" {

/* ---------------------------------------------------------------------------
** Device contexts and objects
** ------------------------------------------------------------------------- */

HDC WINAPI GetDC(HWND)
{
	// The screen: nothing can be drawn into it.
	DcObject *dc = new DcObject();
	dc->bitmap = nullptr;
	return reinterpret_cast<HDC>(dc);
}

int WINAPI ReleaseDC(HWND, HDC hDC)
{
	DcObject *dc = As<DcObject>(hDC, GDI_DC);
	if (!dc)
		return 0;
	delete dc;
	return 1;
}

HDC WINAPI CreateCompatibleDC(HDC)
{
	DcObject *dc = new DcObject();
	dc->bitmap = &Stock().defaultBitmap;
	return reinterpret_cast<HDC>(dc);
}

BOOL WINAPI DeleteDC(HDC hdc)
{
	DcObject *dc = As<DcObject>(hdc, GDI_DC);
	if (!dc)
		return FALSE;
	// The objects selected into the DC can be deleted from now on.
	GdiObject *selected[3] = { dc->bitmap, dc->font, dc->brush };
	delete dc;
	for (int i = 0; i < 3; ++i)
		ReleaseSelection(selected[i]);
	return TRUE;
}

HGDIOBJ WINAPI SelectObject(HDC hdc, HGDIOBJ h)
{
	DcObject *dc = As<DcObject>(hdc, GDI_DC);
	GdiObject *object = reinterpret_cast<GdiObject *>(h);
	if (!dc || !object || object->magic != GdiObject::MAGIC)
		return nullptr;
	switch (object->kind)
	{
	case GDI_BITMAP:
	{
		BitmapObject *previous = dc->bitmap;
		dc->bitmap = static_cast<BitmapObject *>(object);
		++object->selectCount;
		HGDIOBJ result = previous ? previous : &Stock().defaultBitmap;
		ReleaseSelection(previous);
		return result;
	}
	case GDI_FONT:
	{
		FontObject *previous = dc->font;
		dc->font = static_cast<FontObject *>(object);
		++object->selectCount;
		HGDIOBJ result = previous ? previous : DefaultFont();
		ReleaseSelection(previous);
		return result;
	}
	case GDI_BRUSH:
	{
		BrushObject *previous = dc->brush;
		dc->brush = static_cast<BrushObject *>(object);
		++object->selectCount;
		HGDIOBJ result = previous ? previous : &Stock().brushes[WHITE_BRUSH];
		ReleaseSelection(previous);
		return result;
	}
	default:
		return h; // pens and the like: accepted, ignored
	}
}

BOOL WINAPI DeleteObject(HGDIOBJ ho)
{
	GdiObject *object = reinterpret_cast<GdiObject *>(ho);
	if (!object || object->magic != GdiObject::MAGIC || object->kind == GDI_DC)
		return FALSE;
	if (object->stock)
		return TRUE; // shared stock objects stay
	if (object->selectCount > 0)
		return FALSE; // Windows refuses to delete an object that is selected into a DC
	delete object;
	return TRUE;
}

HGDIOBJ WINAPI GetStockObject(int i)
{
	StockObjects &stock = Stock();
	if (i >= WHITE_BRUSH && i <= NULL_BRUSH)
		return &stock.brushes[i];
	if (i >= OEM_FIXED_FONT && i <= DEFAULT_GUI_FONT && i != 15)
		return &stock.fonts[i - OEM_FIXED_FONT];
	if (i >= 0 && i < 32)
		return &stock.other; // pens, DC_BRUSH, DC_PEN and the default palette: accepted and ignored
	return nullptr;
}

int WINAPI GetObjectA(HANDLE h, int c, LPVOID pv)
{
	GdiObject *object = reinterpret_cast<GdiObject *>(h);
	if (!object || object->magic != GdiObject::MAGIC || !pv)
		return 0;
	if (object->kind == GDI_BITMAP && c >= (int)sizeof(BITMAP))
	{
		const BitmapObject *bitmapObject = static_cast<BitmapObject *>(object);
		BITMAP *bitmap = static_cast<BITMAP *>(pv);
		memset(bitmap, 0, sizeof(*bitmap));
		bitmap->bmWidth = bitmapObject->width;
		bitmap->bmHeight = bitmapObject->height;
		bitmap->bmPlanes = 1;
		bitmap->bmBitsPixel = (WORD)bitmapObject->bitsPerPixel;
		bitmap->bmWidthBytes = (LONG)bitmapObject->stride;
		bitmap->bmBits = bitmapObject->bits;
		return (int)sizeof(BITMAP);
	}
	if (object->kind == GDI_FONT && c >= (int)sizeof(LOGFONTA))
	{
		const FontObject *fontObject = static_cast<FontObject *>(object);
		LOGFONTA *lf = static_cast<LOGFONTA *>(pv);
		memset(lf, 0, sizeof(*lf));
		lf->lfHeight = fontObject->lf.lfHeight;
		lf->lfWidth = fontObject->lf.lfWidth;
		lf->lfEscapement = fontObject->lf.lfEscapement;
		lf->lfOrientation = fontObject->lf.lfOrientation;
		lf->lfWeight = fontObject->lf.lfWeight;
		lf->lfItalic = fontObject->lf.lfItalic;
		lf->lfUnderline = fontObject->lf.lfUnderline;
		lf->lfStrikeOut = fontObject->lf.lfStrikeOut;
		lf->lfCharSet = fontObject->lf.lfCharSet;
		lf->lfOutPrecision = fontObject->lf.lfOutPrecision;
		lf->lfClipPrecision = fontObject->lf.lfClipPrecision;
		lf->lfQuality = fontObject->lf.lfQuality;
		lf->lfPitchAndFamily = fontObject->lf.lfPitchAndFamily;
		WideToAnsi(fontObject->lf.lfFaceName, lf->lfFaceName, sizeof(lf->lfFaceName));
		return (int)sizeof(LOGFONTA);
	}
	return 0;
}

/* ---------------------------------------------------------------------------
** Fonts
** ------------------------------------------------------------------------- */

HFONT WINAPI CreateFontW(int cHeight, int cWidth, int cEscapement, int cOrientation, int cWeight, DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality, DWORD iPitchAndFamily, LPCWSTR pszFaceName)
{
	LOGFONTW lf = {};
	lf.lfHeight = cHeight;
	lf.lfWidth = cWidth;
	lf.lfEscapement = cEscapement;
	lf.lfOrientation = cOrientation;
	lf.lfWeight = cWeight;
	lf.lfItalic = (BYTE)bItalic;
	lf.lfUnderline = (BYTE)bUnderline;
	lf.lfStrikeOut = (BYTE)bStrikeOut;
	lf.lfCharSet = (BYTE)iCharSet;
	lf.lfOutPrecision = (BYTE)iOutPrecision;
	lf.lfClipPrecision = (BYTE)iClipPrecision;
	lf.lfQuality = (BYTE)iQuality;
	lf.lfPitchAndFamily = (BYTE)iPitchAndFamily;
	for (int i = 0; pszFaceName && pszFaceName[i] && i < LF_FACESIZE - 1; ++i)
		lf.lfFaceName[i] = pszFaceName[i];
	return MakeFont(lf);
}

HFONT WINAPI CreateFontA(int cHeight, int cWidth, int cEscapement, int cOrientation, int cWeight, DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality, DWORD iPitchAndFamily, LPCSTR pszFaceName)
{
	WCHAR name[LF_FACESIZE];
	AnsiToWide(pszFaceName, name, LF_FACESIZE);
	return CreateFontW(cHeight, cWidth, cEscapement, cOrientation, cWeight, bItalic, bUnderline, bStrikeOut, iCharSet, iOutPrecision, iClipPrecision, iQuality, iPitchAndFamily, name);
}

HFONT WINAPI CreateFontIndirectW(const LOGFONTW *lplf)
{
	if (!lplf)
		return nullptr;
	return MakeFont(*lplf);
}

HFONT WINAPI CreateFontIndirectA(const LOGFONTA *lplf)
{
	if (!lplf)
		return nullptr;
	LOGFONTW lf = {};
	lf.lfHeight = lplf->lfHeight;
	lf.lfWidth = lplf->lfWidth;
	lf.lfEscapement = lplf->lfEscapement;
	lf.lfOrientation = lplf->lfOrientation;
	lf.lfWeight = lplf->lfWeight;
	lf.lfItalic = lplf->lfItalic;
	lf.lfUnderline = lplf->lfUnderline;
	lf.lfStrikeOut = lplf->lfStrikeOut;
	lf.lfCharSet = lplf->lfCharSet;
	lf.lfOutPrecision = lplf->lfOutPrecision;
	lf.lfClipPrecision = lplf->lfClipPrecision;
	lf.lfQuality = lplf->lfQuality;
	lf.lfPitchAndFamily = lplf->lfPitchAndFamily;
	char name[LF_FACESIZE];
	memcpy(name, lplf->lfFaceName, LF_FACESIZE);
	name[LF_FACESIZE - 1] = 0;
	AnsiToWide(name, lf.lfFaceName, LF_FACESIZE);
	return MakeFont(lf);
}

int WINAPI AddFontResourceA(LPCSTR lpFileName)
{
	if (!lpFileName)
		return 0;
	return AddFontPath(AnsiToUtf8(lpFileName));
}

int WINAPI AddFontResourceW(LPCWSTR lpFileName)
{
	return lpFileName ? AddFontPath(WideToUtf8(lpFileName)) : 0;
}

int WINAPI AddFontResourceExA(LPCSTR name, DWORD, PVOID)
{
	return AddFontResourceA(name);
}

int WINAPI AddFontResourceExW(LPCWSTR name, DWORD, PVOID)
{
	return AddFontResourceW(name);
}

BOOL WINAPI RemoveFontResourceA(LPCSTR lpFileName)
{
	if (!lpFileName)
		return FALSE;
	return RemoveFontPath(AnsiToUtf8(lpFileName));
}

BOOL WINAPI RemoveFontResourceW(LPCWSTR lpFileName)
{
	return lpFileName ? RemoveFontPath(WideToUtf8(lpFileName)) : FALSE;
}

BOOL WINAPI RemoveFontResourceExA(LPCSTR name, DWORD, PVOID)
{
	return RemoveFontResourceA(name);
}

BOOL WINAPI RemoveFontResourceExW(LPCWSTR name, DWORD, PVOID)
{
	return RemoveFontResourceW(name);
}

/* ---------------------------------------------------------------------------
** Bitmaps and brushes
** ------------------------------------------------------------------------- */

HBITMAP WINAPI CreateDIBSection(HDC, const BITMAPINFO *pbmi, UINT, void **ppvBits, HANDLE, DWORD)
{
	if (ppvBits)
		*ppvBits = nullptr;
	if (!pbmi)
		return nullptr;
	const BITMAPINFOHEADER &header = pbmi->bmiHeader;
	const int width = header.biWidth;
	const bool topDown = header.biHeight < 0;
	const int height = topDown ? -header.biHeight : header.biHeight;
	const int bitsPerPixel = header.biBitCount ? header.biBitCount : 32;
	if (width <= 0 || height <= 0)
		return nullptr;
	if (bitsPerPixel != 1 && bitsPerPixel != 4 && bitsPerPixel != 8 && bitsPerPixel != 16 && bitsPerPixel != 24 && bitsPerPixel != 32)
		return nullptr;
	// Rows are padded to 32 bits.
	const size_t stride = (((size_t)width * bitsPerPixel + 31) / 32) * 4;
	if (stride * (size_t)height > (size_t)256 * 1024 * 1024)
		return nullptr;

	BitmapObject *bitmap = new BitmapObject();
	bitmap->width = width;
	bitmap->height = height;
	bitmap->bitsPerPixel = bitsPerPixel;
	bitmap->stride = stride;
	bitmap->topDown = topDown;
	bitmap->bits = static_cast<uint8_t *>(calloc(1, stride * (size_t)height + 1));
	if (!bitmap->bits)
	{
		delete bitmap;
		return nullptr;
	}
	if (bitsPerPixel == 16 && header.biCompression == BI_BITFIELDS)
	{
		const DWORD *masks = reinterpret_cast<const DWORD *>(pbmi->bmiColors);
		bitmap->rgb565 = masks[1] == 0x07E0;
	}
	if (bitsPerPixel <= 8)
	{
		const int entries = 1 << bitsPerPixel;
		const int provided = header.biClrUsed ? (int)std::min<DWORD>(header.biClrUsed, entries) : entries;
		if (header.biSize == sizeof(BITMAPINFOHEADER) && header.biClrUsed != 0)
		{
			for (int i = 0; i < provided; ++i)
				bitmap->palette.push_back(Rgb(pbmi->bmiColors[i].rgbRed, pbmi->bmiColors[i].rgbGreen, pbmi->bmiColors[i].rgbBlue));
		}
		else if (bitsPerPixel == 1)
			bitmap->palette = { RGB(0, 0, 0), RGB(255, 255, 255) };
		else
		{
			// No colour table given: a grey ramp, so that black and white text keep their meaning.
			for (int i = 0; i < entries; ++i)
			{
				const int level = i * 255 / (entries - 1);
				bitmap->palette.push_back(Rgb(level, level, level));
			}
		}
	}
	if (ppvBits)
		*ppvBits = bitmap->bits;
	return reinterpret_cast<HBITMAP>(bitmap);
}

HBITMAP WINAPI CreateCompatibleBitmap(HDC, int cx, int cy)
{
	if (cx <= 0 || cy <= 0 || (size_t)cx * (size_t)cy > (size_t)64 * 1024 * 1024)
		return nullptr;
	BitmapObject *bitmap = new BitmapObject();
	bitmap->width = cx;
	bitmap->height = cy;
	bitmap->bitsPerPixel = 32;
	bitmap->stride = (size_t)cx * 4;
	bitmap->topDown = true;
	bitmap->bits = static_cast<uint8_t *>(calloc(1, bitmap->stride * (size_t)cy + 1));
	if (!bitmap->bits)
	{
		delete bitmap;
		return nullptr;
	}
	return reinterpret_cast<HBITMAP>(bitmap);
}

HBRUSH WINAPI CreateSolidBrush(COLORREF color)
{
	BrushObject *brush = new BrushObject();
	brush->color = color & 0x00FFFFFF;
	return reinterpret_cast<HBRUSH>(brush);
}

int WINAPI FillRect(HDC hdc, const RECT *lprc, HBRUSH hbr)
{
	DcObject *dc = As<DcObject>(hdc, GDI_DC);
	if (!dc || !lprc)
		return 0;
	if (!dc->bitmap || !dc->bitmap->bits)
		return 1;
	COLORREF color;
	BrushObject *brush = As<BrushObject>(hbr, GDI_BRUSH);
	if (brush)
	{
		if (brush->hollow)
			return 1;
		color = brush->color;
	}
	else if ((uintptr_t)hbr < 64)
		color = RGB(255, 255, 255); // a COLOR_xxx + 1 value: a light system colour
	else
		return 0;
	FillPixels(dc->bitmap, *lprc, color);
	return 1;
}

BOOL WINAPI BitBlt(HDC hdc, int x, int y, int cx, int cy, HDC hdcSrc, int x1, int y1, DWORD rop)
{
	DcObject *dc = As<DcObject>(hdc, GDI_DC);
	if (!dc)
		return FALSE;
	BitmapObject *destination = dc->bitmap;
	if (!destination || !destination->bits)
		return TRUE;
	RECT rect = { x, y, x + cx, y + cy };
	switch (rop)
	{
	case BLACKNESS:
		FillPixels(destination, rect, RGB(0, 0, 0));
		return TRUE;
	case WHITENESS:
		FillPixels(destination, rect, RGB(255, 255, 255));
		return TRUE;
	case PATCOPY:
		if (dc->brush && !dc->brush->hollow)
			FillPixels(destination, rect, dc->brush->color);
		return TRUE;
	case SRCCOPY:
		break;
	default:
		return FALSE;
	}
	DcObject *sourceDc = As<DcObject>(hdcSrc, GDI_DC);
	BitmapObject *source = sourceDc ? sourceDc->bitmap : nullptr;
	if (!source || !source->bits)
		return TRUE;
	for (int row = 0; row < cy; ++row)
	{
		const int dy = y + row, sy = y1 + row;
		if (dy < 0 || dy >= destination->height || sy < 0 || sy >= source->height)
			continue;
		for (int col = 0; col < cx; ++col)
		{
			const int dx = x + col, sx = x1 + col;
			if (dx < 0 || dx >= destination->width || sx < 0 || sx >= source->width)
				continue;
			SetPixelValue(destination, dx, dy, GetPixelValue(source, sx, sy));
		}
	}
	return TRUE;
}

COLORREF WINAPI GetPixel(HDC hdc, int x, int y)
{
	DcObject *dc = As<DcObject>(hdc, GDI_DC);
	if (!dc || !dc->bitmap || !dc->bitmap->bits || x < 0 || y < 0 || x >= dc->bitmap->width || y >= dc->bitmap->height)
		return CLR_INVALID;
	return GetPixelValue(dc->bitmap, x, y);
}

COLORREF WINAPI SetPixel(HDC hdc, int x, int y, COLORREF color)
{
	DcObject *dc = As<DcObject>(hdc, GDI_DC);
	if (!dc || !dc->bitmap || !dc->bitmap->bits || x < 0 || y < 0 || x >= dc->bitmap->width || y >= dc->bitmap->height)
		return (COLORREF)-1;
	SetPixelValue(dc->bitmap, x, y, color);
	return color;
}

/* ---------------------------------------------------------------------------
** Text state
** ------------------------------------------------------------------------- */

COLORREF WINAPI SetTextColor(HDC hdc, COLORREF color)
{
	DcObject *dc = As<DcObject>(hdc, GDI_DC);
	if (!dc)
		return CLR_INVALID;
	const COLORREF previous = dc->textColor;
	dc->textColor = color & 0x00FFFFFF;
	return previous;
}

COLORREF WINAPI GetTextColor(HDC hdc)
{
	DcObject *dc = As<DcObject>(hdc, GDI_DC);
	return dc ? dc->textColor : CLR_INVALID;
}

COLORREF WINAPI SetBkColor(HDC hdc, COLORREF color)
{
	DcObject *dc = As<DcObject>(hdc, GDI_DC);
	if (!dc)
		return CLR_INVALID;
	const COLORREF previous = dc->bkColor;
	dc->bkColor = color & 0x00FFFFFF;
	return previous;
}

COLORREF WINAPI GetBkColor(HDC hdc)
{
	DcObject *dc = As<DcObject>(hdc, GDI_DC);
	return dc ? dc->bkColor : CLR_INVALID;
}

int WINAPI SetBkMode(HDC hdc, int mode)
{
	DcObject *dc = As<DcObject>(hdc, GDI_DC);
	if (!dc || (mode != TRANSPARENT && mode != OPAQUE))
		return 0;
	const int previous = dc->bkMode;
	dc->bkMode = mode;
	return previous;
}

int WINAPI GetBkMode(HDC hdc)
{
	DcObject *dc = As<DcObject>(hdc, GDI_DC);
	return dc ? dc->bkMode : 0;
}

UINT WINAPI SetTextAlign(HDC hdc, UINT align)
{
	DcObject *dc = As<DcObject>(hdc, GDI_DC);
	if (!dc)
		return GDI_ERROR;
	const UINT previous = dc->textAlign;
	dc->textAlign = align;
	return previous;
}

UINT WINAPI GetTextAlign(HDC hdc)
{
	DcObject *dc = As<DcObject>(hdc, GDI_DC);
	return dc ? dc->textAlign : GDI_ERROR;
}

/* ---------------------------------------------------------------------------
** Text output and measurement
** ------------------------------------------------------------------------- */

BOOL WINAPI ExtTextOutW(HDC hdc, int x, int y, UINT options, const RECT *lprect, LPCWSTR lpString, UINT c, const INT *lpDx)
{
	return ExtTextOutImpl(hdc, x, y, options, lprect, lpString, c, lpDx);
}

BOOL WINAPI ExtTextOutA(HDC hdc, int x, int y, UINT options, const RECT *lprect, LPCSTR lpString, UINT c, const INT *lpDx)
{
	return ExtTextOutImpl(hdc, x, y, options, lprect, lpString, c, lpDx);
}

BOOL WINAPI TextOutW(HDC hdc, int x, int y, LPCWSTR lpString, int c)
{
	return c < 0 ? FALSE : ExtTextOutImpl(hdc, x, y, 0, (const RECT *)nullptr, lpString, (UINT)c, (const INT *)nullptr);
}

BOOL WINAPI TextOutA(HDC hdc, int x, int y, LPCSTR lpString, int c)
{
	return c < 0 ? FALSE : ExtTextOutImpl(hdc, x, y, 0, (const RECT *)nullptr, lpString, (UINT)c, (const INT *)nullptr);
}

int WINAPI DrawTextA(HDC hdc, LPCSTR lpchText, int cchText, LPRECT lprc, UINT format)
{
	return DrawTextImpl(hdc, lpchText, cchText, lprc, format);
}

int WINAPI DrawTextW(HDC hdc, LPCWSTR lpchText, int cchText, LPRECT lprc, UINT format)
{
	return DrawTextImpl(hdc, lpchText, cchText, lprc, format);
}

BOOL WINAPI GetTextExtentPoint32A(HDC hdc, LPCSTR lpString, int c, LPSIZE psizl)
{
	if (!lpString || !As<DcObject>(hdc, GDI_DC))
		return FALSE;
	Text text;
	ToText(lpString, CountOf(lpString, c), &text);
	return TextExtent(hdc, text, psizl);
}

BOOL WINAPI GetTextExtentPoint32W(HDC hdc, LPCWSTR lpString, int c, LPSIZE psizl)
{
	if (!lpString || !As<DcObject>(hdc, GDI_DC))
		return FALSE;
	Text text;
	ToText(lpString, CountOf(lpString, c), &text);
	return TextExtent(hdc, text, psizl);
}

BOOL WINAPI GetTextMetricsW(HDC hdc, LPTEXTMETRICW lptm)
{
	DcObject *dc = As<DcObject>(hdc, GDI_DC);
	if (!dc || !lptm)
		return FALSE;
	const Gdi::ScaledFont &font = Resolved(ActiveFont(dc));
	const LOGFONTW &lf = ActiveFont(dc)->lf;
	memset(lptm, 0, sizeof(*lptm));
	lptm->tmHeight = font.height;
	lptm->tmAscent = font.ascent;
	lptm->tmDescent = font.descent;
	lptm->tmInternalLeading = font.internalLeading;
	lptm->tmExternalLeading = font.externalLeading;
	lptm->tmAveCharWidth = font.avgCharWidth;
	lptm->tmMaxCharWidth = font.maxCharWidth;
	lptm->tmWeight = font.weight;
	lptm->tmOverhang = font.overhang;
	lptm->tmDigitizedAspectX = 96;
	lptm->tmDigitizedAspectY = 96;
	lptm->tmFirstChar = (WCHAR)font.firstChar;
	lptm->tmLastChar = (WCHAR)font.lastChar;
	lptm->tmDefaultChar = (WCHAR)font.defaultChar;
	lptm->tmBreakChar = (WCHAR)font.breakChar;
	lptm->tmItalic = lf.lfItalic ? 255 : 0;
	lptm->tmUnderlined = lf.lfUnderline ? 255 : 0;
	lptm->tmStruckOut = lf.lfStrikeOut ? 255 : 0;
	lptm->tmPitchAndFamily = font.pitchAndFamily;
	lptm->tmCharSet = font.charSet;
	return TRUE;
}

BOOL WINAPI GetTextMetricsA(HDC hdc, LPTEXTMETRIC lptm)
{
	if (!lptm)
		return FALSE;
	TEXTMETRICW wide;
	if (!GetTextMetricsW(hdc, &wide))
		return FALSE;
	lptm->tmHeight = wide.tmHeight;
	lptm->tmAscent = wide.tmAscent;
	lptm->tmDescent = wide.tmDescent;
	lptm->tmInternalLeading = wide.tmInternalLeading;
	lptm->tmExternalLeading = wide.tmExternalLeading;
	lptm->tmAveCharWidth = wide.tmAveCharWidth;
	lptm->tmMaxCharWidth = wide.tmMaxCharWidth;
	lptm->tmWeight = wide.tmWeight;
	lptm->tmOverhang = wide.tmOverhang;
	lptm->tmDigitizedAspectX = wide.tmDigitizedAspectX;
	lptm->tmDigitizedAspectY = wide.tmDigitizedAspectY;
	lptm->tmFirstChar = (CHAR)std::min<int>(wide.tmFirstChar, 255);
	lptm->tmLastChar = (CHAR)std::min<int>(wide.tmLastChar, 255);
	lptm->tmDefaultChar = (CHAR)std::min<int>(wide.tmDefaultChar, 255);
	lptm->tmBreakChar = (CHAR)std::min<int>(wide.tmBreakChar, 255);
	lptm->tmItalic = wide.tmItalic;
	lptm->tmUnderlined = wide.tmUnderlined;
	lptm->tmStruckOut = wide.tmStruckOut;
	lptm->tmPitchAndFamily = wide.tmPitchAndFamily;
	lptm->tmCharSet = wide.tmCharSet;
	return TRUE;
}

BOOL WINAPI GetCharWidth32W(HDC hdc, UINT iFirst, UINT iLast, LPINT lpBuffer)
{
	if (!lpBuffer)
		return FALSE;
	return ForEachChar(hdc, iFirst, iLast, [lpBuffer](const Gdi::ScaledFont &font, UINT c, UINT index) {
		lpBuffer[index] = Gdi::GlyphAdvance(font, c);
	});
}

BOOL WINAPI GetCharWidth32A(HDC hdc, UINT iFirst, UINT iLast, LPINT lpBuffer)
{
	if (!lpBuffer || iFirst > iLast || iLast > 255)
		return FALSE;
	return ForEachChar(hdc, iFirst, iLast, [lpBuffer](const Gdi::ScaledFont &font, UINT c, UINT index) {
		lpBuffer[index] = Gdi::GlyphAdvance(font, Cp1252ToUtf16((unsigned char)c));
	});
}

BOOL WINAPI GetCharABCWidthsW(HDC hdc, UINT wFirst, UINT wLast, LPABC lpABC)
{
	if (!lpABC)
		return FALSE;
	return ForEachChar(hdc, wFirst, wLast, [lpABC](const Gdi::ScaledFont &font, UINT c, UINT index) {
		Gdi::GlyphAbc abc;
		Gdi::GlyphMetrics(font, c, &abc);
		lpABC[index].abcA = abc.a;
		lpABC[index].abcB = (UINT)abc.b;
		lpABC[index].abcC = abc.c;
	});
}

BOOL WINAPI GetCharABCWidthsA(HDC hdc, UINT wFirst, UINT wLast, LPABC lpABC)
{
	if (!lpABC || wFirst > wLast || wLast > 255)
		return FALSE;
	return ForEachChar(hdc, wFirst, wLast, [lpABC](const Gdi::ScaledFont &font, UINT c, UINT index) {
		Gdi::GlyphAbc abc;
		Gdi::GlyphMetrics(font, Cp1252ToUtf16((unsigned char)c), &abc);
		lpABC[index].abcA = abc.a;
		lpABC[index].abcB = (UINT)abc.b;
		lpABC[index].abcC = abc.c;
	});
}

int WINAPI GetTextFaceW(HDC hdc, int c, LPWSTR lpName)
{
	DcObject *dc = As<DcObject>(hdc, GDI_DC);
	if (!dc)
		return 0;
	const Gdi::ScaledFont &font = Resolved(ActiveFont(dc));
	int length = 0;
	while (font.faceName[length])
		++length;
	if (lpName && c > 0)
	{
		const int copy = std::min(length, c - 1);
		memcpy(lpName, font.faceName, copy * sizeof(WCHAR));
		lpName[copy] = 0;
		return copy + 1;
	}
	return length + 1;
}

int WINAPI GetTextFaceA(HDC hdc, int c, LPSTR lpName)
{
	WCHAR wide[LF_FACESIZE];
	if (!GetTextFaceW(hdc, LF_FACESIZE, wide))
		return 0;
	char narrow[LF_FACESIZE];
	WideToAnsi(wide, narrow, sizeof(narrow));
	const int length = (int)strlen(narrow);
	if (lpName && c > 0)
	{
		const int copy = std::min(length, c - 1);
		memcpy(lpName, narrow, copy);
		lpName[copy] = 0;
		return copy + 1;
	}
	return length + 1;
}

} // extern "C"
