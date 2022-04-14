/*
   Copyright (C) 2022
   Matthias P. Braendli, matthias.braendli@mpb.li

    http://www.opendigitalradio.org
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

#include <algorithm>
#include <chrono>
#include <iostream>
#include <iterator>
#include <memory>
#include <thread>
#include <vector>
#include <unordered_map>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "Log.h"
#include "receiver.h"

using namespace std;

static constexpr auto RECONNECT_DELAY = chrono::milliseconds(24);

Receiver::Receiver(source_t& source, std::function<void(tagpacket_t&& tagpacket, Receiver*)> push_tagpacket, bool verbose) :
    source(source),
    m_push_tagpacket_callback(push_tagpacket),
    m_verbose(verbose)
{
    if (source.active) {
        etiLog.level(info) << "Connecting to TCP " << source.hostname << ":" << source.port;
        sock.connect(source.hostname, source.port, /*nonblock*/ true);
    }
}

void Receiver::update_fc_data(const EdiDecoder::eti_fc_data& fc_data) {
    m_dlfc = fc_data.dlfc;
}

void Receiver::assemble(EdiDecoder::ReceivedTagPacket&& tag_data) {
    tagpacket_t tp;
    tp.hostnames = source.hostname;
    tp.seq = tag_data.seq;
    tp.dlfc = m_dlfc;
    tp.tagpacket = move(tag_data.tagpacket);
    tp.received_at = chrono::steady_clock::now();
    tp.timestamp = move(tag_data.timestamp);
    margin = tp.timestamp.to_system_clock() - chrono::system_clock::now();
    m_push_tagpacket_callback(move(tp), this);
}

void Receiver::tick()
{
    if (source.active) {
        if (not sock.valid()) {
            if (reconnect_at < chrono::steady_clock::now()) {
                sock.connect(source.hostname, source.port, /*nonblock*/ true);
                // Mark connected = true only on successful data receive because of nonblock=true
                reconnect_at += RECONNECT_DELAY;
            }
        }
    }
    else {
        if (sock.valid()) {
            etiLog.level(info) << "Disconnecting from TCP " << source.hostname << ":" << source.port;
            sock.close();
            source.connected = false;
            m_edi_decoder.reset();
        }
    }
}
int Receiver::get_margin_ms() const {
    if (source.active) {
        using namespace chrono;
        return duration_cast<milliseconds>(margin).count();
    }
    else {
        return 0;
    }
}

void Receiver::receive()
{
    const size_t bufsize = 32;
    vector<uint8_t> buf(bufsize);
    bool success = false;
    ssize_t ret = ::recv(get_sockfd(), buf.data(), buf.size(), 0);
    if (ret == -1) {
        if (errno == EINTR) {
            success = false;
        }
        else if (errno == ECONNREFUSED) {
            // Behave as if disconnected
        }
        else {
            etiLog.level(error) << "TCP receive () error: " << strerror(errno);
            success = false;
        }
    }
    else if (ret > 0) {
        buf.resize(ret);
        if (!m_edi_decoder) {
            m_edi_decoder = make_shared<EdiDecoder::ETIDecoder>(*this);
            m_edi_decoder->set_verbose(m_verbose);
        }

        m_edi_decoder->push_bytes(buf);
        success = true;
    }
    // ret == 0 means disconnected

    if (not success) {
        sock.close();
        m_edi_decoder.reset();
        source.connected = false;
        reconnect_at = chrono::steady_clock::now() + RECONNECT_DELAY;
    }
    else {
        most_recent_rx_systime = chrono::system_clock::now();
        most_recent_rx_time = chrono::steady_clock::now();
        if (not source.connected) {
            source.num_connects++;
        }
        source.connected = true;
    }
}

