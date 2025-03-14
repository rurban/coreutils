/* mkdir -- make directories
   Copyright (C) 1990-2025 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* David MacKenzie <djm@ai.mit.edu>  */

#include <config.h>
#include <stdio.h>
#include <getopt.h>
#include <sys/types.h>
#include <selinux/label.h>

#include "system.h"
#include "mkdir-p.h"
#include "modechange.h"
#include "prog-fprintf.h"
#include "quote.h"
#include "savewd.h"
#include "selinux.h"
#include "smack.h"

/* The official name of this program (e.g., no 'g' prefix).  */
#define PROGRAM_NAME "mkdir"

#define AUTHORS proper_name ("David MacKenzie")

static struct option const longopts[] =
{
  {GETOPT_SELINUX_CONTEXT_OPTION_DECL},
  {"mode", required_argument, nullptr, 'm'},
  {"parents", no_argument, nullptr, 'p'},
  {"verbose", no_argument, nullptr, 'v'},
  {GETOPT_HELP_OPTION_DECL},
  {GETOPT_VERSION_OPTION_DECL},
  {nullptr, 0, nullptr, 0}
};

void
usage (int status)
{
  if (status != EXIT_SUCCESS)
    emit_try_help ();
  else
    {
      printf (_("Usage: %s [OPTION]... DIRECTORY...\n"), program_name);
      fputs (_("\
Create the DIRECTORY(ies), if they do not already exist.\n\
"), stdout);

      emit_mandatory_arg_note ();

      fputs (_("\
  -m, --mode=MODE   set file mode (as in chmod), not a=rwx - umask\n\
  -p, --parents     no error if existing, make parent directories as needed,\n\
                    with their file modes unaffected by any -m option\n\
  -v, --verbose     print a message for each created directory\n\
"), stdout);
      fputs (_("\
  -Z                   set SELinux security context of each created directory\n\
                         to the default type\n\
      --context[=CTX]  like -Z, or if CTX is specified then set the SELinux\n\
                         or SMACK security context to CTX\n\
"), stdout);
      fputs (HELP_OPTION_DESCRIPTION, stdout);
      fputs (VERSION_OPTION_DESCRIPTION, stdout);
      emit_ancillary_info (PROGRAM_NAME);
    }
  exit (status);
}

/* Options passed to subsidiary functions.  */
struct mkdir_options
{
  /* Function to make an ancestor, or nullptr if ancestors should not be
     made.  */
  int (*make_ancestor_function) (char const *, char const *, void *);

  /* Umask value for when making an ancestor.  */
  mode_t umask_ancestor;

  /* Umask value for when making the directory itself.  */
  mode_t umask_self;

  /* Mode for directory itself.  */
  mode_t mode;

  /* File mode bits affected by MODE.  */
  mode_t mode_bits;

  /* Set the SELinux File Context.  */
  struct selabel_handle *set_security_context;

  /* If not null, format to use when reporting newly made directories.  */
  char const *created_directory_format;
};

/* Report that directory DIR was made, if OPTIONS requests this.  */
static void
announce_mkdir (char const *dir, void *options)
{
  struct mkdir_options const *o = options;
  if (o->created_directory_format)
    prog_fprintf (stdout, o->created_directory_format, quoteaf (dir));
}

/* Make ancestor directory DIR, whose last component is COMPONENT,
   with options OPTIONS.  Assume the working directory is COMPONENT's
   parent.  Return 0 if successful and the resulting directory is
   readable, 1 if successful but the resulting directory is not
   readable, -1 (setting errno) otherwise.  */
static int
make_ancestor (char const *dir, char const *component, void *options)
{
  struct mkdir_options const *o = options;

  if (o->set_security_context
      && defaultcon (o->set_security_context, component, S_IFDIR) < 0
      && ! ignorable_ctx_err (errno))
    error (0, errno, _("failed to set default creation context for %s"),
           quoteaf (dir));

  if (o->umask_ancestor != o->umask_self)
    umask (o->umask_ancestor);
  int r = mkdir (component, S_IRWXUGO);
  if (o->umask_ancestor != o->umask_self)
    {
      int mkdir_errno = errno;
      umask (o->umask_self);
      errno = mkdir_errno;
    }
  if (r == 0)
    {
      r = (o->umask_ancestor & S_IRUSR) != 0;
      announce_mkdir (dir, options);
    }
  return r;
}

