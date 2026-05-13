/*
   Copyright (C) 2026
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
#include <vector>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "Log.h"
#include "receiver.h"
#include "crc.h"

static constexpr auto RECONNECT_DELAY = std::chrono::milliseconds(480);

Receiver::Receiver(source_t& source,
        std::chrono::milliseconds receive_timeout,
        std::function<void(tagpacket_t&&, Receiver*)> push_tagpacket,
        std::function<void(eti_frame_t&&)> eti_frame_callback,
        bool reconstruct_eti,
        int verbosity) :
    source(source),
    m_push_tagpacket_callback(push_tagpacket),
    m_eti_frame_callback(eti_frame_callback),
    m_reconstruct_eti(reconstruct_eti),
    m_receive_timeout(receive_timeout),
    m_verbosity(verbosity)
{
    if (std::holds_alternative<tcp_source_t>(source)) {
        const auto& s = std::get<tcp_source_t>(source);
        enabled = s.enabled_at_startup;
    }
    else {
        const auto& s = std::get<udp_source_t>(source);
        enabled = s.enabled_at_startup;
    }
}

void Receiver::update_protocol(
        const std::string& proto,
        uint16_t major,
        uint16_t minor)
{
    m_proto_valid = (proto == "DETI" and major == 0 and minor == 0);

    if (not m_proto_valid) {
        throw std::invalid_argument("Wrong EDI protocol");
    }
}

void Receiver::update_err(uint8_t err)
{
    if (not m_proto_valid) {
        throw std::logic_error("Cannot update ERR before protocol");
    }
    m_err = err;
}

void Receiver::update_fc_data(const EdiDecoder::eti_fc_data& fc_data)
{
    if (not m_proto_valid) {
        throw std::logic_error("Cannot update FC before protocol");
    }

    m_fc_valid = false;
    m_fc = fc_data;

    if (not m_fc.ficf) {
        throw std::invalid_argument("FIC must be present");
    }

    if (m_fc.mid > 4) {
        throw std::invalid_argument("Invalid MID");
    }

    if (m_fc.fp > 7) {
        throw std::invalid_argument("Invalid FP");
    }

    m_fc_valid = true;
}

void Receiver::update_fic(std::vector<uint8_t>&& fic)
{
    if (not m_proto_valid) {
        throw std::logic_error("Cannot update FIC before protocol");
    }

    m_fic = std::move(fic);
}

void Receiver::update_edi_time(
        uint32_t utco,
        uint32_t seconds)
{
    if (not m_proto_valid) {
        throw std::logic_error("Cannot update time before protocol");
    }

    m_utco = utco;
    m_seconds = seconds;

    // TODO check validity
    m_time_valid = true;
}

void Receiver::update_mnsc(uint16_t mnsc)
{
    if (not m_proto_valid) {
        throw std::logic_error("Cannot update MNSC before protocol");
    }

    m_mnsc = mnsc;
}

void Receiver::update_rfu(uint16_t rfu)
{
    if (not m_proto_valid) {
        throw std::logic_error("Cannot update RFU before protocol");
    }

    m_rfu = rfu;
}

void Receiver::add_subchannel(EdiDecoder::eti_stc_data&& stc)
{
    if (not m_proto_valid) {
        throw std::logic_error("Cannot add subchannel before protocol");
    }

    m_subchannels.emplace_back(std::move(stc));

    if (m_subchannels.size() > 64) {
        throw std::invalid_argument("Too many subchannels");
    }

}

void Receiver::assemble(EdiDecoder::ReceivedTagPacket&& tag_data)
{
    if (not m_proto_valid) {
        throw std::logic_error("Cannot assemble ETI before protocol");
    }

    if (not m_fc_valid) {
        throw std::logic_error("Cannot assemble ETI without FC");
    }

    if (m_fic.empty()) {
        throw std::logic_error("Cannot assemble ETI without FIC data");
    }

    // ETS 300 799 Clause 5.3.2, but we don't support not having
    // a FIC
    if (    (m_fc.mid == 3 and m_fic.size() != 32 * 4) or
            (m_fc.mid != 3 and m_fic.size() != 24 * 4) ) {
        std::stringstream ss;
        ss << "Invalid FIC length " << m_fic.size() <<
            " for MID " << m_fc.mid;
        throw std::invalid_argument(ss.str());
    }

    if (m_reconstruct_eti) {
        std::vector<uint8_t> eti;
        eti.reserve(6144);

        eti.push_back(m_err);

        // FSYNC
        if (m_fc.fct() % 2 == 1) {
            eti.push_back(0xf8);
            eti.push_back(0xc5);
            eti.push_back(0x49);
        }
        else {
            eti.push_back(0x07);
            eti.push_back(0x3a);
            eti.push_back(0xb6);
        }

        // LIDATA
        // FC
        eti.push_back(m_fc.fct());

        const uint8_t NST = m_subchannels.size();

        if (NST == 0) {
            etiLog.level(info) << "Zero subchannels in EDI stream";
        }

        eti.push_back((m_fc.ficf << 7) | NST);

        // We need to pack:
        //  FP 3 bits
        //  MID 2 bits
        //  FL 11 bits

        // FL: EN 300 799 5.3.6
        if ((m_fic.size() % 4) != 0) {
            throw std::logic_error("FIC size is not multiple of 4");
        }

        // FL is length in words of:
        //            STC + EOH + FIC + all subchannels
        uint16_t FL = NST + 1   + m_fic.size() / 4;
        for (const auto& subch : m_subchannels) {
            if ((subch.mst.size() % 4) != 0) {
                throw std::logic_error("SubCh size is not multiple of 4");
            }
            FL += subch.mst.size() / 4;
        }

        const uint16_t fp_mid_fl = (m_fc.fp << 13) | (m_fc.mid << 11) | FL;

        eti.push_back(fp_mid_fl >> 8);
        eti.push_back(fp_mid_fl & 0xFF);

        // STC
        for (const auto& subch : m_subchannels) {
            eti.push_back( (subch.scid << 2) | ((subch.sad & 0x300) >> 8) );
            eti.push_back( subch.sad & 0xff );
            eti.push_back( (subch.tpl << 2) | ((subch.stl() & 0x300) >> 8) );
            eti.push_back( subch.stl() & 0xff );
        }

        // EOH
        // MNSC
        eti.push_back(m_mnsc >> 8);
        eti.push_back(m_mnsc & 0xFF);

        // CRC
        // Calculate CRC from eti[4] to current position
        uint16_t eti_crc = 0xFFFF;
        eti_crc = crc16(eti_crc, &eti[4], eti.size() - 4);
        eti_crc ^= 0xffff;
        eti.push_back(eti_crc >> 8);
        eti.push_back(eti_crc & 0xFF);

        const size_t mst_start = eti.size();
        // MST
        // FIC data
        copy(m_fic.begin(), m_fic.end(), back_inserter(eti));

        // Data stream
        for (const auto& subch : m_subchannels) {
            copy(subch.mst.begin(), subch.mst.end(), back_inserter(eti));
        }

        // EOF
        // CRC
        uint16_t mst_crc = 0xFFFF;
        mst_crc = crc16(mst_crc, &eti[mst_start], eti.size() - mst_start);
        mst_crc ^= 0xffff;
        eti.push_back(mst_crc >> 8);
        eti.push_back(mst_crc & 0xFF);

        // RFU
        eti.push_back(m_rfu >> 8);
        eti.push_back(m_rfu);

        // TIST
        eti.push_back(m_fc.tsta >> 24);
        eti.push_back((m_fc.tsta >> 16) & 0xFF);
        eti.push_back((m_fc.tsta >> 8) & 0xFF);
        eti.push_back(m_fc.tsta & 0xFF);

        if (eti.size() > 6144) {
            std::stringstream ss;
            ss << "ETI length error: " <<
                "FIC[" << m_fic.size() << "] Subch ";

            for (const auto& subch : m_subchannels) {
                ss << (int)subch.stream_index << "[" << subch.mst.size() << "] ";
            }

            etiLog.level(debug) << ss.str();
            throw std::logic_error("ETI frame cannot be longer than 6144: " +
                    std::to_string(eti.size()));
        }

        // Do not resize to 6144, because output is ZMQ, which doesn't need
        // full length frames.
        //eti.resize(6144, 0x55);

        eti_frame_t etiFrame;
        etiFrame.frame = std::move(eti);
        etiFrame.timestamp.seconds = m_seconds;
        etiFrame.timestamp.utco = m_utco;
        etiFrame.timestamp.tsta = m_fc.tsta;
        etiFrame.mnsc = m_mnsc;
        etiFrame.frame_characterisation = std::move(m_fc);

        m_eti_frame_callback(std::move(etiFrame));
    }

    m_mnsc = 0xFFFF;
    m_proto_valid = false;
    m_fc_valid = false;
    m_fic.clear();
    m_subchannels.clear();

    using namespace std::chrono;
    tagpacket_t tp;
    tp.source_urls = source_url();
    tp.seq = tag_data.seq;
    tp.dlfc = m_fc.dlfc;
    tp.afpacket = std::move(tag_data.afpacket);
    tp.received_at = steady_clock::now();
    tp.timestamp = std::move(tag_data.timestamp);
    const auto margin = tp.timestamp.to_system_clock() - system_clock::now();
    m_margins_ms.push_back(duration_cast<milliseconds>(margin).count());
    if (m_margins_ms.size() > 2500 /* 1 minute */) {
        m_margins_ms.pop_front();
    }
    m_push_tagpacket_callback(std::move(tp), this);
}

