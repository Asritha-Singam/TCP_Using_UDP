// server.c
//#llm generated codes begins
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
#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>
#include <openssl/md5.h>
#include <stdarg.h>
#include <sys/select.h>

#include "sham.h"

#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#define RTO_MS 500
#define RECV_BUF_SLOTS 1024

static FILE *log_file = NULL;
static int logging_enabled = 0;

static void open_log(const char *name) {
    char *env = getenv("RUDP_LOG");
    if (env && strcmp(env, "1") == 0) {
        logging_enabled = 1;
        log_file = fopen(name, "w");
    }
}
static void close_logfile(void) {
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
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
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

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: ./server <port> [--chat] [loss_rate]\n");
        return 1;
    }

    srand((unsigned)time(NULL));

    int port = atoi(argv[1]);
    bool chat_mode = false;
    double loss_rate = 0.0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--chat") == 0) {
            chat_mode = true;
        } else {
            loss_rate = atof(argv[i]);
        }
    }

    open_log("server_log.txt");

    int sock = create_udp_socket();
    struct sockaddr_in me; memset(&me, 0, sizeof(me));
    me.sin_family = AF_INET; me.sin_addr.s_addr = INADDR_ANY; me.sin_port = htons(port);
    if (bind(sock, (struct sockaddr*)&me, sizeof(me)) < 0) { perror("bind"); close_logfile(); close(sock); return 1; }
    fcntl(sock, F_SETFL, O_NONBLOCK);

    printf("Server listening on port %d...\n", port);

    struct sockaddr_in cli; socklen_t cli_len = sizeof(cli);
    struct sham_packet rcv;

    // --------- Three-way handshake ----------
    ssize_t rc;
    uint32_t client_isn = 0, server_isn = 0;
    while (1) {
        rc = recvfrom(sock, &rcv, sizeof(rcv), 0, (struct sockaddr*)&cli, &cli_len);
        if (rc >= (ssize_t)sizeof(struct sham_header)) {
            if (ntohs(rcv.hdr.flags) & SHAM_SYN) {
                client_isn = ntohl(rcv.hdr.seq_num);
                timestamped_log("RCV SYN SEQ=%u", client_isn);

                server_isn = (uint32_t)(rand() & 0x7fffffff);
                struct sham_packet synack; memset(&synack, 0, sizeof(synack));
                synack.hdr.seq_num = htonl(server_isn);
                synack.hdr.ack_num = htonl(client_isn + 1);
                synack.hdr.flags = htons(SHAM_SYN | SHAM_ACK);
                safe_sendto(sock, &synack, sizeof(struct sham_header), 0, (struct sockaddr*)&cli, cli_len);
                timestamped_log("SND SYN-ACK SEQ=%u ACK=%u", server_isn, client_isn + 1);

                long long start = now_ms();
                bool handshake_complete = false;
                while (now_ms() - start < 5000) {
                    ssize_t r = recvfrom(sock, &rcv, sizeof(rcv), 0, (struct sockaddr*)&cli, &cli_len);
                    if (r >= (ssize_t)sizeof(struct sham_header) && (ntohs(rcv.hdr.flags) & SHAM_ACK) && ntohl(rcv.hdr.ack_num) == server_isn + 1) {
                        timestamped_log("RCV ACK FOR SYN");
                        handshake_complete = true;
                        break;
                    }
                    sleep_ms(20);
                }
                if (handshake_complete) break;
            }
        } else sleep_ms(50);
    }
    
    uint32_t server_seq = server_isn + 1;
    
    // ----------- Main Logic: Chat or File Transfer -----------
    if (chat_mode) {
        // Chat mode logic as before...
        printf("Chat mode server established. Type messages, /quit to exit.\n");
        fd_set rfds;
        char buf[2048];
        int maxfd = sock > STDIN_FILENO ? sock : STDIN_FILENO;

        while (1) {
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
                        // **** SERVER-INITIATED TERMINATION ****
                        struct sham_packet fin; memset(&fin, 0, sizeof(fin));
                        fin.hdr.seq_num = htonl(server_seq);
                        fin.hdr.flags = htons(SHAM_FIN);
                        safe_sendto(sock, &fin, sizeof(struct sham_header), 0, (struct sockaddr*)&cli, cli_len);
                        timestamped_log("SND FIN SEQ=%u", server_seq);

                        bool ack_for_fin_rcvd = false;
                        bool client_fin_rcvd = false;
                        long long start = now_ms();

                        while(now_ms() - start < 5000) {
                            ssize_t r = recvfrom(sock, &rcv, sizeof(rcv), 0, (struct sockaddr*)&cli, &cli_len);
                            if (r > 0) {
                                uint16_t flags = ntohs(rcv.hdr.flags);
                                if ((flags & SHAM_ACK) && ntohl(rcv.hdr.ack_num) == server_seq + 1) {
                                    timestamped_log("RCV ACK FOR FIN");
                                    ack_for_fin_rcvd = true;
                                }
                                if (flags & SHAM_FIN) {
                                    uint32_t client_fin_seq = ntohl(rcv.hdr.seq_num);
                                    timestamped_log("RCV FIN SEQ=%u", client_fin_seq);
                                    
                                    struct sham_packet final_ack; memset(&final_ack, 0, sizeof(final_ack));
                                    final_ack.hdr.flags = htons(SHAM_ACK);
                                    final_ack.hdr.ack_num = htonl(client_fin_seq + 1);
                                    safe_sendto(sock, &final_ack, sizeof(struct sham_header), 0, (struct sockaddr*)&cli, cli_len);
                                    timestamped_log("SND ACK=%u", ntohl(final_ack.hdr.ack_num));
                                    client_fin_rcvd = true;
                                }
                            }
                            if (ack_for_fin_rcvd && client_fin_rcvd) break;
                            sleep_ms(20);
                        }
                        goto cleanup_and_exit;
                    }
                    struct sham_packet dp; memset(&dp,0,sizeof(dp));
                    dp.hdr.seq_num = htonl(server_seq);
                    snprintf(dp.data, sizeof(dp.data), "%s", buf);
                    size_t ml = strlen(dp.data);
                    safe_sendto(sock, &dp, sizeof(struct sham_header) + ml, 0, (struct sockaddr*)&cli, cli_len);
                    timestamped_log("SND DATA SEQ=%u LEN=%zu", server_seq, ml);
                    server_seq += ml;
                }
                if (FD_ISSET(sock, &rfds)) {
                    ssize_t r = recvfrom(sock, &rcv, sizeof(rcv), 0, NULL, NULL);
                    //for loss in chat
                    if (r > 0 && loss_rate > 0.0) {
                        uint16_t flags = ntohs(rcv.hdr.flags);
                        // Only drop data packets
                        if (!(flags & (SHAM_SYN|SHAM_ACK|SHAM_FIN))) {
                            if (((double)rand() / RAND_MAX) < loss_rate) {
                                timestamped_log("DROP DATA SEQ=%u", ntohl(rcv.hdr.seq_num));
                                continue; // Drop the packet
                            }
                        }
                    }
                    //loss block end
                    if (r >= (ssize_t)sizeof(struct sham_header)) {
                        uint16_t flags = ntohs(rcv.hdr.flags);
                        if (flags & SHAM_FIN) {
                             // **** CLIENT-INITIATED TERMINATION ****
                            uint32_t peer_fin_seq = ntohl(rcv.hdr.seq_num);
                            timestamped_log("RCV FIN SEQ=%u", peer_fin_seq);
                            
                            struct sham_packet ack_for_fin; memset(&ack_for_fin,0,sizeof(ack_for_fin));
                            ack_for_fin.hdr.flags = htons(SHAM_ACK);
                            ack_for_fin.hdr.ack_num = htonl(peer_fin_seq + 1);
                            safe_sendto(sock, &ack_for_fin, sizeof(struct sham_header), 0, (struct sockaddr*)&cli, cli_len);
                            timestamped_log("SND ACK FOR FIN");

                            struct sham_packet server_fin; memset(&server_fin,0,sizeof(server_fin));
                            server_fin.hdr.seq_num = htonl(server_seq);
                            server_fin.hdr.flags = htons(SHAM_FIN);
                            safe_sendto(sock, &server_fin, sizeof(struct sham_header), 0, (struct sockaddr*)&cli, cli_len);
                            timestamped_log("SND FIN SEQ=%u", server_seq);

                            bool final_ack_rcvd = false;
                            long long start = now_ms();
                            while(now_ms() - start < 5000) {
                                ssize_t r2 = recvfrom(sock, &rcv, sizeof(rcv), 0, NULL, NULL);
                                if (r2 >= (ssize_t)sizeof(struct sham_header) && (ntohs(rcv.hdr.flags) & SHAM_ACK) && ntohl(rcv.hdr.ack_num) == server_seq + 1) {
                                    timestamped_log("RCV ACK=%u", ntohl(rcv.hdr.ack_num));
                                    final_ack_rcvd = true;
                                    break;
                                }
                                sleep_ms(20);
                            }
                            goto cleanup_and_exit;
                        } else {
                            if (r > (ssize_t)sizeof(struct sham_header)) {
                                timestamped_log("RCV DATA SEQ=%u LEN=%zu", ntohl(rcv.hdr.seq_num), r - sizeof(struct sham_header));
                                size_t len = r - sizeof(struct sham_header);
                                printf("Client: %.*s\n", (int)len, rcv.data);
                            }
                        }
                    }
                }
            }
        }
    } else {
        // **** FILE TRANSFER LOGIC STARTS HERE ****
        rc = -1;
        long long fname_start = now_ms();
        while (rc <= 0) {
            rc = recvfrom(sock, &rcv, sizeof(rcv), 0, (struct sockaddr*)&cli, &cli_len);
            if (rc > 0) break;
            if (now_ms() - fname_start > 20000) { fprintf(stderr,"Timeout waiting for filename\n"); goto cleanup_and_exit; }
            sleep_ms(50);
        }

        size_t fnlen = (rc > (ssize_t)sizeof(struct sham_header)) ? (size_t)rc - sizeof(struct sham_header) : 0;
        if (fnlen == 0 || fnlen >= 1024) { fprintf(stderr, "Invalid filename received\n"); goto cleanup_and_exit; }
        
        char output_filename[1024];
        memcpy(output_filename, rcv.data, fnlen);
        output_filename[fnlen] = '\0';
        timestamped_log("RCV FILENAME %s", output_filename);

        uint32_t expected_seq = ntohl(rcv.hdr.seq_num) + (uint32_t)fnlen;

        FILE *out = fopen(output_filename, "wb");
        if (!out) { perror("fopen"); goto cleanup_and_exit; }
        MD5_CTX md5ctx;
        MD5_Init(&md5ctx);

        struct sham_packet ackpkt; memset(&ackpkt,0,sizeof(ackpkt));
        ackpkt.hdr.flags = htons(SHAM_ACK);
        ackpkt.hdr.ack_num = htonl(expected_seq);
        safe_sendto(sock, &ackpkt, sizeof(struct sham_header), 0, (struct sockaddr*)&cli, cli_len);
        timestamped_log("SND ACK=%u WIN=65535", expected_seq);

        bool got_fin_from_client = false;

        while (!got_fin_from_client) {
            rc = recvfrom(sock, &rcv, sizeof(rcv), 0, (struct sockaddr*)&cli, &cli_len);
            if (rc > 0) {
                uint16_t flags = ntohs(rcv.hdr.flags);
                if (loss_rate > 0.0 && !(flags & (SHAM_SYN|SHAM_ACK|SHAM_FIN))) {
                    if (((double)rand() / RAND_MAX) < loss_rate) { 
                        timestamped_log("DROP DATA SEQ=%u", ntohl(rcv.hdr.seq_num)); 
                        continue; 
                    }
                }

                if (flags & SHAM_FIN) {
                    uint32_t client_fin_seq = ntohl(rcv.hdr.seq_num);
                    timestamped_log("RCV FIN SEQ=%u", client_fin_seq);
                    ackpkt.hdr.ack_num = htonl(client_fin_seq + 1);
                    safe_sendto(sock, &ackpkt, sizeof(struct sham_header), 0, (struct sockaddr*)&cli, cli_len);
                    timestamped_log("SND ACK FOR FIN");
                    got_fin_from_client = true;
                    continue; // Continue to 4-way handshake
                }

                uint32_t seq = ntohl(rcv.hdr.seq_num);
                size_t data_len = (size_t)rc - sizeof(struct sham_header);
                timestamped_log("RCV DATA SEQ=%u LEN=%zu", seq, data_len);

                if (seq == expected_seq) {
                    if (data_len > 0) {
                        fwrite(rcv.data, 1, data_len, out);
                        MD5_Update(&md5ctx, rcv.data, data_len);
                    }
                    expected_seq += (uint32_t)data_len;
                }
                
                ackpkt.hdr.ack_num = htonl(expected_seq);
                safe_sendto(sock, &ackpkt, sizeof(struct sham_header), 0, (struct sockaddr*)&cli, cli_len);
                timestamped_log("SND ACK=%u WIN=65535", expected_seq);

            } else {
                sleep_ms(10);
            }
        }

        // --- Four-way handshake (server side) ---
        struct sham_packet server_fin; memset(&server_fin,0,sizeof(server_fin));
        server_fin.hdr.seq_num = htonl(server_seq);
        server_fin.hdr.flags = htons(SHAM_FIN);
        safe_sendto(sock, &server_fin, sizeof(struct sham_header), 0, (struct sockaddr*)&cli, cli_len);
        timestamped_log("SND FIN SEQ=%u", server_seq);
        
        long long fin_sent_time = now_ms();
        bool final_ack_rcvd = false;
        
        while(!final_ack_rcvd) {
             rc = recvfrom(sock, &rcv, sizeof(rcv), 0, NULL, NULL);
             if (rc > 0 && (ntohs(rcv.hdr.flags) & SHAM_ACK) && (ntohl(rcv.hdr.ack_num) == server_seq + 1)) {
                 timestamped_log("RCV FINAL ACK=%u", ntohl(rcv.hdr.ack_num));
                 final_ack_rcvd = true;
             } else {
                 // Timeout logic
                 if (now_ms() - fin_sent_time > RTO_MS) {
                     timestamped_log("TIMEOUT on server FIN, RETX FIN SEQ=%u", server_seq);
                     safe_sendto(sock, &server_fin, sizeof(struct sham_header), 0, (struct sockaddr*)&cli, cli_len);
                     fin_sent_time = now_ms();
                 }
             }
             if (now_ms() - fin_sent_time > 4000) { // Safety break
                 timestamped_log("TIMEOUT waiting for final ACK, closing.");
                 break;
             }
        }

        fclose(out);
        unsigned char md5sum[MD5_DIGEST_LENGTH];
        MD5_Final(md5sum, &md5ctx);
        printf("MD5: ");
        for (int i = 0; i < MD5_DIGEST_LENGTH; ++i) printf("%02x", md5sum[i]);
        printf("\n");
    } // End of file transfer mode

cleanup_and_exit:
    close_logfile();
    close(sock);
    printf("Connection closed.\n");
    return 0;
}
//#llm generated code ends