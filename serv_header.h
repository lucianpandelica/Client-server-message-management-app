#ifndef _SERV_HEADER_H_
#define _SERV_HEADER_H_

#include "common.h"
#include "queue.h"

/* starea conexiunii unui client */
enum connection_state {
	STATE_CONNECTED,
    STATE_DISCONNECTED,
    STATE_ID_NEEDED
};

/*
 * structura ce retine atributele unui client, pentru conexiunea curenta
 * (atributele se pot schimba la reconectare - sockfd, ip, port)
 */
typedef struct client {
    char id[MAX_ID_SIZE];
    int sockfd;
    in_addr_t ip;
    in_port_t port;
    enum connection_state state;
    char recv_buf[BUF_LEN];
    size_t recv_len;
    uint8_t recv_DLE;
    uint8_t recv_seq_end;
    queue task_q;
} Client;

typedef struct database {
    Client* v;
    int len;
    int cap;
} Database;

struct __attribute__((packed)) udp_message {
    char topic[MAX_TOPIC_LEN];
    uint8_t data_type;
    char content[MAX_CONTENT_LEN];
};

/*
 * structura pentru datagrama primita de la client UDP, cu scopul de
 * a fi retinuta in memorie. retinem pana cand ref_count devine 0
 */
typedef struct datagram {
    char buf[MAX_TCP_MESS]; // retine bufferul prelucrat
    int len;
    int ref_count;
} Datagram;

typedef struct subs {
    Client* c;
    uint8_t sf;
} Subs;

typedef struct task {
    Datagram* d;
    int bytes_sent;
} Task;

#endif