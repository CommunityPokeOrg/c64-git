/* sha1.h - one-shot SHA-1, portable between cc65 (C64) and host C.
 * Words are held in unsigned long (>= 32 bits on both cc65 and host). */
#ifndef SHA1_H
#define SHA1_H

void sha1(const unsigned char *data, unsigned long len,
          unsigned char digest[20]);

#endif
