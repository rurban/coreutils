/* multibyte I/O functions
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

/*
This module handles reading and parsing multibyte sequences from a file
(either FILE* or file-descriptor) using a fixed-sized buffer.
*/

#ifndef __COREUTILS_MBBUF_H__
#define __COREUTILS_MBBUF_H__

/*
Typical usage:

    struct mbbuf mbb;
    mbbuf_init (&mbb, BUFSIZ);
    FILE* f = fopen(...)

    while (mbbuf_getchar (&mbb, f))
      {
        // 'mbb' contains the information about the parsed sequence:
        //   mbb.mb_str   : the multibyte sequence (valid or invalid)
        //   mbb.mb_len   : number of octets pointed by mb_str.
        //   mbb.mb_valid : True if the sequence is valid (including if NUL).
        //   mbb.wc       : if mb_valid, the wide-char equivalent of 'mb_str'.
      }

    if (mbb.err)
      error (1, errno, ...)

    mbbuf_free (&mbb);
    fclose (f)

Same as above, with file-descriptors:

    int fd = open(....)
    while (mbbuf_fd_getchar(&mbb, fd))
      {
        ...
      }

Same as above, with automatic detection of optimal I/O buffer size:

    const char* filename = "....";
    struct mbbuf mbb;
    FILE* f = fopen(...)
    mbbuf_init_file (&mbb, f, filename);
    while (mbbuf_getchar (&mbb, f))
      {
        ...
      }

*/

/* A struct to manage multibyte buffering of a fixed-sized buffer */
struct mbbuf
{
  /* Internal member variables, should not be touched */
  char  *_buf;
  size_t _alloc;    /* number of bytes allocated for 'buf' */
  size_t _basesize; /* buffer size without MB_MAX_LEN */
  size_t _len;      /* number of bytes read into 'buf' */
  size_t _pos;      /* current reading position in 'buf' (up to 'len') */
  mbstate_t _mbst;  /* current multibyte state */

  bool   eof;       /* if TRUE, EOF encountered */
  bool   err;       /* if TRUE, read-error encountered.
                       'errno' will contain the error */

  /* Variables updated after a call to 'mbbuf_read()/mbbuf_fread()'.
     All variables are READ-ONLY. */
  wchar_t wc;       /* The convert wchar value, if MB_VALID */
  bool    mb_valid; /* Same values as returned from mbtowc(3) */
  char*   mb_str;   /* pointer to the first octet of the converted character.
                       points to _BUF[_POS]. */
  size_t  mb_len;   /* number of octets used for the current multibyte string
                       pointed by MB_STR. */
};

/* Initialize a new MB struct, with the given block-size. */
void
mbbuf_init (struct mbbuf* mbb, size_t blksize);

/* Similar to mbbuf_init, except determines the optimal
   I/O blksize based on the open file-descriptor/stream.
   Dies on failure. */
void
xmbbuf_init_fd (struct mbbuf* mbb, int fd, const char* filename);
void
xmbbuf_init_file (struct mbbuf* mbb, FILE* f, const char* filename);

/* Decode the next multibyte character in the input.
   Return TRUE if a new character is available (in MB)
   the returned character might represent an invalid multibyte sequence).
   Returns FALSE if no more data is available EOF or I/O error. */
bool
mbbuf_getchar (struct mbbuf* mb, FILE *stream);
bool
mbbuf_fd_getchar (struct mbbuf* mb, int fd);

void
mbbuf_free (struct mbbuf *mbb);




/*
The 'mbbuf_filepos' struct keeps track of
file-offset, line number and byte/char position in line,
based on content of 'struct mbb'.

It is kept separately from 'struct mbbuf' to avoid
wasting time counting if no counting/reporting is needed.

Typical usage (buidling upon example above):

    struct mbbuf mbb;
    struct mbbuf_filepos mbfp;

    mbbuf_init (&mbb, BUFSIZ);
    mbbuf_filepos_init (&mbfp);

    FILE* f = fopen(...)
    while (mbbuf_getchar (&mbb, f))
      {
        // do something with character in MBB,
        // and with the information in MBFP.

        // MBFP contains the following values:
        //   mbfp->fileofs:  file-offset in bytes. 0 = first byte of file.
        //   mbfp->linenum:  current line. 1 = first line.
        //   mbfp->col_byte: current BYTE offset in the current line.
        //                   1 = first byte in the line.
        //   mbfp->col_char: current multibyte CHAR in the current line.
        //                   1 = first character in the line.


        // count this character (before reading the next one)
        mbbuf_filepos_advance (&mbb, &mbfp);
      }

*/

/* A struct to track file/line/char position when reading multibyte
   input with a fixed-size buffer. */
struct mbbuf_filepos
{
  size_t fileofs;   /* byte-offset in file. TODO: portable off64_t/loff_t. */
  size_t linenum;   /* line number. 1=first line */
  size_t col_byte;  /* byte offset in current line. 1=first byte */
  size_t col_char;  /* character offset in current line. 1=first character */
};

void
mbbuf_filepos_init (struct mbbuf_filepos* mbfp);

void
mbbuf_filepos_advance (const struct mbbuf *mb, struct mbbuf_filepos *mbfp,
                       const char line_delim);



#endif /* __COREUTILS_MBBUF_H__ */
