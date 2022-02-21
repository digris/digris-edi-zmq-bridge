/*
   Copyright (C) 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009, 2010,
   2011, 2012 Her Majesty the Queen in Right of Canada (Communications
   Research Center Canada)

   Copyright (C) 2020
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

#include "EDISender.h"
#include "Log.h"
#include <cmath>
#include <cstring>
#include <iomanip>
#include <numeric>
#include <map>
#include <algorithm>
#include <limits>

// This is a remnant of the -x option
static const bool DROP_LATE = true;

using namespace std;

EDISender::~EDISender()
{
    _running.store(false);

    if (_process_thread.joinable()) {
        _process_thread.join();
    }
}

void EDISender::start(const edi::configuration_t& conf, const EDISenderSettings& settings)
{
    _edi_conf = conf;
    _settings = settings;

    _edi_sender = make_shared<edi::Sender>(_edi_conf);

    _running.store(true);
    _process_thread = thread(&EDISender::process, this);
}

void EDISender::update_settings(const EDISenderSettings& settings)
{
    _settings = settings;
}

void EDISender::push_tagpacket(tagpacket_t&& tp, Receiver* r)
{
    stringstream ss;
    ss << "EDISender ";
    const auto t_now = chrono::system_clock::now();
    const auto time_t_now = chrono::system_clock::to_time_t(t_now);
    char timestr[100];
    if (std::strftime(timestr, sizeof(timestr), "%Y-%m-%dZ%H:%M:%S", std::gmtime(&time_t_now))) {
        ss << timestr;
    }

    using namespace chrono;
    const auto t_frame = tp.timestamp.to_system_clock();
    const auto t_release = t_frame + milliseconds(_settings.delay_ms);
    const auto margin = t_release - t_now;
    const auto margin_ms = chrono::duration_cast<chrono::milliseconds>(margin).count();
    const bool late = t_release < t_now;

    std::unique_lock<std::mutex> lock(_mutex);
    ss << " P " << _pending_tagpackets.size() << " dlfc  " <<
        tp.dlfc << " margin " << margin_ms << " from " << tp.hostnames;

    // If we receive a packet we already handed off to the other thread
    if (_most_recent_timestamp.valid() and _most_recent_timestamp >= tp.timestamp) {
        ss << " dup&late";
        r->num_late++;
        num_dropped.fetch_add(1);
    }
    else if (not late) {
        const auto t_now_steady = steady_clock::now();
        const bool inhibited = t_now_steady < _output_inhibit_until;
        if (inhibited) {
            ss << " inh";
            num_dropped.fetch_add(1);
        }
        else {
            bool inserted = false;

            for (auto it = _pending_tagpackets.begin(); it != _pending_tagpackets.end(); ++it) {
                if (tp.timestamp < it->timestamp) {
                    _pending_tagpackets.insert(it, move(tp));
                    inserted = true;
                    ss << " new";
                    break;
                }
                else if (tp.timestamp == it->timestamp) {
                    if (tp.dlfc != it->dlfc) {
                        ss << " dlfc err";
                        etiLog.level(warn) << "Received packet " << tp.dlfc << " from "
                            << tp.hostnames <<
                            " with same timestamp but different DLFC than previous packet from "
                            << it->hostnames << " with " << it->dlfc;
                    }
                    else {
                        ss << " dup";
                        it->hostnames += ";" + tp.hostnames;
                    }

                    inserted = true;
                    break;
                }
            }

            if (not inserted) {
                _pending_tagpackets.push_back(move(tp));
            }

            if (late_score > 0) late_score--;
        }
    }
    else {
        ss << " late";
        r->num_late++;
        late_score += LATE_SCORE_INCREASE;
        if (late_score > LATE_SCORE_MAX) late_score = LATE_SCORE_MAX;
    }

    if (_pending_tagpackets.size() > MAX_PENDING_TAGPACKETS) {
        _pending_tagpackets.pop_front();
        num_queue_overruns.fetch_add(1);
        ss << " Drop ";
    }

    lock.unlock();
    ss << "\n";
    if (_settings.live_stats_port > 0) {
        try {
            Socket::UDPSocket udp;
            Socket::InetAddress addr;
            addr.resolveUdpDestination("127.0.0.1", _settings.live_stats_port);
            udp.send(ss.str(), addr);
        }
        catch (const runtime_error& e) { }
    }
}

void EDISender::print_configuration()
{
    if (_edi_conf.enabled()) {
        _edi_conf.print();
    }
    else {
        etiLog.level(info) << "EDI disabled";
    }
}

void EDISender::inhibit()
{
    using namespace std::chrono;
    etiLog.level(info) << "Output backoff for " << duration_cast<milliseconds>(_settings.backoff).count() << " ms";
    _output_inhibit_until = steady_clock::now() + _settings.backoff;

    {
        std::unique_lock<std::mutex> lock(_mutex);
        _pending_tagpackets.clear();
        late_score = 0;
    }
}

bool EDISender::is_running_ok() const
{
    return late_score < LATE_SCORE_THRESHOLD;
}

size_t EDISender::get_late_score() const
{
    std::unique_lock<std::mutex> lock(_mutex);
    return late_score;
}

void EDISender::reset_counters()
{
    num_dropped = 0;
    num_queue_overruns = 0;
    num_dlfc_discontinuities = 0;
    num_frames = 0;

    {
        std::unique_lock<std::mutex> lock(_mutex);
        late_score = 0;
    }
}

void EDISender::send_tagpacket(tagpacket_t& tp)
{
    // Wait until our time is tist_delay after the TIST before
    // we release that frame

    using namespace std::chrono;

    const auto t_frame = tp.timestamp.to_system_clock();
    const auto t_release = t_frame + milliseconds(_settings.delay_ms);
    const auto t_now = system_clock::now();

    const bool late = t_release < t_now;

    if (not late) {
        const auto wait_time = t_release - t_now;
        std::this_thread::sleep_for(wait_time);
    }

    if (late and DROP_LATE) {
        num_dropped.fetch_add(1);
        return;
    }

    const auto t_now_steady = steady_clock::now();
    const bool inhibited = t_now_steady < _output_inhibit_until;

    if (inhibited) {
        num_dropped.fetch_add(1);
        return;
    }

    if (_edi_sender and _edi_conf.enabled()) {
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
}

void EDISender::process()
{
    bool prev_dlfc_valid = false;
    uint16_t prev_dlfc = 0;

    while (_running.load()) {
        tagpacket_t tagpacket;

        {
            std::unique_lock<std::mutex> lock(_mutex);
            if (_pending_tagpackets.size() > 0) {
                tagpacket = _pending_tagpackets.front();
                _most_recent_timestamp = tagpacket.timestamp;
                _pending_tagpackets.pop_front();
            }
        }

        if (tagpacket.tagpacket.empty()) {
            this_thread::sleep_for(chrono::milliseconds(1));
            continue;
        }

        if (not _running.load()) {
            break;
        }

        if (prev_dlfc_valid and ((prev_dlfc + 1) % 5000) != tagpacket.dlfc) {
            cerr << "DLFC discontinuity " << prev_dlfc << " -> " << tagpacket.dlfc << "\n";
            num_dlfc_discontinuities.fetch_add(1);
            inhibit();
        }
        prev_dlfc = tagpacket.dlfc;
        prev_dlfc_valid = true;

        send_tagpacket(tagpacket);
    }
}
