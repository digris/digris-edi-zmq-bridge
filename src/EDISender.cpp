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
#include <numeric>
#include <map>
#include <algorithm>
#include <limits>

using namespace std;

EDISender::~EDISender()
{
    running.store(false);
    tagpackets.trigger_wakeup();

    if (process_thread.joinable()) {
        process_thread.join();
    }
}

void EDISender::start(const edi::configuration_t& conf, int delay_ms, bool drop_late_packets)
{
    edi_conf = conf;
    tist_delay_ms = delay_ms;
    drop_late = drop_late_packets;

    edi_sender = make_shared<edi::Sender>(edi_conf);

    running.store(true);
    process_thread = thread(&EDISender::process, this);
}

void EDISender::push_tagpacket(tagpacket_t&& tp)
{
    tagpackets.push(move(tp));
}

void EDISender::print_configuration()
{
    if (edi_conf.enabled()) {
        edi_conf.print();
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
    const auto t_release = t_frame + milliseconds(tist_delay_ms);
    const auto t_now = system_clock::now();

    const bool late = t_release < t_now;

    buffering_stat_t stat;
    stat.late = late;

    if (not late) {
        const auto wait_time = t_release - t_now;
        std::this_thread::sleep_for(wait_time);
    }

    stat.buffering_time_us = duration_cast<microseconds>(steady_clock::now() - tp.received_at).count();
    buffering_stats.push_back(std::move(stat));

    if (late and drop_late) {
        return;
    }

    if (edi_sender and edi_conf.enabled()) {
        edi::TagPacket edi_tagpacket(0);

        if (tp.seq.seq_valid) {
            edi_sender->override_af_sequence(tp.seq.seq);
        }

        if (tp.seq.pseq_valid) {
            edi_sender->override_pft_sequence(tp.seq.pseq);
        }
        else if (tp.seq.seq_valid) {
            // If the source isn't using PFT, set PSEQ = SEQ so that multihoming
            // with several EDI2EDI instances could work.
            edi_sender->override_pft_sequence(tp.seq.seq);
        }

        edi_tagpacket.raw_tagpacket = move(tp.tagpacket);
        edi_sender->write(edi_tagpacket);
    }
}

void EDISender::process()
{
    while (running.load()) {
        tagpacket_t tagpacket;
        try {
            tagpackets.wait_and_pop(tagpacket);
        }
        catch (const ThreadsafeQueueWakeup&) {
            break;
        }

        if (not running.load()) {
            break;
        }

        const uint16_t dlfc = tagpacket.dlfc;
        const auto tsta = tagpacket.timestamp.tsta;
        send_tagpacket(tagpacket);

        if (dlfc % 250 == 0) { // every six seconds
            const double n = buffering_stats.size();

            size_t num_late = std::count_if(buffering_stats.begin(), buffering_stats.end(),
                    [](const buffering_stat_t& s){ return s.late; });

            double sum = 0.0;
            double min = std::numeric_limits<double>::max();
            double max = -std::numeric_limits<double>::max();
            for (const auto& s : buffering_stats) {
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
            for (const auto& s : buffering_stats) {
                const double t = s.buffering_time_us / 1000.0;
                sq_sum += (t-mean) * (t-mean);
            }
            double stdev = sqrt(sq_sum / n);

            /* Debug code
            stringstream ss;
            ss << "times:";
            for (const auto t : buffering_stats) {
                ss << " " << lrint(t.buffering_time_us / 1000.0);
            }
            etiLog.level(debug) << ss.str();
            // */

            etiLog.level(info) << "Buffering time statistics [milliseconds]:"
                " min: " << min <<
                " max: " << max <<
                " mean: " << mean <<
                " stdev: " << stdev <<
                " late: " <<
                num_late << " of " << buffering_stats.size() << " (" <<
                num_late * 100.0 / n << "%)" <<
                " Frame 0 TS " << ((double)tsta / 16384.0);


            buffering_stats.clear();
        }
    }
}
