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

using namespace std;

volatile sig_atomic_t running = 1;

static std::chrono::steady_clock::duration backoff = std::chrono::milliseconds(DEFAULT_BACKOFF);

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
    cerr << " -r <socket_path>      Enable UNIX DGRAM remote control socket and bind to given path\n";
    cerr << " --version             Show the version and quit.\n\n";

    cerr << "The following options can be given several times, when more than UDP destination is desired:\n";
    cerr << " -p <destination port> Set the destination port.\n";
    cerr << " -d <destination ip>   Set the destination ip.\n";
    cerr << " -s <source port>      Set the source port.\n";
    cerr << " -S <source ip>        Select the source IP in case we want to use multicast.\n";
    cerr << " -t <ttl>              Set the packet's TTL.\n\n";

    cerr << "It is best practice to run this tool under a process supervisor that will restart it automatically." << endl;
}

Receiver::Receiver(const source_t& source, EDISender& edi_sender, bool verbose) :
    source(source),
    edi_sender(edi_sender),
    edi_decoder(*this)
{
    edi_decoder.set_verbose(verbose);

    etiLog.level(info) << "Connecting to TCP " << source.hostname << ":" << source.port;
    sock.connect(source.hostname, source.port, /*nonblock*/ true);
}

void Receiver::update_fc_data(const EdiDecoder::eti_fc_data& fc_data) {
    dlfc = fc_data.dlfc;
}

void Receiver::assemble(EdiDecoder::ReceivedTagPacket&& tag_data) {
    tagpacket_t tp;
    tp.source = source;
    tp.seq = tag_data.seq;
    tp.dlfc = dlfc;
    tp.tagpacket = move(tag_data.tagpacket);
    tp.received_at = std::chrono::steady_clock::now();
    tp.timestamp = move(tag_data.timestamp);
    edi_sender.push_tagpacket(move(tp));
}

void Receiver::tick()
{
    // source.enabled gets modified by RC
    if (source.enabled) {
        if (not sock.valid()) {
            etiLog.level(info) << "Reconnecting to TCP " << source.hostname << ":" << source.port;
            sock.connect(source.hostname, source.port, /*nonblock*/ true);
        }
    }
    else {
        if (sock.valid()) {
            etiLog.level(info) << "Disconnecting from TCP " << source.hostname << ":" << source.port;
            sock.close();
        }
    }
}

