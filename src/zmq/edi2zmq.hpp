/*
   Copyright (C) 2024
   Matthias P. Braendli, matthias.braendli@mpb.li

   http://opendigitalradio.org

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#pragma once

#include <cstdint>
#include "receiver.h"
#include "zmq/dabOutputZMQ.h"

class ETI2ZMQ {
    public:
        ETI2ZMQ();
        void open(const char *destination);
        bool is_open() const;
        std::string endpoint() const;

        void encode_zmq_frame(eti_frame_t&& eti);

    private:
        uint8_t m_expected_next_fp = 0;

        DabOutputZMQ m_out;
        std::string m_endpoint;
};
