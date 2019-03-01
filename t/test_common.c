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

#define _GNU_SOURCE		// for basename() in <string.h>
				// and getopt_long() in <getopt.h>
				// and S_IS*() in <unistd.h>

#include "../include/config.h"

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "test_common.h"

#include "../include/projfs_notify.h"

#define MOUNT_ARGS_USAGE "<lower-path> <mount-path>"

#define MAX_RETVAL_NAME_LEN 40

/* For our test-only purposes, we can just limit paths to NAME_MAX, and then
 * pad our maximum allowed length of projection list entries a bit to account
 * for everything else (i.e., "type name [mode] [size] [source/link]").
 */
#define MAX_PROJLIST_PATH_LEN NAME_MAX
#define MAX_PROJLIST_ENTRY_LEN (MAX_PROJLIST_PATH_LEN * 2 + 100)

#define isquote(c) ((c) == '"' || (c) == '\'')

#define retval_entry(s) #s, -s

struct retval {
	const char *name;
	int val;
};

static const struct retval errno_retvals[] = {
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

static const struct option all_long_opts[] = {
	{ "help", no_argument, NULL, TEST_OPT_NUM_HELP },
	{ "retval", required_argument, NULL, TEST_OPT_NUM_RETVAL },
	{ "retval-file", required_argument, NULL, TEST_OPT_NUM_RETFILE },
	{ "projlist", required_argument, NULL, TEST_OPT_NUM_PROJLIST },
	{ "projlist-file", required_argument, NULL, TEST_OPT_NUM_PROJFILE },
	{ "timeout", required_argument, NULL, TEST_OPT_NUM_TIMEOUT },
	{ "lock-file", required_argument, NULL, TEST_OPT_NUM_LOCKFILE }
};

struct opt_usage {
	const char *usage;
	int req;
};

static const struct opt_usage all_opts_usage[] = {
	{ NULL, 1 },
	{ "allow|deny|null|<error>", 1 },
	{ "<retval-file>", 1 },
	{ "[d|f|s <name> [<dfmode> [<fsize> <fsource>]]|[<slink>]]...", 1 },
	{ "<projlist-file>", 1 },
	{ "<max-seconds>", 1 },
	{ "<lock-file>", 1 }
};

/* option values */
static int optval_retval;
static const char *optval_retfile;
static struct test_projlist_entry *optval_projlist;
static const char *optval_projfile;
static long int optval_timeout;
static const char *optval_lockfile;

static unsigned int opt_set_flags = TEST_OPT_NONE;

static const char *get_program_name(const char *program)
{
	const char *basename;

	basename = strrchr(program, '/');
	if (basename != NULL)
		program = basename + 1;

	// remove libtool script prefix, if any
	if (strncmp(program, "lt-", 3) == 0)
		program += 3;

	return program;
}

__attribute__((noreturn))
static void exit_usage(int err, const char *argv0, struct option *long_opts,
		       const char *args_usage)
{
	FILE *file = err ? stderr : stdout;

	fprintf(file, "Usage: %s", get_program_name(argv0));

	while (long_opts->name != NULL) {
		const struct opt_usage *opt_usage;

		opt_usage = &all_opts_usage[long_opts->val];

		fprintf(file, " %s--%s%s%s%s",
			(opt_usage->req ? "[" : ""),
			long_opts->name,
			(opt_usage->usage == NULL ? "" : " "),
			(opt_usage->usage == NULL ? "" : opt_usage->usage),
			(opt_usage->req ? "]" : ""));

		++long_opts;
	}

	fprintf(file, "%s%s\n",
		(*args_usage == '\0' ? "" : " "),
		args_usage);

	exit(err ? EXIT_FAILURE : EXIT_SUCCESS);
}

__attribute__((noreturn))
void test_exit_error(const char *argv0, const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "%s: ", get_program_name(argv0));

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	fprintf(stderr, "\n");

	exit(EXIT_FAILURE);
}

long int test_parse_long(const char *arg, int base)
{
	long int val;
	char *end;

	errno = 0;
	val = strtol(arg, &end, base);
	if (errno > 0 || end == arg || *end != '\0') {
		errno = EINVAL;
		val = 0;
	}

	return val;
}

int test_parse_retsym(const char *retsym, int *retval)
{
	int ret = -1;
	int i = 0;

	while (errno_retvals[i].name != NULL) {
		const char *name = errno_retvals[i].name;

		if (!strcasecmp(name, retsym)) {
			ret = 0;
			*retval = errno_retvals[i].val;
			break;
		}

		++i;
	}

	return ret;
}

