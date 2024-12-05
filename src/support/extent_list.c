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

/*
 * __wt_extlist_off_remove_overlap --
 *     Remove a range from an extent list, where the range may be part of an overlapping entry.
 */
int
__wt_extlist_off_remove_overlap(
  WT_SESSION_IMPL *session, bool verify, WT_EXTLIST *el, wt_off_t off, wt_off_t size)
{
    WT_EXT *after, *before, *ext;
    wt_off_t a_off, a_size, b_off, b_size;

    /* Search for before and after entries for the offset. */
    __wt_extlist_off_srch_pair(el, off, &before, &after);

    /* If "before" or "after" overlaps, retrieve the overlapping entry. */
    if (before != NULL && before->off + before->size > off) {
        WT_RET(__wt_extlist_off_remove(session, verify, el, before->off, &ext));

        WT_ASSERT(session, ext->off + ext->size >= off + size);

        /* Calculate overlapping extents. */
        a_off = ext->off;
        a_size = off - ext->off;
        b_off = off + size;
        b_size = ext->size - (a_size + size);

        if (a_size > 0) {
            __wt_verbose_debug2(session, WT_VERB_BLOCK,
              "%s: %" PRIdMAX "-%" PRIdMAX " range shrinks to %" PRIdMAX "-%" PRIdMAX, el->name,
              (intmax_t)before->off, (intmax_t)before->off + (intmax_t)before->size,
              (intmax_t)(a_off), (intmax_t)(a_off + a_size));
        }

        if (b_size > 0) {
            __wt_verbose_debug2(session, WT_VERB_BLOCK,
              "%s: %" PRIdMAX "-%" PRIdMAX " range shrinks to %" PRIdMAX "-%" PRIdMAX, el->name,
              (intmax_t)before->off, (intmax_t)before->off + (intmax_t)before->size,
              (intmax_t)(b_off), (intmax_t)(b_off + b_size));
        }
    } else if (after != NULL && off + size > after->off) {
        WT_RET(__wt_extlist_off_remove(session, verify, el, after->off, &ext));

        WT_ASSERT(session, off == ext->off && off + size <= ext->off + ext->size);

        /*
         * Calculate overlapping extents. There's no initial overlap since the after extent
         * presumably cannot begin before "off".
         */
        a_off = WT_BLOCK_INVALID_OFFSET;
        a_size = 0;
        b_off = off + size;
        b_size = ext->size - (b_off - ext->off);

        if (b_size > 0)
            __wt_verbose_debug2(session, WT_VERB_BLOCK,
              "%s: %" PRIdMAX "-%" PRIdMAX " range shrinks to %" PRIdMAX "-%" PRIdMAX, el->name,
              (intmax_t)after->off, (intmax_t)after->off + (intmax_t)after->size, (intmax_t)(b_off),
              (intmax_t)(b_off + b_size));

    } else
        return (WT_NOTFOUND);

    /*
     * If there are overlaps, insert the item; re-use the extent structure and save the allocation
     * (we know there's no need to merge).
     */
    if (a_size > 0) {
        ext->off = a_off;
        ext->size = a_size;
        WT_RET(__wt_extlist_ext_insert(session, el, ext));
        ext = NULL;
    }
    if (b_size > 0) {
        if (ext == NULL)
            WT_RET(__wt_extlist_off_insert(session, el, b_off, b_size));
        else {
            ext->off = b_off;
            ext->size = b_size;
            WT_RET(__wt_extlist_ext_insert(session, el, ext));
            ext = NULL;
        }
    }
    if (ext != NULL)
        __wti_block_ext_free(session, &ext);
    return (0);
}

#ifdef HAVE_DIAGNOSTIC
/*
 * __wt_extlist_overlap_check --
 *     Return if the extent lists overlap.
 */
int
__wt_extlist_overlap_check(WT_SESSION_IMPL *session, WT_EXTLIST *al, WT_EXTLIST *bl)
{
    WT_EXT *a, *b;

    a = al->off[0];
    b = bl->off[0];

    /* Walk the lists in parallel, looking for overlaps. */
    while (a != NULL && b != NULL) {
        /*
         * If there's no overlap, move the lower-offset entry to the next entry in its list.
         */
        if (a->off + a->size <= b->off) {
            a = a->next[0];
            continue;
        }
        if (b->off + b->size <= a->off) {
            b = b->next[0];
            continue;
        }
        WT_RET_PANIC(session, EINVAL, "checkpoint merge check: %s list overlaps the %s list",
          al->name, bl->name);
    }
    return (0);
}
#endif