void Receiver::tick()
{
    if (std::holds_alternative<tcp_source_t>(source)) {
        const auto& s = std::get<tcp_source_t>(source);

        auto do_reconnect = [&]() {
            m_tcp_sock.close();
            m_edi_decoder.reset();

            try {
                if (m_verbosity > 0) {
                    etiLog.level(debug) << "Attempt connect to " << s.hostname << ":" << s.port;
                }
                m_tcp_sock.connect(s.hostname, s.port, /*nonblock*/ true);
                m_tcp_sock_state = tcp_sock_state_e::CONNECTING;
            }
            catch (const std::runtime_error& e) {
                if (m_verbosity > 0) {
                    etiLog.level(debug) << "Connecting to " << s.hostname << ":" << s.port <<
                        " failed: " << e.what();
                }
                m_most_recent_connect_error.message = e.what();
                m_most_recent_connect_error.timestamp = std::chrono::system_clock::now();
            }

            // Mark connected = true only on successful data receive because of nonblock=true
            reconnect_at += RECONNECT_DELAY;
        };

        if (active) {
            switch (m_tcp_sock_state) {
                case tcp_sock_state_e::DISABLED:
                    do_reconnect();
                    break;
                case tcp_sock_state_e::CONNECTING:
                    if (reconnect_at < std::chrono::steady_clock::now()) {
                        etiLog.level(info) << "Timeout during reconnect on TCP " <<
                            s.hostname << ":" << s.port;
                        do_reconnect();
                    }
                    break;
                case tcp_sock_state_e::CONNECTED:
                    if (most_recent_rx_time + m_receive_timeout < std::chrono::steady_clock::now()) {
                        etiLog.level(info) << "Timeout on TCP " << s.hostname << ":" << s.port;
                        do_reconnect();
                    }
                    break;
            }
        }
        else {
            switch (m_tcp_sock_state) {
                case tcp_sock_state_e::DISABLED:
                    break;
                case tcp_sock_state_e::CONNECTING:
                case tcp_sock_state_e::CONNECTED:
                    etiLog.level(info) << "Disconnecting from TCP " << s.hostname << ":" << s.port;
                    m_tcp_sock.close();
                    m_tcp_sock_state = tcp_sock_state_e::DISABLED;
                    m_edi_decoder.reset();
                    break;
            }
        }
    }
    else {
        if (active) {
            if (not m_udp_sock_ready) {
                const auto& s = std::get<udp_source_t>(source);
                etiLog.level(debug) << "UDP reinit " << source_url();

                if (IN_MULTICAST(ntohl(inet_addr(s.mcastaddr.c_str())))) {
                    m_udp_sock.init_receive_multicast(s.port, s.bindto, s.mcastaddr);
                }
                else {
                    m_udp_sock.reinit(s.port, s.bindto);
                }
                m_udp_sock_ready = true;
            }
            else if (most_recent_rx_time + m_receive_timeout < std::chrono::steady_clock::now()) {
                m_udp_sock.close();
                m_udp_sock_ready = false;
            }
        }
        else if (not active and m_udp_sock_ready) {
            etiLog.level(debug) << "Stop UDP from " << source_url();
            m_udp_sock.close();
            m_udp_sock_ready = false;
            m_edi_decoder.reset();
        }
    }
}

