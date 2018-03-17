/**
 * Copyright (c) 2015 LG Electronics, Inc.
 *
 * Unless otherwise specified or set forth in the NOTICE file, all content,
 * including all source code files and documentation files in this repository are:
 * Confidential computer software. Valid license from LG required for
 * possession, use or copying.
 */

#include "parser.h"
#include "string.h"

#define LOGTAG "parser"
#include "debug.h"

using namespace NDL_Esplayer;

#define LOG_PARSER             NDL_LOGV
#define LOG_PARSERV            NDL_LOGV

parser::parser(int frame_buffer_size)
    : frame_buffer_(new unsigned char[frame_buffer_size])
      , frame_buffer_size_(frame_buffer_size)
{
}

parser::~parser() {
    delete [] frame_buffer_;
}

void parser::clear() {
    frame_buffer_filled_  = 0;
    frame_buffer_required_ = 0;
}

int parser::parse(const unsigned char* data, unsigned int len,
        std::function<int(const unsigned char*, unsigned int)> writer)
{
    const unsigned char* pos = data;
    int remained = len;

    if (len == 0) // handle EOS frame
        return writer(data, len);

    if (frame_buffer_filled_ == 0) {
        //
        // We should get here mostly
        //  because all frames is expected to be aligned by frame boundary
        //

        // seek to frame sync bytes.
        int offset = getFrameOffset(pos, remained);
        // discard data because no sync bytes exist
        if (offset < 0) {
            NDLLOG(LOGTAG, NDL_LOGE, "%s : error in finding frame start codes", __func__);
            return len;
        }

        // discard unexpected data until sync bytes
        pos += offset;
        remained -= offset;

        int size = getFrameSize(pos, remained);

        // discard data if frame size parsing is failed
        if (size < 0) {
            NDLLOG(LOGTAG, NDL_LOGE, "%s : error in parsing frame size", __func__);
            return len;
        }

        // handle frame alignment
        if (size <= remained) {
            // write a complete frame
            int written = writer(pos, size);
            if (written < 0) {
                NDLLOG(LOGTAG, NDL_LOGE, "%s : buffer is full, offset:%d, size:%d", __func__, offset, size);
                return -1;
            } else if (written == 0) {
                NDLLOG(LOGTAG, NDL_LOGE, "%s : error in writing frame data, offset:%d, size:%d", __func__, offset, size);
                return len;
            }

            pos += written;
            remained -= written;
        } else {
            // discard data if frame size is bigger than frame buffer size
            if (size > frame_buffer_size_) {
                NDLLOG(LOGTAG, NDL_LOGE, "%s : error in get frame size. frame size : %d, buffer size : %d",
                        __func__, size, frame_buffer_size_);
                return len;
            }

            // partial frame
            //  just keep it, and handle later
            memcpy(frame_buffer_, pos, remained);
            pos += remained;
            frame_buffer_filled_ = remained;
            frame_buffer_required_ = size - remained;

            NDLLOG(LOGTAG, LOG_PARSER, "%s : keep mis-aligned frame data, filled:%d, required %d bytes more",
                    __func__, frame_buffer_filled_, frame_buffer_required_ );
        }
    } else {

        //
        // at this point, we have some fragmented frame data in frame_buffer_
        //

        int copy_to_frame_buffer = (remained < frame_buffer_required_) ? remained : frame_buffer_required_;
        memcpy(frame_buffer_+frame_buffer_filled_, pos, copy_to_frame_buffer);

        if (copy_to_frame_buffer < frame_buffer_required_) { // need more data
            pos += copy_to_frame_buffer;
            remained -= copy_to_frame_buffer;
            frame_buffer_required_ -= copy_to_frame_buffer;
            frame_buffer_filled_ += copy_to_frame_buffer;
        } else { // a complete frame has been built
            int written = writer(frame_buffer_, frame_buffer_filled_ + copy_to_frame_buffer);
            if (written < 0) { // buffer full
                // will try again in the next parse() call (no member was updated here)
                NDLLOG(LOGTAG, NDL_LOGE, "%s : buffer is full", __func__);
                return -1;
            } else if (written == 0) { // failed, discard all data.
                NDLLOG(LOGTAG, NDL_LOGE, "%s : error in writing frame merged, size:%d", __func__, frame_buffer_filled_);
                frame_buffer_required_ = 0;
                frame_buffer_filled_ = 0;
                return len;
            } else {
                NDLLOG(LOGTAG, LOG_PARSER, "%s : write merged frame, size:%d", __func__, written);
                pos += copy_to_frame_buffer;
                remained -= copy_to_frame_buffer;
                frame_buffer_required_ = 0;
                frame_buffer_filled_ = 0;
            }
        }
    }
    NDLLOG(LOGTAG, LOG_PARSERV, "%s : parsed %d bytes, remained : %d bytes", __func__, pos-data, remained);
    return pos-data;
}
