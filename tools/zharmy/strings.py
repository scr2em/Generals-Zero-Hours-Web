"""Game string tables: compiled ``.csf`` (reader) and text ``.str`` (reader and writer).

Both layouts come from ``GameTextManager`` (Core/GameEngine/Source/GameClient/GameText.cpp).

CSF::

    header  " FSC" version, numLabels, numStrings, skip, language          (five little endian int32 after the tag)
    label   " LBL" numStrings, labelLength, label bytes
    string  " RTS" | "WRTS", length, UTF-16LE characters with every bit inverted
            (``WRTS`` is followed by int32 speech name length and the name)

STR::

    // comment
    LABEL:Name
    "text, \\n for line breaks"
    END

The engine reads the STR file as single byte characters, so only Latin-1 text can be written.
"""

import struct

from .vfs import norm


class StringsError(ValueError):
    pass


def parse_csf(data):
    """Returns ``{label: text}`` in file order (the first string of each label, like the engine)."""
    if data[:4] != b" FSC":
        raise StringsError("not a CSF file")
    pos = 24
    out = {}
    n = len(data)
    while pos + 12 <= n:
        if data[pos:pos + 4] != b" LBL":
            raise StringsError("expected a label at offset %d" % pos)
        count, length = struct.unpack_from("<ii", data, pos + 4)
        pos += 12
        label = data[pos:pos + length].decode("latin-1")
        pos += length
        first = None
        for i in range(count):
            tag = data[pos:pos + 4]
            if tag not in (b" RTS", b"WRTS"):
                raise StringsError("bad string tag at offset %d" % pos)
            (chars,) = struct.unpack_from("<i", data, pos + 4)
            pos += 8
            raw = data[pos:pos + chars * 2]
            pos += chars * 2
            if i == 0:
                first = bytes(b ^ 0xFF for b in raw).decode("utf-16-le", "replace")
            if tag == b"WRTS":
                (slen,) = struct.unpack_from("<i", data, pos)
                pos += 4 + slen
        if first is not None and label not in out:
            out[label] = first
    return out


