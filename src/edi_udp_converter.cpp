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
#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#include <variant>
#include <cstring>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "edi_udp_converter.h"
#include "Log.h"
#include "edioutput/Transport.h"
#include "EDIReceiver.hpp"
#include "mpe_deframer.hpp"
#include "gse_deframer.hpp"
#include "common.h"

using namespace std;

volatile sig_atomic_t running = 1;

static void signal_handler(int signum)
{
    if (signum == SIGTERM) {
        fprintf(stderr, "Received SIGTERM\n");
        exit(0);
    }
    else {
        fprintf(stderr, "Received signal %d\n", signum);
    }
    //killpg(0, SIGPIPE);
    running = 0;
}

static void usage()
{
    cerr << "\nUsage:\n";
    cerr << "digris-edi-udp-converter [options]\n\n";
    cerr << "Receive EDI over multicast, remove PFT layer and make AF layer available as TCP server\n\n";

    cerr << " -v               Increase verbosity (Can be given more than once).\n";
    cerr << " --version        Print the version and quit.\n\n";
    cerr << " --http <IP:PORT> Enable HTTP Server listening on given IP:PORT\n";

    cerr << "Input settings\n";
    cerr << " -p PORT          Receive UDP on PORT\n";
    cerr << " -b BINDTO        Bind receive socket to BINDTO address\n";
    cerr << " -m ADDRESS       Receive from multicast ADDRESS\n";
    cerr << " -F PID:IP:PORT   Decode MPE like fedi2eti\n";
    cerr << " -G MIS           Decode GSE like pts2bbf|bbfedi2eti, with additional RTP deframing beforehand\n\n";
    cerr << " -G MIS:IP:PORT   As above, but only extract packets matching the IP:PORT filter\n\n";

    cerr << "Output settings\n";
    cerr << " -T PORT       Listen on TCP port PORT\n\n";

    cerr << "It is best practice to run this tool under a process supervisor that will restart it automatically.\n";
}

static const struct option longopts[] = {
    {"http", required_argument, 0, 2},
    {0, 0, 0, 0}
};

std::string Main::build_stats_json(
                EdiDecoder::EDIReceiver& rx,
                const edi::Sender& edisender)
{
    using namespace chrono;
    stringstream ss;
    ss << "{ \"inputs\": [\n {" <<
            " \"num_frames\": " << rx.num_frames.load() <<
            " }\n],\n";

    ss << " \"main\": {" <<
        " \"process_uptime\": " <<
        duration_cast<milliseconds>(steady_clock::now() - startup_time).count() <<
        " },";

    ss << " \"output\": {"
        " \"tcp_stats\": [";

    const auto tcp_stats = edisender.get_tcp_server_stats();
    for (auto it = tcp_stats.begin(); it != tcp_stats.end(); ++it) {
        if (it != tcp_stats.begin()) {
            ss << ",";
        }
        ss << " { \"listen_port\": " << it->listen_port <<
            ", \"num_connections\": " << it->stats.size() << "} ";
    }

    ss << " ] } ";

    ss << " }";
    return ss.str();
}

int main(int argc, char **argv)
{
    // Version handling is done very early to ensure nothing else but the version gets printed out
    if (argc == 2 and strcmp(argv[1], "--version") == 0) {
        fprintf(stdout, "%s\n",
#if defined(GITVERSION)
                GITVERSION
#else
                PACKAGE_VERSION
#endif
               );
        return 0;
    }

    cerr << "DIGRIS-EDI-UDP-CONVERTER " <<
#if defined(GITVERSION)
        GITVERSION <<
#else
        PACKAGE_VERSION <<
#endif
        " starting up\n" << BANNER_MESSAGE;

    if (argc == 1) {
        usage();
        return 1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(struct sigaction));
    sa.sa_handler = &signal_handler;

    const int sigs[] = {SIGHUP, SIGQUIT, SIGINT, SIGTERM};
    for (int sig : sigs) {
        if (sigaction(sig, &sa, nullptr) == -1) {
            perror("sigaction");
            return EXIT_FAILURE;
        }
    }

    int ret = 1;
    try {
        Main m;
        ret = m.start(argc, argv);

        // To make sure things get printed to stderr
        this_thread::sleep_for(chrono::milliseconds(300));
    }
    catch (const runtime_error &e) {
        etiLog.level(error) << "Runtime error: " << e.what();
    }
    catch (const logic_error &e) {
        etiLog.level(error) << "Logic error! " << e.what();
    }

    return ret;
}

