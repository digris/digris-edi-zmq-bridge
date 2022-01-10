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
#include "ThreadsafeQueue.h"
#include <cmath>
#include <cstring>
#include <iomanip>
#include <numeric>
#include <map>
#include <algorithm>
#include <limits>

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

void EDISender::push_tagpacket(tagpacket_t&& tp)
{
    std::unique_lock<std::mutex> lock(_pending_tagpackets_mutex);
    bool inserted = false;
    for (auto it = _pending_tagpackets.begin(); it != _pending_tagpackets.end(); ++it) {
        if (tp.timestamp < it->timestamp) {
            _pending_tagpackets.insert(it, move(tp));
            inserted = true;
            break;
        }
        else if (tp.timestamp == it->timestamp) {
            if (tp.dlfc != it->dlfc) {
                etiLog.level(warn) << "Received packet " << tp.dlfc << " from "
                    << tp.hostname << ":" << tp.port <<
                    " with same timestamp but different DLFC than previous packet from "
                    << it->hostname << ":" << it->port << " with " << it->dlfc;
            }


#warning "TODO statistics"

            inserted = true;
            break;
        }
    }

    if (not inserted) {
        _pending_tagpackets.push_back(move(tp));
    }

    if (_pending_tagpackets.size() > MAX_PENDING_TAGPACKETS) {
        _pending_tagpackets.pop_front();
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

void EDISender::send_tagpacket(tagpacket_t& tp)
{
    // Wait until our time is tist_delay after the TIST before
    // we release that frame

    using namespace std::chrono;

    const auto t_frame = tp.timestamp.to_system_clock();
    const auto t_release = t_frame + milliseconds(_settings.delay_ms);
    const auto t_latest_release = t_frame + milliseconds(_settings.drop_delay_ms);
    const auto t_now = system_clock::now();

    const bool slightly_late = t_release < t_now;
    const bool late = t_latest_release < t_now;

    buffering_stat_t stat;
    stat.late = slightly_late;

    if (not slightly_late) {
        const auto wait_time = t_release - t_now;
        std::this_thread::sleep_for(wait_time);
    }

    const auto t_now_steady = steady_clock::now();
    stat.inhibited = t_now_steady < _output_inhibit_until;
    stat.dropped = late and _settings.drop_late;
    stat.buffering_time_us = duration_cast<microseconds>(t_now_steady - tp.received_at).count();
    _buffering_stats.push_back(std::move(stat));

    if (late and _settings.drop_late) {
        return;
    }

    if (stat.inhibited) {
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
    }
}

void EDISender::process()
{
    while (_running.load()) {
        tagpacket_t tagpacket;
        bool valid = false;

        {
            std::unique_lock<std::mutex> lock(_pending_tagpackets_mutex);
            if (_pending_tagpackets.size() > 0) {
                tagpacket = _pending_tagpackets.front();
                valid = true;
                _pending_tagpackets.pop_front();
            }
        }

        if (not valid) {
            this_thread::sleep_for(chrono::milliseconds(1));
            continue;
        }

        if (not _running.load()) {
            break;
        }

        const uint16_t dlfc = tagpacket.dlfc;
        const auto tsta = tagpacket.timestamp.tsta;
        send_tagpacket(tagpacket);

        if (dlfc % 250 == 0) { // every six seconds
            const double n = _buffering_stats.size();

            size_t num_late = std::count_if(_buffering_stats.begin(), _buffering_stats.end(),
                    [](const buffering_stat_t& s){ return s.late; });

            size_t num_dropped = std::count_if(_buffering_stats.begin(), _buffering_stats.end(),
                    [](const buffering_stat_t& s){ return s.dropped; });

            size_t num_inhibited = std::count_if(_buffering_stats.begin(), _buffering_stats.end(),
                    [](const buffering_stat_t& s){ return s.inhibited; });

            double sum = 0.0;
            double min = std::numeric_limits<double>::max();
            double max = -std::numeric_limits<double>::max();
            for (const auto& s : _buffering_stats) {
                // convert to milliseconds
                const double t = s.buffering_time_us / 1000.0;
                sum += t;

                if (t < min) {
                    min = t;
                }

                if (t > max) {
                    max = t;
                }
            }
            double mean = sum / n;

            double sq_sum = 0;
            for (const auto& s : _buffering_stats) {
                const double t = s.buffering_time_us / 1000.0;
                sq_sum += (t-mean) * (t-mean);
            }
            double stdev = sqrt(sq_sum / n);

            /* Debug code
            stringstream ss;
            ss << "times:";
            for (const auto t : _buffering_stats) {
                ss << " " << lrint(t.buffering_time_us / 1000.0);
            }
            etiLog.level(debug) << ss.str();
            // */

            etiLog.level(info) << "Buffering time statistics for " <<
                _buffering_stats.size() << " frames [milliseconds]:" <<
                " min: " << min <<
                " max: " << max <<
                " mean: " << mean <<
                " stdev: " << stdev <<
                " late: " << num_late <<
                " dropped: " << num_dropped <<
                " inhibited: " << num_inhibited <<
                " Frame 0 TS " << ((double)tsta / 16384.0);

            _buffering_stats.clear();
        }
    }
}
