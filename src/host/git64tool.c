/* git64tool - host-side companion tool.
 *
 *   git64tool mkpak <gitdir> <out.pak>
 *       Walk <gitdir>/objects/** loose objects, resolve HEAD, and write
 *       a GITREPO.PAK (the real zlib-compressed git object streams,
 *       plus a sorted index) that the C64 can read sequentially.
 *
 *   git64tool report <pak>
 *       Run the exact same report flow the C64 binary runs, against a
 *       pak file, printing to stdout. Its output is the golden file the
 *       emulator run is diffed against.
 *
 *   git64tool cat <pak> <hexsha>
 *       Debug helper: inflate one object to stdout.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <zlib.h>
#include "../pak.h"
#include "../gitobj.h"
#include "../sha1.h"
#include "../report.h"

/* ---------- pak_reader over a seekable file ---------- */

typedef struct { FILE *f; } host_ctx;

static long host_read_at(void *vctx, unsigned long offset,
                         unsigned char *buf, unsigned int len)
{
    host_ctx *c = (host_ctx *)vctx;
    if (fseek(c->f, (long)offset, SEEK_SET) != 0)
        return -1;
    return (long)fread(buf, 1, len, c->f);
}

static void host_put(void *ctx, char c)
{
    (void)ctx;
    if (c == '\n')
        putchar('\n');
    else
        putchar(c);
}

static int is_hex(const char *s, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return 0;
    }
    return 1;
}

/* ---------- mkpak ---------- */

typedef struct {
    unsigned char sha[20];
    unsigned char *comp;
    unsigned long clen;
    unsigned long ilen;
    unsigned char type;
} obj_rec;

static int cmp_rec(const void *a, const void *b)
{
    return memcmp(((const obj_rec *)a)->sha, ((const obj_rec *)b)->sha, 20);
}

static int obj_type(const unsigned char *infl, unsigned long len)
{
    unsigned char t;
    unsigned long blen;
    if (go_parse_header(infl, len, &t, &blen) < 0)
        return 0;
    return t;
}

static int read_file(const char *path, unsigned char **buf,
                     unsigned long *len)
{
    FILE *f = fopen(path, "rb");
    long n;
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    *buf = malloc((size_t)n);
    if (fread(*buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        return -1;
    }
    fclose(f);
    *len = (unsigned long)n;
    return 0;
}

/* resolve HEAD to a commit sha + ref name */
static int read_head(const char *gitdir, unsigned char sha[20],
                     char *refname, size_t refcap)
{
    char path[4096], line[4096];
    FILE *f;

    snprintf(path, sizeof(path), "%s/HEAD", gitdir);
    f = fopen(path, "r");
    if (!f)
        return -1;
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return -1;
    }
    fclose(f);

    if (strncmp(line, "ref:", 4) == 0) {
        char *p = line + 4;
        while (*p == ' ' || *p == '\t')
            p++;
        line[strcspn(line, "\r\n")] = '\0';
        p[strcspn(p, "\r\n")] = '\0';
        snprintf(refname, refcap, "%s", p);
        snprintf(path, sizeof(path), "%s/%s", gitdir, p);
        f = fopen(path, "r");
        if (!f) {                       /* maybe packed */
            snprintf(path, sizeof(path), "%s/packed-refs", gitdir);
            f = fopen(path, "r");
            if (!f)
                return -1;
            while (fgets(line, sizeof(line), f)) {
                if (line[0] == '#' || line[0] == '^')
                    continue;
                if (strlen(line) > 41 && strstr(line, p)) {
                    hex2bin(line, sha, 20);
                    fclose(f);
                    return 0;
                }
            }
            fclose(f);
            return -1;
        }
        if (!fgets(line, sizeof(line), f)) {
            fclose(f);
            return -1;
        }
        fclose(f);
        hex2bin(line, sha, 20);
        return 0;
    }
    /* detached HEAD */
    line[strcspn(line, "\r\n")] = '\0';
    hex2bin(line, sha, 20);
    snprintf(refname, refcap, "DETACHED");
    return 0;
}