/*
 * __wti_extlist_off_remove_overlap --
 *     Remove a range from an extent list, where the range may be part of an overlapping entry.
 */
int
__wti_extlist_off_remove_overlap(
  WT_SESSION_IMPL *session, bool verify, WT_EXTLIST *el, wt_off_t off, wt_off_t size)
{
    WT_ASSERT(session, off != WT_BLOCK_INVALID_OFFSET);
    return (__wt_extlist_off_remove_overlap(session, verify, el, off, size));
}

/*
 * __wt_extlist_merge --
 *     Insert an extent into an extent list, merging if possible (internal version).
 */
int
__wt_extlist_merge(
  WT_SESSION_IMPL *session, bool verify, WT_EXTLIST *el, wt_off_t off, wt_off_t size)
{
    WT_EXT *after, *before, *ext;

    /*
     * Retrieve the records preceding/following the offset. If the records are contiguous with the
     * free'd offset, combine records.
     */
    __wt_extlist_off_srch_pair(el, off, &before, &after);
    if (before != NULL) {
        if (before->off + before->size > off)
            WT_EXT_VERIFY_RET(session, verify, EINVAL,
              "%s: existing range %" PRIdMAX "-%" PRIdMAX " overlaps with merge range %" PRIdMAX
              "-%" PRIdMAX,
              el->name, (intmax_t)before->off, (intmax_t)(before->off + before->size),
              (intmax_t)off, (intmax_t)(off + size));
        if (before->off + before->size != off)
            before = NULL;
    }
    if (after != NULL) {
        if (off + size > after->off) {
            WT_EXT_VERIFY_RET(session, verify, EINVAL,
              "%s: merge range %" PRIdMAX "-%" PRIdMAX " overlaps with existing range %" PRIdMAX
              "-%" PRIdMAX,
              el->name, (intmax_t)off, (intmax_t)(off + size), (intmax_t)after->off,
              (intmax_t)(after->off + after->size));
        }
        if (off + size != after->off)
            after = NULL;
    }
    if (before == NULL && after == NULL) {
        __wt_verbose_debug2(session, WT_VERB_BLOCK, "%s: insert range %" PRIdMAX "-%" PRIdMAX,
          el->name, (intmax_t)off, (intmax_t)(off + size));

        return (__wt_extlist_off_insert(session, el, off, size));
    }

    /*
     * If the "before" offset range abuts, we'll use it as our new record; if the "after" offset
     * range also abuts, include its size and remove it from the system. Else, only the "after"
     * offset range abuts, use the "after" offset range as our new record. In either case, remove
     * the record we're going to use, adjust it and re-insert it.
     */
    if (before == NULL) {
        WT_RET(__wt_extlist_off_remove(session, verify, el, after->off, &ext));

        __wt_verbose_debug2(session, WT_VERB_BLOCK,
          "%s: range grows from %" PRIdMAX "-%" PRIdMAX ", to %" PRIdMAX "-%" PRIdMAX, el->name,
          (intmax_t)ext->off, (intmax_t)(ext->off + ext->size), (intmax_t)off,
          (intmax_t)(off + ext->size + size));

        ext->off = off;
        ext->size += size;
    } else {
        if (after != NULL) {
            size += after->size;
            WT_RET(__wt_extlist_off_remove(session, verify, el, after->off, NULL));
        }
        WT_RET(__wt_extlist_off_remove(session, verify, el, before->off, &ext));

        __wt_verbose_debug2(session, WT_VERB_BLOCK,
          "%s: range grows from %" PRIdMAX "-%" PRIdMAX ", to %" PRIdMAX "-%" PRIdMAX, el->name,
          (intmax_t)ext->off, (intmax_t)(ext->off + ext->size), (intmax_t)ext->off,
          (intmax_t)(ext->off + ext->size + size));

        ext->size += size;
    }
    return (__wt_extlist_ext_insert(session, el, ext));
}

/*
 * __wti_extlist_merge --
 *     Merge one extent list into another.
 */
