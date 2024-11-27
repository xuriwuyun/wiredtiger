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