def build_csf(strings, language=0):
    """Write ``{label: text}`` as CSF (used by the tests)."""
    out = bytearray(b" FSC")
    out += struct.pack("<iiiii", 3, len(strings), len(strings), 0, language)
    for label, text in strings.items():
        out += b" LBL" + struct.pack("<ii", 1, len(label)) + label.encode("latin-1")
        units = text.encode("utf-16-le")
        out += b" RTS" + struct.pack("<i", len(units) // 2) + bytes(b ^ 0xFF for b in units)
    return bytes(out)


def parse_str(data, duplicates=None):
    """Parse STR bytes. Returns ``{label: text}`` in file order; repeated labels are listed in ``duplicates``."""
    lines = data.decode("latin-1").replace("\r\n", "\n").replace("\r", "\n").split("\n")
    out = {}
    i = 0
    while i < len(lines):
        label = lines[i].strip()
        i += 1
        if not label or label.startswith("//"):
            continue
        value = None
        while i < len(lines):
            line = lines[i].strip()
            i += 1
            if line.upper() == "END":
                break
            if line.startswith('"') and value is None:
                body = []
                buf = line[1:]
                slash = False
                while True:
                    done = False
                    for ch in buf:
                        if ch == "\\" and not slash:
                            slash = True
                            body.append(ch)
                            continue
                        if ch == '"' and not slash:
                            done = True
                            break
                        slash = False
                        body.append(ch)
                    if done or i >= len(lines):
                        break
                    body.append("\n")
                    buf = lines[i]
                    i += 1
                value = _unescape("".join(body))
        if value is None:
            value = ""
        if label in out and duplicates is not None:
            duplicates.append(label)
        out.setdefault(label, value)
    return out


def _unescape(raw):
    out = []
    slash = False
    for ch in raw:
        if slash:
            slash = False
            out.append({"n": "\n", "t": "\t"}.get(ch, ch))
        elif ch == "\\":
            slash = True
        else:
            out.append(ch)
    return "".join(out)


def _escape(text):
    out = []
    for ch in text:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\t":
            out.append("\\t")
        elif ch == "\r":
            continue
        else:
            out.append(ch)
    return "".join(out)


def to_latin1(text):
    """Return (text restricted to Latin-1, number of characters replaced by '?')."""
    out = []
    bad = 0
    for ch in text:
        if ord(ch) < 256:
            out.append(ch)
        else:
            out.append("?")
            bad += 1
    return "".join(out), bad


def build_str(strings):
    """Write ``{label: text}`` as STR bytes, sorted by label (case-insensitive) for deterministic output."""
    lines = []
    for label in sorted(strings, key=lambda s: (s.lower(), s)):
        text, _bad = to_latin1(strings[label])
        lines.append(label)
        lines.append('"%s"' % _escape(text))
        lines.append("END")
        lines.append("")
    return "\n".join(lines).encode("latin-1")


class StringTable:
    """The strings a game (a VFS) would load. ``source`` says where they came from."""

    def __init__(self, entries=None, source=None, language=None):
        self.entries = entries or {}
        self.source = source
        self.language = language
        self._lower = {k.lower(): k for k in self.entries}

    def get(self, label):
        key = self._lower.get(label.lower())
        return None if key is None else self.entries[key]

    def has(self, label):
        return label.lower() in self._lower

    def key(self, label):
        return self._lower.get(label.lower())


def table_languages(vfs):
    """{language: path of its table} for the Data/<Language>/Generals.csf (preferred) and .str files of a VFS."""
    csf, strf = {}, {}
    for path in vfs.all_paths():
        parts = path.split("/")
        if len(parts) == 3 and parts[0] == "data":
            if parts[2] == "generals.csf":
                csf[parts[1]] = path
            elif parts[2] == "generals.str":
                strf[parts[1]] = path
    out = dict(strf)
    out.update(csf)
    return out


def mod_languages(mod_vfs, base_vfs):
    """Languages whose string table the mod provides: a table the ruleset lacks or that differs from the ruleset's."""
    out = []
    for lang, path in sorted(table_languages(mod_vfs).items()):
        if base_vfs is None or not base_vfs.exists(path) or not mod_vfs.same_file(base_vfs, path):
            out.append(lang)
    return out


def choose_language(mod_vfs, base_vfs, explicit=None):
    """(language or None, why). ``explicit`` (``--language``) wins. Otherwise the language of the mod's own string
    table: English when the mod ships an English one, else its only (or alphabetically first) language; a mod that
    ships no table of its own uses the game's (English preferred)."""
    have = table_languages(mod_vfs)
    if explicit:
        lang = norm(explicit)
        if lang in have:
            return lang, "--language %s" % lang
        return lang, "--language %s (but no string table for it was found; found: %s)" % (
            lang, ", ".join(sorted(have)) or "none")
    if mod_vfs.exists("Data/Generals.str"):
        return None, "Data/Generals.str (language independent)"
    own = mod_languages(mod_vfs, base_vfs)
    if own:
        if "english" in own:
            return "english", "the mod provides an English string table"
        if len(own) == 1:
            return own[0], "the mod provides only a %s string table (no English one)" % own[0]
        return own[0], "the mod provides string tables for %s; using %s (use --language to choose)" % (
            ", ".join(own), own[0])
    if "english" in have:
        return "english", "the mod has no string table of its own; the game's English one"
    if have:
        lang = sorted(have)[0]
        return lang, "the game only has a %s string table" % lang
    return None, "no string table found"


def load_strings(vfs, language=None):
    """Find and parse the string table of a VFS the way the engine does.

    ``Data/Generals.str`` is tried first (release builds), then ``Data/<Language>/Generals.csf``. If no language
    is given the first one found is used, English preferred.
    """
    if vfs.exists("Data/Generals.str"):
        return StringTable(parse_str(vfs.read("Data/Generals.str")), "Data/Generals.str", language)
    langs = []
    for path in vfs.all_paths():
        parts = path.split("/")
        if len(parts) == 3 and parts[0] == "data" and parts[2] == "generals.csf":
            langs.append(parts[1])
    if not langs:
        for path in vfs.all_paths():
            parts = path.split("/")
            if len(parts) == 3 and parts[0] == "data" and parts[2] == "generals.str":
                langs.append(parts[1])
        if not langs:
            return StringTable({}, None, language)
        chosen = norm(language) if language and norm(language) in langs else ("english" if "english" in langs
                                                                              else sorted(langs)[0])
        return StringTable(parse_str(vfs.read("Data/%s/generals.str" % chosen)),
                           "Data/%s/Generals.str" % chosen, chosen)
    chosen = norm(language) if language and norm(language) in langs else ("english" if "english" in langs
                                                                          else sorted(langs)[0])
    return StringTable(parse_csf(vfs.read("Data/%s/generals.csf" % chosen)), "Data/%s/Generals.csf" % chosen, chosen)
