/*
 * Copyright (c) 2008-2018 LG Electronics, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0



 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.

 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <unistd.h>

#include <thread>

#include "message.h"


#define LOGTAG "test "
#define LOG_VERBOSE 1
#include "debug.h"

#define LOG_TEST  NDL_LOGI

using namespace NDL_Esplayer;


MessageLooper messagelooper;

void postMessage(int64_t delay, const char* desc) {

    int64_t cur = current_time_ns();
    int64_t run_at = cur + delay;
    NDLLOG(LOGTAG, LOG_TEST,
            "current : %10lld post at %10lld with delay %10s [%10lld]"
            , cur
            , run_at
            , desc
            , delay
          );

    auto msg = std::make_shared<Message>([=] {
            int64_t cur = current_time_ns();
            int64_t error = cur - run_at;

            NDLLOG(LOGTAG, LOG_TEST,
                "current : %10lld run  at %10lld with delay %10s [%10lld] error = %10lld"
                , cur
                , run_at
                , desc
                , delay
                , error
                );
            return 0;
            }, delay);

    messagelooper.post(msg);
}

int main(int argc, const char* argv[])
{
    const int64_t ns = 1;
    const int64_t us = 1000 * ns;
    const int64_t ms = 1000 * us;
    const int64_t s  = 1000 * ms;

#define TEST(delay) postMessage(delay, #delay)

    TEST(5   *  s);
    TEST(700 * ms);
    TEST(20  * ms);
    TEST(30  * ms);
    TEST(1   * ms);
    TEST(100 * ms);
    TEST(60  * ms);
    TEST(50  * ms);
    TEST(80  * ms);
    TEST(70  * ms);
    TEST(400 * ms);
    TEST(30  * us);
    TEST(100 * us);
    TEST(200 * us);
    TEST(1000* ns);
    TEST(10  * ns);
    TEST(0);
    TEST(0);

    sleep(9);

    return 0;
}




