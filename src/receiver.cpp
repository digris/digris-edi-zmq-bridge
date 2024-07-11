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

using namespace std;

// TCP Keepalive settings
static constexpr int KA_TIME = 10; // Start keepalive after this period (seconds)
static constexpr int KA_INTVL = 2; // Interval between keepalives (seconds)
static constexpr int KA_PROBES = 3; // Number of keepalives before connection considered broken

static constexpr auto RECONNECT_DELAY = chrono::milliseconds(480);

Receiver::Receiver(source_t& source,
        std::function<void(tagpacket_t&&, Receiver*)> push_tagpacket,
        std::function<void(eti_frame_t&&)> eti_frame_callback,
        bool reconstruct_eti,
        int verbosity) :
    source(source),
    m_push_tagpacket_callback(push_tagpacket),
    m_eti_frame_callback(eti_frame_callback),
    m_reconstruct_eti(reconstruct_eti),
    m_verbosity(verbosity)
{
    if (source.active) {
        etiLog.level(info) << "Connecting to TCP " << source.hostname << ":" << source.port;
        try {
            sock.connect(source.hostname, source.port, /*nonblock*/ true);
            sock.enable_keepalive(KA_TIME, KA_INTVL, KA_PROBES);
        }
        catch (const runtime_error& e) {
            m_most_recent_connect_error.message = e.what();
            m_most_recent_connect_error.timestamp = std::chrono::system_clock::now();
        }
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
        stringstream ss;
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
        uint16_t FL = NST + 1 + m_fic.size();
        for (const auto& subch : m_subchannels) {
            FL += subch.mst.size();
        }

        const uint16_t fp_mid_fl = (m_fc.fp << 13) | (m_fc.mid << 11) | FL;

        eti.push_back(fp_mid_fl >> 8);
        eti.push_back(fp_mid_fl & 0xFF);

        // STC
        for (const auto& subch : m_subchannels) {
            eti.push_back( (subch.scid << 2) | (subch.sad & 0x300) );
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

    using namespace chrono;
    tagpacket_t tp;
    tp.hostnames = source.hostname;
    tp.seq = tag_data.seq;
    tp.dlfc = m_fc.dlfc;
    tp.afpacket = std::move(tag_data.afpacket);
    tp.received_at = steady_clock::now();
    tp.timestamp = std::move(tag_data.timestamp);
    const auto margin = tp.timestamp.to_system_clock() - system_clock::now();
    margins_ms.push_back(duration_cast<milliseconds>(margin).count());
    if (margins_ms.size() > 2500 /* 1 minute */) {
        margins_ms.pop_front();
    }
    m_push_tagpacket_callback(std::move(tp), this);
}

void Receiver::tick()
{
    if (source.active) {
        if (not sock.valid()) {
            if (reconnect_at < chrono::steady_clock::now()) {
                try {
                    sock.connect(source.hostname, source.port, /*nonblock*/ true);
                    sock.enable_keepalive(KA_TIME, KA_INTVL, KA_PROBES);
                }
                catch (const runtime_error& e) {
                    if (m_verbosity > 0) {
                        etiLog.level(debug) << "Connecting to " << source.hostname << ":" << source.port <<
                            " failed: " << e.what();
                    }
                    m_most_recent_connect_error.message = e.what();
                    m_most_recent_connect_error.timestamp = std::chrono::system_clock::now();
                }
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

Receiver::margin_stats_t Receiver::get_margin_stats() const
{
    margin_stats_t r;

    if (source.active and margins_ms.size() > 0) {
        r.num_measurements = margins_ms.size();
        const double n = r.num_measurements;
        double sum = 0.0;
        r.min = std::numeric_limits<double>::max();
        r.max = -std::numeric_limits<double>::max();

        for (const double t : margins_ms) {
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
        for (const double t : margins_ms) {
            sq_sum += (t-r.mean) * (t-r.mean);
        }
        r.stdev = sqrt(sq_sum / n);
    }

    return r;
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
            if (m_verbosity > 0) {
                etiLog.level(debug) << "Receive from " << source.hostname << ":" << source.port << ": Connection refused";
            }
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
            m_edi_decoder->set_verbose(m_verbosity > 1);
        }

        m_edi_decoder->push_bytes(buf);
        success = true;
    }
    // ret == 0 means disconnected

    if (not success) {
        if (m_verbosity > 0) {
            etiLog.level(debug) << "Remote " << source.hostname << ":" << source.port << " closed connection";
        }
        sock.close();
        m_edi_decoder.reset();
        source.connected = false;
        reconnect_at = chrono::steady_clock::now() + RECONNECT_DELAY;
    }
    else {
        most_recent_rx_systime = chrono::system_clock::now();
        most_recent_rx_time = chrono::steady_clock::now();
        if (not source.connected) {
            if (m_verbosity > 0) {
                etiLog.level(debug) << "Connection to " << source.hostname << ":" << source.port << " reestablished";
            }
            source.num_connects++;
            reconnected_at = chrono::steady_clock::now();
        }
        source.connected = true;
    }
}

void Receiver::set_verbosity(int verbosity)
{
    m_verbosity = verbosity;
    if (m_edi_decoder) {
        m_edi_decoder->set_verbose(m_verbosity > 1);
    }
}
