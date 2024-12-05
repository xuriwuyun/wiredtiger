/*-
 * Copyright (c) 2014-present MongoDB, Inc.
 * Copyright (c) 2008-2014 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

#include "wt_internal.h"

/*
 * __extlist_cache_ext_alloc --
 *     Allocate a new WT_EXT structure.
 */
static int
__extlist_cache_ext_alloc(WT_SESSION_IMPL *session, WT_EXT **extp)
{
    WT_EXT *ext;

    size_t skipdepth;

    skipdepth = __wt_skip_choose_depth(session);
    WT_RET(__wt_calloc(session, 1, sizeof(WT_EXT) + skipdepth * 2 * sizeof(WT_EXT *), &ext));
    ext->depth = (uint8_t)skipdepth;
    (*extp) = ext;

    return (0);
}

/*
 * __wti_extlist_cache_ext_alloc --
 *     Return a WT_EXT structure for use.
 */
int
__wti_extlist_cache_ext_alloc(WT_SESSION_IMPL *session, WT_EXT **extp)
{
    WT_EXT *ext;
    WT_EXTLIST_CACHE *extlist_cache;
    u_int i;

    extlist_cache = session->extlist_cache;

    /* Return a WT_EXT structure for use from a cached list. */
    if (extlist_cache != NULL && extlist_cache->ext_cache != NULL) {
        ext = extlist_cache->ext_cache;
        extlist_cache->ext_cache = ext->next[0];

        /* Clear any left-over references. */
        for (i = 0; i < ext->depth; ++i)
            ext->next[i] = ext->next[i + ext->depth] = NULL;

        /*
         * The count is advisory to minimize our exposure to bugs, but don't let it go negative.
         */
        if (extlist_cache->ext_cache_cnt > 0)
            --extlist_cache->ext_cache_cnt;

        *extp = ext;
        return (0);
    }

    return (__extlist_cache_ext_alloc(session, extp));
}

/*
 * __extlist_cache_ext_prealloc --
 *     Pre-allocate WT_EXT structures.
 */
static int
__extlist_cache_ext_prealloc(WT_SESSION_IMPL *session, u_int max)
{
    WT_EXT *ext;
    WT_EXTLIST_CACHE *extlist_cache;

    extlist_cache = session->extlist_cache;

    for (; extlist_cache->ext_cache_cnt < max; ++extlist_cache->ext_cache_cnt) {
        WT_RET(__extlist_cache_ext_alloc(session, &ext));

        ext->next[0] = extlist_cache->ext_cache;
        extlist_cache->ext_cache = ext;
    }
    return (0);
}

/*
 * __wt_extlist_cache_ext_free --
 *     Add a WT_EXT structure to the cached list.
 */
void
__wt_extlist_cache_ext_free(WT_SESSION_IMPL *session, WT_EXT **ext)
{
    WT_EXTLIST_CACHE *extlist_cache;

    if ((extlist_cache = session->extlist_cache) == NULL)
        __wt_free(session, *ext);
    else {
        (*ext)->next[0] = extlist_cache->ext_cache;
        extlist_cache->ext_cache = *ext;

        ++extlist_cache->ext_cache_cnt;
    }
}

/*
 * __extlist_cache_ext_discard --
 *     Discard some or all of the WT_EXT structure cache.
 */
static int
__extlist_cache_ext_discard(WT_SESSION_IMPL *session, u_int max)
{
    WT_EXT *ext, *next;
    WT_EXTLIST_CACHE *extlist_cache;

    extlist_cache = session->extlist_cache;
    if (max != 0 && extlist_cache->ext_cache_cnt <= max)
        return (0);

    for (ext = extlist_cache->ext_cache; ext != NULL;) {
        next = ext->next[0];
        __wt_free(session, ext);
        ext = next;

        --extlist_cache->ext_cache_cnt;
        if (max != 0 && extlist_cache->ext_cache_cnt <= max)
            break;
    }
    extlist_cache->ext_cache = ext;

    if (max == 0 && extlist_cache->ext_cache_cnt != 0)
        WT_RET_MSG(session, WT_ERROR, "incorrect count in session handle's block manager cache");
    return (0);
}

/*
 * __extlist_cache_size_alloc --
 *     Allocate a new WT_SIZE structure.
 */
static int
__extlist_cache_size_alloc(WT_SESSION_IMPL *session, WT_SIZE **szp)
{
    return (__wt_calloc_one(session, szp));
}

/*
 * __wti_extlist_cache_size_alloc --
 *     Return a WT_SIZE structure for use.
 */
int
__wti_extlist_cache_size_alloc(WT_SESSION_IMPL *session, WT_SIZE **szp)
{
    WT_EXTLIST_CACHE *extlist_cache;

    extlist_cache = session->extlist_cache;

    /* Return a WT_SIZE structure for use from a cached list. */
    if (extlist_cache != NULL && extlist_cache->sz_cache != NULL) {
        (*szp) = extlist_cache->sz_cache;
        extlist_cache->sz_cache = extlist_cache->sz_cache->next[0];

        /*
         * The count is advisory to minimize our exposure to bugs, but don't let it go negative.
         */
        if (extlist_cache->sz_cache_cnt > 0)
            --extlist_cache->sz_cache_cnt;
        return (0);
    }

    return (__extlist_cache_size_alloc(session, szp));
}

