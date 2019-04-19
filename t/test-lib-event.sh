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

EVENT_LOG="expect.event.log"
EVENT_ERR="expect.event.err"

event_msg_notify="test event notification for"
event_msg_perm="test permission request for"

event_rename_dir="0x0000-400000c0"
event_create_dir="0x0000-40000100"
event_delete_dir="0x0001-40000000"

event_rename_file="0x0000-000000c0"
event_create_file="0x0000-00000100"
event_delete_file="0x0001-00000000"

# Format into "$event_msg_head" and "$event_err_msg" log and error messages
# matching those output by the test mount helper programs.
# If an error is expected, "$1" should be "error" and "$2" should contain
# the errno name, e.g., "ENOMEM".  The next arguments (starting at either
# "$3" or "$1", depending on whether an error is expected or not) should be
# the category of event (e.g., "notify" or "perm"), the type of event
# (e.g., "create_dir" or "delete_file"), the file path
# (relative to the projfs mount point) on which the event is expected,
# and optionally the target file path of the event if one is expected.
projfs_event_printf () {
	if test ":$1" = ":error"
	then
		shift
		err=$("$TEST_DIRECTORY"/get_strerror "$1"); shift
	else
		err=""
		event_err_msg=""
	fi

	eval msg=\$event_msg_"$1"
	eval code=\$event_"$2"

	event_msg_head="  $msg $3"
	if test ":$4" != ":"
	then
		event_msg_head="$event_msg_head, $4"
	fi
	event_msg_head="$event_msg_head: $code, "

	if test ":$err" != ":"
	then
		event_err_msg="projfs: event handler failed"
		event_err_msg="$event_err_msg: $err; event mask $code, pid "
	fi
}

# Execute a command, capturing its process ID, then format the pid into
# messages as expected from the test mount helper programs.  The messages
# are appended to "$EVENT_LOG" and, if an error is expected, into
# "$EVENT_ERR".
# Requires that projfs_event_printf has been called first to format the
# expected messages (without the pid) and flag whether an error is expected.
projfs_event_exec () {
	if test ":$event_err_msg" = ":"
	then
		event_err=""
	else
		event_err="$EVENT_ERR"
	fi &&
	projfs_log_exec "$EVENT_LOG" "$event_msg_head" "$event_err" \
		"$event_err_msg" "$@"
}

