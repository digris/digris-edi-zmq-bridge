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
    cerr << " -m (merge|switch)         Choose input merging or switching mode. (default: merge)\n";
    cerr << " --switch-delay <ms>       Set the delay after an input interruption before switching (default: " << DEFAULT_SWITCH_DELAY << " ms).\n";
    cerr << " -c <host:port>            Connect to given host and port using TCP.\n";
    cerr << " -F <host:port>            Add fallback input to given host and port using TCP.\n";
    cerr << " -w <delay>                Keep every ETI frame until TIST is <delay> milliseconds after current system time.\n";
    cerr << "                           Negative delay values are also allowed.\n";
    cerr << " -C <path to script>       Before starting, run the given script, and only start if it returns 0.\n";
    cerr << "                           This is useful for checking that NTP is properly synchronised\n";
    cerr << " -P                        Disable PFT and send AFPackets.\n";
    cerr << " -f <fec>                  Set the FEC.\n";
    cerr << " -i <interleave>           Configure the interleaver with given interleave percentage: 0% send all fragments at once, 100% spread over 24ms, >100% spread and interleave. Default 95%\n";
    cerr << " -D                        Dump the EDI to edi.debug file.\n";
    cerr << " -v                        Enables verbose mode.\n";
    cerr << " -a <alignement>           Set the alignment of the TAG Packet (default 8).\n";
    cerr << " -b <backoff>              Number of milliseconds to backoff after an interruption (default " << DEFAULT_BACKOFF << ").\n";
    cerr << " -r <socket_path>          Enable UNIX DGRAM remote control socket and bind to given path\n";
    cerr << " --version                 Show the version and quit.\n\n";

    cerr << "The following options can be given several times, when more than UDP destination is desired:\n";
    cerr << " -p <destination port>     Set the destination port.\n";
    cerr << " -d <destination ip>       Set the destination ip.\n";
    cerr << " -s <source port>          Set the source port.\n";
    cerr << " -S <source ip>            Select the source IP in case we want to use multicast.\n";
    cerr << " -t <ttl>                  Set the packet's TTL.\n\n";

    cerr << "Debugging utilities\n";
    cerr << " --live-stats-port <port>  Send live statistics to UDP 127.0.0.1:PORT. Receive with nc -uklp PORT\n\n";

    cerr << "It is best practice to run this tool under a process supervisor that will restart it automatically." << endl;
}

static const struct option longopts[] = {
    {"switch-delay", required_argument, 0, 1},
    {"live-stats-port", required_argument, 0, 2},
    {0, 0, 0, 0}
};