/*
 * __extlist_cache_size_prealloc --
 *     Pre-allocate WT_SIZE structures.
 */
static int
__extlist_cache_size_prealloc(WT_SESSION_IMPL *session, u_int max)
{
    WT_EXTLIST_CACHE *extlist_cache;
    WT_SIZE *sz;

    extlist_cache = session->extlist_cache;

    for (; extlist_cache->sz_cache_cnt < max; ++extlist_cache->sz_cache_cnt) {
        WT_RET(__extlist_cache_size_alloc(session, &sz));

        sz->next[0] = extlist_cache->sz_cache;
        extlist_cache->sz_cache = sz;
    }
    return (0);
}

/*
 * __wti_extlist_cache_size_free --
 *     Add a WT_SIZE structure to the cached list.
 */
void
__wti_extlist_cache_size_free(WT_SESSION_IMPL *session, WT_SIZE **sz)
{
    WT_EXTLIST_CACHE *extlist_cache;

    if ((extlist_cache = session->extlist_cache) == NULL)
        __wt_free(session, *sz);
    else {
        (*sz)->next[0] = extlist_cache->sz_cache;
        extlist_cache->sz_cache = *sz;

        ++extlist_cache->sz_cache_cnt;
    }
}

/*
 * __extlist_cache_size_discard --
 *     Discard some or all of the WT_SIZE structure cache.
 */
static int
__extlist_cache_size_discard(WT_SESSION_IMPL *session, u_int max)
{
    WT_EXTLIST_CACHE *extlist_cache;
    WT_SIZE *nsz, *sz;

    extlist_cache = session->extlist_cache;
    if (max != 0 && extlist_cache->sz_cache_cnt <= max)
        return (0);

    for (sz = extlist_cache->sz_cache; sz != NULL;) {
        nsz = sz->next[0];
        __wt_free(session, sz);
        sz = nsz;

        --extlist_cache->sz_cache_cnt;
        if (max != 0 && extlist_cache->sz_cache_cnt <= max)
            break;
    }
    extlist_cache->sz_cache = sz;

    if (max == 0 && extlist_cache->sz_cache_cnt != 0)
        WT_RET_MSG(session, WT_ERROR, "incorrect count in session handle's block manager cache");
    return (0);
}

/*
 * __extlist_cache_manager_session_cleanup --
 *     Clean up the session handle's block manager information.
 */
static int
__extlist_cache_manager_session_cleanup(WT_SESSION_IMPL *session)
{
    WT_DECL_RET;

    if (session->extlist_cache == NULL)
        return (0);

    WT_TRET(__extlist_cache_ext_discard(session, 0));
    WT_TRET(__extlist_cache_size_discard(session, 0));

    __wt_free(session, session->extlist_cache);

    return (ret);
}

/*
 * __wt_extlist_cache_ext_prealloc --
 *     Pre-allocate WT_EXT and WT_SIZE structures.
 */
int
__wt_extlist_cache_ext_prealloc(WT_SESSION_IMPL *session, u_int max)
{
    if (session->extlist_cache == NULL) {
        WT_RET(__wt_calloc(session, 1, sizeof(WT_EXTLIST_CACHE), &session->extlist_cache));
        session->extlist_cache_cleanup = __extlist_cache_manager_session_cleanup;
    }
    WT_RET(__extlist_cache_ext_prealloc(session, max));
    WT_RET(__extlist_cache_size_prealloc(session, max));
    return (0);
}

/*
 * __wt_extlist_cache_ext_discard --
 *     Discard WT_EXT and WT_SIZE structures after checkpoint runs.
 */
int
__wt_extlist_cache_ext_discard(WT_SESSION_IMPL *session, u_int max)
{
    WT_RET(__extlist_cache_ext_discard(session, max));
    WT_RET(__extlist_cache_size_discard(session, max));
    return (0);
}

#ifdef HAVE_UNITTEST
int
__ut_exlist_cache_ext_alloc(WT_SESSION_IMPL *session, WT_EXT **extp)
{
    return (__extlist_cache_ext_alloc(session, extp));
}

int
__ut_exlist_cache_ext_prealloc(WT_SESSION_IMPL *session, u_int max)
{
    return (__extlist_cache_ext_prealloc(session, max));
}

int
__ut_extlist_cache_size_alloc(WT_SESSION_IMPL *session, WT_SIZE **szp)
{
    return (__extlist_cache_size_alloc(session, szp));
}

int
__ut_extlist_cache_size_prealloc(WT_SESSION_IMPL *session, u_int max)
{
    return (__extlist_cache_size_prealloc(session, max));
}

int
__ut_extlist_cache_manager_session_cleanup(WT_SESSION_IMPL *session)
{
    return (__extlist_cache_manager_session_cleanup(session));
}

int
__ut_extlist_cache_ext_discard(WT_SESSION_IMPL *session, u_int max)
{
    return (__extlist_cache_ext_discard(session, max));
}

int
__ut_extlist_cache_size_discard(WT_SESSION_IMPL *session, u_int max)
{
    return (__extlist_cache_size_discard(session, max));
}
#endif
