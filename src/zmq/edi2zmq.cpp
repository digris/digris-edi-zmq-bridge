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

#include <cstdint>
#include "zmq/edi2zmq.hpp"

ETI2ZMQ::ETI2ZMQ() : m_out("tcp", false) { }

void ETI2ZMQ::open(const char *destination)
{
    m_out.Open(destination);
    m_endpoint = destination;
}

bool ETI2ZMQ::is_open() const
{
    return not m_endpoint.empty();
}

std::string ETI2ZMQ::endpoint() const
{
    return m_endpoint;
}

void ETI2ZMQ::encode_zmq_frame(eti_frame_t&& eti)
{
    if (eti.frame_characterisation.fp % 4 == m_expected_next_fp) {
        m_expected_next_fp = (m_expected_next_fp + 1) % 4;

        m_out.Write(eti.frame.data(), eti.frame.size());

        // No metadata, because this tool is to reconstruct ZMQ
        // for the Easydab, which doesn't support metadata
    }
    else if (m_expected_next_fp != 0) {
        throw std::runtime_error("Unexpected frame phase");
    }
}
