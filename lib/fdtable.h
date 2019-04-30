/* Linux Projected Filesystem
   Copyright (C) 2019 GitHub, Inc.

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

#ifndef _FDTABLE_H
#define _FDTABLE_H

#define MAX_TABLE_SIZE 65536

struct fdtable;

struct fdtable *fdtable_create(void);
void fdtable_destroy(struct fdtable *table);

int fdtable_insert(struct fdtable *table, int fd, pid_t pid);
int fdtable_replace(struct fdtable *table, int fd, pid_t pid);
int fdtable_remove(struct fdtable *table, int fd, pid_t *pid);

#endif /* _FDTABLE_H */
