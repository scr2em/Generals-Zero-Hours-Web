#!/usr/bin/env python3
"""Run the converter directly: ``python3 tools/zharmy/zharmy.py convert ...`` (same as ``python3 -m zharmy`` in tools/)."""

import os
import sys

if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path.insert(0, os.path.dirname(here))
    from zharmy.cli import main
    sys.exit(main())