static void read_retfile(int *retval, unsigned int *flags)
{
	FILE *file;
	char retsym[MAX_RETVAL_NAME_LEN + 1];

	file = fopen(optval_retfile, "r");
	if (file == NULL) {
		if (errno != ENOENT)
			warn("unable to open retval file: %s",
			     optval_retfile);
		goto out;
	}

	errno = 0;
	if (fgets(retsym, sizeof(retsym), file) != NULL) {
		char *c;

		c = strchr(retsym, '\n');
		if (c != NULL)
			*c = '\0';

		if (test_parse_retsym(retsym, retval) < 0) {
			warnx("invalid symbol in retval file: %s: %s",
			      optval_retfile, retsym);
		}

		*flags = TEST_VAL_SET | TEST_FILE_EXIST | TEST_FILE_VALID;
	}
	else if (errno > 0)
		warn("unable to read retval file: %s", optval_retfile);
	else
		*flags = TEST_FILE_EXIST;

	if (fclose(file) != 0)
		warn("unable to close retval file: %s", optval_retfile);

out:
	return;
}

static void append_projlist_entry(struct test_projlist_entry *entry,
				  struct test_projlist_entry **first_entry,
				  struct test_projlist_entry **last_entry)
{
	if (entry == NULL)
		return;

	if (*first_entry == NULL)
		*first_entry = entry;

	if (*last_entry != NULL)
		(*last_entry)->next = entry;
	*last_entry = entry;
}

/* We assume our locale has been set appropriately; e.g., LC_ALL=C
 * is set by test-lib.sh.
 */
static inline const char *skip_blanks(const char *s)
{
	while(*s != '\0' && isblank(*s))
		++s;
	return s;
}

/* We implement minimal quoting and escaping, just sufficient for our test
 * purposes.  Specifically, in single- or double-quoted strings, we support
 * the escape sequences \" \' \\ \n and \t with their usual meanings.
 */
static int parse_projlist_path(const char *s, int ispath, char **path)
{
	char buf[MAX_PROJLIST_PATH_LEN + 1];
	const char *e = s;
	char q = '\0';
	char c;
	size_t len = 0;

	if (isquote(*e)) {
		q = *e;
		++e;
	}

	c = *e;
	while (c != '\0' && len < sizeof(buf)) {
		if (q != '\0') {
			if (c == q) {
				++e;
				if (*e == '\0' || isblank(*e))
					break;
				else
					return -EINVAL;
			}
			else if (c == '\\') {
				++e;
				c = *e;

				switch (c) {
				case 'n':
					c = '\n';
					break;

				case 't':
					c = '\t';
					break;

				case '"':
				case '\'':
				case '\\':
					break;

				default:
					return -EINVAL;
				}
			}
		}
		else if (isblank(c)) {
			break;
		}

		if (c == '/' && !ispath)
			return -EINVAL;

		buf[len++] = c;
		c = *(++e);
	}

	if (len == 0)
		return -EINVAL;
	else if (len > MAX_PROJLIST_PATH_LEN)
		return -ENAMETOOLONG;

	buf[len] = '\0';

	*path = strdup(buf);
	if (*path == NULL)
		return -errno;

	return (e - s);
}

static void warn_projlist_path(int err, const char *field, const char *buf)
{
	if (err == ENOMEM) {
		warn("unable to allocate projection list entry %s", field);
	}
	else {
		warnx("invalid entry %s %sin projection list: %s", field,
		      (err == ENAMETOOLONG ? "(too long) " : ""), buf);
	}
}

static int parse_projlist_entry(const char *buf,
				struct test_projlist_entry **first_entry,
				struct test_projlist_entry **last_entry)
{
	struct test_projlist_entry entry = { 0 };
	struct test_projlist_entry *new_entry;
	const char *s = buf;
	int len;

	s = skip_blanks(s);
	if (*s == '\0' || *s == '#')
		return 0;

	if (isblank(*(s + 1))) {
		switch (*s) {
		case 'd':
			entry.mode = S_IFDIR;
			break;

		case 'f':
			entry.mode = S_IFREG;
			break;

		case 'l':
			entry.mode = S_IFLNK;
			break;

		default:
			break;
		}
	}
	if (entry.mode == 0) {
		warnx("invalid type flag in projection list: %s", buf);
		return -1;
	}

