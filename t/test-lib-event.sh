# Event notification test library included by some test scripts.
#
# Copyright (C) 2019 GitHub, Inc.
#

EVENT_LOG="expect.event.log"
EVENT_ERR="expect.event.err"

event_msg_notify="test event notification for"
event_msg_perm="test permission request for"

event_msg_vfs="TestNotifyOperation for"

event_create_dir="0x0001-40000000"
event_delete_dir="0x0000-40000400"

event_delete_file="0x0000-00000400"
event_create_file="0x0001-00000000"

event_vfs_create_dir="1, 0x00000004"
event_vfs_delete_dir="1, 0x00000010"

event_vfs_create_file="0, 0x00000004"
event_vfs_delete_file="0, 0x00000010"

# Format into "$event_msg" and "$event_err_msg" log and error messages
# matching those output by the test mount helper programs.  If an error
# is expected, "$1" should be "error" and "$2" should contain the errno
# name, e.g., "ENOMEM".  The next three arguments (starting at either
# "$3" or "$1", depending on whether an error is expected or not) should be
# the category of event (e.g., "notify" or "perm"), and the type of event
# (e.g., "create_dir" or "delete_file"), and the file path
# (relative to the projfs mount point) on which the event is expected,
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

	if test ":$1" = ":vfs"
	then
		eval vfs_code=\$event_vfs_"$2"
		event_msg_head="  $msg $3: "
		event_msg_tail=", $vfs_code"
	else
		event_msg_head="  $msg $3: $code, "
		event_msg_tail=""
	fi

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
	if test ":$event_msg_tail" != ":"
	then
		event_msg_tail=", $1$event_msg_tail"; # prepend cmd for VFS
	fi &&
	if test ":$event_err_msg" = ":"
	then
		event_err=""
	else
		event_err="$EVENT_ERR"
	fi &&
	projfs_log_exec "$EVENT_LOG" "$event_msg_head" "$event_msg_tail" \
		"$event_err" "$event_err_msg" "$@"
}

