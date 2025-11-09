#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include "headsock.h"

int main(int argc, char *argv[])
{
  if (argc < 2)
  {
    fprintf(stderr, "Usage: %s <output_file>\n", argv[0]);
    exit(1);
  }
  char *outfile = argv[1];
  FILE *out = fopen(outfile, "wb");

  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  struct sockaddr_in addr, cli;
  socklen_t clen = sizeof(cli);
  addr.sin_family = AF_INET;
  addr.sin_port = htons(9000);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  bind(sock, (struct sockaddr *)&addr, sizeof(addr));

  uint32_t expected_seq = 0;
  int fin_seen = 0;
  size_t total_bytes = 0;
  struct timeval t0, t1;
  gettimeofday(&t0, NULL);

  while (1)
  {
    unsigned char pkt[sizeof(du_hdr_t) + MAX_DU_PAYLOAD];
    ssize_t n = recvfrom(sock, pkt, sizeof(pkt), 0,
                         (struct sockaddr *)&cli, &clen);
    if (n < (ssize_t)sizeof(du_hdr_t))
      continue;
    du_hdr_t hdr;
    memcpy(&hdr, pkt, sizeof(hdr));
    unsigned char *payload = pkt + sizeof(hdr);

    int cycle[3] = {1, 2, 3};
    int cidx = 0;
    int got_in_cycle = 0;

    if (hdr.seq == expected_seq)
    {
      fwrite(payload, 1, hdr.len, out);
      total_bytes += hdr.len;
      expected_seq++;
      got_in_cycle++;
      if (hdr.fin)
        fin_seen = 1;
    }

    if (got_in_cycle >= cycle[cidx])
    {
      ack_t ack = {.batch_id = hdr.batch_id, .next_seq = expected_seq};
      sendto(sock, &ack, sizeof(ack), 0, (struct sockaddr *)&cli, clen);
      got_in_cycle = 0;
      cidx = (cidx + 1) % 3;
    }

    if (fin_seen && hdr.seq + 1 == expected_seq)
      break;
  }

  gettimeofday(&t1, NULL);
  double ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_usec - t0.tv_usec) / 1000.0;
  printf("Received %zu bytes in %.3f ms\n", total_bytes, ms);
  printf("Throughput: %.3f Mbps\n", (total_bytes * 8.0) / (ms * 1000.0));

  fclose(out);
  close(sock);
  return 0;
}