	s = skip_blanks(++s);
	if (*s == '\0') {
		warnx("missing entry name in projection list: %s", buf);
		return -1;
	}

	len = parse_projlist_path(s, 0, &entry.name);
	if (len < 0) {
		warn_projlist_path(-len, "name", buf);
		return -1;
	}
	s += len;

	if (S_ISDIR(entry.mode) || S_ISREG(entry.mode)) {
		int set = 0;

		s = skip_blanks(s);
		if (*s == '\0') {
			warnx("missing entry mode in projection list: %s",
			      buf);
			goto out_name;
		}
		else if (*s == '0' && *(s + 1) != 'x') {
			unsigned long int mode;
			char *e;

			errno = 0;
			mode = strtoul(s, &e, 0);
			len = e - s;
			if (errno == 0 && len >= 4 && len <= 5 &&
			    (isblank(*e) || *e == '\0')) {
				entry.mode |= mode;
				set = 1;
			}
			s = e;
		}

		if (!set) {
			warnx("invalid entry mode in projection list: %s",
			      buf);
			goto out_name;
		}
	}

	if (S_ISREG(entry.mode)) {
		s = skip_blanks(s);
		if (*s == '\0') {
			warnx("missing entry size in projection list: %s",
			      buf);
			goto out_name;
		}

		entry.size = -1;
		if (isdigit(*s)) {
			unsigned long long int size;
			char *e;

			errno = 0;
			size = strtoull(s, &e, 0);
			if (errno == 0 && (isblank(*e) || *e == '\0'))
				entry.size = size;
			s = e;
		}

		if (entry.size < 0) {
			warnx("invalid entry size in projection list: %s",
			      buf);
			goto out_name;
		}
	}

	if (S_ISLNK(entry.mode) || S_ISREG(entry.mode)) {
		const char *field;

		field = S_ISREG(entry.mode) ? "source path" : "target path";

		s = skip_blanks(s);
		if (*s == '\0') {
			warnx("missing entry %s in projection list: %s",
			      field, buf);
			goto out_name;
		}

		len = parse_projlist_path(s, 1, &entry.lnk_or_src);
		if (len < 0) {
			warn_projlist_path(-len, field, buf);
			goto out_name;
		}
		s += len;
	}

	s = skip_blanks(s);
	if (*s != '\0') {
		warnx("invalid extra fields in projection list: %s", buf);
		goto out_link;
	}

	new_entry = malloc(sizeof(entry));
	if (new_entry == NULL) {
		warn("unable to allocate projection list entry");
		goto out_link;
	}
	memcpy(new_entry, &entry, sizeof(entry));

	append_projlist_entry(new_entry, first_entry, last_entry);

	return 0;

out_link:
	if (entry.lnk_or_src != NULL)
		free(entry.lnk_or_src);
out_name:
	free(entry.name);

	return -1;
}

static int parse_projlist(const char *list,
			  struct test_projlist_entry **projlist)
{
	char buf[MAX_PROJLIST_ENTRY_LEN + 1];
	struct test_projlist_entry *first_entry = NULL, *last_entry = NULL;
	int ret = 0;

	while (*list != '\0') {
		const char *c;
		size_t len;

		c = strchr(list, '\n');
		if (c == NULL) {
			len = strlen(list);
			c = list + len;
		}
		else {
			len = c - list;
			++c;
		}

		if (len > MAX_PROJLIST_ENTRY_LEN) {
			warnx("invalid entry (line too long) in "
			      "projection list: %s", list);
			ret = -1;
			break;
		}

		memcpy(buf, list, len);
		buf[len] = '\0';

		ret = parse_projlist_entry(buf, &first_entry, &last_entry);
		if (ret < 0)
			break;

		list = c;
	}

	if (ret == 0)
		*projlist = first_entry;

	return ret;
}