int
__wti_extlist_merge(WT_SESSION_IMPL *session, bool verify, WT_EXTLIST *a, WT_EXTLIST *b)
{
    WT_EXT *ext;
    WT_EXTLIST tmp;
    u_int i;

    /*
     * We should hold the live lock here when running on the live checkpoint. But there is no easy
     * way to determine if the checkpoint is live so we cannot assert the locking here.
     */

    __wt_verbose_debug2(session, WT_VERB_BLOCK, "merging %s into %s", a->name, b->name);

    /*
     * Sometimes the list we are merging is much bigger than the other: if so, swap the lists around
     * to reduce the amount of work we need to do during the merge. The size lists have to match as
     * well, so this is only possible if both lists are tracking sizes, or neither are.
     */
    if (a->track_size == b->track_size && a->entries > b->entries) {
        tmp = *a;
        a->bytes = b->bytes;
        b->bytes = tmp.bytes;
        a->entries = b->entries;
        b->entries = tmp.entries;
        for (i = 0; i < WT_SKIP_MAXDEPTH; i++) {
            a->off[i] = b->off[i];
            b->off[i] = tmp.off[i];
            a->sz[i] = b->sz[i];
            b->sz[i] = tmp.sz[i];
        }
    }

    WT_EXT_FOREACH (ext, a->off)
        WT_RET(__wt_extlist_merge(session, verify, b, ext->off, ext->size));

    return (0);
}

/*
 * __wt_extlist_append --
 *     Append a new entry to the allocation list.
 */
int
__wt_extlist_append(
  WT_SESSION_IMPL *session, bool verify, WT_EXTLIST *el, wt_off_t off, wt_off_t size)
{
    WT_EXT **astack[WT_SKIP_MAXDEPTH], *last_ext;
    u_int i;

    WT_UNUSED(verify);
    WT_ASSERT(session, el->track_size == 0);

    /*
     * Identical to __wt_extlist_merge, when we know the file is being extended, that is, the
     * information is either going to be used to extend the last object on the list, or become a new
     * object ending the list.
     *
     * The terminating element of the list is cached, check it; otherwise, get a stack for the last
     * object in the skiplist, check for a simple extension, and otherwise append a new structure.
     */
    if ((last_ext = el->last) != NULL && last_ext->off + last_ext->size == off)
        /* Extend the last object on the list. off is adjacent to the end of the last extent.*/
        last_ext->size += size;
    else {
        /* Update last_ext and, in case appending an extent, determine where to append an extent. */
        last_ext = __wt_extlist_off_srch_last(el->off, astack);
        if (last_ext != NULL && last_ext->off + last_ext->size == off)
            /* Extend the last object on the list. off is adjacent to the end of the last extent.*/
            last_ext->size += size;
        else {
            if (last_ext != NULL)
                /* Assert that this is appending an extent after the last extent. */
                WT_ASSERT(session, last_ext->off + last_ext->size < off);
            WT_RET(__wti_block_ext_alloc(session, &last_ext));
            last_ext->off = off;
            last_ext->size = size;

            for (i = 0; i < last_ext->depth; ++i)
                *astack[i] = last_ext;
            ++el->entries;
        }

        /* Update the cached end-of-list */
        el->last = last_ext;
    }
    el->bytes += (uint64_t)size;

    return (0);
}

/*
 * __wti_extlist_insert_ext --
 *     Insert an extent into an extent list, merging if possible.
 */
int
__wti_extlist_insert_ext(
  WT_SESSION_IMPL *session, WT_BLOCK *block, WT_EXTLIST *el, wt_off_t off, wt_off_t size)
{
    /*
     * There are currently two copies of this function (this code is a one- liner that calls the
     * internal version of the function, which means the compiler should compress out the function
     * call). It's that way because the interface is still fluid, I'm not convinced there won't be a
     * need for a functional split between the internal and external versions in the future.
     *
     * Callers of this function are expected to have already acquired any locks required to
     * manipulate the extent list.
     */
    return (__wt_extlist_merge(session, block->verify, el, off, size));
}

/*
 * __wti_extlist_init --
 *     Initialize an extent list.
 */
int
__wti_extlist_init(
  WT_SESSION_IMPL *session, WT_EXTLIST *el, const char *name, const char *extname, bool track_size)
{
    size_t size;

    WT_CLEAR(*el);

    size =
      (name == NULL ? 0 : strlen(name)) + strlen(".") + (extname == NULL ? 0 : strlen(extname) + 1);
    WT_RET(__wt_calloc_def(session, size, &el->name));
    WT_RET(__wt_snprintf(
      el->name, size, "%s.%s", name == NULL ? "" : name, extname == NULL ? "" : extname));

    el->offset = WT_BLOCK_INVALID_OFFSET;
    el->track_size = track_size;
    return (0);
}
