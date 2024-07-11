/*
    This tool allows to extract bbframes which are encapsulated within a pseudo
    transport stream.

    Take from https://github.com/newspaperman/bbframe-tools

    **pts2bbf** will decapsulate on TS PID 0x010e (decimal 270) according to the
    description from Digital Devices
    (see https://github.com/DigitalDevices/dddvb/blob/master/docs/bbframes ).

    Licence: GPLv3

    It also contains code from bbfed2eti
    LICENCE: Mozilla Public License, version 2.0

    Adapted to odr-edi2edi by mpb in june 2024
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

#include "gse_deframer.hpp"

using namespace std;

constexpr size_t TS_PACKET_SIZE = 188;


GSEDeframer::GSEDeframer(const char* optarg)
{
    has_mis = true;
    mis = strtol(optarg, nullptr, 10);

    fragmentor = new unsigned char*[256];
    for (int i=0; i<256;i++) {
        fragmentor[i]=0;
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

void GSEDeframer::process_ts(const uint8_t *ts) {
    uint16_t pid = ts[1];
    pid &= 0x01F;
    pid <<= 8;
    pid |= ts[2];

    if (pid == 0x010e) {
        const uint8_t *buf = nullptr;
        size_t buflen = 0;

#if 1
        if ((ts[8] & 0xff) == 0xb8) { //START INDICATOR
            buf = ts+8;
            buflen = ts[7];
        }
        else {
            buf = ts+9;
            buflen = ts[7]-1;
        }
        fprintf(stderr, "process_ts %zu\n", buflen);
        prepare_bbframe(buf, buflen);
#else
        prepare_bbframe(ts+8, ts[7]);
#endif
    }
}


void GSEDeframer::prepare_bbframe(const uint8_t* buf, size_t len)
{
    const uint8_t *b = buf;
    for (size_t i = 0; i < len; i++) {
        m_bbframe.push_back(*b++);
    }

    while (m_bbframe.size() > 0 and m_bbframe[0] != 0xb8) {
        fprintf(stderr, "prep_bbframe skip sync %zu\n", m_bbframe.size());
        m_bbframe.pop_front();
    }

    // l3sync + bbheader
    if (m_bbframe.size() < 1 + 10) {
        return;
    }

    uint8_t dfl1 = m_bbframe[1+5];
    uint8_t dfl2 = m_bbframe[1+6];
    size_t bblength = (((uint16_t)dfl1) << 8 | dfl2) >> 3;

    if (m_bbframe.size() < 1 + 10 + bblength) {
        return;
    }

    uint8_t maType1 = m_bbframe[1+0];
    uint8_t maType2 = m_bbframe[1+1];

    fprintf(stderr, "prep_bbframe %d %d %zu %zu\n", maType1, maType2, bblength, m_bbframe.size());

    if (has_mis and maType2 != mis) {
        for (size_t i = 0; i < 1 + 10 + bblength; i++) {
            m_bbframe.pop_front();
        }
        return;
    }

    size_t pos = 0;
    while (pos < bblength - 4) { //last 4 bytes contain crc32
        const size_t gseLength = ((uint16_t)m_bbframe[1+10+pos]) << 8 | m_bbframe[1+10+pos+1];

        fprintf(stderr, "prep_bbframe gselen=%zu at pos=%zu\n", gseLength, pos);

        if ((m_bbframe[1+10+pos] & 0xf0) == 0) {
            fprintf(stderr, "prep_bbframe 0xf0 at pos=%zu\n", pos);
            break;
        }

        if (gseLength + 2 > bblength-pos) {
            fprintf(stderr, "prep_bbframe short buf at pos=%zu\n", pos);
            break;
        }

        std::vector<uint8_t> gse(gseLength);
        std::copy(m_bbframe.begin() + 1 + 10 + pos,
                m_bbframe.begin() + 1 + 10 + pos + gseLength,
                gse.begin());

        if (!process_bbframe(gse.data(), gse.size())) break;
        pos += gseLength + 2;
    }

    fprintf(stderr, "prep_bbframe discard %zu\n", bblength);
    for (size_t i = 0; i < 1 + 10 + bblength; i++) {
        m_bbframe.pop_front();
    }
}


static bool isSelected(const uint8_t* buf)
{
#if 0
    if(has_src_ip && (!(buf[0]==src_ip[0] && buf[1]==src_ip[1] && buf[2]==src_ip[2] && buf[3]==src_ip[3])))
        return false;
    if(has_dst_ip && (!(buf[4]==dst_ip[0] && buf[5]==dst_ip[1] && buf[6]==dst_ip[2] && buf[7]==dst_ip[3])))
        return false;
    if(has_src_port &&(!(buf[0x8]==src_port[0] && buf[0x9]==src_port[1])))
        return false;
    if(has_dst_port &&(!(buf[0xa]==dst_port[0] && buf[0xb]==dst_port[1])))
        return false;
#endif
    return true;
}

bool GSEDeframer::process_bbframe(const uint8_t* payload, size_t gseLength)
{
    //fprintf(stderr, "GSELength:%x\n", gseLength);
    unsigned int offset=0;
    unsigned int fragID=0;
    //START=1 STOP=0
    if((payload[0]&0xC0)==0x80) {
        fragID=payload[2];
        unsigned int length=(payload[3]<<8) | payload[4];
        if(fragmentor[fragID]!=0)
            delete [] fragmentor[fragID];
        fragmentor[fragID]=new unsigned char[length+2];
        fragmentorLength[fragID]=length+2;
        fragmentor[fragID][0]=payload[0];
        fragmentor[fragID][1]=payload[1];
        //SET START=1 STOP=1
        fragmentor[fragID][0]|=0xC0;
        memcpy(&fragmentor[fragID][2], &payload[5], gseLength-3);
        fragmentorPos[fragID]=gseLength-1;
    }
    //START=0 STOP=0
    else if((payload[0]&0xC0)==0x00) {
        fragID=payload[2];
        if(fragmentor[fragID]==0)
            return true;
        memcpy(&fragmentor[fragID][fragmentorPos[fragID]], &payload[3], gseLength-1);
        fragmentorPos[fragID]+=gseLength-1;
    }
    //START=0 STOP=1
    else if((payload[0]&0xC0)==0x40) {
        fragID=payload[2];
        if(fragmentor[fragID]==0)
            return true;
        memcpy(&fragmentor[fragID][fragmentorPos[fragID]], &payload[3], gseLength-5);
        fragmentorPos[fragID]+=gseLength-1;
        process_bbframe(fragmentor[fragID],fragmentorLength[fragID]);
        delete [] fragmentor[fragID];
        fragmentor[fragID]=0;

    }
    //START=1 STOP=1
    else if((payload[0]&0xC0)==0xC0) {
        if(payload[offset+2]==0x00 && payload[offset+3]==0x04) {
            //LABEL
            if((payload[0]&0x30)==0x01) {
                offset += 3;
            }
            else if((payload[0]&0x30)==0x00) {
                offset += 6;
            }
            //fprintf(stderr, "Start of 00 04 packet:%02x %02x %02x %02x %02x\n",payload[6], payload[7], payload[8], payload[9], payload[10]);
            if(payload[6]&0x80) {
                offset+=3;
                offset += 2;
                offset += 0x10;
                if(isSelected(&payload[offset])) {
                    offset +=0x10;
                    active=payload[7];
                }
                else {
                    active=0;
                    return true;
                }
            }
            else if(active==payload[7]) {
                offset+=9;
            }
            else {
                return true;
            }
        }
        else if(payload[offset+2]==0x00 && payload[offset+3]==0x00) {
            //LABEL
            if((payload[0]&0x30)==0x01) {
                offset += 3;
            }
            else if((payload[0]&0x30)==0x00) {
                offset += 6;
            }
            offset += 2;
            offset += 0x10;
            if(isSelected(&payload[offset])) {
                offset+=0x10;
            }
            else return true;
        }
        else if(payload[offset+2]==0x08 && payload[offset+3]==0x00) {
            if((payload[0]&0x30)==0x01) {
                offset += 3;
            }
            else if((payload[0]&0x30)==0x00) {
                offset += 6;
            }
            offset += 0x10;
            if(isSelected(&payload[offset])) {
                offset+=0x10;
            }
            else return true;
        }
        m_extracted_frames.emplace_back(gseLength+2-offset);
        std::copy(&payload[offset], &payload[gseLength+2], m_extracted_frames.back().begin());

        return true;
    }
    //PADDING
    else if((payload[0]&0xf0)==0x00) {
        return false;
    }
    return true;
}

std::vector<std::vector<uint8_t> > GSEDeframer::get_deframed_packets()
{
    std::vector<std::vector<uint8_t> > r;
    std::swap(r, m_extracted_frames);
    return r;
}
