/**
 * Copyright (c) 2015 LG Electronics, Inc.
 *
 * Unless otherwise specified or set forth in the NOTICE file, all content,
 * including all source code files and documentation files in this repository are:
 * Confidential computer software. Valid license from LG required for
 * possession, use or copying.
 */
/**
 * file ac3parser.h
 *
 * author Kanghwan Jang (kanghwan.jang@lge.com)
 * date 2015.05.26
 */

#ifndef PARSER_PARSER_H_
#define PARSER_PARSER_H_

#include <functional>

namespace NDL_Esplayer {

    class parser {
        public:
            parser(int frame_buffer_size);
            virtual ~parser();

            void clear();
            int parse(const unsigned char* data, unsigned int len,
                    std::function<int(const unsigned char*, unsigned int)> writer);

        protected:
            virtual int getFrameOffset(const unsigned char* data, unsigned int len) = 0;
            virtual int getFrameSize(const unsigned char* data, unsigned int len) = 0;

        private:
            unsigned char* frame_buffer_ = nullptr;
            int frame_buffer_size_ {0};

            int frame_buffer_filled_ {0};   // number of bytes written to frame_buffer_
            int frame_buffer_required_ {0}; // number of bytes required to complete a frame
    };


} //namespace NDL_Esplayer {

#endif // #ifndef PARSER_PARSER_H_
