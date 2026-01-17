// client.c
// #llm generated code begins
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdbool.h>
#include <netdb.h>
#include <openssl/md5.h>
#include <stdarg.h>
#include <sys/select.h>

#include "sham.h"

#define RTO_MS 500
#define SND_WND_PACKETS 10
#define MAX_SENT_SLOTS 128

static FILE *log_file = NULL;
static int logging_enabled = 0;

static void open_log(const char *name) {
    char *env = getenv("RUDP_LOG");
    if (env && strcmp(env, "1") == 0) {
        logging_enabled = 1;
        log_file = fopen(name, "w");
    }
}
static void close_log(void) {
    if (log_file) fclose(log_file);
    log_file = NULL;
    logging_enabled = 0;
}
static void timestamped_log(const char *fmt, ...) {
    if (!logging_enabled || !log_file) return;
    struct timeval tv; gettimeofday(&tv, NULL);
    time_t cur = tv.tv_sec;
    struct tm *tm = localtime(&cur);
    char timebuf[64];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm);
    fprintf(log_file, "[%s.%06ld] [LOG] ", timebuf, (long)tv.tv_usec);
    va_list ap; va_start(ap, fmt); vfprintf(log_file, fmt, ap); va_end(ap);
    fprintf(log_file, "\n"); fflush(log_file);
}

static void sleep_ms(int ms) {
    struct timespec ts = { ms/1000, (ms%1000) * 1000000L };
    nanosleep(&ts, NULL);
}
static long long now_ms(void) {
    struct timeval tv; gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000LL + tv.tv_usec / 1000;
}
static int create_udp_socket(void) {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) { perror("socket"); exit(1); }
    return s;
}
static ssize_t safe_sendto(int sock, const void *buf, size_t len, int flags,
                           const struct sockaddr *dest_addr, socklen_t addrlen) {
    ssize_t r = sendto(sock, buf, len, flags, dest_addr, addrlen);
    if (r < 0) perror("sendto");
    return r;
}

