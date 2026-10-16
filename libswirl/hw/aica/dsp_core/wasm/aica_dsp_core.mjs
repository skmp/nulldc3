/*
	This file is part of libswirl

	Loads aica_dsp_core.wasm, the AICA DSP core (../aica_dsp_core.h), and wraps its C interface
	(aica_dsp_capi.h) into an object per core.

	    import { loadAicaDsp } from "./aica_dsp_core.mjs";

	    const module = await loadAicaDsp();          // the .wasm next to this file, or what is passed in
	    const dsp = module.create();                 // a core with 2 MB of wave RAM
	    dsp.writeReg(0x3400, 0x0100);                // as the CPU writes
	    dsp.inputsMixs[0] = sample << 4;             // what a channel sends to bus 0
	    dsp.setInputs({ sent: 1 });
	    dsp.runSample();
	    const out = dsp.efreg[0];

	The arrays (ram, coef, madrs, mpro, temp, mems, mixs, efreg, exts, view, inputsMixs, inputsFetchWords) are
	views of the module's memory: reads and writes go straight to the core. They are made anew when the memory has
	grown, so take them from the object when they are used and do not keep them across a create().
*/

export const AICA_DSP_API_VERSION = 1;

//The indices of get() and set()
export const REG = Object.freeze({
	STEP: 0, SAMPLES: 1, MDEC_CT: 2, RBPL: 3, ACC: 4, Y_REG: 5, FRC_REG: 6, ADRS_REG: 7, LATCH: 8, LATCH_COPY: 9,
	MEMVAL: 10, NOFL_PIPE: 11, MIXS_DSP: 12, MIXS_FILL: 13,
});

//The words of view: what the last step() computed
export const VIEW = Object.freeze({
	step: 0, MDEC_CT: 1, INPUTS: 2, SHIFTED: 3, X: 4, Y: 5, B: 6, ACC: 7, FRC_REG: 8, Y_REG: 9, ADRS_REG: 10,
	memval: 11, memval_nofl: 12, latch: 13, latch_copy: 14, mem_op: 15, mem_addr: 16, mem_data: 17, slot_taken: 18,
	mixs_bank: 19,
});

export const VIEW_WORDS = 20;

//view[VIEW.mem_op]
export const MEM_OP = Object.freeze({ NONE: 0, READ: 1, WRITE: 2, WRITE_NO_READ: 3, LOST: 4 });

//The fields of a step in the order decode() and encode() have them
export const FIELDS = Object.freeze([
	"TRA", "TWT", "TWA", "XSEL", "YSEL", "IRA", "IWT", "IWA", "TABLE", "MWT", "MRD", "EWT", "EWA", "ADRL", "FRCL",
	"SHIFT", "YRL", "NEGB", "ZERO", "BSEL", "NOFL", "MASA", "ADREB", "NXADR",
]);

class AicaDsp
{
	constructor(module, handle)
	{
		this.module = module;
		this.handle = handle;
		this.ramSize = module.exports.aica_dsp_ram_size(handle);
	}

	//Gives the core's memory back. The object and its arrays are not to be used after it
	destroy()
	{
		if (this.stopsAt)
			this.module.exports.aica_dsp_free(this.stopsAt);

		this.module.exports.aica_dsp_destroy(this.handle);
		this.handle = 0;
	}

	reset() { this.module.exports.aica_dsp_reset(this.handle); }

	//As the CPU reads and writes: offsets from the start of the AICA's register area
	readReg(off) { return this.module.exports.aica_dsp_reg_read(this.handle, off) >>> 0; }
	writeReg(off, value) { this.module.exports.aica_dsp_reg_write(this.handle, off, value); }

	//As a debugger does: what the hardware has, also where its CPU reads 0 or cannot write
	peekReg(off) { return this.module.exports.aica_dsp_reg_peek(this.handle, off) >>> 0; }
	pokeReg(off, value) { this.module.exports.aica_dsp_reg_poke(this.handle, off, value); }

	//One step, the rest of the sample, or steps up to a breakpoint: stops is null or 128 bytes, non zero where a
	//step is one. Returns the number of steps that were executed
	step() { this.module.exports.aica_dsp_step(this.handle); }
	runSample() { this.module.exports.aica_dsp_run_sample(this.handle); }

	run(count, stops = null)
	{
		const { exports } = this.module;

		if (!stops)
			return exports.aica_dsp_run(this.handle, count, 0) >>> 0;

		if (!this.stopsAt)
			this.stopsAt = exports.aica_dsp_alloc(128);

		new Uint8Array(this.module.memory.buffer, this.stopsAt, 128).set(stops);

		return exports.aica_dsp_run(this.handle, count, this.stopsAt) >>> 0;
	}

	get(index) { return this.module.exports.aica_dsp_get(this.handle, index); }
	set(index, value) { this.module.exports.aica_dsp_set(this.handle, index, value); }

	get currentStep() { return this.get(REG.STEP); }
	get sampleCounter() { return this.get(REG.SAMPLES) >>> 0; }

	//The inputs of the samples: sent, a bit per bus that inputsMixs has a sum for; fetch, a bit per channel (a
	//BigInt, or its two halves in an array) that fetches and leaves inputsFetchWords[channel]. on: false leaves
	//every event to sweepBegin(), mixsSend(), channelFetch() and busRead()
	setInputs({ on = true, sent = 0, fetch = 0n } = {})
	{
		const [low, high] = Array.isArray(fetch) ? fetch
			: [Number(BigInt(fetch) & 0xFFFFFFFFn), Number((BigInt(fetch) >> 32n) & 0xFFFFFFFFn)];

		this.module.exports.aica_dsp_set_inputs(this.handle, on ? 1 : 0, sent, low, high);
	}

