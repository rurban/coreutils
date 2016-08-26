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

/* Written by Assaf Gordon.  */

#include <config.h>

#include "system.h"
#include "error.h"
#include "ioblksize.h"
#include "multibyte.h"
#include "mbbuffer.h"
#include "safe-read.h"


#if 0
static inline void
debug_print (const char* prefix, struct mbbuf *mb)
{
  fprintf (stderr, "== %-20s == mb->_pos=%-8zu  mb->_len=%-8zu   == ",
           prefix, mb->_pos, mb->_len);

  for (size_t i=0; i < mb->_len; ++i)
    {
      const unsigned char c = (unsigned char) *(mb->_buf + mb->_pos + i);
      if (i>0)
        fputc (' ', stderr);

      if (isascii (c) && isprint (c) && (!isspace(c)))
        fputc (c, stderr);
      else
        fprintf (stderr, "0x%02x", (unsigned char)c);
    }
  fputc ('\n', stderr);
}
#endif


/* Return the file's io_blksize, or BUFSIZ.
   Die on failure. */
static size_t
xget_fd_ioblk (int fd, const char* file)
{
  struct stat stat_buf;
  u_long u;

  if (fstat (fd, &stat_buf) < 0)
    {
      error (0, errno, "fstat failed on %s", quotef (file));
      return false;
    }

  u = io_blksize (stat_buf);

  /* TODO: consider an upper limit? */
  if (u>0)
    return u;

  return BUFSIZ;
}

/* Return the file's io_blksize, or BUFSIZ - for an STDIO stream. */
static size_t
xget_file_ioblk (FILE* f, const char* file)
{
  return xget_fd_ioblk (fileno (f), file);
}


void
mbbuf_init (struct mbbuf* mbb, size_t blksize)
{
  memset (mbb, 0, sizeof (struct mbbuf));
  mbb->_basesize = blksize;
  mbb->_alloc = blksize + MB_CUR_MAX;
  mbb->_buf = xmalloc (mbb->_alloc);
}

void
xmbbuf_init_fd (struct mbbuf* mbb, int fd, const char* filename)
{
  mbbuf_init (mbb, xget_fd_ioblk (fd, filename));
}

void
xmbbuf_init_file (struct mbbuf* mbb, FILE* f, const char* filename)
{
  mbbuf_init (mbb, xget_file_ioblk (f, filename));
}


void
mbbuf_free (struct mbbuf *mbb)
{
  free (mbb->_buf);
  /* TODO: skip the following for optimization, keep for lint/debugging */
  mbb->_buf = NULL;
  mbb->_alloc = (size_t)-1;
  mbb->_len = (size_t)-1;
  mbb->_pos = (size_t)-1;
  mbb->_basesize = (size_t)-1;
  memset (&mbb->_mbst, 0, sizeof (mbstate_t));
}


/* Internal function, common for both FILE* and file-descriptor
   'getchar' functions.

   MB->_BUF, MB->_POS, MB->_LEN should be set correctly after
   reading more data from the input file.

   sets MB->MB_STR, MB->MB_LEN, MB->STATE according to the parsed
   multibyte sequence.
   increments MB->_POS and decrements MB->_LEN accordingly (by MB->MB_LEN).
 */
static void
mbbuf_parse_next_char (struct mbbuf *mb)
{
  /* Parse the next character */
  mb->mb_str = mb->_buf + mb->_pos;

  const size_t n = mbrtowc (&mb->wc, mb->mb_str, mb->_len, &mb->_mbst);

  #if 0
  fprintf (stderr, "parse_next_char: mb_str[0] = '%c' 0x%02x   mb_len=%zu\n",
           (unsigned char)mb->mb_str[0],
           (unsigned char)mb->mb_str[0],
           n);
  #endif

  switch (n)
    {
    case (size_t)-1:
      /* Invalid Multibyte Seqeunce */
      mb->mb_len = 1;
      mb->mb_valid = false;
      memset (&mb->_mbst, 0, sizeof (mb->_mbst));
      break;

    case (size_t)-2:
      /* Incomplete multibyte sequence, at the end of the file. This
         must be the end of the file, as the code ensures that any
         additional octets are read if available.
         Treat as invalid multibyte sequence - 1 octet at a time. */
      mb->mb_len = 1;
      mb->mb_valid = false;
      memset (&mb->_mbst, 0, sizeof (mb->_mbst));
      break;

    case 0:                /* The NUL character. Assume 1 octet. */
      mb->mb_valid = true;
      mb->mb_len = 1 ;
      break;

    default:               /* Valid (multi)byte sequence of n octets. */
      mb->mb_valid = true;
      mb->mb_len = n ;
      break;
    }

  /* On Error, reset mbstate */
  if (!mb->mb_valid)
    memset (&mb->_mbst, 0, sizeof (mbstate_t));

  mb->_pos += mb->mb_len;
  mb->_len -= mb->mb_len;
}