static int mkpak(const char *gitdir, const char *outpath)
{
    char objdir[4096], path[4096];
    DIR *d;
    struct dirent *de;
    obj_rec *recs = NULL;
    unsigned int n = 0, cap = 0;
    unsigned char head[20];
    char refname[64];
    unsigned long dataofs;
    FILE *out;
    unsigned int i;

    if (read_head(gitdir, head, refname, sizeof(refname)) != 0) {
        fprintf(stderr, "mkpak: cannot resolve HEAD in %s\n", gitdir);
        return 1;
    }
    snprintf(objdir, sizeof(objdir), "%s/objects", gitdir);
    d = opendir(objdir);
    if (!d) {
        fprintf(stderr, "mkpak: cannot open %s\n", objdir);
        return 1;
    }

    while ((de = readdir(d)) != NULL) {
        DIR *sd;
        struct dirent *se;
        if (strlen(de->d_name) != 2 || !is_hex(de->d_name, 2))
            continue;
        snprintf(path, sizeof(path), "%s/%s", objdir, de->d_name);
        sd = opendir(path);
        if (!sd)
            continue;
        while ((se = readdir(sd)) != NULL) {
            char fpath[4096], hexs[41];
            unsigned char *infl;
            unsigned long ilen;
            uLongf dlen;
            int zrc;

            if (strlen(se->d_name) != 38 || !is_hex(se->d_name, 38))
                continue;
            if (n >= PAK_MAX_OBJ) {
                fprintf(stderr,
                        "mkpak: too many objects (> %d); pack/git-gc the repo or raise PAK_MAX_OBJ\n",
                        PAK_MAX_OBJ);
                closedir(sd); closedir(d);
                return 1;
            }
            if (n == cap) {
                cap = cap ? cap * 2 : 16;
                recs = realloc(recs, cap * sizeof(*recs));
            }
            snprintf(hexs, sizeof(hexs), "%s%s", de->d_name, se->d_name);
            hex2bin(hexs, recs[n].sha, 20);
            snprintf(fpath, sizeof(fpath), "%s/%s", path, se->d_name);
            if (read_file(fpath, &recs[n].comp, &recs[n].clen) != 0) {
                fprintf(stderr, "mkpak: cannot read %s\n", fpath);
                return 1;
            }
            if (recs[n].clen > PAK_MAX_COMP) {
                fprintf(stderr, "mkpak: %s too big (%lu > %lu)\n",
                        hexs, recs[n].clen, PAK_MAX_COMP);
                return 1;
            }
            /* inflate on host to learn type + size (sanity check too) */
            dlen = PAK_MAX_INFL * 4;
            infl = malloc(dlen);
            zrc = uncompress(infl, &dlen, recs[n].comp, recs[n].clen);
            if (zrc != Z_OK) {
                fprintf(stderr, "mkpak: %s not a zlib stream (%d)\n",
                        hexs, zrc);
                return 1;
            }
            if (dlen > PAK_MAX_INFL) {
                fprintf(stderr, "mkpak: %s inflates to %lu > %lu\n",
                        hexs, dlen, PAK_MAX_INFL);
                return 1;
            }
            recs[n].ilen = dlen;
            recs[n].type = (unsigned char)obj_type(infl, dlen);
            /* sanity: sha1(inflated) must equal filename hash */
            {
                unsigned char got[20];
                sha1(infl, dlen, got);
                if (memcmp(got, recs[n].sha, 20) != 0) {
                    fprintf(stderr, "mkpak: %s sha mismatch\n", hexs);
                    return 1;
                }
            }
            free(infl);
            n++;
        }
        closedir(sd);
    }
    closedir(d);

    if (n == 0) {
        fprintf(stderr, "mkpak: no loose objects found "
                "(repo may be fully packed - run 'git gc' is the "
                "opposite of what you want; commit something first)\n");
        return 1;
    }

    qsort(recs, n, sizeof(*recs), cmp_rec);

    out = fopen(outpath, "wb");
    if (!out) {
        fprintf(stderr, "mkpak: cannot write %s\n", outpath);
        return 1;
    }

    /* header */
    {
        unsigned char hdr[PAK_HDR_SIZE];
        memset(hdr, 0, sizeof(hdr));
        memcpy(hdr + PAK_HDR_MAGIC, PAK_MAGIC, 8);
        hdr[PAK_HDR_VER] = PAK_VERSION;
        hdr[PAK_HDR_COUNT] = (unsigned char)(n & 0xFF);
        hdr[PAK_HDR_COUNT + 1] = (unsigned char)(n >> 8);
        memcpy(hdr + PAK_HDR_HEADSHA, head, 20);
        memset(hdr + PAK_HDR_HEADREF, 0, 24);
        memcpy(hdr + PAK_HDR_HEADREF, refname,
               strlen(refname) < 24 ? strlen(refname) : 24);
        dataofs = PAK_HDR_SIZE + (unsigned long)n * PAK_IDX_SIZE;
        for (i = 0; i < 4; i++)
            hdr[PAK_HDR_IDXOFS + i] =
                (unsigned char)((PAK_HDR_SIZE >> (8 * i)) & 0xFF);
        fwrite(hdr, 1, PAK_HDR_SIZE, out);
    }

    /* index */
    {
        unsigned long ofs = PAK_HDR_SIZE + (unsigned long)n * PAK_IDX_SIZE;
        for (i = 0; i < n; i++) {
            unsigned char e[PAK_IDX_SIZE];
            memset(e, 0, sizeof(e));
            memcpy(e + PAK_E_SHA, recs[i].sha, 20);
            e[PAK_E_OFS]     = (unsigned char)(ofs & 0xFF);
            e[PAK_E_OFS + 1] = (unsigned char)((ofs >> 8) & 0xFF);
            e[PAK_E_OFS + 2] = (unsigned char)((ofs >> 16) & 0xFF);
            e[PAK_E_OFS + 3] = (unsigned char)((ofs >> 24) & 0xFF);
            e[PAK_E_CLEN]     = (unsigned char)(recs[i].clen & 0xFF);
            e[PAK_E_CLEN + 1] = (unsigned char)((recs[i].clen >> 8) & 0xFF);
            e[PAK_E_CLEN + 2] = (unsigned char)((recs[i].clen >> 16) & 0xFF);
            e[PAK_E_CLEN + 3] = (unsigned char)((recs[i].clen >> 24) & 0xFF);
            e[PAK_E_ILEN]     = (unsigned char)(recs[i].ilen & 0xFF);
            e[PAK_E_ILEN + 1] = (unsigned char)((recs[i].ilen >> 8) & 0xFF);
            e[PAK_E_TYPE]     = recs[i].type;
            fwrite(e, 1, PAK_IDX_SIZE, out);
            ofs += recs[i].clen;
        }
        (void)dataofs;
    }

    /* data: raw zlib streams exactly as found in .git/objects */
    for (i = 0; i < n; i++) {
        fwrite(recs[i].comp, 1, recs[i].clen, out);
        free(recs[i].comp);
    }
    fclose(out);
    free(recs);
    printf("mkpak: %u objects, HEAD=%s\n", n, refname);
    return 0;
}

