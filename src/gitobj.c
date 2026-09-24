/* gitobj.c - parse inflated git loose objects: header, commit, tree. */
#include <string.h>
#include "pak.h"
#include "gitobj.h"

int go_parse_header(const unsigned char *buf, unsigned long len,
                    unsigned char *type_out, unsigned long *body_len)
{
    unsigned long i;
    unsigned long blen = 0;
    unsigned char t;

    /* "<type> <len>\0" */
    i = 0;
    while (i < len && buf[i] != ' ' && i < 8)
        i++;
    if (i >= len || buf[i] != ' ')
        return -1;
    if (i == 4 && memcmp(buf, "blob", 4) == 0)
        t = GO_TYPE_BLOB;
    else if (i == 4 && memcmp(buf, "tree", 4) == 0)
        t = GO_TYPE_TREE;
    else if (i == 6 && memcmp(buf, "commit", 6) == 0)
        t = GO_TYPE_COMMIT;
    else if (i == 3 && memcmp(buf, "tag", 3) == 0)
        t = GO_TYPE_TAG;
    else
        return -2;
    i++;
    while (i < len && buf[i] >= '0' && buf[i] <= '9') {
        blen = blen * 10 + (buf[i] - '0');
        i++;
    }
    if (i >= len || buf[i] != 0)
        return -3;
    i++;
    *type_out = t;
    *body_len = blen;
    return (int)i;
}

static void copy_cstr(char *dst, unsigned int dstcap,
                      const unsigned char *src, unsigned long n)
{
    unsigned int i = 0;
    if (dstcap == 0)
        return;
    while (i < n && i < dstcap - 1) {
        dst[i] = (char)src[i];
        i++;
    }
    dst[i] = '\0';
}

/* find a header line "key value\n" at position pos (must be at line start);
 * copies value (without newline) and returns offset past '\n', or -1 */
static long hdr_line(const unsigned char *body, unsigned long len,
                     unsigned long pos, const char *key,
                     unsigned char *val, unsigned int valcap)
{
    unsigned long klen = strlen(key);
    unsigned long e;
    if (pos + klen > len || memcmp(body + pos, key, klen) != 0 ||
        body[pos + klen] != ' ')
        return -1;
    e = pos + klen + 1;
    {
        unsigned long v = 0;
        while (e + v < len && body[e + v] != '\n')
            v++;
        if (e + v >= len)
            return -1;
        copy_cstr((char *)val, valcap, body + e, v);
        return (long)(e + v + 1);
    }
}

int go_parse_commit(const unsigned char *body, unsigned long len,
                    commit_info *ci)
{
    unsigned long pos = 0;
    unsigned char val[64];
    long n;

    memset(ci, 0, sizeof(*ci));

    n = hdr_line(body, len, pos, "tree", val, sizeof(val));
    if (n < 0)
        return -1;
    hex2bin((const char *)val, ci->tree, 20);
    pos = (unsigned long)n;

    while (ci->nparents < 3) {
        n = hdr_line(body, len, pos, "parent", val, sizeof(val));
        if (n < 0)
            break;
        hex2bin((const char *)val, ci->parents[ci->nparents], 20);
        ci->nparents++;
        pos = (unsigned long)n;
    }

    n = hdr_line(body, len, pos, "author", val, sizeof(val));
    if (n >= 0) {
        copy_cstr(ci->author, sizeof(ci->author), val,
                  strlen((const char *)val));
        pos = (unsigned long)n;
    } else {
        ci->author[0] = '\0';
    }

    /* skip remaining header lines (committer, gpgsig, ...) to blank line */
    while (pos < len && body[pos] != '\n') {
        while (pos < len && body[pos] != '\n')
            pos++;
        if (pos < len)
            pos++;
    }
    if (pos < len)
        pos++;                          /* past the blank line */

    /* subject = first message line */
    {
        unsigned long e = pos;
        while (e < len && body[e] != '\n')
            e++;
        copy_cstr(ci->subject, sizeof(ci->subject), body + pos, e - pos);
    }
    return 0;
}

int go_parse_tree(const unsigned char *body, unsigned long len,
                  tree_entry *entries, unsigned char max)
{
    unsigned long pos = 0;
    unsigned char n = 0;

    while (pos < len && n < max) {
        unsigned long mode = 0;
        unsigned long nameend;
        unsigned int i;

        /* "<mode> <name>\0<20-byte sha>" */
        while (pos < len && body[pos] != ' ') {
            mode = mode * 8 + (body[pos] - '0');   /* mode is octal */
            pos++;
        }
        if (pos >= len)
            return -1;
        pos++;
        nameend = pos;
        while (nameend < len && body[nameend] != 0)
            nameend++;
        if (nameend >= len || nameend + 21 > len)
            return -1;

        entries[n].mode = mode;
        copy_cstr(entries[n].name, sizeof(entries[n].name),
                  body + pos, nameend - pos);
        for (i = 0; i < 20; i++)
            entries[n].sha[i] = body[nameend + 1 + i];
        n++;
        pos = nameend + 21;
    }
    return n;
}
