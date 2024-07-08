/*
   Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009 Her Majesty the Queen in
   Right of Canada (Communications Research Center Canada)

   Copyright (C) 2024
   Matthias P. Braendli, matthias.braendli@mpb.li

    http://www.opendigitalradio.org

   An object-oriented version of the output channels.
*/
/*
   This file is part of the ODR-mmbTools.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.
   */

#pragma once

#include <stdexcept>
#include <cstring>
#include <vector>
#include <chrono>
#include <memory>

#include "zmq/zmq.hpp"
#include "zmq/metadata.h"

// Abstract base class for all outputs
class DabOutput
{
    public:
        virtual int Open(const char* name) = 0;
        int Open(std::string name)
        {
            return Open(name.c_str());
        }

        // Return -1 on failure
        virtual int Write(void* buffer, int size) = 0;
        virtual int Close() = 0;

        virtual ~DabOutput() {}

        virtual std::string get_info() const = 0;

        virtual void setMetadata(std::shared_ptr<OutputMetadata> &md) = 0;
};

// ----- used in File and Fifo outputs
enum EtiFileType {
    ETI_FILE_TYPE_NONE = 0,
    ETI_FILE_TYPE_RAW,
    ETI_FILE_TYPE_STREAMED,
    ETI_FILE_TYPE_FRAMED
};

#define NUM_FRAMES_PER_ZMQ_MESSAGE 4
/* A concatenation of four ETI frames,
 * whose maximal size is 6144.
 *
 * If we transmit four frames in one zmq message,
 * we do not risk breaking ETI vs. transmission frame
 * phase.
 *
 * The frames are concatenated in buf, and
 * their sizes is given in the buflen array.
 *
 * Most of the time, the buf will not be completely
 * filled
 */
struct zmq_dab_message_t
{
    zmq_dab_message_t() : buf()
    {
        /* set buf lengths to invalid */
        buflen[0] = -1;
        buflen[1] = -1;
        buflen[2] = -1;
        buflen[3] = -1;

        version = 1;
    }
    uint32_t version;
    int16_t buflen[NUM_FRAMES_PER_ZMQ_MESSAGE];
    /* The head stops here. Use the macro below to calculate
     * the head size.
     */

    uint8_t  buf[NUM_FRAMES_PER_ZMQ_MESSAGE*6144];

    /* The packet is then followed with metadata appended to it,
     * according to dabOutput/metadata.h
     */
};

#define ZMQ_DAB_MESSAGE_HEAD_LENGTH (4 + NUM_FRAMES_PER_ZMQ_MESSAGE*2)

// -------------- ZeroMQ message queue ------------------
class DabOutputZMQ : public DabOutput
{
    public:
        DabOutputZMQ(const std::string &zmq_proto, bool allow_metadata) :
            endpoint_(""),
            zmq_proto_(zmq_proto), zmq_context_(1),
            zmq_pub_sock_(zmq_context_, ZMQ_PUB),
            zmq_message_ix(0),
            m_allow_metadata(allow_metadata)
        { }

        DabOutputZMQ(const DabOutputZMQ& other) = delete;
        DabOutputZMQ& operator=(const DabOutputZMQ& other) = delete;

        virtual ~DabOutputZMQ()
        {
            zmq_pub_sock_.close();
        }

        std::string get_info(void) const {
            return "zmq: " + zmq_proto_ + "://" + endpoint_;
        }

        int Open(const char* endpoint);
        int Write(void* buffer, int size);
        int Close();
        void setMetadata(std::shared_ptr<OutputMetadata> &md);

    private:
        std::string endpoint_;
        std::string zmq_proto_;
        zmq::context_t zmq_context_; // handle for the zmq context
        zmq::socket_t zmq_pub_sock_; // handle for the zmq publisher socket

        zmq_dab_message_t zmq_message;
        int zmq_message_ix;

        bool m_allow_metadata;
        std::vector<std::shared_ptr<OutputMetadata> > meta_;
};

