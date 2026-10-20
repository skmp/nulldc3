#!/bin/bash
# window_checklist.sh <nulldc3> <work dir> [once]
#
# Does at the window of the emulator what a person does there, with the emulator thread parked by the debugger
# (session emuwin.parked) and again with a pause asked for all the time (session emuwin.pending):
#
#   run 1   the menu key; in the menu Save State, Load State, Restart and Exit; then the close button
#   run 2   the escape key, twice; then the close button
#   run 3   the close button, with nothing before it
#
# then two actions in one batch of events, the second while the machine is on its way to the stop of the first:
# the close button twice, and the escape key with the close button behind it. Several runs each, with the guest
# running (session emuwin.running), parked and with a pause asked for all the time; a guest that is stopped
# while it runs has a frame queued for the window, as a program that draws has at most moments;
#
# then, with a window as well, the session emu.reset (a reset of the guest while parked and with a pause pending),
# the session emu.step (a reset of the guest and a stop by the host while a step executes), a guest that returns
# to the BIOS (sessions emuwin.exit, emuwin.exitpending): the thread ends, the window stays; the title of the
# window at two stops of one thread (session emuwin.twice); and the close button in the moment in which the
# guest resets the machine (session emuwin.closereset, ten runs).
# With "once" every item is done one time: a window for each, and none of the repetitions.
# It says for each whether the machine did what it has to: one line "PASS <session>: <item>" or
# "FAIL <session>: <item>". The output of every run is <work dir>/<session>.<run>/log.txt.
#
# It needs a display (DISPLAY, default :0) and a C compiler with Xlib for xsend.c. The events are sent to the
# window of the run; nothing is typed or clicked on the desktop. The window of a run is the one that is there
# after its start and was not before: run it where nothing else opens windows at that moment.
# Exit code 0 when every item passed.

bin=$(readlink -f "$1"); work=$2; once=$3
here=$(dirname "$(readlink -f "$0")")

if [ ! -x "$bin" ] || [ -z "$work" ] || { [ -n "$once" ] && [ "$once" != once ]; }; then
	echo "usage: window_checklist.sh <nulldc3> <work dir> [once]"
	exit 2
fi

export DISPLAY=${DISPLAY:-:0}
mkdir -p "$work"
work=$(readlink -f "$work")

xsend="$work/xsend"
${CC:-cc} -O1 -o "$xsend" "$here/xsend.c" -lX11 || { echo "xsend.c did not build"; exit 2; }

# the window title while the debugger holds the machine: "nullDC <version> [paused by debugger]", the version
# being the build's, which --version prints as "nullDC <version>" (its last line on stdout: the Linux build prints
# its personality and paging lines in front of it)
version=$("$bin" --version | tail -n 1)
case "$version" in
"nullDC "?*) ;;
*) echo "$bin --version did not say the version (\"$version\")"; exit 2 ;;
esac
PAUSED="$version [paused by debugger]"

# keycodes of the X server, and where the buttons of the menu are in a window of 640 x 480
KEY_TAB=23; KEY_ESC=9
LOAD="238 164"; SAVE="401 164"; RESTART="238 280"; EXIT="401 280"

fail=0
session=""; dir=""; log=""; pid=""; win=""; at=0

pass() { echo "PASS $session: $1"; }
bad()  { echo "FAIL $session: $1"; fail=1; }

# wait_line <seconds> <pattern>: a line behind the ones already looked at that matches; they are looked at then
wait_line() {
	local n=$(( $1 * 10 )) found
	while [ $n -gt 0 ]; do
		found=$(tail -n +$((at + 1)) "$log" | grep -a -n -m 1 -E "$2" | cut -d: -f1)
		if [ -n "$found" ]; then at=$((at + found)); return 0; fi
		sleep 0.1; n=$((n - 1))
	done
	return 1
}