void Receiver::receive()
{
    const size_t bufsize = 32;
    std::vector<uint8_t> buf(bufsize);
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
            std::string errstr(strerror(errno));
            throw std::runtime_error("TCP receive after poll() error: " + errstr);
        }
    }
    else if (ret > 0) {
        buf.resize(ret);
        edi_decoder.push_bytes(buf);
        success = true;
    }
    // ret == 0 means disconnected

    if (not success) {
        sock.close();
        etiLog.level(info) << "Source disconnected, reconnecting and enabling output inhibit backoff";
        edi_sender.inhibit_until(chrono::steady_clock::now() + backoff);
    }
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
        ch = getopt(argc, argv, "c:C:d:p:r:s:S:t:Pf:i:Dva:b:w:x:h");
        switch (ch) {
            case -1:
                break;
            case 'c':
                {
                    string optarg_s = optarg;
                    const auto pos_colon = optarg_s.find(":");
                    if (pos_colon == string::npos or pos_colon == 0) {
                        etiLog.level(error) << "source does not contain host:port";
                        return 1;
                    }

                    const bool enabled = true;
                    sources.push_back({optarg_s.substr(0, pos_colon), stoi(optarg_s.substr(pos_colon+1)), enabled});
                }
                break;
            case 'C':
                startupcheck = optarg;
                break;
            case 'r':
                rc_socket_name = optarg;
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
                edisendersettings.delay_ms = std::stoi(optarg);
                break;
            case 'x':
                edisendersettings.drop_late = true;
                edisendersettings.drop_delay_ms = std::stoi(optarg);
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

    if (sources.empty()) {
        etiLog.level(error) << "No sources given";
        return 1;
    }

    if (edi_conf.destinations.empty()) {
        etiLog.level(error) << "No EDI destinations set";
        return 1;
    }

    etiLog.level(info) << "Setting up EDI2EDI with delay " << edisendersettings.delay_ms << " ms. " <<
        (edisendersettings.drop_late ? "Will" : "Will not") <<
        " drop late packets (" << edisendersettings.drop_delay_ms << " ms)";

    if (not rc_socket_name.empty()) {
        try {
            init_rc();
        }
        catch (const runtime_error& e)
        {
            etiLog.level(error) << "RC socket init failed: " << e.what();
            return 1;
        }
    }

    vector<Receiver> receivers;
    receivers.reserve(16); // Ensure the receivers don't get moved around, as their edi_decoder needs their address
    for (const auto& source : sources) {
        receivers.emplace_back(source, edisender, edi_conf.verbose);
    }


    // 15 because RC can consume an additional slot in struct pollfd fds below
    if (receivers.size() > 15) {
        etiLog.level(error) << "Max 15 sources supported";
        return 1;
    }

    edisender.start(edi_conf, edisendersettings);
    edisender.print_configuration();

    try {
        do {
            size_t num_fds = 0;
            struct pollfd fds[16];
            unordered_map<int, Receiver*> sockfd_to_receiver;
            for (auto& rx : receivers) {

                rx.tick();

                int fd = rx.get_sockfd();
                if (fd != -1) {
                    fds[num_fds].fd = fd;
                    fds[num_fds].events = POLLIN;
                    num_fds++;

                    sockfd_to_receiver.emplace(fd, &rx);
                }
            }

            if (rc_socket != -1) {
                fds[num_fds].fd = rc_socket;
                fds[num_fds].events = POLLIN;
                num_fds++;
            }

            int retval = poll(fds, num_fds, 8000);

            if (retval == -1 and errno == EINTR) {
                running = 0;
            }
            else if (retval == -1) {
                std::string errstr(strerror(errno));
                throw std::runtime_error("poll() error: " + errstr);
            }
            else if (retval > 0) {
                for (size_t i = 0; i < num_fds; i++) {
                    if (fds[i].revents & POLLIN) {
                        if (rc_socket != 1 and fds[i].fd == rc_socket) {
                            handle_rc_request();
                        }
                        else {
                            // This can throw out_of_range, which is a logic_error and should never happen
                            sockfd_to_receiver.at(fds[i].fd)->receive();
                        }
                    }
                }
            }
            else {
                etiLog.level(error) << "TCP receive timeout";
            }
        } while (running);
    }
    catch (const runtime_error& e) {
        etiLog.level(error) << "Caught exception: " << e.what();
        return 1;
    }
    catch (const logic_error& e) {
        etiLog.level(error) << "Caught logic error: " << e.what();
        return 1;
    }

    return 0;
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

void Main::init_rc()
{
    rc_socket = ::socket(AF_UNIX, SOCK_DGRAM, 0);
    if (rc_socket == -1) {
        throw runtime_error("RC socket creation failed: " + string(strerror(errno)));
    }

#if 0
    int flags = fcntl(rc_socket, F_GETFL);
    if (flags == -1) {
        std::string errstr(strerror(errno));
        throw std::runtime_error("RC socket: Could not get socket flags: " + errstr);
    }

    if (fcntl(rc_socket, F_SETFL, flags | O_NONBLOCK) == -1) {
        std::string errstr(strerror(errno));
        throw std::runtime_error("RC socket: Could not set O_NONBLOCK: " + errstr);
    }
#endif

    struct sockaddr_un claddr;
    memset(&claddr, 0, sizeof(struct sockaddr_un));
    claddr.sun_family = AF_UNIX;
    snprintf(claddr.sun_path, sizeof(claddr.sun_path), "%s", rc_socket_name.c_str());

    (void)unlink(rc_socket_name.c_str());
    int ret = ::bind(rc_socket, (const struct sockaddr *) &claddr, sizeof(struct sockaddr_un));
    if (ret == -1) {
        throw runtime_error("RC socket bind failed " + string(strerror(errno)));
    }
}

bool Main::handle_rc_request()
{
    struct sockaddr_un claddr;
    memset(&claddr, 0, sizeof(struct sockaddr_un));
    claddr.sun_family = AF_UNIX;
    socklen_t claddr_len = sizeof(struct sockaddr_un);

    std::vector<uint8_t> buf(1024);
    ssize_t ret = ::recvfrom(rc_socket, buf.data(), buf.size(), 0, (struct sockaddr*)&claddr, &claddr_len);
    if (ret == -1) {
        if (errno == EINTR) {
            return false;
        }
        else {
            throw runtime_error(string("Can't receive RC data: ") + strerror(errno));
        }
    }
    else if (ret == 0) {
        etiLog.level(error) << "RC socket recvfrom returned 0!";
        return false;
    }
    else {
        buf.resize(ret);
        const auto cmd = string{buf.begin(), buf.end()};
        string response;

        try {
            const string cmd_response = handle_rc_command(cmd);
            stringstream ss;
            ss << "{\"status\": \"ok\", \"cmd\": \"" + cmd + "\"";
            if (not cmd_response.empty()) {
                ss << ", \"response\": " << cmd_response;
            }
            ss << "}";
            response = ss.str();
        }
        catch (const std::exception& e) {
            response = "{\"status\": \"error\", \"cmd\": \"" + cmd + "\", \"message\": \"" + e.what() + "\"}";
        }

        ssize_t ret = ::sendto(rc_socket, response.data(), response.size(), 0,
                (struct sockaddr*)&claddr, sizeof(struct sockaddr_un));
        if (ret == -1) {
            etiLog.level(warn) << "Could not send response to RC: " << strerror(errno);
        }
        else if (ret != (ssize_t)response.size()) {
            etiLog.log(warn, "RC response short send: %zu bytes of %zu transmitted",
                    ret, response.size());
        }

        return true;
    }
}

std::string Main::handle_rc_command(const std::string& cmd)
{
    string r = "";

    if (cmd.rfind("get settings", 0) == 0) {
        using namespace chrono;
        stringstream ss;
        ss << "{ \"delay\": " << edisendersettings.delay_ms <<
            ", \"drop-late\": " << (edisendersettings.drop_late ? "true" : "false") <<
            ", \"drop-delay\": " << edisendersettings.drop_delay_ms <<
            ", \"backoff\": " << duration_cast<milliseconds>(backoff).count() <<
            "}";
        r = ss.str();
    }
    else if (cmd.rfind("list inputs", 0) == 0) {
        stringstream ss;
        ss << "[\n";
        for (auto it = sources.begin() ; it != sources.end();) {
            ss << " {";

            ss << " \"hostname\": \"" << it->hostname << "\"," <<
                  " \"port\": \"" << it->port << "\"," <<
                  " \"enabled\": " << (it->enabled ? "true" : "false");

            ++it;
            if (it == sources.end()) {
                ss << " }\n";
            }
            else {
                ss << " },\n";
            }
        }
        ss << "]";
        r = ss.str();
    }
    else if (cmd.rfind("set input enable ", 0) == 0) {
        auto input = cmd.substr(17, cmd.size());
        for (auto& source : sources) {
            if (source.hostname + ":" + to_string(source.port) == input) {
                source.enabled = true;
                etiLog.level(info) << "RC enabling input " << input;
                break;
            }
        }
    }
    else if (cmd.rfind("set input disable ", 0) == 0) {
        auto input = cmd.substr(18, cmd.size());
        for (auto& source : sources) {
            if (source.hostname + ":" + to_string(source.port) == input) {
                source.enabled = false;
                etiLog.level(info) << "RC disabling input " << input;
                break;
            }
        }
    }
    else if (cmd.rfind("set delay ", 0) == 0) {
        auto value = stoi(cmd.substr(10, cmd.size()));
        if (value < -100000 or value > 100000) {
            throw invalid_argument("delay value out of bounds +/- 100s");
        }
        edisendersettings.delay_ms = value;
        edisender.update_settings(edisendersettings);
        etiLog.level(info) << "RC setting delay to " << value;
    }
    else if (cmd.rfind("set drop-delay ", 0) == 0) {
        auto value = stoi(cmd.substr(15, cmd.size()));
        if (value < -100000 or value > 100000) {
            throw invalid_argument("delay value out of bounds +/- 100s");
        }
        edisendersettings.drop_delay_ms = value;
        edisender.update_settings(edisendersettings);
        etiLog.level(info) << "RC setting drop-delay to " << value;
    }
    else if (cmd.rfind("set drop-late ", 0) == 0) {
        auto value = stoi(cmd.substr(14, cmd.size()));
        if (value == 0 or value == 1) {
            edisendersettings.drop_late = value;
            edisender.update_settings(edisendersettings);
            etiLog.level(info) << "RC setting drop-late to " << value;
        }
        else {
            throw invalid_argument("value must be 0 or 1");
        }
    }
    else if (cmd.rfind("set backoff ", 0) == 0) {
        auto value = stoi(cmd.substr(12, cmd.size()));
        if (value < 0 or value > 100000) {
            throw invalid_argument("backoff value out of bounds 0 to 100s");
        }

        etiLog.level(info) << "RC setting backoff to " << value;
        backoff = std::chrono::milliseconds(value);
    }

    return r;
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

