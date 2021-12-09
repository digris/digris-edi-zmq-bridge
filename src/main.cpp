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
#include <signal.h>
#include <getopt.h>
#include "Log.h"
#include "main.h"

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
    cerr << "odr-edi2edi [options] -c <source>\n\n";

    cerr << "Options:\n";
    cerr << "The following options can be given only once:\n";
    cerr << " -c <host:port>        Connect to given host and port using TCP.\n";
    cerr << " -w <delay>            Keep every ETI frame until TIST is <delay> milliseconds after current system time.\n";
    cerr << "                       Negative delay values are also allowed.\n";
    cerr << " -x <drop_delay>       Drop frames where for which are too late, defined by the drop delay.\n";
    cerr << " -C <path to script>   Before starting, run the given script, and only start if it returns 0.\n";
    cerr << "                       This is useful for checking that NTP is properly synchronised\n";
    cerr << " -P                    Disable PFT and send AFPackets.\n";
    cerr << " -f <fec>              Set the FEC.\n";
    cerr << " -i <interleave>       Configure the interleaver with given interleave percentage: 0% send all fragments at once, 100% spread over 24ms, >100% spread and interleave. Default 95%\n";
    cerr << " -D                    Dump the EDI to edi.debug file.\n";
    cerr << " -v                    Enables verbose mode.\n";
    cerr << " -a <alignement>       Set the alignment of the TAG Packet (default 8).\n";
    cerr << " -b <backoff>          Number of milliseconds to backoff after an input reset (default " << DEFAULT_BACKOFF << ").\n";
    cerr << " --version             Show the version and quit.\n\n";

    cerr << "The following options can be given several times, when more than UDP destination is desired:\n";
    cerr << " -p <destination port> Set the destination port.\n";
    cerr << " -d <destination ip>   Set the destination ip.\n";
    cerr << " -s <source port>      Set the source port.\n";
    cerr << " -S <source ip>        Select the source IP in case we want to use multicast.\n";
    cerr << " -t <ttl>              Set the packet's TTL.\n\n";

    cerr << "It is best practice to run this tool under a process supervisor that will restart it automatically." << endl;
}


void Main::update_fc_data(const EdiDecoder::eti_fc_data& fc_data) {
    dlfc = fc_data.dlfc;
}

void Main::assemble(EdiDecoder::ReceivedTagPacket&& tag_data) {
    tagpacket_t tp;
    tp.seq = tag_data.seq;
    tp.dlfc = dlfc;
    tp.tagpacket = move(tag_data.tagpacket);
    tp.received_at = std::chrono::steady_clock::now();
    tp.timestamp = move(tag_data.timestamp);
    edisender.push_tagpacket(move(tp));
}

int Main::start(int argc, char **argv)
{
    edi_conf.enable_pft = true;

    if (argc == 1) {
        usage();
        return 1;
    }

    int ch = 0;
    while (ch != -1) {
        ch = getopt(argc, argv, "c:C:d:p:s:S:t:Pf:i:Dva:b:w:x:h");
        switch (ch) {
            case -1:
                break;
            case 'c':
                source = optarg;
                break;
            case 'C':
                startupcheck = optarg;
                break;
            case 'd':
            case 's':
            case 'S':
            case 't':
            case 'p':
                parse_destination_args(ch);
                break;
            case 'P':
                edi_conf.enable_pft = false;
                break;
            case 'f':
                edi_conf.fec = std::stoi(optarg);
                break;
            case 'i':
                {
                    int interleave_percent = std::stoi(optarg);
                    if (interleave_percent != 0) {
                        if (interleave_percent < 0) {
                            throw std::runtime_error("EDI output: negative interleave value is invalid.");
                        }

                        edi_conf.fragment_spreading_factor = (double)interleave_percent / 100.0;
                    }
                }
                break;
            case 'D':
                edi_conf.dump = true;
                break;
            case 'v':
                edi_conf.verbose = true;
                break;
            case 'a':
                edi_conf.tagpacket_alignment = std::stoi(optarg);
                break;
            case 'b':
                backoff = std::chrono::milliseconds(std::stoi(optarg));
                break;
            case 'w':
                delay_ms = std::stoi(optarg);
                break;
            case 'x':
                drop_late_packets = true;
                drop_delay_ms = std::stoi(optarg);
                break;
            case 'h':
            default:
                usage();
                return 1;
        }
    }

    if (not startupcheck.empty()) {
        etiLog.level(info) << "Running startup check '" << startupcheck << "'";
        int wstatus = system(startupcheck.c_str());

        if (WIFEXITED(wstatus)) {
            if (WEXITSTATUS(wstatus) == 0) {
                etiLog.level(info) << "Startup check ok";
            }
            else {
                etiLog.level(error) << "Startup check failed, returned " << WEXITSTATUS(wstatus);
                return 1;
            }
        }
        else {
            etiLog.level(error) << "Startup check failed, child didn't terminate normally";
            return 1;
        }
    }

    add_edi_destination();

    if (source.empty()) {
        etiLog.level(error) << "source option is missing";
        return 1;
    }

    const auto pos_colon = source.find(":");
    if (pos_colon == string::npos or pos_colon == 0) {
        etiLog.level(error) << "source does not contain host:port";
        return 1;
    }

    const string connect_to_host = source.substr(0, pos_colon);
    const int connect_to_port = stod(source.substr(pos_colon+1));

    if (edi_conf.destinations.empty()) {
        etiLog.level(error) << "No EDI destinations set";
        return 1;
    }

    etiLog.level(info) << "Setting up EDI2EDI with delay " << delay_ms << " ms. " <<
        (drop_late_packets ? "Will" : "Will not") << " drop late packets (" << drop_delay_ms << " ms)";

    edisender.start(edi_conf, delay_ms, drop_late_packets, drop_delay_ms);
    edisender.print_configuration();

    try {
        while (running) {
            EdiDecoder::ETIDecoder edi_decoder(*this);

            edi_decoder.set_verbose(edi_conf.verbose);
            run(edi_decoder, connect_to_host, connect_to_port);
            if (not running) {
                break;
            }
            etiLog.level(info) << "Source disconnected, reconnecting and enabling output inhibit backoff";
            edisender.inhibit_until(chrono::steady_clock::now() + backoff);

            // There is no state inside the edisender or inside Main that we need to
            // clear.
        }
    }
    catch (const std::runtime_error& e) {
        etiLog.level(error) << "Caught exception: " << e.what();
        return 1;
    }

    return 0;
}

