/*
	Compares dbg::DisasmDsp with the DSP playground of dreamcast-devtools (src/app/dsp/dspUtils.ts).

	For every sampled step:
	  - the line must be "MPRO[step] = " + disassembleDesc(decodeInst(words)), the playground's own
	    disassembly ("TRA:0" stands in for the empty string of an all-zero step, which the playground's
	    assembler does not accept as a line)
	  - the line, fed to the playground's assembler, must give back the step index and the words
	    (restricted to the bits its instruction format defines)

	Run with tsx:  tsx dsp_playground.mts --dsp-utils <dspUtils.ts> --test-bin <disasm_test>
	Exit status: 0 identical, 1 differences.
*/
import { spawnSync } from "node:child_process";
import { pathToFileURL } from "node:url";

const args = new Map<string, string>();
for (let i = 2; i + 1 < process.argv.length; i += 2) {
  args.set(process.argv[i], process.argv[i + 1]);
}

const dspUtilsPath = args.get("--dsp-utils");
const testBin = args.get("--test-bin");
if (!dspUtilsPath || !testBin) {
  console.error("usage: tsx dsp_playground.mts --dsp-utils <dspUtils.ts> --test-bin <disasm_test>");
  process.exit(2);
}

const { decodeInst, encodeInst, disassembleDesc, assembleSource } = await import(pathToFileURL(dspUtilsPath).href);

// mulberry32
let state = 0x41494341;
const rng = (): number => {
  state = (state + 0x6d2b79f5) | 0;
  let t = state;
  t = Math.imul(t ^ (t >>> 15), t | 1);
  t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
  return (t ^ (t >>> 14)) >>> 0;
};

type Sample = { step: number; words: number[] };
const samples: Sample[] = [];

// every step index with an empty instruction, then every single bit of every word
for (let step = 0; step < 128; step++) {
  samples.push({ step, words: [0, 0, 0, 0] });
}
for (let word = 0; word < 4; word++) {
  for (let bit = 0; bit < 16; bit++) {
    const words = [0, 0, 0, 0];
    words[word] = 1 << bit;
    samples.push({ step: (word * 16 + bit) & 127, words });
  }
}
samples.push({ step: 127, words: [0xffff, 0xffff, 0xffff, 0xffff] });
// random words: dense, and sparse ones that leave most fields zero
for (let n = 0; n < 100000; n++) {
  const sparse = n % 2 === 1;
  const words = [0, 0, 0, 0].map(() => (sparse ? rng() & rng() & rng() : rng()) & 0xffff);
  samples.push({ step: rng() & 127, words });
}

const input = samples.map((s) => `${s.step} ${s.words.map((w) => w.toString(16)).join(" ")}\n`).join("");
const run = spawnSync(testBin, ["dsp"], { input, encoding: "utf8", maxBuffer: 1 << 28 });
if (run.status !== 0) {
  console.error(`disasm_test dsp failed: ${run.status} ${run.stderr}`);
  process.exit(1);
}
const lines = run.stdout.split("\n");
if (lines[lines.length - 1] === "") {
  lines.pop();
}

let identical = 0;
let empty = 0;
let reassembled = 0;
let mismatches = 0;
const report = (what: string, sample: Sample, got: string, want: string) => {
  if (++mismatches <= 20) {
    console.log(`  MISMATCH ${what}: step ${sample.step} words ${sample.words.map((w) => w.toString(16))}: got "${got}" want "${want}"`);
  }
};

if (lines.length !== samples.length) {
  console.log(`  MISMATCH line count: got ${lines.length} want ${samples.length}`);
  mismatches++;
}

// every line ends in \n alone, as on Linux (on Windows disasm_test puts its stdout in binary mode): lines that end in
// \r\n are the stdout of a text mode, and are not taken apart at \r\n here
const crlf = lines.filter((line) => line.endsWith("\r")).length;
if (crlf) {
  console.log(`  MISMATCH line ends: ${crlf} lines of disasm_test end in \\r\\n, not in \\n (its stdout in text mode)`);
  mismatches++;
}

for (let i = 0; i < Math.min(lines.length, samples.length); i++) {
  const sample = samples[i];
  const desc = decodeInst(sample.words);
  const text: string = disassembleDesc(desc);
  const want = `MPRO[${sample.step}] = ${text || "TRA:0"}`;

  if (lines[i] !== want) {
    report("text", sample, lines[i], want);
    continue;
  }
  identical++;
  if (!text) {
    empty++;
  }

  // the line pastes into the playground: its assembler returns the same step and words
  let back = "";
  try {
    const parsed = assembleSource([lines[i]]);
    back = parsed.MPRO.map((m: { index: number; encoded: number[] }) => `${m.index}:${m.encoded.join(",")}`).join(";");
  } catch (error) {
    back = `error: ${error instanceof Error ? error.message : String(error)}`;
  }
  const wantBack = `${sample.step}:${encodeInst(desc).join(",")}`;
  if (back !== wantBack) {
    report("assemble", sample, back, wantBack);
    continue;
  }
  reassembled++;
}

console.log("dsp vs dreamcast-devtools dspUtils.ts");
console.log(`  steps compared        ${samples.length}`);
console.log(`  identical             ${identical}`);
console.log(`    of which empty steps, "TRA:0" for the playground's "" ${empty}`);
console.log(`  reassembled to the same words by the playground assembler ${reassembled}`);
console.log(`  unexplained           ${mismatches}`);
process.exit(mismatches ? 1 : 0);
