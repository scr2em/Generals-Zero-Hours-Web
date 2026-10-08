"""Makes ``spk``, ``gen`` and ``build_pack`` importable from the tests."""
import os
import sys

TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PACK = os.path.dirname(TOOLS)
for p in (TOOLS, PACK):
    if p not in sys.path:
        sys.path.insert(0, p)
