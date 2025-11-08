#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/select.h>
#include "headsock.h"

#define TIMEOUT_MS 300

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

  // open file
  FILE *fp = fopen(file_path, "rb");
  fseek(fp, 0, SEEK_END);
  long fsize = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  unsigned char *buf = malloc(fsize);
  fread(buf, 1, fsize, fp);
  fclose(fp);

  // create UDP socket
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  struct sockaddr_in srv;
  srv.sin_family = AF_INET;
  srv.sin_port = htons(9000);
  inet_pton(AF_INET, server_ip, &srv.sin_addr);

  // size pattern 1 to 2 to 3
  int pattern[3] = {1, 2, 3};
  int pat_i = 0;
  uint32_t seq = 0, next_ack = 0, batch_id = 0;
  size_t sent_bytes = 0;
  struct timeval t0, t1;
  gettimeofday(&t0, NULL);

  while ((long)(seq * DU) < fsize || (seq * DU == fsize && next_ack < seq))
  {
    int batch_sz = varying ? pattern[pat_i] : 1;
    if (varying)
      pat_i = (pat_i + 1) % 3;
    batch_id++;
    uint32_t first_seq = seq;

  resend_batch:
    // send each DU in batch
    for (int k = 0; k < batch_sz; k++)
    {
      uint32_t cur = first_seq + k;
      long offset = (long)cur * DU;
      if (offset >= fsize)
        break;
      uint16_t len = (fsize - offset >= DU) ? DU : (uint16_t)(fsize - offset);

      du_hdr_t hdr = {.seq = cur, .batch_id = batch_id, .len = len, .fin = (offset + len == fsize)};
      unsigned char pkt[sizeof(hdr) + MAX_DU_PAYLOAD];
      memcpy(pkt, &hdr, sizeof(hdr));
      memcpy(pkt + sizeof(hdr), buf + offset, len);
      sendto(sock, pkt, sizeof(hdr) + len, 0,
             (struct sockaddr *)&srv, sizeof(srv));
      sent_bytes += len;
    }

    // wait for ACK
    for (;;)
    {
      fd_set rf;
      FD_ZERO(&rf);
      FD_SET(sock, &rf);
      struct timeval to = {.tv_sec = 0, .tv_usec = TIMEOUT_MS * 1000};
      int r = select(sock + 1, &rf, NULL, NULL, &to);
      if (r == 0)
      {
        goto resend_batch;
      }
      ack_t ack;
      struct sockaddr_in from;
      socklen_t flen = sizeof(from);
      ssize_t n = recvfrom(sock, &ack, sizeof(ack), 0,
                           (struct sockaddr *)&from, &flen);
      if (n != sizeof(ack) || ack.batch_id != batch_id)
        continue;
      next_ack = ack.next_seq;
      if (next_ack >= first_seq + batch_sz || (next_ack * DU) >= fsize)
      {
        seq = next_ack;
        break;
      }
      else
      {
        first_seq = next_ack;
        goto resend_batch;
      }
    }
  }

  gettimeofday(&t1, NULL);
  double ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_usec - t0.tv_usec) / 1000.0;
  printf("Sent %ld bytes in %.3f ms\n", fsize, ms);
  printf("Throughput: %.3f Mbps\n", (fsize * 8.0) / (ms * 1000.0));

  free(buf);
  close(sock);
  return 0;
}