struct sent_slot {
    int in_use;
    struct sham_packet pkt;
    ssize_t len;
    long long sent_time_ms;
};

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr,
            "Usage:\n File: ./client <server_ip> <server_port> <input_file> <output_file_name> [loss_rate]\n Chat: ./client <server_ip> <server_port> --chat [loss_rate]\n");
        return 1;
    }

    srand((unsigned)time(NULL));

    const char *server_ip = argv[1];
    int server_port = atoi(argv[2]);

    bool chat_mode = false;
    const char *input_file = NULL;
    const char *output_file_name = NULL;
    double loss_rate = 0.0;

    // --- Step 1: Determine the mode (chat vs. file) ---
    if (strcmp(argv[3], "--chat") == 0) {
        chat_mode = true;
    } else {
        // File mode requires at least 5 arguments
        if (argc < 5) {
            fprintf(stderr, "Error: Missing input and output file names.\n");
            return 1;
        }
        input_file = argv[3];
        output_file_name = argv[4];
    }

    // --- Step 2: Check for the optional loss_rate based on the mode ---
    if (chat_mode) {
        // For chat mode, loss_rate is the 5th argument (index 4)
        // ./client <ip> <port> --chat [loss_rate]
        if (argc == 5) {
            loss_rate = atof(argv[4]);
        }
    } else {
        // For file mode, loss_rate is the 6th argument (index 5)
        // ./client <ip> <port> <in> <out> [loss_rate]
        if (argc == 6) {
            loss_rate = atof(argv[5]);
        }
    }

    open_log("client_log.txt");

    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_port = htons(server_port);
    if (inet_pton(AF_INET, server_ip, &srv.sin_addr) != 1) {
        fprintf(stderr, "Invalid server IP\n"); return 1;
    }
    socklen_t srv_len = sizeof(srv);

    int sock = create_udp_socket();
    fcntl(sock, F_SETFL, O_NONBLOCK);

    // ---- three-way handshake ----
    uint32_t client_isn = (uint32_t)(rand() & 0x7fffffff);
    struct sham_packet syn; memset(&syn, 0, sizeof(syn));
    syn.hdr.seq_num = htonl(client_isn);
    syn.hdr.flags = htons(SHAM_SYN);

    safe_sendto(sock, &syn, sizeof(struct sham_header), 0, (struct sockaddr*)&srv, srv_len);
    timestamped_log("SND SYN SEQ=%u", client_isn);

    struct sham_packet rcv;
    uint32_t server_isn = 0;
    long long start = now_ms();
    bool handshake_complete = false;
    while(now_ms() - start < 5000) {
        ssize_t rc = recvfrom(sock, &rcv, sizeof(rcv), 0, NULL, NULL);
        if (rc >= (ssize_t)sizeof(struct sham_header)) {
            uint16_t flags = ntohs(rcv.hdr.flags);
            if ((flags & (SHAM_SYN | SHAM_ACK))) {
                server_isn = ntohl(rcv.hdr.seq_num);
                timestamped_log("RCV SYN-ACK SEQ=%u ACK=%u", server_isn, ntohl(rcv.hdr.ack_num));
                
                struct sham_packet ack; memset(&ack, 0, sizeof(ack));
                ack.hdr.ack_num = htonl(server_isn + 1);
                ack.hdr.flags = htons(SHAM_ACK);
                safe_sendto(sock, &ack, sizeof(struct sham_header), 0, (struct sockaddr*)&srv, srv_len);
                timestamped_log("SND ACK FOR SYN");
                handshake_complete = true;
                break;
            }
        }
        sleep_ms(20);
    }
    
    if (!handshake_complete) {
        fprintf(stderr, "Handshake failed.\n");
        close_log(); close(sock); return 1;
    }
    
    uint32_t client_seq = client_isn + 1;

    // ---------- CHAT MODE ----------
    if (chat_mode) {
        // Chat mode logic as before...
        printf("Chat mode established. Type messages, /quit to exit.\n");
        fd_set rfds;
        char buf[2048];
        int maxfd = sock > STDIN_FILENO ? sock : STDIN_FILENO;
        bool shutting_down = false;
        
        while (!shutting_down) {
            // ... (chat logic remains the same as previous correct version)
            FD_ZERO(&rfds);
            FD_SET(STDIN_FILENO, &rfds);
            FD_SET(sock, &rfds);
            struct timeval tv = {1, 0};
            int sel = select(maxfd+1, &rfds, NULL, NULL, &tv);
            if (sel > 0) {
                 if (FD_ISSET(STDIN_FILENO, &rfds)) {
                    if (!fgets(buf, sizeof(buf), stdin)) break;
                    buf[strcspn(buf, "\n")] = 0;
                    if (strcmp(buf, "/quit") == 0) {
                        // **** CLIENT-INITIATED TERMINATION ****
                        struct sham_packet fin; memset(&fin,0,sizeof(fin));
                        fin.hdr.seq_num = htonl(client_seq);
                        fin.hdr.flags = htons(SHAM_FIN);
                        safe_sendto(sock, &fin, sizeof(struct sham_header), 0, (struct sockaddr*)&srv, srv_len);
                        timestamped_log("SND FIN SEQ=%u", client_seq);

                        bool ack_for_fin_rcvd = false;
                        bool server_fin_rcvd = false;
                        long long term_start = now_ms();

                        while(now_ms() - term_start < 5000) {
                            ssize_t r = recvfrom(sock, &rcv, sizeof(rcv), 0, NULL, NULL);
                            if (r > 0) {
                                uint16_t flags = ntohs(rcv.hdr.flags);
                                if ((flags & SHAM_ACK) && ntohl(rcv.hdr.ack_num) == client_seq + 1) {
                                    timestamped_log("RCV ACK FOR FIN");
                                    ack_for_fin_rcvd = true;
                                }
                                if (flags & SHAM_FIN) {
                                    uint32_t server_fin_seq = ntohl(rcv.hdr.seq_num);
                                    timestamped_log("RCV FIN SEQ=%u", server_fin_seq);

                                    struct sham_packet final_ack; memset(&final_ack, 0, sizeof(final_ack));
                                    final_ack.hdr.flags = htons(SHAM_ACK);
                                    final_ack.hdr.ack_num = htonl(server_fin_seq + 1);
                                    safe_sendto(sock, &final_ack, sizeof(struct sham_header), 0, (struct sockaddr*)&srv, srv_len);
                                    timestamped_log("SND ACK=%u", ntohl(final_ack.hdr.ack_num));
                                    server_fin_rcvd = true;
                                }
                            }
                            if (ack_for_fin_rcvd && server_fin_rcvd) break;
                            sleep_ms(20);
                        }
                        shutting_down = true;
                        continue;
                    }
                    struct sham_packet dp; memset(&dp,0,sizeof(dp));
                    dp.hdr.seq_num = htonl(client_seq);
                    snprintf(dp.data, sizeof(dp.data), "%s", buf);
                    size_t ml = strlen(dp.data);
                    safe_sendto(sock, &dp, sizeof(struct sham_header) + ml, 0, (struct sockaddr*)&srv, srv_len);
                    timestamped_log("SND DATA SEQ=%u LEN=%zu", client_seq, ml);
                    client_seq += ml;
                }
                if (FD_ISSET(sock, &rfds)) {
                    ssize_t rc = recvfrom(sock, &rcv, sizeof(rcv), 0, NULL, NULL);
                    // --- ADD THIS BLOCK TO SIMULATE LOSS ---
                    if (rc > 0 && loss_rate > 0.0) {
                        uint16_t flags = ntohs(rcv.hdr.flags);
                        // Only drop data packets, not control packets
                        if (!(flags & (SHAM_SYN|SHAM_ACK|SHAM_FIN))) {
                            if (((double)rand() / RAND_MAX) < loss_rate) {
                                timestamped_log("DROP DATA SEQ=%u", ntohl(rcv.hdr.seq_num));
                                continue; // Drop the packet
                            }
                        }
                    }
                    // --- END OF NEW BLOCK ---
                    if (rc >= (ssize_t)sizeof(struct sham_header)) {
                        uint16_t flags = ntohs(rcv.hdr.flags);
                        if (flags & SHAM_FIN) {
                             // **** SERVER-INITIATED TERMINATION ****
                            uint32_t peer_fin_seq = ntohl(rcv.hdr.seq_num);
                            timestamped_log("RCV FIN SEQ=%u", peer_fin_seq);
                            
                            struct sham_packet ack_for_fin; memset(&ack_for_fin,0,sizeof(ack_for_fin));
                            ack_for_fin.hdr.flags = htons(SHAM_ACK);
                            ack_for_fin.hdr.ack_num = htonl(peer_fin_seq + 1);
                            safe_sendto(sock, &ack_for_fin, sizeof(struct sham_header), 0, (struct sockaddr*)&srv, srv_len);
                            timestamped_log("SND ACK FOR FIN");
                            
                            struct sham_packet myfin; memset(&myfin, 0, sizeof(myfin));
                            myfin.hdr.seq_num = htonl(client_seq);
                            myfin.hdr.flags = htons(SHAM_FIN);
                            safe_sendto(sock, &myfin, sizeof(struct sham_header), 0, (struct sockaddr*)&srv, srv_len);
                            timestamped_log("SND FIN SEQ=%u", client_seq);

                            bool final_ack_rcvd = false;
                            long long term_start = now_ms();
                            while(now_ms() - term_start < 5000) {
                                ssize_t r2 = recvfrom(sock, &rcv, sizeof(rcv), 0, NULL, NULL);
                                if (r2 >= (ssize_t)sizeof(struct sham_header) && (ntohs(rcv.hdr.flags) & SHAM_ACK) && ntohl(rcv.hdr.ack_num) == client_seq + 1) {
                                    timestamped_log("RCV ACK=%u", ntohl(rcv.hdr.ack_num));
                                    final_ack_rcvd = true;
                                    break;
                                }
                                sleep_ms(20);
                            }
                            shutting_down = true;
                        } else {
                            if (rc > (ssize_t)sizeof(struct sham_header)) {
                                size_t data_len = rc - sizeof(struct sham_header);
                                timestamped_log("RCV DATA SEQ=%u LEN=%zu", ntohl(rcv.hdr.seq_num), rc - sizeof(struct sham_header));
                                printf("Server: %.*s\n", (int)data_len, rcv.data);
                            }
                        }
                    }
                }
            }
        }
    } else {
        // **** FILE TRANSFER LOGIC STARTS HERE ****
        uint32_t base_seq = client_isn + 1;
        uint32_t next_seq = base_seq;

        // Send filename first
        struct sham_packet namepkt; memset(&namepkt, 0, sizeof(namepkt));
        namepkt.hdr.seq_num = htonl(base_seq);
        snprintf(namepkt.data, sizeof(namepkt.data), "%s", output_file_name);
        size_t nlen = strlen(namepkt.data);
        safe_sendto(sock, &namepkt, sizeof(struct sham_header) + nlen, 0, (struct sockaddr*)&srv, srv_len);
        timestamped_log("SND FILENAME %s", namepkt.data);
        next_seq = base_seq + (uint32_t)nlen;

        // Sliding window implementation
        FILE *fp = fopen(input_file, "rb");
        if (!fp) { perror("fopen input"); close_log(); close(sock); return 1; }
        
        struct sent_slot slots[MAX_SENT_SLOTS] = {0};
        uint32_t highest_acked = base_seq - 1;

        bool eof = false;
        while (!eof || highest_acked < next_seq -1) {
            int inflight = 0;
            for(int i=0; i<MAX_SENT_SLOTS; ++i) if(slots[i].in_use) inflight++;
            
            while(inflight < SND_WND_PACKETS && !eof) {
                char data_buf[SHAM_PAYLOAD];
                size_t r = fread(data_buf, 1, SHAM_PAYLOAD, fp);
                if (r == 0) { eof = true; break; }

                struct sham_packet dp; memset(&dp, 0, sizeof(dp));
                dp.hdr.seq_num = htonl(next_seq);
                memcpy(dp.data, data_buf, r);
                ssize_t slen = sizeof(struct sham_header) + (ssize_t)r;
                safe_sendto(sock, &dp, slen, 0, (struct sockaddr*)&srv, srv_len);
                timestamped_log("SND DATA SEQ=%u LEN=%zu", next_seq, r);
                
                int slot = -1;
                for(int i=0; i<MAX_SENT_SLOTS; ++i) if(!slots[i].in_use) { slot=i; break; }
                if(slot == -1) { break; /* Should not happen if inflight is correct */ }
                
                slots[slot].in_use = 1;
                memcpy(&slots[slot].pkt, &dp, slen);
                slots[slot].len = slen;
                slots[slot].sent_time_ms = now_ms();
                
                next_seq += (uint32_t)r;
                inflight++;
            }
            
            ssize_t rc = recvfrom(sock, &rcv, sizeof(rcv), 0, NULL, NULL);
            if (rc > 0 && (ntohs(rcv.hdr.flags) & SHAM_ACK)) {
                uint32_t ackn = ntohl(rcv.hdr.ack_num);
                timestamped_log("RCV ACK=%u", ackn);
                if (ackn > highest_acked) {
                    for (int i = 0; i < MAX_SENT_SLOTS; ++i) if (slots[i].in_use) {
                        uint32_t pseq = ntohl(slots[i].pkt.hdr.seq_num);
                        int plen = (int)slots[i].len - (int)sizeof(struct sham_header);
                        if (pseq + (uint32_t)plen < ackn) slots[i].in_use = 0;
                    }
                    highest_acked = ackn - 1;
                }
            }
            
            long long now = now_ms();
            for (int i = 0; i < MAX_SENT_SLOTS; ++i) if (slots[i].in_use) {
                if (now - slots[i].sent_time_ms > RTO_MS) {
                    uint32_t seq = ntohl(slots[i].pkt.hdr.seq_num);
                    timestamped_log("TIMEOUT SEQ=%u", seq);
                    safe_sendto(sock, &slots[i].pkt, slots[i].len, 0, (struct sockaddr*)&srv, srv_len);
                    slots[i].sent_time_ms = now_ms();
                    timestamped_log("RETX DATA SEQ=%u LEN=%ld", seq, slots[i].len - (long)sizeof(struct sham_header));
                }
            }
            sleep_ms(10);
        }
        fclose(fp);

        // --- File Transfer Termination ---
        // client.c

        // --- File Transfer Termination ---
        struct sham_packet finp; memset(&finp,0,sizeof(finp));
        finp.hdr.seq_num = htonl(next_seq);
        finp.hdr.flags = htons(SHAM_FIN);
        safe_sendto(sock, &finp, sizeof(struct sham_header), 0, (struct sockaddr*)&srv, srv_len);
        timestamped_log("SND FIN SEQ=%u", next_seq);

        long long fin_sent_time = now_ms();
        bool ack_for_fin_rcvd = false;
        bool server_fin_rcvd = false;
        uint32_t server_fin_seq = 0;

        // Single loop to wait for both ACK and FIN
        while (!ack_for_fin_rcvd || !server_fin_rcvd) {
            ssize_t rc = recvfrom(sock, &rcv, sizeof(rcv), 0, NULL, NULL);
            if (rc > 0) {
                uint16_t flags = ntohs(rcv.hdr.flags);
                // Check for ACK of our FIN
                if ((flags & SHAM_ACK) && ntohl(rcv.hdr.ack_num) == next_seq + 1) {
                    if (!ack_for_fin_rcvd) timestamped_log("RCV ACK FOR FIN");
                    ack_for_fin_rcvd = true;
                }
                // Check for server's FIN
                if (flags & SHAM_FIN) {
                    if (!server_fin_rcvd) {
                         server_fin_seq = ntohl(rcv.hdr.seq_num);
                         timestamped_log("RCV FIN SEQ=%u", server_fin_seq);
                         server_fin_rcvd = true;
                    }
                }
            } else {
                // Timeout logic
                if (now_ms() - fin_sent_time > RTO_MS && !ack_for_fin_rcvd) {
                    timestamped_log("TIMEOUT on client FIN, RETX FIN SEQ=%u", next_seq);
                    safe_sendto(sock, &finp, sizeof(struct sham_header), 0, (struct sockaddr*)&srv, srv_len);
                    fin_sent_time = now_ms();
                }
            }
            // Break if we are done and have waited a bit
            if (ack_for_fin_rcvd && server_fin_rcvd) {
                 break;
            }
        }

        // Send the final ACK for the server's FIN
        if(server_fin_rcvd) {
            struct sham_packet final_ack; memset(&final_ack, 0, sizeof(final_ack));
            final_ack.hdr.flags = htons(SHAM_ACK);
            final_ack.hdr.ack_num = htonl(server_fin_seq + 1);
            safe_sendto(sock, &final_ack, sizeof(struct sham_header), 0, (struct sockaddr*)&srv, srv_len);
            timestamped_log("SND FINAL ACK=%u", ntohl(final_ack.hdr.ack_num));
        }

        sleep_ms(2 * RTO_MS); // Wait briefly to ensure final ACK is sent
    } // End of file transfer mode

    close_log();
    close(sock);
    printf("Connection closed.\n");
    return 0;
}
//#llm generated code ends