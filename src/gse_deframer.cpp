/*
    This file contains parts from both pts2bbf and bbfedi2eti

    pts2bbf allows to extract bbframes which are encapsulated within a pseudo
    transport stream. Taken from https://github.com/newspaperman/bbframe-tools

    **pts2bbf** will decapsulate on TS PID 0x010e (decimal 270) according to the
    description from Digital Devices
    (see https://github.com/DigitalDevices/dddvb/blob/master/docs/bbframes ).

    Licence: GPLv3


    It also contains code from bbfed2eti
    LICENCE: Mozilla Public License, version 2.0

    Adapted to odr-edi2edi by mpb in summer 2024
 */
#include <vector>
#include <sstream>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>

#include "gse_deframer.hpp"

using namespace std;

constexpr size_t TS_PACKET_SIZE = 188;

constexpr bool HAS_MIS = true;

GSEDeframer::GSEDeframer(const char *triplet)
{
    std::stringstream ss(triplet);
    std::string item;
    std::vector<std::string> elems;
    while (std::getline(ss, item, ':')) {
        elems.push_back(std::move(item));
    }

    if (elems.size() == 1) {
        m_mis = strtol(optarg, nullptr, 10);
    }
    else if (elems.size() == 3) {
        m_mis = std::strtol(elems[0].c_str(), nullptr, 10);
        std::string ip = elems[1];

        uint32_t ip_u32 = 0;
        if (inet_pton(AF_INET, ip.c_str(), &ip_u32) != 1) {
            throw std::runtime_error("Invalid ip: " + ip);
        }

        m_ip = ip_u32;
        m_port = std::strtol(elems[2].c_str(), nullptr, 10);
    }
    else {
        throw std::runtime_error("PID:IP:PORT needs to have 3 elements!");
    }

    m_debug = getenv("DEBUG") ? 1 : 0;
}

void GSEDeframer::process_packet(const std::vector<uint8_t>& udp_packet)
{
    // Packets have an RTP header
    // 0x80 0x21 [2 bytes seq nr] [4 bytes timestamp] 0x0F 0x0F 0x0F 0x0F
    // Followed by several MPEG-TS packets

    if (udp_packet.size() > 12 and
            udp_packet[0] == 0x80 and
            udp_packet[1] == 0x21) {

        vector<uint8_t> rtp_payload(udp_packet.size()-12);
        std::copy(udp_packet.begin() + 12, udp_packet.end(), rtp_payload.begin());

        if (rtp_payload.size() % TS_PACKET_SIZE != 0) {
            fprintf(stderr, "RTP packet payload size %zu not multiple of %zu\n",
                    rtp_payload.size(), TS_PACKET_SIZE);
            return;
        }

        for (size_t i = 0; i < rtp_payload.size(); i += TS_PACKET_SIZE) {
            process_ts(rtp_payload.data() + i);
        }
    }
    else {
        fprintf(stderr, "UDP packet does not appear to have RTP: %02x %02x\n",
                udp_packet[0], udp_packet[1]);
    }
}

/*  process_ts is from pts2bbf.

    https://github.com/DigitalDevices/dddvb/blob/master/docs/bbframes

    Packet format:

    The BBFrames are packetized into MPEG2 private sections (0x80), one section per transport stream
    packet. The PID is fixed at 0x010E.


    Header packet of frame:

    0x47 0x41 0x0E 0x1X 0x00 0x80 0x00 L 0xB8 BBHeader (169 * Data)

    L: Section Length, always 180 (0xB4)
    BBHeader: 10 Bytes BBFrame header (see DVB-S2, EN-302307)
    Data: 169 Bytes of BBFrame payload


    Payload packets:

    0x47 0x41 0x0E 0x1X 0x00 0x80 0x00 L N (179 * Data)

    L: Section Length, always 180 (0xB4)
    N: Packet counter, starting with 0x01 after header packet
    Data: 179 Bytes of BBFrame payload


    Last packet:
    0x47 0x41 0x0E 0x1X 0x00 0x80 0x00 L N ((L-1) * Data)  ((180 – L) * 0xFF)

    L: Section Length, remaining Data – 1, (0x01 .. 0xB4)
    N: Packet counter
    Data: L-1 Bytes of BBFrame payload
*/

void GSEDeframer::process_ts(const uint8_t *ts) {
    uint16_t pid = ts[1];
    pid &= 0x01F;
    pid <<= 8;
    pid |= ts[2];

    if (pid == 0x010e) {
        const uint8_t *buf = nullptr;
        size_t buflen = 0;

#if DEBUG
        for (int i = 0; i < 188; i++) {
            fprintf(stderr, "%02X ", ts[i]);
        }

        if (ts[8] == 0xb8) fprintf(stderr, " BEGIN");
#endif

        if (ts[8] == 0xb8) { //START INDICATOR
            buf = ts+8;
            buflen = ts[7];
        }
        else {
            buf = ts+9;
            buflen = ts[7]-1;
        }
#if DEBUG
        fprintf(stderr, " %d -> %zu\n", (int)ts[7], buflen);
#endif
        prepare_bbframe(buf, buflen);
    }
}


