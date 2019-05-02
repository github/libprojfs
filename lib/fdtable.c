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

#define _GNU_SOURCE

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fdtable.h"

/*
 * We implement an open-addressed hash table with linear probing,
 * using a single array containing both keys (file descriptors)
 * and values (pids).
 *
 * Because fds are guaranteed to be non-negative, we can also use the key of
 * each pair to store 'empty' and 'removed' sentinels as negative integers.
 *
 * Our array size is always a power of two, to simplify reducing our hash
 * values to smaller index values.  As we need to keep the load factor of
 * our table below 0.7, we resize the array when the number of stored keys
 * is greater than 2/3 the array length, or less than 1/6 the array length.
 *
 * For our present purposes we can assume all lookups succeed, including
 * insertions (since fds are unique, there can be no conflicts), and all
 * removals, as we remove only during the FUSE release operation, which is
 * guaranteed to be called exactly once per fd.
 *
 * We don't split our keys and values into distinct arrays as testing
 * indicates that successful lookup poperations are slightly faster with a
 * single array, given our very small data size (typically eight bytes per
 * key/value pair).  Since we expect to never have failed lookups, distinct
 * key and value arrays do not appear to give any performance advantage.
 *
 * While file descriptors generally increase monotonically, we can't rely
 * on that universally, as the Linux VFS will recycle descriptors which have
 * been closed.
 *
 * The number of table entries we have to probe in the case of collisions
 * of our hash function is minimized, given our often near-sequential
 * keys, by using an implementation of Knuth's multiplicative (Fibonacci)
 * hashing with a prime factor very near the product of 2^32 and the golden
 * ratio conjugate (captial Phi), as described in:
 *
 * https://dl.acm.org/citation.cfm?id=1268381
 * http://www.citi.umich.edu/projects/linux-scalability/reports/hash.html
 *
 * https://en.wikipedia.org/wiki/Universal_hashing#Avoiding_modular_arithmetic
 *
 * While many sources emphasize use of the high bits of the full hash value
 * when reducing to an array index, testing with both random and,
 * especially, sequential and mostly-sequential key values indicates we
 * achieve more single-probe-only lookups and shorter maximum probe lengths
 * when using the lower bits of the hash for the array index.
 */

struct fd_pid_entry {
	int fd;
	pid_t pid;
};

struct fdtable {
	unsigned int size;
	unsigned int used;
	uint32_t mask;
	long l1cache_linesize;
	struct fd_pid_entry *array;
	pthread_mutex_t mutex;
};

#define DEFAULT_CACHE_LINESIZE 64

#define DEFAULT_TABLE_SIZE 32
#define MIN_TABLE_SIZE DEFAULT_TABLE_SIZE

static int create_array(struct fdtable *table, unsigned int table_size)
{
	void *array;
	size_t array_bytes;

	array_bytes = table_size * sizeof(*table->array);

	if (posix_memalign(&array, table->l1cache_linesize,
			   array_bytes) != 0) {
		errno = ENOMEM;
		return -1;
	}

	// fill array with int values corresponding to SENTINEL_EMPTY (-1)
	memset(array, 0xFF, array_bytes);

	table->size = table_size;
	table->mask = table_size - 1;
	table->array = array;

	return 0;
}

struct fdtable *fdtable_create(void)
{
	struct fdtable *table;

	table = calloc(1, sizeof(*table));
	if (table == NULL)
		return NULL;

	table->l1cache_linesize = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
	if (table->l1cache_linesize == -1)
		table->l1cache_linesize = DEFAULT_CACHE_LINESIZE;

	if (create_array(table, DEFAULT_TABLE_SIZE) == -1)
		goto out_table;

	if (pthread_mutex_init(&table->mutex, NULL) != 0)
		goto out_array;

	return table;

out_array:
	free(table->array);
out_table:
	free(table);
	return NULL;
}

#define SENTINEL_EMPTY -1
#define SENTINEL_REMOVED -2

enum entry_operation {
	OP_INSERT = 0,
	OP_REHASH,
	OP_REPLACE,
	OP_REMOVE
};

enum update_result {
	UPDATE_ERROR = -1,
	UPDATE_SUCCESS,
	UPDATE_CONTINUE
};