static void read_projfile(struct test_projlist_entry **projlist,
			  unsigned int *flags)
{
	FILE *file;
	char buf[MAX_PROJLIST_ENTRY_LEN + 1];
	struct test_projlist_entry *first_entry = NULL, *last_entry = NULL;

	*projlist = NULL;

	file = fopen(optval_projfile, "r");
	if (file == NULL) {
		if (errno != ENOENT) {
			warn("unable to open projection list file: %s",
			     optval_projfile);
		}
		goto out;
	}

	errno = 0;
	while (fgets(buf, sizeof(buf), file) != NULL) {
		char *c;

		c = strchr(buf, '\n');
		if (c != NULL) {
			*c = '\0';
		}
		else if (!feof(file)) {
			warnx("invalid entry (line too long) in "
			      "projection list file: %s: %s",
			      optval_projfile, buf);
			goto out_close;
		}

		if (parse_projlist_entry(buf, &first_entry, &last_entry) < 0)
			goto out_close;
	}

	if (errno > 0) {
		warn("unable to read projection list file: %s",
		     optval_projfile);
	}
	else if (last_entry == NULL) {
		*flags = TEST_FILE_EXIST;
	}
	else {
		*projlist = first_entry;
		*flags = TEST_VAL_SET | TEST_FILE_EXIST | TEST_FILE_VALID;
	}

out_close:
	if (fclose(file) != 0) {
		warn("unable to close projection list file: %s",
		     optval_projfile);
	}

out:
	return;
}

static struct option *get_long_opts(unsigned int opt_flags)
{
	struct option *long_opts;
	unsigned int tmp_flags = opt_flags;
	size_t num_opts = 0;
	int opt_idx = 0;
	int opt_num = 0;

	// slow counting, but obvious, and only needs to execute once
	while (tmp_flags > 0) {
		if ((tmp_flags & 1) == 1)
			++num_opts;
		tmp_flags >>= 1;
	}

	long_opts = calloc(num_opts + 1, sizeof(struct option));
	if (long_opts == NULL) {
		fprintf(stderr, "unable to allocate options array: %s\n",
			strerror(errno));
		exit(EXIT_FAILURE);
	}

	while (opt_flags > 0) {
		unsigned int opt_flag = (0x0001 << opt_num);

		if ((opt_flags & opt_flag) > 0)
			memcpy(&long_opts[opt_idx++], &all_long_opts[opt_num],
			       sizeof(struct option));

		opt_flags &= ~opt_flag;
		++opt_num;
	}

	return long_opts;
}

void test_parse_opts(int argc, char *const argv[], unsigned int opt_flags,
		     int min_args, int max_args, char *args[],
		     const char *args_usage)
{
	struct option *long_opts;
	int num_args;
	int arg_idx = 0;
	int err = 0;
	int val;

	opt_flags |= TEST_OPT_HELP;

	long_opts = get_long_opts(opt_flags);

	opterr = 0;
	do {
		val = getopt_long(argc, argv, "h", long_opts, NULL);
		if (val < 0)
			break;

		switch (val) {
		case 'h':
		case TEST_OPT_NUM_HELP:
			exit_usage(0, argv[0], long_opts, args_usage);

		case TEST_OPT_NUM_RETVAL:
			if (test_parse_retsym(optarg, &optval_retval) < 0)
				test_exit_error(argv[0],
						"invalid retval symbol: %s",
						optarg);
			opt_set_flags |= TEST_OPT_RETVAL;
			break;

		case TEST_OPT_NUM_RETFILE:
			optval_retfile = optarg;
			opt_set_flags |= TEST_OPT_RETFILE;
			break;

		case TEST_OPT_NUM_PROJLIST:
			if (parse_projlist(optarg, &optval_projlist) < 0)
				test_exit_error(argv[0],
						"invalid projection list: %s",
						optarg);
			opt_set_flags |= TEST_OPT_PROJLIST;
			break;

		case TEST_OPT_NUM_PROJFILE:
			optval_projfile = optarg;
			opt_set_flags |= TEST_OPT_PROJFILE;
			break;

		case TEST_OPT_NUM_TIMEOUT:
			optval_timeout = test_parse_long(optarg, 10);
			if (errno > 0 || optval_timeout < 0)
				test_exit_error(argv[0],
						"invalid timeout: %s",
						optarg);
			opt_set_flags |= TEST_OPT_TIMEOUT;
			break;

		case TEST_OPT_NUM_LOCKFILE:
			optval_lockfile = optarg;
			opt_set_flags |= TEST_OPT_LOCKFILE;
			break;

		case '?':
			if (optopt > 0)
				test_exit_error(argv[0],
						"invalid option: -%c",
						optopt);
			test_exit_error(argv[0], "invalid option: %s",
					argv[optind - 1]);

		default:
			test_exit_error(argv[0], "unknown getopt code: %d",
					val);
		}
	}
	while (!err);

	num_args = argc - optind;
	if (err || num_args < min_args || num_args > max_args)
		exit_usage(1, argv[0], long_opts, args_usage);

	while (optind < argc)
		args[arg_idx++] = argv[optind++];

	while (num_args++ < max_args)
		args[arg_idx++] = NULL;
}

