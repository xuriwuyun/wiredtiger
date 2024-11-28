/*-
 * Copyright (c) 2014-present MongoDB, Inc.
 * Copyright (c) 2008-2014 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

#include "wt_internal.h"

/* FIXME-WT-13797 Remove this macro from block_ext.c when fully migrated. */

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

/*
 * __wt_extlist_first_srch --
 *     Search the skiplist for the first available slot.
 */
bool
__wt_extlist_first_srch(WT_EXT **head, wt_off_t size, WT_EXT ***stack)
{
    WT_EXT *ext;

    /*
     * Linear walk of the available chunks in offset order; take the first one that's large enough.
     */
    WT_EXT_FOREACH (ext, head)
        if (ext->size >= size)
            break;
    if (ext == NULL)
        return (false);

    /* Build a stack for the offset we want. */
    __wt_extlist_off_srch(head, ext->off, stack, false);
    return (true);
}

/*
 * __wt_extlist_size_srch --
 *     Search the by-size skiplist for the specified size.
 */
void
__wt_extlist_size_srch(WT_SIZE **head, wt_off_t size, WT_SIZE ***stack)
{
    WT_SIZE **szp;
    int i;

    /*
     * Start at the highest skip level, then go as far as possible at each level before stepping
     * down to the next.
     *
     * Return a stack for an exact match or the next-largest item.
     */
    for (i = WT_SKIP_MAXDEPTH - 1, szp = &head[i]; i >= 0;)
        if (*szp != NULL && (*szp)->size < size)
            szp = &(*szp)->next[i];
        else
            stack[i--] = szp--;
}

/*
 * __wt_extlist_off_srch_pair --
 *     Search a by-offset skiplist for before/after records of the specified offset.
 */
void
__wt_extlist_off_srch_pair(WT_EXTLIST *el, wt_off_t off, WT_EXT **beforep, WT_EXT **afterp)
{
    WT_EXT **extp, **head;
    int i;

    *beforep = *afterp = NULL;

    head = el->off;

    /*
     * Start at the highest skip level, then go as far as possible at each level before stepping
     * down to the next.
     */
    for (i = WT_SKIP_MAXDEPTH - 1, extp = &head[i]; i >= 0;) {
        if (*extp == NULL) {
            --i;
            --extp;
            continue;
        }

        if ((*extp)->off < off) { /* Keep going at this level */
            *beforep = *extp;
            extp = &(*extp)->next[i];
        } else { /* Drop down a level */
            *afterp = *extp;
            --i;
            --extp;
        }
    }
}

/*
 * __wt_extlist_ext_insert --
 *     Insert an extent into an extent list.
 */
int
__wt_extlist_ext_insert(WT_SESSION_IMPL *session, WT_EXTLIST *el, WT_EXT *ext)
{
    WT_EXT **astack[WT_SKIP_MAXDEPTH];
    WT_SIZE **sstack[WT_SKIP_MAXDEPTH], *szp;
    u_int i;

    /*
     * If we are inserting a new size onto the size skiplist, we'll need a new WT_SIZE structure for
     * that skiplist.
     */
    if (el->track_size) {
        __wt_extlist_size_srch(el->sz, ext->size, sstack);
        szp = *sstack[0];
        if (szp == NULL || szp->size != ext->size) {
            WT_RET(__wti_block_size_alloc(session, &szp));
            szp->size = ext->size;
            szp->depth = ext->depth;
            for (i = 0; i < ext->depth; ++i) {
                szp->next[i] = *sstack[i];
                *sstack[i] = szp;
            }
        }

        /*
         * Insert the new WT_EXT structure into the size element's offset skiplist.
         */
        __wt_extlist_off_srch(szp->off, ext->off, astack, true);
        for (i = 0; i < ext->depth; ++i) {
            ext->next[i + ext->depth] = *astack[i];
            *astack[i] = ext;
        }
    }
#ifdef HAVE_DIAGNOSTIC
    if (!el->track_size)
        for (i = 0; i < ext->depth; ++i)
            ext->next[i + ext->depth] = NULL;
#endif

    /* Insert the new WT_EXT structure into the offset skiplist. */
    __wt_extlist_off_srch(el->off, ext->off, astack, false);
    for (i = 0; i < ext->depth; ++i) {
        ext->next[i] = *astack[i];
        *astack[i] = ext;
    }

    ++el->entries;
    el->bytes += (uint64_t)ext->size;

    /* Update the cached end-of-list. */
    if (ext->next[0] == NULL)
        el->last = ext;

    return (0);
}

