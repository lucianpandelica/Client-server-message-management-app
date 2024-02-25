#ifndef _CLI_HEADER_H_
#define _CLI_HEADER_H_

#include <stdint.h>
#include "common.h"

/* formate payload UDP */
struct __attribute__((packed)) int_payload {
    uint8_t sign;
    uint32_t num;
};

struct __attribute__((packed)) short_payload {
    uint16_t abs_t_100;
};

struct __attribute__((packed)) float_payload {
    uint8_t sign;
    uint32_t abs_t_pow;
    uint8_t abs_pow; // power of ten
};

typedef struct task_client {
    char buf[MAX_TCP_MESS];
    int bytes_total;
    int bytes_sent;
    int type;
} Task_client;

#endif