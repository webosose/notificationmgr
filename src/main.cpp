// Copyright (c) 2013-2018 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <glib.h>
#include <errno.h>

#include "Logging.h"
#include "NotificationService.h"

static GMainLoop * s_main_loop = NULL;

void
term_handler(int signal)
{
    g_main_loop_quit(s_main_loop);
}

int
main(int argc, char **argv)
{
    LOG_DEBUG("entering %s in %s", __func__, __FILE__ );

    signal(SIGTERM, term_handler);

    s_main_loop = g_main_loop_new(NULL, FALSE);

    NotificationService::instance()->attach(s_main_loop);

    g_main_loop_run(s_main_loop);

    NotificationService::instance()->detach();

    g_main_loop_unref(s_main_loop);

    LOG_DEBUG("exiting %s in %s", __func__, __FILE__ );

    return EXIT_SUCCESS;
}
