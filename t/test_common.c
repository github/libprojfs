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

/* Limit the maximum allowed length of attribute list entries, and the
 * maximum number of entries, because some filesystems like ext4 have
 * their total xattr storage space limited to a single filesystem block,
 * which may be as small as 1 kB.
 */
#define MAX_ATTRLIST_ENTRY_LEN 256
#define MAX_ATTRLIST_TOTAL_LEN 1024

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
	{ "attrlist", required_argument, NULL, TEST_OPT_NUM_ATTRLIST },
	{ "attrlist-file", required_argument, NULL, TEST_OPT_NUM_ATTRFILE },
	{ "timeout", required_argument, NULL, TEST_OPT_NUM_TIMEOUT },
	{ "lock-file", required_argument, NULL, TEST_OPT_NUM_LOCKFILE }
};

static const char *const all_mount_opts[] = {
	"--debug",
	"--initial",
	"--log=",
	NULL
};

struct opt_usage {
	const char *usage;
	int req;
};

static const struct opt_usage all_opts_usage[] = {
	{ NULL, 1 },
	{ "allow|deny|null|<error>", 1 },
	{ "<retval-file>", 1 },
	{ "[<name> <value>]...", 1 },
	{ "<attrlist-file>", 1 },
	{ "<max-seconds>", 1 },
	{ "<lock-file>", 1 }
};

static int optval_retval;
static const char *optval_retfile;
static struct test_list_entry *optval_attrlist;
static const char *optval_attrfile;
static long int optval_timeout;
static const char *optval_lockfile;

static unsigned int opt_set_flags = TEST_OPT_NONE;

#define LIST_TYPE_ATTR 0

#define list_name(x) (list_type_names[(x)])

static const char *list_type_names[] = {
	"attribute"
};

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

