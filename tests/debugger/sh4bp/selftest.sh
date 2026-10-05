#!/bin/bash
# selftest.sh <devcast> <elf> [work dir]
#
# Runs the sh4bp selftest of the emulator (libswirl/debugger/dbg_selftest_sh4bp.cpp) in three sessions: the SH4 on
# the JIT, on the interpreter, and on the JIT with the debugger told to use the interpreter fallback. Every
# session has to end with exit code 0. What the sessions print as "note: cmp <key> <value>" is then compared:
#
#   b1.*  b4.*  b7.end     registers and step traces: the JIT's and the fallback's are the interpreter's
#   b2.time.blocks  b3.time.blocks  b12.blocks
#                          time, and for b12 the state of the integer workload: the fallback's are the JIT's
#
# The emulator needs a display. Every session runs with its own HOME and configuration under the work dir.
# Exit code 0 when all of it held.

bin=$1; elf=$2; work=${3:-$(mktemp -d)}

if [ ! -x "$bin" ] || [ ! -f "$elf" ]; then
	echo "usage: selftest.sh <devcast> <elf> [work dir]"
	exit 2
fi

mkdir -p "$work"
fail=0

session() {
	local name=$1; shift
	local dir="$work/$name"

	rm -rf "$dir"; mkdir -p "$dir/home" "$dir/cfg" "$dir/data"

	( cd "$dir" && ulimit -S -c 1 2>/dev/null
	  exec env DISPLAY="${DISPLAY:-:0}" HOME="$dir/home" XDG_CONFIG_HOME="$dir/cfg" XDG_DATA_HOME="$dir/data" \
		stdbuf -oL -eL "$bin" -config audio:backend=none , Debugger:Port=0 , Debugger:SelfTest=sh4bp , Debugger:StartPaused=yes "$@" "$elf" \
		> "$work/$name.log" 2>&1 ) &
	local pid=$!
	local waited=0

	while kill -0 "$pid" 2>/dev/null && [ "$waited" -lt 1800 ]; do sleep 1; waited=$((waited + 1)); done

	if kill -0 "$pid" 2>/dev/null; then
		kill -TERM "$pid"; sleep 2; kill -0 "$pid" 2>/dev/null && kill -KILL "$pid"
		wait "$pid" 2>/dev/null
		echo "FAIL $name: the session did not end"
		fail=1
		return
	fi

	wait "$pid"; local rc=$?
	local last=$(grep '^SelfTest: .* passed, .* failed' "$work/$name.log" | tail -1)

	if [ "$rc" -eq 0 ]; then
		echo "ok   $name: $last"
	else
		echo "FAIL $name: exit code $rc; $last"
		grep 'SelfTest: .*FAIL' "$work/$name.log" | head -20
		fail=1
	fi

	grep '^SelfTest: .*: note: cmp ' "$work/$name.log" | sed 's/^SelfTest: .*: note: cmp //' | sort > "$work/$name.cmp"
}

# compare <session a> <session b> <key pattern>
compare() {
	local a=$1 b=$2 pattern=$3
	local ka=$(grep -E "^($pattern) " "$work/$a.cmp") kb=$(grep -E "^($pattern) " "$work/$b.cmp")

	if [ -n "$ka" ] && [ "$ka" = "$kb" ]; then
		echo "ok   $a = $b: $(echo "$ka" | wc -l) values ($pattern)"
	else
		echo "FAIL $a = $b ($pattern)"
		diff <(echo "$ka") <(echo "$kb") | head -20
		fail=1
	fi
}

session jit
session interpreter , config:Dynarec.Enabled=no
session fallback , Debugger:ForceFallback=yes

compare jit interpreter 'b1\.[^ ]*|b4\.[^ ]*|b7\.end'
compare fallback interpreter 'b1\.[^ ]*|b4\.[^ ]*|b7\.end'
compare fallback jit 'b2\.time\.blocks|b3\.time\.blocks|b12\.blocks'

if [ "$fail" -eq 0 ]; then echo "sh4bp selftest: PASS"; else echo "sh4bp selftest: FAIL, logs in $work"; fi
exit $fail
