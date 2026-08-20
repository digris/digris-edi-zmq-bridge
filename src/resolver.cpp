/*
   Copyright (C) 2026
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

#include "resolver.h"
#include "utils.h"
#include "Log.h"

Resolver::Resolver(bool verbose) :
    verbose(verbose)
{
    if (pipe(m_pipe_fildes) < 0) {
        throw std::runtime_error("Resolver: Cannot create pipe");
    }

    m_thread = std::thread(&Resolver::run, this);
}

Resolver::~Resolver()
{
    running = false;

    new_request_condvar.notify_one();
    if (m_thread.joinable()) {
        m_thread.join();
    }

    close(m_pipe_fildes[0]);
    close(m_pipe_fildes[1]);
}

int Resolver::get_poll_sockfd() const
{
    return m_pipe_fildes[0];
}

void Resolver::request_resolve(const std::string& hostname, int port)
{
    if (verbose)
        etiLog.level(debug) << "Resolve " << hostname << ":" << port;

    {
        std::unique_lock lock(requests_mutex);

        RequestHostPort host_port{hostname, port};

        if (requests.contains(host_port)) {
            return;
        }

        requests[host_port] = {};
    }
    new_request_condvar.notify_one();
}


std::vector<Resolver::ResolveResult> Resolver::get_resolve_results()
{
    std::vector<ResolveResult> results;

    {
        std::unique_lock<std::mutex> lock(requests_mutex);

        for (auto& req : requests) {
            if (req.second.state == RequestState::Resolved) {
                results.push_back({
                        req.first.hostname,
                        req.first.port,
                        req.second.addr
                        });
            }
        }

        std::erase_if(requests, [](const auto& req) { return req.second.state == RequestState::Resolved; });
    }

    return results;
}

void Resolver::run()
{
    set_thread_name("resolver");

    while (running) {
        std::vector<RequestHostPort> pending_requests;

        {
            std::unique_lock<std::mutex> lock(requests_mutex);

            while (pending_requests.size() == 0 and running) {
                for (auto& req : requests) {
                    if (req.second.state == RequestState::Pending) {
                        pending_requests.push_back(req.first);
                    }
                }

                if (pending_requests.size() == 0) {
                    new_request_condvar.wait(lock);
                }
            }
        }

        for (auto& req : pending_requests) {
            if (verbose)
                etiLog.level(debug) << "Resolving " << req.hostname << ":" << req.port;

            try {
                Socket::InetAddress addr;
                addr.resolveTcpDestination(req.hostname, req.port);

                if (verbose) {
                    etiLog.level(debug) << "Resolved " << req.hostname << ":" << req.port <<
                        " to " << addr.to_string();
                }

                {
                    std::unique_lock<std::mutex> lock(requests_mutex);
                    auto& rr = requests.at(req);
                    rr.addr = std::move(addr);
                    rr.state = RequestState::Resolved;
                }

                char byte = 1;
                (void)write(m_pipe_fildes[1], &byte, 1);
            }
            catch (const std::runtime_error& e) {
                etiLog.level(error) << "Failed to resolve " << req.hostname << ":" << req.port <<
                    ": " << e.what();

                {
                    std::unique_lock<std::mutex> lock(requests_mutex);
                    requests.erase(req);
                }
            }
        }

    }
}
