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
** WebAssembly port: tests of the GDI text emulation (Dependencies/WebCompat/src/win32_gdi.cpp and
** gdi_font.cpp), part of the WebCompat node test. They drive the same call sequence as the game's
** FontCharsClass (WW3D2 render2dsentence.cpp) and check metrics, pixels, the A and W variants, font
** registration and the face name substitution.
*/
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <string>
#include <vector>

#include "check.h"

namespace
{

// The set-up of FontCharsClass::Create_GDI_Font: a font, a top-down 24 bit DIB of 2*point size and a memory DC.
struct GameFont
{
	HDC dc = nullptr;
	HFONT font = nullptr;
	HBITMAP bitmap = nullptr;
	HFONT oldFont = nullptr;
	HBITMAP oldBitmap = nullptr;
	uint8_t *bits = nullptr;
	int size = 0;
	int stride = 0;
	TEXTMETRIC tm = {};

	GameFont(const char *face, int pointSize, bool bold, int width = 0, int quality = ANTIALIASED_QUALITY, int bitCount = 24, bool topDown = true)
	{
		size = pointSize * 2;
		const int height = -MulDiv(pointSize, 96, 72);
		HDC screen = GetDC(nullptr);
		font = CreateFont(height, width, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
			OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, quality, VARIABLE_PITCH, face);
		BITMAPINFOHEADER header = {};
		header.biSize = sizeof(header);
		header.biWidth = size;
		header.biHeight = topDown ? -size : size;
		header.biPlanes = 1;
		header.biBitCount = (WORD)bitCount;
		header.biCompression = BI_RGB;
		bitmap = CreateDIBSection(screen, (const BITMAPINFO *)&header, DIB_RGB_COLORS, (void **)&bits, nullptr, 0);
		dc = CreateCompatibleDC(screen);
		ReleaseDC(nullptr, screen);
		oldBitmap = (HBITMAP)SelectObject(dc, bitmap);
		oldFont = (HFONT)SelectObject(dc, font);
		SetBkColor(dc, RGB(0, 0, 0));
		SetTextColor(dc, RGB(255, 255, 255));
		stride = ((size * bitCount / 8) + 3) & ~3;
		GetTextMetrics(dc, &tm);
	}

	~GameFont()
	{
		SelectObject(dc, oldFont);
		DeleteObject(font);
		SelectObject(dc, oldBitmap);
		DeleteObject(bitmap);
		DeleteDC(dc);
	}

	// Store_GDI_Char: clears the cell with ETO_OPAQUE and draws one character.
	SIZE Draw(WCHAR ch, int x = 0)
	{
		RECT rect = { 0, 0, size, size };
		ExtTextOutW(dc, x, 0, ETO_OPAQUE, &rect, &ch, 1, nullptr);
		SIZE extent = {};
		GetTextExtentPoint32W(dc, &ch, 1, &extent);
		return extent;
	}

