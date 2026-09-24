/* sha1.c - one-shot SHA-1 (FIPS 180-1).
 * Simple, small-footprint implementation for the C64's flat 64K space:
 * the message is hashed in place, padding is synthesized on the fly. */
#include "sha1.h"

#define W32 unsigned long

static W32 rol32(W32 v, unsigned char n)
{
    return ((v << n) | (v >> (32 - n))) & 0xFFFFFFFFUL;
}

void sha1(const unsigned char *data, unsigned long len,
          unsigned char digest[20])
{
    /* statics: cc65 allows only a handful of locals per function */
    static W32 h[5];
    static unsigned long ml, total, pos;
    ml = len * 8UL;                       /* message bit length (< 2^32) */

    /* total = len + 1 (0x80) + k zeros + 8 length bytes, multiple of 64 */
    total = len + 9;
    while (total & 63)
        total++;

    h[0] = 0x67452301UL;
    h[1] = 0xEFCDAB89UL;
    h[2] = 0x98BADCFEUL;
    h[3] = 0x10325476UL;
    h[4] = 0xC3D2E1F0UL;

    {
    static W32 w[80];
    static W32 a, b, c, d, e, f, k, t, v;
    static unsigned long bidx, sh;
    static unsigned char i, j, by;
    for (pos = 0; pos < total; pos += 64) {

        for (i = 0; i < 16; i++) {
            v = 0;
            for (j = 0; j < 4; j++) {
                bidx = pos + (unsigned long)i * 4 + j;
                if (bidx < len)
                    by = data[bidx];
                else if (bidx == len)
                    by = 0x80;
                else if (bidx >= total - 8) {
                    /* big-endian 64-bit length; ml is only 32 bits and
                     * a 32-bit shift would be UB, so guard it */
                    sh = (total - 1 - bidx) * 8;
                    by = (sh >= 32) ? 0 : (unsigned char)(ml >> sh);
                }
                else
                    by = 0;
                v = (v << 8) | by;
            }
            w[i] = v;
        }
        for (i = 16; i < 80; i++)
            w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4];
        for (i = 0; i < 80; i++) {
            if (i < 20)      { f = (b & c) | ((~b) & d);        k = 0x5A827999UL; }
            else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1UL; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCUL; }
            else             { f = b ^ c ^ d;                   k = 0xCA62C1D6UL; }
            t = (rol32(a, 5) + f + e + k + w[i]) & 0xFFFFFFFFUL;
            e = d; d = c; c = rol32(b, 30); b = a; a = t;
        }
        h[0] = (h[0] + a) & 0xFFFFFFFFUL;
        h[1] = (h[1] + b) & 0xFFFFFFFFUL;
        h[2] = (h[2] + c) & 0xFFFFFFFFUL;
        h[3] = (h[3] + d) & 0xFFFFFFFFUL;
        h[4] = (h[4] + e) & 0xFFFFFFFFUL;
    }
    }                                             /* end static-locals block */

    {
        unsigned char k;
        for (k = 0; k < 5; k++) {
            digest[k * 4]     = (unsigned char)(h[k] >> 24);
            digest[k * 4 + 1] = (unsigned char)(h[k] >> 16);
            digest[k * 4 + 2] = (unsigned char)(h[k] >> 8);
            digest[k * 4 + 3] = (unsigned char)(h[k]);
        }
    }
}
