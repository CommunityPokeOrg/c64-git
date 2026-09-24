/* gitobj.h - parse inflated git loose objects.
 * An inflated loose object is: "<type> <len>\0<body>" where
 * type in {blob, tree, commit, tag} and len = decimal body length. */
#ifndef GITOBJ_H
#define GITOBJ_H

#define GO_TYPE_BLOB   PAK_TYPE_BLOB
#define GO_TYPE_TREE   PAK_TYPE_TREE
#define GO_TYPE_COMMIT PAK_TYPE_COMMIT
#define GO_TYPE_TAG    PAK_TYPE_TAG

/* parse object header; returns body offset, or -1 on error.
 * type_out gets PAK_TYPE_*. */
int go_parse_header(const unsigned char *buf, unsigned long len,
                    unsigned char *type_out, unsigned long *body_len);

typedef struct commit_info_s {
    unsigned char tree[20];
    unsigned char parents[3][20];
    unsigned char nparents;
    char author[80];        /* "Name <mail> ts tz" truncated */
    char subject[64];       /* first line of the message */
} commit_info;

/* parse a commit body (already past the object header). returns 0/-1 */
int go_parse_commit(const unsigned char *body, unsigned long len,
                    commit_info *ci);

typedef struct tree_entry_s {
    unsigned long mode;                 /* e.g. 040000, 100644, 100755 */
    char name[48];
    unsigned char sha[20];
} tree_entry;

/* parse a tree body into entries[]. returns entry count or <0 */
int go_parse_tree(const unsigned char *body, unsigned long len,
                  tree_entry *entries, unsigned char max);

#endif
