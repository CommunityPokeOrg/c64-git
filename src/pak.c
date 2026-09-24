/* pak.c - GITREPO.PAK reader: index, lookup, inflate via puff. */
#include <string.h>
#include "pak.h"
#include "puff.h"

unsigned char hexval(char c)
{
    if (c >= '0' && c <= '9') return (unsigned char)(c - '0');
    if (c >= 'a' && c <= 'f') return (unsigned char)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return (unsigned char)(c - 'A' + 10);
    return 0;
}

void hex2bin(const char *hex, unsigned char *bin, unsigned int n)
{
    unsigned int i;
    for (i = 0; i < n; i++)
        bin[i] = (unsigned char)((hexval(hex[i * 2]) << 4) |
                                 hexval(hex[i * 2 + 1]));
}

void bin2hex(const unsigned char *bin, char *hex, unsigned int n)
{
    static const char hexd[] = "0123456789abcdef";
    unsigned int i;
    for (i = 0; i < n; i++) {
        hex[i * 2]     = hexd[bin[i] >> 4];
        hex[i * 2 + 1] = hexd[bin[i] & 15];
    }
    hex[n * 2] = '\0';
}

unsigned long get_u32le(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

unsigned int get_u16le(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

int pak_load_index(pak_reader *r, pak_index *idx)
{
    unsigned char hdr[PAK_HDR_SIZE];
    unsigned long idxofs;
    unsigned int n;

    if (r->read_at(r->ctx, 0, hdr, PAK_HDR_SIZE) != PAK_HDR_SIZE)
        return -1;
    if (memcmp(hdr + PAK_HDR_MAGIC, PAK_MAGIC, 8) != 0)
        return -2;
    if (hdr[PAK_HDR_VER] != PAK_VERSION)
        return -3;
    n = get_u16le(hdr + PAK_HDR_COUNT);
    if (n > PAK_MAX_OBJ)
        return -4;
    idx->count = n;
    memcpy(idx->head_sha, hdr + PAK_HDR_HEADSHA, 20);
    memcpy(idx->head_ref, hdr + PAK_HDR_HEADREF, 24);
    idx->head_ref[24] = '\0';
    idxofs = get_u32le(hdr + PAK_HDR_IDXOFS);
    if (n == 0)
        return 0;
    if (r->read_at(r->ctx, idxofs, idx->entries,
                   n * PAK_IDX_SIZE) != (long)(n * PAK_IDX_SIZE))
        return -5;
    return 0;
}

int pak_find(const pak_index *idx, const unsigned char sha[20])
{
    unsigned int i;
    for (i = 0; i < idx->count; i++) {
        if (memcmp(idx->entries + i * PAK_IDX_SIZE + PAK_E_SHA, sha, 20) == 0)
            return (int)(i * PAK_IDX_SIZE);
    }
    return -1;
}

/* inflate a zlib stream (2-byte header + raw deflate + adler32) into out.
 * returns inflated length, or <0 on error. */
long zlib_inflate(const unsigned char *src, unsigned long srclen,
                  unsigned char *dst, unsigned long dstcap)
{
    unsigned long destlen = dstcap;
    unsigned long sourcelen = srclen - 2;
    int rc;
    if (srclen < 6)
        return -1;
    /* skip 2-byte zlib header (CMF/FLG); adler32 trailer is ignored
     * (SHA-1 over the object is our integrity check anyway) */
    rc = puff(dst, &destlen, src + 2, &sourcelen);
    if (rc != 0)
        return -2;
    return (long)destlen;
}

long pak_inflate_obj(pak_reader *r, const pak_index *idx, int entry_ofs,
                     unsigned char *compbuf, unsigned long compcap,
                     unsigned char *outbuf, unsigned long outcap)
{
    const unsigned char *e = idx->entries + entry_ofs;
    unsigned long ofs  = get_u32le(e + PAK_E_OFS);
    unsigned long clen = get_u32le(e + PAK_E_CLEN);

    if (clen > compcap)
        return -10;
    if (r->read_at(r->ctx, ofs, compbuf, (unsigned int)clen) != (long)clen)
        return -11;
    return zlib_inflate(compbuf, clen, outbuf, outcap);
}
