/* system-dependent multibyte-related definitions for coreutils
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
#ifndef __COREUTILS_MULTIBYTE_H__
#define __COREUTILS_MULTIBYTE_H__

/*
These are taken from Pádraig Brady's 'i18n' branch of coreutils
at: https://github.com/pixelb/coreutils/commit/2a2f58c1ee
*/

/* Get mbstate_t, mbrtowc(), wcwidth(). */
#if HAVE_WCHAR_H
# include <wchar.h>
#endif

/* Get iswblank(). */
#if HAVE_WCTYPE_H
# include <wctype.h>
#endif

/* MB_LEN_MAX is incorrectly defined to be 1 in at least one GCC
   installation; work around this configuration error.  */
#if !defined MB_LEN_MAX || MB_LEN_MAX < 2
# define MB_LEN_MAX 16
#endif

/* agn,TODO: is this still valid? if so, shouldn't it be "!defined mbstat_t"? */
#if 0
  /* Some systems, like BeOS, have multibyte encodings but lack mbstate_t.  */
  #if HAVE_MBRTOWC && defined mbstate_t
  # define mbrtowc(pwc, s, n, ps) (mbrtowc) (pwc, s, n, 0)
  #endif
#endif



/* Return TRUE if the current locale is multibyte locale
   (and coreutils was compiled with multibyte support).
   TRUE does not imply UTF-8 locale (or any other specific locale).

   The following locales are known to return FALSE:
     'C', 'POSIX'
     'en_US.iso88591' (on glibc)

   The following locales are known to return TRUE:
     'C.UTF-8' (on musl-libc)
     'en_US.iso88591' (on musl-libc)
   */
bool
use_multibyte (void);


/* Return TRUE if the name of the current locale
   ends with one of the following:
      UTF-8
      UTF8
      utf-8
      utf8
   This only checks the string of the locale name -
   not actual implementation support. */
bool
is_utf8_locale_name (void);


/* Return TRUE if the current locale is able to parse UTF-8 input
   and internal representation of wchar_t supports
   AT LEAST 16-bit unicode code-point values.
   TODO: distinguish between UCS-2 and BMP. */
bool
is_utf8_wchar_ucs2 (void);


/* Return TRUE if the current locale is able to parse UTF-8 input
   and internal representation of wchar_t supports
   32-bit unicode code-point values (UCS-4).

   Only SMP1 (Supp. Multilingual Plane) is checked.
   TODO: Consider adding SIP-2 and others. */
bool
is_utf8_wchar_ucs4 (void);


/* Debug helper: print to stdout the conversion
   results of testing utf8/ucs2/ucs4. */
void
debug_utf8_ucs4 (void);

#endif /* __COREUTILS_MULTIBYTE_H__ */