# start <run>: the emulator with the session, and its window
start() {
	dir="$work/$session.$1"; log="$dir/log.txt"; at=0
	rm -rf "$dir"; mkdir -p "$dir/portable/mappings" "$dir/portable/data"

	# the escape key is a button of the emulator only when a mapping says so
	printf '[emulator]\nmapping_name = X11 Keyboard\nbtn_escape = %d\nbtn_menu = %d\n' $KEY_ESC $KEY_TAB > "$dir/portable/mappings/X11_Keyboard.cfg"

	local before=$("$xsend" list | sort)

	( cd "$dir" && ulimit -S -c 1 2>/dev/null
	  exec env -i DISPLAY="$DISPLAY" \
		"$bin" --portable "$dir/portable" --config audio:backend=none , config:Dreamcast.RTC=946684800 , config:SavePopup.isShown=yes , \
		Debugger:SelfTest=emuwin.$session "$here/emu_guest.bin" > "$log" 2>&1 ) &
	pid=$!

	case "$session" in
	parked)  wait_line 30 'SelfTest: emuwin.parked: ok: parked: "pause"' ;;
	pending|running) wait_line 30 'the guest runs from here' ;;
	twice)   wait_line 30 'the first stop; the guest runs on in a while' ;;
	closereset) wait_line 30 'the guest is about to reset the machine' ;;
	*)       wait_line 30 'the guest is about to return to the BIOS' ;;
	esac || { bad "run $1: the session did not come to its state"; return 1; }

	# a run that closes its window itself may be gone by now
	[ "$session" = closereset ] && return 0

	win=$(comm -13 <(echo "$before") <("$xsend" list | sort) | head -1)
	[ -n "$win" ] || { bad "run $1: no window"; return 1; }
	return 0
}

# the_end <what>: the process ends within 10 s, by itself, with exit code 0
the_end() {
	local n=100
	while kill -0 $pid 2>/dev/null && [ $n -gt 0 ]; do sleep 0.1; n=$((n - 1)); done

	if kill -0 $pid 2>/dev/null; then
		bad "$1: the process is still there after 10 s"
		kill -KILL $pid; wait $pid 2>/dev/null
		return 1
	fi

	wait $pid; local rc=$?
	if [ $rc -ne 0 ] || grep -a -q -E "Fatal error|Verify Failed|fault_handler|: FAILED: " "$log"; then
		bad "$1: exit code $rc, or a failure in the output"
		return 1
	fi
	pass "$1"
}

stop_now() { kill -KILL $pid 2>/dev/null; wait $pid 2>/dev/null; }

# menu <what>: the menu key stops the machine
menu() {
	"$xsend" key $win $KEY_TAB
	wait_line 10 'emuwin: state Stopped host-stopped' || { bad "$1"; return 1; }
	sleep 0.5		# the menu is drawn
}

checklist() {
	session=$1

	# run 1: the menu and its buttons
	if start 1; then
		if [ "$session" = parked ]; then
			[ "$("$xsend" title $win)" = "$PAUSED" ] && pass "the window title says that the debugger paused" || bad "the window title says that the debugger paused"
		fi

		if menu "the menu key stops the machine and opens the menu"; then
			"$xsend" click $win $SAVE
			if wait_line 10 'emuwin: state Running' && [ -s "$dir/portable/data/nodisk.state" ]; then
				pass "the menu key stops the machine and opens the menu"
				pass "menu Save State saves and resumes"
			else
				bad "menu Save State saves and resumes"
			fi
		fi

		if menu "the menu key, a second time"; then
			"$xsend" click $win $LOAD
			if wait_line 10 'Loaded state from' && wait_line 10 'emuwin: state Stopped host-stopped' && wait_line 10 'emuwin: state Running'; then
				pass "menu Load State loads (a new stop of the stopped machine) and resumes"
			else
				bad "menu Load State loads (a new stop of the stopped machine) and resumes"
			fi
		fi

		if menu "the menu key, a third time"; then
			"$xsend" click $win $RESTART
			if wait_line 10 'emuwin: state Stopped host-stopped' && wait_line 10 'emuwin: state Running' && wait_line 10 '"start", pc A0000000'; then
				pass "menu Restart resets and resumes: the next thread starts at the reset vector"
			else
				bad "menu Restart resets and resumes: the next thread starts at the reset vector"
			fi
		fi

		# the guest boots again before the next stop (the session that pauses all the time resumes its start park)
		[ "$session" = pending ] && sleep 1

		if menu "the menu key, a fourth time"; then
			"$xsend" click $win $EXIT
			if wait_line 10 'emuwin: state NoMachine no-game' && kill -0 $pid 2>/dev/null; then
				pass "menu Exit takes the machine apart, the process stays"
			else
				bad "menu Exit takes the machine apart, the process stays"
			fi
		fi

		"$xsend" close $win
		the_end "the close button with no machine ends the process"
	else
		stop_now
	fi

	# run 2: the escape key
	if start 2; then
		"$xsend" key $win $KEY_ESC
		if wait_line 10 'emuwin: state Stopped host-stopped'; then
			pass "the escape key stops the machine"
		else
			bad "the escape key stops the machine"
		fi

		"$xsend" key $win $KEY_ESC
		sleep 1
		if kill -0 $pid 2>/dev/null && ! grep -a -q -E "Fatal error|Verify Failed" "$log"; then
			pass "the escape key for a stopped machine does nothing"
		else
			bad "the escape key for a stopped machine does nothing"
		fi

		"$xsend" close $win
		the_end "the close button with a stopped machine ends the process"
	else
		stop_now
	fi

	# run 3: the close button
	if start 3; then
		"$xsend" close $win
		the_end "the close button ends the process, without a hang"
	else
		stop_now
	fi
}

