/*
   Copyright (C) 2021
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

#include <chrono>
#include <iostream>
#include <iterator>
#include <memory>
#include <thread>
#include <vector>
#include <cmath>
#include <cstring>
#include "edi/ETIDecoder.hpp"
#include "EDISender.h"
#include "edioutput/TagItems.h"
#include "edioutput/TagPacket.h"
#include "edioutput/Transport.h"
#include "edi/ETIDecoder.hpp"

constexpr long DEFAULT_BACKOFF = 5000;

void signal_handler(int signum);

class Receiver : public EdiDecoder::ETIDataCollector {
    public:
        Receiver(const source_t& source, EDISender& edisender, bool verbose);
        Receiver(const Receiver&) = delete;
        Receiver operator=(const Receiver&) = delete;
        Receiver(Receiver&&) = default;
        Receiver& operator=(Receiver&&) = delete;

        // Tell the ETIWriter what EDI protocol we receive in *ptr.
        // This is not part of the ETI data, but is used as check
        virtual void update_protocol(
                const std::string& proto,
                uint16_t major,
                uint16_t minor) override { }

        // Update the data for the frame characterisation
        virtual void update_fc_data(const EdiDecoder::eti_fc_data& fc_data) override;

        // Ignore most events because we are interested in retransmitting EDI, not
        // decoding it
        virtual void update_fic(std::vector<uint8_t>&& fic) override { }
        virtual void update_err(uint8_t err) override { }
        virtual void update_edi_time(uint32_t utco, uint32_t seconds) override { }
        virtual void update_mnsc(uint16_t mnsc) override { }
        virtual void update_rfu(uint16_t rfu) override { }
        virtual void add_subchannel(EdiDecoder::eti_stc_data&& stc) override { }

        // Tell the ETIWriter that the AFPacket is complete
        virtual void assemble(EdiDecoder::ReceivedTagPacket&& tag_data) override;

        // Must return -1 if the socket is not poll()able
        int get_sockfd() const { return sock.get_sockfd(); }

        void receive();

        void tick();

    private:
        const source_t& source;
        EDISender& edi_sender;
        EdiDecoder::ETIDecoder edi_decoder;
        uint16_t dlfc = 0;

        std::chrono::steady_clock::time_point reconnect_at = std::chrono::steady_clock::now();

        Socket::TCPSocket sock;
};

class Main {
    public:
        int start(int argc, char **argv);

    private:
        void add_edi_destination();
        void parse_destination_args(char option);

        void init_rc();
        bool handle_rc_request();
        std::string handle_rc_command(const std::string& cmd);

        std::shared_ptr<edi::udp_destination_t> edi_destination;
        bool source_port_set = false;
        bool source_addr_set = false;
        bool ttl_set = false;
        bool dest_addr_set = false;
        bool dest_port_set = false;
        edi::configuration_t edi_conf;
        std::string startupcheck;
        std::vector<source_t> sources;

        EDISenderSettings edisendersettings;
        EDISender edisender;

        std::string rc_socket_name = "";
        int rc_socket = -1;
};