bool
mbbuf_getchar (struct mbbuf* mb, FILE *stream)
{
  /* Refill the buffer if needed, moving remaining octets
     to the beginning of the buffer. */
  if (mb->_len < MB_LEN_MAX && !mb->eof && !mb->err)
    {
      memmove (mb->_buf, mb->_buf + mb->_pos, mb->_len);
      const size_t s = fread (mb->_buf + mb->_len,
                              sizeof (char), mb->_basesize, stream);

      if (s < (sizeof (char)*mb->_basesize))
        {
          if (ferror (stream))
            {
              mb->err = true;
              return false;
            }

          mb->eof = true;
        }

      mb->_len += s;
      mb->_pos = 0;
    }

  /* No more octets in the buffer */
  if (mb->_len == 0)
    return false;

  mbbuf_parse_next_char (mb);
  return true;
}

bool
mbbuf_fd_getchar (struct mbbuf* mb, int fd)
{
  if (mb->err)
    return false;

  /* Refill the buffer if needed,  */
  if (mb->_len < MB_CUR_MAX && !mb->eof)
    {
      /* move remaining octets to the beginning of the buffer.*/
      if (mb->_pos > 0)
        {
          memmove (mb->_buf, mb->_buf + mb->_pos, mb->_len);
          mb->_pos = 0;
        }

      /* read data from the file.

         Need to have at least MB_LEN_MAX octets (unless it's EOF),
         otherwise mbrtowc might return -2 (incomplete mb sequence)
         for a valid sequence. Thus, loop until buffer is full.

         read(2) can return less octets than requested,
         (in extreme cases it can return just 1 octet at a time). */
      char* pbuf = mb->_buf + mb->_len;
      while (mb->_len < MB_CUR_MAX && !mb->eof)
        {
          /* read _BASESIZE octets if possible,
             which is hopefully the optimal block-size for FD */
          const size_t cnt = MIN (mb->_basesize, mb->_alloc - mb->_len);

          /* safe_read used for EINTR restarts */
          const size_t s = safe_read (fd, pbuf, cnt);

          /* I/O error - flag it and bail out */
          if (s == SAFE_READ_ERROR)
            {
              mb->err = true;
              return false;
            }

          /* EOF detected - flag it and continue
             (there could be more octets left in the buffer from
             previous reads */
          if (s==0)
            mb->eof = true;

          mb->_len += s;
          pbuf += s;
        }
    }

  /* No more octets in the buffer */
  if (mb->_len == 0)
    return false;

  mbbuf_parse_next_char (mb);
  return true;
}



void
mbbuf_filepos_init (struct mbbuf_filepos* mbfp)
{
  memset (mbfp, 0, sizeof (struct mbbuf_filepos));
  mbfp->linenum = 1; /* first line, before the first newline */
  mbfp->col_byte = 1;
  mbfp->col_char = 1;
}

void
mbbuf_filepos_advance (const struct mbbuf *mb, struct mbbuf_filepos *mbfp,
                       const char line_delim)
{
  const size_t l = mb->mb_len;

  mbfp->fileofs += l;
  mbfp->col_byte += l;
  mbfp->col_char++; /* TODO: do we count logical characters in
                       case of invalid MB sequence? */

  /* Current implementation: only single-byte line-delimiters are counted. */
  if (mb->mb_valid && (l==1) && (mb->mb_str[0] == line_delim))
    {
      ++mbfp->linenum;
      mbfp->col_byte = 1;
      mbfp->col_char = 1;
    }
}
