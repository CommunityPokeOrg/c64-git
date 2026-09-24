/* pak.h - GITREPO.PAK: a 1541/sequential-read-friendly container of git
 * loose objects. Layout is documented in README.md. All integers LE. */
#ifndef PAK_H
#define PAK_H

#include "ascii_charmap.h"

#define PAK_MAGIC       "C64GITPK"
#define PAK_VERSION     1

/* header (fixed 60 bytes) */
#define PAK_HDR_MAGIC   0    /* 8 bytes  "C64GITPK" */
#define PAK_HDR_VER     8    /* u8 */
#define PAK_HDR_FLAGS   9    /* u8 */
#define PAK_HDR_COUNT   10   /* u16 number of index entries */
#define PAK_HDR_HEADSHA 12   /* 20 bytes binary sha1 of HEAD commit */
#define PAK_HDR_HEADREF 32   /* 24 bytes zero-padded ref name */
#define PAK_HDR_IDXOFS  56   /* u32 absolute offset of index */
#define PAK_HDR_SIZE    60

/* index entry (32 bytes, sorted by sha) */
#define PAK_IDX_SIZE    32
#define PAK_E_SHA       0    /* 20 bytes */
#define PAK_E_OFS       20   /* u32 offset of zlib stream */
#define PAK_E_CLEN      24   /* u32 compressed length */
#define PAK_E_ILEN      28   /* u16 inflated length (<= 65535) */
#define PAK_E_TYPE      30   /* u8: 1=blob 2=tree 3=commit 4=tag */
#define PAK_E_PAD       31

#define PAK_TYPE_BLOB   1
#define PAK_TYPE_TREE   2
#define PAK_TYPE_COMMIT 3
#define PAK_TYPE_TAG    4

/* resource caps (same code compiles for host and C64; these are the
 * C64 limits - host build can raise them via -D) */
#ifndef PAK_MAX_OBJ
#define PAK_MAX_OBJ     64
#endif
#ifndef PAK_MAX_COMP
#define PAK_MAX_COMP    (12UL*1024UL)
#endif
#ifndef PAK_MAX_INFL
#define PAK_MAX_INFL    (12UL*1024UL)
#endif

/* storage abstraction: the caller supplies random access by absolute
 * offset. On the C64 this wraps cbm_read on a sequentially-read file
 * (re-open + skip when seeking backwards). */
typedef struct pak_reader_s {
    void *ctx;
    long (*read_at)(void *ctx, unsigned long offset,
                    unsigned char *buf, unsigned int len);
} pak_reader;

typedef struct pak_index_s {
    unsigned int count;
    unsigned char entries[PAK_MAX_OBJ * PAK_IDX_SIZE];
    unsigned char head_sha[20];
    char head_ref[25];
} pak_index;

/* returns 0 on success, <0 on error */
int  pak_load_index(pak_reader *r, pak_index *idx);

/* find index entry for binary sha1; returns offset into entries[] or -1 */
int  pak_find(const pak_index *idx, const unsigned char sha[20]);

/* read + inflate object described by index entry at entry_ofs;
 * fills outbuf (must be PAK_MAX_INFL) with "<type> <len>\0<body>";
 * returns inflated length or <0 on error. */
long pak_inflate_obj(pak_reader *r, const pak_index *idx, int entry_ofs,
                     unsigned char *compbuf, unsigned long compcap,
                     unsigned char *outbuf, unsigned long outcap);

/* helpers */
unsigned char hexval(char c);
void hex2bin(const char *hex, unsigned char *bin, unsigned int n);
void bin2hex(const unsigned char *bin, char *hex, unsigned int n);
unsigned long get_u32le(const unsigned char *p);
unsigned int  get_u16le(const unsigned char *p);

#endif
