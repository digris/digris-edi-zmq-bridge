/*
   Copyright (C) 2025
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

#pragma once
#include <chrono>
#include <memory>
#include <vector>
#include <cmath>
#include <cstring>
#include "Socket.h"
#include "edi/ETIDecoder.hpp"


struct tagpacket_t {
    // source information
    std::string hostnames;

    std::vector<uint8_t> afpacket;
    uint16_t dlfc;
    EdiDecoder::frame_timestamp_t timestamp;
    std::chrono::steady_clock::time_point received_at;
    EdiDecoder::seq_info_t seq;
};

struct source_t {
    source_t(std::string hostname, int port, bool enabled) :
        hostname(hostname), port(port), enabled(enabled) {}

    void reset_counters() { num_connects = 0; }

    std::string hostname;
    int port;

    // User-controlled setting
    bool enabled;

    // Mode merging: active will be set for all enabled inputs.
    // Mode switching: only one input will be active
    bool active = false;

    bool connected = false;

    uint64_t num_connects = 0;
};

struct eti_frame_t {
    std::vector<uint8_t> frame;
    uint16_t mnsc;
    EdiDecoder::frame_timestamp_t timestamp;
    EdiDecoder::eti_fc_data frame_characterisation;
};

class Receiver : public EdiDecoder::ETIDataCollector {
    public:
        Receiver(
                source_t& source,
                std::function<void(tagpacket_t&&, Receiver*)> push_tagpacket,
                std::function<void(eti_frame_t&&)> eti_frame_callback,
                bool reconstruct_eti,
                int verbosity
                );
        Receiver(const Receiver&) = delete;
        Receiver operator=(const Receiver&) = delete;
        Receiver(Receiver&&) = default;
        Receiver& operator=(Receiver&&) = delete;

        // Tell the ETIWriter what EDI protocol we receive in *ptr.
        // This is not part of the ETI data, but is used as check
        virtual void update_protocol(
                const std::string& proto,
                uint16_t major,
                uint16_t minor) override;

        // Update the data for the frame characterisation
        virtual void update_fc_data(const EdiDecoder::eti_fc_data& fc_data) override;

        // Collect data for ZMQ frame reconstruction
        virtual void update_fic(std::vector<uint8_t>&& fic) override;
        virtual void update_err(uint8_t err) override;
        virtual void update_edi_time(uint32_t utco, uint32_t seconds) override;
        virtual void update_mnsc(uint16_t mnsc) override;
        virtual void update_rfu(uint16_t rfu) override;
        virtual void add_subchannel(EdiDecoder::eti_stc_data&& stc) override;

        // Tell the ETIWriter that the AFPacket is complete
        virtual void assemble(EdiDecoder::ReceivedTagPacket&& tag_data) override;

        // Must return -1 if the socket is not poll()able
        int get_sockfd() const { return sock.get_sockfd(); }

        void receive();
        void tick();
        struct margin_stats_t {
            double min = 0.0;
            double max = 0.0;
            double mean = 0.0;
            double stdev = 0.0;
            size_t num_measurements = 0;
        };
        margin_stats_t get_margin_stats() const;

        std::chrono::system_clock::time_point get_systime_last_packet() const
        {
            return most_recent_rx_systime;
        }

        std::chrono::steady_clock::time_point get_time_last_packet() const
        {
            return most_recent_rx_time;
        }

        uint64_t connection_uptime_ms() const
        {
            using namespace std::chrono;
            return duration_cast<milliseconds>(steady_clock::now() - reconnected_at).count();
        }

        struct connection_error_t {
            std::string message;
            std::chrono::system_clock::time_point timestamp;
        };

        connection_error_t get_last_connection_error() const
        {
            return m_most_recent_connect_error;
        }

        void reset_counters() { num_late = 0; }

        source_t& source;

        // The EDISender will update the late count
        uint64_t num_late = 0;

        void set_verbosity(int verbosity);

    private:
        std::function<void(tagpacket_t&& tagpacket, Receiver*)> m_push_tagpacket_callback;
        std::function<void(eti_frame_t&&)> m_eti_frame_callback;
        bool m_reconstruct_eti = false;
        std::shared_ptr<EdiDecoder::ETIDecoder> m_edi_decoder;

        bool m_fc_valid = false;
        EdiDecoder::eti_fc_data m_fc;
        bool m_proto_valid = false;
        uint8_t m_err = 0x00;
        // m_fic is valid if non-empty
        std::vector<uint8_t> m_fic;
        std::list<EdiDecoder::eti_stc_data> m_subchannels;
        bool m_time_valid = false;
        uint32_t m_utco = 0;
        uint32_t m_seconds = 0;
        uint16_t m_mnsc = 0xffff;
        // 16 bits: RFU field in EOH
        uint16_t m_rfu = 0xffff;

        int m_verbosity;

        connection_error_t m_most_recent_connect_error;

        std::chrono::steady_clock::time_point reconnect_at = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point reconnected_at = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point most_recent_rx_time = std::chrono::steady_clock::time_point();
        std::chrono::system_clock::time_point most_recent_rx_systime = std::chrono::system_clock::time_point();

        std::deque<int> margins_ms;

        Socket::TCPSocket sock;
};
