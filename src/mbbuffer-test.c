/* mbbuffer temporary test program
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
This program exercises the mbbuffer module code.
It reads multibyte input from STDIN and prints it to STDOUT
either as-is or with detailed parsing report.

Two input modes are supports:
  -f/--file-descriptor:  use MBBUFFER with read(2)
  -s/--stream:           use MBBUFFER with fread(3) (default).
Given the sample input Both input modes should produce the exact same output.
An additional option (-b/--bufsize=N) allows forcing a specific buffer-size
(for the fixed-size buffering code. default: BUFSIZ).

Two output modes are supported:
  -r/--report:           print detailed report about each parsed character.
  -p/--pass-through:     print input as-is (default).


The "-p/--pass-through" mode is useful for autmatic testing of the buffering
code. The following should all output the exact same content as the input:

    mbbuffer-test -f -p         < input > output.filedsec
    mbbuffer-test -f -p -b 30   < input > output.filedesc-30
    mbbuffer-test -f -p -b 4096 < input > output.filedesc-4096
    mbbuffer-test -s -p         < input > output.stream
    mbbuffer-test -s -p -b 30   < input > output.stream-30
    mbbuffer-test -s -p -b 4096 < input > output.stream-4096

The "-r/--report" mode is useful for manual debugging / troubleshooting
invalid multibyte files.
The input below is
  'ANGSTROM SIGN' (U+212B)
  <space>
  'MATHEMATICAL SANS-SERIF BOLD SMALL BETA' (U+1D771)
  <newline>
  'DOUBLE-STRUCK CAPITAL C' (U+2102)
  <space>
  <invalid multibyte sequence>
  <space>
  e
  <space>
  'TURNED CAPITAL F' (U+2132)

  $ printf '\342\204\253 \360\235\235\261\n\342\204\202 \342\342e \342\204\262'\
        | ./src/mbbuffer-test -r
  ofs  line colB colC V wc(dec) wc(hex) Ch n octets
  0    1    1    1    y    8491 0x0212b Å  3 0xe2 0x84 0xab
  3    1    4    2    y      32 0x00020 =  1 0x20
  4    1    5    3    y  120689 0x1d771 𝝱  4 0xf0 0x9d 0x9d 0xb1
  8    1    9    4    y      10 0x0000a =  1 0x0a
  9    2    1    1    y    8450 0x02102 ℂ  3 0xe2 0x84 0x82
  12   2    4    2    y      32 0x00020 =  1 0x20
  13   2    5    3    n       *       * *  1 0xe2
  14   2    6    4    n       *       * *  1 0xe2
  15   2    7    5    y     101 0x00065 e  1 e
  16   2    8    6    y      32 0x00020 =  1 0x20
  17   2    9    7    y    8498 0x02132 Ⅎ  3 0xe2 0x84 0xb2

The output columns are:
  ofs:     byte offset in the input file
  line:    logical line number (with '\n' as delimiter)
  colB:    byte-offset in the current line (1 = first byte)
  colC:    multibyte char-offset in the current line (1= first char)
  V:       'y' if it's a valid single/multibyte character
  wc(dec): decimal value of 'wchar_t', or '*' if invalid char.
  wc(hex): hex value of 'wchar_t', or '*' if invalid char.
  Ch:      The multibyte character (if the terminal can render the glyph).
           '*' if invalid multibyte. '=' if valid but non-graphic.
  n:       Number of octets of the multibyte sequence.
  octets:  Hex values of the octets composing this character.
 */

#include <config.h>

#include <stdio.h>
#include <getopt.h>
#include <sys/types.h>
#include "system.h"
#include "safe-read.h"
#include "multibyte.h"
#include "mbbuffer.h"
#include "error.h"

static size_t bufsize = BUFSIZ;

/* if TRUE,  fread(3) using stdio stream.
   if FALSE, read(2) using file-descriptor. */
static bool stream = true;

/* if TRUE, print detailed report about the input
   (such as mb-octets, wchar_t, file position).
   if FALSE, print input as-is. */
static bool report = false;

