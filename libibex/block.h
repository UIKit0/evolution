
/* public interfaces for block io routines */

#ifndef _BLOCK_H
#define _BLOCK_H

#include <glib.h>

typedef guint32 nameid_t;
typedef guint32 blockid_t;

#define BLOCK_BITS (8)
#define BLOCK_SIZE (1<<BLOCK_BITS)
#define CACHE_SIZE 256		/* blocks in disk cache */

/* root block */
struct _root {
	char version[4];

	blockid_t free;		/* list of free blocks */
	blockid_t roof;		/* top of allocated space, everything below is in a free or used list */

	blockid_t words;	/* root of words index */
	blockid_t names;	/* root of names index */
};

/* key data for each index entry */
struct _idx_key {
	blockid_t root;
	int keyoffset;
};

/* disk structure for blocks */
struct _block {
	blockid_t next;		/* next block */
	guint32 used;		/* number of elements used */
	union {
		struct _idx_key keys[(BLOCK_SIZE-8)/sizeof(struct _idx_key)];
		char keydata[BLOCK_SIZE-8]; /* key data */
		nameid_t data[(BLOCK_SIZE-8)/4]; /* references */
	} block_u;
};
#define bl_data block_u.data
#define bl_keys block_u.keys
#define bl_keydata block_u.keydata

/* custom list structure, for a simple/efficient cache */
struct _listnode {
	struct _listnode *next;
	struct _listnode *prev;
};
struct _list {
	struct _listnode *head;
	struct _listnode *tail;
	struct _listnode *tailpred;
};

void ibex_list_new(struct _list *v);
struct _listnode *ibex_list_addhead(struct _list *l, struct _listnode *n);
struct _listnode *ibex_list_addtail(struct _list *l, struct _listnode *n);
struct _listnode *ibex_list_remove(struct _listnode *n);

/* in-memory structure for block cache */
struct _memblock {
	struct _memblock *next;
	struct _memblock *prev;

	blockid_t block;
	int flags;

	struct _block data;
};
#define BLOCK_DIRTY (1<<0)

struct _memcache {
	struct _list nodes;
	int count;		/* nodes in cache */

	GHashTable *index;	/* blockid->memblock mapping */
	int fd;			/* file fd */

	/* temporary here */
	struct _IBEXWord *words; /* word index */
};

struct _memcache *ibex_block_cache_open(const char *name, int flags, int mode);
void ibex_block_cache_close(struct _memcache *block_cache);
void ibex_block_cache_sync(struct _memcache *block_cache);
void ibex_block_cache_flush(struct _memcache *block_cache);

blockid_t ibex_block_get(struct _memcache *block_cache);
void ibex_block_free(struct _memcache *block_cache, blockid_t blockid);
void ibex_block_dirty(struct _block *block);
struct _block *ibex_block_read(struct _memcache *block_cache, blockid_t blockid);

#endif /* ! _BLOCK_H */