Receiver::margin_stats_t Receiver::get_margin_stats() const
{
    margin_stats_t r;

    if (active and m_margins_ms.size() > 0) {
        r.num_measurements = m_margins_ms.size();
        const double n = r.num_measurements;
        double sum = 0.0;
        r.min = std::numeric_limits<double>::max();
        r.max = -std::numeric_limits<double>::max();

        for (const double t : m_margins_ms) {
            sum += t;

            if (t < r.min) {
                r.min = t;
            }

            if (t > r.max) {
                r.max = t;
            }
        }
        r.mean = sum / n;

        double sq_sum = 0;
        for (const double t : m_margins_ms) {
            sq_sum += (t-r.mean) * (t-r.mean);
        }
        r.stdev = sqrt(sq_sum / n);
    }

    return r;
}

int Receiver::get_sockfd() const
{
    if (std::holds_alternative<tcp_source_t>(source)) {
        return m_tcp_sock.get_sockfd();
    }
    else {
        return m_udp_sock.getNativeSocket();
    }
}

bool Receiver::connected() const
{
    if (std::holds_alternative<tcp_source_t>(source)) {
        return m_tcp_sock_state == tcp_sock_state_e::CONNECTED;
    }
    else {
        return m_udp_sock_ready;
    }
}

void Receiver::receive()
{
    if (std::holds_alternative<tcp_source_t>(source)) {
        receive_tcp();
    }
    else {
        receive_udp();
    }
}

