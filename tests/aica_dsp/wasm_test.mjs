/*
	aica.dsp.wasm: aica_dsp_core.wasm, loaded by its loader, against the core compiled for this machine.

	    node wasm_test.mjs <aica_dsp_core.mjs> <aica_dsp_core.wasm> <trace>

	The trace is what aica_dsp_wasm_trace (wasm_trace.cpp) did to the native core through the C interface, call for
	call, with what the core answered. The same calls are made here, through the loader's object, and every answer
	has to be the same: what the CPU and a debugger read of a register, and the digests of the state blob, of the
	wave RAM and of the views of every single step.
*/
import { readFileSync } from "node:fs";
import { pathToFileURL } from "node:url";

const T = {
	REG_WRITE: 1, REG_POKE: 2, RAM_FILL: 3, INPUTS: 4, STEPS: 5, RUN_SAMPLES: 6, RUN: 7, BUS_READ: 8, CHANNEL_FETCH: 9,
	SWEEP_BEGIN: 10, MIXS_SEND: 11, MIXS_SEND_LATE: 12, SAVE_LOAD: 13, SET: 14, EXTS: 15, CHECK: 16, REG_READ: 17, GET: 18,
	RESET: 19, END: 20,
};

const [loaderPath, wasmPath, tracePath] = process.argv.slice(2);

if (!tracePath)
{
	console.error("node wasm_test.mjs <aica_dsp_core.mjs> <aica_dsp_core.wasm> <trace>");
	process.exit(2);
}

const { loadAicaDsp, VIEW_WORDS } = await import(pathToFileURL(loaderPath).href);
const module = await loadAicaDsp(readFileSync(wasmPath));

const traceBytes = readFileSync(tracePath);
const trace = new Uint32Array(traceBytes.buffer, traceBytes.byteOffset, traceBytes.byteLength >> 2);

if (trace[0] !== 0x52544441 || trace[1] !== 1)
{
	console.error(`${tracePath} is not a trace of this test`);
	process.exit(2);
}

function fnv(hash, bytes)
{
	for (let i = 0; i < bytes.length; i++)
		hash = Math.imul(hash ^ bytes[i], 16777619) >>> 0;

	return hash;
}

const FNV_START = 2166136261;
const dsp = module.create(trace[2]);

//a second core that is used in between: the first one must not notice
const other = module.create(trace[2]);

let at = 3;
let viewDigest = FNV_START;
let answers = 0, wrong = 0, operations = 0;

function expect(what, got, want)
{
	answers++;

	if ((got >>> 0) !== (want >>> 0))
	{
		if (wrong++ < 10)
			console.log(`operation ${operations}: ${what} is ${(got >>> 0).toString(16)} in the module, ${(want >>> 0).toString(16)} in the native core`);
	}
}