/* Process a command-line file name.  */
static int
process_dir (char *dir, struct savewd *wd, void *options)
{
  struct mkdir_options const *o = options;

  /* If possible set context before DIR created.  */
  if (o->set_security_context)
    {
      if (! o->make_ancestor_function
          && defaultcon (o->set_security_context, dir, S_IFDIR) < 0
          && ! ignorable_ctx_err (errno))
        error (0, errno, _("failed to set default creation context for %s"),
               quoteaf (dir));
    }

  int ret = (make_dir_parents (dir, wd, o->make_ancestor_function, options,
                               o->mode, announce_mkdir,
                               o->mode_bits, (uid_t) -1, (gid_t) -1, true)
             ? EXIT_SUCCESS
             : EXIT_FAILURE);

  /* FIXME: Due to the current structure of make_dir_parents()
     we don't have the facility to call defaultcon() before the
     final component of DIR is created.  So for now, create the
     final component with the context from previous component
     and here we set the context for the final component. */
  if (ret == EXIT_SUCCESS && o->set_security_context
      && o->make_ancestor_function)
    {
      if (! restorecon (o->set_security_context, last_component (dir), false)
          && ! ignorable_ctx_err (errno))
        error (0, errno, _("failed to restore context for %s"),
               quoteaf (dir));
    }

  return ret;
}

int
main (int argc, char **argv)
{
  char const *specified_mode = nullptr;
  int optc;
  char const *scontext = nullptr;
  struct mkdir_options options;

  options.make_ancestor_function = nullptr;
  options.mode = S_IRWXUGO;
  options.mode_bits = 0;
  options.created_directory_format = nullptr;
  options.set_security_context = nullptr;

  initialize_main (&argc, &argv);
  set_program_name (argv[0]);
  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  atexit (close_stdout);

  while ((optc = getopt_long (argc, argv, "pm:vZ", longopts, nullptr)) != -1)
    {
      switch (optc)
        {
        case 'p':
          options.make_ancestor_function = make_ancestor;
          break;
        case 'm':
          specified_mode = optarg;
          break;
        case 'v': /* --verbose  */
          options.created_directory_format = _("created directory %s");
          break;
        case 'Z':
          if (is_smack_enabled ())
            {
              /* We don't yet support -Z to restore context with SMACK.  */
              scontext = optarg;
            }
          else if (is_selinux_enabled () > 0)
            {
              if (optarg)
                scontext = optarg;
              else
                {
                  options.set_security_context = selabel_open (SELABEL_CTX_FILE,
                                                               nullptr, 0);
                  if (! options.set_security_context)
                    error (0, errno, _("warning: ignoring --context"));
                }
            }
          else if (optarg)
            {
              error (0, 0,
                     _("warning: ignoring --context; "
                       "it requires an SELinux/SMACK-enabled kernel"));
            }
          break;
        case_GETOPT_HELP_CHAR;
        case_GETOPT_VERSION_CHAR (PROGRAM_NAME, AUTHORS);
        default:
          usage (EXIT_FAILURE);
        }
    }

  if (optind == argc)
    {
      error (0, 0, _("missing operand"));
      usage (EXIT_FAILURE);
    }

  /* FIXME: This assumes mkdir() is done in the same process.
     If that's not always the case we would need to call this
     like we do when options.set_security_context.  */
  if (scontext)
    {
      int ret = 0;
      if (is_smack_enabled ())
        ret = smack_set_label_for_self (scontext);
      else
        ret = setfscreatecon (scontext);

      if (ret < 0)
        error (EXIT_FAILURE, errno,
               _("failed to set default file creation context to %s"),
               quote (scontext));
    }


  if (options.make_ancestor_function || specified_mode)
    {
      mode_t umask_value = umask (0);
      options.umask_ancestor = umask_value & ~(S_IWUSR | S_IXUSR);

      if (specified_mode)
        {
          struct mode_change *change = mode_compile (specified_mode);
          if (!change)
            error (EXIT_FAILURE, 0, _("invalid mode %s"),
                   quote (specified_mode));
          options.mode = mode_adjust (S_IRWXUGO, true, umask_value, change,
                                      &options.mode_bits);
          options.umask_self = umask_value & ~options.mode;
          free (change);
        }
      else
        {
          options.mode = S_IRWXUGO;
          options.umask_self = umask_value;
        }

      umask (options.umask_self);
    }

  return savewd_process_files (argc - optind, argv + optind,
                               process_dir, &options);
}
