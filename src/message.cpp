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

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/select.h>

#include "message.h"
#include "ndl-directmedia2/media-common.h"

#define LOGTAG "msg  "
#include "debug.h"

#define LOG_MSG         NDL_LOGD
#define LOG_MSG_LOCK    NDL_LOGV

#define MSG_RETRY
#ifdef MSG_RETRY
#define RETRY_GAP_TIME 100000 //100ms
#endif

using namespace NDL_Esplayer;


MessageLooper::MessageLooper()
    : message_handler_thread_(&MessageLooper::loop, this)
{
}

MessageLooper::~MessageLooper()
{
    if(message_handler_thread_.joinable()) {
        postQuit();

        NDLLOG(LOGTAG, LOG_MSG_LOCK, "[%10lld] message_handler_thread_.join +", current_time_ns());
        message_handler_thread_.join();
        NDLLOG(LOGTAG, LOG_MSG_LOCK, "[%10lld] message_handler_thread_.join -", current_time_ns());
    }
}

//thread name length is restricted under MAX_THREAD_NAME_LEN
void MessageLooper::setName(const char* name)
{
    if( name && strlen(name) < MAX_THREAD_NAME_LEN){
        pthread_setname_np(message_handler_thread_.native_handle(), name);
        strncpy((char*)thread_name, name, MAX_THREAD_NAME_LEN);
    }
    else if(name)
        NDLLOG(LOGTAG, NDL_LOGE, "name:%s, wrong size:%d < %d", name, strlen(name), MAX_THREAD_NAME_LEN);
    else
        NDLLOG(LOGTAG, NDL_LOGE, "null name");
}

// This function is only for re-scheduling render message
void MessageLooper::reschedule() {
    NDLLOG(LOGTAG, LOG_MSG_LOCK, "[%10lld] message queue locking for adjust delay", current_time_ns());
    std::unique_lock<std::mutex> lock(message_queue_lock_);
    NDLLOG(LOGTAG, LOG_MSG_LOCK, "[%10lld] message queue locked for adjust delay, queue size = %d",
            current_time_ns(), message_queue_.size());

    if(message_queue_.empty()) {
        paused_time_ = 0;
        return;
    }
    if (!running_ && paused_time_ != 0) {
        paused_time_ = current_time_ns() - paused_time_;

        auto i = message_queue_.begin();
        // reschedule first message with paused time
        (*i)->addDelay(paused_time_); // add paused_time_ in first render message

        // get base timestamp and run at
        int64_t base_ts = (*i)->getTimestamp(); // to reschedule with timestamp
        int64_t base_run_at = (*i)->getRunAt(); // base run_at

        ++i;
        while(i!=message_queue_.end()) {
            int64_t timestamp = (*i)->getTimestamp();
            // if timestamp is exist, reschedule with timestamp
            if(timestamp > 0)
                (*i)->setRunAt(base_run_at + (timestamp - base_ts) * 1000);
            // if there is no timestamp, reschedule with paused time
            else
                (*i)->addDelay(paused_time_);
            ++i;
        }
        paused_time_ = 0;
    }
    NDLLOG(LOGTAG, LOG_MSG_LOCK, "[%10lld] message queue unlock for adjust delay, queue size = %d",
            current_time_ns(), message_queue_.size());
}

void MessageLooper::setRunningState(bool run) {
    {
        NDLLOG(LOGTAG, LOG_MSG_LOCK, "[%10lld] message queue locking for set running state", current_time_ns());
        std::unique_lock<std::mutex> lock(message_queue_lock_);
        NDLLOG(LOGTAG, LOG_MSG_LOCK, "[%10lld] message queue locked for set running state, queue size = %d",
                current_time_ns(), message_queue_.size());
        if (run) {
            paused_time_ = 0;
        }
        else {
            paused_time_ = current_time_ns();
        }
        running_ = run;
        NDLLOG(LOGTAG, LOG_MSG_LOCK, "[%10lld] message queue unlock for set running state, queue size = %d",
                current_time_ns(), message_queue_.size());
    }
    message_state_changed_cond_.notify_one();
}

