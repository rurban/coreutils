#!/usr/bin/perl
# Exercise expand with multibyte input

# Copyright (C) 2016 Free Software Foundation, Inc.

# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

use strict;

(my $ME = $0) =~ s|.*/||;

my $limits = getlimits ();
my $UINTMAX_OFLOW = $limits->{UINTMAX_OFLOW};

(my $program_name = $0) =~ s|.*/||;
my $prog = 'expand';

my $locale = $ENV{LOCALE_FR_UTF8};
! defined $locale || $locale eq 'none'
  and CuSkip::skip "$ME: this test requires FR-UTF8 locale\n";

## NOTE about tests locales:
## Tests starting with 'mb' will have {ENV=>"LC_ALL=$locale"}
## added to them automatically - results are multibyte-aware.
## Tests starting with 'sb' have the same input but will be
## run under C locale and will be treated as single-bytes.
## This enables interleaving C/UTF8 tests
## (for easier comparison of expected results).

my @Tests =
  (
   # baseline: single-byte characters
   ['mb1', {IN=>"aaaa\tb"}, {OUT=>"aaaa    b"}],
   ['sb1', {IN=>"aaaa\tb"}, {OUT=>"aaaa    b"}],

   # GREEK SMALL LETTER ALPHA (U+03B1) - 2 octets
   ['mb2', {IN=>"\xCE\xB1aaa\tb"}, {OUT=>"\xCE\xB1aaa    b"}],
   ['sb2', {IN=>"\xCE\xB1aaa\tb"}, {OUT=>"\xCE\xB1aaa   b"}],

   # THORN WITH STROKE  (U+A764) - 3 octets
   ['mb3', {IN=>"\xEA\x9D\xA4aaa\tb"}, {OUT=>"\xEA\x9D\xA4aaa    b"}],
   ['sb3', {IN=>"\xEA\x9D\xA4aaa\tb"}, {OUT=>"\xEA\x9D\xA4aaa  b"}],

   # GOTHIC LETTER LAGUS (U+1033B) - 4 octets
   ['mb4', {IN=>"\xF0\x90\x8C\xBBaaa\tb"}, {OUT=>"\xF0\x90\x8C\xBBaaa    b"}],
   ['sb4', {IN=>"\xF0\x90\x8C\xBBaaa\tb"}, {OUT=>"\xF0\x90\x8C\xBBaaa b"}],

   # GREEK SMALL LETTER ALPHA (U+03B1) - repeated 4 times
   # multibyte: should be treated as 4 characters, then 4 spaces, then 'b'.
   # singlebyte: should be treated as 8 characters, then 8 spaces (the second
   #             tab column), then 'b'.
   ['mb5', {IN=>"\xCE\xB1\xCE\xB1\xCE\xB1\xCE\xB1\tb"},
           {OUT=>"\xCE\xB1\xCE\xB1\xCE\xB1\xCE\xB1    b"}],
   ['sb5', {IN=>"\xCE\xB1\xCE\xB1\xCE\xB1\xCE\xB1\tb"},
           {OUT=>"\xCE\xB1\xCE\xB1\xCE\xB1\xCE\xB1        b"}],


   # Invalid UTF8 multibyte sequence - should be treated as two single-byte
   # characters in both cases.
   ['mb-e1', {IN=>"\xCE\xCEaaa\tb"}, {OUT=>"\xCE\xCEaaa   b"}],
   ['sb-e1', {IN=>"\xCE\xCEaaa\tb"}, {OUT=>"\xCE\xCEaaa   b"}],

  );


# Force multibyte locale in all tests.
#
# NOTE about the ERR_SUBST:
# The error tests above (e1/e2/e3/e4) expect error messages in C locale
# having single-quote character (ASCII 0x27).
# In UTF-8 locale, the error messages will use:
#  'LEFT SINGLE QUOTATION MARK'  (U+2018) (UTF8: 0xE2 0x80 0x98)
#  'RIGHT SINGLE QUOTATION MARK' (U+2019) (UTF8: 0xE2 0x80 0x99)
# So we replace them with ascii single-quote and the results will
# match the expected error string.
if ($locale ne 'C')
  {
    my @new;
    foreach my $t (@Tests)
      {
        my ($tname) = @$t;
        if ($tname =~ /^mb/)
          {
            push @$t, ({ENV => "LC_ALL=$locale"},
                       {ERR_SUBST => "s/\xe2\x80[\x98\x99]/'/g"});
          }
      }
  }


my $save_temps = $ENV{DEBUG};
my $verbose = $ENV{VERBOSE};

my $fail = run_tests ($program_name, $prog, \@Tests, $save_temps, $verbose);
exit $fail;
