/*
	The DSP playground's assembler (dreamcast-devtools src/app/dsp/dspUtils.ts, assembleSource, imported
	unmodified) on the sounding guest's DSP program, for dsptest_text.py: one line for each line it assembles,

	    MPRO <step> <w0> <w1> <w2> <w3>     COEF <i> <value>     MADRS <i> <value>     MEMS_H <i> <value>

	values in decimal, in the order of the text.

	Run with tsx:  tsx dsptest_text.mts --dsp-utils <dspUtils.ts> --text <dsptest_dsp.txt>
	Exit status: 0 assembled, 1 the assembler refused the text, 2 usage.
*/
import { readFileSync } from "node:fs";
import { pathToFileURL } from "node:url";

const args = new Map<string, string>();
for (let i = 2; i + 1 < process.argv.length; i += 2) {
  args.set(process.argv[i], process.argv[i + 1]);
}

const dspUtilsPath = args.get("--dsp-utils");
const textPath = args.get("--text");
if (!dspUtilsPath || !textPath) {
  console.error("usage: tsx dsptest_text.mts --dsp-utils <dspUtils.ts> --text <dsptest_dsp.txt>");
  process.exit(2);
}

const { assembleSource } = await import(pathToFileURL(dspUtilsPath).href);

let parsed;
try {
  parsed = assembleSource(readFileSync(textPath, "utf8").split("\n"));
} catch (error) {
  console.log(`the assembler refuses the text: ${error instanceof Error ? error.message : String(error)}`);
  process.exit(1);
}

for (const m of parsed.MPRO as Array<{ index: number; encoded: number[] }>) {
  console.log(`MPRO ${m.index} ${m.encoded.join(" ")}`);
}
for (const c of parsed.COEF as Array<{ index: number; value: number }>) {
  console.log(`COEF ${c.index} ${c.value}`);
}
for (const a of parsed.MADRS as Array<{ index: number; value: number }>) {
  console.log(`MADRS ${a.index} ${a.value}`);
}
for (const s of parsed.MEMS as Array<{ index: number; low?: number; high?: number }>) {
  if (s.low !== undefined) {
    console.log(`MEMS_L ${s.index} ${s.low}`);
  }
  if (s.high !== undefined) {
    console.log(`MEMS_H ${s.index} ${s.high}`);
  }
}
