// "Read in place" mode, part 2 of 3: a read-only WasmFS backend that serves the player's own File
// objects (picked with showDirectoryPicker, see direct-source.js) to the engine, synchronously.
// Linked into the game with --js-library (see GeneralsMD/Code/Main/CMakeLists.txt); the C++ side is
// WebStorage.cpp (WebPlatform_MountStorage).
//
// How it works
//   * WasmFS' JSImpl backend (wasmfs_create_jsimpl_backend) lets JavaScript answer read/getSize for the
//     files of a mount. WebStorage.cpp creates one empty file per picked file (names in lower case) in
//     that backend; allocFile() below learns which File object belongs to which WasmFS file.
//   * read() runs on the thread that calls read(2): the engine, or the audio stream thread. It reads the
//     File with FileReaderSync (allowed in dedicated workers, which pthreads are) and copies the bytes
//     into the wasm heap. Nothing but the requested blocks is ever read: memory use is bounded by the
//     block cache (options.cacheBytes, 64 MB for the engine thread, options.threadCacheBytes, 16 MB, for each other thread), whatever the size of the game's archives.
//   * Every thread has its own JS realm. The File objects reach all of them (zhdirect_pre.js) and the
//     file <-> WasmFS file association is kept in a table in the shared wasm heap (C++ allocates it,
//     see zh_direct_header in WebStorage.cpp), so any thread can serve any file.
//   * The files are read-only: writes fail with EROFS. (The engine never writes into its install; the
//     user data lives in OPFS, see WebStorage.cpp.)
//
// Read cache
//   FileReaderSync has a high fixed cost per call (a synchronous round trip to the browser process,
//   about 0.2 ms to several ms depending on the machine), while the game reads archives in many small
//   pieces (directories, small assets). Reads are therefore served from segments: block aligned pieces
//   of a file, fetched with one FileReaderSync call and kept in an LRU. A segment is one block
//   (options.blockSize) for random access and grows (doubling, up to options.maxSegment) while the
//   file is read sequentially, so streaming music or the archive directories cost few calls. Reads of
//   options.directThreshold bytes or more skip the cache and go straight from the file to the heap.

