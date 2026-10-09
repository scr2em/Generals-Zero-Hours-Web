// Pre-JS of every web build (runs on the page and in every worker). When the engine thread dies of an
// uncaught error, for example a wasm trap such as "function signature mismatch", the page is only told the
// message ("worker sent an error!"). The stack, which names the functions, is only known inside the worker:
// print it from there. In a pthread, err() is the page's printErr, so the line reaches the page's log.
if (Error.stackTraceLimit < 60) Error.stackTraceLimit = 60;
if (typeof WorkerGlobalScope != 'undefined' && self instanceof WorkerGlobalScope) {
	self.addEventListener('error', (e) => {
		const stack = e && e.error && e.error.stack;
		if (!stack) return;
		try { err('Engine thread stopped: ' + stack); } catch (_) { console.error(stack); }
	});
}
