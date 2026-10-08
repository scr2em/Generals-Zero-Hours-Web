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
** WebAssembly port: printf and scanf for wide strings, and the narrow
** _snprintf family, with the semantics of the Microsoft C runtime.
**
** In the wide printf functions %s is a wide string and %S a narrow one, in
** the narrow functions it is the other way round; %ls / %ws and %hs force the
** type. Narrow strings are Windows-1252. All numeric conversions are handed
** to the C library's snprintf one at a time.
*/
#include "webcompat_internal.h"
#include "charset.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>

using namespace WebCompat;

namespace
{

// Output character conversion.
template <typename OC> OC FromNarrow(unsigned char c);
template <> char FromNarrow<char>(unsigned char c) { return (char)c; }
template <> wchar_t FromNarrow<wchar_t>(unsigned char c) { return (wchar_t)Cp1252ToUtf16(c); }

template <typename OC> OC FromWide(unsigned short c);
template <> char FromWide<char>(unsigned short c)
{
	const int mapped = Utf16ToCp1252(c);
	return mapped < 0 ? '?' : (char)mapped;
}
template <> wchar_t FromWide<wchar_t>(unsigned short c) { return (wchar_t)c; }

// Collects the output; keeps counting after the buffer is full.
template <typename OC> struct Sink
{
	OC *buffer;
	size_t capacity;
	size_t count;

	void Put(OC c)
	{
		if (count < capacity)
			buffer[count] = c;
		++count;
	}
};

struct Spec
{
	bool left, plus, space, alternate, zero;
	int width;
	int precision;
	char lengthModifier; // 0, 'h', 'l' (long), 'L' (long long), 'w', 'I' (pointer sized), 'j', 'q' (long double)
};

template <typename OC> void Pad(Sink<OC> &sink, int count, OC c)
{
	for (int i = 0; i < count; ++i)
		sink.Put(c);
}

// Emits a string argument. `wide` says what the argument points to.
template <typename OC> void EmitString(Sink<OC> &sink, const Spec &spec, const void *argument, bool wide)
{
	size_t length = 0;
	const char *narrow = static_cast<const char *>(argument);
	const wchar_t *wideText = static_cast<const wchar_t *>(argument);
	if (!argument)
	{
		static const char s_null[] = "(null)";
		narrow = s_null;
		wide = false;
		length = 6;
	}
	const size_t limit = spec.precision < 0 ? (size_t)-1 : (size_t)spec.precision;
	if (wide && argument)
	{
		while (length < limit && wideText[length])
			++length;
	}
	else if (argument)
	{
		while (length < limit && narrow[length])
			++length;
	}
	else if (limit < length)
	{
		length = limit;
	}

	const int padding = spec.width > (int)length ? spec.width - (int)length : 0;
	if (!spec.left)
		Pad<OC>(sink, padding, spec.zero ? FromNarrow<OC>('0') : FromNarrow<OC>(' '));
	for (size_t i = 0; i < length; ++i)
		sink.Put(wide ? FromWide<OC>((unsigned short)wideText[i]) : FromNarrow<OC>((unsigned char)narrow[i]));
	if (spec.left)
		Pad<OC>(sink, padding, FromNarrow<OC>(' '));
}

template <typename OC> void EmitChar(Sink<OC> &sink, const Spec &spec, unsigned value, bool wide)
{
	const int padding = spec.width > 1 ? spec.width - 1 : 0;
	if (!spec.left)
		Pad<OC>(sink, padding, FromNarrow<OC>(' '));
	sink.Put(wide ? FromWide<OC>((unsigned short)value) : FromNarrow<OC>((unsigned char)value));
	if (spec.left)
		Pad<OC>(sink, padding, FromNarrow<OC>(' '));
}

// Builds "%<flags><width>.<precision><length><conversion>" for the C library.
void BuildSpec(char *out, const Spec &spec, const char *length, char conversion)
{
	char *p = out;
	*p++ = '%';
	if (spec.left) *p++ = '-';
	if (spec.plus) *p++ = '+';
	if (spec.space) *p++ = ' ';
	if (spec.alternate) *p++ = '#';
	if (spec.zero) *p++ = '0';
	if (spec.width > 0)
		p += sprintf(p, "%d", spec.width > 480 ? 480 : spec.width);
	if (spec.precision >= 0)
		p += sprintf(p, ".%d", spec.precision > 480 ? 480 : spec.precision);
	while (*length)
		*p++ = *length++;
	*p++ = conversion;
	*p = 0;
}

template <typename OC> void EmitFormatted(Sink<OC> &sink, const char *text, int length)
{
	for (int i = 0; i < length; ++i)
		sink.Put(FromNarrow<OC>((unsigned char)text[i]));
}

// The formatter. FC is the format string's character type, OC the output's.
// `wideFormat` says whether %s means a wide string.
template <typename FC, typename OC>
void Format(Sink<OC> &sink, const FC *format, va_list args, bool wideFormat)
{
	for (const FC *p = format; *p;)
	{
		if (*p != '%')
		{
			sink.Put(sizeof(FC) == 1 ? FromNarrow<OC>((unsigned char)*p) : FromWide<OC>((unsigned short)*p));
			++p;
			continue;
		}
		const FC *start = p;
		++p;

		Spec spec = { false, false, false, false, false, 0, -1, 0 };
		for (;; ++p)
		{
			if (*p == '-') spec.left = true;
			else if (*p == '+') spec.plus = true;
			else if (*p == ' ') spec.space = true;
			else if (*p == '#') spec.alternate = true;
			else if (*p == '0') spec.zero = true;
			else break;
		}
		if (*p == '*')
		{
			spec.width = va_arg(args, int);
			if (spec.width < 0)
			{
				spec.left = true;
				spec.width = -spec.width;
			}
			++p;
		}
		else
		{
			while (*p >= '0' && *p <= '9')
				spec.width = spec.width * 10 + (*p++ - '0');
		}
		if (*p == '.')
		{
			++p;
			spec.precision = 0;
			if (*p == '*')
			{
				spec.precision = va_arg(args, int);
				++p;
			}
			else
			{
				while (*p >= '0' && *p <= '9')
					spec.precision = spec.precision * 10 + (*p++ - '0');
			}
		}
		// Length modifiers.
		if (*p == 'h')
		{
			spec.lengthModifier = 'h';
			++p;
			if (*p == 'h')
				++p;
		}
		else if (*p == 'l')
		{
			spec.lengthModifier = 'l';
			++p;
			if (*p == 'l')
			{
				spec.lengthModifier = 'L';
				++p;
			}
		}
		else if (*p == 'L')
		{
			spec.lengthModifier = 'q';
			++p;
		}
		else if (*p == 'w')
		{
			spec.lengthModifier = 'w';
			++p;
		}
		else if (*p == 'z' || *p == 't')
		{
			spec.lengthModifier = 'I';
			++p;
		}
		else if (*p == 'j')
		{
			spec.lengthModifier = 'L';
			++p;
		}
		else if (*p == 'I')
		{
			++p;
			if (p[0] == '6' && p[1] == '4')
			{
				spec.lengthModifier = 'L';
				p += 2;
			}
			else if (p[0] == '3' && p[1] == '2')
			{
				p += 2;
			}
			else
			{
				spec.lengthModifier = 'I';
			}
		}

		const FC conversion = *p;
		if (!conversion)
		{
			// A format that ends in the middle of a specification.
			break;
		}
		++p;
		if (spec.left)
			spec.zero = false;

		char cSpec[48];
		char text[560];
		int length;
		switch (conversion)
		{
		case '%':
			sink.Put(FromNarrow<OC>('%'));
			break;

		case 'd':
		case 'i':
		{
			if (spec.precision >= 0)
				spec.zero = false;
			if (spec.lengthModifier == 'L')
			{
				BuildSpec(cSpec, spec, "ll", 'd');
				length = snprintf(text, sizeof(text), cSpec, va_arg(args, long long));
			}
			else
			{
				BuildSpec(cSpec, spec, spec.lengthModifier == 'h' ? "h" : "", 'd');
				length = snprintf(text, sizeof(text), cSpec, va_arg(args, int));
			}
			EmitFormatted(sink, text, length < (int)sizeof(text) ? length : (int)sizeof(text) - 1);
			break;
		}

		case 'u':
		case 'x':
		case 'X':
		case 'o':
		{
			if (spec.precision >= 0)
				spec.zero = false;
			if (spec.lengthModifier == 'L')
			{
				BuildSpec(cSpec, spec, "ll", (char)conversion);
				length = snprintf(text, sizeof(text), cSpec, va_arg(args, unsigned long long));
			}
			else
			{
				BuildSpec(cSpec, spec, spec.lengthModifier == 'h' ? "h" : "", (char)conversion);
				length = snprintf(text, sizeof(text), cSpec, va_arg(args, unsigned int));
			}
			EmitFormatted(sink, text, length < (int)sizeof(text) ? length : (int)sizeof(text) - 1);
			break;
		}

		case 'f':
		case 'F':
		case 'e':
		case 'E':
		case 'g':
		case 'G':
		case 'a':
		case 'A':
		{
			if (spec.lengthModifier == 'q')
			{
				BuildSpec(cSpec, spec, "L", (char)conversion);
				length = snprintf(text, sizeof(text), cSpec, va_arg(args, long double));
			}
			else
			{
				BuildSpec(cSpec, spec, "", (char)conversion);
				length = snprintf(text, sizeof(text), cSpec, va_arg(args, double));
			}
			EmitFormatted(sink, text, length < (int)sizeof(text) ? length : (int)sizeof(text) - 1);
			break;
		}

		case 'p':
		{
			// Eight upper case hex digits, as on 32-bit Windows.
			Spec pointerSpec = spec;
			pointerSpec.precision = 8;
			pointerSpec.alternate = false;
			pointerSpec.zero = false;
			BuildSpec(cSpec, pointerSpec, "", 'X');
			length = snprintf(text, sizeof(text), cSpec, (unsigned int)(uintptr_t)va_arg(args, void *));
			EmitFormatted(sink, text, length < (int)sizeof(text) ? length : (int)sizeof(text) - 1);
			break;
		}

		case 'c':
		case 'C':
		{
			bool wide = wideFormat ? conversion == 'c' : conversion == 'C';
			if (spec.lengthModifier == 'l' || spec.lengthModifier == 'w')
				wide = true;
			else if (spec.lengthModifier == 'h')
				wide = false;
			EmitChar(sink, spec, va_arg(args, unsigned int), wide);
			break;
		}

		case 's':
		case 'S':
		{
			bool wide = wideFormat ? conversion == 's' : conversion == 'S';
			if (spec.lengthModifier == 'l' || spec.lengthModifier == 'w')
				wide = true;
			else if (spec.lengthModifier == 'h')
				wide = false;
			EmitString(sink, spec, va_arg(args, const void *), wide);
			break;
		}

		case 'n':
			*va_arg(args, int *) = (int)sink.count;
			break;

		default:
			// Not a conversion: print the specification as it was written.
			for (const FC *q = start; q < p; ++q)
				sink.Put(sizeof(FC) == 1 ? FromNarrow<OC>((unsigned char)*q) : FromWide<OC>((unsigned short)*q));
			break;
		}
	}
}

// The Microsoft _snprintf contract: if the output fills the buffer exactly
// there is no terminator, if it does not fit the result is -1.
template <typename FC, typename OC>
int FormatMicrosoft(OC *buffer, size_t count, const FC *format, va_list args, bool wideFormat)
{
	Sink<OC> sink = { buffer, count, 0 };
	Format<FC, OC>(sink, format, args, wideFormat);
	if (sink.count < count)
	{
		buffer[sink.count] = 0;
		return (int)sink.count;
	}
	return sink.count == count ? (int)sink.count : -1;
}

// The C99 contract of swprintf: always terminated, -1 if truncated.
template <typename FC, typename OC>
int FormatC99(OC *buffer, size_t count, const FC *format, va_list args, bool wideFormat)
{
	if (count == 0)
		return -1;
	Sink<OC> sink = { buffer, count - 1, 0 };
	Format<FC, OC>(sink, format, args, wideFormat);
	if (sink.count < count)
	{
		buffer[sink.count] = 0;
		return (int)sink.count;
	}
	buffer[count - 1] = 0;
	return -1;
}

template <typename FC, typename OC>
int FormatCount(const FC *format, va_list args, bool wideFormat)
{
	Sink<OC> sink = { nullptr, 0, 0 };
	Format<FC, OC>(sink, format, args, wideFormat);
	return (int)sink.count;
}

} // namespace

