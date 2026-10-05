#!/bin/bash
# selftest.sh <devcast> <elf> [work dir] [config item ...]
#
# Runs the sh4bp selftest of the emulator (libswirl/debugger/dbg_selftest_sh4bp.cpp) in three sessions: the SH4 on
# the JIT, on the interpreter, and on the JIT with the debugger told to use the interpreter fallback. Every
# session has to end with exit code 0. What the sessions print as "note: cmp <key> <value>" is then compared:
#
#   b1.*  b4.*  b7.end     registers and step traces: the JIT's and the fallback's are the interpreter's
#   b2.time.blocks  b3.time.blocks  b12.blocks
#                          time, and for b12 the state of the integer workload: the fallback's are the JIT's
#
# Then the gates on the booted guest, for the same three and for the JIT and the fallback with idleskip off: a
# session of the tests sh4bpboot (a run for every case) and sessions of their twins sh4bpbase (the same runs
# with nothing of the cases in them). What they print for a run, "note: cmp boot.<gate>.<case> <state>", has
# to be equal:
#
#   two sessions of sh4bpbase agree; when they do not a third is run, and two of the three have to
#   the session of sh4bpboot is theirs; when it is not it is run twice more, and fails when it differs again
#
# How the baselines of the fallback compare with the JIT's is printed (B12): their FPU arithmetic differs.
#
# The emulator needs a display. Every session runs with its own HOME and configuration under the work dir, and
# with the real-time clock pinned: the guest reads the clock. The config items are given to every session as
# well, section:key=value each: for one, config:pvr.backend=none in a build that has that renderer.
# Exit code 0 when all of it held.

bin=$1; elf=$2; work=${3:-$(mktemp -d)}

if [ ! -x "$bin" ] || [ ! -f "$elf" ]; then
	echo "usage: selftest.sh <devcast> <elf> [work dir] [config item ...]"
	exit 2
fi

shift 2; [ $# -gt 0 ] && shift

more=()
for item in "$@"; do more+=(, "$item"); done

mkdir -p "$work"
fail=0

session() {
	local name=$1; shift
	local dir="$work/$name"

	rm -rf "$dir"; mkdir -p "$dir/home" "$dir/cfg" "$dir/data"

	( cd "$dir" && ulimit -S -c 1 2>/dev/null
	  exec env DISPLAY="${DISPLAY:-:0}" HOME="$dir/home" XDG_CONFIG_HOME="$dir/cfg" XDG_DATA_HOME="$dir/data" \
		stdbuf -oL -eL "$bin" -config audio:backend=none , config:Debug.FixedRtc=946684800 , Debugger:Port=0 , Debugger:SelfTest=sh4bp , \
		Debugger:StartPaused=yes "${more[@]}" "$@" "$elf" \
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

# same <session a> <session b>: both printed the same states of the booted guest, and printed some
same() {
	local ka=$(grep '^boot\.' "$work/$1.cmp") kb=$(grep '^boot\.' "$work/$2.cmp")

	[ -n "$ka" ] && [ "$ka" = "$kb" ]
}

# booted <name> [config item ...]: the gates on the booted guest in this configuration.
# $work/<name>-base.cmp is left as the baseline that two sessions agreed on
booted() {
	local name=$1; shift

	session $name-boot "$@" , Debugger:SelfTest=sh4bpboot
	session $name-base "$@" , Debugger:SelfTest=sh4bpbase
	session $name-base2 "$@" , Debugger:SelfTest=sh4bpbase

	if ! same $name-base $name-base2; then
		echo "note $name: two sessions of baselines differ, a third decides"
		diff <(grep '^boot\.' "$work/$name-base.cmp") <(grep '^boot\.' "$work/$name-base2.cmp") | head -6
		session $name-base3 "$@" , Debugger:SelfTest=sh4bpbase

		if same $name-base2 $name-base3; then
			cp "$work/$name-base2.cmp" "$work/$name-base.cmp"
		elif ! same $name-base $name-base3; then
			echo "FAIL $name: no two of three sessions of baselines agree on the booted guest"
			fail=1
			return
		fi
	fi

	if same $name-boot $name-base; then
		echo "ok   $name: $(grep -c '^boot\.' "$work/$name-base.cmp") runs of the booted guest, each in the state of its baseline"
		return
	fi

	echo "note $name: the cases differ from the baselines, they are run twice more"
	diff <(grep '^boot\.' "$work/$name-boot.cmp") <(grep '^boot\.' "$work/$name-base.cmp") | head -6

	session $name-boot2 "$@" , Debugger:SelfTest=sh4bpboot
	session $name-boot3 "$@" , Debugger:SelfTest=sh4bpboot

	if same $name-boot2 $name-base && same $name-boot3 $name-base; then
		echo "ok   $name: $(grep -c '^boot\.' "$work/$name-base.cmp") runs of the booted guest, each in the state of its baseline; one session differed and two repetitions did not"
	else
		echo "FAIL $name: the booted guest with the cases is not in the state of its baseline"
		diff <(grep '^boot\.' "$work/$name-boot2.cmp") <(grep '^boot\.' "$work/$name-base.cmp") | head -20
		diff <(grep '^boot\.' "$work/$name-boot3.cmp") <(grep '^boot\.' "$work/$name-base.cmp") | head -20
		fail=1
	fi
}

session jit
session interpreter , config:Dynarec.Enabled=no
session fallback , Debugger:ForceFallback=yes

compare jit interpreter 'b1\.[^ ]*|b4\.[^ ]*|b7\.end'
compare fallback interpreter 'b1\.[^ ]*|b4\.[^ ]*|b7\.end'
compare fallback jit 'b2\.time\.blocks|b3\.time\.blocks|b12\.blocks'

booted jit
booted interpreter , config:Dynarec.Enabled=no
booted fallback , Debugger:ForceFallback=yes
booted jit-idleskip-off , config:Dynarec.idleskip=no
booted fallback-idleskip-off , Debugger:ForceFallback=yes , config:Dynarec.idleskip=no

# B12 on the booted guest: printed, not failed
for pair in "fallback jit" "fallback-idleskip-off jit-idleskip-off"; do
	set -- $pair

	if [ -s "$work/$1-base.cmp" ] && [ -s "$work/$2-base.cmp" ]; then
		differ=$(diff <(grep '^boot\.' "$work/$1-base.cmp") <(grep '^boot\.' "$work/$2-base.cmp") | grep -c '^<')
		echo "note $1 against $2: $differ of $(grep -c '^boot\.' "$work/$1-base.cmp") baselines of the booted guest differ"
	fi
done

if [ "$fail" -eq 0 ]; then echo "sh4bp selftest: PASS"; else echo "sh4bp selftest: FAIL, logs in $work"; fi
exit $fail
