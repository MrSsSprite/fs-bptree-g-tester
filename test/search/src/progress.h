#ifndef PROGRESS_H
#define PROGRESS_H

/*----------------------------- Public Functions -----------------------------*/
/**
 * @brief   Show the item a loop is working on, on the line it keeps rewriting
 *
 * One call per turn of a loop, instead of one printed line per turn: a case that
 * walks thousands of positions then occupies one line of the terminal rather
 * than thousands.  The line is rewritten in place only when stdout is a
 * terminal -- a redirected stdout is read as a log, where the carriage returns
 * would be noise -- so a run that is captured prints the verdicts of
 * `progress_done' and nothing per turn.
 *
 * @param[in] fmt  `printf' format of the line, without its newline
 *
 * @note  Any output that is not the next `progress_update' has to be preceded by
 *        `progress_break', or it is written where the line is: the terminal
 *        cursor is left at the end of it.  The tools' runner does that for the
 *        report it replays.
 * @note  Whether stdout is a terminal is decided once, by the first call of the
 *        module; a caller that reopens stdout later keeps that answer.  A line
 *        wider than the terminal is clipped from the left, down to the `...'
 *        marker itself at the degenerate widths, and one longer than the room
 *        the module renders in is cut short.
 */
void progress_update(const char *fmt, ...);
/**
 * @brief   Replace the live line with the verdict of the loop that just ended
 *
 * @param[in] fmt  `printf' format of the verdict, without its newline
 *
 * @note  The last update of a loop that is through is dropped: the verdict says
 *        what the loop did, the per-item line would only repeat its last turn.
 *        A verdict is never clipped -- it is printed once, and a narrow terminal
 *        wrapping it is better than a `...' that drops the phase it names.
 */
void progress_done(const char *fmt, ...);
/**
 * @brief   Finish the live line, so that other output starts on a line of its
 *          own
 *
 * On a terminal the line is already visible: it only gets its newline.  In a
 * redirected run no per-turn line was ever printed, so the item the update holds
 * -- the one whose turn is being reported -- is printed before the output that
 * follows.  A no-op when no update is open.
 *
 * @note  Call it when an operation the loop was reporting turns out to have
 *        failed, and before the assertion that reports it.
 */
void progress_break(void);
/*--------------------------- Public Functions END ---------------------------*/

#endif
