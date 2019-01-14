/* VFSforGit ProjFS API
   Copyright (C) 2018-2019 GitHub, Inc.

   See the COPYING file distributed with this library for additional
   information regarding copyright ownership.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library, in the file LICENSE.md; if not,
   see <http://www.gnu.org/licenses/>.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "projfs.h"
#include "projfs_i.h"
#include "projfs_vfsapi.h"

static char *get_proc_cmdline(pid_t pid)
{
	char proc_cmdline_path[14 + 3*sizeof(pid_t) + 1];  // /proc/%d/cmdline
	int fd;
	char *cmdline = NULL;
	int len;

	sprintf(proc_cmdline_path, "/proc/%d/cmdline", pid);

	fd = open(proc_cmdline_path, O_RDONLY);
	if (fd < 0)
		goto out;

	/* While PATH_MAX may not be sufficent for pathological cases, we
	 * assume it's adaquate for all normal processes which might be
	 * accessing a git working directory.
	 *
	 * Also note that the contents of /proc/<pid>/cmdline will contain
	 * nul-separated command-line arguments, so in the usual case
	 * our buffer will have "extra" data after the actual command
	 * path, which will be ignored by our callers.  But we add a nul
	 * just in case we hit a pathologically long command path.
	 */
	cmdline = malloc(PATH_MAX + 1);
	if (cmdline == NULL)
		goto out_fd;

	len = read(fd, cmdline, PATH_MAX);
	if (len > 0)
		cmdline[len] = '\0';
	else {
		free(cmdline);
		cmdline = NULL;
	}

out_fd:
	close(fd);
out:
	return cmdline;
}

static int convert_result_to_errno(PrjFS_Result result)
{
	int ret = 0;

	switch (result) {
	case PrjFS_Result_Success:
		break;
	case PrjFS_Result_Pending:
		ret = EINPROGRESS;
		break;

	case PrjFS_Result_EInvalidArgs:
		ret = EINVAL;
		break;
	case PrjFS_Result_EInvalidOperation:
		ret = EPERM;
		break;
	case PrjFS_Result_ENotSupported:
		ret = ENOTSUP;
		break;

	case PrjFS_Result_EDriverNotLoaded:
		ret = ENODEV;
		break;
	case PrjFS_Result_EOutOfMemory:
		ret = ENOMEM;
		break;
	case PrjFS_Result_EFileNotFound:
	case PrjFS_Result_EPathNotFound:
		ret = ENOENT;
		break;
	case PrjFS_Result_EAccessDenied:
		ret = EPERM;
		break;
	case PrjFS_Result_EInvalidHandle:
		ret = EBADF;
		break;
	case PrjFS_Result_EIOError:
		ret = EIO;
		break;
	case PrjFS_Result_ENotYetImplemented:
		ret = ENOSYS;
		break;

	case PrjFS_Result_Invalid:
	default:
		ret = EINVAL;	// should imply an internal error, not client's
	}

	return -ret;		// return negated value for convenience
}

static int handle_nonproj_event(struct projfs_event *event, int perm)
{
	PrjFS_NotifyOperationCallback *callback =
		((PrjFS_Callbacks *) (event->user_data))->NotifyOperation;
	PrjFS_NotificationType notificationType = PrjFS_NotificationType_None;
	unsigned char providerId[PrjFS_PlaceholderIdLength];
	unsigned char contentId[PrjFS_PlaceholderIdLength];
	PrjFS_Result result;
	uint64_t mask = event->mask;
	char *cmdline = NULL;
	const char *triggeringProcessName = "";
	int ret = 0;

	if (callback == NULL)
		goto out;

	if (mask & PROJFS_DELETE_SELF)
		notificationType = PrjFS_NotificationType_PreDelete;
	else if (mask & PROJFS_CREATE_SELF)
		notificationType = PrjFS_NotificationType_NewFileCreated;
	// TODO: dispatch for additional event types

	if (notificationType == PrjFS_NotificationType_None)
		goto out;

	cmdline = get_proc_cmdline(event->pid);
	if (cmdline != NULL)
		triggeringProcessName = (const char *) cmdline;

	result = callback(0, event->path, providerId, contentId,
			  event->pid, triggeringProcessName,
			  (mask & PROJFS_ONDIR) ? 1 : 0,
			  notificationType, event->target_path);

	ret = convert_result_to_errno(result);

out:
	if (perm) {
		if (ret == 0)
			ret = PROJFS_ALLOW;
		else if (ret == -EPERM)
			ret = PROJFS_DENY;
	}

	if (cmdline != NULL)
		free(cmdline);

	return ret;
}

static int handle_notify_event(struct projfs_event *event)
{
	return handle_nonproj_event(event, 0);
}

static int handle_perm_event(struct projfs_event *event)
{
	return handle_nonproj_event(event, 1);
}

PrjFS_Result PrjFS_StartVirtualizationInstance(
    _In_    const char*                             storageRootFullPath,
    _In_    const char*                             virtualizationRootFullPath,
    _In_    PrjFS_Callbacks                         callbacks,
    _In_    unsigned int                            poolThreadCount,
    _Out_   PrjFS_MountHandle**                     mountHandle
)
{
	struct projfs *fs;
	struct projfs_handlers handlers;
	PrjFS_Callbacks *user_data;
	PrjFS_Result result = PrjFS_Result_Success;

	user_data = malloc(sizeof(callbacks));
	if (user_data == NULL) {
		result = PrjFS_Result_EOutOfMemory;
		goto out;
	}
	memcpy(user_data, &callbacks, sizeof(callbacks));

	handlers.handle_notify_event = handle_notify_event;
	handlers.handle_perm_event = handle_perm_event;

	// TODO: better error responses, where possible
	fs = projfs_new(storageRootFullPath, virtualizationRootFullPath,
			&handlers, sizeof(handlers), user_data);
	if (fs == NULL) {
		result = PrjFS_Result_Invalid;
		goto out;
	}

	// TODO: respect poolThreadCount
	(void) poolThreadCount;

	if (projfs_start(fs) != 0) {
		result = PrjFS_Result_Invalid;
		goto out_fs;
	}

	*mountHandle = (PrjFS_MountHandle *) fs;
	return result;

out_fs:
	projfs_stop(fs);
out:
	free(user_data);

	return result;
}

void PrjFS_StopVirtualizationInstance(
    _In_    const PrjFS_MountHandle*                mountHandle
)
{
	struct projfs *fs = (struct projfs *) mountHandle;
	void *user_data;

	user_data = projfs_stop(fs);
	free(user_data);
}

// TODO: likely unneeded; remove from VFS API and LinuxFileSystemVirtualizer
//       - OR -
//       use as a place to check for pre-existing mounts and clean them up
PrjFS_Result PrjFS_ConvertDirectoryToVirtualizationRoot(
    _In_    const char*                             virtualizationRootFullPath
)
{
	// TODO: remove from function signature if not used
	(void) virtualizationRootFullPath;

	return PrjFS_Result_Success;
}

