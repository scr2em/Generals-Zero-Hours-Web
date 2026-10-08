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
** WebAssembly port: fonts for the GDI text emulation. See gdi_font.h.
**
** Why stb_truetype and not FreeType: it is a single public domain header the
** build already fetches (cmake/stb.cmake), it needs no Emscripten port download
** and no separate library, and the game needs only unhinted outline coverage:
** the glyphs are drawn once into a DIB, converted to 4 bit alpha and cached in
** a texture. What it does not do is TrueType bytecode hinting, so small text
** is a little softer than GDI's and advance widths come from the unhinted
** outlines (rounded to whole pixels, which is what GDI reports for hinted
** TrueType fonts at the sizes the game uses; Liberation Sans/Serif/Mono have the
** same advance widths as Arial/Times New Roman/Courier New by design).
*/
#include "gdi_font.h"

#include "charset.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <mutex>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb_truetype.h>

namespace WebCompat
{
namespace Gdi
{

namespace
{

// ---------------------------------------------------------------------------
// Faces
// ---------------------------------------------------------------------------

struct Blob
{
	std::vector<uint8_t> bytes;
};

} // namespace

struct Face
{
	std::shared_ptr<Blob> blob;
	stbtt_fontinfo info;
	int unitsPerEm = 1000;
	int winAscent = 0;
	int winDescent = 0;
	int hheaAscent = 0;
	int hheaDescent = 0;
	int hheaLineGap = 0;
	int avgWidth = 0;
	int xMin = 0;
	int xMax = 0;
	int weight = FW_NORMAL;
	bool italic = false;
	bool fixedPitch = false;
	int family = FF_DONTCARE;
	int firstChar = 0x20;
	int lastChar = 0xFFFF;
	std::string familyName;
	std::vector<std::string> names; // lower case family, typographic family and full names
};

namespace
{

std::string Utf16ToUtf8(const WCHAR *s, size_t length)
{
	std::string result;
	for (size_t i = 0; i < length; ++i)
	{
		uint32_t c = (uint16_t)s[i];
		if (IsHighSurrogate(c) && i + 1 < length && IsLowSurrogate((uint16_t)s[i + 1]))
		{
			c = 0x10000 + ((c - 0xD800) << 10) + ((uint16_t)s[i + 1] - 0xDC00);
			++i;
		}
		unsigned char buffer[4];
		const size_t n = EncodeUtf8(c, buffer);
		result.append((const char *)buffer, n);
	}
	return result;
}

std::string LowerAscii(std::string s)
{
	for (size_t i = 0; i < s.size(); ++i)
	{
		if (s[i] >= 'A' && s[i] <= 'Z')
			s[i] = (char)(s[i] - 'A' + 'a');
	}
	return s;
}

std::string Trim(const std::string &s)
{
	size_t begin = 0;
	size_t end = s.size();
	while (begin < end && (s[begin] == ' ' || s[begin] == '\t'))
		++begin;
	while (end > begin && (s[end - 1] == ' ' || s[end - 1] == '\t'))
		--end;
	return s.substr(begin, end - begin);
}

// The name table strings of one kind, as UTF-8.
void CollectNames(unsigned char *data, unsigned nameTable, int nameId, std::vector<std::string> *out)
{
	if (!nameTable)
		return;
	const int count = ttUSHORT(data + nameTable + 2);
	const unsigned stringOffset = nameTable + ttUSHORT(data + nameTable + 4);
	for (int i = 0; i < count; ++i)
	{
		unsigned char *record = data + nameTable + 6 + 12 * i;
		const int platform = ttUSHORT(record);
		const int encoding = ttUSHORT(record + 2);
		const int id = ttUSHORT(record + 6);
		const int length = ttUSHORT(record + 8);
		const int offset = ttUSHORT(record + 10);
		if (id != nameId)
			continue;
		unsigned char *string = data + stringOffset + offset;
		std::string value;
		if (platform == 3 || platform == 0)
		{
			// UTF-16 big endian (Windows Symbol and Unicode encodings alike)
			std::vector<WCHAR> units;
			for (int k = 0; k + 1 < length; k += 2)
				units.push_back((WCHAR)((string[k] << 8) | string[k + 1]));
			value = Utf16ToUtf8(units.data(), units.size());
		}
		else if (platform == 1 && encoding == 0)
		{
			// Mac Roman; the family names in the fonts are plain ASCII.
			for (int k = 0; k < length; ++k)
			{
				unsigned char b = string[k];
				if (b < 0x80)
					value.push_back((char)b);
				else
				{
					unsigned char buffer[4];
					const size_t n = EncodeUtf8(b, buffer); // approximates Mac Roman with Latin-1
					value.append((const char *)buffer, n);
				}
			}
		}
		else
			continue;
		if (!value.empty() && std::find(out->begin(), out->end(), value) == out->end())
			out->push_back(value);
	}
}

// Reads the properties the font mapper and the text metrics need.
bool InitFace(Face *face, const std::shared_ptr<Blob> &blob, int offset)
{
	face->blob = blob;
	unsigned char *data = blob->bytes.data();
	if (!stbtt_InitFont(&face->info, data, offset))
		return false;

	const unsigned head = stbtt__find_table(data, face->info.fontstart, "head");
	if (!head)
		return false;
	face->unitsPerEm = ttUSHORT(data + head + 18);
	if (face->unitsPerEm <= 0)
		return false;
	face->xMin = ttSHORT(data + head + 36);
	face->xMax = ttSHORT(data + head + 40);
	const int macStyle = ttUSHORT(data + head + 44);

	stbtt_GetFontVMetrics(&face->info, &face->hheaAscent, &face->hheaDescent, &face->hheaLineGap);

	face->weight = (macStyle & 1) ? FW_BOLD : FW_NORMAL;
	face->italic = (macStyle & 2) != 0;
	face->winAscent = face->hheaAscent;
	face->winDescent = -face->hheaDescent;
	face->avgWidth = 0;

	int panoseFamily = 0;
	int panoseSerif = 0;
	int panoseProportion = 0;
	const unsigned os2 = stbtt__find_table(data, face->info.fontstart, "OS/2");
	if (os2)
	{
		face->avgWidth = ttSHORT(data + os2 + 2);
		int weightClass = ttUSHORT(data + os2 + 4);
		if (weightClass > 0)
		{
			if (weightClass < 10)
				weightClass *= 100;
			face->weight = std::min(weightClass, 1000);
		}
		panoseFamily = data[os2 + 32];
		panoseSerif = data[os2 + 33];
		panoseProportion = data[os2 + 35];
		const int selection = ttUSHORT(data + os2 + 62);
		face->italic = (selection & 1) != 0 || (macStyle & 2) != 0;
		face->firstChar = ttUSHORT(data + os2 + 64);
		face->lastChar = ttUSHORT(data + os2 + 66);
		// usWinAscent / usWinDescent are what GDI uses for the cell height.
		const int winAscent = ttUSHORT(data + os2 + 74);
		const int winDescent = ttUSHORT(data + os2 + 76);
		if (winAscent + winDescent > 0)
		{
			face->winAscent = winAscent;
			face->winDescent = winDescent;
		}
	}
	const unsigned post = stbtt__find_table(data, face->info.fontstart, "post");
	if (post && ttULONG(data + post + 12) != 0)
		face->fixedPitch = true;
	if (panoseProportion == 9)
		face->fixedPitch = true;

	if (face->fixedPitch)
		face->family = FF_MODERN;
	else if (panoseFamily == 2 && panoseSerif >= 2 && panoseSerif <= 10)
		face->family = FF_ROMAN;
	else if (panoseFamily == 2 && panoseSerif >= 11 && panoseSerif <= 13)
		face->family = FF_SWISS;
	else if (panoseFamily == 3)
		face->family = FF_SCRIPT;
	else if (panoseFamily == 4)
		face->family = FF_DECORATIVE;

	// tmAveCharWidth is the average over the lower case letters and the space, weighted by their frequency
	// in English text (the formula of OS/2 version 0, which gives Arial its 904). The xAvgCharWidth of newer
	// fonts, an average over all glyphs, is wider than what GDI reports for the Windows fonts, and
	// Liberation Sans 2.x is among those.
	{
		static const int kWeights[27] = { 64, 14, 27, 35, 100, 20, 14, 42, 63, 3, 6, 35, 20, 56, 56, 17, 4, 49, 56, 71, 31, 10, 18, 3, 18, 2, 166 };
		long total = 0;
		bool complete = true;
		for (int i = 0; i < 27; ++i)
		{
			const int codePoint = i < 26 ? 'a' + i : ' ';
			if (stbtt_FindGlyphIndex(&face->info, codePoint) == 0)
			{
				complete = false;
				break;
			}
			int advance = 0, bearing = 0;
			stbtt_GetCodepointHMetrics(&face->info, codePoint, &advance, &bearing);
			total += (long)advance * kWeights[i];
		}
		if (complete)
			face->avgWidth = (int)((total + 500) / 1000);
	}
	if (face->avgWidth <= 0)
	{
		int advance = 0, bearing = 0;
		stbtt_GetCodepointHMetrics(&face->info, 'x', &advance, &bearing);
		face->avgWidth = advance > 0 ? advance : face->unitsPerEm / 2;
	}

	// Names. nameID 1 is the family, 16 the typographic family, 4 the full name.
	const unsigned nameTable = stbtt__find_table(data, face->info.fontstart, "name");
	std::vector<std::string> families;
	CollectNames(data, nameTable, 1, &families);
	std::vector<std::string> typographic;
	CollectNames(data, nameTable, 16, &typographic);
	std::vector<std::string> full;
	CollectNames(data, nameTable, 4, &full);
	if (!families.empty())
		face->familyName = families[0];
	else if (!typographic.empty())
		face->familyName = typographic[0];
	else if (!full.empty())
		face->familyName = full[0];
	for (const std::vector<std::string> *list : { &families, &typographic, &full })
	{
		for (size_t i = 0; i < list->size(); ++i)
			face->names.push_back(LowerAscii((*list)[i]));
	}
	return true;
}

// Reads a file into memory. The buffer is on the heap: threads may have a small stack.
std::shared_ptr<Blob> ReadFile(const std::string &path)
{
	FILE *file = fopen(path.c_str(), "rb");
	if (!file)
		return nullptr;
	std::shared_ptr<Blob> blob = std::make_shared<Blob>();
	std::vector<uint8_t> chunk(64 * 1024);
	size_t n;
	while ((n = fread(chunk.data(), 1, chunk.size(), file)) > 0)
	{
		blob->bytes.insert(blob->bytes.end(), chunk.begin(), chunk.begin() + n);
		if (blob->bytes.size() > (size_t)128 * 1024 * 1024)
		{
			fclose(file);
			return nullptr;
		}
	}
	fclose(file);
	return blob->bytes.empty() ? nullptr : blob;
}

// Loads all faces of a file (a TrueType collection holds several).
std::vector<std::shared_ptr<Face>> LoadFaces(const std::string &path)
{
	std::vector<std::shared_ptr<Face>> faces;
	std::shared_ptr<Blob> blob = ReadFile(path);
	if (!blob)
		return faces;
	// Pad so that the table lookups of a truncated file stay inside the buffer.
	const size_t size = blob->bytes.size();
	blob->bytes.resize(size + 16, 0);
	for (int index = 0;; ++index)
	{
		const int offset = stbtt_GetFontOffsetForIndex(blob->bytes.data(), index);
		if (offset < 0 || (size_t)offset >= size)
			break;
		std::shared_ptr<Face> face = std::make_shared<Face>();
		if (InitFace(face.get(), blob, offset))
			faces.push_back(face);
	}
	return faces;
}

// ---------------------------------------------------------------------------
// The registry: the fonts the game added, the bundled fonts
// ---------------------------------------------------------------------------

struct Registration
{
	std::string key;
	int refs = 0;
	std::vector<std::shared_ptr<Face>> faces;
};

enum Category { CATEGORY_SANS, CATEGORY_SERIF, CATEGORY_MONO, CATEGORY_COUNT };

const char *const kBundledFiles[CATEGORY_COUNT][2] = {
	{ "LiberationSans-Regular.ttf", "LiberationSans-Bold.ttf" },
	{ "LiberationSerif-Regular.ttf", "LiberationSerif-Bold.ttf" },
	{ "LiberationMono-Regular.ttf", "LiberationMono-Bold.ttf" },
};

struct BundledSlot
{
	std::shared_ptr<Face> face;
	bool tried = false;
};

struct Registry
{
	std::mutex lock;
	std::vector<Registration> registered;
	BundledSlot bundled[CATEGORY_COUNT][2];
	std::string bundledDirectory;
	uint32_t generation = 1;
};

Registry &Reg()
{
	static Registry registry;
	return registry;
}

std::string BundledDirectory(Registry &registry)
{
	if (!registry.bundledDirectory.empty())
		return registry.bundledDirectory;
	const char *environment = getenv("WEBCOMPAT_FONT_DIR");
	return (environment && *environment) ? environment : "/webfonts";
}

// Called with the registry locked.
std::shared_ptr<Face> BundledFace(Registry &registry, Category category, bool bold)
{
	BundledSlot &slot = registry.bundled[category][bold ? 1 : 0];
	if (!slot.tried)
	{
		slot.tried = true;
		const std::string path = BundledDirectory(registry) + "/" + kBundledFiles[category][bold ? 1 : 0];
		std::vector<std::shared_ptr<Face>> faces = LoadFaces(path);
		if (!faces.empty())
			slot.face = faces[0];
	}
	return slot.face;
}

// Substitutes for the faces that Windows ships, the way a font replacement table of
// a Windows emulator works. Names are compared lower case.
struct Alias
{
	const char *name;
	Category category;
};

const Alias kAliases[] = {
	// Arial and its relatives: Liberation Sans has the metrics of Arial.
	{ "arial", CATEGORY_SANS }, { "arial unicode ms", CATEGORY_SANS }, { "arial narrow", CATEGORY_SANS },
	{ "arial black", CATEGORY_SANS }, { "helvetica", CATEGORY_SANS }, { "helvetica neue", CATEGORY_SANS },
	{ "tahoma", CATEGORY_SANS }, { "verdana", CATEGORY_SANS }, { "segoe ui", CATEGORY_SANS },
	{ "microsoft sans serif", CATEGORY_SANS }, { "ms sans serif", CATEGORY_SANS }, { "ms shell dlg", CATEGORY_SANS },
	{ "ms shell dlg 2", CATEGORY_SANS }, { "ms ui gothic", CATEGORY_SANS }, { "calibri", CATEGORY_SANS },
	{ "trebuchet ms", CATEGORY_SANS }, { "lucida sans unicode", CATEGORY_SANS }, { "lucida sans", CATEGORY_SANS },
	{ "lucida grande", CATEGORY_SANS }, { "century gothic", CATEGORY_SANS }, { "franklin gothic medium", CATEGORY_SANS },
	{ "impact", CATEGORY_SANS }, { "system", CATEGORY_SANS }, { "sans", CATEGORY_SANS }, { "sans-serif", CATEGORY_SANS },
	{ "sans serif", CATEGORY_SANS }, { "generals", CATEGORY_SANS }, { "dejavu sans", CATEGORY_SANS },
	{ "noto sans", CATEGORY_SANS }, { "liberation sans", CATEGORY_SANS },
	// Times New Roman: Liberation Serif.
	{ "times new roman", CATEGORY_SERIF }, { "times", CATEGORY_SERIF }, { "ms serif", CATEGORY_SERIF },
	{ "georgia", CATEGORY_SERIF }, { "cambria", CATEGORY_SERIF }, { "book antiqua", CATEGORY_SERIF },
	{ "palatino linotype", CATEGORY_SERIF }, { "palatino", CATEGORY_SERIF }, { "garamond", CATEGORY_SERIF },
	{ "century", CATEGORY_SERIF }, { "bookman old style", CATEGORY_SERIF }, { "serif", CATEGORY_SERIF },
	{ "dejavu serif", CATEGORY_SERIF }, { "liberation serif", CATEGORY_SERIF },
	// Courier New: Liberation Mono.
	{ "courier new", CATEGORY_MONO }, { "courier", CATEGORY_MONO }, { "lucida console", CATEGORY_MONO },
	{ "consolas", CATEGORY_MONO }, { "andale mono", CATEGORY_MONO }, { "monospace", CATEGORY_MONO },
	{ "fixedsys", CATEGORY_MONO }, { "terminal", CATEGORY_MONO }, { "dejavu sans mono", CATEGORY_MONO },
	{ "liberation mono", CATEGORY_MONO }, { "ms gothic", CATEGORY_MONO }, { "ms mincho", CATEGORY_MONO },
};

Category CategoryFor(const std::string &lowerName, BYTE pitchAndFamily)
{
	for (size_t i = 0; i < sizeof(kAliases) / sizeof(kAliases[0]); ++i)
	{
		if (lowerName == kAliases[i].name)
			return kAliases[i].category;
	}
	const int family = pitchAndFamily & 0xF0;
	if ((pitchAndFamily & 3) == FIXED_PITCH || family == FF_MODERN)
		return CATEGORY_MONO;
	if (family == FF_ROMAN)
		return CATEGORY_SERIF;
	return CATEGORY_SANS;
}

bool NameMatches(const Face &face, const std::string &lowerName)
{
	for (size_t i = 0; i < face.names.size(); ++i)
	{
		if (face.names[i] == lowerName)
			return true;
	}
	return false;
}

// The registered face that suits the request best, or null if no registered
// font carries the face name. Called with the registry locked.
std::shared_ptr<Face> FindRegistered(Registry &registry, const std::string &lowerName, int weight, bool italic)
{
	if (lowerName.empty())
		return nullptr;
	std::shared_ptr<Face> best;
	int bestScore = 0;
	for (size_t r = 0; r < registry.registered.size(); ++r)
	{
		for (size_t f = 0; f < registry.registered[r].faces.size(); ++f)
		{
			const std::shared_ptr<Face> &face = registry.registered[r].faces[f];
			if (!NameMatches(*face, lowerName))
				continue;
			const int score = abs(face->weight - weight) + (face->italic != italic ? 1000 : 0);
			if (!best || score < bestScore)
			{
				best = face;
				bestScore = score;
			}
		}
	}
	return best;
}

// A face that has a glyph for the code point, for characters that the chosen face lacks:
// the registered fonts first (the game's own fonts may hold CJK), then the bundled ones.
std::shared_ptr<const Face> FindFallbackFace(uint32_t codePoint, bool bold, const Face *exclude, int *glyph)
{
	Registry &registry = Reg();
	std::lock_guard<std::mutex> guard(registry.lock);
	for (size_t r = 0; r < registry.registered.size(); ++r)
	{
		for (size_t f = 0; f < registry.registered[r].faces.size(); ++f)
		{
			const std::shared_ptr<Face> &face = registry.registered[r].faces[f];
			if (face.get() == exclude)
				continue;
			const int index = stbtt_FindGlyphIndex(&face->info, (int)codePoint);
			if (index > 0)
			{
				*glyph = index;
				return face;
			}
		}
	}
	for (int category = 0; category < CATEGORY_COUNT; ++category)
	{
		std::shared_ptr<Face> face = BundledFace(registry, (Category)category, bold);
		if (!face && bold)
			face = BundledFace(registry, (Category)category, false);
		if (!face || face.get() == exclude)
			continue;
		const int index = stbtt_FindGlyphIndex(&face->info, (int)codePoint);
		if (index > 0)
		{
			*glyph = index;
			return face;
		}
	}
	return nullptr;
}

// The face, glyph and scale to use for a code point.
struct GlyphRef
{
	std::shared_ptr<const Face> face;
	int glyph = 0;
	float scaleX = 0;
	float scaleY = 0;
};

bool ResolveGlyph(const ScaledFont &font, uint32_t codePoint, GlyphRef *ref)
{
	if (!font.face)
		return false;
	int glyph = stbtt_FindGlyphIndex(&font.face->info, (int)codePoint);
	std::shared_ptr<const Face> face = font.face;
	if (glyph == 0 && codePoint >= 0x20)
	{
		int fallbackGlyph = 0;
		std::shared_ptr<const Face> fallback = FindFallbackFace(codePoint, font.weight >= FW_SEMIBOLD, font.face.get(), &fallbackGlyph);
		if (fallback)
		{
			face = fallback;
			glyph = fallbackGlyph;
		}
	}
	ref->face = face;
	ref->glyph = glyph;
	ref->scaleY = (float)font.ppem / (float)face->unitsPerEm;
	ref->scaleX = ref->scaleY * font.widthFactor;
	return true;
}

int RoundToInt(float v)
{
	return (int)floorf(v + 0.5f);
}

} // namespace

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

uint32_t RegistryGeneration()
{
	Registry &registry = Reg();
	std::lock_guard<std::mutex> guard(registry.lock);
	return registry.generation;
}

int AddFontFile(const std::string &key, const std::string &path)
{
	Registry &registry = Reg();
	const std::string lowerKey = LowerAscii(key);
	{
		std::lock_guard<std::mutex> guard(registry.lock);
		for (size_t i = 0; i < registry.registered.size(); ++i)
		{
			if (registry.registered[i].key == lowerKey)
			{
				++registry.registered[i].refs;
				return (int)registry.registered[i].faces.size();
			}
		}
	}
	// Load outside the lock: it reads the file.
	std::vector<std::shared_ptr<Face>> faces = LoadFaces(path);
	if (faces.empty())
		return 0;
	std::lock_guard<std::mutex> guard(registry.lock);
	Registration registration;
	registration.key = lowerKey;
	registration.refs = 1;
	registration.faces = faces;
	registry.registered.push_back(registration);
	++registry.generation;
	return (int)faces.size();
}

bool RemoveFontFile(const std::string &key)
{
	Registry &registry = Reg();
	const std::string lowerKey = LowerAscii(key);
	std::lock_guard<std::mutex> guard(registry.lock);
	for (size_t i = 0; i < registry.registered.size(); ++i)
	{
		if (registry.registered[i].key == lowerKey)
		{
			if (--registry.registered[i].refs <= 0)
			{
				registry.registered.erase(registry.registered.begin() + i);
				++registry.generation;
			}
			return true;
		}
	}
	return false;
}

void SetBundledFontDirectory(const std::string &directory)
{
	Registry &registry = Reg();
	std::lock_guard<std::mutex> guard(registry.lock);
	registry.bundledDirectory = directory;
	for (int c = 0; c < CATEGORY_COUNT; ++c)
	{
		for (int b = 0; b < 2; ++b)
			registry.bundled[c][b] = BundledSlot();
	}
	++registry.generation;
}

void ResolveFont(const LOGFONTW &lf, ScaledFont *out)
{
	*out = ScaledFont();

	size_t nameLength = 0;
	while (nameLength < LF_FACESIZE && lf.lfFaceName[nameLength])
		++nameLength;
	const std::string requestedName = Trim(Utf16ToUtf8(lf.lfFaceName, nameLength));
	const std::string lowerName = LowerAscii(requestedName);
	const int weight = lf.lfWeight > 0 ? (int)lf.lfWeight : FW_NORMAL;
	const bool italic = lf.lfItalic != 0;

	bool nameMatched = false;
	std::shared_ptr<Face> face;
	bool synthBold = false;
	{
		Registry &registry = Reg();
		std::lock_guard<std::mutex> guard(registry.lock);
		out->generation = registry.generation;
		face = FindRegistered(registry, lowerName, weight, italic);
		nameMatched = face != nullptr;
		if (!face)
		{
			const Category category = CategoryFor(lowerName, lf.lfPitchAndFamily);
			const bool wantBold = weight >= FW_SEMIBOLD;
			face = BundledFace(registry, category, wantBold);
			if (!face && wantBold)
			{
				face = BundledFace(registry, category, false);
				synthBold = face != nullptr;
			}
			if (!face && category != CATEGORY_SANS)
				face = BundledFace(registry, CATEGORY_SANS, wantBold);
		}
	}
	if (face && nameMatched)
		synthBold = weight >= FW_SEMIBOLD && face->weight < FW_SEMIBOLD;
	out->face = face;
	out->synthBold = synthBold;
	out->synthItalic = italic && face && !face->italic;
	out->antialias = lf.lfQuality != NONANTIALIASED_QUALITY;
	out->underline = lf.lfUnderline != 0;
	out->strikeOut = lf.lfStrikeOut != 0;
	out->weight = face ? (synthBold ? std::max(weight, (int)FW_BOLD) : face->weight) : weight;
	out->charSet = lf.lfCharSet == DEFAULT_CHARSET ? (BYTE)ANSI_CHARSET : lf.lfCharSet;

	// The name GDI reports is the face that was asked for when it is one the system maps (as
	// on Windows, where it is the face that was selected), else the family of the chosen font.
	{
		const std::string &name = (nameMatched || requestedName.empty()) && face ? face->familyName : requestedName;
		std::vector<WCHAR> wide;
		const unsigned char *p = (const unsigned char *)name.data();
		size_t remaining = name.size();
		while (remaining > 0 && wide.size() < LF_FACESIZE - 1)
		{
			uint32_t c = 0;
			size_t n = DecodeUtf8(p, remaining, &c);
			if (n == 0)
			{
				c = *p;
				n = 1;
			}
			p += n;
			remaining -= n;
			if (c >= 0x10000)
			{
				c -= 0x10000;
				wide.push_back((WCHAR)(0xD800 + (c >> 10)));
				if (wide.size() < LF_FACESIZE - 1)
					wide.push_back((WCHAR)(0xDC00 + (c & 0x3FF)));
			}
			else
				wide.push_back((WCHAR)c);
		}
		for (size_t i = 0; i < wide.size(); ++i)
			out->faceName[i] = wide[i];
		out->faceName[wide.size()] = 0;
	}

	// Size. A negative height is the em height in pixels, a positive one the
	// cell height (ascent + descent) and zero selects a default size.
	const int upem = face ? face->unitsPerEm : 1000;
	const int winAscent = face ? face->winAscent : 900;
	const int winDescent = face ? face->winDescent : 200;
	auto ceilScaled = [upem](int units, int ppem) { return (units * ppem + upem - 1) / upem; };
	int ppem;
	if (lf.lfHeight < 0)
		ppem = -lf.lfHeight;
	else if (lf.lfHeight > 0)
	{
		// The largest em size whose cell fits the requested height.
		ppem = std::max(1, (int)((int64_t)lf.lfHeight * upem / (winAscent + winDescent)) + 1);
		while (ppem > 1 && ceilScaled(winAscent, ppem) + ceilScaled(winDescent, ppem) > lf.lfHeight)
			--ppem;
	}
	else
		ppem = 12;
	ppem = std::max(1, std::min(ppem, 1024));
	out->ppem = ppem;

	out->ascent = ceilScaled(winAscent, ppem);
	out->descent = ceilScaled(winDescent, ppem);
	out->height = out->ascent + out->descent;
	out->internalLeading = std::max(0, out->height - ppem);
	if (face)
	{
		const int lineSpacing = RoundToInt((float)(face->hheaAscent - face->hheaDescent + face->hheaLineGap) * ppem / upem);
		out->externalLeading = std::max(0, lineSpacing - out->height);
		const float averageUnscaled = (float)face->avgWidth * ppem / upem;
		if (lf.lfWidth > 0 && averageUnscaled > 0.0f)
			out->widthFactor = std::max(0.25f, std::min(4.0f, (float)lf.lfWidth / averageUnscaled));
		out->avgCharWidth = RoundToInt(averageUnscaled * out->widthFactor);
		out->maxCharWidth = (int)ceilf((float)(face->xMax - face->xMin) * ppem / upem * out->widthFactor);
		out->firstChar = std::min(face->firstChar, 255);
		out->lastChar = std::min(face->lastChar, 255);
		if (out->firstChar > out->lastChar)
			out->firstChar = 0x20, out->lastChar = 0xFF;
		out->pitchAndFamily = (BYTE)(TMPF_VECTOR | TMPF_TRUETYPE | (face->fixedPitch ? 0 : TMPF_FIXED_PITCH) | face->family);
	}
	else
	{
		out->avgCharWidth = std::max(1, ppem / 2);
		out->maxCharWidth = ppem;
		out->pitchAndFamily = (BYTE)(lf.lfPitchAndFamily & 0xF0);
	}
	if (out->synthBold)
		out->overhang = 1;
}

int GlyphAdvance(const ScaledFont &font, uint32_t codePoint)
{
	GlyphRef ref;
	if (!ResolveGlyph(font, codePoint, &ref))
		return font.avgCharWidth;
	int advance = 0, bearing = 0;
	stbtt_GetGlyphHMetrics(&ref.face->info, ref.glyph, &advance, &bearing);
	return RoundToInt((float)advance * ref.scaleX);
}

bool GlyphMetrics(const ScaledFont &font, uint32_t codePoint, GlyphAbc *abc)
{
	GlyphRef ref;
	*abc = GlyphAbc();
	if (!ResolveGlyph(font, codePoint, &ref))
	{
		abc->advance = font.avgCharWidth;
		abc->c = font.avgCharWidth;
		return false;
	}
	int advance = 0, bearing = 0;
	stbtt_GetGlyphHMetrics(&ref.face->info, ref.glyph, &advance, &bearing);
	abc->advance = RoundToInt((float)advance * ref.scaleX);
	int x0, y0, x1, y1;
	stbtt_GetGlyphBitmapBoxSubpixel(&ref.face->info, ref.glyph, ref.scaleX, ref.scaleY, 0, 0, &x0, &y0, &x1, &y1);
	if (x1 > x0)
	{
		abc->a = x0;
		abc->b = x1 - x0;
	}
	abc->c = abc->advance - abc->a - abc->b;
	return true;
}

bool RenderGlyph(const ScaledFont &font, uint32_t codePoint, GlyphBitmap *out)
{
	*out = GlyphBitmap();
	GlyphRef ref;
	if (!ResolveGlyph(font, codePoint, &ref))
	{
		out->advance = font.avgCharWidth;
		return false;
	}
	int advanceUnits = 0, bearing = 0;
	stbtt_GetGlyphHMetrics(&ref.face->info, ref.glyph, &advanceUnits, &bearing);
	out->advance = RoundToInt((float)advanceUnits * ref.scaleX);
	if (codePoint < 0x20)
		return false; // control characters have no visible glyph

	int x0, y0, x1, y1;
	stbtt_GetGlyphBitmapBoxSubpixel(&ref.face->info, ref.glyph, ref.scaleX, ref.scaleY, 0, 0, &x0, &y0, &x1, &y1);
	int width = x1 - x0;
	int height = y1 - y0;
	if (width <= 0 || height <= 0 || width > 4096 || height > 4096)
		return false;
	std::vector<uint8_t> bitmap((size_t)width * height, 0);
	stbtt_MakeGlyphBitmapSubpixel(&ref.face->info, bitmap.data(), width, height, width, ref.scaleX, ref.scaleY, 0.0f, 0.0f, ref.glyph);
	int left = x0;
	const int top = y0;

	if (font.synthItalic)
	{
		// Shear about the baseline by 12 degrees, like GDI's simulated italics.
		const float slope = 0.2126f;
		float minShift = 0, maxShift = 0;
		for (int row = 0; row < height; ++row)
		{
			const float shift = -((float)(top + row) + 0.5f) * slope;
			minShift = row == 0 ? shift : std::min(minShift, shift);
			maxShift = row == 0 ? shift : std::max(maxShift, shift);
		}
		const int shiftBase = (int)floorf(minShift);
		const int newWidth = width + (int)ceilf(maxShift) - shiftBase + 1;
		std::vector<uint8_t> sheared((size_t)newWidth * height, 0);
		for (int row = 0; row < height; ++row)
		{
			const float shift = -((float)(top + row) + 0.5f) * slope;
			const int whole = (int)floorf(shift);
			const float fraction = shift - (float)whole;
			for (int x = 0; x < width; ++x)
			{
				const int value = bitmap[(size_t)row * width + x];
				if (!value)
					continue;
				const int target = x + whole - shiftBase;
				uint8_t &a = sheared[(size_t)row * newWidth + target];
				a = (uint8_t)std::min(255, a + (int)(value * (1.0f - fraction) + 0.5f));
				uint8_t &b = sheared[(size_t)row * newWidth + target + 1];
				b = (uint8_t)std::min(255, b + (int)(value * fraction + 0.5f));
			}
		}
		bitmap.swap(sheared);
		width = newWidth;
		left += shiftBase;
	}

	if (font.synthBold)
	{
		// Smear one pixel to the right.
		std::vector<uint8_t> bold((size_t)(width + 1) * height, 0);
		for (int row = 0; row < height; ++row)
		{
			for (int x = 0; x < width; ++x)
			{
				const uint8_t value = bitmap[(size_t)row * width + x];
				uint8_t &a = bold[(size_t)row * (width + 1) + x];
				a = std::max(a, value);
				uint8_t &b = bold[(size_t)row * (width + 1) + x + 1];
				b = std::max(b, value);
			}
		}
		bitmap.swap(bold);
		width += 1;
	}

	if (!font.antialias)
	{
		for (size_t i = 0; i < bitmap.size(); ++i)
			bitmap[i] = bitmap[i] >= 128 ? 255 : 0;
	}

	out->left = left;
	out->top = top;
	out->width = width;
	out->height = height;
	out->coverage.swap(bitmap);
	return true;
}

std::string FaceFamilyName(const ScaledFont &font)
{
	return font.face ? font.face->familyName : std::string();
}

bool FaceHasGlyph(const ScaledFont &font, uint32_t codePoint)
{
	return font.face && stbtt_FindGlyphIndex(&font.face->info, (int)codePoint) > 0;
}

} // namespace Gdi
} // namespace WebCompat
