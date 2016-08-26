/* multibyte temporary test program
   Copyright (C) 1989-2016 Free Software Foundation, Inc.

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

/* Written by Assaf Gordon.  */

/*
This program prints locale,utf,wchar_t information,
based on current locale and libc implementation.

Examples:

In C locale, no multibyte-support is detected:

    $ detected locale: C
    use_multibyte: false
    is_utf8_locale_name: false
    is_utf8_wchar_ucs2:  false
    is_utf8_wchar_ucs4:  false
    mbstr( \x72 ) = wchar_t ( 0x0072 ) - as expected
    mbstr( \xce \xb1 ) = wchar_t ( 0x00ce )  - mismatch, n=1 (expected U+03b1)
    ...

In UTF-8 locale and glibc, wchar_t is implemented as UCS4:

    $ LC_ALL=en_US.UTF-8 ./src/multibyte-test
    detected locale: en_US.UTF-8
    use_multibyte: true
    is_utf8_locale_name: true
    is_utf8_wchar_ucs2:  true
    is_utf8_wchar_ucs4:  true
    mbstr( \x72 ) = wchar_t ( 0x0072 ) - as expected
    mbstr( \xce \xb1 ) = wchar_t ( 0x03b1 ) - as expected
    mbstr( \xea \x9d \xa4 ) = wchar_t ( 0xa764 ) - as expected
    mbstr( \xef \xb9 \xaa ) = wchar_t ( 0xfe6a ) - as expected
    mbstr( \xf0 \x90 \x8c \xbb ) = wchar_t ( 0x1033b ) - as expected
    mbstr( \xf0 \x9f \x82 \xb1 ) = wchar_t ( 0x1f0b1 ) - as expected

On FreeBSD, under non-utf8 multibyte locales, whcar_t is NOT UCS4:

    $ LC_ALL=zh_TW.Big5 ./src/multibyte-test
    detected locale: zh_TW.Big5
    use_multibyte: true
    is_utf8_locale_name: false
    is_utf8_wchar_ucs2:  false
    is_utf8_wchar_ucs4:  false
    mbstr( \x72 ) = wchar_t ( 0x0072 ) - as expected
    mbstr( \xce \xb1 ) = wchar_t ( 0xceb1 )  - mismatch, n=2 (expected U+03b1)
    ...

*/

#include <config.h>

#include <stdio.h>
#include "system.h"
#include "multibyte.h"

static inline const char *
btos (const bool b)
{
  return b?"true":"false";
}

int
main (void)
{
  const char* l = setlocale(LC_ALL,"");

  printf ("detected locale: %s\n", l);

  printf ("use_multibyte: %s\n", btos (use_multibyte ()));
  printf ("is_utf8_locale_name: %s\n", btos (is_utf8_locale_name ()));
  printf ("is_utf8_wchar_ucs2:  %s\n", btos (is_utf8_wchar_ucs2 ()));
  printf ("is_utf8_wchar_ucs4:  %s\n", btos (is_utf8_wchar_ucs4 ()));

  debug_utf8_ucs4 ();

  return 0;
}
