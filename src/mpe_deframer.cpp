/*
 * Taken from eti-tools' fedi2eti.c
 * LICENCE: Mozilla Public License, version 2.0
 *
 * Uses parts of astra-sm and edi2eti.c
 *
 * Created on: 01.06.2018
 *     Author: athoik
 *
 * Adapted to odr-edi2edi by mpb in june 2024
 */

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <stdexcept>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <vector>
#include <sstream>

#include "mpe_deframer.hpp"

MPEDeframer::MPEDeframer(const std::string& triplet)
{
    std::stringstream ss(triplet);
    std::string item;
    std::vector<std::string> elems;
    while (std::getline(ss, item, ':')) {
        elems.push_back(std::move(item));
    }

    if (elems.size() != 3) {
        throw std::runtime_error("PID:IP:PORT needs to have 3 elements!");
    }

    m_pid = std::strtol(elems[0].c_str(), nullptr, 10);
    std::string ip = elems[1];

    uint32_t ip_u32 = 0;
    if (inet_pton(AF_INET, ip.c_str(), &ip_u32) != 1) {
        throw std::runtime_error("Invalid ip: " + ip);
    }

    m_ip = ip_u32;
    m_port = std::strtol(elems[2].c_str(), nullptr, 10);
    m_debug = getenv("DEBUG") ? 1 : 0;
}

void MPEDeframer::process_packet(const std::vector<uint8_t>& udp_packet)
{
    if (udp_packet.size() % TS_PACKET_SIZE != 0) {
        fprintf(stderr, "UDP packet size %zu not multiple of %d\n",
                udp_packet.size(), TS_PACKET_SIZE);
        return;
    }

    for (size_t i = 0; i < udp_packet.size(); i += TS_PACKET_SIZE) {
        process_ts(udp_packet.data() + i*TS_PACKET_SIZE);
    }
}

void MPEDeframer::process_ts(const uint8_t *ts)
{
    if (not(TS_IS_SYNC(ts) && TS_GET_PID(ts) == m_pid)) {
        return;
    }

    const uint8_t *payload = TS_GET_PAYLOAD(ts);
    if (!payload) {
        return;
    }

    const uint8_t cc = TS_GET_CC(ts);

    if(TS_IS_PAYLOAD_START(ts))
    {
        const uint8_t ptr_field = *payload;
        ++payload; // skip pointer field

        if(ptr_field > 0)
        { // pointer field
            if(ptr_field >= TS_BODY_SIZE)
            {
                m_psi.buffer_skip = 0;
                return;
            }
            if(m_psi.buffer_skip > 0)
            {
                if(((m_psi.cc + 1) & 0x0f) != cc)
                { // discontinuity error
                    m_psi.buffer_skip = 0;
                    return;
                }
                memcpy(&m_psi.buffer[m_psi.buffer_skip], payload, ptr_field);
                if(m_psi.buffer_size == 0)
                { // incomplete PSI header
                    const size_t psi_buffer_size = PSI_BUFFER_GET_SIZE(m_psi.buffer);
                    if(psi_buffer_size <= 3 || psi_buffer_size > PSI_MAX_SIZE)
                    {
                        m_psi.buffer_skip = 0;
                        return;
                    }
                    m_psi.buffer_size = psi_buffer_size;
                }
                if(m_psi.buffer_size != m_psi.buffer_skip + ptr_field)
                { // checking PSI length
                    m_psi.buffer_skip = 0;
                    return;
                }
                m_psi.buffer_skip = 0;
                extract_edi();
            }
            payload += ptr_field;
        }
        while(((payload - ts) < TS_PACKET_SIZE) && (payload[0] != 0xff))
        {
            m_psi.buffer_size = 0;

            const uint8_t remain = (ts + TS_PACKET_SIZE) - payload;
            if(remain < 3)
            {
                memcpy(m_psi.buffer, payload, remain);
                m_psi.buffer_skip = remain;
                break;
            }

            const size_t psi_buffer_size = PSI_BUFFER_GET_SIZE(payload);
            if(psi_buffer_size <= 3 || psi_buffer_size > PSI_MAX_SIZE)
                break;

            const size_t cpy_len = (ts + TS_PACKET_SIZE) - payload;
            if(cpy_len > TS_BODY_SIZE)
                break;

            m_psi.buffer_size = psi_buffer_size;
            if(psi_buffer_size > cpy_len)
            {
                memcpy(m_psi.buffer, payload, cpy_len);
                m_psi.buffer_skip = cpy_len;
                break;
            }
            else
            {
                memcpy(m_psi.buffer, payload, psi_buffer_size);
                m_psi.buffer_skip = 0;
                extract_edi();
                payload += psi_buffer_size;
            }
        }
    }
    else
    { // !TS_PUSI(ts)
        if(!m_psi.buffer_skip)
            return;
        if(((m_psi.cc + 1) & 0x0f) != cc)
        { // discontinuity error
            m_psi.buffer_skip = 0;
            return;
        }
        if(m_psi.buffer_size == 0)
        { // incomplete PSI header
            if(m_psi.buffer_skip >= 3)
            {
                m_psi.buffer_skip = 0;
                return;
            }
            memcpy(&m_psi.buffer[m_psi.buffer_skip], payload, 3 - m_psi.buffer_skip);
            const size_t psi_buffer_size = PSI_BUFFER_GET_SIZE(m_psi.buffer);
            if(psi_buffer_size <= 3 || psi_buffer_size > PSI_MAX_SIZE)
            {
                m_psi.buffer_skip = 0;
                return;
            }
            m_psi.buffer_size = psi_buffer_size;
        }
        const size_t remain = m_psi.buffer_size - m_psi.buffer_skip;
        if(remain <= TS_BODY_SIZE)
        {
            memcpy(&m_psi.buffer[m_psi.buffer_skip], payload, remain);
            m_psi.buffer_skip = 0;
            extract_edi();
        }
        else
        {
            memcpy(&m_psi.buffer[m_psi.buffer_skip], payload, TS_BODY_SIZE);
            m_psi.buffer_skip += TS_BODY_SIZE;
        }
    }
    m_psi.cc = cc;
}

