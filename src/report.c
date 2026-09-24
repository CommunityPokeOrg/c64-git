/* report.c - shared report flow: identical logic and output bytes on
 * host and C64, which is what makes the emulator-vs-host diff test a
 * real end-to-end check rather than a smoke test. */
#include <string.h>
#include "pak.h"
#include "gitobj.h"
#include "sha1.h"
#include "report.h"

#define LOG_DEPTH  4
#define MAX_TREE   16
#define BLOB_PREVIEW_LINES 4
#define LINE_MAX   40                 /* C64 screen is 40 columns */

void emit_char(emit_t *e, char c) { e->put(e->ctx, c); }

void emit_str(emit_t *e, const char *s)
{
    while (*s)
        e->put(e->ctx, *s++);
}

void emit_hex(emit_t *e, const unsigned char *bin, unsigned int n)
{
    char tmp[41];
    bin2hex(bin, tmp, n);
    emit_str(e, tmp);
}

void emit_u32(emit_t *e, unsigned long v)
{
    char tmp[12];
    unsigned char i = 12;
    tmp[--i] = '\0';
    if (v == 0)
        tmp[--i] = '0';
    while (v) {
        tmp[--i] = (char)('0' + (v % 10));
        v /= 10;
    }
    emit_str(e, tmp + i);
}

void emit_oct(emit_t *e, unsigned long v)
{
    char tmp[12];
    unsigned char i = 12;
    tmp[--i] = '\0';
    if (v == 0)
        tmp[--i] = '0';
    while (v) {
        tmp[--i] = (char)('0' + (v % 8));
        v /= 8;
    }
    emit_str(e, tmp + i);
}

static void emit_nl(emit_t *e) { e->put(e->ctx, '\n'); }

/* truncated, line-fitting emit of a text field */
static void emit_field(emit_t *e, const char *s, unsigned char max)
{
    unsigned char i = 0;
    while (s[i] && i < max) {
        e->put(e->ctx, s[i]);
        i++;
    }
}

/* fetch + inflate object by sha; also verifies its SHA-1.
 * returns inflated length, or <0.  *type gets GO_TYPE_*, *boff the
 * body offset inside objbuf. */
static long get_obj(pak_reader *r, const pak_index *idx,
                    const unsigned char sha[20],
                    unsigned char *compbuf, unsigned char *objbuf,
                    unsigned char *type, int *boff,
                    unsigned int *ok, unsigned int *bad)
{
    int eofs = pak_find(idx, sha);
    long n;
    unsigned char got[20];
    unsigned long body_len;

    if (eofs < 0)
        return -1;
    n = pak_inflate_obj(r, idx, eofs, compbuf, PAK_MAX_COMP,
                        objbuf, PAK_MAX_INFL);
    if (n < 0)
        return -2;

    sha1(objbuf, (unsigned long)n, got);
    if (memcmp(got, sha, 20) == 0)
        (*ok)++;
    else
        (*bad)++;

    *boff = go_parse_header(objbuf, (unsigned long)n, type, &body_len);
    return n;
}

