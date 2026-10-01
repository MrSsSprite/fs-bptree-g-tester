/*----------------------------- Private Includes -----------------------------*/
#include "progress.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
/*--------------------------- Private Includes END ---------------------------*/


/*------------------------------ Private Defines -----------------------------*/
/* Room a line is rendered in.  An item line is an instance path plus the key and
 * the position -- under a hundred bytes, so the margin is wide -- and a line
 * longer than this is cut by `vsnprintf', which is why the module's header says
 * so. */
#define PROG_TEXT_MAX 1024
/*------------------------------ Private Defines END --------------------------*/


/*---------------------------- Private Variables -----------------------------*/
/* the line as it stands on the terminal; when stdout is not a terminal it is
 * the item whose turn is still open, which `progress_break' prints for the log */
static char prog_text[PROG_TEXT_MAX];
/* its length: what a shorter update has to blank out behind itself */
static size_t prog_len;
/* an update no call has finished yet */
static _Bool prog_open;
/* `isatty' of stdout, decided once: -1 until the first call */
static int prog_tty = -1;
/*---------------------------- Private Variables END --------------------------*/


/*--------------------------- Forward Declarations ---------------------------*/
static _Bool _prog_is_tty(void);
static size_t _prog_width(void);
static size_t _prog_render(char *dst, size_t size, const char *fmt, va_list ap,
                           _Bool clip);
/*------------------------- Forward Declarations END -------------------------*/


/*----------------------------- Public Functions -----------------------------*/
void progress_update(const char *fmt, ...)
{
   char line[PROG_TEXT_MAX];
   size_t len, i;
   va_list ap;

   va_start(ap, fmt);
   len = _prog_render(line, sizeof line, fmt, ap, _prog_is_tty());
   va_end(ap);

   if (_prog_is_tty())
    {
      /* start of the line, the update, then blanks over whatever the longer
       * text of the previous turn left there */
      fputc('\r', stdout);
      fwrite(line, 1, len, stdout);
      for (i = len; i < prog_len; i++) fputc(' ', stdout);
      /* no newline follows: without this the line would sit in the buffer of
       * stdio and the terminal would show the previous turn */
      fflush(stdout);
    }

   memcpy(prog_text, line, len + 1);
   prog_len = len;
   prog_open = 1;
}


void progress_done(const char *fmt, ...)
{
   char line[PROG_TEXT_MAX];
   size_t len, i;
   va_list ap;

   va_start(ap, fmt);
   /* a verdict is printed once: it is never clipped, only the live line is */
   len = _prog_render(line, sizeof line, fmt, ap, 0);
   va_end(ap);

   if (_prog_is_tty() && prog_open)
    {
      /* wipe the live line: the verdict takes its place */
      fputc('\r', stdout);
      for (i = 0; i < prog_len; i++) fputc(' ', stdout);
      fputc('\r', stdout);
    }

   /* A redirected run never saw the per-turn lines, and the last one belongs to
    * a loop that is through: it is dropped, the verdict is the report */
   fwrite(line, 1, len, stdout);
   fputc('\n', stdout);
   fflush(stdout);

   prog_open = 0;
   prog_len = 0;
   prog_text[0] = '\0';
}


void progress_break(void)
{
   if (!prog_open) return;

   if (_prog_is_tty())
      /* the line is on the terminal already: it only has to end, so that what
       * follows does not continue it */
      fputc('\n', stdout);
   else
    {
      /* no update was ever shown here: the item is the log's only trace of the
       * turn that is being reported */
      fwrite(prog_text, 1, prog_len, stdout);
      fputc('\n', stdout);
    }
   fflush(stdout);

   prog_open = 0;
   prog_len = 0;
   prog_text[0] = '\0';
}
/*--------------------------- Public Functions END ---------------------------*/


/*---------------------------- Private Functions -----------------------------*/
/**
 * @brief   Whether stdout is a terminal the live line may be drawn on
 *
 * @return  1 for a terminal that can erase a character, 0 otherwise
 */
static _Bool _prog_is_tty(void)
{
   const char *term;

   if (prog_tty < 0)
    {
      term = getenv("TERM");
      /* A redirected stdout is a log: the carriage returns would be noise in
       * it, and `dumb' cannot erase, so both get the verdict lines only. */
      prog_tty = isatty(STDOUT_FILENO) &&
                 (term == NULL || strcmp(term, "dumb") != 0);
    }

   return prog_tty != 0;
}


/**
 * @brief   Width a line may occupy on the terminal
 *
 * One column short of the terminal: a line that reaches the last one wraps, and
 * the next update would then start on the wrong row.
 *
 * @return  the usable width, 79 when the terminal does not report one
 */
static size_t _prog_width(void)
{
   struct winsize ws;

   if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 1)
      return (size_t)ws.ws_col - 1;

   return 79;
}


/**
 * @brief   Render one line, clipped to the width of the terminal when asked
 *
 * A clipped line loses its head, behind a `...': what an update moves -- the
 * instance name, the key, the position -- is at its end, while the head is the
 * same for the whole loop.  A terminal too narrow for that end cuts it too; the
 * line is only ever shorter than what the caller formatted.
 *
 * @param[out] dst   destination, NUL terminated
 * @param[in]  size  capacity of @p dst
 * @param[in]  fmt   format to render
 * @param[in]  ap    its arguments
 * @param[in]  clip  1 to fit the line to the terminal, 0 to keep it whole
 *
 * @return  the length written to @p dst
 */
static size_t _prog_render(char *dst, size_t size, const char *fmt, va_list ap,
                           _Bool clip)
{
   char full[PROG_TEXT_MAX];
   size_t len, width;

   if (vsnprintf(full, sizeof full, fmt, ap) < 0)
    {
      dst[0] = '\0';
      return 0;
    }

   len = strlen(full);
   /* Only a terminal has a width, and only the live line is bound by it: a
    * verdict wraps at worst, where a `...' would drop the phase it names. */
   width = clip ? _prog_width() : len;
   if (len > width)
    {
      size_t tail = width > 3 ? width - 3 : 0;

      if (tail + 4 > sizeof full) tail = sizeof full - 4;
      memmove(full, full + len - tail, tail);
      full[tail] = '\0';
      snprintf(dst, size, "...%s", full);
    }
   else
      snprintf(dst, size, "%s", full);

   return strlen(dst);
}
/*-------------------------- Private Functions END ---------------------------*/
