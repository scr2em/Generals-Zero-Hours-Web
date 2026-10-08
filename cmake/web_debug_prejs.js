// Pre-JS of the RTS_WEB_DEBUG build (runs on the page and in every worker): Chromium keeps only
// 10 stack frames by default, which is too few to see where in the game a wasm trap came from.
Error.stackTraceLimit = 200;