/* ---------- report / cat ---------- */

static int do_report(const char *pakpath)
{
    host_ctx hc;
    pak_reader r;
    emit_t e;

    hc.f = fopen(pakpath, "rb");
    if (!hc.f) {
        fprintf(stderr, "report: cannot open %s\n", pakpath);
        return 1;
    }
    r.ctx = &hc;
    r.read_at = host_read_at;
    e.ctx = NULL;
    e.put = host_put;
    run_report(&r, &e);
    fclose(hc.f);
    return 0;
}

static int do_cat(const char *pakpath, const char *hexsha)
{
    host_ctx hc;
    pak_reader r;
    pak_index idx;
    unsigned char sha[20];
    static unsigned char compbuf[PAK_MAX_COMP];
    static unsigned char objbuf[PAK_MAX_INFL];
    int eofs;
    long n;

    hc.f = fopen(pakpath, "rb");
    if (!hc.f)
        return 1;
    r.ctx = &hc;
    r.read_at = host_read_at;
    if (pak_load_index(&r, &idx) != 0) {
        fprintf(stderr, "cat: bad pak\n");
        return 1;
    }
    hex2bin(hexsha, sha, 20);
    eofs = pak_find(&idx, sha);
    if (eofs < 0) {
        fprintf(stderr, "cat: not found\n");
        return 1;
    }
    n = pak_inflate_obj(&r, &idx, eofs, compbuf, PAK_MAX_COMP,
                        objbuf, PAK_MAX_INFL);
    if (n < 0) {
        fprintf(stderr, "cat: inflate failed\n");
        return 1;
    }
    fwrite(objbuf, 1, (size_t)n, stdout);
    fclose(hc.f);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr,
                "usage: git64tool mkpak <gitdir> <out.pak>\n"
                "       git64tool report <pak>\n"
                "       git64tool cat <pak> <hexsha>\n");
        return 1;
    }
    if (strcmp(argv[1], "mkpak") == 0 && argc == 4)
        return mkpak(argv[2], argv[3]);
    if (strcmp(argv[1], "report") == 0 && argc == 3)
        return do_report(argv[2]);
    if (strcmp(argv[1], "cat") == 0 && argc == 4)
        return do_cat(argv[2], argv[3]);
    fprintf(stderr, "bad args\n");
    return 1;
}
