// Prints the wasm call stack of every worker thread of a page, over the Chrome DevTools protocol.
// Used by smoke.mjs --stack when the game seems to hang: the stack shows where.
//
//   import { dumpWorkerStacks } from './cdpstack.mjs';
//   console.log(await dumpWorkerStacks(9333));   // browser started with --remote-debugging-port=9333
//
// Pausing a thread this way is only for diagnosis; the page is not usable afterwards.
export async function dumpWorkerStacks(port) {
	const version = await (await fetch(`http://127.0.0.1:${port}/json/version`)).json();
	const ws = new WebSocket(version.webSocketDebuggerUrl);
	await new Promise((resolve, reject) => { ws.onopen = resolve; ws.onerror = reject; });
	let nextId = 1;
	const pending = new Map();
	const paused = new Map(); // sessionId -> callFrames
	const attached = new Map(); // sessionId -> target info
	ws.onmessage = (ev) => {
		const msg = JSON.parse(ev.data);
		if (msg.id && pending.has(msg.id)) { pending.get(msg.id)(msg); pending.delete(msg.id); return; }
		if (msg.method === 'Debugger.paused') paused.set(msg.sessionId, msg.params.callFrames);
	};
	const send = (method, params = {}, sessionId) => new Promise((resolve) => {
		const id = nextId++;
		pending.set(id, resolve);
		ws.send(JSON.stringify({ id, method, params, sessionId }));
	});
	const { result } = await send('Target.getTargets');
	const workers = result.targetInfos.filter((t) => t.type === 'worker' || t.type === 'shared_worker');
	for (const t of workers) {
		const r = await send('Target.attachToTarget', { targetId: t.targetId, flatten: true });
		const sessionId = r.result && r.result.sessionId;
		if (!sessionId) continue;
		attached.set(sessionId, t);
		await send('Debugger.enable', {}, sessionId);
		await send('Debugger.pause', {}, sessionId);
	}
	await new Promise((r) => setTimeout(r, 1500));
	let out = '';
	for (const [sessionId, t] of attached) {
		const frames = paused.get(sessionId);
		out += `--- worker ${t.title || t.url} ${frames ? '' : '(did not pause: idle or blocked in a wait)'}\n`;
		if (frames) for (const f of frames.slice(0, 40)) out += `    ${f.functionName || '(anonymous)'}  ${f.url.split('/').pop()}:${f.location.lineNumber}\n`;
	}
	ws.close();
	return out;
}
