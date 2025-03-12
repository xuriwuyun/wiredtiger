#!/usr/bin/env python
#
# Public Domain 2014-present MongoDB, Inc.
# Public Domain 2008-2014 WiredTiger, Inc.
#
# This is free and unencumbered software released into the public domain.
#
# Anyone is free to copy, modify, publish, use, compile, sell, or
# distribute this software, either in source code form or as a compiled
# binary, for any purpose, commercial or non-commercial, and by any
# means.
#
# In jurisdictions that recognize copyright laws, the author or authors
# of this software dedicate any and all copyright interest in the
# software to the public domain. We make this dedication for the benefit
# of the public at large and to the detriment of our heirs and
# successors. We intend this dedication to be an overt act of
# relinquishment in perpetuity of all present and future rights to this
# software under copyright law.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
# OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
# ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
# OTHER DEALINGS IN THE SOFTWARE.
#
# [TEST_TAGS]
# ignored_file
# [END_TAGS]

# hook_timestamp.py
#
# Insert the use of timestamps into data sets.
#
# These hooks have three functions.  The primary one is setting up the platform API to return
# a "timestamper".  The dataset package uses this platform API, and so will run with timestamps
# when this hook is enabled.  The timestamper provides a timestamping cursor that "knows" to wrap
# timestamped transactions around certain operations, like insert.
#
# Secondly, we set hooks on the transaction APIs so we know when a transaction has been started or
# finished by the test application. If the application already a transaction in progress,
# the timestamping cursor should not try to open a transaction, but can place a timestamp on the
# current transaction.
#
# To run, for example, the cursor tests with these hooks enabled:
#     ../test/suite/run.py --hook timestamp cursor
#
from __future__ import print_function

import sys, unittest, wthooks, wttest, wttimestamp
from wiredtiger import stat
import wiredtiger
import time

# These are the hook functions that are run when particular APIs are called.

def wiredtiger_open_args(ignored_self, args):
    args = list(args)       # convert from a readonly tuple to a writeable list

    import os
    # check if LR_DEST exists
    if not os.path.exists(args[0] + '/LR_DEST'):
        os.makedirs(args[0] + '/LR_DEST')

    args[0] += '/LR_DEST'   # modify the home dir
    return args


def session_wiredtiger_open_replace(orig_wiredtiger_open, session_self, config):

    # If a LR_SOURCE directory exists we can start in live restore mode

    # We need stats enabled so we can wait for live_restore to complete on connection_close
    if "statistics=" not in config:
        config += ",statistics=(fast)"

    ret = orig_wiredtiger_open(session_self, config)
    return ret

def session_connection_close_replace(orig_connection_close, conn_self, config):

    session = conn_self.open_session()
    stat_cursor = session.open_cursor("statistics:")

    state = 0
    timeout = 120
    iteration_count = 0

    # # TODO - once wiredtiger_open is setup correctly this should always be true
    # if stat_cursor[stat.conn.live_restore_state][2] != wiredtiger.WT_LIVE_RESTORE_INIT:

    #     while (iteration_count < timeout):

    #         state = stat_cursor[stat.conn.live_restore_state][2]
    #         # conn_self.prout(f'Looping until finish, live restore state is: {state}, \
    #         #             Current iteration: is {iteration_count}')
    #         if (state == wiredtiger.WT_LIVE_RESTORE_COMPLETE):
    #             break

    #         time.sleep(1)
    #         iteration_count += 1

    #     if(state != wiredtiger.WT_LIVE_RESTORE_COMPLETE):
    #         print("jasdjnadsnjkasjdnkdjnaks")
    #         exit(1)
    #     # conn_self.assertEqual(state, wiredtiger.WT_LIVE_RESTORE_COMPLETE)

    stat_cursor.close()
    session.close()

    ret = orig_connection_close(conn_self, config)


    return ret

# Every hook file must have one or more classes descended from WiredTigerHook
# This is where the hook functions are 'hooked' to API methods.
class LiveRestoreHookCreator(wthooks.WiredTigerHookCreator):
    def __init__(self, arg=0):
        # Caller can specify an optional command-line argument.  We're not using it
        # now, but this is where it would show up.

        # Override some platform APIs
        self.platform_api = wthooks.DefaultPlatformAPI()

    # Determine whether a test should be skipped, if it should also return the reason for skipping.
    # The timestamp hook skips tests that don't use datasets.
    def should_skip(self, test) -> (bool, str):
        skip_categories = [
            ("backup",               "We're already backing up."),
            ("chunkcache",           "TODO - chunkcache tests all used tiered. Determine why."),
            ("live_restore",         "We're already testing live_restore."),
            ("inmem",                "Can't live restore in-memory databases."),
            ("schema",               "TODO - apparently these are all tiered tests?"),
            ("stat",                 "TODO - Turn off for now. Issues with stat=none"),
            ("tiered",               "Can't live restore tiered databases."),
        ]

        for (skip_string, skip_reason) in skip_categories:
            if skip_string in str(test):
                return (True, skip_reason)

        return (False, None)

    # Skip tests that won't work on timestamp cursors
    def register_skipped_tests(self, tests):
        for t in tests:
            (should_skip, skip_reason) = self.should_skip(t)
            if should_skip:
                wttest.register_skipped_test(t, "live_restore", skip_reason)

    def get_platform_api(self):
        return self.platform_api

    def setup_hooks(self):
        pass

        # self.wiredtiger['wiredtiger_open'] = (wthooks.HOOK_ARGS, wiredtiger_open_args)

        orig_wiredtiger_open = self.wiredtiger['wiredtiger_open']
        self.wiredtiger['wiredtiger_open'] =  (wthooks.HOOK_REPLACE, lambda s, config=None:
          session_wiredtiger_open_replace(orig_wiredtiger_open, s, config))

        orig_connection_close = self.Connection['close']
        self.Connection['close'] =  (wthooks.HOOK_REPLACE, lambda s, config=None:
          session_connection_close_replace(orig_connection_close, s, config))


# Every hook file must have a top level initialize function,
# returning a list of WiredTigerHook objects.
def initialize(arg):
    return [LiveRestoreHookCreator(arg)]