void test_print_value_quoted(const char *value, size_t size)
{
	char c;
	size_t i = 0;

	putchar('"');
	while (i < size) {
		c = *(value + i++);

		switch (c) {
		case '"':
		case '\'':
		case '\\':
			printf("\\%c", (int)c);
			break;

		case '\t':
			printf("\\t");
			break;

		case '\n':
			printf("\\n");
			break;

		default:
			if (isprint(c))
				putchar(c);
			else
				printf("\\%03o", ((unsigned int)c) & 0377);
			break;
		}
	}
	putchar('"');
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
		char *s;

		s = strchr(retsym, '\n');
		if (s != NULL)
			*s = '\0';

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

static void append_list_entry(struct test_list_entry *entry,
			      struct test_list_entry **first_entry,
			      struct test_list_entry **last_entry)
{
	if (entry == NULL)
		return;

	if (*first_entry == NULL)
		*first_entry = entry;

	if (*last_entry != NULL)
		(*last_entry)->next = entry;
	*last_entry = entry;
}

struct parse_entry_cur {
	int list_type;
	const char *buf;
	const char *cur;
};

static void warn_parse_entry_err(struct parse_entry_cur *cursor, int err,
				 const char *field)
{
	if (err == ENOMEM) {
		warn("unable to allocate %s list entry %s",
		     list_name(cursor->list_type), field);
	} else {
		warnx("invalid entry %s %sin %s list: %s", field,
		      (err == ENAMETOOLONG ? "(too long) " : ""),
		      list_name(cursor->list_type), cursor->buf);
	}
}

/* We assume our locale has been set appropriately; e.g., LC_ALL=C
 * is set by test-lib.sh.
 */
static const char *skip_blanks(struct parse_entry_cur *cursor)
{
	const char *s = cursor->cur;

	while (*s != '\0' && isblank(*s))
		++s;

	cursor->cur = s;
	return s;
}

#define PARSE_FLAG_NONE		0x00
#define PARSE_FLAG_ALLOW_EMPTY	0x01
#define PARSE_FLAG_ALLOW_NULL	0x02
#define PARSE_FLAG_ALLOW_SLASH	0x04

/* We implement minimal quoting and escaping, just sufficient for our test
 * purposes.  Specifically, in single- or double-quoted strings, we support
 * the escape sequences \" \' \\ \n \t and \0 with their usual meanings.
 */
static int parse_data(struct parse_entry_cur *cursor, unsigned int flags,
		      size_t max_len, void **data, size_t *size)
{
	char buf[max_len + 1];
	const char *s = cursor->cur;
	char q = '\0';
	char c;
	size_t len = 0;

	if (isquote(*s)) {
		q = *s;
		++s;
	}

	c = *s;
	while (c != '\0' && len < sizeof(buf)) {
		if (q != '\0') {
			if (c == q) {
				q = '\0';
				++s;
				if (*s == '\0' || isblank(*s))
					break;
				else
					return -EINVAL;
			}
			else if (c == '\\') {
				++s;
				c = *s;

				switch (c) {
				case '0':
					if (!(flags & PARSE_FLAG_ALLOW_NULL))
						return -EINVAL;
					c = '\0';
					break;

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

		if (c == '/' && !(flags & PARSE_FLAG_ALLOW_SLASH))
			return -EINVAL;

		buf[len++] = c;
		c = *(++s);
	}

	if (q != '\0')
		return -EINVAL;

	if (len == 0 && !(flags & PARSE_FLAG_ALLOW_EMPTY))
		return -EINVAL;
	else if (len > max_len)
		return -ENAMETOOLONG;

	buf[len] = '\0';

	*data = malloc(len + 1);
	if (*data == NULL)
		return -errno;
	memcpy(*data, buf, len + 1);
	if (size != NULL)
		*size = len;

	cursor->cur = s;
	return 0;
}

static int parse_attr_name(struct parse_entry_cur *cursor, char **name)
{
	int ret;

	ret = parse_data(cursor, PARSE_FLAG_ALLOW_SLASH,
			 MAX_ATTRLIST_ENTRY_LEN, (void**)name, NULL);
	if (ret < 0) {
		warn_parse_entry_err(cursor, -ret, "name");
		return -1;
	}
	return 0;
}

static int parse_attr_value(struct parse_entry_cur *cursor, void **value,
			    size_t *size)
{
	int ret;

	ret = parse_data(cursor,
			 (PARSE_FLAG_ALLOW_EMPTY | PARSE_FLAG_ALLOW_NULL
						 | PARSE_FLAG_ALLOW_SLASH),
			 MAX_ATTRLIST_ENTRY_LEN, value, size);
	if (ret < 0) {
		warn_parse_entry_err(cursor, -ret, "value");
		return -1;
	}
	return 0;
}

static void free_attr(union test_entry *entry)
{
	struct test_attr *attr = &entry->attr;

	if (attr->name != NULL)
		free(attr->name);
	if (attr->value != NULL)
		free(attr->value);
}

static int parse_attr(struct parse_entry_cur *cursor, union test_entry *entry)
{
	struct test_attr *attr = &entry->attr;

	if (parse_attr_name(cursor, &attr->name) < 0)
		return -1;

	skip_blanks(cursor);

	if (parse_attr_value(cursor, &attr->value, &attr->size) < 0)
		goto out_err;

	return 0;

out_err:
	free_attr(entry);
	return -1;
}

static int parse_list_entry(const char *buf, int type,
			    int (*parse_entry)(struct parse_entry_cur *,
					       union test_entry *),
			    void (*free_entry)(union test_entry *entry),
			    struct test_list_entry **first_entry,
			    struct test_list_entry **last_entry)
{
	struct parse_entry_cur cursor = { type, buf, buf };
	struct test_list_entry entry = { 0 };
	struct test_list_entry *new_entry;
	const char *s;

	s = skip_blanks(&cursor);
	if (*s == '\0' || *s == '#')
		return 0;

	if (parse_entry(&cursor, &entry.entry) < 0)
		return -1;

	s = skip_blanks(&cursor);
	if (*s != '\0') {
		warnx("invalid extra fields in %s list: %s",
		      list_name(type), buf);
		goto out_entry;
	}

	new_entry = malloc(sizeof(entry));
	if (new_entry == NULL) {
		warn("unable to allocate %s list entry", list_name(type));
		goto out_entry;
	}
	memcpy(new_entry, &entry, sizeof(entry));

	append_list_entry(new_entry, first_entry, last_entry);

	return 0;

out_entry:
	free_entry(&entry.entry);
	return -1;
}

static int parse_list(const char *list, int type, size_t max_entry_len,
		      int (*parse_entry)(struct parse_entry_cur *,
					 union test_entry *),
		      void (*free_entry)(union test_entry *entry),
		      struct test_list_entry **list_first_entry)
{
	char buf[max_entry_len + 1];
	struct test_list_entry *first_entry = NULL, *last_entry = NULL;
	int ret = 0;

	while (*list != '\0') {
		const char *s;
		size_t len;

		s = strchr(list, '\n');
		if (s == NULL) {
			len = strlen(list);
			s = list + len;
		} else {
			len = s - list;
			++s;
		}

		if (len > max_entry_len) {
			warnx("invalid entry (line too long) in "
			      "%s list: %s", list_name(type), list);
			ret = -1;
			break;
		}

		memcpy(buf, list, len);
		buf[len] = '\0';

		ret = parse_list_entry(buf, type, parse_entry, free_entry,
				       &first_entry, &last_entry);
		if (ret < 0)
			break;

		list = s;
	}

	if (ret == 0)
		*list_first_entry = first_entry;

	return ret;
}

static size_t get_attrlist_size(struct test_list_entry *entry)
{
	size_t size = 0;

	while (entry != NULL) {
		size += strlen(entry->entry.attr.name) +
			entry->entry.attr.size;
		entry = entry->next;
	}

	return size;
}

static int parse_attrlist(const char *list, struct test_list_entry **attrlist)
{
	int ret;

	ret = parse_list(list, LIST_TYPE_ATTR, MAX_ATTRLIST_ENTRY_LEN,
			 parse_attr, free_attr, attrlist);
	if (ret == 0 &&
	    get_attrlist_size(*attrlist) > MAX_ATTRLIST_TOTAL_LEN) {
		warnx("invalid attribute list (too long): %s", list);
		ret = -1;
	}
	return ret;
}

static void
read_list_file(const char *pathname, int type, size_t max_entry_len,
	       int (*parse_entry)(struct parse_entry_cur *,
				  union test_entry *),
	       void (*free_entry)(union test_entry *entry),
	       struct test_list_entry **entrylist, unsigned int *flags)
{
	FILE *file;
	char buf[max_entry_len + 2];	// include newline
	struct test_list_entry *first_entry = NULL, *last_entry = NULL;

	*entrylist = NULL;

	file = fopen(pathname, "r");
	if (file == NULL) {
		if (errno != ENOENT) {
			warn("unable to open %s list file: %s",
			     list_name(type), pathname);
		}
		goto out;
	}

	errno = 0;
	while (fgets(buf, sizeof(buf), file) != NULL) {
		char *s;

		s = strchr(buf, '\n');
		if (s != NULL) {
			*s = '\0';
		} else if (!feof(file)) {
			warnx("invalid entry (line too long) in "
			      "%s list file: %s: %s",
			      list_name(type), pathname, buf);
			goto out_close;
		}

		if (parse_list_entry(buf, type, parse_entry, free_entry,
				     &first_entry, &last_entry) < 0) {
			goto out_close;
		}
	}

	if (errno > 0) {
		warn("unable to read %s list file: %s",
		     list_name(type), pathname);
	} else if (last_entry == NULL) {
		*flags = TEST_FILE_EXIST;
	} else {
		*entrylist = first_entry;
		*flags = TEST_VAL_SET | TEST_FILE_EXIST | TEST_FILE_VALID;
	}

out_close:
	if (fclose(file) != 0) {
		warn("unable to close %s list file: %s",
		     list_name(type), pathname);
	}
out:
	return;
}

static void read_attrfile(struct test_list_entry **attrlist,
			  unsigned int *flags)
{
	*attrlist = NULL;
	read_list_file(optval_attrfile, LIST_TYPE_ATTR, MAX_ATTRLIST_ENTRY_LEN,
		       parse_attr, free_attr, attrlist, flags);
	if (get_attrlist_size(*attrlist) > MAX_ATTRLIST_TOTAL_LEN) {
		warnx("invalid attribute list file (too long): %s",
		      optval_attrfile);
		*attrlist = NULL;
	}
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

static int check_valid_mount_opt(const char *opt)
{
	int i = 0;

	while (all_mount_opts[i] != NULL) {
		if (strncmp(opt, all_mount_opts[i],
			    strlen(all_mount_opts[i])) == 0) {
			return 1;
		}
		++i;
	}

	return 0;
}

static void add_mount_arg(const char *argv0,
			  struct test_mount_args *mount_args,
			  const char *mount_arg)
{
	int argc = mount_args->argc + 1;
	const char **argv;

	if (mount_args->argv == NULL)
		argv = malloc(argc * sizeof(char*));
	else
		argv = realloc(mount_args->argv, argc * sizeof(char*));

	if (argv == NULL) {
		test_exit_error(argv0,
				"unable to allocate mount options array");
	}

	argv[argc - 1] = mount_arg;

	mount_args->argc = argc;
	mount_args->argv = argv;
}

void test_parse_opts(int argc, char *const argv[], unsigned int opt_flags,
		     int min_args, int max_args, char *args[],
		     struct test_mount_args *mount_args,
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

		case TEST_OPT_NUM_ATTRLIST:
			if (parse_attrlist(optarg, &optval_attrlist) < 0)
				test_exit_error(argv[0],
						"invalid attribute list: %s",
						optarg);
			opt_set_flags |= TEST_OPT_ATTRLIST;
			break;

		case TEST_OPT_NUM_ATTRFILE:
			optval_attrfile = optarg;
			opt_set_flags |= TEST_OPT_ATTRFILE;
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
			if (optopt > 0) {
				test_exit_error(argv[0], "invalid option: -%c",
						optopt);
			}
			else if (mount_args != NULL &&
				 check_valid_mount_opt(argv[optind - 1])) {
				add_mount_arg(argv[0], mount_args,
					      argv[optind - 1]);
			} else {
				test_exit_error(argv[0], "invalid option: %s",
						argv[optind - 1]);
			}
			break;

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
			   const char **lower_path, const char **mount_path,
			   struct test_mount_args *mount_args)
{
	char *args[2];

	mount_args->argc = 0;
	mount_args->argv = NULL;

	test_parse_opts(argc, argv, opt_flags, 2, 2, args, mount_args,
			MOUNT_ARGS_USAGE);

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
		struct test_list_entry **e;
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

		case TEST_OPT_ATTRLIST:
			e = va_arg(ap, struct test_list_entry**);
			f = va_arg(ap, unsigned int*);
			*f = TEST_VAL_UNSET | TEST_FILE_NONE;
			if (ret_flag != TEST_OPT_NONE) {
				*e = optval_attrlist;
				*f |= TEST_VAL_SET;
			} else if ((opt_set_flags & TEST_OPT_ATTRFILE)
				   != TEST_OPT_NONE) {
				read_attrfile(e, f);
				ret_flags |= opt_flag;
			}
			break;

		case TEST_OPT_ATTRFILE:
			s = va_arg(ap, const char**);
			if (ret_flag != TEST_OPT_NONE)
				*s = optval_attrfile;
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

static void test_free_opts_va(unsigned int opt_flags, va_list ap)
{
	struct test_list_entry *next_entry, *entry;

	if ((opt_flags & TEST_OPT_ATTRFILE) != TEST_OPT_NONE) {
		entry = va_arg(ap, struct test_list_entry *);

		while (entry != NULL) {
			next_entry = entry->next;

			free_attr(&entry->entry);
			free(entry);

			entry = next_entry;
		}
	}
}

void test_free_opts(unsigned int opt_flags, ...)
{
	va_list ap;

	va_start(ap, opt_flags);
	test_free_opts_va(opt_flags, ap);
	va_end(ap);
}

void test_free_mount_opts(struct test_mount_args *mount_args,
			  unsigned int opt_flags, ...)
{
	va_list ap;

	if (mount_args->argv != NULL)
		free(mount_args->argv);

	va_start(ap, opt_flags);
	test_free_opts_va(opt_flags, ap);
	va_end(ap);
}

struct projfs *test_start_mount(const char *lowerdir, const char *mountdir,
				const struct projfs_handlers *handlers,
				size_t handlers_size, void *user_data,
				struct test_mount_args *mount_args)
{
	struct projfs *fs;

	fs = projfs_new(lowerdir, mountdir, handlers, handlers_size,
			user_data, mount_args->argc, mount_args->argv);

	if (fs == NULL)
		errx(EXIT_FAILURE, "unable to create filesystem");

	if (projfs_start(fs) < 0)
		errx(EXIT_FAILURE, "unable to start filesystem");

	return fs;
}

void *test_stop_mount(struct projfs *fs, struct test_mount_args *mount_args)
{
	void *user_data = projfs_stop(fs);

	if ((opt_set_flags & TEST_OPT_ATTRFILE) != TEST_OPT_NONE) {
		test_free_mount_opts(mount_args, opt_set_flags,
				     optval_attrlist);
	}

	return user_data;
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