# twice <session> <runs>: two actions of the host that the window finds in one pass over its events. The first
# stops the machine; the second comes while the emulator thread is on its way out and has frames queued for
# the window to draw
twice() {
	local i

	session=$1
	for i in $(seq 1 $2); do
		if start close2-$i; then
			"$xsend" close2 $win
			the_end "run $i: the close button twice in a row ends the process, without a hang"
		else
			stop_now
		fi

		if start keyclose-$i; then
			"$xsend" keyclose $win $KEY_ESC
			the_end "run $i: the escape key and the close button in a row end the process, without a hang"
		else
			stop_now
		fi
	done
}

# with_window <session> <item>: a session of the selftest emu that ends by itself, in a run with a window
with_window() {
	session=$1
	dir="$work/$1"; log="$dir/log.txt"
	rm -rf "$dir"; mkdir -p "$dir/portable/data"

	( cd "$dir" && ulimit -S -c 1 2>/dev/null
	  exec env -i DISPLAY="$DISPLAY" \
		timeout 120 "$bin" --portable "$dir/portable" --config audio:backend=none , config:Dreamcast.RTC=946684800 , \
		Debugger:SelfTest=$1 "$here/emu_guest.bin" > "$log" 2>&1 )

	if [ $? -eq 0 ] && grep -a -q '^SelfTest: 1 passed, 0 failed' "$log"; then
		pass "$2"
	else
		bad "$2"
	fi
}

# reset_and_exit: with a window, a reset of the guest and an exit of the guest
reset_and_exit() {
	# the reset register while parked, with a pause pending, resets by calls
	with_window emu.reset "a guest reset while parked resets and stays parked; with a pause pending it parks with reason reset"

	# with a backend that steps: the reset register written in a step, the host stop while a step executes
	with_window emu.step "a guest reset in a step is one stop, the reset's; a host stop during a step ends the thread when the step has returned"

	# the guest returns to the BIOS: the thread ends, the window stays
	local which
	for which in exit exitpending; do
		session=$which
		if start 1; then
			if wait_line 10 'emuwin: state Stopped guest-exit' && sleep 1 && kill -0 $pid 2>/dev/null; then
				pass "the exit of the guest ends the emulator thread (guest-exit), the window stays"
			else
				bad "the exit of the guest ends the emulator thread (guest-exit), the window stays"
			fi

			"$xsend" close $win
			the_end "the close button behind a guest exit ends the process"
		else
			stop_now
		fi
	done
}

# title_twice: the title says that the debugger paused at the second stop of a thread as at its first
title_twice() {
	local paused="$PAUSED" n

	session=twice
	if start 1; then
		[ "$("$xsend" title $win)" = "$paused" ] && pass "the window title at the first stop of a thread" || bad "the window title at the first stop of a thread"

		# the running machine takes the title back
		wait_line 10 'emuwin: twice: running'
		n=50
		while [ "$("$xsend" title $win)" = "$paused" ] && [ $n -gt 0 ]; do sleep 0.1; n=$((n - 1)); done
		[ $n -gt 0 ] && pass "the running machine writes its frame rate into the title" || bad "the running machine writes its frame rate into the title"

		if wait_line 20 'emuwin: twice: the second stop' && sleep 0.3 && [ "$("$xsend" title $win)" = "$paused" ]; then
			pass "the window title at the second stop of the same thread"
		else
			bad "the window title at the second stop of the same thread (\"$("$xsend" title $win)\")"
		fi

		"$xsend" close $win
		the_end "the close button while parked ends the process"
	else
		stop_now
	fi
}

# close_in_reset: the session closes its window itself, as the window does for its close button, while the guest
# resets the machine
close_in_reset() {
	local i

	session=closereset
	for i in $(seq 1 $1); do
		if start $i; then
			the_end "run $i: the close button while the guest resets the machine ends the process"
		else
			stop_now
		fi
	done
}

# how often the items that race are done
if [ -n "$once" ]; then few=1; many=1; else few=3; many=10; fi

checklist parked
checklist pending
twice running $few
twice parked $few
twice pending $many
reset_and_exit
title_twice
close_in_reset $many

[ $fail -eq 0 ] && echo "window checklist: every item passed" || echo "window checklist: FAILED"
exit $fail
