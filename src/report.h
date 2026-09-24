/* report.h - the shared "git client" demo flow.
 * Runs identically on the host (golden output) and on the C64:
 * read GITREPO.PAK, walk HEAD history, list the root tree, show a
 * blob, verifying SHA-1 of every object it touches. */
#ifndef REPORT_H
#define REPORT_H

#include "pak.h"

/* output sink: one character at a time ('\n' = end of line).
 * host sends them to stdout/a file; the C64 sends them to the screen
 * (as CR) and to a RAM capture buffer at $8000. */
typedef struct emit_s {
    void *ctx;
    void (*put)(void *ctx, char c);
} emit_t;

void emit_char(emit_t *e, char c);
void emit_str(emit_t *e, const char *s);
void emit_hex(emit_t *e, const unsigned char *bin, unsigned int n);
void emit_u32(emit_t *e, unsigned long v);
void emit_oct(emit_t *e, unsigned long v);

int run_report(pak_reader *r, emit_t *e);

#endif
