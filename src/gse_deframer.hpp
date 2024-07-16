#pragma once
/*
   Copyright (C) 2024
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

#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>

struct bbheader {
    uint8_t MaType1;
    uint8_t MaType2;
    uint8_t Upl1;
    uint8_t Upl2;
    uint8_t Dfl1;
    uint8_t Dfl2;
    int8_t Sync;
    int8_t SyncD1;
    int8_t SyncD2;
    int8_t Crc8;
};

struct layer3 {
	unsigned char L3Sync;
	/*unsigned char AcmCommand;
	unsigned char CNI;
	unsigned char PlFrameId;*/
	bbheader header;
	unsigned char payload[7264-10];
};

class GSEDeframer {
public:
    GSEDeframer(const char* optarg);

    void process_packet(const std::vector<uint8_t>& udp_packet);

    std::vector<std::vector<uint8_t> > get_deframed_packets();

private:
    void process_ts(const uint8_t *ts);
    void prepare_bbframe(const uint8_t* buf, size_t len);
    bool process_bbframe(const uint8_t *payload, size_t gseLength);
    void process_ipv4_pdu(std::vector<uint8_t>&& pdu);

    std::deque<uint8_t> m_bbframe;
    std::vector<std::vector<uint8_t> > m_extracted_frames;

    bool m_debug = false;

    bool has_mis = true;
    uint8_t mis = 0;

    // keys are FragID
    struct PDUData {
        std::vector<uint8_t> pdu_data;
        uint16_t total_length = 0;
        uint16_t protocol_type = 0;
    };
    std::unordered_map<uint8_t, PDUData > fragments;
};