void Main::run(EdiDecoder::ETIDecoder& edi_decoder, const string& connect_to_host, int connect_to_port)
{
    Socket::TCPSocket sock;
    etiLog.level(info) << "Connecting to TCP " << connect_to_host << ":" << connect_to_port;
    try {
        sock.connect(connect_to_host, connect_to_port, 2000);
    }
    catch (const std::runtime_error& e) {
        etiLog.level(error) << "Error connecting to source: " << e.what();
        return;
    }

    ssize_t ret = 0;
    do {
        const size_t bufsize = 32;
        std::vector<uint8_t> buf(bufsize);
        try {
            ret = sock.recv(buf.data(), buf.size(), 0, 8000);
            if (ret > 0) {
                buf.resize(ret);
                std::vector<uint8_t> frame;
                edi_decoder.push_bytes(buf);
            }
        }
        catch (const Socket::TCPSocket::Interrupted&) {
        }
        catch (const Socket::TCPSocket::Timeout&) {
            etiLog.level(error) << "TCP receive timeout";
            ret = 0;
        }
    } while (running and ret > 0);
}

void Main::add_edi_destination()
{
    if (not dest_addr_set) {
        throw std::runtime_error("Destination address not specified for destination number " +
                std::to_string(edi_conf.destinations.size() + 1));
    }

    edi_conf.destinations.push_back(move(edi_destination));
    edi_destination = std::make_shared<edi::udp_destination_t>();

    source_port_set = false;
    source_addr_set = false;
    ttl_set = false;
    dest_addr_set = false;
    dest_port_set = false;
}

/* There is some state inside the parsing of destination arguments,
 * because several destinations can be given.  */
void Main::parse_destination_args(char option)
{
    if (not edi_destination) {
        edi_destination = std::make_shared<edi::udp_destination_t>();
    }

    switch (option) {
        case 'p':
            if (dest_port_set) {
                add_edi_destination();
            }
            edi_destination->dest_port = std::stoi(optarg);
            dest_port_set = true;
            break;
        case 's':
            if (source_port_set) {
                add_edi_destination();
            }
            edi_destination->source_port = std::stoi(optarg);
            source_port_set = true;
            break;
        case 'S':
            if (source_addr_set) {
                add_edi_destination();
            }
            edi_destination->source_addr = optarg;
            source_addr_set = true;
            break;
        case 't':
            if (ttl_set) {
                add_edi_destination();
            }
            edi_destination->ttl = std::stoi(optarg);
            ttl_set = true;
            break;
        case 'd':
            if (dest_addr_set) {
                add_edi_destination();
            }
            edi_destination->dest_addr = optarg;
            dest_addr_set = true;
            break;
        default:
            throw std::logic_error("parse_destination_args invalid");
    }
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

    cerr << "ODR-EDI2EDI " <<
#if defined(GITVERSION)
        GITVERSION <<
#else
        PACKAGE_VERSION <<
#endif
        " starting up" << endl;

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
        Main m;
        ret = m.start(argc, argv);

        // To make sure things get printed to stderr
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    catch (const std::runtime_error &e) {
        etiLog.level(error) << "Runtime error: " << e.what();
    }
    catch (const std::logic_error &e) {
        etiLog.level(error) << "Logic error! " << e.what();
    }

    return ret;
}