void GSEDeframer::prepare_bbframe(const uint8_t* buf, size_t len)
{
    if (m_bbframe.empty() and buf[0] != 0xb8) {
        //fprintf(stderr, "prepare_bbframe SKIP\n");
        return;
    }

    for (size_t i = 0; i < len; i++) {
        m_bbframe.push_back(buf[i]);
    }

    while (m_bbframe.size() > 0 and m_bbframe[0] != 0xb8) {
        //fprintf(stderr, "prep_bbframe skip sync %02x %zu\n", m_bbframe[0], m_bbframe.size());
        m_bbframe.pop_front();
    }

    // l3sync + bbheader
    if (m_bbframe.size() < 1 + 10) {
        return;
    }

    //uint8_t maType1 = m_bbframe[1+0];
    uint8_t maType2 = m_bbframe[1+1];
    //uint8_t upl1 = m_bbframe[1+2];
    //uint8_t upl2 = m_bbframe[1+3];
    //size_t upl = ((upl1 << 8) | upl2) / 8;
    uint16_t dfl1 = m_bbframe[1+4];
    uint16_t dfl2 = m_bbframe[1+5];
    size_t bblength = ((dfl1 << 8) | dfl2) / 8;
    //fprintf(stderr, "prep_bbframe maType1=%d maType2=%d bblen=%zu %zu\n", maType1, maType2, bblength, m_bbframe.size());

    if (m_bbframe.size() < 1 + 10 + bblength) {
        //fprintf(stderr, "prep_bbframe TOO SHORT DFL=%zu UPL=%ld\n", bblength, upl);
        return;
    }

    if (HAS_MIS and maType2 != m_mis) {
        for (size_t i = 0; i < 1 + 10 + bblength; i++) {
            m_bbframe.pop_front();
        }
#if DEBUG
        fprintf(stderr, "prep_bbframe mis=%d != %d HEAD:", maType2, mis);
        for (int i = 0; i < 10 and i < m_bbframe.size(); i++) {
            fprintf(stderr, "%02X ", m_bbframe.at(i));
        }
        fprintf(stderr, "\n");
#endif
        return;
    }

    //fprintf(stderr, "prep_bbframe ENOUGH bblen=%zu %zu\n", bblength, m_bbframe.size());

    size_t pos = 0;
    while (pos < bblength - 4) { //last 4 bytes contain crc32
        if ((m_bbframe[1+10+pos] & 0xf0) == 0) { // start=0, end=0, LT=0. See TS 102 606-1 Table 2
            fprintf(stderr, "prep_bbframe only padding at pos=%zu\n", pos);
            break;
        }

        const uint16_t gseLength1 = m_bbframe[1+10+pos] & 0x0F;
        const uint16_t gseLength2 = m_bbframe[1+10+pos+1];
        const size_t gseLength = (gseLength1 << 8) | gseLength2;

        //fprintf(stderr, "prep_bbframe gselen=%zu at pos=%zu\n", gseLength, pos);

        if (gseLength + 2 > bblength-pos) {
            fprintf(stderr, "prep_bbframe short buf at pos=%zu\n", pos);
            break;
        }

        std::vector<uint8_t> gse(gseLength + 2);
        std::copy(m_bbframe.begin() + 1 + 10 + pos,
                m_bbframe.begin() + 1 + 10 + pos + gse.size(),
                gse.begin());

        if (!process_bbframe(gse.data(), gse.size())) break;
        pos += gseLength + 2;
    }

    //fprintf(stderr, "prep_bbframe discard %zu\n", 1 + 10 + bblength);
    for (size_t i = 0; i < 1 + 10 + bblength; i++) {
        m_bbframe.pop_front();
    }
#if DEBUG
    fprintf(stderr, "prep_bbframe OUT HEAD:");
    for (int i = 0; i < 10 and i < m_bbframe.size(); i++) {
        fprintf(stderr, "%02X ", m_bbframe.at(i));
    }
    fprintf(stderr, "\n");
#endif
}

