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

#include <string>
#include <chrono>
#include <optional>
#include <variant>

#include "EDIReceiver.hpp"
#include "edioutput/EDIConfig.h"
#include "gse_deframer.hpp"
#include "mpe_deframer.hpp"
#include "webserver.h"

class Main {
    public:
        int start(int argc, char **argv);

    private:
        std::string build_stats_json(
                EdiDecoder::EDIReceiver& rx,
                const edi::Sender& edisender);

        edi::configuration_t edi_conf;

        int verbosity = 0;

        unsigned int rx_port = 0;
        std::string rx_bindto = "0.0.0.0";
        std::string rx_mcastaddr;

        std::variant<std::monostate, MPEDeframer, GSEDeframer> deframer;

        const std::chrono::steady_clock::time_point startup_time =
            std::chrono::steady_clock::now();

        std::chrono::steady_clock::time_point last_stats_update_time =
            std::chrono::steady_clock::now();
        std::optional<WebServer> webserver;
};
