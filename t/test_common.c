/* Linux Projected Filesystem
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
   License along with this library, in the file COPYING.LIB; if not,
   see <http://www.gnu.org/licenses/>.
*/

#define _GNU_SOURCE		// for basename() in <string.h>

#include "../include/config.h"

#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "test_common.h"

#include "../include/projfs_notify.h"

#define RETVAL_OPT_NAME "--retval"
#define RETVAL_OPT_HELP "allow|deny|null|<error>"

#define retval_entry(s) #s, -s

struct retval {
	const char *name;
	int val;
};

// list based on VFS API convert_result_to_errno()
static struct retval errno_retvals[] = {
	{ "null",	0		},
	{ "allow", 	PROJFS_ALLOW	},
	{ "deny",	PROJFS_DENY	},
	{ retval_entry(EBADF)		},
	{ retval_entry(EINPROGRESS)	},
	{ retval_entry(EINVAL)		},
	{ retval_entry(EIO)		},
	{ retval_entry(ENODEV)		},
	{ retval_entry(ENOENT)		},
	{ retval_entry(ENOMEM)		},
	{ retval_entry(ENOTSUP)		},
	{ retval_entry(EPERM)		},
	{ retval_entry(ENOSYS)		},
	{ NULL,		0		}
};

#define VFSAPI_PREFIX "PrjFS_Result_"
#define VFSAPI_PREFIX_LEN (sizeof(VFSAPI_PREFIX) - 1)

#ifdef PROJFS_VFSAPI
#define get_retvals(v) ((v) ? vfsapi_retvals : errno_retvals)

#define retval_vfsapi_entry(s) #s, s

// list based on VFS API convert_result_to_errno()
static struct retval vfsapi_retvals[] = {
	{ "null",	PrjFS_Result_Invalid			},
	{ "allow",	PrjFS_Result_Success			},
	{ "deny",	PrjFS_Result_EAccessDenied		},
	{ retval_vfsapi_entry(PrjFS_Result_Invalid)		},
	{ retval_vfsapi_entry(PrjFS_Result_Success)		},
	{ retval_vfsapi_entry(PrjFS_Result_Pending)		},
	{ retval_vfsapi_entry(PrjFS_Result_EInvalidArgs)	},
	{ retval_vfsapi_entry(PrjFS_Result_EInvalidOperation)	},
	{ retval_vfsapi_entry(PrjFS_Result_ENotSupported)	},
	{ retval_vfsapi_entry(PrjFS_Result_EDriverNotLoaded)	},
	{ retval_vfsapi_entry(PrjFS_Result_EOutOfMemory)	},
	{ retval_vfsapi_entry(PrjFS_Result_EFileNotFound)	},
	{ retval_vfsapi_entry(PrjFS_Result_EPathNotFound)	},
	{ retval_vfsapi_entry(PrjFS_Result_EAccessDenied)	},
	{ retval_vfsapi_entry(PrjFS_Result_EInvalidHandle)	},
	{ retval_vfsapi_entry(PrjFS_Result_EIOError)		},
	{ retval_vfsapi_entry(PrjFS_Result_ENotYetImplemented)	},
	{ NULL,		0					}
};
#else /* !PROJFS_VFSAPI */
#define get_retvals(v) errno_retvals
#endif /* !PROJFS_VFSAPI */

int tst_find_retval(int vfsapi, const char *retname, const char *optname)
{
	const struct retval *retvals = get_retvals(vfsapi);
	int i = 0;

	while (retvals[i].name != NULL) {
		const char *name = retvals[i].name;

		if (!strcasecmp(name, retname) ||
		    (vfsapi &&
		     !strncmp(name, VFSAPI_PREFIX, VFSAPI_PREFIX_LEN) &&
		     !strcasecmp(name + VFSAPI_PREFIX_LEN, retname)))
			return retvals[i].val;
		++i;
	}

	errx(EXIT_FAILURE, "invalid %s option: %s",
	     optname, retname);
}

void tst_parse_opts(int argc, const char **argv, int vfsapi,
		    const char **lower_path, const char **mount_path,
		    int *retval)
{
	const char *retname = NULL;
	int arg_offset = 0;

	if (retval != NULL && argc > 2 && !strcmp(argv[1], RETVAL_OPT_NAME)) {
		retname = argv[2];
		arg_offset = 2;
	}

	if (argc - arg_offset != 3) {
		fprintf(stderr, "Usage: %s ", basename(argv[0]));
		if (retval != NULL) {
			fprintf(stderr, "\\\n\t[%s %s] ",
				RETVAL_OPT_NAME, RETVAL_OPT_HELP);
		}
		fprintf(stderr, "<lower-path> <mount-path>\n");
		exit(EXIT_FAILURE);
	}

	*lower_path = argv[1 + arg_offset];
	*mount_path = argv[2 + arg_offset];

	if (retval != NULL) {
		if (retname == NULL)
			*retval = RETVAL_DEFAULT;
		else
			*retval = tst_find_retval(vfsapi, retname,
						  RETVAL_OPT_NAME);
	}
}

struct projfs *tst_start_mount(const char *lowerdir, const char *mountdir,
			       const struct projfs_handlers *handlers,
			       size_t handlers_size, void *user_data)
{
	struct projfs *fs;

	fs = projfs_new(lowerdir, mountdir, handlers, handlers_size,
			user_data);

	if (fs == NULL)
		err(EXIT_FAILURE, "unable to create filesystem");

	if (projfs_start(fs) < 0)
		err(EXIT_FAILURE, "unable to start filesystem");

	return fs;
}

void *tst_stop_mount(struct projfs *fs)
{
	return projfs_stop(fs);
}

#ifdef PROJFS_VFSAPI
void tst_start_vfsapi_mount(const char *storageRootFullPath,
			    const char *virtualizationRootFullPath,
			    PrjFS_Callbacks callbacks,
			    unsigned int poolThreadCount,
			    PrjFS_MountHandle** mountHandle)
{
	PrjFS_Result ret;

	ret = PrjFS_StartVirtualizationInstance(storageRootFullPath,
						virtualizationRootFullPath,
						callbacks, poolThreadCount,
						mountHandle);

	if (ret != PrjFS_Result_Success)
		err(EXIT_FAILURE, "unable to start filesystem: %d", ret);
}

void tst_stop_vfsapi_mount(PrjFS_MountHandle* mountHandle)
{
	PrjFS_StopVirtualizationInstance(mountHandle);
}
#endif /* PROJFS_VFSAPI */

static void signal_handler(int sig)
{
	(void) sig;
}

void tst_wait_signal(void)
{
	int tty = isatty(STDIN_FILENO);

	if (tty < 0)
		warn("unable to check stdin");
	else if (tty) {
		printf("hit Enter to stop: ");
		getchar();
	}
	else {
		struct sigaction sa;

		memset(&sa, 0, sizeof(struct sigaction));
		sa.sa_handler = signal_handler;
		sigemptyset(&(sa.sa_mask));
		sa.sa_flags = 0;

		/* replace libfuse's handler so we can exit tests cleanly */
		if (sigaction(SIGTERM, &sa, 0) < 0)
			warn("unable to set signal handler");
		else
			pause();
	}
}