	sweepBegin() { this.module.exports.aica_dsp_sweep_begin(this.handle); }
	mixsSend(bus, value) { this.module.exports.aica_dsp_mixs_send(this.handle, bus, value); }
	mixsSendLate(bus, value) { this.module.exports.aica_dsp_mixs_send_late(this.handle, bus, value); }
	channelFetch(word) { this.module.exports.aica_dsp_channel_fetch(this.handle, word); }
	busRead(word) { this.module.exports.aica_dsp_bus_read(this.handle, word); }

	get ram() { return this.#array(Uint8Array, "aica_dsp_ram", this.ramSize); }
	get coef() { return this.#array(Uint16Array, "aica_dsp_coef", 128); }
	get madrs() { return this.#array(Uint16Array, "aica_dsp_madrs", 64); }
	get mpro() { return this.#array(Uint16Array, "aica_dsp_mpro", 512); }
	get temp() { return this.#array(Int32Array, "aica_dsp_temp", 128); }
	get mems() { return this.#array(Int32Array, "aica_dsp_mems", 32); }
	get mixs() { return this.#array(Int32Array, "aica_dsp_mixs", 32); }
	get efreg() { return this.#array(Int16Array, "aica_dsp_efreg", 16); }
	get exts() { return this.#array(Int16Array, "aica_dsp_exts", 2); }
	get view() { return this.#array(Int32Array, "aica_dsp_view", VIEW_WORDS); }
	get inputsMixs() { return this.#array(Int32Array, "aica_dsp_inputs_mixs", 16); }
	get inputsFetchWords() { return this.#array(Uint16Array, "aica_dsp_inputs_fetch_words", 64); }

	//The state as bytes, without the wave RAM, and back. loadState returns false for bytes that are no state
	saveState()
	{
		const { exports, memory } = this.module;
		const size = exports.aica_dsp_state_size();
		const at = exports.aica_dsp_alloc(size);

		exports.aica_dsp_save_state(this.handle, at);

		const blob = new Uint8Array(memory.buffer, at, size).slice();

		exports.aica_dsp_free(at);

		return blob;
	}

	loadState(blob)
	{
		const { exports, memory } = this.module;
		const at = exports.aica_dsp_alloc(blob.length || 1);

		new Uint8Array(memory.buffer, at, blob.length).set(blob);

		const loaded = exports.aica_dsp_load_state(this.handle, at, blob.length) !== 0;

		exports.aica_dsp_free(at);

		return loaded;
	}

	#arrays = new Map();

	#array(type, pointer, length)
	{
		const buffer = this.module.memory.buffer;
		let array = this.#arrays.get(pointer);

		if (!array || array.buffer !== buffer)
		{
			array = new type(buffer, this.module.exports[pointer](this.handle), length);
			this.#arrays.set(pointer, array);
		}

		return array;
	}
}

class AicaDspModule
{
	constructor(instance)
	{
		this.exports = instance.exports;
		this.memory = instance.exports.memory;
		this.scratch = this.exports.aica_dsp_alloc(32);
	}

	//A core after power on. ramSize: bytes of wave RAM, a power of two (the hardware has 2 MB or 8 MB), or 0
	create(ramSize = 2 << 20)
	{
		const handle = this.exports.aica_dsp_create(ramSize);

		if (!handle)
			throw new Error(`AICA DSP: no core with ${ramSize} bytes of wave RAM`);

		return new AicaDsp(this, handle);
	}

	//The fields of a step, from its four MPRO words and back
	decode(words)
	{
		new Uint16Array(this.memory.buffer, this.scratch, 4).set(words);
		this.exports.aica_dsp_decode(this.scratch, this.scratch + 8);

		const bytes = new Uint8Array(this.memory.buffer, this.scratch + 8, FIELDS.length);

		return Object.fromEntries(FIELDS.map((name, index) => [name, bytes[index]]));
	}

	encode(fields)
	{
		new Uint8Array(this.memory.buffer, this.scratch + 8, FIELDS.length).set(FIELDS.map((name) => fields[name] ?? 0));
		this.exports.aica_dsp_encode(this.scratch + 8, this.scratch);

		return Array.from(new Uint16Array(this.memory.buffer, this.scratch, 4));
	}

	//The 16 bit float format of the wave RAM
	pack(value24) { return this.exports.aica_dsp_pack(value24) >>> 0; }
	unpack(word) { return this.exports.aica_dsp_unpack(word); }
}

/*
	source: where the module is. Nothing: aica_dsp_core.wasm next to this file, fetched. A URL or a string: fetched.
	A Response or a promise of one, the bytes of the file, or a WebAssembly.Module: used as it is.
*/
export async function loadAicaDsp(source)
{
	const imports = { env: { emscripten_notify_memory_growth() { } } };

	if (source === undefined)
		source = new URL("./aica_dsp_core.wasm", import.meta.url);

	if (typeof source === "string" || source instanceof URL)
		source = fetch(source);

	source = await source;

	let instance;

	if (source instanceof WebAssembly.Module)
		instance = await WebAssembly.instantiate(source, imports);
	else if (typeof Response !== "undefined" && source instanceof Response)
		instance = (await WebAssembly.instantiate(await source.arrayBuffer(), imports)).instance;
	else
		instance = (await WebAssembly.instantiate(source, imports)).instance;

	instance.exports._initialize?.();

	const version = instance.exports.aica_dsp_api_version();

	if (version !== AICA_DSP_API_VERSION)
		throw new Error(`AICA DSP: the module has interface version ${version}, this loader ${AICA_DSP_API_VERSION}`);

	return new AicaDspModule(instance);
}
