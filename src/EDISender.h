/*
   Copyright (C) 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009, 2010,
   2011, 2012 Her Majesty the Queen in Right of Canada (Communications
   Research Center Canada)

   Copyright (C) 2022
   Matthias P. Braendli, matthias.braendli@mpb.li

    http://www.opendigitalradio.org
   */
/*
   This file is part of the ODR-mmbTools.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as
   published by the Free Software Foundation, either version 3 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once
#include <iostream>
#include <iterator>
#include <chrono>
#include <atomic>
#include <thread>
#include <mutex>
#include <list>
#include <vector>
#include "receiver.h"
#include "edioutput/TagItems.h"
#include "edioutput/TagPacket.h"
#include "edioutput/Transport.h"
#include "edi/common.hpp"

static constexpr size_t MAX_PENDING_TAGPACKETS = 1000;

constexpr long DEFAULT_BACKOFF = 5000;

struct EDISenderSettings {
    int live_stats_port = 0;
    int delay_ms = -500;
    bool drop_late = true;
    std::chrono::steady_clock::duration backoff = std::chrono::milliseconds(DEFAULT_BACKOFF);
};

class EDISender {
    public:
        EDISender() = default;
        EDISender(const EDISender& other) = delete;
        EDISender& operator=(const EDISender& other) = delete;
        ~EDISender();
        void start(const edi::configuration_t& conf, const EDISenderSettings& settings);
        void update_settings(const EDISenderSettings& settings);
        void push_tagpacket(tagpacket_t&& tagpacket, Receiver* r);
        void print_configuration(void);

        // Return true if the output is sending frames at the nominal rate, without excessive late packets
        bool is_running_ok() const;
        int backoff_milliseconds_remaining() const;

        ssize_t get_num_dropped() const { return num_dropped; }
        ssize_t get_num_queue_overruns() const { return num_queue_overruns; }
        ssize_t get_num_dlfc_discontinuities() const { return num_dlfc_discontinuities; }
        ssize_t get_frame_count() const { return num_frames; }
        size_t get_late_score() const;

        void reset_counters();

    private:
        void inhibit();
        void send_tagpacket(tagpacket_t& frame);
        void process(void);

        std::chrono::steady_clock::time_point _output_inhibit_until = std::chrono::steady_clock::now();

        edi::configuration_t _edi_conf;
        EDISenderSettings _settings;
        std::atomic<bool> _running;
        std::thread _process_thread;

        std::atomic<uint64_t> num_dropped = ATOMIC_VAR_INIT(0);
        std::atomic<uint64_t> num_queue_overruns = ATOMIC_VAR_INIT(0);
        std::atomic<uint64_t> num_dlfc_discontinuities = ATOMIC_VAR_INIT(0);
        std::atomic<uint64_t> num_frames = ATOMIC_VAR_INIT(0);

        std::shared_ptr<edi::Sender> _edi_sender;

        // All fields below protected by _mutex
        mutable std::mutex _mutex;
        // ordered by transmit timestamps
        std::list<tagpacket_t> _pending_tagpackets;
        EdiDecoder::frame_timestamp_t _most_recent_timestamp;

        // Every late packet increses the score by LATE_SCORE_INCREASE, every valid packet decreases by 1.
        // If we reach the threshold we are not ok anymore.
        const int LATE_SCORE_INCREASE = 10;
        const int LATE_SCORE_THRESHOLD = 100;
        const int LATE_SCORE_MAX = 200;
        int late_score = 0;

        bool _show_backoff_ended_message = false;

};