int Main::start(int argc, char **argv)
{
    edi_conf.enable_pft = true;

    if (argc == 1) {
        usage();
        return 1;
    }

    int ch = 0;
    int index = 0;
    while (ch != -1) {
        ch = getopt_long(argc, argv, "c:C:d:F:m:p:r:s:S:t:Pf:i:Dva:b:w:x:h", longopts, &index);
        switch (ch) {
            case -1:
                break;
            case 1: // --switch-delay
                switch_delay = chrono::milliseconds(stoi(optarg));
                break;
            case 2: // --live-stats-port
                edisendersettings.live_stats_port = stoi(optarg);
                break;
            case 'm':
                if (strcmp(optarg, "switch") == 0) {
                    mode = Mode::Switching;
                }
                else if (strcmp(optarg, "merge") == 0) {
                    mode = Mode::Merging;
                }
                else {
                    etiLog.level(error) << "Invalid mode selected";
                    return 1;
                }
                break;
            case 'c':
            case 'F':
                {
                    string optarg_s = optarg;
                    const auto pos_colon = optarg_s.find(":");
                    if (pos_colon == string::npos or pos_colon == 0) {
                        etiLog.level(error) << "source does not contain host:port";
                        return 1;
                    }

                    const bool enabled = ch == 'c';
                    sources.push_back({
                            optarg_s.substr(0, pos_colon),
                            stoi(optarg_s.substr(pos_colon+1)),
                            enabled});
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
                edi_conf.fec = stoi(optarg);
                break;
            case 'i':
                {
                    int interleave_percent = stoi(optarg);
                    if (interleave_percent != 0) {
                        if (interleave_percent < 0) {
                            throw runtime_error("EDI output: negative interleave value is invalid.");
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
                edi_conf.tagpacket_alignment = stoi(optarg);
                break;
            case 'b':
                backoff = chrono::milliseconds(stoi(optarg));
                break;
            case 'w':
                edisendersettings.delay_ms = stoi(optarg);
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

    size_t num_enabled = count_if(sources.cbegin(), sources.cend(), [](const source_t& src) { return src.enabled; });
    if (num_enabled == 0) {
        etiLog.level(warn) << "Starting up with zero enabled sources. Did you forget to add a -c option?";
    }

    if (edi_conf.destinations.empty()) {
        etiLog.level(error) << "No EDI destinations set";
        return 1;
    }

    etiLog.level(info) << "Setting up EDI2EDI with delay " << edisendersettings.delay_ms << " ms. ";

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

    receivers.reserve(16); // Ensure the receivers don't get moved around, as their edi_decoder needs their address
    for (auto& source : sources) {
        auto callback = [&](tagpacket_t&& tp, Receiver* r) { edisender.push_tagpacket(move(tp), r); };
        receivers.emplace_back(source, callback, edi_conf.verbose);
    }


    // 15 because RC can consume an additional slot in struct pollfd fds below
    if (receivers.size() > 15) {
        etiLog.level(error) << "Max 15 sources supported";
        return 1;
    }

    edisender.start(edi_conf, edisendersettings);
    edisender.print_configuration();

    if (mode == Mode::Switching) {
        ensure_one_active();
    }

    try {
        do {
            switch (mode) {
                case Mode::Switching:
                    {
                        using namespace chrono;
                        const auto now = steady_clock::now();

                        if (std::count_if(receivers.cbegin(), receivers.cend(),
                                [](const Receiver& r) { return r.source.active; }) != 1) {
                            etiLog.level(error) << "Switching error: more than one input active";
                        }

                        // Assumes only one active
                        for (auto rx = receivers.begin(); rx != receivers.end(); ++rx) {
                            if (rx->source.active) {
                                bool force_switch = false;

                                // Changed through RC
                                if (rx->source.active and not rx->source.enabled) {
                                    etiLog.level(info) << "Unset " << rx->source.hostname << " active ";
                                    rx->source.active = false;
                                    force_switch = true;
                                }

                                auto packet_age = duration_cast<milliseconds>(now - rx->get_time_last_packet());
                                if (force_switch or packet_age > switch_delay) {
                                    bool switched = false;
                                    auto rx2 = rx;
                                    do {
                                        // Rotate through the sources
                                        ++rx2;
                                        if (rx2 == receivers.end()) {
                                            rx2 = receivers.begin();
                                        }

                                        if (rx2 != rx and rx2->source.enabled) {
                                            rx->source.active = false;
                                            rx2->source.active = true;
                                            switched = true;

                                            etiLog.level(warn) << "Switching from " <<
                                                rx->source.hostname << ":" << rx->source.port <<
                                                " to " <<
                                                rx2->source.hostname << ":" << rx2->source.port <<
                                                " because of lack of data";
                                            break;
                                        }
                                    } while (rx2 != rx);

                                    if (switched) {
                                        edisender.inhibit_for(backoff);
                                    }
                                    else {
                                        ensure_one_active();
                                    }
                                }
                                break;
                            }
                        }

                    }
                    break;
                case Mode::Merging:
                    for (auto& source : sources) {
                        source.active = source.enabled;
                    }
                    break;
            }


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

            int retval = poll(fds, num_fds, 240);

            if (retval == -1 and errno == EINTR) {
                running = 0;
            }
            else if (retval == -1) {
                string errstr(strerror(errno));
                throw runtime_error("poll() error: " + errstr);
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
                num_poll_timeout++;
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

void Main::ensure_one_active()
{
    if (std::count_if(receivers.cbegin(), receivers.cend(), [](const Receiver& r) { return r.source.active; }) == 0) {
        // Activate the first enabled source
        for (auto& source : sources) {
            if (source.enabled) {
                etiLog.level(info) << "Activating first input " << source.hostname << ":" << source.port;
                source.active = true;
                break;
            }
        }
    }
}

void Main::add_edi_destination()
{
    if (not dest_addr_set) {
        throw runtime_error("Destination address not specified for destination number " +
                to_string(edi_conf.destinations.size() + 1));
    }

    edi_conf.destinations.push_back(move(edi_destination));
    edi_destination = make_shared<edi::udp_destination_t>();

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
        edi_destination = make_shared<edi::udp_destination_t>();
    }

    switch (option) {
        case 'p':
            if (dest_port_set) {
                add_edi_destination();
            }
            edi_destination->dest_port = stoi(optarg);
            dest_port_set = true;
            break;
        case 's':
            if (source_port_set) {
                add_edi_destination();
            }
            edi_destination->source_port = stoi(optarg);
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
            edi_destination->ttl = stoi(optarg);
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
            throw logic_error("parse_destination_args invalid");
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
        string errstr(strerror(errno));
        throw runtime_error("RC socket: Could not get socket flags: " + errstr);
    }

    if (fcntl(rc_socket, F_SETFL, flags | O_NONBLOCK) == -1) {
        string errstr(strerror(errno));
        throw runtime_error("RC socket: Could not set O_NONBLOCK: " + errstr);
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

    vector<uint8_t> buf(1024);
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
        catch (const exception& e) {
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

string Main::handle_rc_command(const string& cmd)
{
    string r = "";

    if (cmd.rfind("get settings", 0) == 0) {
        using namespace chrono;
        stringstream ss;
        ss << "{ \"delay\": " << edisendersettings.delay_ms <<
            ", \"backoff\": " << duration_cast<milliseconds>(backoff).count() <<
            ", \"live_stats_port\": " << edisendersettings.live_stats_port <<
            "}";
        r = ss.str();
    }
    else if (cmd.rfind("stats", 0) == 0) {
        stringstream ss;
        ss << "{ \"inputs\": [\n";
        for (auto it = receivers.begin(); it != receivers.end();) {

            auto rx_packet_time =
                chrono::system_clock::to_time_t(it->get_systime_last_packet());

            ss << "{" <<
                  " \"hostname\": \"" << it->source.hostname << "\"," <<
                  " \"port\": " << it->source.port << "," <<
                  " \"last_packet_received_at\": " << rx_packet_time << "," <<
                  " \"connected\": " << (it->source.connected ? "true" : "false") << "," <<
                  " \"active\": " << (it->source.active ? "true" : "false") << "," <<
                  " \"enabled\": " << (it->source.enabled ? "true" : "false") << ",";

            ss << " \"stats\": {" <<
                  " \"margin\": " << it->get_margin_ms() << "," <<
                  " \"num_late\": " << it->num_late << "," <<
                  " \"num_connects\": " << it->source.num_connects <<
                  " } }";

            ++it;
            if (it == receivers.end()) {
                ss << "\n";
            }
            else {
                ss << ",\n";
            }
        }
        ss << "],\n";

        ss << " \"main\": { \"poll_timeouts\": " << num_poll_timeout << " },";

        ss << " \"output\": {"
            " \"frames\": " << edisender.get_frame_count() << "," <<
            " \"num_dlfc_discontinuities\": " << edisender.get_num_dlfc_discontinuities() << "," <<
            " \"num_queue_overruns\": " << edisender.get_num_queue_overruns() << "," <<
            " \"num_dropped\": " << edisender.get_num_dropped() <<
            "} }";

        r = ss.str();
    }
    else if (cmd.rfind("set input enable ", 0) == 0) {
        auto input = cmd.substr(17, cmd.size());
        bool found = false;
        for (auto& source : sources) {
            if (source.hostname + ":" + to_string(source.port) == input) {
                source.enabled = true;
                etiLog.level(info) << "RC enabling input " << input;
                found = true;
                break;
            }
        }

        if (not found) {
            etiLog.level(info) << "RC disable input " << input << " impossible: input not found.";
            throw invalid_argument("Cannot find specified input");
        }
    }
    else if (cmd.rfind("set input disable ", 0) == 0) {
        auto input = cmd.substr(18, cmd.size());
        bool found = false;
        for (auto& source : sources) {
            if (source.hostname + ":" + to_string(source.port) == input) {
                source.enabled = false;
                etiLog.level(info) << "RC disabling input " << input;
                found = true;
                break;
            }
        }

        if (not found) {
            etiLog.level(info) << "RC disable input " << input << " impossible: input not found.";
            throw invalid_argument("Cannot find specified input");
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
    else if (cmd.rfind("set backoff ", 0) == 0) {
        auto value = stoi(cmd.substr(12, cmd.size()));
        if (value < 0 or value > 100000) {
            throw invalid_argument("backoff value out of bounds 0 to 100s");
        }

        etiLog.level(info) << "RC setting backoff to " << value;
        backoff = chrono::milliseconds(value);
    }
    else if (cmd.rfind("set live_stats_port ", 0) == 0) {
        auto value = stoi(cmd.substr(20, cmd.size()));
        if (value < 0 or value > 65535) {
            throw invalid_argument("udp_live_stats_port value out of bounds");
        }
        edisendersettings.live_stats_port = value;
        edisender.update_settings(edisendersettings);
        etiLog.level(info) << "RC setting udp_live_stats_port to " << value;
    }
    else {
        throw runtime_error("Unknown command");
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

