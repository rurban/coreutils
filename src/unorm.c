/* Fix and normalize multibyte characters in files
   Copyright (C) 2016 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include <config.h>
#include <getopt.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <langinfo.h>
#include <wchar.h>

#include "system.h"
#include "multibyte.h"
#include "mbbuffer.h"
#include "assert.h"
#include "xalloc.h"
#include "argmatch.h"
#include "unicodeio.h"
#include "fadvise.h"
#include "error.h"
#include "quote.h"
#include "uninorm.h"

/* The official name of this program (e.g., no 'g' prefix).  */
#define PROGRAM_NAME "unorm"

#define AUTHORS proper_name ("Assaf Gordon")


enum input_error_policy
{
  policy_discard,             /* silently ignore and discard */
  policy_abort,               /* report error and exit */
  policy_replace,             /* replace with a fixed marker character */
  policy_recode               /* encode invalid octets (e.g. <0x%02x>) */
};

static char const *const policy_from_args[] =
{
  "discard", "abort", "replace", "recode", NULL
};
static enum input_error_policy const policy_to_types[] =
{
  policy_discard, policy_abort, policy_replace, policy_recode
};

enum unicode_normalization
{
  norm_nfd = 0,
  norm_nfc,
  norm_nfkd,
  norm_nfkc
};

static char const *const normalization_from_args[] =
{
  "nfd","nfc","nfkd","nfkc",
  /* undocumented hack: accept shorter options,
     allowing: '-nd' and '-nfd' (equivalent to '-n nfd').
     If this is too confusing, remove it. */
  "fd", "fc", "fkd", "fkc",
  "d",  "c",  "kd",  "kc",
  NULL
};
static enum unicode_normalization const normalization_to_types[] =
{
  norm_nfd, norm_nfc, norm_nfkd, norm_nfkc,
  norm_nfd, norm_nfc, norm_nfkd, norm_nfkc,
  norm_nfd, norm_nfc, norm_nfkd, norm_nfkc
};
static uninorm_t const normalization_to_uninorm_type[] =
{
  /* enum to gnulib's uninorm types */
  &uninorm_nfd, &uninorm_nfc, &uninorm_nfkd, &uninorm_nfkc
};


enum
{
  REPLACE_CHAR_OPTION = CHAR_MAX+1,
  RECODE_STRING_OPTION
};

static struct option const longopts[] =
{
  {"abort",           no_argument,       NULL, 'A'},
  {"check",           no_argument,       NULL, 'c'},
  {"discard",         no_argument,       NULL, 'D'},
  {"policy",          required_argument, NULL, 'p'},
  {"normalization",   required_argument, NULL, 'n'},
  {"replace",         no_argument,       NULL, 'R'},
  {"recode",          no_argument,       NULL, 'C'},
  {"recode-format",   required_argument, NULL, RECODE_STRING_OPTION},
  {"replace-char",    required_argument, NULL, REPLACE_CHAR_OPTION},
  {"verbose",         no_argument,       NULL, 'v'},
  {"zero-terminated", no_argument,       NULL, 'z'},
  {GETOPT_HELP_OPTION_DECL},
  {GETOPT_VERSION_OPTION_DECL},
  {NULL, 0, NULL, 0}
};

/* Report input errors (line,column) to STDERR */
static bool verbose;

/* True if --check, no output will be generated */
static bool checkmode;

/* True if --normalization was requested */
static bool normalize;

/* gnulib's uninorm type (defined in uninorm.h) */
static uninorm_t normalization;

/* normalization filter struct */
struct uninorm_filter *unorm_filter;

/* line delimiter.  */
static unsigned char line_delim = '\n';

/* Default invalid-input policy */
static enum input_error_policy input_error_policy = policy_replace;

/* Nonzero if we have ever read standard input.  */
static bool have_read_stdin;

/* Unicode value to use as invalid-octet marker,
   default: 'REPLACEMENT CHARACTER' (U+FFFD) */
static unsigned int invalid_octet_marker = 65533;

/* A printf-like format string taking one unsigned char value,
   to be used with 'recode' policy. */
static const char* invalid_octet_recode_format = "<0x%02x>";

/* The currently open filename, used for error reporting */
static const char* filename;

/* The currently open file descriptor */
static int filedesc = -1;

/* multibyte-parsing struct for the currently open input file */
static struct mbbuf mbb;

/* file position tracking for the currently open input file */
static struct mbbuf_filepos mbfp;

/* The character in MBB is an invalid multibyte sequence.
   Process it according to the requested policy. */