for (let done = false; !done; operations++)
{
	const op = trace[at++];

	switch (op)
	{
	case T.REG_WRITE: dsp.writeReg(trace[at], trace[at + 1]); at += 2; break;
	case T.REG_POKE: dsp.pokeReg(trace[at], trace[at + 1]); at += 2; break;

	case T.RAM_FILL:
	{
		const ram = dsp.ram;
		let x = trace[at++];

		for (let i = 0; i < ram.length; i++)
		{
			x ^= x << 13;
			x ^= x >>> 17;
			x ^= x << 5;
			ram[i] = x & 0xFF;
		}

		break;
	}

	case T.INPUTS:
	{
		const [on, sent, low, high] = trace.subarray(at, at + 4);

		at += 4;
		dsp.inputsMixs.set(new Int32Array(trace.buffer, trace.byteOffset + 4 * at, 16));
		at += 16;
		dsp.inputsFetchWords.set(trace.subarray(at, at + 64));
		at += 64;
		dsp.setInputs({ on: on !== 0, sent, fetch: [low, high] });
		break;
	}

	case T.STEPS:
	{
		const count = trace[at++];

		for (let i = 0; i < count; i++)
		{
			dsp.step();

			const view = dsp.view;

			viewDigest = fnv(viewDigest, new Uint8Array(view.buffer, view.byteOffset, 4 * VIEW_WORDS));

			//the other core runs in between
			if ((i & 31) === 0)
				other.step();
		}

		break;
	}

	case T.RUN_SAMPLES:
	{
		const count = trace[at++];

		for (let i = 0; i < count; i++)
		{
			dsp.runSample();
			other.runSample();
		}

		break;
	}

	case T.RUN:
	{
		const count = trace[at++];
		const stops = new Uint8Array(128);

		for (let i = 0; i < 128; i++)
			stops[i] = (trace[at + (i >> 5)] >>> (i & 31)) & 1;

		at += 4;
		expect("the number of steps to the breakpoint", dsp.run(count, stops), trace[at++]);
		break;
	}

	case T.BUS_READ: dsp.busRead(trace[at++]); break;
	case T.CHANNEL_FETCH: dsp.channelFetch(trace[at++]); break;
	case T.SWEEP_BEGIN: dsp.sweepBegin(); break;
	case T.MIXS_SEND: dsp.mixsSend(trace[at], trace[at + 1] | 0); at += 2; break;
	case T.MIXS_SEND_LATE: dsp.mixsSendLate(trace[at], trace[at + 1] | 0); at += 2; break;

	case T.SAVE_LOAD:
	{
		const blob = dsp.saveState();

		dsp.reset();
		expect("LoadState of its own state", dsp.loadState(blob) ? 1 : 0, 1);
		expect("LoadState of a cut state", dsp.loadState(blob.subarray(0, blob.length - 1)) ? 1 : 0, 0);
		break;
	}

	case T.SET: dsp.set(trace[at], trace[at + 1] | 0); at += 2; break;
	case T.EXTS: dsp.exts[trace[at]] = trace[at + 1]; at += 2; break;

	case T.CHECK:
		expect("the digest of the state", fnv(FNV_START, dsp.saveState()), trace[at++]);
		expect("the digest of the wave RAM", fnv(FNV_START, dsp.ram), trace[at++]);
		expect("the digest of the step views", viewDigest, trace[at++]);
		viewDigest = FNV_START;
		break;

	case T.REG_READ:
		expect(`the CPU's read of ${trace[at].toString(16)}`, dsp.readReg(trace[at]), trace[at + 1]);
		expect(`a debugger's read of ${trace[at].toString(16)}`, dsp.peekReg(trace[at]), trace[at + 2]);
		at += 3;
		break;

	case T.GET:
		expect(`register ${trace[at]}`, dsp.get(trace[at]), trace[at + 1]);
		at += 2;
		break;

	case T.RESET:
		dsp.reset();
		other.pokeReg(0x3400 + 16 * (operations & 127), operations);
		break;

	case T.END: done = true; break;

	default:
		console.error(`operation ${op} at word ${at - 1} of the trace is none of this test's`);
		process.exit(2);
	}
}

//the helpers of the module
const fields = module.decode([0xFFFE, 0xFFFE, 0xFFFF, 0xFF80]);

expect("a decoded TRA", fields.TRA, 0x7F);
expect("a decoded MASA", fields.MASA, 0x3F);
expect("a decoded SHIFT", fields.SHIFT, 3);
expect("an encoded row", module.encode(fields).reduce((sum, word) => sum + word, 0), 0xFFFE + 0xFFFE + 0xFFFF + 0xFF80);
expect("pack(-1)", module.pack(-1), 0xE7FF);
expect("unpack(0xFFFF)", module.unpack(0xFFFF), -1);

dsp.destroy();
other.destroy();

console.log(`${operations} operations replayed, ${answers} answers compared, ${wrong} differ`);
console.log(wrong ? "aica.dsp.wasm: FAIL" : "aica.dsp.wasm: PASS");
process.exit(wrong ? 1 : 0);
