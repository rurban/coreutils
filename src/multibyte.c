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
   along with this program.  If not, see <http://www.gnu.org/licenses/>. */

/* Written by Assaf Gordon.  */

#include <config.h>

#include "system.h"
#include "multibyte.h"

struct utf8_ucs4
{
  const char* utf8;    /* input as UTF-8 multibyte string */
  const uint32_t ucs4; /* expected output as UCS-4 codepoint */
};

static const struct utf8_ucs4 utf8_ucs4_tests[] =
  {
    /* ASCII */
    { "r",            0x0072 }, /* Latin Small Letter R (U+0072) */

    /* BMP */
    { "\xCE\xB1",     0x03B1 }, /* GREEK SMALL LETTER ALPHA (U+03B1) */
    { "\xEA\x9D\xA4", 0xA764 }, /* THORN WITH STROKE  (U+A764) */
    { "\xEF\xB9\xAA", 0xFE6A }, /* SMALL PERCENT SIGN (U+FE6A) */

    /* SMP */
    { "\xF0\x90\x8C\xBB", 0x1033B},  /* GOTHIC LETTER LAGUS (U+1033B) */
    { "\xF0\x9F\x82\xB1", 0x1F0B1},  /* PLAYING CARD ACE OF HEARTS (U+1F0B1) */

    { NULL, 0 }
  };


extern bool
use_multibyte (void)
{
#if HAVE_MBRTOWC
  return (MB_CUR_MAX > 1);
#else
  return false;
#endif
}


/* Surely there's a better way.... */
extern bool
is_utf8_locale_name (void)
{
  /* TODO: add #ifdef HAVE_MBRTOWC and also return FALSE if not available? */
  const char *l = setlocale (LC_CTYPE, NULL);
  if (!l)
    return false;

  l = strrchr (l, '.');
  if (!l)
    return false;
  ++l;

  return (STREQ (l,"UTF-8") || STREQ(l,"UTF8")
          || STREQ (l,"utf-8") || STREQ (l,"utf8"));
}

/* Return TRUE if the given multibyte string results in the EXPECTED
   value for wchar_t. */
static bool
_check_mb_wc (const char* mbstr,  const uint32_t expected,
              bool verbose)
{
  mbstate_t mbs;
  wchar_t wc;
  size_t n;
  const size_t l = strlen (mbstr);

  memset (&mbs, 0, sizeof (mbs));
  n = mbrtowc (&wc, mbstr, l, &mbs);

  if (verbose)
    {
      fputs ("mbstr( ", stdout);
      for (size_t i=0;i<l;++i)
        printf ("\\x%02x ", (unsigned char)mbstr[i]);
      fputs (") ",stdout);

      if ((n == (size_t)-1) || (n==(size_t)-2))
        {
          /* conversion failed */
          printf ("failed conversion, n=%zu (expected U+%04x)\n", n, expected);
        }
      else
        {
          printf ("= wchar_t ( 0x%04x ) ", (uint32_t)wc);
          if ( (l==n) && ((uint32_t)wc == expected) )
            puts ("- as expected");
          else
            printf (" - mismatch, n=%zu (expected U+%04x)\n", n, expected);
        }
    }

  return (l==n) && ( ((uint32_t)wc) == expected);
}

static bool
_check_utf8_ucs (bool verbose, bool check_ucs4)
{
  bool ok = true;

  const size_t min_size = check_ucs4 ? 4 : 2 ;
  if (sizeof(wchar_t)<min_size)
    return false;

  const struct utf8_ucs4 *p = utf8_ucs4_tests;
  while (p && p->utf8)
    {
      if (check_ucs4 || (p->ucs4 <= 0xFFFF))
          ok &= _check_mb_wc (p->utf8, p->ucs4, verbose);

      ++p;
    }

  return ok;
}

extern bool
is_utf8_wchar_ucs2 (void)
{
  return _check_utf8_ucs (false, false);
}

extern bool
is_utf8_wchar_ucs4 (void)
{
  return _check_utf8_ucs (false, true);
}

extern void
debug_utf8_ucs4 (void)
{
  _check_utf8_ucs (true, true);
}