int run_report(pak_reader *r, emit_t *e)
{
    /* statics: cc65 allows only a handful of locals per function */
    static pak_index idx;               /* ~2K */
    static unsigned char compbuf[PAK_MAX_COMP];
    static unsigned char objbuf[PAK_MAX_INFL];
    static unsigned int ok, bad;
    static unsigned char type;
    static int boff;
    static unsigned char cur_sha[20];
    static commit_info ci;
    static tree_entry tree[MAX_TREE];
    int rc;
    long n;
    unsigned char depth;

    ok = 0; bad = 0;

    emit_str(e, "=== C64GIT REPORT ===\n");

    rc = pak_load_index(r, &idx);
    if (rc != 0) {
        emit_str(e, "ERR PAK INDEX ");
        emit_u32(e, (unsigned long)(-rc));
        emit_nl(e);
        return -1;
    }

    emit_str(e, "HEAD-REF ");
    emit_field(e, idx.head_ref, 30);
    emit_nl(e);
    emit_str(e, "HEAD-SHA ");
    emit_hex(e, idx.head_sha, 8);       /* short form on screen */
    emit_nl(e);
    emit_str(e, "OBJECTS ");
    emit_u32(e, idx.count);
    emit_nl(e);

    /* ---- git log: walk parents from HEAD ---- */
    memcpy(cur_sha, idx.head_sha, 20);
    for (depth = 0; depth < LOG_DEPTH; depth++) {

        n = get_obj(r, &idx, cur_sha, compbuf, objbuf, &type, &boff,
                    &ok, &bad);
        if (n < 0 || type != GO_TYPE_COMMIT) {
            emit_str(e, "ERR COMMIT\n");
            break;
        }
        if (go_parse_commit(objbuf + boff, (unsigned long)n - boff,
                            &ci) != 0) {
            emit_str(e, "ERR PARSE\n");
            break;
        }

        emit_str(e, "-- COMMIT ");
        emit_hex(e, cur_sha, 6);
        emit_nl(e);
        emit_str(e, "AUTHOR ");
        emit_field(e, ci.author, 32);
        emit_nl(e);
        emit_str(e, "MSG ");
        emit_field(e, ci.subject, 36);
        emit_nl(e);

        if (ci.nparents == 0)
            break;
        memcpy(cur_sha, ci.parents[0], 20);
    }

    /* ---- ls-tree of the first commit's root tree ---- */
    emit_str(e, "-- TREE ");
    emit_hex(e, ci.tree, 6);
    emit_nl(e);
    n = get_obj(r, &idx, ci.tree, compbuf, objbuf, &type, &boff,
                &ok, &bad);
    if (n < 0 || type != GO_TYPE_TREE) {
        emit_str(e, "ERR TREE\n");
    } else {
        int i, nent;
        int blob_idx = -1;
        nent = go_parse_tree(objbuf + boff, (unsigned long)n - boff,
                             tree, MAX_TREE);
        if (nent < 0) {
            emit_str(e, "ERR TREE PARSE\n");
            nent = 0;
        }
        for (i = 0; i < nent; i++) {
            char modestr[8];
            unsigned char j;
            /* octal mode, 6 digits */
            emit_str(e, "  ");
            for (j = 0; j < 6; j++)
                modestr[j] = '0';
            modestr[6] = '\0';
            {
                unsigned long m = tree[i].mode;
                for (j = 6; j-- > 0;) {
                    modestr[j] = (char)('0' + (m % 8));
                    m /= 8;
                }
            }
            emit_str(e, modestr);
            emit_char(e, ' ');
            emit_field(e, tree[i].name, 20);
            emit_char(e, ' ');
            emit_hex(e, tree[i].sha, 4);
            emit_nl(e);
            if (tree[i].mode != 040000 && blob_idx < 0)
                blob_idx = i;
        }

        /* ---- git show <first blob> : preview ---- */
        if (blob_idx >= 0) {
            emit_str(e, "-- BLOB ");
            emit_field(e, tree[blob_idx].name, 20);
            emit_nl(e);
            n = get_obj(r, &idx, tree[blob_idx].sha, compbuf, objbuf,
                        &type, &boff, &ok, &bad);
            if (n < 0 || type != GO_TYPE_BLOB) {
                emit_str(e, "ERR BLOB\n");
            } else {
                unsigned long p = (unsigned long)boff;
                unsigned char line, col;
                unsigned long end = (unsigned long)n;
                for (line = 0; line < BLOB_PREVIEW_LINES && p < end;
                     line++) {
                    emit_str(e, "> ");
                    col = 0;
                    while (p < end && objbuf[p] != '\n' &&
                           col < LINE_MAX - 2) {
                        char ch = (char)objbuf[p];
                        if (ch < 32 || ch > 126)
                            ch = '.';
                        emit_char(e, ch);
                        col++;
                        p++;
                    }
                    if (p < end && objbuf[p] == '\n')
                        p++;
                    emit_nl(e);
                }
            }
        }
    }

    emit_str(e, "SHA1-OK ");
    emit_u32(e, ok);
    emit_str(e, " FAIL ");
    emit_u32(e, bad);
    emit_nl(e);
    emit_str(e, "*END*\n");
    return (bad == 0 && rc == 0) ? 0 : -1;
}
