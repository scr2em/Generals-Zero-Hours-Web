#!/usr/bin/env bash
# Assembles the static web site (GitHub Pages) from a web build of Zero Hour.
#
#   scripts/web/assemble_pages.sh <build dir> <output dir>
#
# <build dir> is a configured and built `emscripten` preset build directory
# (targets z_generals and starter_pack). The output directory is replaced.
#
# GitHub Pages cannot send the COOP/COEP headers the game needs for
# SharedArrayBuffer; the launcher registers coi-serviceworker.js, which adds
# them on the first visit (the page reloads once).
set -euo pipefail

if [ $# -ne 2 ]; then
    echo "usage: $0 <build dir> <output dir>" >&2
    exit 2
fi

site_src="$1/GeneralsMD"
out="$2"

for f in z_generals.html z_generals.js z_generals.wasm importer.js armies.js armyimport.js armyimport-ui.js armyimport-worker.js armylibrary.js coi-serviceworker.js; do
    if [ ! -f "$site_src/$f" ]; then
        echo "error: $site_src/$f is missing; build the z_generals target first" >&2
        exit 1
    fi
done
if [ ! -f "$site_src/starterpack/manifest.json" ]; then
    echo "error: $site_src/starterpack is missing; build the starter_pack target first" >&2
    exit 1
fi

rm -rf "$out"
mkdir -p "$out"

# The page, the engine and the launcher's scripts.
cp "$site_src/z_generals.js" "$site_src/z_generals.wasm" "$site_src/importer.js" "$site_src/armies.js" "$site_src/coi-serviceworker.js" "$out/"
# Importing armies from a mod in the browser: the page's scripts, the converter (zharmy.zip) and Pyodide (pyodide/).
cp "$site_src/armyimport.js" "$site_src/armyimport-ui.js" "$site_src/armyimport-worker.js" "$site_src/armylibrary.js" "$out/"
if [ -f "$site_src/zharmy.zip" ] && [ -f "$site_src/pyodide/pyodide.asm.wasm" ]; then
    cp "$site_src/zharmy.zip" "$out/"
    cp -R "$site_src/pyodide" "$out/pyodide"
else
    echo "note: no converter in $site_src (configured with -DRTS_WEB_PYODIDE=OFF?): the launcher will not import armies from a mod" >&2
fi
for f in direct-source.js zhnet.js; do
    if [ -f "$site_src/$f" ]; then cp "$site_src/$f" "$out/"; fi
done
# Any other module files emitted next to the engine (e.g. worker or data files).
for f in "$site_src"/z_generals.*; do
    case "$f" in
        *.html) ;;
        *) cp "$f" "$out/" ;;
    esac
done

# The free starter content (original, GPL-3.0-or-later).
cp -R "$site_src/starterpack" "$out/starterpack"

# Multiplayer needs a signaling server, which a static host cannot run: hide
# the "Play with friends" section on this site.
python3 - "$site_src/z_generals.html" "$out/index.html" <<'EOF'
import sys
src, dst = sys.argv[1], sys.argv[2]
page = open(src, encoding='utf-8').read()
hide = '<style>#choice-net{display:none!important}</style>\n'
if '</head>' not in page:
    sys.exit('error: no </head> in ' + src)
page = page.replace('</head>', hide + '</head>', 1)
open(dst, 'w', encoding='utf-8').write(page)
EOF
cp "$out/index.html" "$out/z_generals.html"

# Serve files as they are (no Jekyll processing).
touch "$out/.nojekyll"

echo "Site assembled in $out:"
du -sh "$out"
