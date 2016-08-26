#!/usr/bin/perl
# Basic tests for "unorm".

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

(my $program_name = $0) =~ s|.*/||;
my $prog = 'unorm';

my $limits = getlimits ();

@ENV{qw(LANGUAGE LANG LC_ALL)} = ('C') x 3;

my $locale = $ENV{LOCALE_FR_UTF8};
! defined $locale || $locale eq 'none'
  and $locale = 'C';

my $try = "Try '$prog --help' for more information.\n";

my @Tests =
    (
     ['1', {IN_PIPE => "1234\n"},            {OUT => "1234\n"}],
    );


# NOTE: The required {ENV=>"LC_ALL=$mb_locale"} will be automatically added
# to all tests (below) - no need to repeatedly specify it.
#
# The following common sequences are used (hex/oct encoded in perl strings):
#  \342 - beginning of a valid-but-incomplete UTF-8 sequence
#  \xEF\xBF\xBD - UTF-8 encoding of U+FFFD 'REPLACEMENT CHARACTER'
my @Locale_Tests =
    (
     # default policy: replace
     ['inv1', {IN_PIPE => "a\342b\n"},  {OUT => "a\xEF\xBF\xBDb\n"}],
     ['inv2', '--replace', {IN_PIPE => "a\342b\n"},
                           {OUT => "a\xEF\xBF\xBDb\n"}],
     ['inv3', '-R', {IN_PIPE => "a\342b\n"}, {OUT => "a\xEF\xBF\xBDb\n"}],
     ['inv4', '--policy=replace', {IN_PIPE => "a\342b\n"},
                                  {OUT => "a\xEF\xBF\xBDb\n"}],

     # policy: recode
     ['inv5', '--recode', {IN_PIPE => "a\342b\n"}, {OUT => "a<0xe2>b\n"}],
     ['inv6', '-C'      , {IN_PIPE => "a\342b\n"}, {OUT => "a<0xe2>b\n"}],
     ['inv7', '--policy=recode', {IN_PIPE => "a\342b\n"},
                                 {OUT => "a<0xe2>b\n"}],

     # policy: discard
     ['inv8', '--discard',         {IN_PIPE => "a\342b\n"}, {OUT => "ab\n"}],
     ['inv9', '-D',                {IN_PIPE => "a\342b\n"}, {OUT => "ab\n"}],
     ['inv10', '--policy=discard', {IN_PIPE => "a\342b\n"}, {OUT => "ab\n"}],

     # policy: abort
     ['inv11', '--abort',        {IN_PIPE => "a\342b\n"}, {EXIT => 1},
      {OUT_SUBST => 's/.*//'},
      {ERR => "$prog: '(stdin)': line 1 char 2 (byte 2): " .
           "found invalid multibyte sequence, octet 0xe2 / 0342\n"}],
     ['inv12', '-A',             {IN_PIPE => "a\342b\n"}, {EXIT => 1},
      {OUT_SUBST => 's/.*//'},
      {ERR => "$prog: '(stdin)': line 1 char 2 (byte 2): " .
           "found invalid multibyte sequence, octet 0xe2 / 0342\n"}],
     ['inv13', '--policy=abort', {IN_PIPE => "a\342b\n"}, {EXIT => 1},
      {OUT_SUBST => 's/.*//'},
      {ERR => "$prog: '(stdin)': line 1 char 2 (byte 2): " .
           "found invalid multibyte sequence, octet 0xe2 / 0342\n"}],

     # custom replacement characeter
     #  U+25B2 'BLACK UP-POINTING TRIANGLE' (decinal: 9650, octal: 22662)
     #  in UTF-8: 0xE2 0x96 0xB2
     ['r1', '-R --replace-char=0x25B2', {IN_PIPE => "a\342b\n"},
      {OUT => "a\xE2\x96\xB2b\n"}],
     ['r2', '-R --replace-char=9650',   {IN_PIPE => "a\342b\n"},
      {OUT => "a\xE2\x96\xB2b\n"}],
     ['r3', '-R --replace-char=022662', {IN_PIPE => "a\342b\n"},
      {OUT => "a\xE2\x96\xB2b\n"}],
     # Invalid replacement character error.
     # NOTE: the locale is fr_FR.UTF-8, the single-quote characters are:
     #   \xe2\x80\x98
     #   \xe2\x80\x99
     ['r4', '-R --replace-char=', {EXIT=>1},
      {ERR => "$prog: invalid replace unicode character value " .
              "\xe2\x80\x98\xe2\x80\x99\n"}],
     ['r5', '-R --replace-char=43a', {EXIT=>1},
      {ERR => "$prog: invalid replace unicode character value " .
              "\xe2\x80\x98" . "43a" . "\xe2\x80\x99\n"}],
     ['r6', '-R --replace-char=-30', {EXIT=>1},
      {ERR => "$prog: invalid replace unicode character value " .
              "\xe2\x80\x98" . "-30" . "\xe2\x80\x99\n"}],
     ['r7', '-R --replace-char=0x123456', {EXIT=>1},
      {ERR => "$prog: invalid replace unicode character value " .
              "\xe2\x80\x98" . "0x123456" . "\xe2\x80\x99\n"}],

     # Unicode normalization.
     # Example taken from:
     #  Unicode Explained, 2006, 1st Edition (ISBN: 0-596-10121-X)
     #  by Jukka K. Korpela
     #  Chapter 5, page 239
     # Input is 'fiance' with ligature 'fi' (U+FB01) and e with acute (U+0039).
     #  'LATIN SMALL LIGATURE FI'   (U+FB01) in UTF-8: 0xEF 0xAC 0x81
     #  'SMALL LETTER E WITH ACUTE' (U+00E9) in UTF-8: 0xC3 0xA9
     #  'COMBINING ACUTE ACCENT'    (U+0301) in UTF-8: 0xCC 0x81

     # NFD (Normalization Form D): decompose the 'e'
     # (canonical decomposition)
     ['nrn1', '-n nfd',  {IN_PIPE => "\xEF\xAC\x81anc\xC3\xA9\n"},
      {OUT => "\xEF\xAC\x81ance\xCC\x81\n"}],

     # NFC (Normalization Form C): decompose the 'e', then recomponse it.
     # (canonical decomposition followed by canonical composition)
     ['nrn2', '-n nfc',  {IN_PIPE => "\xEF\xAC\x81anc\xC3\xA9\n"},
      {OUT => "\xEF\xAC\x81anc\xC3\xA9\n"}],

     # NFKD (Normalization Form KD): decompose 'fi' and 'e'
     # (K=compatability decomposition)
     ['nrn3', '-n nfkd', {IN_PIPE => "\xEF\xAC\x81anc\xC3\xA9\n"},
      {OUT => "fiance\xCC\x81\n"}],

     # NFKC (Normalization Form KC): decompose 'fi' and 'e', then recompose 'e'.
     # (K=compatability decomposition, followed by canonical composition)
     ['nrn4', '-n nfkc', {IN_PIPE => "\xEF\xAC\x81anc\xC3\xA9\n"},
      {OUT => "fianc\xC3\xA9\n"}],

     # check mode
     ['c1', '--check',       {IN_PIPE => "a\342b\n"}, {EXIT => 1},
      {OUT_SUBST => 's/.*//'},
      {ERR => "$prog: '(stdin)': line 1 char 2 (byte 2): " .
           "found invalid multibyte sequence, octet 0xe2 / 0342\n"}],
     ['c2', '--check',       {IN_PIPE => "a\xEF\xAC\x81b\n"}, {OUT=>""}],

     # Ensure mbstate is reset between calls.
     # Otherwise, this is treated as "\342\240" followed by another "\240" -
     # which is a valid UTF-8 sequence.
     ['e1', '-v', {IN_PIPE => "\342\240"}, {OUT => "\xEF\xBF\xBD\xEF\xBF\xBD"},
      #DEV: during development, testing of STREAM vs LINE-BY-LInE,
      #     discard trailing newlines (which are automatically added
      #     with  line-by-line, but not with stream)
      {OUT_SUBST => 's/\n$//s'},
      {ERR => "$prog: '(stdin)': line 1 char 1 (byte 1): " .
              "found invalid multibyte sequence, octet 0xe2 / 0342\n" .
              "$prog: '(stdin)': line 1 char 2 (byte 2): " .
              "found invalid multibyte sequence, octet 0xa0 / 0240\n" }]
  );



# For every locale test,
# insert {ENV => "LC_ALL=$mb_locale"} and add it to @Tests.
if ($locale ne 'C')
  {

    my @new;
    foreach my $t (@Locale_Tests)
      {
        push @new, [ @$t, {ENV => "LC_ALL=$locale"}];
      }
    push @Tests, @new;
  }


my $save_temps = $ENV{SAVE_TEMPS};
my $verbose = $ENV{VERBOSE};

my $fail = run_tests ($program_name, $prog, \@Tests, $save_temps, $verbose);
exit $fail;
