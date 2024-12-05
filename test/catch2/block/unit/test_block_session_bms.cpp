/*-
 * Copyright (c) 2014-present MongoDB, Inc.
 * Copyright (c) 2008-2014 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

/* FIXME-WT-13797 Filename and comments refer to block manager and block manager session. */
/*
 * [block_session_extlist_cache]: block_session.c
 * The block manager extent list consists of both extent and size type blocks. This unit test
 * suite tests aims to test all of the allocation and frees of combined extent and size functions.
 *
 * The file aims to test the management of the block manager session.
 */
#include "wt_internal.h"
#include <catch2/catch.hpp>
#include "../util_block.h"
#include "../../wrappers/mock_session.h"

TEST_CASE("Block session: __wt_extlist_cache_ext_prealloc with null block manager",
  "[block_session_extlist_cache]")
{
    std::shared_ptr<mock_session> session = mock_session::build_test_mock_session();

    SECTION("Prealloc with null block manager")
    {
        WT_EXTLIST_CACHE *extlist_cache = nullptr;

        __wt_random_init(&session->get_wt_session_impl()->rnd);

        REQUIRE(__wt_extlist_cache_ext_prealloc(session->get_wt_session_impl(), 0) == 0);
        extlist_cache =
          static_cast<WT_EXTLIST_CACHE *>(session->get_wt_session_impl()->extlist_cache);

        REQUIRE(extlist_cache != nullptr);
        __wt_free(nullptr, extlist_cache);

        WT_SESSION_IMPL *session_impl = session->get_wt_session_impl();
        session_impl->extlist_cache = nullptr;
    }
}

TEST_CASE("Block session: __wt_extlist_cache_ext_prealloc", "[block_session_extlist_cache]")
{
    std::shared_ptr<mock_session> session = mock_session::build_test_mock_session();
    WT_EXTLIST_CACHE *extlist_cache = session->setup_block_manager_session();

    SECTION("Prealloc with block manager")
    {
        REQUIRE(__wt_extlist_cache_ext_prealloc(session->get_wt_session_impl(), 2) == 0);
        REQUIRE(session->get_wt_session_impl()->extlist_cache == extlist_cache);
        validate_ext_list(extlist_cache, 2);
        validate_size_list(extlist_cache, 2);
    }

    SECTION("Prealloc with existing cache")
    {
        REQUIRE(__wt_extlist_cache_ext_prealloc(session->get_wt_session_impl(), 2) == 0);
        REQUIRE(session->get_wt_session_impl()->extlist_cache == extlist_cache);
        validate_ext_list(extlist_cache, 2);
        validate_size_list(extlist_cache, 2);

        REQUIRE(__wt_extlist_cache_ext_prealloc(session->get_wt_session_impl(), 5) == 0);
        validate_ext_list(extlist_cache, 5);
        validate_size_list(extlist_cache, 5);
    }
}

TEST_CASE("Block session: __block_manager_session_cleanup", "[block_session_extlist_cache]")
{
    std::shared_ptr<mock_session> session = mock_session::build_test_mock_session();
    WT_EXTLIST_CACHE *extlist_cache = session->setup_block_manager_session();
    WT_SESSION_IMPL *session_impl = session->get_wt_session_impl();

    SECTION("Free with null session block manager ")
    {
        std::shared_ptr<mock_session> session_no_extlist_cache =
          mock_session::build_test_mock_session();
        REQUIRE(__ut_extlist_cache_manager_session_cleanup(
                  session_no_extlist_cache->get_wt_session_impl()) == 0);
        REQUIRE(session_no_extlist_cache->get_wt_session_impl()->extlist_cache == nullptr);
    }

    SECTION("Calling free with session block manager")
    {
        REQUIRE(session_impl->extlist_cache != nullptr);
        REQUIRE(__ut_extlist_cache_manager_session_cleanup(session_impl) == 0);
        REQUIRE(session_impl->extlist_cache == nullptr);
    }

    SECTION("Calling free with session block manager and cache")
    {
        REQUIRE(__wt_extlist_cache_ext_prealloc(session_impl, 2) == 0);
        validate_ext_list(extlist_cache, 2);
        validate_size_list(extlist_cache, 2);

        REQUIRE(session_impl->extlist_cache != nullptr);
        REQUIRE(__ut_extlist_cache_manager_session_cleanup(session_impl) == 0);
        REQUIRE(session_impl->extlist_cache == nullptr);
    }

    SECTION("Calling free with session block manager and fake extent cache")
    {
        REQUIRE(__wt_extlist_cache_ext_prealloc(session_impl, 2) == 0);
        validate_ext_list(extlist_cache, 2);
        validate_size_list(extlist_cache, 2);

        extlist_cache->ext_cache_cnt = 3;

        REQUIRE(session_impl->extlist_cache != nullptr);
        REQUIRE(__ut_extlist_cache_manager_session_cleanup(session_impl) == WT_ERROR);
        REQUIRE(session_impl->extlist_cache == nullptr);
    }

    SECTION("Calling free with session block manager and fake size cache")
    {
        REQUIRE(__wt_extlist_cache_ext_prealloc(session_impl, 2) == 0);
        validate_ext_list(extlist_cache, 2);
        validate_size_list(extlist_cache, 2);

        extlist_cache->sz_cache_cnt = 3;

        REQUIRE(session_impl->extlist_cache != nullptr);
        REQUIRE(__ut_extlist_cache_manager_session_cleanup(session_impl) == WT_ERROR);
        REQUIRE(session_impl->extlist_cache == nullptr);
    }
}