addToLibrary({
  $zhDirect__deps: ['$wasmFS$backends'],
  $zhDirect: {
    // ---- per realm (thread) state
    isMountThread: false, // the thread that created the files (the engine thread): it gets the big cache
    pending: -1,          // index of the entry whose WasmFS file is being created (engine thread, mount time)
    ptrToIndex: null,     // WasmFS file pointer -> entry index
    reader: null,         // FileReaderSync
    perFile: null,        // entry index -> { segs: [{start, end, bytes, id}], lastEnd, lastLen }
    lru: null,            // Map id -> seg, oldest first
    nextId: 1,
    cached: 0,
    reported: null,       // entries whose read error was logged
    stats: { reads: 0, hits: 0, fetches: 0, fetchedBytes: 0, directReads: 0, directBytes: 0, evictions: 0, errors: 0 },

    payload: function () {
      return Module['zhDirect'];
    },

    option: function (name, fallback) {
      var o = Module['zhDirect'] && Module['zhDirect'].options;
      var v = o && o[name];
      return (typeof v === 'number' && v > 0) ? v : fallback;
    },

    // The table that associates WasmFS files with entries lives in the shared heap.
    tbl: null,
    table: function () {
      if (!zhDirect.tbl) {
        var header = wasmExports['zh_direct_header']() >> 2;
        zhDirect.tbl = { base: HEAPU32[header] >> 2, count: HEAPU32[header + 1] };
      }
      return zhDirect.tbl;
    },

    createBackend: function () {
      var d = zhDirect;
      return {
        allocFile(file) {
          // Called while WebStorage.cpp creates the files; pending was set by zh_direct_entry.
          if (d.pending >= 0) {
            d.isMountThread = true;
            var t = d.table();
            HEAPU32[t.base + d.pending] = file;
            if (!d.ptrToIndex) d.ptrToIndex = new Map();
            d.ptrToIndex.set(file, d.pending);
            d.pending = -1;
          }
        },
        freeFile(file) {
          var i = d.resolve(file);
          if (i >= 0) {
            HEAPU32[d.table().base + i] = 0;
            d.ptrToIndex.delete(file);
          }
        },
        getSize(file) {
          var i = d.resolve(file);
          return i < 0 ? 0 : d.payload().entries[i].file.size;
        },
        read(file, buffer, length, offset) {
          var i = d.resolve(file);
          return i < 0 ? 0 : d.read(i, buffer, length, offset);
        },
        write(file, buffer, length, offset) {
          return -{{{ cDefs.EROFS }}};
        },
        setSize(file, size) {
          return -{{{ cDefs.EROFS }}};
        },
      };
    },

    // Entry index of a WasmFS file, or -1 for a file the engine created itself (those are empty).
    resolve: function (file) {
      var d = zhDirect;
      var t = d.table();
      if (!d.ptrToIndex) {
        // First use on this thread: learn the files the engine thread created.
        d.ptrToIndex = new Map();
        for (var i = 0; i < t.count; i++) {
          var p = HEAPU32[t.base + i];
          if (p) d.ptrToIndex.set(p, i);
        }
      }
      var index = d.ptrToIndex.get(file);
      // A freed file's slot is cleared; a reused address must not find the old file.
      return (index !== undefined && HEAPU32[t.base + index] === file) ? index : -1;
    },

    state: function (index) {
      var d = zhDirect;
      if (!d.perFile) {
        d.perFile = new Map();
        d.lru = new Map();
        d.reported = new Set();
        d.reader = new FileReaderSync();
      }
      var s = d.perFile.get(index);
      if (!s) d.perFile.set(index, s = { segs: [], lastEnd: -1, lastLen: 0 });
      return s;
    },

    fail: function (index, error) {
      var d = zhDirect;
      d.stats.errors++;
      if (!d.reported.has(index)) {
        d.reported.add(index);
        var entry = d.payload().entries[index];
        err('Cannot read ' + entry.path + ': ' + (error && error.name) + ' ' + (error && error.message) +
          ' (was the file changed or moved on disk after the folder was opened? Open the folder again.)');
      }
      return -{{{ cDefs.EIO }}};
    },

    read: function (index, dest, length, offset) {
      var d = zhDirect;
      var file = d.payload().entries[index].file;
      var size = file.size;
      if (offset >= size || length <= 0) return 0;
      if (offset + length > size) length = size - offset;
      var st = d.state(index);
      d.stats.reads++;
      try {
        if (length >= d.option('directThreshold', 1048576)) {
          // A big read (a whole texture, a movie...): straight into the heap, nothing to cache.
          var whole = new Uint8Array(d.reader.readAsArrayBuffer(file.slice(offset, offset + length)));
          HEAPU8.set(whole, dest);
          d.stats.directReads++;
          d.stats.directBytes += length;
          st.lastEnd = offset + length;
          return length;
        }
        var end = offset + length;
        var done = 0;
        while (done < length) {
          var pos = offset + done;
          var seg = d.find(st, pos);
          if (seg) {
            if (done === 0) d.stats.hits++;
            // touch
            d.lru.delete(seg.id);
            d.lru.set(seg.id, seg);
          } else {
            seg = d.fetch(index, st, file, pos, end, pos === offset && offset === st.lastEnd);
          }
          var n = Math.min(seg.end, end) - pos;
          HEAPU8.set(seg.bytes.subarray(pos - seg.start, pos - seg.start + n), dest + done);
          done += n;
        }
        st.lastEnd = end;
        return length;
      } catch (error) {
        return d.fail(index, error);
      }
    },

    // The segment of st that contains pos, if any (segs are sorted and do not overlap).
    find: function (st, pos) {
      var segs = st.segs;
      var lo = 0, hi = segs.length - 1;
      while (lo <= hi) {
        var mid = (lo + hi) >> 1;
        var s = segs[mid];
        if (pos < s.start) hi = mid - 1;
        else if (pos >= s.end) lo = mid + 1;
        else return s;
      }
      return null;
    },

    fetch: function (index, st, file, pos, wantEnd, sequential) {
      var d = zhDirect;
      var block = d.option('blockSize', 262144);
      var maxSegment = Math.max(block, d.option('maxSegment', 4194304));
      var len = block;
      if (sequential) len = Math.min(maxSegment, Math.max(block, st.lastLen * 2));
      var start = Math.floor(pos / block) * block;
      var end = Math.max(start + len, Math.ceil(Math.min(wantEnd, start + maxSegment) / block) * block);
      end = Math.min(end, file.size);
      // Do not overlap the next cached segment.
      var segs = st.segs;
      var at = 0;
      while (at < segs.length && segs[at].start <= pos) at++;   // first segment after pos
      if (at < segs.length && segs[at].start < end) end = segs[at].start;
      var bytes = new Uint8Array(d.reader.readAsArrayBuffer(file.slice(start, end)));
      d.stats.fetches++;
      d.stats.fetchedBytes += bytes.length;
      st.lastLen = end - start;
      var seg = { start: start, end: end, bytes: bytes, id: d.nextId++, index: index };
      segs.splice(at, 0, seg);
      d.lru.set(seg.id, seg);
      d.cached += bytes.length;
      d.evict(seg);
      return seg;
    },

    evict: function (keep) {
      var d = zhDirect;
      // The engine thread does nearly all reading; the audio stream thread only needs a little.
      var limit = d.isMountThread ? d.option('cacheBytes', 67108864) : d.option('threadCacheBytes', 16777216);
      for (var [id, seg] of d.lru) {
        if (d.cached <= limit) break;
        if (seg === keep) continue;
        d.lru.delete(id);
        d.cached -= seg.bytes.length;
        var segs = d.perFile.get(seg.index).segs;
        segs.splice(segs.indexOf(seg), 1);
        d.stats.evictions++;
      }
    },
  },

  // The wasmFS$backends map is looked up by backend pointer. Only the thread that creates the
  // backend registers it, so every other thread gets one on demand (they can read files, too).
  $zhDirectInstall__deps: ['$wasmFS$backends', '$zhDirect'],
  $zhDirectInstall__postset: 'zhDirectInstall();',
  $zhDirectInstall: () => {
    if (typeof wasmFS$backends === 'undefined') return;
    wasmFS$backends = new Proxy(wasmFS$backends, {
      get(target, key) {
        var value = target[key];
        if (value === undefined && typeof key === 'string' && key !== '' && !isNaN(key) && Module['zhDirect']) {
          value = target[key] = zhDirect.createBackend();
        }
        return value;
      },
    });
  },

  // ---- called by WebStorage.cpp (engine thread, at mount time)

  // 1 when the page handed over a list of files (Module.zhDirect), else 0.
  zh_direct_available__deps: ['$zhDirect', '$zhDirectInstall'],
  zh_direct_available__sig: 'i',
  zh_direct_available: () => {
    var p = zhDirect.payload();
    return (p && p.entries && p.entries.length) ? 1 : 0;
  },

  zh_direct_count__deps: ['$zhDirect'],
  zh_direct_count__sig: 'i',
  zh_direct_count: () => zhDirect.payload().entries.length,

  // Copies the path of entry i (e.g. "game/data/ini/gamedata.ini") to buffer and makes it the entry the
  // next created WasmFS file belongs to. Returns the path length, or -1 when it does not fit,
  // -2 when the file is too big for the JSImpl backend (its getSize is a 32 bit int).
  zh_direct_entry__deps: ['$zhDirect'],
  zh_direct_entry__sig: 'iipi',
  zh_direct_entry: (i, buffer, capacity) => {
    var entry = zhDirect.payload().entries[i];
    if (entry.file.size > 0x7fffffff) return -2;
    var length = lengthBytesUTF8(entry.path);
    if (length + 1 > capacity) return -1;
    stringToUTF8(entry.path, buffer, capacity);
    zhDirect.pending = i;
    return length;
  },

  zh_direct_register_backend__deps: ['$zhDirect', '$wasmFS$backends'],
  zh_direct_register_backend__sig: 'vp',
  zh_direct_register_backend: (backend) => {
    wasmFS$backends[backend] = zhDirect.createBackend();
  },

  // Counters of this thread's reads as JSON (for tests and the log).
  zh_direct_stats__deps: ['$zhDirect'],
  zh_direct_stats__sig: 'ipi',
  zh_direct_stats: (buffer, capacity) => {
    var s = Object.assign({ cachedBytes: zhDirect.cached }, zhDirect.stats);
    var text = JSON.stringify(s);
    if (lengthBytesUTF8(text) + 1 > capacity) return -1;
    stringToUTF8(text, buffer, capacity);
    return lengthBytesUTF8(text);
  },
});
