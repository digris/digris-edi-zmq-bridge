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
#include <string>
#include <vector>

/*
 * mpegts/tscore.h
 */

#define TS_PACKET_SIZE 188
#define TS_HEADER_SIZE 4
#define TS_BODY_SIZE (TS_PACKET_SIZE - TS_HEADER_SIZE)

#define TS_IS_SYNC(_ts) ((_ts[0] == 0x47))
#define TS_IS_PAYLOAD(_ts) ((_ts[3] & 0x10))
#define TS_IS_PAYLOAD_START(_ts) ((TS_IS_PAYLOAD(_ts) && (_ts[1] & 0x40)))
#define TS_IS_AF(_ts) ((_ts[3] & 0x20))

#define TS_GET_PID(_ts) ((uint16_t)(((_ts[1] & 0x1F) << 8) | _ts[2]))

#define TS_GET_CC(_ts) (_ts[3] & 0x0F)

#define TS_GET_PAYLOAD(_ts) ( \
    (!TS_IS_PAYLOAD(_ts)) ? (NULL) : ( \
        (!TS_IS_AF(_ts)) ? (&_ts[TS_HEADER_SIZE]) : ( \
            (_ts[4] >= TS_BODY_SIZE - 1) ? (NULL) : (&_ts[TS_HEADER_SIZE + 1 + _ts[4]])) \
        ) \
    )

/*
 * mpegts/psi.h
 */

#define PSI_MAX_SIZE 0x00000FFF
#define PSI_HEADER_SIZE 3
#define PSI_BUFFER_GET_SIZE(_b) \
    (PSI_HEADER_SIZE + (((_b[1] & 0x0f) << 8) | _b[2]))

struct mpegts_psi_t {
    uint8_t cc = 0;
    uint32_t crc32 = 0;

    uint16_t buffer_size = 0;
    uint16_t buffer_skip = 0;
    uint8_t buffer[PSI_MAX_SIZE];
};

class MPEDeframer {
public:
    MPEDeframer(const std::string& triplet);

    void process_ts(const std::vector<uint8_t>& mpeg_ts_packet);

    std::vector<std::vector<uint8_t> > get_deframed_packets();

private:
    void extract_edi();

    std::vector<std::vector<uint8_t> > m_extracted_frames;

    mpegts_psi_t m_psi;
    uint32_t m_pid = 0;

    uint16_t m_port = 0;
    uint32_t m_ip = 0;
    bool m_debug = false;
};
