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

#include "../include/config.h"

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
	{ "<max-seconds>", 1 },
	{ "<lock-file>", 1 }
};

/* option values */
static int optval_retval;
static const char *optval_retfile;
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

void test_free_opts(struct test_mount_args *mount_args)
{
	if (mount_args->argv != NULL)
		free(mount_args->argv);
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