/*
 * __wt_extlist_off_insert --
 *     Insert a file range into an extent list.
 */
int
__wt_extlist_off_insert(WT_SESSION_IMPL *session, WT_EXTLIST *el, wt_off_t off, wt_off_t size)
{
    WT_EXT *ext;

    WT_RET(__wti_block_ext_alloc(session, &ext));
    ext->off = off;
    ext->size = size;

    return (__wt_extlist_ext_insert(session, el, ext));
}

/*
 * __wt_extlist_off_srch_inclusive --
 *     Search a by-offset skiplist for the extent that contains the given offset, or if there is no
 *     such extent, then get the next extent.
 */
WT_EXT *
__wt_extlist_off_srch_inclusive(WT_EXTLIST *el, wt_off_t off)
{
    WT_EXT *after, *before;

    __wt_extlist_off_srch_pair(el, off, &before, &after);

    /* Check if the search key is in the before extent. Otherwise return the after extent. */
    if (before != NULL && before->off <= off && before->off + before->size > off)
        return (before);
    else
        return (after);
}

#if defined(HAVE_DIAGNOSTIC) || defined(HAVE_UNITTEST)
/*
 * __wt_extlist_off_match --
 *     Return if any part of a specified range appears on a specified extent list.
 */
bool
__wt_extlist_off_match(WT_EXTLIST *el, wt_off_t off, wt_off_t size)
{
    WT_EXT *after, *before;

    if (WT_UNLIKELY(size == 0))
        return (false);

    /* Search for before and after entries for the offset. */
    __wt_extlist_off_srch_pair(el, off, &before, &after);

    /* If "before" or "after" overlaps, we have a winner. */
    if (before != NULL && before->off + before->size > off)
        return (true);
    if (after != NULL && off + size > after->off)
        return (true);
    return (false);
}
#endif

/*
 * __wt_extlist_off_remove --
 *     Remove a record from an extent list.
 */
int
__wt_extlist_off_remove(
  WT_SESSION_IMPL *session, bool verify, WT_EXTLIST *el, wt_off_t off, WT_EXT **extp)
{
    WT_EXT **astack[WT_SKIP_MAXDEPTH], *ext;
    WT_SIZE **sstack[WT_SKIP_MAXDEPTH], *szp;
    u_int i;

    /* Find and remove the record from the by-offset skiplist. */
    __wt_extlist_off_srch(el->off, off, astack, false);
    ext = *astack[0];
    if (ext == NULL || ext->off != off)
        goto corrupt;
    for (i = 0; i < ext->depth; ++i)
        *astack[i] = ext->next[i];

    /*
     * Find and remove the record from the size's offset skiplist; if that empties the by-size
     * skiplist entry, remove it as well.
     */
    if (el->track_size) {
        __wt_extlist_size_srch(el->sz, ext->size, sstack);
        szp = *sstack[0];
        if (szp == NULL || szp->size != ext->size)
            WT_RET_PANIC(session, EINVAL, "extent not found in by-size list during remove");
        __wt_extlist_off_srch(szp->off, off, astack, true);
        ext = *astack[0];
        if (ext == NULL || ext->off != off)
            goto corrupt;
        for (i = 0; i < ext->depth; ++i)
            *astack[i] = ext->next[i + ext->depth];
        if (szp->off[0] == NULL) {
            for (i = 0; i < szp->depth; ++i)
                *sstack[i] = szp->next[i];
            __wti_block_size_free(session, &szp);
        }
    }
#ifdef HAVE_DIAGNOSTIC
    if (!el->track_size) {
        bool not_null;
        for (i = 0, not_null = false; i < ext->depth; ++i)
            if (ext->next[i + ext->depth] != NULL)
                not_null = true;
        WT_ASSERT(session, not_null == false);
    }
#endif

    --el->entries;
    el->bytes -= (uint64_t)ext->size;

    /* Return the record if our caller wants it, otherwise free it. */
    if (extp == NULL) {
        WT_EXT *ext_to_free = ext;
        __wti_block_ext_free(session, &ext_to_free);
    } else
        *extp = ext;

    /* Update the cached end-of-list. */
    if (el->last == ext)
        /* To save time, update to the correct value later. */
        el->last = NULL;

    return (0);

corrupt:
    WT_EXT_VERIFY_RET(
      session, verify, EINVAL, "attempt to remove non-existent offset from an extent list");
}