void MPEDeframer::extract_edi()
{
    if (m_psi.buffer[0] != 0x3e) {
        return;
    }

    const uint8_t *ptr = m_psi.buffer;
    size_t len = m_psi.buffer_size;

    /* MAC address */
    unsigned char dest_mac[6];
    dest_mac[5] = m_psi.buffer[3];
    dest_mac[4] = m_psi.buffer[4];
    dest_mac[3] = m_psi.buffer[8];
    dest_mac[2] = m_psi.buffer[9];
    dest_mac[1] = m_psi.buffer[10];
    dest_mac[0] = m_psi.buffer[11];

    if (m_debug) {
        fprintf(stderr, "MAC addess: %2X:%2X:%2X:%2X:%2X:%2X\n", dest_mac[0],
            dest_mac[1], dest_mac[2], dest_mac[3], dest_mac[4], dest_mac[5]);
    }

    /* Parse IP header */
    unsigned char *ip = m_psi.buffer + 12;

    /* IP version - v4 */
    char version = (ip[0] & 0xF0) >> 4;
    if(version != 4) {
        fprintf(stderr, "Not IP packet.. ver=%d\n", version);
        return;
    }

    /* Protocol number */
    char proto = ip[9];

    /* filter non-UDP packets */
    if(proto != 17) {
        fprintf(stderr, "Not UDP protocol %d\n", proto);
        return;
    }

    /* packet length */
    //unsigned short len_ip = ip[2] << 8 | ip[3];

    /* source IP addres */
    unsigned char src_ip[4];
    src_ip[0] = ip[12];
    src_ip[1] = ip[13];
    src_ip[2] = ip[14];
    src_ip[3] = ip[15];

    /* Destination IP address */
    unsigned char dst_ip[4];
    dst_ip[0] = ip[16];
    dst_ip[1] = ip[17];
    dst_ip[2] = ip[18];
    dst_ip[3] = ip[19];

    unsigned char *udp = ip + 20;
    unsigned short src_port = (udp[0] << 8) | udp[1];
    unsigned short dst_port = (udp[2] << 8) | udp[3];
    unsigned short len_udp  = (udp[4] << 8) | udp[5];
    //unsigned short chk_udp  = (udp[6] << 8) | udp[7];

    if (m_debug) {
        fprintf(stderr, "UDP %d.%d.%d.%d:%d --> %d.%d.%d.%d:%d  [%d bytes payload (%zu) EDI packet %c%c]\n",
            src_ip[0], src_ip[1], src_ip[2], src_ip[3], src_port,
            dst_ip[0], dst_ip[1], dst_ip[2], dst_ip[3], dst_port,
            len_udp-8, len-40, ptr[40], ptr[41]);
    }

    /* maybe use dst_ip[0] + dst_ip[1] << 8 + dst_ip[2] << 16 + dst_ip[3] << 24 instead? */
    char dbuf[18];
    sprintf(dbuf, "%u.%u.%u.%u", dst_ip[0], dst_ip[1], dst_ip[2], dst_ip[3]);
    uint32_t dip;

    /* skip unknown ip or port */
    if (inet_pton (AF_INET, dbuf, &dip) != 1) return;
    if (dip != m_ip) return;
    if (dst_port != m_port) return;

    /* skip headers: MPE + IP + UDP */
    ptr += (12 + 20 + 8);
    len -= (12 + 20 + 8);

    m_extracted_frames.emplace_back(len_udp-8);
    std::copy(udp+8, udp+len_udp, m_extracted_frames.back().begin());
}

std::vector<std::vector<uint8_t> > MPEDeframer::get_deframed_packets()
{
    std::vector<std::vector<uint8_t> > r;
    std::swap(r, m_extracted_frames);
    return r;
}
