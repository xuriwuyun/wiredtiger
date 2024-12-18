/*-
 * Copyright (c) 2014-present MongoDB, Inc.
 * Copyright (c) 2008-2014 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

/*
 * Tests of the Live Restore extent lists. Extent lists track "holes" in a file representing ranges
 * of data that still need to be copied from the source directory into the destination directory.
 * [live_restore_extent_list]
 */

#include "../utils_live_restore.h"

using namespace utils;

TEST_CASE("Live Restore Directory List", "[live_restore],[live_restore_directory_list]")
{
    /*
     * Note: this code runs once per SECTION so we're creating a brand new WT database for each
     * section. If this gets slow we can make it static and manually clear the destination and
     * source for each section.
     */
    live_restore_test_env env;

    WT_SESSION_IMPL *session = env.session;
    WT_SESSION *wt_session = reinterpret_cast<WT_SESSION *>(session);
    WT_LIVE_RESTORE_FS *lr_fs = env.lr_fs;

    SECTION("List files in directory - Only files in destination")
    {
        // Create some files in the destination directory.
        create_file(env.dest_file_path("file1.txt").c_str(), 1000);
        create_file(env.dest_file_path("file2.txt").c_str(), 1000);

        // List files in the destination directory.
        char **dirlist = NULL;
        uint32_t count;
        const char * prefix = NULL;
        lr_fs->iface.fs_directory_list((WT_FILE_SYSTEM *)lr_fs, wt_session, env.DB_DEST.c_str(), prefix, &dirlist, &count);

    }
}
