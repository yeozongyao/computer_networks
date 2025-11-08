#pragma once
#include <stdint.h>
#define MAX_DU_PAYLOAD 8192

#pragma pack(push,1)
typedef struct {
  uint32_t seq;
  uint32_t batch_id;
  uint16_t len;
  uint8_t  fin;
} du_hdr_t;

typedef struct {
  uint32_t batch_id;
  uint32_t next_seq;
} ack_t;
#pragma pack(pop)


//pragma push and pop used to ensure no padding is added to the structs and only the necessary bytes as defined are used
//this is important to ensure the data structure is consistent across different systems
//du_hdr_t defines the header for a data unit with fields for sequence number, batch ID, payload length and a finish flag
//ack_t defines the structure for acknowledgments with fields for batch ID and the next expected sequence number
