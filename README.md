# TCP Using UDP (SHAM Protocol)

## Overview

This project implements a **Reliable UDP Protocol** (named SHAM) that emulates TCP-like functionality over UDP. It demonstrates how a transport layer protocol can achieve reliable, ordered data delivery on top of an unreliable datagram protocol. This is a networking project developed for IIIT-H (International Institute of Information Technology, Hyderabad).

## Features

- **Reliable Data Delivery**: Implements sequence numbers, acknowledgments, and retransmission to ensure all data arrives in order
- **Flow Control**: Window-based flow control to manage sender and receiver buffer sizes
- **Dual Modes**: 
  - **File Transfer Mode**: Transfer files between client and server with automatic verification
  - **Chat Mode**: Real-time bidirectional communication (interactive chat)
- **Packet Loss Simulation**: Configurable packet loss rate for testing protocol robustness
- **Logging System**: Optional detailed logging of protocol events for debugging
- **MD5 Verification**: File integrity verification using MD5 checksums
- **Timeout & Retransmission**: Automatic retransmission of lost packets (RTO = 500ms)
- **Connection Management**: Three-way handshake (SYN) and graceful connection termination (FIN)

## Project Structure

```
TCP_Using_UDP/
├── client.c           # Client implementation with file/chat modes
├── server.c           # Server implementation with file/chat modes
├── sham.h             # Protocol header definitions
├── Makefile           # Build configuration
└── README.md          # This file
```

## Protocol Specification (SHAM)

### Packet Structure

```c
struct sham_header {
    uint32_t seq_num;      // Byte-based sequence number of first byte in payload
    uint32_t ack_num;      // Next expected byte (cumulative ACK)
    uint16_t flags;        // Control flags (SYN, ACK, FIN)
    uint16_t window_size;  // Flow control window (bytes)
};

struct sham_packet {
    struct sham_header hdr;
    char data[1024];       // Payload (max 1024 bytes)
};
```

### Flags

- **SHAM_SYN (0x1)**: Synchronization - initiates connection
- **SHAM_ACK (0x2)**: Acknowledgment - acknowledges received data
- **SHAM_FIN (0x4)**: Finish - gracefully closes connection

### Key Parameters

| Parameter | Value | Description |
|-----------|-------|-------------|
| RTO_MS | 500 | Retransmission Timeout in milliseconds |
| SND_WND_PACKETS | 10 | Sender window size (packets) |
| MAX_SENT_SLOTS | 128 | Maximum buffered outgoing packets |
| RECV_BUF_SLOTS | 1024 | Maximum buffered incoming packets |
| SHAM_PAYLOAD | 1024 | Maximum payload per packet (bytes) |

## Building the Project

### Prerequisites

- GCC compiler
- OpenSSL library (libcrypto) for MD5 checksums
- POSIX-compliant system (Linux, macOS, or WSL on Windows)

### Compilation

```bash
make              # Build both client and server
make client       # Build only client
make server       # Build only server
make clean        # Remove compiled binaries and logs
```

The compiled binaries will be `client` and `server` executables.

## Usage

### File Transfer Mode

#### Server (File Receive)

```bash
./server <port> [loss_rate]
```

**Parameters:**
- `<port>`: Port number to listen on (e.g., 5000)
- `[loss_rate]`: Optional packet loss rate (0.0 to 1.0, default 0.0)

**Example:**
```bash
./server 5000 0.1    # Listen on port 5000 with 10% packet loss
```

#### Client (File Send)

```bash
./client <server_ip> <server_port> <input_file> <output_file_name> [loss_rate]
```

**Parameters:**
- `<server_ip>`: IP address of server (e.g., 127.0.0.1, 192.168.1.10)
- `<server_port>`: Port number server is listening on
- `<input_file>`: Path to file to send
- `<output_file_name>`: Filename to save as on server
- `[loss_rate]`: Optional packet loss rate (0.0 to 1.0, default 0.0)

**Example:**
```bash
./client 127.0.0.1 5000 document.pdf received_document.pdf 0.05
```

### Chat Mode

#### Server (Chat)

```bash
./server <port> --chat [loss_rate]
```

**Example:**
```bash
./server 5000 --chat 0.1
```

#### Client (Chat)

```bash
./client <server_ip> <server_port> --chat [loss_rate]
```

**Example:**
```bash
./client 127.0.0.1 5000 --chat 0.1
```

Once connected, type messages to chat interactively. Press `Ctrl+C` or send special termination sequences to end the session.

## Logging

Enable detailed protocol logging by setting the `RUDP_LOG` environment variable:

```bash
export RUDP_LOG=1
./server 5000
```

This will generate:
- `server_log.txt` - Server-side logs
- `client_log.txt` - Client-side logs

Logs include timestamped events such as:
- Packet sends/receives
- Timeouts and retransmissions
- ACK processing
- Window updates
- Connection state transitions

