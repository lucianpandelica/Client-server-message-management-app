#ifndef _COMMON_H_
#define _COMMON_H_

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#define MAX_ID_SIZE 11
#define MAXBUFSIZE 1552
#define BACKLOG 10
#define BUF_LEN 60 /* pentru mesaje TCP */

#define MAX_TOPIC_LEN 50
#define MAX_CONTENT_LEN 1500

#define MAX_TCP_MESS 3500

#define DLE (char)16
#define STX (char)2
#define ETX (char)3

#define ID_PASS_TYPE 0
#define SUBS_TYPE 1
#define UNSUBS_TYPE 2

/* structura header TCP */
struct __attribute__((packed)) tcp_hdr {
    uint8_t op_type;
    uint16_t size;
};

/* functii comune server - client */

#endif