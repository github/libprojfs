# Event notification test library included by some test scripts.
#
# Copyright (C) 2018-2019 GitHub, Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see http://www.gnu.org/licenses/ .

EVENT_OUT="expect.event.out"
EVENT_LOG="expect.event.log"

event_msg_notify="test event notification for"
event_msg_perm="test permission request for"

event_msg_err="event handler failed"

event_notify_close_file="0x0000-00000008"
event_notify_rename_file="0x0000-000000c0"
event_notify_create_file="0x0000-00000100"
event_notify_delete_file="0x0000-00000200"
event_notify_link_file="0x1000-00000100"

event_notify_rename_dir="0x0000-400000c0"
event_notify_create_dir="0x0000-40000100"
event_notify_delete_dir="0x0000-40000200"

event_perm_delete_file="0x0002-00000000"
event_perm_rename_file="0x0004-00000000"

event_perm_delete_dir="0x0002-40000000"
event_perm_rename_dir="0x0004-40000000"

NL=$(printf "\nx")
NL="${NL%%x}"

OUT_FMT='  %s %s%s: %s, %s'
LOG_FMT='%s: %s; mask %s, pid %s, path %s%s'
LOG_TARGET_FMT=', target path %s'

# Format into "$event_out_msgs" and "$event_log_msgs" the standard output
# and logged error messages expected from the test mount helper programs.
# If logged errors are expected, "$1" should be "error" and "$2" should
# contain the errno name, e.g., "ENOMEM".  The next arguments (starting at
# either "$3" or "$1", depending on whether logged error messages are
# expected or not) should be the category of event (e.g., "notify" or "perm"),
# the type of event (e.g., "create_dir" or "delete_file"), the file path
# (relative to the projfs mount point) on which the event is expected,
# and optionally the target file path of the event if one is expected.
projfs_event_printf () {
	if test "$1" = error
	then
		shift
		err=$("$TEST_DIRECTORY"/get_strerror "$1"); shift
	else
		err=""
	fi

	eval msg=\$event_msg_"$1"
	eval code=\$event_"$1"_"$2"

	if test -z "$4"
	then
		target_msg=""
	else
		target_msg=", $4"
	fi
	out_msg=$(printf "$OUT_FMT" \
		"$msg" "$3" "$target_msg" "$code" "$EXEC_PID_MARK")

	event_out_msgs="${event_out_msgs:+$event_out_msgs$NL}$out_msg"

	if test -n "$err"
	then
		if test -z "$4"
		then
			target_msg=""
		else
			target_msg=$(printf "$LOG_TARGET_FMT" "$4")
		fi
		log_msg=$(printf "$LOG_FMT" \
			"$event_msg_err" "$err" "$code" "$EXEC_PID_MARK" \
			"$3" "$target_msg")

		event_log_msgs="${event_log_msgs:+$event_log_msgs$NL}$log_msg"
	fi
}

# Execute a command, capturing its process ID, then format the pid into
# messages as expected from the test mount helper programs.  The messages
# are appended to "$EVENT_OUT" and, if a logged error message is expected,
# into "$EVENT_LOG".
# Requires that projfs_event_printf has been called first to format the
# expected messages (without the pid) and flag whether an error is expected.
projfs_event_exec () {
	if test -z "$event_log_msgs"
	then
		event_log=""
	else
		event_log="$EVENT_LOG"
	fi &&
	projfs_log_exec "$EVENT_OUT" "$event_out_msgs" \
		"$event_log" "$event_log_msgs" "$@"; ret=$?

	event_out_msgs=""
	event_log_msgs=""

	return $ret
}