static void
process_invalid_octet (void)
{
  assert (mbb.mb_str);
  assert (mbb.mb_len>=1);
  const unsigned char c = (unsigned char)mbb.mb_str[0];

  if (verbose)
    error (0, 0, _("%s: line %"PRIuMAX" char %"PRIuMAX" (byte %"PRIuMAX"): " \
                   "found invalid multibyte sequence, octet 0x%02x / 0%03o"),
           quotef(filename), mbfp.linenum, mbfp.col_byte, mbfp.col_char,
           c, c);

  switch (input_error_policy)
    {
    case policy_discard:
      break;

    case policy_abort:
      exit (EXIT_FAILURE);

    case policy_replace:
      /* TODO: flush unicode normalization */
      print_unicode_char (stdout, invalid_octet_marker, 1);
      break;

    case policy_recode:
      /* TODO: flush unicode normalization */
      printf (invalid_octet_recode_format, c);
      break;
    }
}

static void
open_input_file (const char* fn)
{
  const bool is_stdin = STREQ (fn,"-");

  /* displayed filename for error reporting */
  filename = is_stdin?"(stdin)":fn;

  if (is_stdin)
    {
      have_read_stdin = true;
      filedesc = STDIN_FILENO;
    }
  else
    {
      filedesc = open (filename, O_RDONLY|O_BINARY);
      if (filedesc == -1)
        error (0, errno, "%s", quotef (filename));
    }

  fdadvise (filedesc, 0, 0, FADVISE_SEQUENTIAL);

  /* init multibyte-parsing data */
  xmbbuf_init_fd (&mbb, filedesc, filename);

  /* init file-position tracking */
  mbbuf_filepos_init (&mbfp);
}

static void
close_input_file (void)
{
  if (!STREQ (filename, "(stdin)")  && close (filedesc) < 0)
      error (0, errno, "%s", quotef (filename));

  mbbuf_free (&mbb);
}

/* Callback function to unicode_filter_create .
   FIXME: this implementation assumes wchar_t is ucs4
   (i.e. uses unicode code-points) */
static int
write_normalized_char (void *stream_data, ucs4_t uc)
{
  char tmp[MB_LEN_MAX];
  const int i = wctomb ((char*)&tmp, uc);
  assert (i>0); /* if failed, uninorm's sent an invalid value */

  /* error checked automatically in close_stdout */
  fwrite (tmp, 1, i, stdout);
  return 0;
}

static void
create_unorm_filter (void)
{
  if (normalization)
    {
      unorm_filter = uninorm_filter_create (normalization,
                                            write_normalized_char,
                                            NULL);
      if (!unorm_filter)
        error (EXIT_FAILURE, errno, "uninorm_filter_create");
    }
}

static void
close_unorm_filter (void)
{
  /* flush any remaining normalization characters, and free filter */
  if (normalization)
    {
      if (uninorm_filter_free (unorm_filter))
        error (EXIT_FAILURE, errno, _("uninorm_filter_free"));
    }
}


/* Process FILE, write to standard output.
   Return true if successful.  */
static bool
unorm_file (char const *file)
{
  open_input_file (file);
  create_unorm_filter ();

  while (mbbuf_fd_getchar (&mbb, filedesc))
    {
      if (mbb.mb_valid)
        {
          /* Valid multibyte sequence */
          if (!checkmode)
            {
              if (normalization)
                uninorm_filter_write (unorm_filter, mbb.wc);
              else
                fwrite (mbb.mb_str, 1, mbb.mb_len, stdout);
            }
        }
      else
        {
          /* Found invalid multibyte sequence */
          process_invalid_octet ();
        }

      /* track file position */
      mbbuf_filepos_advance (&mbb, &mbfp, line_delim);
    }

  if (mbb.err)
    error (1, errno, "%s", quotef (filename));

  close_unorm_filter ();
  close_input_file ();
  return true;
}