void Receiver::receive_udp()
{
    try {
        auto p = m_udp_sock.receive(2048);
        if (not p.buffer.empty()) {
            EdiDecoder::Packet packet{std::move(p.buffer)};
            if (!m_edi_decoder) {
                m_edi_decoder = std::make_shared<EdiDecoder::ETIDecoder>(*this);
                m_edi_decoder->set_verbose(m_verbosity > 1);
            }
            m_edi_decoder->push_packet(std::move(packet));

            using namespace std::chrono;
            most_recent_rx_systime = system_clock::now();
            most_recent_rx_time = steady_clock::now();
        }
    }
    catch (const std::runtime_error& e)
    {
        etiLog.level(error) << "UDP receive " << source_url() << " error: " << strerror(errno);
    }
}

void Receiver::receive_tcp()
{
    using namespace std::chrono;

    constexpr size_t bufsize = 256;
    m_tcp_rx_buf.resize(bufsize);
    bool success = false;
    ssize_t ret = ::recv(get_sockfd(), m_tcp_rx_buf.data(), m_tcp_rx_buf.size(), 0);
    if (ret == -1) {
        if (errno == EINTR) {
            success = false;
        }
        else if (errno == ECONNREFUSED) {
            // Behave as if disconnected
            if (m_verbosity > 0) {
                etiLog.level(debug) << "Receive from " << source_url() << " Connection refused";
            }
        }
        else {
            etiLog.level(error) << "TCP receive " << source_url() << " error: " << strerror(errno);
            success = false;
        }
    }
    else if (ret > 0) {
        m_tcp_rx_buf.resize(ret);
        if (!m_edi_decoder) {
            m_edi_decoder = std::make_shared<EdiDecoder::ETIDecoder>(*this);
            m_edi_decoder->set_verbose(m_verbosity > 1);
        }

        m_edi_decoder->push_bytes(m_tcp_rx_buf);
        success = true;
    }
    // ret == 0 means disconnected

    if (not success) {
        etiLog.level(debug) << "Remote " << source_url() << " closed connection";
        m_tcp_sock.close();
        m_edi_decoder.reset();
        m_tcp_sock_state = tcp_sock_state_e::DISABLED;
        reconnect_at = steady_clock::now() + RECONNECT_DELAY;
    }
    else {
        most_recent_rx_systime = system_clock::now();
        most_recent_rx_time = steady_clock::now();
        if (m_tcp_sock_state == tcp_sock_state_e::CONNECTING) {
            etiLog.level(debug) << "Connection to " << source_url() << " reestablished";
            m_num_connects++;
            reconnected_at = steady_clock::now();
        }
        m_tcp_sock_state = tcp_sock_state_e::CONNECTED;
    }
}

void Receiver::set_verbosity(int verbosity)
{
    m_verbosity = verbosity;
    if (m_edi_decoder) {
        m_edi_decoder->set_verbose(m_verbosity > 1);
    }
}

std::string Receiver::source_url() const {
    if (std::holds_alternative<tcp_source_t>(source)) {
        const auto& s = std::get<tcp_source_t>(source);
        return std::string{"tcp://"} + s.hostname + ":" + std::to_string(s.port);
    }
    else {
        const auto& s = std::get<udp_source_t>(source);
        if (not s.mcastaddr.empty()) {
            return std::string{"udp://"} +
                s.bindto + "@" + s.mcastaddr + ":" + std::to_string(s.port);
        }
        else {
            return std::string{"udp://"} +
                s.bindto + ":" + std::to_string(s.port);
        }
    }
}

