//#llm generated code begins
#ifndef SHAM_H
#define SHAM_H

#include <stdint.h>

#define SHAM_PAYLOAD 1024

// Flags
#define SHAM_SYN 0x1
#define SHAM_ACK 0x2
#define SHAM_FIN 0x4

#pragma pack(push,1)
struct sham_header {
    uint32_t seq_num;      // byte-based sequence number of first byte in payload
    uint32_t ack_num;      // next expected byte (cumulative ack)
    uint16_t flags;        // control flags (SYN, ACK, FIN)
    uint16_t window_size;  // flow control window (bytes)
};

// full packet = header + payload
struct sham_packet {
    struct sham_header hdr;
    char data[SHAM_PAYLOAD];
};
#pragma pack(pop)

#endif // SHAM_H
//#llm generated code ends