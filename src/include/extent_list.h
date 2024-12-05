/*-
 * Copyright (c) 2014-present MongoDB, Inc.
 * Copyright (c) 2008-2014 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

#pragma once

/*
 * WT_EXT_VERIFY_RET --
 *	Handle extension list errors that would normally panic the system but
 * which should fail gracefully when verifying.
 */
#define WT_EXT_VERIFY_RET(session, verify, v, ...)                                                 \
    do {                                                                                           \
        int __ret = (v);                                                                           \
        __wt_err(session, __ret, __VA_ARGS__);                                                     \
        return ((verify) ? __ret :                                                                 \
                           __wt_panic(session, WT_PANIC, "block manager extension list failure")); \
    } while (0)

/*
 * An extent list is based on two skiplists: first, a by-offset list linking WT_EXT elements and
 * sorted by file offset (low-to-high), second, a by-size list linking WT_SIZE elements and sorted
 * by chunk size (low-to-high).
 *
 * Additionally, each WT_SIZE element on the by-size has a skiplist of its own, linking WT_EXT
 * elements and sorted by file offset (low-to-high). This list has an entry for extents of a
 * particular size.
 *
 * The trickiness is each individual WT_EXT element appears on two skiplists. In order to minimize
 * allocation calls, we allocate a single array of WT_EXT pointers at the end of the WT_EXT
 * structure, for both skiplists, and store the depth of the skiplist in the WT_EXT structure. The
 * skiplist entries for the offset skiplist start at WT_EXT.next[0] and the entries for the size
 * skiplist start at WT_EXT.next[WT_EXT.depth].
 *
 * One final complication: we only maintain the per-size skiplist for the avail list, the alloc and
 * discard extent lists are not searched based on size.
 */

/*
 * WT_EXTLIST --
 *	An extent list.
 */
struct __wt_extlist {
    char *name; /* Name */

    uint64_t bytes;   /* Byte count */
    uint32_t entries; /* Entry count */

    uint32_t objectid; /* Written object ID */
    wt_off_t offset;   /* Written extent offset */
    uint32_t checksum; /* Written extent checksum */
    uint32_t size;     /* Written extent size */

    bool track_size; /* Maintain per-size skiplist */

    WT_EXT *last; /* Cached last element */

    WT_EXT *off[WT_SKIP_MAXDEPTH]; /* Size/offset skiplists */
    WT_SIZE *sz[WT_SKIP_MAXDEPTH];
};

/*
 * WT_EXT --
 *	Encapsulation of an extent, either allocated or freed within the
 * checkpoint.
 */
struct __wt_ext {
    wt_off_t off;  /* Extent's file offset */
    wt_off_t size; /* Extent's Size */

    uint8_t depth; /* Skip list depth */

    /*
     * Variable-length array, sized by the number of skiplist elements. The first depth array
     * entries are the address skiplist elements, the second depth array entries are the size
     * skiplist.
     */
    WT_EXT *next[0]; /* Offset, size skiplists */
};

/*
 * WT_SIZE --
 *	Encapsulation of a block size skiplist entry.
 */
struct __wt_size {
    wt_off_t size; /* Size */

    uint8_t depth; /* Skip list depth */

    WT_EXT *off[WT_SKIP_MAXDEPTH]; /* Per-size offset skiplist */

    /*
     * We don't use a variable-length array for the size skiplist, we want to be able to use any
     * cached WT_SIZE structure as the head of a list, and we don't know the related WT_EXT
     * structure's depth.
     */
    WT_SIZE *next[WT_SKIP_MAXDEPTH]; /* Size skiplist */
};

/*
 * WT_EXT_FOREACH --
 *	Walk all extents in an extent list.
 */
#define WT_EXT_FOREACH(skip, head) \
    for ((skip) = (head)[0]; (skip) != NULL; (skip) = (skip)->next[0])

/* FIXME-WT-13797 - Remove all CODE_CHANGE comments once they're been acknowledged in the PR. */
/* CODE_CHANGE - WT_EXT_FOREACH is removed as dead code. */

/*
 * WT_EXT_FOREACH_FROM_OFFSET_INCL --
 *	Walk a by-offset skiplist from the given offset, starting with the extent that contains the
 * given offset if available.
 */
#define WT_EXT_FOREACH_FROM_OFFSET_INCL(skip, el, start)                          \
    for ((skip) = __wt_extlist_off_srch_inclusive((el), (start)); (skip) != NULL; \
         (skip) = (skip)->next[0])