static void
parse_command_line (int argc, char* argv[])
{
  static struct option const longopts[] =
    {
      {"stream",          no_argument,       NULL, 's'},
      {"bufsize",         required_argument, NULL, 'b'},
      {"report",          no_argument,       NULL, 'r'},
      {"pass-through",    no_argument,       NULL, 'p'},
      {"file-descriptor", no_argument,       NULL, 'f'},
      {NULL, 0, NULL, 0}
    };

 while (true)
    {
      int c = getopt_long (argc, argv, "rpsfb:", longopts, NULL);
      if (c == -1)
        break;

      switch (c)
        {
        case 'b': /* buffer size */
          bufsize = atoi(optarg);
          if (bufsize <= 0)
            error (1, 0, "invalid --buffer-size '%s'", optarg);
          break;

        case 'r': /* print detailed report about each parsed char */
          report = true;
          break;

        case 'p': /* pass input as-is */
          report = false;
          break;

        case 's': /* use stdio streams */
          stream = true;
          break;

        case 'f': /* use file-desciptor */
          stream = false;
          break;

        default:
          /* TODO: usage */
          exit (EXIT_FAILURE);
        }
    }
}

/* Print the process multibyte character in MBB:
    Whether it's valid or not,
    The wchar_t value if it's valid (commonly, the unicode codepoint),
    and the multibyte octets. */
static void
print_mbchar (const struct mbbuf* mbb, const struct mbbuf_filepos *mbfp)
{
  static bool first = true;

  /*
    'mbb' contains the information about the parsed sequence:
      mbb->mb_str   : the multibyte sequence (valid or invalid)
      mbb->mb_len   : number of octets pointed by mb_str.
      mbb->mb_valid : True if the sequence is valid (including if NUL).
      mbb->wc       : if mb_valid, the wide-char equivalent of 'mb_str'.
  */

  /* Print header */
  if (first)
    {
      first = false;
      puts ("ofs  line colB colC V wc(dec) wc(hex) Ch n octets");
    }

  /* Print file/line position */
  printf ("%-4zu %-4zu %-4zu %-4zu ",
          mbfp->fileofs, mbfp->linenum, mbfp->col_byte, mbfp->col_char);


  /* Print wchar information */
  if (mbb->mb_valid)
    printf ("y %7zu 0x%05zx ", (size_t)mbb->wc, (size_t)mbb->wc);
  else
    fputs ("n       *       * ",stdout);

  /* Print the multibyte-sequence as-is (the terminal should render
     the proper glyph */
  if (mbb->mb_valid)
    {
      if (iswgraph (mbb->wc))
        {
          fwrite (mbb->mb_str, 1, mbb->mb_len, stdout);
          if (wcwidth (mbb->wc)==1)
            putc (' ', stdout);
          putc (' ', stdout);
        }
      else
        {
          /* ugly hack: Valid multibyte character, but not a graphic one -
             print a place-holder. */
          fputs ("=  ", stdout);
        }
    }
  else
    {
      /* ugly hack: invalid multibyte character, but not a graphic one -
         print a place-holder. */
      fputs ("*  ", stdout);
    }

  /* print the number of octets */
  printf ("%zu ", mbb->mb_len);

  /* Print multibyte octets */
  for (size_t i=0; i<mbb->mb_len; ++i)
    {
      const unsigned char c = (unsigned char)mbb->mb_str[i];
      if (i>0)
        putchar (' ');

      if (isascii (c) && isprint (c) && (!isspace(c)))
        putchar (c);
      else
        printf ("0x%02x", (unsigned char)c);
    }

  putchar ('\n');
}


int
main (int argc, char **argv)
{
  struct mbbuf mbb;
  struct mbbuf_filepos mbfp;

  setlocale (LC_ALL, "");

  /* TODO:
  initialize_main (&argc, &argv);
  set_program_name (argv[0]);
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);
  atexit (close_stdout);
  */

  parse_command_line (argc, argv);

  mbbuf_init (&mbb, bufsize);
  mbbuf_filepos_init (&mbfp);

  while (1)
    {
      /* Input method: file-descriptor or stream */
      bool have_char;
      if (stream)
        have_char = mbbuf_getchar (&mbb, stdin);
      else
        have_char = mbbuf_fd_getchar (&mbb, STDIN_FILENO);

      if (!have_char)
        break;

      /* output method: detailed report or as-is */
      if (report)
        {
          print_mbchar (&mbb, &mbfp);
          mbbuf_filepos_advance (&mbb, &mbfp, '\n');
        }
      else
        fwrite (mbb.mb_str, 1, mbb.mb_len, stdout);
    }


  if (mbb.err)
    error (1, errno, "(stdin) input error");

  mbbuf_free (&mbb);

  /* TODO: close+check stdin */

  return 0;
}