## Testing & Scenarios

### Local Loopback Test

```bash
# Terminal 1 - Server
./server 5000

# Terminal 2 - Client (transfer file.txt)
./client 127.0.0.1 5000 file.txt received_file.txt
```

### Network Loss Simulation

Test protocol robustness with artificial packet loss:

```bash
# Terminal 1 - Server with 20% loss
./server 5000 0.2

# Terminal 2 - Client with 20% loss
./client 127.0.0.1 5000 large_file.iso received.iso 0.2
```

### Interactive Chat

```bash
# Terminal 1
./server 5000 --chat

# Terminal 2
./client 127.0.0.1 5000 --chat
```

## Protocol Behavior

### Connection Establishment

1. Client sends SYN packet to server
2. Server receives SYN and responds with SYN-ACK
3. Client receives SYN-ACK and sends ACK
4. Connection established

### Data Transfer

1. Client sends data packets with sequence numbers
2. Server receives and buffers packets in correct order
3. Server sends ACK with cumulative next-expected sequence number
4. Client retransmits unacknowledged packets after RTO
5. Flow control via window size prevents buffer overflow

### Connection Termination

1. Sender initiates close by sending FIN packet
2. Receiver acknowledges FIN
3. Receiver sends FIN packet
4. Sender acknowledges receiver's FIN
5. Connection closed

## File Integrity

Files transferred via the protocol are verified using MD5 checksums:
- Client computes MD5 of source file
- Server computes MD5 of received file
- Checksums are compared and printed upon completion
- Verification ensures no data corruption during transfer

## Implementation Details

### Client Features

- Sliding window protocol for efficient data transmission
- Sent packet buffer with timeout tracking
- File reading and MD5 computation
- Interactive chat with non-blocking I/O
- Graceful error handling and recovery

### Server Features

- Multi-slot receive buffer for out-of-order packets
- Automatic hole filling for efficient ACK generation
- File writing with atomic operations
- Chat mode with dual-direction communication
- Packet loss injection for testing

### Key Algorithms

- **Selective Repeat (SR) ARQ**: Sends multiple packets before awaiting ACKs
- **Cumulative Acknowledgment**: ACKs indicate highest continuously received byte
- **Exponential Backoff**: Optional timeout adjustments (can be extended)
- **MD5 Checksum**: Ensures file integrity end-to-end

## Troubleshooting

### Server won't start
- **Issue**: "Address already in use"
- **Solution**: Wait a minute for port to reset, or change port number

### File transfer slow
- **Issue**: High retransmission rate
- **Solution**: Reduce `loss_rate` parameter, or increase RTO_MS in source code

### Missing output file
- **Issue**: File not created on server side
- **Solution**: Check server console for errors, ensure write permissions in working directory

### Chat mode unresponsive
- **Issue**: Messages not appearing
- **Solution**: Ensure OpenSSL is installed, check network connectivity

### Compilation errors
- **Issue**: "openssl/md5.h: No such file"
- **Solution**: Install OpenSSL development package:
  ```bash
  # Ubuntu/Debian
  sudo apt-get install libssl-dev
  
  # macOS
  brew install openssl
  ```

## Performance Characteristics

| Metric | Typical Value |
|--------|---------------|
| Throughput (no loss) | ~100 Mbps (loopback) |
| Latency | 1-2 ms (loopback) |
| Retransmission Timeout | 500 ms |
| Max Window Size | 10 packets (~10 KB) |

*Note: Performance varies based on system, network conditions, and packet loss rate*

## Limitations & Future Improvements

### Current Limitations

- Fixed window size (no dynamic adjustment)
- Fixed RTO (no exponential backoff)
- Single concurrent client per server
- Maximum payload of 1024 bytes
- No encryption or authentication

### Possible Enhancements

- Dynamic window scaling (Nagle's algorithm)
- Adaptive RTO calculation (Karn's algorithm)
- Multiple concurrent client support (threading)
- Larger MTU support
- TLS/SSL encryption layer
- Congestion control (TCP Reno-style)
- SACK (Selective Acknowledgment)
- Path MTU discovery

## References

- **TCP/IP Illustrated**: Volume 1, Part 1 (TCP Protocol)
- **Computer Networking**: Kurose & Ross - Reliable Data Transfer chapter
- **RFC 793**: TCP Protocol Specification
- **OpenSSL Documentation**: MD5 computation

## Authors

- Developed for IIIT-H Networking Course
- Implementation using POSIX C standards

## License

This project is provided as-is for educational purposes.

## Contact & Support

For issues or questions:
1. Check the logging output with `RUDP_LOG=1`
2. Verify both client and server are using compatible parameters
3. Test with loopback (127.0.0.1) before testing over network
4. Review source code comments for implementation details

---

**Last Updated**: January 2026

**Version**: 1.0