int Main::start(int argc, char **argv)
{
    int ch = 0;
    int index = 0;
    while (ch != -1) {
        ch = getopt_long(argc, argv, "b:F:G:T:m:p:v", longopts, &index);
        switch (ch) {
            case -1:
                break;
            case 2: // --http
                {
                    stringstream all_args;
                    for (int i = 0; i < argc; i++) {
                        if (i > 0) all_args << " ";
                        all_args << argv[i];
                    }

                    string optarg_s = optarg;
                    const auto pos_colon = optarg_s.find(":");
                    if (pos_colon == string::npos or pos_colon == 0) {
                        etiLog.level(error) << "--http argument does not contain host:port";
                        return 1;
                    }

                    webserver.emplace(
                            optarg_s.substr(0, pos_colon),
                            stoi(optarg_s.substr(pos_colon+1)),
                            all_args.str());
                }
                break;
            case 'F':
                deframer = MPEDeframer(optarg);
                break;
            case 'G':
                deframer = GSEDeframer(optarg);
                break;
            case 'b':
                rx_bindto = optarg;
                break;
            case 'T':
                {
                    auto edi_destination = make_shared<edi::tcp_server_t>();
                    edi_destination->listen_port = stoi(optarg);
                    edi_destination->pft_settings.enable_pft = false;
                    edi_conf.destinations.push_back(std::move(edi_destination));
                }
                break;
            case 'm':
                rx_mcastaddr = optarg;
                break;
            case 'p':
                rx_port = stoi(optarg);
                break;
            case 'v':
                verbosity++;
                break;
            case 'h':
            default:
                usage();
                return 1;
        }
    }

    edi_conf.verbose = verbosity > 1;

    if (edi_conf.destinations.empty()) {
        etiLog.level(error) << "No EDI destinations set";
        return 1;
    }

    int ret = 1;

    try {
        edi::Sender edi_sender(edi_conf);

        Socket::UDPReceiver rx;

        EdiDecoder::EDIReceiver edi_rx(edi_sender);
        edi_rx.set_verbose(verbosity > 2);

        rx.add_receive_port(rx_port, rx_bindto, rx_mcastaddr);

        vector<Socket::UDPReceiver::ReceivedPacket> rx_packets;
        while (running) {
            rx_packets.clear();

            try {
                rx_packets = rx.receive(100);
            }
            catch (const Socket::UDPReceiver::Interrupted&) {
                running = false;
            }
            catch (const Socket::UDPReceiver::Timeout&) {
            }

            if (std::holds_alternative<MPEDeframer>(deframer)) {
                auto& mpe_deframer = std::get<MPEDeframer>(deframer);

                for (auto& rp : rx_packets) {
                    mpe_deframer.process_packet(rp.packetdata);
                }

                for (auto& deframed : mpe_deframer.get_deframed_packets()) {
                    EdiDecoder::Packet p;
                    p.buf = std::move(deframed);
                    p.received_on_port = 0;
                    edi_rx.push_packet(p);
                }
            }
            else if (std::holds_alternative<GSEDeframer>(deframer)) {
                auto& gse_deframer = std::get<GSEDeframer>(deframer);

                for (auto& rp : rx_packets) {
                    gse_deframer.process_packet(rp.packetdata);
                }

                for (auto& deframed : gse_deframer.get_deframed_packets()) {
                    EdiDecoder::Packet p;
                    p.buf = std::move(deframed);
                    p.received_on_port = 0;
                    edi_rx.push_packet(p);
                }
            }
            else {
                for (auto& rp : rx_packets) {
                    EdiDecoder::Packet p;
                    p.buf = std::move(rp.packetdata);
                    p.received_on_port = rp.port_received_on;
                    edi_rx.push_packet(p);
                }
            }

            if (webserver.has_value()) {
                using namespace std::chrono;
                if (last_stats_update_time + seconds(1) < steady_clock::now()) {
                    webserver->update_stats_json(build_stats_json(edi_rx, edi_sender));
                }
            }
        }
        // To make sure things get printed to stderr
        this_thread::sleep_for(chrono::milliseconds(300));
    }
    catch (const runtime_error &e) {
        etiLog.level(error) << "Runtime error: " << e.what();
    }
    catch (const logic_error &e) {
        etiLog.level(error) << "Logic error! " << e.what();
    }

    // Ensure stderr log gets written
    this_thread::sleep_for(chrono::milliseconds(50));
    return ret;
}

