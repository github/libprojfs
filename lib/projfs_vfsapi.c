/* VFSForGit ProjFS API
   Copyright (C) 2018-2019 GitHub, Inc.

   See the NOTICE file distributed with this library for additional
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
   License along with this library, in the file COPYING; if not,
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
#include "projfs_vfsapi.h"

/* NOTE: projfs_i.h should not be included;
 *       use the public projfs API only in VFS code
 */

struct _PrjFS_FileHandle
{
	int fd;
};

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

static PrjFS_Result convert_errno_to_result(int err)
{
	PrjFS_Result result = PrjFS_Result_Invalid;

	switch(err) {
	case 0:
		result = PrjFS_Result_Success;
		break;

	case EACCES:
	case EEXIST:
	case EPERM:
	case EROFS:
		result = PrjFS_Result_EAccessDenied;
		break;
	case EBADF:
		result = PrjFS_Result_EInvalidHandle;
		break;
	case EDQUOT:
	case EIO:
	case ENODATA:	// also ENOATTR; see getxattr(2)
	case ENOSPC:
		result = PrjFS_Result_EIOError;
		break;
	case EFAULT:
	case EINVAL:
	case EOVERFLOW:
		result = PrjFS_Result_EInvalidArgs;
		break;
	case ELOOP:
	case EMLINK:
	case ENAMETOOLONG:
	case ENOENT:
	case ENOTDIR:
		result = PrjFS_Result_EPathNotFound;
		break;
	case ENOMEM:
		result = PrjFS_Result_EOutOfMemory;
		break;
	case ENOSYS:
		result = PrjFS_Result_ENotYetImplemented;
		break;
	case ENOTEMPTY:
		result = PrjFS_Result_EDirectoryNotEmpty;
		break;
	case ENOTSUP:
		result = PrjFS_Result_ENotSupported;
		break;

	default:
		result = PrjFS_Result_Invalid;
	}

	return result;
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
	case PrjFS_Result_EVirtualizationInvalidOperation:
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
	case PrjFS_Result_EDirectoryNotEmpty:
		ret = ENOTEMPTY;
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

static int handle_proj_event(struct projfs_event *event)
{
	PrjFS_Callbacks *callbacks;
	PrjFS_Result result;
	char *cmdline = NULL;
	const char *triggeringProcessName = "";
	int ret = 0;

	callbacks = (PrjFS_Callbacks *) projfs_get_user_data(event->fs);
	if (callbacks == NULL)
		goto out;

	cmdline = get_proc_cmdline(event->pid);
	if (cmdline != NULL)
		triggeringProcessName = (const char *) cmdline;

	if (event->mask & PROJFS_ONDIR) {
		PrjFS_EnumerateDirectoryCallback *callback =
			callbacks->EnumerateDirectory;

		if (callback == NULL)
			goto out;

		result = callback(0, event->path,
				  event->pid, triggeringProcessName);
	}
	else {
		PrjFS_GetFileStreamCallback *callback =
			callbacks->GetFileStream;
		unsigned char providerId[PrjFS_PlaceholderIdLength];
		unsigned char contentId[PrjFS_PlaceholderIdLength];
		PrjFS_FileHandle fh = { event->fd };

		if (callback == NULL)
			goto out;

		result = callback(0, event->path, providerId, contentId,
				  event->pid, triggeringProcessName, &fh);
	}

	ret = convert_result_to_errno(result);

out:
	if (cmdline != NULL)
		free(cmdline);

	return ret;
}

static int handle_nonproj_event(struct projfs_event *event, int perm)
{
	PrjFS_Callbacks *callbacks;
	PrjFS_NotifyOperationCallback *callback;
	PrjFS_NotificationType notificationType = PrjFS_NotificationType_None;
	unsigned char providerId[PrjFS_PlaceholderIdLength];
	unsigned char contentId[PrjFS_PlaceholderIdLength];
	PrjFS_Result result;
	uint64_t mask = event->mask;
	char *cmdline = NULL;
	const char *triggeringProcessName = "";
	int ret = 0;

	callbacks = (PrjFS_Callbacks *) projfs_get_user_data(event->fs);
	if (callbacks == NULL)
		goto out;

	callback = callbacks->NotifyOperation;
	if (callback == NULL)
		goto out;

	if (mask & PROJFS_DELETE_SELF)
		notificationType = PrjFS_NotificationType_PreDelete;
	else if (mask & PROJFS_MOVE_SELF)
		notificationType = PrjFS_NotificationType_FileRenamed;
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

	handlers.handle_proj_event = handle_proj_event;
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

PrjFS_Result PrjFS_WritePlaceholderDirectory(
    _In_    const PrjFS_MountHandle*                mountHandle,
    _In_    const char*                             relativePath
)
{
	struct projfs *fs = (struct projfs *) mountHandle;
	int ret;

	ret = projfs_create_proj_dir(fs, relativePath);

	return convert_errno_to_result(ret);
}

PrjFS_Result PrjFS_WritePlaceholderFile(
    _In_    const PrjFS_MountHandle*                mountHandle,
    _In_    const char*                             relativePath,
    _In_    unsigned char                           providerId[PrjFS_PlaceholderIdLength],
    _In_    unsigned char                           contentId[PrjFS_PlaceholderIdLength],
    _In_    unsigned long                           fileSize,
    _In_    uint16_t                                fileMode
)
{
	struct projfs *fs = (struct projfs *) mountHandle;
	int ret;

	// TODO: need to wrap the content+provider IDs into a private struct
	//	 until then, prevent compiler warnings
	ret = projfs_create_proj_file(fs, relativePath, fileSize, fileMode);
	(void)providerId;
	(void)contentId;

	return convert_errno_to_result(ret);
}

PrjFS_Result PrjFS_WriteSymLink(
    _In_    const PrjFS_MountHandle*                mountHandle,
    _In_    const char*                             relativePath,
    _In_    const char*                             symLinkTarget
)
{
	struct projfs *fs = (struct projfs *) mountHandle;
	int ret;

	ret = projfs_create_proj_symlink(fs, relativePath, symLinkTarget);

	return convert_errno_to_result(ret);
}

PrjFS_Result PrjFS_WriteFileContents(
    _In_    const PrjFS_FileHandle*                 fileHandle,
    _In_    const void*                             bytes,
    _In_    unsigned int                            byteCount
)
{
	int fd = fileHandle->fd;

	while (byteCount) {
		ssize_t res = write(fd, bytes, byteCount);
		if (res == -1)
			return convert_errno_to_result(errno);
		bytes = (void *)(((uintptr_t)bytes) + res);
		byteCount -= res;
	}

	return PrjFS_Result_Success;
}
