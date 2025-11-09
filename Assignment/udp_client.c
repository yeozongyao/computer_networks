#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/select.h>
#include "headsock.h"

#define TIMEOUT_MS 800
#define MAX_RETRIES 50

int main(int argc, char *argv[])
{
  if (argc < 5)
  {
    fprintf(stderr, "Usage: %s <server_ip> <file_path> <du_size> <mode[varying|stop]>\n", argv[0]);
    exit(1);
  }

  char *server_ip = argv[1];
  char *file_path = argv[2];
  int DU = atoi(argv[3]);
  int varying = strcmp(argv[4], "varying") == 0;

  if (DU <= 0 || DU > MAX_DU_PAYLOAD)
  {
    fprintf(stderr, "Invalid DU size (1..%d)\n", MAX_DU_PAYLOAD);
    return 1;
  }

  // open file
  FILE *fp = fopen(file_path, "rb");
  if (!fp)
  {
    perror("fopen");
    return 1;
  }
  fseek(fp, 0, SEEK_END);
  long fsize = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  unsigned char *buf = (unsigned char *)malloc(fsize);
  if (!buf)
  {
    perror("malloc");
    fclose(fp);
    return 1;
  }
  if (fread(buf, 1, fsize, fp) != (size_t)fsize)
  {
    perror("fread");
    free(buf);
    fclose(fp);
    return 1;
  }
  fclose(fp);

  // create UDP socket
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0)
  {
    perror("socket");
    free(buf);
    return 1;
  }

  struct sockaddr_in srv;
  memset(&srv, 0, sizeof(srv));
  srv.sin_family = AF_INET;
  srv.sin_port = htons(9000);
  if (inet_pton(AF_INET, server_ip, &srv.sin_addr) != 1)
  {
    fprintf(stderr, "inet_pton failed\n");
    free(buf);
    close(sock);
    return 1;
  }

  // size pattern 1 - 2 - 3
  int pattern[3] = {1, 2, 3};
  int pat_i = 0;

  // strict-batch state
  uint32_t total_du = (uint32_t)((fsize + DU - 1) / DU);
  uint32_t seq = 0; 
  uint32_t batch_id = 0;
  size_t sent_bytes = 0;
  int timeout_count = 0;

  struct timeval t0, t1;
  gettimeofday(&t0, NULL);

  while (seq < total_du)
  {
    int batch_sz = varying ? pattern[pat_i] : 1;
    uint32_t target = seq + (uint32_t)batch_sz;
    if (target > total_du)
      target = total_du;

    batch_id++;
    int tries = 0;

  resend_batch:
    // send the entire batch [seq .. target-1]
    for (uint32_t cur = seq; cur < target; ++cur)
    {
      long offset = (long)cur * DU;
      uint16_t len = (uint16_t)((offset + DU <= fsize) ? DU : (fsize - offset));

      du_hdr_t hdr = {.seq = cur, .batch_id = batch_id, .len = len, .fin = (offset + len == fsize)};
      unsigned char pkt[sizeof(hdr) + MAX_DU_PAYLOAD];
      memcpy(pkt, &hdr, sizeof(hdr));
      memcpy(pkt + sizeof(hdr), buf + offset, len);

      ssize_t s = sendto(sock, pkt, sizeof(hdr) + len, 0,
                         (struct sockaddr *)&srv, sizeof(srv));
      if (s < 0)
      {
        perror("sendto");
        free(buf);
        close(sock);
        return 1;
      }
      sent_bytes += len; // counts retransmissions too (for data_rate)
    }

    // wait for a single ACK that confirms the whole batch
    for (;;)
    {
      fd_set rf;
      FD_ZERO(&rf);
      FD_SET(sock, &rf);
      struct timeval to = {.tv_sec = 0, .tv_usec = TIMEOUT_MS * 1000};
      int r = select(sock + 1, &rf, NULL, NULL, &to);
      if (r == 0)
      {
        timeout_count++;
        if (++tries >= MAX_RETRIES)
        {
          fprintf(stderr, "Exceeded max retries (batch_id=%u)\n", batch_id);
          free(buf);
          close(sock);
          return 2;
        }
        goto resend_batch;
      }

      ack_t ack;
      struct sockaddr_in from;
      socklen_t flen = sizeof(from);
      ssize_t n = recvfrom(sock, &ack, sizeof(ack), 0,
                           (struct sockaddr *)&from, &flen);
      if (n != (ssize_t)sizeof(ack))
        continue;
      if (ack.batch_id != batch_id)
        continue;

      // strict: only accept if entire batch was acknowledged
      if (ack.next_seq == target)
      {
        seq = target;
        if (varying)
          pat_i = (pat_i + 1) % 3; // rotate 1 → 2 → 3
        break;
      }
      else
      {
        if (++tries >= MAX_RETRIES)
        {
          fprintf(stderr, "Exceeded max retries (batch_id=%u)\n", batch_id);
          free(buf);
          close(sock);
          return 2;
        }
        goto resend_batch;
      }
    }
  }

  gettimeofday(&t1, NULL);
  double ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_usec - t0.tv_usec) / 1000.0;
  double tput_orig = (fsize * 8.0) / (ms * 1000.0);
  double rate_sent = (sent_bytes * 8.0) / (ms * 1000.0);

  printf("mode=%s du=%d file_bytes=%ld sent_bytes=%zu time_ms=%.3f throughput_Mbps=%.6f data_rate_Mbps=%.6f timeouts=%d\n",
         varying ? "varying" : "stop", DU, fsize, sent_bytes, ms, tput_orig, rate_sent, timeout_count);
  fflush(stdout);

  free(buf);
  close(sock);
  return 0;
}
