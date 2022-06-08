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
#include <iomanip>
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
#include "main.h"
#include "edioutput/TagItems.h"
#include "edioutput/TagPacket.h"
#include "edioutput/Transport.h"
#include "EDIReceiver.hpp"

using namespace std;

volatile sig_atomic_t running = 1;

void signal_handler(int signum)
{
    if (signum == SIGTERM) {
        fprintf(stderr, "Received SIGTERM\n");
        exit(0);
    }
    //killpg(0, SIGPIPE);
    running = 0;
}

static void usage()
{
    cerr << "\nUsage:\n";
    cerr << "odr-edimcast2edi [options]\n\n";
    cerr << "Receive EDI over multicast, remove PFT layer and make AF layer available as TCP server\n\n";

    cerr << " -v            Increase verbosity (Can be given more than once).\n";
    cerr << " --version      the version and quit.\n\n";

    cerr << "Input settings\n";
    cerr << " -p PORT       Receive UDP on PORT\n";
    cerr << " -b BINDTO     Bind receive socket to BINDTO address\n";
    cerr << " -m ADDRESS    Receive from multicast ADDRESS\n\n";

    cerr << "Output settings\n";
    cerr << " -l PORT       Listen on port PORT\n\n";

    cerr << "It is best practice to run this tool under a process supervisor that will restart it automatically.\n";
}

static const struct option longopts[] = {
    {"live-stats-port", required_argument, 0, 2},
    {0, 0, 0, 0}
};

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

    cerr << "ODR-EDIMCAST2EDI " <<
#if defined(GITVERSION)
        GITVERSION <<
#else
        PACKAGE_VERSION <<
#endif
        " starting up" << endl;

    if (argc == 1) {
        usage();
        return 1;
    }

    int verbosity = 0;

    edi::configuration_t edi_conf;
    edi_conf.enable_pft = false;

    unsigned int rx_port = 0;
    string rx_bindto;
    string rx_mcastaddr;

    int ch = 0;
    int index = 0;
    while (ch != -1) {
        ch = getopt_long(argc, argv, "b:l:m:p:v", longopts, &index);
        switch (ch) {
            case 2: // --live-stats-port
                //int live_stats_port = stoi(optarg);
                break;
            case 'b':
                rx_bindto = optarg;
                break;
            case 'l':
                {
                    auto edi_destination = make_shared<edi::tcp_server_t>();
                    edi_destination->listen_port = stoi(optarg);
                    edi_conf.destinations.push_back(move(edi_destination));
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

    try {
        edi::Sender edi_sender(edi_conf);

        Socket::UDPReceiver rx;

        EdiDecoder::EDIReceiver edi_rx;

        rx.add_receive_port(rx_port, rx_bindto, rx_mcastaddr);

        while (running) {
            vector<Socket::UDPReceiver::ReceivedPacket> rx_packets;
            try {
                rx_packets = rx.receive(100);
            }
            catch (const Socket::UDPReceiver::Interrupted&) {
                running = false;
            }
            catch (const Socket::UDPReceiver::Timeout&) {
            }

            for (auto& rp : rx_packets) {
                auto received_from = rp.received_from;
                EdiDecoder::Packet p;
                p.buf = move(rp.packetdata);
                p.received_on_port = rp.port_received_on;
                edi_rx.push_packet(p);
            }
        }

        /*
        if (edi_conf.enabled()) {
            edi::TagPacket edi_tagpacket(0);

            if (tp.seq.seq_valid) {
                _edi_sender->override_af_sequence(tp.seq.seq);
            }

            if (tp.seq.pseq_valid) {
                _edi_sender->override_pft_sequence(tp.seq.pseq);
            }
            else if (tp.seq.seq_valid) {
                // If the source isn't using PFT, set PSEQ = SEQ so that multihoming
                // with several EDI2EDI instances could work.
                _edi_sender->override_pft_sequence(tp.seq.seq);
            }

            edi_tagpacket.raw_tagpacket = move(tp.tagpacket);
            _edi_sender->write(edi_tagpacket);
            num_frames.fetch_add(1);
        }
        */
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

