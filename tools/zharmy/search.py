"""Where the engine looks for model, texture and sound files. One place, used by the converter and the validator.

Models and textures (``GameFileClass::Set_Name`` in W3DFileSystem.cpp, the WW3D2 asset manager and texture loader):

* a ``.w3d`` request is looked up as ``Data/<Language>/Art/W3D/<file>`` first, then ``Art/W3D/<file>``;
  a model name with a dot (``HOUSE.BODY``) loads the file of the part before the dot; animations
  (``HIER.ANIM``) load ``<ANIM>.w3d`` (``WW3DAssetManager::Create_Render_Obj`` / ``Get_HAnim``);
* a texture request ``X.tga`` first tries ``X.dds`` (the DDS loader swaps the extension), then ``X.tga``; a
  request for ``X.dds`` finds only a ``.dds`` file (the TGA fallback opens the name as written). Directories:
  ``Data/<Language>/Art/Textures/`` first, then ``Art/Textures/``;
* the user data folders (``W3D/`` and ``Textures/`` of the profile) are not part of a game or mod.

Sounds (``AudioEventRTS::generateFilename``): the root, the folders and the extension come from
``AudioSettings.ini`` (defaults ``Data\\Audio``, ``Sounds``, ``Tracks``, ``Speech``, ``wav``). A sound effect is
``<root>/Sounds/<name>.wav``, music ``<root>/Tracks/<Filename>``, speech ``<root>/Speech/<Filename>``; in each case
the localized copy ``<folder>/<Language>/<file>`` is used when it exists, before the plain one.
"""

import os
import re

DEFAULT_AUDIO = {"AudioRoot": "Data/Audio", "SoundsFolder": "Sounds", "MusicFolder": "Tracks",
                 "StreamingFolder": "Speech", "SoundsExtension": "wav"}


def audio_settings(vfs):
    """The AudioSettings of a file system (defaults for what the file does not say)."""
    out = dict(DEFAULT_AUDIO)
    if vfs is None:
        return out
    path = "data/ini/audiosettings.ini"
    try:
        if not vfs.exists(path):
            return out
        text = vfs.read(path).decode("latin-1")
    except (OSError, KeyError):
        return out
    for raw in text.split("\n"):
        line = raw.split(";", 1)[0].strip()
        m = re.match(r"^(AudioRoot|SoundsFolder|MusicFolder|StreamingFolder|SoundsExtension)\s*=?\s*(\S+)", line)
        if m:
            out[m.group(1)] = m.group(2).replace("\\", "/").strip("/")
    return out


def _langs(language):
    """The localized folder searched: the given language, English by default, none for ``language == ""``."""
    if language == "":
        return []
    lang = (language or "english").lower()
    return [lang]


def w3d_stem(kind, token):
    """The file stem the engine loads for a Model / Anim reference (None when there is none)."""
    t = token.lower()
    if kind == "Model":
        return t.split(".", 1)[0]
    if kind == "Anim":
        return t.partition(".")[2] if "." in t else None
    return t


def w3d_paths(stem, language=None):
    stem = stem.lower()
    return ["data/%s/art/w3d/%s.w3d" % (lang, stem) for lang in _langs(language)] + ["art/w3d/%s.w3d" % stem]


def texture_paths(token, language=None):
    t = token.lower()
    stem, ext = os.path.splitext(t)
    names = [stem + ".dds"]
    if ext == ".dds":
        pass
    elif ext in ("", ".tga"):
        names.append(stem + ".tga")
    else:
        names.append(t)
    out = []
    for lang in _langs(language):
        out.extend("data/%s/art/textures/%s" % (lang, n) for n in names)
    out.extend("art/textures/%s" % n for n in names)
    return out


def audio_paths(kind, token, language=None, settings=None):
    s = settings or DEFAULT_AUDIO
    root = s["AudioRoot"].lower()
    t = token.lower()
    if kind == "AudioFile":
        folder = "%s/%s" % (root, s["SoundsFolder"].lower())
        file = "%s.%s" % (t, s["SoundsExtension"].lower())
    elif kind == "TrackFile":
        folder = "%s/%s" % (root, s["MusicFolder"].lower())
        file = t
    else:
        folder = "%s/%s" % (root, s["StreamingFolder"].lower())
        file = t
    return ["%s/%s/%s" % (folder, lang, file) for lang in _langs(language)] + ["%s/%s" % (folder, file)]


def asset_paths(kind, token, language=None, settings=None):
    """Candidate paths (priority order) for an asset reference of the given kind."""
    if kind in ("Model", "Anim"):
        stem = w3d_stem(kind, token)
        return w3d_paths(stem, language) if stem else []
    if kind == "Texture":
        return texture_paths(token, language)
    if kind in ("AudioFile", "TrackFile", "SpeechFile"):
        return audio_paths(kind, token, language, settings)
    return []


def first_existing(vfs, paths):
    for p in paths:
        if vfs.exists(p):
            return p
    return None


def language_index(vfs):
    """{'art/w3d/x.w3d': 'data/<language>/art/w3d/x.w3d'} for every localized art file (diagnostics only)."""
    out = {}
    for p in vfs.all_paths():
        if p.startswith("data/") and "/art/" in p:
            parts = p.split("/", 2)
            if len(parts) == 3 and parts[2].startswith("art/"):
                out.setdefault(parts[2], p)
    return out


def other_language_hint(index, kind, token):
    """A localized copy in a language folder other than the one searched, or None."""
    if kind in ("Model", "Anim"):
        stem = w3d_stem(kind, token)
        return index.get("art/w3d/%s.w3d" % stem) if stem else None
    if kind == "Texture":
        stem = os.path.splitext(token.lower())[0]
        return index.get("art/textures/%s.dds" % stem) or index.get("art/textures/%s.tga" % stem)
    return None


def same_stem_other_extension(vfs, token):
    """For a texture request: a file with the same stem but the other extension (the engine does not substitute)."""
    stem = os.path.splitext(token.lower())[0]
    for p in ("art/textures/%s.tga" % stem, "art/textures/%s.dds" % stem):
        if vfs.exists(p):
            return p
    return None


def localized_tails(vfs):
    """Paths of the localized copies of art and audio files with the language folder taken out
    (``data/german/art/w3d/x.w3d`` -> ``art/w3d/x.w3d``, ``data/audio/sounds/german/x.wav`` ->
    ``data/audio/sounds/x.wav``), so a check can accept a file that exists for some language."""
    out = set()
    for p in vfs.all_paths():
        parts = p.split("/")
        if len(parts) > 3 and parts[0] == "data" and parts[2] == "art":
            out.add("/".join(parts[2:]))
        elif len(parts) > 4 and parts[0] == "data" and parts[1] == "audio":
            out.add("/".join(parts[:3] + parts[4:]))
    return out


def exists_any(vfs, plain_paths, tails=None):
    """True when one of the plain paths exists or exists in a localized folder."""
    for p in plain_paths:
        if vfs.exists(p) or (tails is not None and p in tails):
            return True
    return False
