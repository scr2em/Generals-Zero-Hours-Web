"""StarterPack toolkit: writers for the Zero Hour engine's file formats.

Every module here produces a format the engine parses (BIG archives, TGA
textures, W3D models, WAV audio, STR string tables, WND window layouts,
DataChunk map files, ...). The layouts were derived from the engine's own
parsers in this repository, never from game data.

Everything is deterministic: the same inputs always produce the same bytes.
"""

__all__ = []
