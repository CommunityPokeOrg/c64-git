/* git64.c - the C64 git client PoC.
 *
 * Reads GITREPO.PAK (SEQ) from device 8, runs the shared report flow
 * (inflate, sha1-verify, log/tree/blob display) and emits output to:
 *   - the screen via CHROUT
 *   - a RAM capture buffer at $8000..$9F00, terminated by a magic
 *     marker at $9FFE/$9FFF, which the test harness reads back through
 *     VICE's remote monitor.
 *
 * Files on the disk image:
 *   GIT64        - this program (PRG)
 *   GITREPO.PAK  - object store (SEQ), produced by git64tool mkpak
 */
#include <cbm.h>
#include <string.h>
#include "../pak.h"
#include "../gitobj.h"
#include "../report.h"

#define DEV          8
/* CBM DOS does no case folding; with the ASCII identity charmap this
 * literal emits the same 0x47 0x49 ... bytes c1541 stores in the
 * directory entry. */
#define PAK_FILE     "GITREPO.PAK,S,R"
#define CH_LF        10
#define CH_CR        13
#define CH_LOWERCASE 14

/* RAM capture buffer + done-marker addresses */
#define REPORT_BUF   ((unsigned char *)0x8000)
#define REPORT_END   ((unsigned char *)0x9F00)
#define MAGIC_PTR    ((unsigned char *)0x9FFE)

/* ---------- sequential file reading with rewind-by-reopen -------- */

static unsigned char skipbuf[256];
static unsigned long fpos;

static int pak_reopen(void)
{
    cbm_close(2);
    cbm_open(2, DEV, 2, PAK_FILE);
    fpos = 0;
    if (cbm_k_readst() != 0)
        return -1;
    return 0;
}

static long c64_read_at(void *ctx, unsigned long offset,
                        unsigned char *buf, unsigned int len)
{
    unsigned long toskip;
    (void)ctx;

    if (offset < fpos) {
        if (pak_reopen() != 0)
            return -1;
    }
    toskip = offset - fpos;
    while (toskip > 0) {
        unsigned int want = toskip > sizeof(skipbuf)
                            ? sizeof(skipbuf) : (unsigned int)toskip;
        int got = cbm_read(2, skipbuf, want);
        if (got <= 0)
            return -1;
        fpos += (unsigned long)got;
        toskip -= (unsigned long)got;
    }
    {
        int got = cbm_read(2, buf, len);
        if (got < 0)
            return -1;
        fpos += (unsigned long)got;
        return (long)got;
    }
}

/* ---------- emit: screen + RAM buffer ---------- */

static unsigned char *rptr;

static void c64_put(void *ctx, char c)
{
    (void)ctx;
    if (rptr < REPORT_END)
        *rptr++ = (unsigned char)c;
    if (c == '\n')
        cbm_k_bsout(CH_CR);
    else
        cbm_k_bsout((unsigned char)c);
}

int main(void)
{
    pak_reader r;
    emit_t e;
    int rc;

    cbm_k_bsout(CH_LOWERCASE);          /* lower/upper charset */
    cbm_k_bsout(147);                   /* clr/home */

    rptr = REPORT_BUF;
    *MAGIC_PTR = 0;
    *(MAGIC_PTR + 1) = 0;

    r.ctx = NULL;
    r.read_at = c64_read_at;
    e.ctx = NULL;
    e.put = c64_put;

    fpos = 0;
    if (pak_reopen() != 0) {
        emit_str(&e, "CANNOT OPEN GITREPO.PAK\n");
        emit_str(&e, "*END*\n");
    } else {
        rc = run_report(&r, &e);
        (void)rc;
        cbm_close(2);
    }

    emit_str(&e, "C64GIT DONE\n");

    /* tell the monitor harness we're finished */
    *MAGIC_PTR = 0xC6;
    *(MAGIC_PTR + 1) = 0x64;

    for (;;)
        ;                               /* keep the screen up */
    return 0;
}
