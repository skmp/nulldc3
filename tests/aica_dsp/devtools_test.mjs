/*
	aica.dsp.wasm.devtools: the DSP module the UI commits (dreamcast-devtools, src/wasm/aica-dsp-core/) is the core
	of this tree.

	    node devtools_test.mjs <module dir> <wasm_test.mjs> <trace> <build.sh> <work dir>

	The module dir is that of the UI checkout; everything of the module is taken from there, by its names:
	PROVENANCE.json, aica_dsp_core.wasm, aica_dsp_core.mjs.

	1. PROVENANCE.json has the keys the playground's package fixed, and no other: repository, treeCommit,
	   dspCoreCommit, dirty, emscripten, apiVersion, files; files has aica_dsp_core.wasm and aica_dsp_core.mjs, each
	   with bytes and sha256, and the two files beside it are those bytes.
	2. wasm_test.mjs replays the trace of the native core (what aica.dsp.wasm replays on the tree's own build) on
	   the UI's loader and the UI's wasm: every answer is the native core's.
	3. Where the emcc that build.sh uses is found - on the PATH, else in the emsdk of EMSDK, or of ~/emsdk when
	   EMSDK is not set, under upstream/emscripten - and its emcc -dumpversion is the emscripten of PROVENANCE.json,
	   compared as strings, build.sh builds the module from this tree into the work dir with that emcc, and the
	   UI's two files have to be its bytes. With no such emcc the test says why and does not compare: node and the
	   UI checkout are all it needs.
*/
import { createHash } from "node:crypto";
import { accessSync, constants, existsSync, mkdirSync, readFileSync, rmSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { delimiter, dirname, join } from "node:path";
import { homedir } from "node:os";

const [moduleDir, wasmTest, tracePath, buildScript, workDir] = process.argv.slice(2);

if (!workDir)
{
	console.error("node devtools_test.mjs <module dir> <wasm_test.mjs> <trace> <build.sh> <work dir>");
	process.exit(2);
}

const FILES = ["aica_dsp_core.wasm", "aica_dsp_core.mjs"];
const KEYS = ["repository", "treeCommit", "dspCoreCommit", "dirty", "emscripten", "apiVersion", "files"];

let failed = 0;

function check(ok, what)
{
	console.log(`${ok ? "ok" : "FAILED"}: ${what}`);
	failed += ok ? 0 : 1;
	return ok;
}

function sha256(bytes)
{
	return createHash("sha256").update(bytes).digest("hex");
}

function finish()
{
	console.log(failed ? `aica.dsp.wasm.devtools: FAIL, ${failed} checks failed` : "aica.dsp.wasm.devtools: PASS");
	process.exit(failed ? 1 : 0);
}

//1. The provenance and the bytes beside it
const provenancePath = join(moduleDir, "PROVENANCE.json");

if (!check(existsSync(provenancePath), `${provenancePath} is there`))
	finish();

let provenance = null;

try
{
	provenance = JSON.parse(readFileSync(provenancePath, "utf8"));
}
catch (error)
{
	check(false, `PROVENANCE.json is JSON: ${error.message}`);
	finish();
}

const keys = Object.keys(provenance ?? {});

check(keys.length === KEYS.length && KEYS.every((key) => keys.includes(key)), `PROVENANCE.json has the keys ${KEYS.join(", ")} and no other: it has ${keys.join(", ")}`);
check(typeof provenance.repository === "string" && typeof provenance.emscripten === "string" && typeof provenance.dirty === "boolean"
	&& Number.isInteger(provenance.apiVersion) && /^[0-9a-f]{40}$/.test(provenance.treeCommit ?? "") && /^[0-9a-f]{40}$/.test(provenance.dspCoreCommit ?? ""),
	"repository and emscripten are strings, dirty is a boolean, apiVersion an integer, treeCommit and dspCoreCommit commits");

const files = provenance.files ?? {};
const named = Object.keys(files);

check(named.length === FILES.length && FILES.every((name) => named.includes(name)), `files names ${FILES.join(" and ")} and nothing else: it names ${named.join(", ")}`);

const committed = {};

for (const name of FILES)
{
	const path = join(moduleDir, name);

	if (!check(existsSync(path), `${name} is beside PROVENANCE.json`))
		continue;

	committed[name] = readFileSync(path);

	const entry = files[name] ?? {};
	const digest = sha256(committed[name]);

	check(entry.bytes === committed[name].length && entry.sha256 === digest,
		`${name} is the file PROVENANCE.json names: ${committed[name].length} bytes, sha256 ${digest} (PROVENANCE.json: ${entry.bytes} bytes, ${entry.sha256})`);
}

console.log(`the module: ${provenance.repository} ${provenance.treeCommit}, its core at ${provenance.dspCoreCommit}${provenance.dirty ? " with changes" : ""}, `
	+ `emscripten ${provenance.emscripten}, interface version ${provenance.apiVersion}`);

if (failed)
	finish();

//2. The native core's trace, replayed on the UI's loader and wasm
const replay = spawnSync(process.execPath, [wasmTest, join(moduleDir, "aica_dsp_core.mjs"), join(moduleDir, "aica_dsp_core.wasm"), tracePath],
	{ encoding: "utf8", stdio: ["ignore", "pipe", "pipe"], timeout: 120000 });

process.stdout.write(replay.stdout ?? "");
process.stdout.write(replay.stderr ?? "");
check(replay.status === 0, `the trace of the native core replayed on the UI's module (wasm_test.mjs), exit code ${replay.status}`);

//3. The committed bytes against a fresh build of this tree, by the emcc build.sh uses, where it is of the version
function executable(path)
{
	try
	{
		accessSync(path, constants.X_OK);
		return true;
	}
	catch
	{
		return false;
	}
}

function findEmcc()
{
	//build.sh: command -v emcc, else ${EMSDK:-$HOME/emsdk}/upstream/emscripten
	for (const dir of (process.env.PATH ?? "").split(delimiter))
	{
		if (dir && executable(join(dir, "emcc")))
			return join(dir, "emcc");
	}

	const emsdk = process.env.EMSDK ? process.env.EMSDK : join(process.env.HOME || homedir(), "emsdk");
	const emcc = join(emsdk, "upstream", "emscripten", "emcc");

	return executable(emcc) ? emcc : null;
}

const emcc = findEmcc();

if (emcc === null)
{
	console.log("the committed bytes are not compared with a fresh build: no emcc on the PATH and no emsdk in " + (process.env.EMSDK || "~/emsdk"));
	finish();
}

const asked = spawnSync(emcc, ["-dumpversion"], { encoding: "utf8", stdio: ["ignore", "pipe", "pipe"], timeout: 120000 });
const version = (asked.stdout ?? "").trim();

if (asked.status !== 0 || version !== provenance.emscripten)
{
	console.log(`the committed bytes are not compared with a fresh build: the emcc build.sh uses is ${emcc}, version "${version}"`
		+ `${asked.status !== 0 ? ` (emcc -dumpversion ended with ${asked.status})` : ""}, PROVENANCE.json names "${provenance.emscripten}"`);
	finish();
}

const out = join(workDir, "fresh");

rmSync(out, { recursive: true, force: true });
mkdirSync(out, { recursive: true });

//build.sh finds the emcc that was asked first on the PATH
const built = spawnSync("sh", [buildScript, out], { encoding: "utf8", stdio: ["ignore", "pipe", "pipe"], timeout: 280000,
	env: { ...process.env, PATH: dirname(emcc) + delimiter + (process.env.PATH ?? "") } });

process.stdout.write(built.stdout ?? "");
process.stdout.write(built.stderr ?? "");

if (check(built.status === 0, `build.sh of this tree with ${emcc} (emscripten ${version}), exit code ${built.status}`))
{
	for (const name of FILES)
	{
		const fresh = readFileSync(join(out, name));

		check(Buffer.compare(fresh, committed[name]) === 0, `${name} of the UI is the fresh build's: ${committed[name].length} and ${fresh.length} bytes, `
			+ `sha256 ${sha256(committed[name])} and ${sha256(fresh)}`);
	}
}

if (!failed)
	rmSync(out, { recursive: true, force: true });

finish();
