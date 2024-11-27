/*-
 * Copyright (c) 2014-present MongoDB, Inc.
 * Copyright (c) 2008-2014 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

#include "wt_internal.h"

/*
 * __wt_extlist_off_srch_last --
 *     Return the last element in the list, along with a stack for appending.
 *
 * Return a stack such that the caller can append a new entry to the skip list by inserting it after
 *     each element in the stack. For non-empty levels, this will be the last element at that level
 *     of the skip list. For a level with no entries, this will be the corresponding entry in the
 *     head stack.
 */
WT_EXT *
__wt_extlist_off_srch_last(WT_EXT **head, WT_EXT ***stack)
{
    WT_EXT **extp, *last;
    int i;

    last = NULL; /* The list may be empty */

    /*
     * Start at the highest skip level, then go as far as possible at each level before stepping
     * down to the next.
     */
    for (i = WT_SKIP_MAXDEPTH - 1, extp = &head[i]; i >= 0;)
        if (*extp != NULL) {
            last = *extp;
            extp = &(*extp)->next[i];
        } else
            stack[i--] = extp--;
    return (last);
}

/*
 * __wt_extlist_off_srch --
 *     Search a by-offset skiplist (either the primary by-offset list, or the by-offset list
 *     referenced by a size entry), for the specified offset.
 */
void
__wt_extlist_off_srch(WT_EXT **head, wt_off_t off, WT_EXT ***stack, bool skip_off)
{
    WT_EXT **extp;
    int i;

    /*
     * Start at the highest skip level, then go as far as possible at each level before stepping
     * down to the next.
     *
     * Return a stack for an exact match or the next-largest item.
     *
     * The WT_EXT structure contains two skiplists, the primary one and the per-size bucket one: if
     * the skip_off flag is set, offset the skiplist array by the depth specified in this particular
     * structure.
     */
    for (i = WT_SKIP_MAXDEPTH - 1, extp = &head[i]; i >= 0;)
        if (*extp != NULL && (*extp)->off < off)
            extp = &(*extp)->next[i + (skip_off ? (*extp)->depth : 0)];
        else
            stack[i--] = extp--;
}