void test_parse_mount_opts(int argc, char *const argv[],
			   unsigned int opt_flags,
			   const char **lower_path, const char **mount_path)
{
	char *args[2];

	test_parse_opts(argc, argv, opt_flags, 2, 2, args, MOUNT_ARGS_USAGE);

	*lower_path = args[0];
	*mount_path = args[1];
}

unsigned int test_get_opts(unsigned int opt_flags, ...)
{
	unsigned int opt_flag = TEST_OPT_HELP;
	unsigned int ret_flags = TEST_OPT_NONE;
	va_list ap;

	va_start(ap, opt_flags);

	while (opt_flags != TEST_OPT_NONE) {
		unsigned int ret_flag;
		struct test_projlist_entry **e;
		unsigned int *f;
		int *i;
		long int *l;
		const char **s;

		opt_flag <<= 1;
		if ((opt_flags & opt_flag) == TEST_OPT_NONE)
			continue;
		opt_flags &= ~opt_flag;

		ret_flag = opt_set_flags & opt_flag;
		ret_flags |= ret_flag;

		switch (opt_flag) {
			case TEST_OPT_RETVAL:
				i = va_arg(ap, int*);
				f = va_arg(ap, unsigned int*);
				*f = TEST_VAL_UNSET | TEST_FILE_NONE;
				if (ret_flag != TEST_OPT_NONE) {
					*i = optval_retval;
					*f |= TEST_VAL_SET;
				} else if ((opt_set_flags & TEST_OPT_RETFILE)
					   != TEST_OPT_NONE) {
					read_retfile(i, f);
					ret_flags |= opt_flag;
				}
				break;

			case TEST_OPT_RETFILE:
				s = va_arg(ap, const char**);
				if (ret_flag != TEST_OPT_NONE)
					*s = optval_retfile;
				break;

			case TEST_OPT_PROJLIST:
				e = va_arg(ap, struct test_projlist_entry**);
				f = va_arg(ap, unsigned int*);
				*f = TEST_VAL_UNSET | TEST_FILE_NONE;
				if (ret_flag != TEST_OPT_NONE) {
					*e = optval_projlist;
					*f |= TEST_VAL_SET;
				} else if ((opt_set_flags & TEST_OPT_PROJFILE)
					   != TEST_OPT_NONE) {
					read_projfile(e, f);
					ret_flags |= opt_flag;
				}
				break;
			case TEST_OPT_PROJFILE:
				s = va_arg(ap, const char**);
				if (ret_flag != TEST_OPT_NONE)
					*s = optval_projfile;
				break;

			case TEST_OPT_TIMEOUT:
				l = va_arg(ap, long int*);
				if (ret_flag != TEST_OPT_NONE)
					*l = optval_timeout;
				break;

			case TEST_OPT_LOCKFILE:
				s = va_arg(ap, const char**);
				if (ret_flag != TEST_OPT_NONE)
					*s = optval_lockfile;
				break;

			default:
				errx(EXIT_FAILURE,
				     "unknown option flag: %u", opt_flag);
		}
	}

	va_end(ap);

	return ret_flags;
}

void test_free_opts(void)
{
	struct test_projlist_entry *next_entry, *entry = optval_projlist;

	while (entry != NULL) {
		next_entry = entry->next;

		free(entry->name);
		if (entry->lnk_or_src != NULL)
			free(entry->lnk_or_src);
		free(entry);

		entry = next_entry;
	}
}

struct projfs *test_start_mount(const char *lowerdir, const char *mountdir,
				const struct projfs_handlers *handlers,
				size_t handlers_size, void *user_data)
{
	struct projfs *fs;

	fs = projfs_new(lowerdir, mountdir, handlers, handlers_size,
			user_data);

	if (fs == NULL)
		errx(EXIT_FAILURE, "unable to create filesystem");

	if (projfs_start(fs) < 0)
		errx(EXIT_FAILURE, "unable to start filesystem");

	return fs;
}

void *test_stop_mount(struct projfs *fs)
{
	return projfs_stop(fs);
}

static void signal_handler(int sig)
{
	(void) sig;
}

void test_wait_signal(void)
{
	int tty = isatty(STDIN_FILENO);

	if (tty == 1) {
		printf("hit Enter to stop: ");
		getchar();
	}
	else if (errno == EINVAL || errno == ENOTTY) {
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
	else
		warn("unable to check stdin");
}