extern "C" {

int _vsnprintf(char *buffer, size_t count, const char *format, va_list args)
{
	return FormatMicrosoft<char, char>(buffer, count, format, args, false);
}

int _snprintf(char *buffer, size_t count, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	const int result = _vsnprintf(buffer, count, format, args);
	va_end(args);
	return result;
}

int _vscprintf(const char *format, va_list args)
{
	return FormatCount<char, char>(format, args, false);
}

int _vsnwprintf(wchar_t *buffer, size_t count, const wchar_t *format, va_list args)
{
	return FormatMicrosoft<wchar_t, wchar_t>(buffer, count, format, args, true);
}

int _snwprintf(wchar_t *buffer, size_t count, const wchar_t *format, ...)
{
	va_list args;
	va_start(args, format);
	const int result = _vsnwprintf(buffer, count, format, args);
	va_end(args);
	return result;
}

int _vscwprintf(const wchar_t *format, va_list args)
{
	return FormatCount<wchar_t, wchar_t>(format, args, true);
}

int vswprintf(wchar_t *buffer, size_t count, const wchar_t *format, va_list args)
{
	return FormatC99<wchar_t, wchar_t>(buffer, count, format, args, true);
}

int swprintf(wchar_t *buffer, size_t count, const wchar_t *format, ...)
{
	va_list args;
	va_start(args, format);
	const int result = vswprintf(buffer, count, format, args);
	va_end(args);
	return result;
}

int vfwprintf(FILE *file, const wchar_t *format, va_list args)
{
	// A narrow stream: write the text as Windows-1252.
	va_list copy;
	va_copy(copy, args);
	const int length = _vscwprintf(format, copy);
	va_end(copy);
	if (length < 0)
		return -1;
	wchar_t *wide = static_cast<wchar_t *>(malloc(((size_t)length + 1) * sizeof(wchar_t)));
	if (!wide)
		return -1;
	vswprintf(wide, (size_t)length + 1, format, args);
	for (int i = 0; i < length; ++i)
	{
		const int mapped = Utf16ToCp1252((unsigned short)wide[i]);
		fputc(mapped < 0 ? '?' : mapped, file);
	}
	free(wide);
	return length;
}

int fwprintf(FILE *file, const wchar_t *format, ...)
{
	va_list args;
	va_start(args, format);
	const int result = vfwprintf(file, format, args);
	va_end(args);
	return result;
}

int vwprintf(const wchar_t *format, va_list args)
{
	return vfwprintf(stdout, format, args);
}

int wprintf(const wchar_t *format, ...)
{
	va_list args;
	va_start(args, format);
	const int result = vfwprintf(stdout, format, args);
	va_end(args);
	return result;
}

/* ---------------------------------------------------------------------------
** scanf
** ------------------------------------------------------------------------- */

// Reads %d %i %u %x %o %f %e %g %s %c %[...] %n and literals from a wide string.
int vswscanf(const wchar_t *input, const wchar_t *format, va_list args)
{
	const wchar_t *in = input;
	int assigned = 0;
	bool inputFailed = false;

	for (const wchar_t *f = format; *f;)
	{
		if (iswspace((wint_t)(unsigned short)*f))
		{
			while (iswspace((wint_t)(unsigned short)*in))
				++in;
			++f;
			continue;
		}
		if (*f != '%')
		{
			if (*in != *f)
				return (!*in && assigned == 0) ? -1 : assigned;
			++in;
			++f;
			continue;
		}
		++f;
		if (*f == '%')
		{
			while (iswspace((wint_t)(unsigned short)*in))
				++in;
			if (*in != '%')
				return assigned;
			++in;
			++f;
			continue;
		}
		const bool suppress = *f == '*';
		if (suppress)
			++f;
		int width = 0;
		while (*f >= '0' && *f <= '9')
			width = width * 10 + (*f++ - '0');
		char modifier = 0;
		if (*f == 'h' || *f == 'l' || *f == 'L')
		{
			modifier = (char)*f++;
			if (modifier == 'l' && *f == 'l')
			{
				modifier = 'L';
				++f;
			}
		}
		const wchar_t conversion = *f++;
		if (!conversion)
			break;

		if (conversion != 'c' && conversion != '[' && conversion != 'n')
		{
			while (iswspace((wint_t)(unsigned short)*in))
				++in;
		}
		if (!*in && conversion != 'n')
			return assigned == 0 ? -1 : assigned;
		(void)inputFailed;

		switch (conversion)
		{
		case 'd':
		case 'i':
		case 'u':
		case 'x':
		case 'X':
		case 'o':
		{
			const int base = conversion == 'x' || conversion == 'X' ? 16 : conversion == 'o' ? 8 : conversion == 'i' ? 0 : 10;
			wchar_t limited[72];
			int n = 0;
			while (in[n] && n < 70 && (width == 0 || n < width))
			{
				limited[n] = in[n];
				++n;
			}
			limited[n] = 0;
			wchar_t *end;
			const long long value = (conversion == 'u' || base == 16 || base == 8)
				? (long long)wcstoull(limited, &end, base)
				: wcstoll(limited, &end, base);
			if (end == limited)
				return assigned;
			in += end - limited;
			if (!suppress)
			{
				if (modifier == 'L')
					*va_arg(args, long long *) = value;
				else if (modifier == 'h')
					*va_arg(args, short *) = (short)value;
				else
					*va_arg(args, int *) = (int)value;
				++assigned;
			}
			break;
		}
		case 'f':
		case 'e':
		case 'E':
		case 'g':
		case 'G':
		{
			wchar_t limited[72];
			int n = 0;
			while (in[n] && n < 70 && (width == 0 || n < width))
			{
				limited[n] = in[n];
				++n;
			}
			limited[n] = 0;
			wchar_t *end;
			const double value = wcstod(limited, &end);
			if (end == limited)
				return assigned;
			in += end - limited;
			if (!suppress)
			{
				if (modifier == 'l' || modifier == 'L')
					*va_arg(args, double *) = value;
				else
					*va_arg(args, float *) = (float)value;
				++assigned;
			}
			break;
		}
		case 's':
		{
			wchar_t *out = suppress ? nullptr : va_arg(args, wchar_t *);
			int n = 0;
			while (*in && !iswspace((wint_t)(unsigned short)*in) && (width == 0 || n < width))
			{
				if (out)
					out[n] = *in;
				++n;
				++in;
			}
			if (out)
			{
				out[n] = 0;
				++assigned;
			}
			break;
		}
		case 'c':
		{
			wchar_t *out = suppress ? nullptr : va_arg(args, wchar_t *);
			const int count = width ? width : 1;
			for (int i = 0; i < count; ++i)
			{
				if (!*in)
					return assigned == 0 ? -1 : assigned;
				if (out)
					out[i] = *in;
				++in;
			}
			if (out)
				++assigned;
			break;
		}
		case '[':
		{
			bool negate = false;
			if (*f == '^')
			{
				negate = true;
				++f;
			}
			const wchar_t *setStart = f;
			if (*f == ']')
				++f;
			while (*f && *f != ']')
				++f;
			const wchar_t *setEnd = f;
			if (*f == ']')
				++f;
			wchar_t *out = suppress ? nullptr : va_arg(args, wchar_t *);
			int n = 0;
			while (*in && (width == 0 || n < width))
			{
				bool inSet = false;
				for (const wchar_t *s = setStart; s < setEnd; ++s)
				{
					if (s + 2 < setEnd && s[1] == '-')
					{
						if (*in >= s[0] && *in <= s[2])
							inSet = true;
						s += 2;
					}
					else if (*s == *in)
					{
						inSet = true;
					}
				}
				if (inSet == negate)
					break;
				if (out)
					out[n] = *in;
				++n;
				++in;
			}
			if (n == 0)
				return assigned;
			if (out)
			{
				out[n] = 0;
				++assigned;
			}
			break;
		}
		case 'n':
			if (!suppress)
				*va_arg(args, int *) = (int)(in - input);
			break;
		default:
			return assigned;
		}
	}
	return assigned;
}

int swscanf(const wchar_t *input, const wchar_t *format, ...)
{
	va_list args;
	va_start(args, format);
	const int result = vswscanf(input, format, args);
	va_end(args);
	return result;
}

/* ---------------------------------------------------------------------------
** Time
** ------------------------------------------------------------------------- */

size_t wcsftime(wchar_t *buffer, size_t maxsize, const wchar_t *format, const struct tm *tm)
{
	// Format with the narrow function; the conversion specifiers are ASCII.
	char narrowFormat[256];
	size_t length = 0;
	for (; format[length] && length + 1 < sizeof(narrowFormat); ++length)
	{
		const int mapped = Utf16ToCp1252((unsigned short)format[length]);
		narrowFormat[length] = mapped < 0 ? '?' : (char)mapped;
	}
	narrowFormat[length] = 0;
	char narrowOutput[512];
	const size_t written = strftime(narrowOutput, sizeof(narrowOutput), narrowFormat, tm);
	if (written + 1 > maxsize)
		return 0;
	for (size_t i = 0; i < written; ++i)
		buffer[i] = (wchar_t)Cp1252ToUtf16((unsigned char)narrowOutput[i]);
	buffer[written] = 0;
	return written;
}

wchar_t *_wfullpath(wchar_t *absPath, const wchar_t *relPath, size_t maxLength)
{
	char narrow[MAX_PATH * 3];
	if (WideCharToMultiByte(CP_UTF8, 0, relPath, -1, narrow, sizeof(narrow), nullptr, nullptr) == 0)
		return nullptr;
	char full[PATH_MAX];
	const DWORD length = GetFullPathNameA(narrow, sizeof(full), full, nullptr);
	if (length == 0 || length >= sizeof(full))
		return nullptr;
	wchar_t local[PATH_MAX];
	const int wideLength = MultiByteToWideChar(CP_UTF8, 0, full, -1, local, PATH_MAX);
	if (wideLength == 0)
		return nullptr;
	if (!absPath)
		return _wcsdup(local);
	if ((size_t)wideLength > maxLength)
		return nullptr;
	memcpy(absPath, local, (size_t)wideLength * sizeof(wchar_t));
	return absPath;
}

} // extern "C"
