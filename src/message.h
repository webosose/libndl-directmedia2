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

#ifndef NDL_DIRECTMEDIA2_MESSAGE_H_
#define NDL_DIRECTMEDIA2_MESSAGE_H_

#include <assert.h>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <list>
#include <time.h>
#include <thread>
#include <functional>

#define MAX_THREAD_NAME_LEN 16
#define MSG_THRESHOLD_SIZE 50

namespace NDL_Esplayer {


    inline int64_t current_time_ns() {
        timespec t;
        if(clock_gettime(CLOCK_MONOTONIC, &t) == 0) {
            int64_t ns = t.tv_sec * 1000000000LL + t.tv_nsec;
            return ns;
        }
        return -1;
    }

    using MessageHandler = std::function<int()>;

    class Message {
        public:
            Message() {
            }

            explicit Message(MessageHandler handler, int64_t delay = 0, int64_t timestamp = 0)
                : handler_(handler),
                timestamp_ (timestamp){
                    setDelay(delay);
                }

            explicit Message(MessageHandler handler, MessageHandler canceller, int64_t delay = 0, int64_t timestamp = 0)
                : handler_(handler),
                canceller_ (canceller),
                timestamp_ (timestamp){
                    setDelay(delay);
                }

            int64_t getRunAt() const {
                return run_at_;
            }

            void setRunAt(int64_t run_at) {
                run_at_ = run_at;
            }

            //TODO : It is needed re-factoring with timestamp_ variable.
            int64_t getTimestamp() const {
                return timestamp_;
            }

            void setHandler(MessageHandler handler) {
                handler_ = handler;
            }

            void setDelay(int64_t delay_ns) {
                run_at_ = current_time_ns() + delay_ns;
            }

            void addDelay(int64_t time_ns) {
                run_at_ += time_ns;
            }

            int handle() {
                if(handler_)
                    return handler_();
                return 0;
            }

            void cancel() {
                if(canceller_)
                    canceller_();
            }
        private:
            int64_t run_at_ {0};//nano-seconds
            MessageHandler handler_ {nullptr};
            MessageHandler canceller_ {nullptr};
            //TODO : It is needed re-factoring. It is added for scheduling the renderer.
            //  We did not consider scheduling the renderer when we design Message & MessageLooper.
            // Currently, we are using Message & MessageLooper for scheduling, too.
            // For scheduling the renderer, it is needed to consider timestamp rather than run_at_
            //  in the case of Step-IFrame function.
            // We need to re-factoring it and esplayer render scheduling part, too.
            //  But not now, so I just added it.
            int64_t timestamp_ {0};
    };

    class MessageLooper {
        public:
            MessageLooper();
            ~MessageLooper();
            void setName(const char* name); // max 16 characters, including the terminating null byte.
            void post(const std::shared_ptr<Message>& message);
            void append(const std::shared_ptr<Message>& message);
            void setRunningState(bool run);
            void reschedule(); // only for rescheduling render message
            void clearAll();
            void cancelAll();
            int size();
        private:
            void* loop();
            void postQuit();

        private:
            bool running_ {true};
            int64_t paused_time_ {0};
            const char thread_name[MAX_THREAD_NAME_LEN] {"libndl_looper"};

            std::mutex message_queue_lock_;
            std::condition_variable message_queue_changed_cond_;
            std::condition_variable message_state_changed_cond_;
            std::list<std::shared_ptr<Message>>  message_queue_;
            std::thread message_handler_thread_;

            MessageLooper(MessageLooper const&) = delete;
            void operator=(MessageLooper const&) = delete;
    };

} //namespace lwm_omx_player

#endif //#ifndef NDL_DIRECTMEDIA2_MESSAGE_H_
