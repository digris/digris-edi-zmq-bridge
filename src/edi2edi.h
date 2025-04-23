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

#include <chrono>
#include <memory>
#include <vector>
#include <cmath>
#include <cstring>
#include "receiver.h"
#include "EDISender.h"
#include "zmq/edi2zmq.hpp"
#include "webserver.h"

constexpr long DEFAULT_SWITCH_DELAY = 2000;

class Main {
    public:
        int start(int argc, char **argv);

    private:
        void ensure_one_active();

        void add_udp_edi_dest();
        void parse_udp_dest_args(char option);

        void init_rc();
        bool handle_rc_request();
        std::string handle_rc_command(const std::string& cmd);
        std::string build_stats_json(bool include_settings);

        std::shared_ptr<edi::udp_destination_t> edi_udp_dest;
        edi::pft_settings_t pft_settings = {};
        bool source_port_set = false;
        bool source_addr_set = false;
        bool ttl_set = false;
        bool dest_addr_set = false;
        bool dest_port_set = false;

        edi::configuration_t edi_conf;
        std::string startupcheck;
        std::vector<Receiver> receivers;
        std::vector<source_t> sources;

        EDISenderSettings edisendersettings;
        EDISender edisender;

        ETI2ZMQ eti_zmq_sender;

        int verbosity = 0;

        std::string rc_socket_name = "";
        int rc_socket = -1;

        std::chrono::steady_clock::duration switch_delay =
            std::chrono::milliseconds(DEFAULT_SWITCH_DELAY);

        const std::chrono::steady_clock::time_point startup_time =
            std::chrono::steady_clock::now();

        enum class Mode {
            Switching,
            Merging,
        };

        Mode mode = Mode::Merging;

        uint64_t num_poll_timeout = 0;

        std::optional<WebServer> webserver;
        std::chrono::steady_clock::time_point last_stats_update_time =
            std::chrono::steady_clock::now();
};