	// The blue channel (the game reads only this one) of the top-down 24 bit bitmap.
	int Pixel(int x, int y) const { return bits[y * stride + x * 3]; }
	int LitPixels() const
	{
		int n = 0;
		for (int y = 0; y < size; ++y)
			for (int x = 0; x < size; ++x)
				n += Pixel(x, y) != 0;
		return n;
	}
	// Rows and columns of the bounding box of the lit pixels (right/bottom exclusive).
	void Bounds(int *left, int *top, int *right, int *bottom) const
	{
		*left = size; *top = size; *right = 0; *bottom = 0;
		for (int y = 0; y < size; ++y)
			for (int x = 0; x < size; ++x)
				if (Pixel(x, y))
				{
					if (x < *left) *left = x;
					if (y < *top) *top = y;
					if (x + 1 > *right) *right = x + 1;
					if (y + 1 > *bottom) *bottom = y + 1;
				}
	}
	std::vector<uint8_t> Snapshot() const { return std::vector<uint8_t>(bits, bits + stride * size); }
};

// A memory DC with a font made from a face name, for metric queries.
struct MetricsDc
{
	HDC dc;
	HFONT font;
	HGDIOBJ old;
	MetricsDc(const char *face, int height, int weight = FW_NORMAL, int width = 0, int italic = 0, BYTE pitch = VARIABLE_PITCH)
	{
		dc = CreateCompatibleDC(nullptr);
		font = CreateFont(height, width, 0, 0, weight, italic, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
			CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, pitch, face);
		old = SelectObject(dc, font);
	}
	~MetricsDc()
	{
		SelectObject(dc, old);
		DeleteObject(font);
		DeleteDC(dc);
	}
	int Width(char c) const
	{
		INT w = 0;
		GetCharWidth32A(dc, (UINT)(unsigned char)c, (UINT)(unsigned char)c, &w);
		return w;
	}
	int Extent(const char *text) const
	{
		SIZE s = {};
		GetTextExtentPoint32A(dc, text, (int)strlen(text), &s);
		return s.cx;
	}
	TEXTMETRIC Metrics() const
	{
		TEXTMETRIC tm = {};
		GetTextMetrics(dc, &tm);
		return tm;
	}
	std::string Face() const
	{
		char name[64] = {};
		GetTextFaceA(dc, sizeof(name), name);
		return name;
	}
};

void TestMetrics()
{
	// Liberation Sans 16 px (12 pt at 96 dpi): units per em 2048, usWinAscent 1854, usWinDescent 434, like Arial.
	MetricsDc arial("Arial", -16);
	TEXTMETRIC tm = arial.Metrics();
	CHECK(tm.tmAscent == 15 && tm.tmDescent == 4);        // 14.48 and 3.39 round up
	CHECK(tm.tmHeight == 19 && tm.tmInternalLeading == 3); // cell 19, em 16
	CHECK(tm.tmExternalLeading == 0);
	CHECK(tm.tmWeight == FW_NORMAL && tm.tmOverhang == 0);
	CHECK(tm.tmAveCharWidth == 7); // Arial: 904 units at 16 px = 7.06
	CHECK(tm.tmMaxCharWidth >= 16);
	CHECK(tm.tmDigitizedAspectX == 96 && tm.tmDigitizedAspectY == 96);
	CHECK(tm.tmFirstChar == 0x20 && (unsigned char)tm.tmLastChar == 0xFF && tm.tmBreakChar == ' ');
	CHECK((tm.tmPitchAndFamily & TMPF_TRUETYPE) && (tm.tmPitchAndFamily & TMPF_FIXED_PITCH) && (tm.tmPitchAndFamily & 0xF0) == FF_SWISS);
	CHECK(tm.tmItalic == 0 && tm.tmUnderlined == 0 && tm.tmStruckOut == 0);

	// Advance widths are Arial's, rounded to whole pixels: A 1366, space 569, i 455, W 1933 units.
	CHECK(arial.Width('A') == 11 && arial.Width(' ') == 4 && arial.Width('i') == 4 && arial.Width('W') == 15);
	CHECK(arial.Extent("Hello") == arial.Width('H') + arial.Width('e') + arial.Width('l') * 2 + arial.Width('o'));
	CHECK(arial.Extent("") == 0);

	// The W variant agrees with the A variant.
	SIZE wide = {};
	CHECK(GetTextExtentPoint32W(arial.dc, L"Hello", 5, &wide) && wide.cx == arial.Extent("Hello") && wide.cy == tm.tmHeight);

	// A positive height is the cell height: the em shrinks until ascent + descent fit.
	MetricsDc cell("Arial", 19);
	CHECK(cell.Metrics().tmHeight == 19 && cell.Metrics().tmHeight - cell.Metrics().tmInternalLeading == 16);
	MetricsDc smallCell("Arial", 12);
	CHECK(smallCell.Metrics().tmHeight <= 12 && smallCell.Metrics().tmHeight >= 11);

	// Bold uses the Bold face: heavier, wider, no overhang.
	MetricsDc bold("Arial", -16, FW_BOLD);
	CHECK(bold.Metrics().tmWeight == FW_BOLD && bold.Metrics().tmOverhang == 0);
	CHECK(bold.Width('A') > arial.Width('A') && bold.Extent("Hello World") > arial.Extent("Hello World"));

	// lfWidth scales the average character width (the game asks for 0.4 of the height for "Generals").
	MetricsDc narrow("Arial", -16, FW_NORMAL, 6);
	CHECK(narrow.Metrics().tmAveCharWidth == 6);
	CHECK(narrow.Extent("Hello World") < arial.Extent("Hello World"));
	CHECK(narrow.Metrics().tmHeight == tm.tmHeight);

	// Serif and monospace faces
	MetricsDc times("Times New Roman", -16);
	CHECK((times.Metrics().tmPitchAndFamily & 0xF0) == FF_ROMAN && times.Extent("Hello World") != arial.Extent("Hello World"));
	MetricsDc courier("Courier New", -16, FW_NORMAL, 0, 0, FIXED_PITCH | FF_MODERN);
	CHECK(!(courier.Metrics().tmPitchAndFamily & TMPF_FIXED_PITCH) && (courier.Metrics().tmPitchAndFamily & 0xF0) == FF_MODERN);
	CHECK(courier.Width('i') == courier.Width('W') && courier.Width('W') == 10); // 1229 / 2048 * 16 = 9.6
	CHECK(courier.Extent("0123456789") == 100);

	// Scaling: bigger text is bigger.
	MetricsDc big("Arial", -32);
	CHECK(big.Metrics().tmHeight >= 2 * tm.tmHeight - 2 && big.Extent("Hello") >= 2 * arial.Extent("Hello") - 5);

	// Without a bitmap or font the stock font and the 1x1 bitmap apply and nothing breaks.
	HDC plain = CreateCompatibleDC(nullptr);
	TEXTMETRIC stock = {};
	CHECK(GetTextMetrics(plain, &stock) && stock.tmHeight > 0 && stock.tmWeight == FW_BOLD);
	CHECK(ExtTextOutW(plain, 0, 0, 0, nullptr, L"x", 1, nullptr));
	DeleteDC(plain);
}

void TestRaster()
{
	// 'I' of Liberation Sans: a stem 1409 units high (cap height) standing on the baseline.
	{
		GameFont font("Arial", 12, false);
		const SIZE extent = font.Draw(L'I');
		CHECK(extent.cy == font.tm.tmHeight && extent.cx == 4);
		int l, t, r, b;
		font.Bounds(&l, &t, &r, &b);
		CHECK(b == font.tm.tmAscent);                      // the baseline: the bottom row is the one above it
		CHECK(t >= font.tm.tmAscent - 13 && t <= font.tm.tmAscent - 10); // 11 px cap height
		CHECK(l >= 0 && r <= extent.cx + 1);
		CHECK(font.Pixel(0, font.size - 1) == 0 && font.Pixel(font.size - 1, font.size - 1) == 0); // the cell was cleared
	}
	// Anti-aliasing: grey levels between black and white; every pixel is a grey (the channels are equal).
	{
		GameFont font("Arial", 12, false);
		font.Draw(L'O');
		bool grey = false;
		for (int y = 0; y < font.size; ++y)
			for (int x = 0; x < font.size; ++x)
			{
				const uint8_t *p = font.bits + y * font.stride + x * 3;
				CHECK(p[0] == p[1] && p[1] == p[2]);
				grey = grey || (p[0] > 0 && p[0] < 255);
			}
		CHECK(grey);
	}
	// NONANTIALIASED_QUALITY: only black and white.
	{
		GameFont font("Arial", 12, false, 0, NONANTIALIASED_QUALITY);
		font.Draw(L'O');
		bool lit = false, other = false;
		for (int y = 0; y < font.size; ++y)
			for (int x = 0; x < font.size; ++x)
			{
				const int v = font.Pixel(x, y);
				lit = lit || v == 255;
				other = other || (v != 0 && v != 255);
			}
		CHECK(lit && !other);
	}
	// Redrawing a character clears the cell first (ETO_OPAQUE with the background colour).
	{
		GameFont font("Arial", 12, false);
		font.Draw(L'W');
		const int wPixels = font.LitPixels();
		font.Draw(L'.');
		CHECK(font.LitPixels() < wPixels / 2 && font.LitPixels() > 0);
	}
	// A and W variants draw the same pixels; 0xE9 (e acute) and the euro sign (0x80 in Windows-1252) work.
	{
		GameFont font("Arial", 12, false);
		RECT cell = { 0, 0, font.size, font.size };
		ExtTextOutA(font.dc, 0, 0, ETO_OPAQUE, &cell, "A", 1, nullptr);
		const std::vector<uint8_t> narrowA = font.Snapshot();
		font.Draw(L'A');
		CHECK(font.Snapshot() == narrowA);
		ExtTextOutA(font.dc, 0, 0, ETO_OPAQUE, &cell, "\xE9", 1, nullptr);
		const std::vector<uint8_t> acute = font.Snapshot();
		font.Draw(0x00E9);
		CHECK(font.Snapshot() == acute && font.LitPixels() > 10);
		ExtTextOutA(font.dc, 0, 0, ETO_OPAQUE, &cell, "\x80", 1, nullptr);
		const std::vector<uint8_t> euro = font.Snapshot();
		font.Draw(0x20AC);
		CHECK(font.Snapshot() == euro && font.LitPixels() > 10);
		SIZE a = {}, w = {};
		GetTextExtentPoint32A(font.dc, "\xE9", 1, &a);
		GetTextExtentPoint32W(font.dc, L"\x00E9", 1, &w);
		CHECK(a.cx == w.cx && a.cx > 0);
	}
	// Characters the bundled fonts lack (CJK) draw the missing-glyph box, the same for all of them.
	{
		GameFont font("Arial", 12, false);
		font.Draw(0x4E2D);
		const std::vector<uint8_t> first = font.Snapshot();
		CHECK(font.LitPixels() > 8);
		font.Draw(0x4E2E);
		CHECK(font.Snapshot() == first);
		SIZE extent = {};
		const WCHAR pair[] = { 0xD83D, 0xDE00 }; // a surrogate pair is one character
		GetTextExtentPoint32W(font.dc, pair, 2, &extent);
		SIZE one = {};
		GetTextExtentPoint32W(font.dc, L"\x4E2D", 1, &one);
		CHECK(extent.cx == one.cx);
	}
	// 'W' is shifted one pixel by the game (xOrigin) and bold glyphs are heavier.
	{
		GameFont regular("Arial", 12, false);
		regular.Draw(L'H');
		const int regularPixels = regular.LitPixels();
		int sum = 0;
		for (int y = 0; y < regular.size; ++y) for (int x = 0; x < regular.size; ++x) sum += regular.Pixel(x, y);
		GameFont bold("Arial", 12, true);
		bold.Draw(L'H');
		int boldSum = 0;
		for (int y = 0; y < bold.size; ++y) for (int x = 0; x < bold.size; ++x) boldSum += bold.Pixel(x, y);
		CHECK(boldSum > sum && bold.LitPixels() >= regularPixels);
		regular.Draw(L'H', 3);
		int l, t, r, b;
		regular.Bounds(&l, &t, &r, &b);
		CHECK(l >= 3);
	}
	// Bottom-up DIBs store the rows the other way round.
	{
		GameFont down("Arial", 12, false, 0, ANTIALIASED_QUALITY, 24, true);
		GameFont up("Arial", 12, false, 0, ANTIALIASED_QUALITY, 24, false);
		down.Draw(L'F');
		up.Draw(L'F');
		bool same = true;
		for (int y = 0; y < down.size; ++y)
			same = same && memcmp(down.bits + y * down.stride, up.bits + (up.size - 1 - y) * up.stride, down.size * 3) == 0;
		CHECK(same && down.LitPixels() > 0);
	}
	// 32 bit, 16 bit (555) and 8 bit (grey ramp) DIBs get the text too.
	{
		GameFont d32("Arial", 12, false, 0, ANTIALIASED_QUALITY, 32);
		d32.Draw(L'L');
		int lit = 0;
		for (int i = 0; i < d32.size * d32.size; ++i)
			lit += d32.bits[i * 4] != 0 && d32.bits[i * 4] == d32.bits[i * 4 + 1];
		CHECK(lit > 5);
		GameFont d16("Arial", 12, false, 0, ANTIALIASED_QUALITY, 16);
		d16.Draw(L'L');
		int lit16 = 0;
		for (int i = 0; i < d16.size * d16.size; ++i)
			lit16 += ((uint16_t *)d16.bits)[i] != 0;
		CHECK(lit16 > 5);
		GameFont d8("Arial", 12, false, 0, ANTIALIASED_QUALITY, 8);
		d8.Draw(L'L');
		int lit8 = 0;
		for (int i = 0; i < d8.size * d8.size; ++i)
			lit8 += d8.bits[i] != 0;
		CHECK(lit8 > 5);
	}
	// Colours, transparent background, alignment, clipping, explicit spacing.
	{
		GameFont font("Arial", 12, false);
		SetTextColor(font.dc, RGB(255, 0, 0));
		font.Draw(L'I');
		int red = 0, other = 0;
		for (int y = 0; y < font.size; ++y)
			for (int x = 0; x < font.size; ++x)
			{
				const uint8_t *p = font.bits + y * font.stride + x * 3;
				red += p[2] > 0 && p[0] == 0 && p[1] == 0;
				other += p[0] != 0 || p[1] != 0;
			}
		CHECK(red > 0 && other == 0); // BGR order: only the red channel
		CHECK(SetTextColor(font.dc, RGB(1, 2, 3)) == RGB(255, 0, 0) && GetTextColor(font.dc) == RGB(1, 2, 3));
		SetTextColor(font.dc, RGB(255, 255, 255));

		RECT cell = { 0, 0, font.size, font.size };
		SetBkColor(font.dc, RGB(0, 0, 128));
		ExtTextOutW(font.dc, 0, 0, ETO_OPAQUE, &cell, L"", 0, nullptr);
		CHECK(font.Pixel(5, 5) == 128);
		CHECK(SetBkMode(font.dc, TRANSPARENT) == OPAQUE && GetBkMode(font.dc) == TRANSPARENT);
		ExtTextOutW(font.dc, 0, 0, 0, nullptr, L"I", 1, nullptr);
		CHECK(font.Pixel(font.size - 1, font.size - 1) == 128);              // untouched
		CHECK(font.bits[(font.tm.tmAscent - 3) * font.stride + 1 * 3 + 2] > 0); // text drawn over it (red channel of the stem)
		// OPAQUE mode paints the text box even without ETO_OPAQUE
		SetBkMode(font.dc, OPAQUE);
		SetBkColor(font.dc, RGB(0, 0, 0));
		ExtTextOutW(font.dc, 0, 0, ETO_OPAQUE, &cell, L"", 0, nullptr);
		SetBkColor(font.dc, RGB(0, 0, 200));
		ExtTextOutW(font.dc, 0, 0, 0, nullptr, L"III", 3, nullptr);
		SIZE mmm = {};
		GetTextExtentPoint32W(font.dc, L"III", 3, &mmm);
		CHECK(font.Pixel(0, 0) != 0 && font.Pixel(mmm.cx - 1, mmm.cy - 1) != 0 && font.Pixel(mmm.cx + 2, 2) == 0);
		SetBkColor(font.dc, RGB(0, 0, 0));
		SetBkMode(font.dc, TRANSPARENT);

		// Right alignment ends at x, centre alignment is centred on it.
		ExtTextOutW(font.dc, 0, 0, ETO_OPAQUE, &cell, L"", 0, nullptr);
		CHECK(SetTextAlign(font.dc, TA_RIGHT | TA_TOP) == (TA_LEFT | TA_TOP));
		ExtTextOutW(font.dc, 20, 0, 0, nullptr, L"III", 3, nullptr);
		int l, t, r, b;
		font.Bounds(&l, &t, &r, &b);
		CHECK(r <= 20 + 1 && r >= 20 - 3 && l >= 20 - mmm.cx - 1);
		ExtTextOutW(font.dc, 0, 0, ETO_OPAQUE, &cell, L"", 0, nullptr);
		SetTextAlign(font.dc, TA_CENTER | TA_BASELINE);
		ExtTextOutW(font.dc, 20, 20, 0, nullptr, L"III", 3, nullptr);
		font.Bounds(&l, &t, &r, &b);
		CHECK(l >= 20 - mmm.cx / 2 - 2 && r <= 20 + mmm.cx / 2 + 2 && b == 20);
		SetTextAlign(font.dc, TA_LEFT | TA_TOP);

		// ETO_CLIPPED
		ExtTextOutW(font.dc, 0, 0, ETO_OPAQUE, &cell, L"", 0, nullptr);
		RECT clip = { 0, 0, 5, font.size };
		ExtTextOutW(font.dc, 0, 0, ETO_CLIPPED, &clip, L"HHHH", 4, nullptr);
		font.Bounds(&l, &t, &r, &b);
		CHECK(r <= 5 && r > 0);

		// lpDx replaces the advances
		ExtTextOutW(font.dc, 0, 0, ETO_OPAQUE, &cell, L"", 0, nullptr);
		const INT spacing[2] = { 12, 12 };
		ExtTextOutW(font.dc, 0, 0, 0, nullptr, L"II", 2, spacing);
		font.Bounds(&l, &t, &r, &b);
		CHECK(r >= 12 + 1 && r <= 12 + 5);
	}
	// Underline and strike-out add a line under / through the text.
	{
		HDC dc = CreateCompatibleDC(nullptr);
		BITMAPINFOHEADER header = {};
		header.biSize = sizeof(header); header.biWidth = 40; header.biHeight = -40; header.biPlanes = 1; header.biBitCount = 24;
		uint8_t *bits = nullptr;
		HBITMAP bitmap = CreateDIBSection(dc, (const BITMAPINFO *)&header, DIB_RGB_COLORS, (void **)&bits, nullptr, 0);
		SelectObject(dc, bitmap);
		HFONT plain = CreateFont(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, "Arial");
		HFONT underlined = CreateFont(-16, 0, 0, 0, FW_NORMAL, FALSE, TRUE, FALSE, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, "Arial");
		SetTextColor(dc, RGB(255, 255, 255));
		SetBkMode(dc, TRANSPARENT);
		SelectObject(dc, plain);
		ExtTextOutW(dc, 0, 0, 0, nullptr, L"I", 1, nullptr);
		int before = 0;
		for (int i = 0; i < 40 * 40; ++i) before += bits[i * 3] != 0;
		memset(bits, 0, 40 * 40 * 3);
		SelectObject(dc, underlined);
		ExtTextOutW(dc, 0, 0, 0, nullptr, L"I", 1, nullptr);
		int after = 0;
		for (int i = 0; i < 40 * 40; ++i) after += bits[i * 3] != 0;
		CHECK(after > before);
		DeleteDC(dc);
		DeleteObject(plain);
		DeleteObject(underlined);
		DeleteObject(bitmap);
	}
	// Synthetic italics shear the glyphs; the upright face stays upright.
	{
		GameFont upright("Arial", 12, false);
		upright.Draw(L'I');
		int l, t, r, b;
		upright.Bounds(&l, &t, &r, &b);
		const int uprightWidth = r - l;
		HDC dc = CreateCompatibleDC(nullptr);
		BITMAPINFOHEADER header = {};
		header.biSize = sizeof(header); header.biWidth = 40; header.biHeight = -40; header.biPlanes = 1; header.biBitCount = 24;
		uint8_t *bits = nullptr;
		HBITMAP bitmap = CreateDIBSection(dc, (const BITMAPINFO *)&header, DIB_RGB_COLORS, (void **)&bits, nullptr, 0);
		SelectObject(dc, bitmap);
		HFONT italic = CreateFont(-16, 0, 0, 0, FW_NORMAL, TRUE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, "Arial");
		SelectObject(dc, italic);
		SetTextColor(dc, RGB(255, 255, 255));
		SetBkMode(dc, TRANSPARENT);
		ExtTextOutW(dc, 4, 0, 0, nullptr, L"I", 1, nullptr);
		int minX = 40, maxX = 0;
		for (int y = 0; y < 40; ++y) for (int x = 0; x < 40; ++x) if (bits[(y * 40 + x) * 3]) { minX = std::min(minX, x); maxX = std::max(maxX, x); }
		CHECK(maxX - minX + 1 > uprightWidth + 1);
		TEXTMETRIC tm = {};
		GetTextMetrics(dc, &tm);
		CHECK(tm.tmItalic != 0);
		DeleteDC(dc);
		DeleteObject(italic);
		DeleteObject(bitmap);
	}
}

void TestObjectsAndDc()
{
	GameFont game("Arial", 10, false);
	// A font is not deleted while it is selected, and SelectObject returns what was selected before.
	HFONT other = CreateFont(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, "Courier New");
	HGDIOBJ previous = SelectObject(game.dc, other);
	CHECK(previous == game.font);
	CHECK(DeleteObject(game.font)); // deselected: deletion works (the font is re-created below)
	game.font = CreateFont(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, "Arial");
	CHECK(SelectObject(game.dc, game.font) == other);
	CHECK(DeleteObject(other));
	CHECK(!DeleteObject(game.font)); // selected: Windows refuses
	game.oldFont = (HFONT)SelectObject(game.dc, game.oldFont);
	CHECK(game.oldFont == game.font);
	game.oldFont = (HFONT)SelectObject(game.dc, game.font);

	// GetObject returns the LOGFONT; the face name is the one that was asked for.
	LOGFONT lf = {};
	CHECK(GetObject(game.font, sizeof(lf), &lf) == sizeof(lf) && lf.lfHeight == -13 && strcmp(lf.lfFaceName, "Arial") == 0 && lf.lfWeight == FW_NORMAL);
	BITMAP bm = {};
	CHECK(GetObject(game.bitmap, sizeof(bm), &bm) && bm.bmWidth == game.size && bm.bmBitsPixel == 24 && bm.bmBits == game.bits);

	// Stock objects are shared and survive DeleteObject.
	HGDIOBJ gui = GetStockObject(DEFAULT_GUI_FONT);
	CHECK(gui != nullptr && DeleteObject(gui) && GetStockObject(DEFAULT_GUI_FONT) == gui);
	HDC dc = CreateCompatibleDC(nullptr);
	CHECK(SelectObject(dc, gui) != nullptr);
	TEXTMETRIC tm = {};
	CHECK(GetTextMetrics(dc, &tm) && tm.tmHeight > 0);
	DeleteDC(dc);

	// Drawing on a compatible bitmap, GetPixel/SetPixel, FillRect, BitBlt.
	HDC memory = CreateCompatibleDC(nullptr);
	HBITMAP bitmap = CreateCompatibleBitmap(memory, 20, 20);
	HGDIOBJ old = SelectObject(memory, bitmap);
	HBRUSH brush = CreateSolidBrush(RGB(10, 20, 30));
	RECT rect = { 2, 2, 8, 8 };
	CHECK(FillRect(memory, &rect, brush));
	CHECK(GetPixel(memory, 3, 3) == RGB(10, 20, 30) && GetPixel(memory, 9, 9) == RGB(0, 0, 0) && GetPixel(memory, 50, 50) == CLR_INVALID);
	SetPixel(memory, 15, 15, RGB(200, 100, 50));
	CHECK(GetPixel(memory, 15, 15) == RGB(200, 100, 50));
	HDC copy = CreateCompatibleDC(memory);
	HBITMAP target = CreateCompatibleBitmap(memory, 20, 20);
	SelectObject(copy, target);
	CHECK(BitBlt(copy, 0, 0, 20, 20, memory, 0, 0, SRCCOPY));
	CHECK(GetPixel(copy, 3, 3) == RGB(10, 20, 30) && GetPixel(copy, 15, 15) == RGB(200, 100, 50));
	SelectObject(memory, old);
	DeleteObject(bitmap);
	DeleteObject(brush);
	DeleteObject(target);
	DeleteDC(memory);
	DeleteDC(copy);

	// DrawText measures and aligns.
	HDC dt = CreateCompatibleDC(nullptr);
	HFONT arial = CreateFont(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, "Arial");
	SelectObject(dt, arial);
	RECT measure = { 0, 0, 0, 0 };
	SIZE extent = {};
	GetTextExtentPoint32A(dt, "Hello World", 11, &extent);
	CHECK(DrawTextA(dt, "Hello World", -1, &measure, DT_CALCRECT | DT_SINGLELINE) == extent.cy && measure.right == extent.cx && measure.bottom == extent.cy);
	RECT lines = { 0, 0, 0, 0 };
	DrawTextA(dt, "Hello\nWorld", -1, &lines, DT_CALCRECT);
	CHECK(lines.bottom == extent.cy * 2);
	RECT wrap = { 0, 0, 50, 0 };
	DrawTextW(dt, L"Hello World again", -1, &wrap, DT_CALCRECT | DT_WORDBREAK);
	CHECK(wrap.bottom >= extent.cy * 2 && wrap.right <= 50);
	DeleteObject(arial);
	DeleteDC(dt);
}

// The two files that AddFontResource registers are the bundled ones, with the family renamed so that the lookup
// can tell a registered font from the fallback.
bool WriteRenamed(const char *source, const char *destination, const char *from, const char *to)
{
	FILE *in = fopen(source, "rb");
	if (!in)
		return false;
	std::vector<unsigned char> data;
	unsigned char buffer[4096];
	size_t n;
	while ((n = fread(buffer, 1, sizeof(buffer), in)) > 0)
		data.insert(data.end(), buffer, buffer + n);
	fclose(in);
	const size_t length = strlen(from);
	for (size_t i = 0; i + length <= data.size(); ++i)
	{
		if (memcmp(&data[i], from, length) == 0)
			memcpy(&data[i], to, length);
		// UTF-16 big endian
		if (i + length * 2 <= data.size())
		{
			bool match = true;
			for (size_t k = 0; k < length; ++k)
				match = match && data[i + k * 2] == 0 && data[i + k * 2 + 1] == (unsigned char)from[k];
			if (match)
				for (size_t k = 0; k < length; ++k)
					data[i + k * 2 + 1] = (unsigned char)to[k];
		}
	}
	FILE *out = fopen(destination, "wb");
	if (!out)
		return false;
	fwrite(data.data(), 1, data.size(), out);
	fclose(out);
	return true;
}

void TestFontRegistration()
{
	// Not registered yet: the unknown face falls back to the sans font, which is proportional.
	{
		MetricsDc unknown("Registered Mono", -16);
		CHECK(unknown.Width('i') != unknown.Width('W'));
		CHECK(unknown.Face() == "Registered Mono");
	}
	CHECK(WriteRenamed("/webfonts/LiberationMono-Regular.ttf", "/registered_mono.ttf", "Liberation Mono", "Registered Mono"));
	CHECK(WriteRenamed("/webfonts/LiberationMono-Bold.ttf", "/registered_mono_bold.ttf", "Liberation Mono", "Registered Mono"));

	// A file that does not exist, or is not a font, is not added (AddFontResource returns 0).
	CHECK(AddFontResourceA("\\no\\such\\font.ttf") == 0);
	{
		FILE *junk = fopen("/not_a_font.ttf", "wb");
		fputs("this is not a font file", junk);
		fclose(junk);
		CHECK(AddFontResourceA("/not_a_font.ttf") == 0);
	}

	// The game's path style: backslashes, any case, a drive-less relative path.
	CHECK(AddFontResourceA("\\Registered_Mono.ttf") == 1);
	{
		MetricsDc mono("Registered Mono", -16);
		CHECK(mono.Width('i') == mono.Width('W') && mono.Width('W') == 10);
		CHECK(mono.Face() == "Registered Mono");
		CHECK(mono.Metrics().tmWeight == FW_NORMAL);
		// the face name matches without regard to case
		MetricsDc lower("registered mono", -16);
		CHECK(lower.Width('i') == 10);
		// A registered font is found before the substitution table; bold without a bold face is synthesised.
		MetricsDc bold("Registered Mono", -16, FW_BOLD);
		CHECK(bold.Metrics().tmWeight == FW_BOLD && bold.Metrics().tmOverhang == 1);
		CHECK(bold.Extent("MM") == 2 * 10 + 1);
	}
	// A second file of the same family supplies the bold face.
	CHECK(AddFontResourceA("/registered_mono_bold.ttf") == 1);
	{
		MetricsDc bold("Registered Mono", -16, FW_BOLD);
		CHECK(bold.Metrics().tmWeight == FW_BOLD && bold.Metrics().tmOverhang == 0);
		MetricsDc regular("Registered Mono", -16, FW_NORMAL);
		CHECK(regular.Metrics().tmWeight == FW_NORMAL);
	}
	// A font that is not in the file system is not found by a bare name unless it is a bundled one.
	CHECK(AddFontResourceA("LiberationSerif-Bold.ttf") == 1);
	{
		// ... and that registered font now supplies the serif family, bold only
		MetricsDc serif("Liberation Serif", -16);
		CHECK(serif.Metrics().tmWeight == FW_BOLD);
	}
	CHECK(RemoveFontResourceA("LiberationSerif-Bold.ttf"));
	// Registrations are counted like on Windows.
	CHECK(AddFontResourceA("/registered_mono.ttf") == 1);
	CHECK(RemoveFontResourceA("/registered_mono.ttf"));
	{
		MetricsDc mono("Registered Mono", -16);
		CHECK(mono.Width('i') == 10); // still registered once
	}
	CHECK(RemoveFontResourceA("/registered_mono.ttf"));
	CHECK(RemoveFontResourceA("/registered_mono_bold.ttf"));
	CHECK(!RemoveFontResourceA("/registered_mono_bold.ttf"));
	{
		MetricsDc gone("Registered Mono", -16);
		CHECK(gone.Width('i') != gone.Width('W')); // back to the fallback
	}
	// Existing DCs notice a registration made after the font was selected.
	{
		MetricsDc late("Registered Mono", -16);
		const int before = late.Width('i');
		CHECK(AddFontResourceW(L"/registered_mono.ttf") == 1);
		CHECK(before != late.Width('i') && late.Width('i') == 10);
		CHECK(RemoveFontResourceW(L"/registered_mono.ttf"));
	}
	// Ex variants
	CHECK(AddFontResourceExA("/registered_mono.ttf", FR_PRIVATE, nullptr) == 1 && RemoveFontResourceExA("/registered_mono.ttf", FR_PRIVATE, nullptr));
	remove("/registered_mono.ttf");
	remove("/registered_mono_bold.ttf");
	remove("/not_a_font.ttf");
}

void TestFallbackMapping()
{
	const int pixel = -16;
	MetricsDc sans("Liberation Sans", pixel);
	const char *sansNames[] = { "Arial", "arial", "Helvetica", "Tahoma", "Verdana", "Generals", "MS Sans Serif", "Segoe UI", "No Such Face", "" };
	for (size_t i = 0; i < sizeof(sansNames) / sizeof(sansNames[0]); ++i)
	{
		MetricsDc font(sansNames[i], pixel);
		CHECK(font.Extent("Hello World") == sans.Extent("Hello World"));
		CHECK(font.Metrics().tmHeight == sans.Metrics().tmHeight);
	}
	MetricsDc serif("Liberation Serif", pixel);
	const char *serifNames[] = { "Times New Roman", "times new roman", "Georgia", "MS Serif" };
	for (size_t i = 0; i < sizeof(serifNames) / sizeof(serifNames[0]); ++i)
	{
		MetricsDc font(serifNames[i], pixel);
		CHECK(font.Extent("Hello World") == serif.Extent("Hello World"));
	}
	CHECK(serif.Extent("Hello World") != sans.Extent("Hello World"));
	MetricsDc mono("Liberation Mono", pixel);
	const char *monoNames[] = { "Courier New", "Courier", "Lucida Console", "Consolas" };
	for (size_t i = 0; i < sizeof(monoNames) / sizeof(monoNames[0]); ++i)
	{
		MetricsDc font(monoNames[i], pixel);
		CHECK(font.Extent("Hello World") == mono.Extent("Hello World"));
	}
	// An unknown face takes its family from the pitch and family bits of the request.
	MetricsDc unknownFixed("Some Terminal Face", pixel, FW_NORMAL, 0, 0, FIXED_PITCH | FF_MODERN);
	CHECK(unknownFixed.Extent("Hello World") == mono.Extent("Hello World"));
	MetricsDc unknownRoman("Some Book Face", pixel, FW_NORMAL, 0, 0, VARIABLE_PITCH | FF_ROMAN);
	CHECK(unknownRoman.Extent("Hello World") == serif.Extent("Hello World"));
	// The face name reported is the one that was asked for.
	MetricsDc arial("Arial", pixel);
	CHECK(arial.Face() == "Arial");
	// Faces like Arial 8 pt (11 px) have the Windows cell: ascent 10, descent 3
	MetricsDc small("Arial", -11);
	CHECK(small.Metrics().tmAscent == 10 && small.Metrics().tmDescent == 3 && small.Metrics().tmHeight == 13);
	MetricsDc ten("Arial", -13);
	CHECK(ten.Metrics().tmAscent == 12 && ten.Metrics().tmDescent == 3 && ten.Metrics().tmHeight == 15);

	// The character widths of the A variant cover the Windows-1252 range, ABC widths add up to the advance.
	INT widths[256] = {};
	CHECK(GetCharWidth32A(arial.dc, 0, 255, widths) && widths['A'] == arial.Width('A') && widths[0xE9] > 0);
	ABC abc[256] = {};
	CHECK(GetCharABCWidthsA(arial.dc, 0, 255, abc));
	for (int c = 'A'; c <= 'Z'; ++c)
		CHECK(abc[c].abcA + (int)abc[c].abcB + abc[c].abcC == widths[c]);
	CHECK(abc['I'].abcB >= 1 && abc['I'].abcA >= 0);
	WCHAR wideFace[LF_FACESIZE] = {};
	CHECK(GetTextFaceW(arial.dc, LF_FACESIZE, wideFace) == 6 && wcscmp(wideFace, L"Arial") == 0);
	// CreateFontIndirect and the W variants build the same font as CreateFont
	LOGFONTW lfw = {};
	lfw.lfHeight = pixel;
	lfw.lfWeight = FW_NORMAL;
	wcscpy(lfw.lfFaceName, L"Times New Roman");
	HFONT indirect = CreateFontIndirectW(&lfw);
	HDC dc = CreateCompatibleDC(nullptr);
	SelectObject(dc, indirect);
	SIZE extent = {};
	GetTextExtentPoint32W(dc, L"Hello World", 11, &extent);
	CHECK(extent.cx == serif.Extent("Hello World"));
	TEXTMETRICW wideMetrics = {};
	CHECK(GetTextMetricsW(dc, &wideMetrics) && wideMetrics.tmHeight == serif.Metrics().tmHeight && wideMetrics.tmBreakChar == L' ');
	DeleteDC(dc);
	DeleteObject(indirect);
}

} // namespace

void TestGdiText()
{
	TestMetrics();
	TestRaster();
	TestObjectsAndDc();
	TestFontRegistration();
	TestFallbackMapping();
}
