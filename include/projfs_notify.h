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
   License along with this library, in the file COPYING; if not,
   see <http://www.gnu.org/licenses/>.
*/

#ifndef PROJFS_NOTIFY_H
#define PROJFS_NOTIFY_H

/** @file
 *
 * This file defines the event notification interface of ProjFS
 */

#include <sys/types.h>			/* for pid_t, etc. */

#ifdef __cplusplus
extern "C" {
#endif

#define himask(x)	(((uint64_t)x) << 32)

/** Filesystem events which may be reported */
#define PROJFS_CLOSE_WRITE	0x00000008	/* Writable file closed */
#define PROJFS_MOVE		0x000000C0	/* File/dir moved (TO+FROM) */
#define PROJFS_CREATE		0x00000100	/* File/dir created */
#define PROJFS_OPEN_PERM	0x00010000	/* File open perm (wr only) */
#define PROJFS_DELETE_PERM	himask(0x0001)	/* Delete permission */

/** Filesystem event flags */
#define PROJFS_ONDIR		0x40000000	/* Event occurred on dir */

/** Event permission handler responses */
#define PROJFS_ALLOW		0x01
#define PROJFS_DENY		0x02

#ifdef __cplusplus
}
#endif


/*
 * This interface should match that defined by the fanotify/inotify APIs
 * and underlying fsnotify interface.
 */

#ifdef HAVE_SYS_FANOTIFY_H
#include <sys/fanotify.h>

#if (PROJFS_CLOSE_WRITE	!= FAN_CLOSE_WRITE ||	\
     PROJFS_OPEN_PERM	!= FAN_OPEN_PERM ||	\
     PROJFS_ONDIR	!= FAN_ONDIR ||		\
     PROJFS_ALLOW	!= FAN_ALLOW ||		\
     PROJFS_DENY	!= FAN_DENY)
#error "Projfs notification API out of sync with sys/fanotify.h API"
#endif
#endif /* HAVE_SYS_FANOTIFY_H */

#ifdef HAVE_SYS_INOTIFY_H
#include <sys/inotify.h>

#if (PROJFS_CLOSE_WRITE	!= IN_CLOSE_WRITE ||	\
     PROJFS_MOVE	!= IN_MOVE ||	\
     PROJFS_CREATE	!= IN_CREATE ||	\
     PROJFS_ONDIR	!= IN_ISDIR)
#error "Projfs notification API out of sync with sys/inotify.h API"
#endif
#endif /* HAVE_SYS_INOTIFY_H */

#endif /* PROJFS_NOTIFY_H */