bool GSEDeframer::process_bbframe(const uint8_t* payload, size_t payloadLen)
{
    // Refer to ETSI TS 102 606-1 Table 2
    const bool start = payload[0] & 0b10000000;
    const bool end = payload[0] & 0b01000000;
    const uint32_t lt = (payload[0] >> 4) & 0x03;

    if (not start and not end and lt == 0) {
        // Only padding
        return false;
    }

    const uint16_t gseLength = ((payload[0] & 0x0F) << 8) | payload[1];

    //fprintf(stderr, "GSE: payload %zu, start=%d, end=%d, LT=%d, gseLength=%d\n", payloadLen, start, end, lt, gseLength);

    if (start and not end) {
        uint8_t frag_id = payload[2];
        uint16_t total_length = (payload[3] << 8) | payload[4];
        uint16_t protocol_type = (payload[5] << 8) | payload[6];
        size_t offset = 7;
        if (lt == 0x01) {
            offset += 3;
        }
        else if (lt == 0x00) {
            offset += 6;
        }
        std::copy(payload + offset,
                payload + 2 + gseLength,
                std::back_inserter(fragments[frag_id].pdu_data));
        fragments[frag_id].total_length = total_length;
        fragments[frag_id].protocol_type = protocol_type;
    }
    else if (not start and not end) {
        uint8_t frag_id = payload[2];
        size_t offset = 3;

        if (fragments.count(frag_id) > 0) {
            std::copy(payload + offset,
                    payload + 2 + gseLength,
                    std::back_inserter(fragments.at(frag_id).pdu_data));
        }
    }
    else if (not start and end) {
        uint8_t frag_id = payload[2];
        size_t offset = 3;
        const size_t CRCLEN = 4;
        if (fragments.count(frag_id) > 0) {
            std::copy(payload + offset,
                    payload + 2 + gseLength - CRCLEN,
                    std::back_inserter(fragments.at(frag_id).pdu_data));

#if DEBUG
            fprintf(stderr, "COMPLETE %d: prot=%04X len=%zu\n",
                    (int)frag_id,
                    fragments[frag_id].protocol_type,
                    fragments[frag_id].pdu_data.size());
#endif

            if (fragments[frag_id].protocol_type == 0x0800) {
                process_ipv4_pdu(std::move(fragments[frag_id].pdu_data));
            }

            fragments.erase(frag_id);
        }
    }
    else if (start and end) {
        uint16_t protocol_type = (payload[2] << 8) | payload[3];
        size_t offset = 4;
        if (lt == 0x01) {
            offset += 3;
        }
        else if (lt == 0x00) {
            offset += 6;
        }
        std::vector<uint8_t> pdu_data;
        std::copy(payload + offset,
                payload + 2 + gseLength,
                std::back_inserter(pdu_data));

        //fprintf(stderr, "COMPLETE: prot=%04X len=%zu\n", protocol_type, pdu_data.size());
        if (protocol_type == 0x0800) {
            process_ipv4_pdu(std::move(pdu_data));
        }
    }
    return true;
}

void GSEDeframer::process_ipv4_pdu(std::vector<uint8_t>&& pdu) {
    const uint8_t version = pdu[0] >> 4;
    const uint8_t ihl = pdu[0] & 0x0F;

    if (version == 4 and pdu[9] == 0x11) { // UDP
        /* Source address
        unsigned char src_ip[4];
        src_ip[0] = pdu[12];
        src_ip[1] = pdu[13];
        src_ip[2] = pdu[14];
        src_ip[3] = pdu[15]; */

        unsigned char dst_ip[4];
        dst_ip[0] = pdu[16];
        dst_ip[1] = pdu[17];
        dst_ip[2] = pdu[18];
        dst_ip[3] = pdu[19];

        size_t udp_header_offset = ihl * 4;
        const size_t UDP_HEADER_SIZE = 4;

        if (m_ip != 0 and m_port != 0) {
            char dbuf[18];
            sprintf(dbuf, "%u.%u.%u.%u", dst_ip[0], dst_ip[1], dst_ip[2], dst_ip[3]);
            uint32_t dip;

            /* skip unknown ip or port */
            if (inet_pton (AF_INET, dbuf, &dip) != 1) return;
            if (dip != m_ip) return;
            const uint8_t *udp = pdu.data() + udp_header_offset;
            const uint16_t dst_port = (udp[2] << 8) | udp[3];
            if (dst_port != m_port) return;
        }

#if DEBUG
        {
            const uint8_t *udp = pdu.data() + udp_header_offset;
            uint16_t s_port = (udp[0] << 8) | udp[1];
            uint16_t d_port = (udp[2] << 8) | udp[3];
            uint16_t udp_len = (udp[4] << 8) | udp[5];

            fprintf(stderr, "IPv4/UDP %d/%zu %d.%d.%d.%d:%d -> %d.%d.%d.%d:%d\n",
                    udp_len, pdu.size() - udp_header_offset,
                    pdu[12], pdu[13], pdu[14], pdu[15], s_port,
                    pdu[16], pdu[17], pdu[18], pdu[19], d_port);
        }
#endif

        // I don't know what this additional header is.
        // First byte is always 0x05, 2nd byte is 0x17 or 0x19, 3rd and 4th change
        const size_t UNKNOWN_HEADER_LEN = 4;

        m_extracted_frames.emplace_back(pdu.size() - udp_header_offset - UDP_HEADER_SIZE - UNKNOWN_HEADER_LEN);
        std::copy(pdu.begin() + udp_header_offset + UDP_HEADER_SIZE + UNKNOWN_HEADER_LEN, pdu.end(),
                m_extracted_frames.back().begin());
    }
}

std::vector<std::vector<uint8_t> > GSEDeframer::get_deframed_packets()
{
    std::vector<std::vector<uint8_t> > r;
    std::swap(r, m_extracted_frames);
    return r;
}
