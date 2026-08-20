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

#pragma once

#include <thread>
#include <atomic>
#include <map>
#include <string>

#include "ThreadsafeQueue.h"
#include "Socket.h"

class Resolver
{
    public:
        Resolver(bool verbose);
        Resolver(Resolver&) = delete;
        Resolver& operator=(Resolver&) = delete;
        virtual ~Resolver();

        int get_poll_sockfd() const;

        void request_resolve(const std::string& hostname, int port);

        struct ResolveResult {
            std::string hostname = "";
            int port = 0;
            Socket::InetAddress addr;
        };
        std::vector<ResolveResult> get_resolve_results();

    private:
        void run();

        bool verbose;

        // We use pipe() because that's something we can poll(),
        // even though we carry no information in the pipe itself.
        int m_pipe_fildes[2];

        std::thread m_thread;
        std::atomic<bool> running = ATOMIC_VAR_INIT(true);

        enum class RequestState { Pending, Resolved };

        struct RequestHostPort {
            std::string hostname = "";
            int port = 0;

            auto operator<=>(const RequestHostPort &) const = default;
        };

        struct ResolveRequest {
            RequestState state = RequestState::Pending;

            Socket::InetAddress addr;
        };

        std::mutex requests_mutex;
        std::map<RequestHostPort, ResolveRequest> requests;

        std::condition_variable new_request_condvar;
};

