Tests of the read in place mode (see ../INTEGRATION.md).

  direct_test.cpp    the program the backend is tested with (CMake target web_direct_test): mount, lookup,
                     listing, stat, reads, seeks, threads, read only, changed files, memory
  make_fixture.py    writes the folder it reads (pattern files; the big one can be 1.5 GB)
  direct_boot.html   test page and demo: picks a folder, opens it with direct-source.js, boots a build
  run_direct.mjs     Playwright driver for that page (Chromium), with mouse steps and memory sampling
  run_all.sh         all of the above, in order

Booting the real game from a folder, with the starter pack (python3 Content/StarterPack/build_pack.py out
--name StarterPack; copy it to ZeroHour/ next to an empty inizh.big, Generals/ini.big holds any BIG):

  node run_direct.mjs --site build/em-direct/GeneralsMD --folder <dir with ZeroHour and Generals> \
     --script z_generals.js --frames 40 --arg -webdirectstats --steps "c:186,240 w:4 c:700,566 w:25 s:game" --out shots

--mode opfs does the same through a copy into OPFS (the old way) for comparison.