void
usage (int status)
{
  if (status != EXIT_SUCCESS)
    emit_try_help ();
  else
    {
      printf (_("\
Usage: %s [OPTION]... [FILE]...\n\
"), program_name);
      fputs (_("\
Fix and adjust multibyte character in files\n\
"), stdout);
      emit_mandatory_arg_note ();

      fputs (_("\
  -A, --abort          same as --policy=abort\n\
"), stdout);

      fputs (_("\
  -C, --recode         same as --policy=recode\n\
"), stdout);

      fputs (_("\
  -c, --check          validate input, no output\n\
"), stdout);

      fputs (_("\
  -D, --discard        same as --policy=discard\n\
"), stdout);

      fputs (_("\
  -n, --normalization=NORM\n\
                       apply unicode normalization NORM:, one of:\n\
                       nfd, nfc, nfkd, nfkc. Normalization requires\n\
                       UTF-8 locales.\n\
"), stdout);
      fputs (_("\
  -p, --policy=POLICY  invalid-input policy: discard, abort\n\
                       replace (default), recode\n\
"), stdout);

      fputs (_("\
  -R, --replace        same as --policy=replace\n\
"), stdout);

      fputs (_("\
      --replace-char=N\n\
                       with 'replace' policy, use unicode character N\n\
                       (default: 0xFFFD 'REPLACEMENT CHARACTER')\n\
"), stdout);
      fputs (_("\
      --recode-format=FMT\n\
                       with 'recode' policy, recode invalid octets\n\
                       using FMT printf-format (default: '<0x%02x>')\n\
"), stdout);
      fputs (_("\
  -v, --verbose        report location of invalid input\n\
  -z, --zero-terminated    line delimiter is NUL, not newline\n\
"), stdout);

      fputs (HELP_OPTION_DESCRIPTION, stdout);
      fputs (VERSION_OPTION_DESCRIPTION, stdout);

      emit_ancillary_info (PROGRAM_NAME);
    }
  exit (status);
}


static void
parse_command_line (int argc, char* argv[])
{
  while (true)
    {
      int c = getopt_long (argc, argv, "ACDRcn:p:vz", longopts, NULL);
      if (c == -1)
        break;

      switch (c)
        {
        case 'A':
          input_error_policy = policy_abort;
          break;

        case 'C':
          input_error_policy = policy_recode;
          break;

        case 'D':
          input_error_policy = policy_discard;
          break;

        case 'R':
          input_error_policy = policy_replace;
          break;

        case 'c':
          input_error_policy = policy_abort;
          verbose = true;
          checkmode = true;
          break;

        case 'n':
          normalize = true;
          {
            enum unicode_normalization un = XARGMATCH ("--normalization",
                                                       optarg,
                                                       normalization_from_args,
                                                       normalization_to_types);

            normalization = normalization_to_uninorm_type[un];
          }
          break;

        case 'p':
          input_error_policy = XARGMATCH ("--policy", optarg,
                                          policy_from_args, policy_to_types);
          break;

        case 'v':
          verbose = true;
          break;

        case 'z':
          line_delim = '\0';
          break;

        case REPLACE_CHAR_OPTION:
          {
            /* TODO: perhaps allow 'U+XXXX' (just replace 'U+' with
               '0x' then can strtol */
            char *e = NULL;
            errno = 0;
            long l = strtol (optarg, &e, 0);
            if (e==NULL || *e!='\0' || l<=0 || l>0x10FFFF )
              error (EXIT_FAILURE, 0,
                     _("invalid replace unicode character value %s"),
                     quote (optarg));
            invalid_octet_marker = (unsigned int)l;
          }
          break;

        case RECODE_STRING_OPTION:
          /* TODO: validate printf format string, ensure it contains
             only one valid format specifier, and valid multibyte sequences */
          invalid_octet_recode_format = optarg;
          break;

        case_GETOPT_HELP_CHAR;
        case_GETOPT_VERSION_CHAR (PROGRAM_NAME, AUTHORS);

        default:
          usage (EXIT_FAILURE);
        }
    }

  if (input_error_policy == policy_abort)
    verbose = true;

  if (normalize && !is_utf8_wchar_ucs4 ())
    {
      const char *l = setlocale (LC_CTYPE,NULL);
      error (EXIT_FAILURE, 0, _("--normalization requires UTF-8 locale " \
                                "with UCS4 wchar_t encoding " \
                                "(detected locale: %s)"), quote(l));
    }
}

int
main (int argc, char **argv)
{
  bool ok = true;

  initialize_main (&argc, &argv);
  set_program_name (argv[0]);
  if (!setlocale (LC_ALL, ""))
    error (EXIT_FAILURE, 0, _("failed to set locale")); /* TODO: use errno? */
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  atexit (close_stdout);

  parse_command_line (argc, argv);

  /* STREAM by blocks */
  if (optind == argc)
    ok = unorm_file ("-");
  else
    for (ok = true; optind < argc; optind++)
      ok &= unorm_file (argv[optind]);

  if (have_read_stdin && close (STDIN_FILENO) < 0)
    {
      error (0, errno, "-");
      ok = false;
    }

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
