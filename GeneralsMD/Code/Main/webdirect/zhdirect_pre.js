// "Read in place" mode, part 1 of 3: moving the player's File objects to the engine's threads.
// Linked into the game with --pre-js (see GeneralsMD/Code/Main/CMakeLists.txt); runs on the browser
// main thread and in every pthread worker, in the scope of the emscripten module.
//
// The page (direct-source.js) holds File objects of the files the player picked. A File is
// structured-cloneable, so it can be posted to a worker, and a worker can read it synchronously
// with FileReaderSync. That is what the WasmFS backend in library_zhdirect.js does. The engine runs
// on a pthread whose worker is taken from the module's pool, so the files are posted to every
// worker of the pool (a File is only a handle, cloning it is cheap) before the engine thread
// starts: messages to one worker are handled in order, so the engine thread has them when its
// 'run' message arrives. No file data is copied.
//
// Main thread API (called by direct-source.js from Module.preRun):
//   Module.zhDirectAttach(payload)   payload = { version, entries: [{ path, file }], options }
//
// Workers keep the payload in Module.zhDirect; WebStorage.cpp mounts the direct tree when it is set.

if (typeof ENVIRONMENT_IS_PTHREAD !== 'undefined' && ENVIRONMENT_IS_PTHREAD) {
  // A plain listener next to emscripten's own onmessage handler (which ignores messages without 'cmd').
  self.addEventListener('message', function (e) {
    var data = e.data;
    if (data && data.zhDirect) {
      Module['zhDirect'] = data.zhDirect;
    }
  });
} else if (typeof ENVIRONMENT_IS_WORKER === 'undefined' || !ENVIRONMENT_IS_WORKER) {
  Module['zhDirectAttach'] = function (payload) {
    var message = { zhDirect: payload };
    var post = function (worker) {
      try { worker.postMessage(message); } catch (error) { console.error('zhdirect: cannot post the file list to a worker', error); }
    };
    var pool = [];
    for (var id in PThread.pthreads) pool.push(PThread.pthreads[id]);
    PThread.unusedWorkers.concat(pool).forEach(post);
    // Workers that the runtime adds later (when the pool is used up) get the files, too.
    if (!PThread.__zhDirectWrapped) {
      PThread.__zhDirectWrapped = true;
      var allocate = PThread.allocateUnusedWorker;
      PThread.allocateUnusedWorker = function () {
        var worker = allocate.apply(this, arguments);
        post(worker);
        return worker;
      };
    }
    Module['zhDirectAttached'] = payload.entries.length;
  };
}
