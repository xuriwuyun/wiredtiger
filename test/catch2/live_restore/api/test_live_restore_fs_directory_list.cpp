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
#include <set>

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

    SECTION("Directory list - Files only in destination")
    {
        std::string file_1 = "file1.txt";
        std::string file_2 = "file2.txt";

        // Create some files in the destination directory.
        create_file(env.dest_file_path(file_1).c_str(), 1000);
        create_file(env.dest_file_path(file_2).c_str(), 1000);

        std::set<std::string> expected_files{file_1, file_2};

        // List files in the destination directory.
        char **dirlist = NULL;
        uint32_t count;
        const char *prefix = NULL;

        // Two files in dest
        lr_fs->iface.fs_directory_list(
          (WT_FILE_SYSTEM *)lr_fs, wt_session, env.DB_DEST.c_str(), prefix, &dirlist, &count);
        REQUIRE(count == 2);
        std::set<std::string> found_files{};
        for (int i = 0; i < count; i++) {
            std::string found_file(dirlist[i]);
            found_files.insert(found_file);
        }
        REQUIRE(found_files == expected_files);
        lr_fs->iface.fs_directory_list_free((WT_FILE_SYSTEM *)lr_fs, wt_session, dirlist, count);
    }
}