static enum update_result
try_update_entry(struct fdtable *table, unsigned int index,
		 int fd, pid_t *pid, enum entry_operation op)
{
	struct fd_pid_entry *entry = &table->array[index];
	int entry_fd = entry->fd;

	switch (op) {
	case OP_INSERT:
	case OP_REHASH:
		if (entry_fd > SENTINEL_EMPTY)
			return UPDATE_CONTINUE;
		entry->fd = fd;
		entry->pid = *pid;
		if (op == OP_INSERT)
			++table->used;
		break;
	case OP_REPLACE:
	case OP_REMOVE:
		if (entry_fd == fd) {
			if (op == OP_REPLACE) {
				entry->pid = *pid;
			} else {
				entry->fd = SENTINEL_REMOVED;
				*pid = entry->pid;
				--table->used;
			}
		} else {
			/* We should never reach the end of a cluster of
			 * entries; this would imply a FUSE error.
			 */
			return (entry_fd == SENTINEL_EMPTY) ? UPDATE_ERROR
							    : UPDATE_CONTINUE;
		}
	default:
		break;
	}

	return UPDATE_SUCCESS;
}

// prime near 2^32 * golden ratio conjugate
#define GOLDEN_RATIO_PRIME 2654435761U

static inline unsigned int hash32_index(uint32_t key, uint32_t mask)
{
	return (key * GOLDEN_RATIO_PRIME) & mask;
}

static int update_entry(struct fdtable *table, int fd, pid_t *pid,
			enum entry_operation op)
{
	unsigned int index = hash32_index(fd, table->mask);
	unsigned int i;
	enum update_result res;

	for (i = index; i < table->size; ++i) {
		res = try_update_entry(table, i, fd, pid, op);
		if (res != UPDATE_CONTINUE)
			return (res == UPDATE_SUCCESS) ? 0 : -1;
	}
	for (i = 0; i < index; ++i) {
		res = try_update_entry(table, i, fd, pid, op);
		if (res != UPDATE_CONTINUE)
			return (res == UPDATE_SUCCESS) ? 0 : -1;
	}

	/* We should never exhaust our table as we resize before updating
	 * (or fail while resizing), so we should never reach here.
	 */
	return -1;
}

static int resize_table(struct fdtable *table, unsigned int new_size)
{
	struct fd_pid_entry *array = table->array;
	unsigned int old_size = table->size;
	unsigned int i;

	if (new_size > MAX_TABLE_SIZE) {
		errno = ENOMEM;
		return -1;
	}

	if (create_array(table, new_size) == -1)
		return -1;

	for (i = 0; i < old_size; ++i) {
		if (array[i].fd < 0)
			continue;
		// we know table size is sufficient so ignore return code
		(void)update_entry(table, array[i].fd, &array[i].pid,
				   OP_REHASH);
	}

	return 0;
}

#define max_load(sz) (2 * (sz) / 3)
#define min_load(sz) (1 * (sz) / 6)

int fdtable_insert(struct fdtable *table, int fd, pid_t pid)
{
	int ret;

	pthread_mutex_lock(&table->mutex);

	if (table->used + 1 > max_load(table->size)) {
		ret = resize_table(table, table->size * 2);
		if (ret == -1)
			goto out;
	}
	ret = update_entry(table, fd, &pid, OP_INSERT);

out:
	pthread_mutex_unlock(&table->mutex);
	return ret;
}

int fdtable_replace(struct fdtable *table, int fd, pid_t pid)
{
	int ret;

	pthread_mutex_lock(&table->mutex);
	ret = update_entry(table, fd, &pid, OP_REPLACE);
	pthread_mutex_unlock(&table->mutex);
	return ret;
}

int fdtable_remove(struct fdtable *table, int fd, pid_t *pid)
{
	int ret;

	pthread_mutex_lock(&table->mutex);

	if (table->size > MIN_TABLE_SIZE &&
	    table->used - 1 < min_load(table->size)) {
		ret = resize_table(table, table->size / 2);
		if (ret == -1)
			goto out;
	}
	ret = update_entry(table, fd, pid, OP_REMOVE);

out:
	pthread_mutex_unlock(&table->mutex);
	return ret;
}

void fdtable_destroy(struct fdtable *table)
{
	pthread_mutex_destroy(&table->mutex);
	free(table->array);
	free(table);
}
