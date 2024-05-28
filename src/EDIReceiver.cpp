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
#include "EDIReceiver.hpp"
#include "Log.h"

namespace EdiDecoder {

using namespace std;

EDIReceiver::EDIReceiver(edi::Sender& sender) :
    m_dispatcher(std::bind(&EDIReceiver::packet_completed, this)),
    m_sender(sender)
{
    using std::placeholders::_1;
    using std::placeholders::_2;
    // Register these tags to avoid the "unknown TAG" warning message
    m_dispatcher.register_tag("*ptr",
            std::bind(&EDIReceiver::decode_starptr, this, _1, _2));
    m_dispatcher.register_tag("deti",
            std::bind(&EDIReceiver::decode_deti, this, _1, _2));
    m_dispatcher.register_tag("est",
            std::bind(&EDIReceiver::decode_estn, this, _1, _2));
    m_dispatcher.register_tag("*dmy",
            std::bind(&EDIReceiver::decode_stardmy, this, _1, _2));

    m_dispatcher.register_afpacket_handler(std::bind(&EDIReceiver::handle_afpacket, this, _1));
}

void EDIReceiver::set_verbose(bool verbose)
{
    m_dispatcher.set_verbose(verbose);
}


void EDIReceiver::push_packet(Packet &pack)
{
    m_dispatcher.push_packet(pack);
}

void EDIReceiver::setMaxDelay(int num_af_packets)
{
    m_dispatcher.setMaxDelay(num_af_packets);
}

bool EDIReceiver::decode_starptr(const std::vector<uint8_t>& value, const tag_name_t&)
{
    if (value.size() != 0x40 / 8) {
        etiLog.log(warn, "Incorrect length %02lx for *PTR", value.size());
        return false;
    }

    char protocol_sz[5];
    protocol_sz[4] = '\0';
    copy(value.begin(), value.begin() + 4, protocol_sz);
    string protocol(protocol_sz);
    m_protocol = protocol;

    /*
    uint16_t major = read_16b(value.begin() + 4);
    uint16_t minor = read_16b(value.begin() + 6);
    */

    return true;
}

bool EDIReceiver::decode_deti(const std::vector<uint8_t>&, const tag_name_t&)
{
    return true;
}

bool EDIReceiver::decode_estn(const std::vector<uint8_t>&, const tag_name_t&)
{
    return true;
}

bool EDIReceiver::decode_stardmy(const std::vector<uint8_t>&, const tag_name_t&)
{
    return true;
}

void EDIReceiver::packet_completed()
{
    if (m_protocol != "DETI") {
        etiLog.level(info) << "Received frame with unknown protocol " << m_protocol;
    }
}

bool EDIReceiver::handle_afpacket(std::vector<uint8_t>&& value)
{
#if 0
    const auto seq = m_dispatcher.get_seq_info();
    if (seq.seq_valid) {
        m_sender.override_af_sequence(seq.seq);
    }

    if (seq.pseq_valid) {
        m_sender.override_pft_sequence(seq.pseq);
    }
    else if (seq.seq_valid) {
        // If the source isn't using PFT, set PSEQ = SEQ, for consistency
        m_sender.override_pft_sequence(seq.seq);
    }
#endif

    m_sender.write(std::move(value));
    num_frames.fetch_add(1);
    return true;
}

}
