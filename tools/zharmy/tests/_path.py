import os
import sys

TOOLS = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
if TOOLS not in sys.path:
    sys.path.insert(0, TOOLS)