void* MessageLooper::loop()
{
    int ret = 0;
    while(1) {
        std::shared_ptr<Message> message;

        {
            NDLLOG(LOGTAG, LOG_MSG_LOCK, "[%10lld] loop locking", current_time_ns());
            std::unique_lock<std::mutex> lock(message_queue_lock_);
            NDLLOG(LOGTAG, LOG_MSG_LOCK, "[%10lld] loop locked, queue size = %d", current_time_ns(), message_queue_.size());

            if (!running_) {
                NDLLOG(LOGTAG, LOG_MSG_LOCK, "messge looper wait to set run .... %d ", running_);
                message_state_changed_cond_.wait(lock);
                NDLLOG(LOGTAG, LOG_MSG_LOCK, "messge looper is running now.... %d", running_);
                continue;
            }

            int queue_size = size();
            if( queue_size > MSG_THRESHOLD_SIZE ) {
                NDLLOG(LOGTAG, LOG_MSG_LOCK, "[%s] Warning!! message queue size is over %d. You have to control threshold. size:%d", thread_name, MSG_THRESHOLD_SIZE, queue_size);
                //TODO: We have to maintain inserted item size under MSG_THRESHOLD_SIZE
            }

            if(message_queue_.empty()) {
                NDLLOG(LOGTAG, LOG_MSG_LOCK, "[%10lld] loop wait", current_time_ns());
                message_queue_changed_cond_.wait(lock);
                continue;
            }

            message = message_queue_.front();
            if(!message) {
                //quit message
                NDLLOG(LOGTAG, LOG_MSG, "[%10lld] %s, %s:got a quit message", current_time_ns(), thread_name, __FUNCTION__);
                break;
            }

            int64_t now = current_time_ns();
            int64_t run_at = message->getRunAt();

            if(run_at && run_at > now ) {
                NDLLOG(LOGTAG, LOG_MSG_LOCK, "[%10lld] loop wait_for(%10lld), run_at = %lld", current_time_ns(), run_at - now, run_at);
                message_queue_changed_cond_.wait_for(lock,
                        std::chrono::nanoseconds(run_at - now));
                continue;
            }
#if !defined(MSG_RETRY)  //pop later according to return result
            message_queue_.pop_front();
#endif
            NDLLOG(LOGTAG, LOG_MSG_LOCK, "[%10lld] loop unlock, queue size = %d", current_time_ns(), message_queue_.size());
        }

        NDLLOG(LOGTAG, LOG_MSG_LOCK, "[%s] queue size:%d, calling handler", thread_name, message_queue_.size());
        ret = message->handle();
#ifdef MSG_RETRY
        if( ret == NDL_ESP_RESULT_SUCCESS ) {
            NDLLOG(LOGTAG, LOG_MSG_LOCK, "[%10lld] loop locking", current_time_ns());
            std::unique_lock<std::mutex> lock(message_queue_lock_);
            NDLLOG(LOGTAG, LOG_MSG_LOCK, "[%10lld] loop locked, queue size = %d", current_time_ns(), message_queue_.size());

            // clearAll can clean all message_queue_ before pop_front
            if( !message_queue_.empty())
                message_queue_.pop_front();

            NDLLOG(LOGTAG, LOG_MSG_LOCK, "[%10lld] loop unlock, queue size = %d", current_time_ns(), message_queue_.size());
        } else {
            NDLLOG(LOGTAG, NDL_LOGV, "ret:%d, we got buffer full result. let's wait %dms for emptybufferdone",ret, RETRY_GAP_TIME/1000);
            usleep(RETRY_GAP_TIME);
        }
#endif

    }
    return 0;
}

void MessageLooper::post(const std::shared_ptr<Message>& message)
{
    {
        NDLLOG(LOGTAG, LOG_MSG_LOCK, "[%10lld] push locking", current_time_ns());
        std::lock_guard<std::mutex> lock(message_queue_lock_);
        NDLLOG(LOGTAG, LOG_MSG_LOCK, "[%10lld] push locked", current_time_ns());

        int64_t run_at = 0;
        if(message)
            run_at = message->getRunAt();

        auto i = message_queue_.begin();
        while(i!=message_queue_.end() && (*i)->getRunAt() <= run_at)
            ++i;

        message_queue_.insert(i, message);

        NDLLOG(LOGTAG, LOG_MSG_LOCK, "[%10lld] push unlock, queue size = %d", current_time_ns(), message_queue_.size());
    }
    message_queue_changed_cond_.notify_one();

}

void MessageLooper::append(const std::shared_ptr<Message>& message)
{
    {
        NDLLOG(LOGTAG, LOG_MSG_LOCK, "[%10lld] push locking", current_time_ns());
        std::lock_guard<std::mutex> lock(message_queue_lock_);
        NDLLOG(LOGTAG, LOG_MSG_LOCK, "[%10lld] push locked", current_time_ns());

        message_queue_.push_back(message);

        NDLLOG(LOGTAG, LOG_MSG_LOCK, "[%10lld] push unlock, queue size = %d", current_time_ns(), message_queue_.size());
    }
    message_queue_changed_cond_.notify_one();

}

void MessageLooper::postQuit()
{
    NDLLOG(LOGTAG, LOG_MSG, "post quit message");
    setRunningState(true);
    std::shared_ptr<Message> quit(0);
    post(quit);
}

void MessageLooper::clearAll()
{
    NDLLOG(LOGTAG, LOG_MSG_LOCK, "[%10lld] cancelAll loop locking", current_time_ns());
    std::lock_guard<std::mutex> lock(message_queue_lock_);
    NDLLOG(LOGTAG, LOG_MSG_LOCK, "[%10lld] cancelAll loop locked, queue size = %d",
            current_time_ns(), message_queue_.size());
    NDLLOG(LOGTAG, LOG_MSG, "cancel %d messages", message_queue_.size());
    message_queue_.clear();
    NDLLOG(LOGTAG, LOG_MSG_LOCK, "[%10lld] cancelAll loop unlock, queue size = %d",
            current_time_ns(), message_queue_.size());
}

void MessageLooper::cancelAll()
{
    while(!message_queue_.empty()) {
        std::shared_ptr<Message> message;
        {
            NDLLOG(LOGTAG, LOG_MSG_LOCK, "[%10lld] cancel loop locking", current_time_ns());
            std::unique_lock<std::mutex> lock(message_queue_lock_);
            NDLLOG(LOGTAG, LOG_MSG_LOCK, "[%10lld] cancel loop locked, queue size = %d", current_time_ns(), message_queue_.size());
            message = message_queue_.front();
            if(!message) {
                //quit message
                NDLLOG(LOGTAG, LOG_MSG_LOCK, "[%10lld] %s:got a quit message", current_time_ns(), __FUNCTION__);
                break;
            }
            message_queue_.pop_front();
            NDLLOG(LOGTAG, LOG_MSG_LOCK, "[%10lld] cancel loop unlock, queue size = %d", current_time_ns(), message_queue_.size());
        }
        message->cancel();
    }
}

int MessageLooper::size()
{
    //std::unique_lock<std::mutex> lock(message_queue_lock_);
    int queue_size = message_queue_.size();
    return queue_size;
}
