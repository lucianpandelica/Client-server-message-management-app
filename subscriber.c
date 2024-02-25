#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/epoll.h>
#include <math.h>

#include "w_epoll.h"
#include "util.h"
#include "queue.h"
#include "cli_header.h"

int socket_desc;
uint16_t server_port;
struct sockaddr_in server_addr;
struct in_addr ip;
int epollfd;

char server_message[MAX_TCP_MESS];
int serv_len;

int debug_enabled;
uint8_t recv_DLE;
uint8_t recv_seq_end;
queue task_q_client;

void print_debug(char* string) {
    if(debug_enabled == 0)
        return;
    
    printf("(DEBUG) %s", string);
}

Task_client* prepare_message(char* buf, int buf_len) {

    /* analog 'prepare_datagram' din server */
    Task_client* new_t = (Task_client*) malloc(sizeof(Task_client));
    memset(new_t->buf, 0, MAX_TCP_MESS);

    int ct = 0;

    new_t->buf[ct] = DLE;
    ct++;
    new_t->buf[ct] = STX;
    ct++;

    for (int i = 0; i < buf_len; i++) {
        /* escape la escape */
        if (buf[i] == DLE) {
            new_t->buf[ct] = DLE;
            ct++;
        }
            
        new_t->buf[ct] = buf[i];
        ct++;
    }

    new_t->buf[ct] = DLE;
    ct++;
    new_t->buf[ct] = ETX;
    ct++;

    /* setam lungimea mesajului encodat */
    new_t->bytes_total = ct;
    /* initializam numarul de bytes trimisi */
    new_t->bytes_sent = 0;

    return new_t;
}

void fill_serv_addr() {
    memset(&server_addr, 0, sizeof(server_addr));

    /* completam cu informatia serverului */
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = ip.s_addr;
    server_addr.sin_port = htons(server_port);
}

void connect_to_server() {
    int res;

    /* creare socket */
    socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    DIE(socket_desc < 0, "socket");

    /* setam optiunile acestuia */
    int optval = 1;
    res = setsockopt(socket_desc, SOL_SOCKET, TCP_NODELAY, &optval, sizeof(optval));
    DIE(res < 0, "setsockopt");

    res = setsockopt(socket_desc, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    DIE(res < 0, "setsockopt");
    
    /* ne conectam la server */
    res = connect(socket_desc, (struct sockaddr*)&server_addr, sizeof(server_addr));
    DIE(res < 0, "connect");
}

void create_id_pass_task(char* id, int id_len) {
    /* id are la final \0 */

    /* functia se comporta ca principiu analog 'create_tasks' din server */
    int res;

    int mess_size = sizeof(struct tcp_hdr) + id_len;
    char* packed_mess = (char*) malloc(mess_size * sizeof(char));
    memset(packed_mess, 0, mess_size);

    struct tcp_hdr* mess = (struct tcp_hdr*) packed_mess;
    mess->op_type = ID_PASS_TYPE;
    mess->size = (uint16_t) mess_size;

    memcpy(packed_mess + sizeof(struct tcp_hdr), id, id_len);

    Task_client* t;
    t = prepare_message(packed_mess, mess_size);
    t->type = ID_PASS_TYPE;

    /* adaugam task-ul la coada de task-uri */
    queue_enq(task_q_client, t);

    /* setam socket-ul pentru evenimente de out */
    res = w_epoll_update_fd_inout(epollfd, socket_desc);
	DIE(res < 0, "w_epoll_update_fd_inout");

    free(packed_mess);
}

void close_client() {
    int res;

    res = w_epoll_remove_fd(epollfd, socket_desc);
    DIE(res < 0, "w_epoll_remove_fd");

    close(socket_desc);

    /* dezalocam resursele */
}

void print_message(char* topic,
                   uint16_t topic_size, 
                   char* content, 
                   uint16_t content_size, 
                   struct in_addr ip_addr,
                   uint16_t port,
                   uint8_t data_type) {
    
    uint32_t num_32;
    uint16_t num_16;

    int final_int = 0;
    float final_short_real = 0;
    float final_float = 0;
    double abs_pow;

    struct int_payload* int_p;
    struct short_payload* short_p;
    struct float_payload* float_p;

    switch (data_type) {
        case 0:
            int_p = (struct int_payload*) content;
            num_32 = ntohl(int_p->num);

            final_int = 0;
            final_int = final_int + num_32;

            /* in functie de byte-ul de semn */
            if (int_p->sign == 1) {
                final_int = - final_int;
            }

            printf("%s:%u - %s - INT - %d\n", inet_ntoa(ip_addr),
                                              port,
                                              topic, 
                                              final_int);
            break;
        
        case 1:
            short_p = (struct short_payload*) content;
            num_16 = ntohs(short_p->abs_t_100);

            final_short_real = 0;
            final_short_real = final_short_real + num_16;

            final_short_real = final_short_real / 100;

            printf("%s:%u - %s - SHORT_REAL - %.2f\n", inet_ntoa(ip_addr),
                                                       port,
                                                       topic,
                                                       final_short_real);
            break;

        case 2:
            float_p = (struct float_payload*) content;
            num_32 = ntohl(float_p->abs_t_pow);

            final_float = 0;
            final_float = final_float + num_32;

            abs_pow = pow(10, (float_p->abs_pow));
            final_float = final_float / abs_pow;

            if (float_p->sign == 1) {
                final_float = - final_float;
            }

            printf("%s:%u - %s - FLOAT - %.*f\n", inet_ntoa(ip_addr),
                                                  port,
                                                  topic,
                                                  float_p->abs_pow,
                                                  final_float);
            break;
        
        case 3:
            printf("%s:%u - %s - STRING - %s\n", inet_ntoa(ip_addr),
                                                 port,
                                                 topic,
                                                 content);
            break;
        
        default:
            break;
    }
}

void print_recv_message() {
    int off = 0;

    uint32_t ip;
    memcpy(&ip, server_message + off, sizeof(uint32_t));
    off = off + sizeof(uint32_t);

    uint16_t port;
    memcpy(&port, server_message + off, sizeof(uint16_t));
    off = off + sizeof(uint16_t);

    uint8_t data_type;
    memcpy(&data_type, server_message + off, sizeof(uint8_t));
    off = off + sizeof(uint8_t);

    uint16_t content_size;
    memcpy(&content_size, server_message + off, sizeof(uint16_t));
    off = off + sizeof(uint16_t);

    char* content = (char*) malloc((content_size + 1) * sizeof(char));
    memcpy(content, server_message + off, content_size);
    content[content_size] = '\0';
    off = off + content_size;

    uint16_t topic_size;
    memcpy(&topic_size, server_message + off, sizeof(uint16_t));
    off = off + sizeof(uint16_t);

    char* topic = (char*) malloc((topic_size + 1) * sizeof(char));
    memcpy(topic, server_message + off, topic_size);
    topic[topic_size] = '\0';
    off = off + topic_size;

    struct in_addr ip_addr;
    ip_addr.s_addr = ip;

    print_message(topic, topic_size, content, content_size, ip_addr, port, data_type);

    free(content);
    free(topic);
}

int handle_recv() {
    /* functia se comporta analog 'handle_tcp_recv' din server */
    int buf_len;
    char buf[MAX_TCP_MESS];
    memset(buf, 0, MAX_TCP_MESS);
    buf_len = recv(socket_desc, buf, MAX_TCP_MESS, 0);
    
    if(buf_len < 0) {
        print_debug("ERROR: Error while receiving server's msg.\n");
        close_client();
        return -1;
    }
    if(buf_len == 0) {
        print_debug("Connection closed from server.\n");
        close_client();
        return -1;
    }

    char charConv[2];
    charConv[1] = '\0';

    char byte;
    int i = 0;

    while (i < buf_len) {
        
        if (recv_DLE == 1) {
            /* luam ultimul byte din mesajul anterior */
            byte = DLE;
            i = -1;
        } else {
            byte = buf[i];
        }

        if (byte == DLE) {

            i++;
            /* daca am ajuns la finalul bufferului si ultimul
             * caracter intalnit e DLE */
            if (i == buf_len) {
                recv_DLE = 1;
                break;
            }

            byte = buf[i];
            recv_DLE = 0;

            if (byte == STX) {
                memset(server_message, 0, MAX_TCP_MESS);
                serv_len = 0;
                recv_seq_end = 0;
            } else if (byte == ETX) {
                print_recv_message();
                recv_seq_end = 1;
            } else if (byte == DLE) {
                if (recv_seq_end == 0) {
                    charConv[0] = byte;
                    //strcat(c->recv_buf, charConv);
                    memcpy(server_message + serv_len, charConv, 1);
                    serv_len = serv_len + 1;
                }
            } else {
                print_debug("Something gone wrong with message delimiters.\n");
            }
        } else {
            /* verificam daca ne aflam intre doua mesaje */
            if (recv_seq_end == 0) {
                charConv[0] = byte;
                memcpy(server_message + serv_len, charConv, 1);
                serv_len = serv_len + 1;
            }
        }

        i++;
    }

    return 0;
}

int handle_send() {
    int res;

    /* functia se comporta analog 'handle_tcp_send' din server */
    
    if (queue_empty(task_q_client)) {
        /* am incheiat trimiterea datelor */
        res = w_epoll_update_fd_in(epollfd, socket_desc);
		DIE(res < 0, "w_epoll_update_fd_in");

        return 0;
    }

    Task_client* crt_task = queue_deq(task_q_client);
    int num_bytes;
    int offset = crt_task->bytes_sent;
    int bytes_left = crt_task->bytes_total - crt_task->bytes_sent;

    /* incercam sa trimitem restul de bytes netrimisi */
    num_bytes = send(socket_desc, (crt_task->buf + offset), bytes_left, 0);

    if (num_bytes < 0) {
        print_debug("ERROR: Error in communication.\n");
        /* reintroducem in coada task-ul, deoarece nu l-am finalizat */
        queue_enq(task_q_client, crt_task);
        /* deconectam clientul */
        close_client();

        return -1;
    }
    if (num_bytes == 0) {
        print_debug("Connection closed.\n");
        queue_enq(task_q_client, crt_task);
        close_client();

        return -1;
    }
    if (num_bytes < bytes_left) {
        /* actualizam numarul de bytes trimisi */
        crt_task->bytes_sent = crt_task->bytes_sent + num_bytes;
        /* nu am terminat task-ul, il reintroducem in coada */
        queue_enq(task_q_client, crt_task);
    }

    /* task finalizat */
    crt_task->bytes_sent = crt_task->bytes_total;

    if (crt_task->type == SUBS_TYPE) {
        printf("Subscribed to topic.\n");
    } else if (crt_task->type == UNSUBS_TYPE) {
        printf("Unsubscribed from topic.\n");
    }

    free(crt_task);

    return 0;
}

void create_subs_task(char* topic, uint8_t sf) {
    int res;
    int topic_len = strlen(topic);
    int mess_size = sizeof(struct tcp_hdr) + sizeof(uint8_t) + (topic_len + 1);

    /* pregatim mesajul, alcatuim dupa protocol si adaugam delimitatorii */
    char* packed_mess = (char*) malloc(mess_size * sizeof(char));
    memset(packed_mess, 0, mess_size);

    struct tcp_hdr* mess = (struct tcp_hdr*) packed_mess;
    mess->op_type = SUBS_TYPE;
    mess->size = (uint16_t) mess_size;

    uint8_t sf_aux = sf;
    memcpy(packed_mess + sizeof(struct tcp_hdr), &sf_aux, sizeof(uint8_t));

    memcpy(packed_mess + sizeof(struct tcp_hdr) + sizeof(uint8_t), topic, topic_len);

    Task_client* t;
    t = prepare_message(packed_mess, mess_size);
    t->type = SUBS_TYPE;

    queue_enq(task_q_client, t);

    /* setam socket-ul pentru evenimente de out */
    res = w_epoll_update_fd_inout(epollfd, socket_desc);
	DIE(res < 0, "w_epoll_update_fd_inout");

    free(packed_mess);
}

void create_unsubs_task(char* topic) {
    int res;
    int topic_len = strlen(topic);
    int mess_size = sizeof(struct tcp_hdr) + (topic_len + 1);

    /* pregatim mesajul, alcatuim dupa protocol si adaugam delimitatorii */
    char* packed_mess = (char*) malloc(mess_size * sizeof(char));
    memset(packed_mess, 0, mess_size);

    struct tcp_hdr* mess = (struct tcp_hdr*) packed_mess;
    mess->op_type = UNSUBS_TYPE;
    mess->size = (uint16_t) mess_size;

    memcpy(packed_mess + sizeof(struct tcp_hdr), topic, topic_len);

    Task_client* t;
    t = prepare_message(packed_mess, mess_size);
    t->type = UNSUBS_TYPE;

    /* adaugam task-ul la coada de task-uri */
    queue_enq(task_q_client, t);

    /* setam socket-ul pentru evenimente de out */
    res = w_epoll_update_fd_inout(epollfd, socket_desc);
	DIE(res < 0, "w_epoll_update_fd_inout");

    free(packed_mess);
}

int handle_command() {

    char *command = NULL;
    size_t comm_len = 0;
    ssize_t comm_size = 0;
    const char delim[3] = " \n";
    int req_arg = 0;
    int ct_arg = 0;

    char topic[MAX_TOPIC_LEN];
    memset(topic, 0, MAX_TOPIC_LEN);

    uint8_t sf = 0;
    uint8_t op_type = 0;

    /* comanda va fi terminata cu '\n' */
    comm_size = getline(&command, &comm_len, stdin);
    if (comm_size < 0)
        print_debug("ERROR: Failed reading from STDIN.\n");

    if (strcmp(command, "exit\n") == 0) {
        close_client();
        free(command);
        return -1;
    } else {
        /* identificam comanda */
        char *tok;
        tok = strtok(command, delim);

        if (strcmp(tok, "subscribe") == 0) {
            req_arg = 3;
            op_type = SUBS_TYPE;
        } else if (strcmp(tok, "unsubscribe") == 0) {
            req_arg = 2;
            op_type = UNSUBS_TYPE;
        } else {
            print_debug("ERROR: Invalid command.\n");
            return 0;
        }

        while (tok != NULL) {
            ct_arg++;

            if (ct_arg == 2) {
                /* topic */
                strcpy(topic, tok);
            }

            if (ct_arg == 3) {
                /* store-and-forward */
                sf = (uint8_t) atoi(tok);
            }

            tok = strtok(NULL, delim);
        }

        if (ct_arg != req_arg) {
            print_debug("Invalid number of arguments!\n");
            free(command);
            return 0;
        }

        /* apelam functia specifica in functie de tip */
        switch(op_type) {
            case SUBS_TYPE:
                create_subs_task(topic, sf);
                break;
            case UNSUBS_TYPE:
                create_unsubs_task(topic);
            default:
                break;
        }
    }

    free(command);

    return 0;
}

int main(int argc, char** argv)
{
    int res;

    /* dezactivam bufferingul la afisare */
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    DIE(argc < 4, "Please provide minimum number of arguments!\n");

    /* verificam daca ne aflam in modul de debug */
    if (argc == 5) {
        if (strcmp(argv[4], "--debug") == 0)
            debug_enabled = 1;
    } else {
        debug_enabled = 0;
    }

    memset(server_message, 0, MAX_TCP_MESS);
    /* initializam coada de task-uri */
    task_q_client = queue_create();
    recv_DLE = 0;
    recv_seq_end = 0;

    /* preluam adresa IP a serverului data de client */
    memset(&ip, 0, sizeof(struct in_addr));
    res = inet_aton(argv[2], &ip);
    DIE(res == 0, "inet_aton");

    /* preluam portul serverului dat de client */
    server_port = (uint16_t) atoi(argv[3]);

    /* completam adresa serverului */
    fill_serv_addr();
    
    /* ne conectam la server */
    connect_to_server();

    /* folosim epoll pentru multiplexare */
	epollfd = w_epoll_create();
	DIE(epollfd < 0, "w_epoll_create");

    res = w_epoll_add_fd_in(epollfd, socket_desc);
	DIE(res < 0, "w_epoll_add_fd_inout");

    res = w_epoll_add_fd_in(epollfd, STDIN_FILENO);
	DIE(res < 0, "w_epoll_add_fd_in");

    /* copiem id-ul */
    int id_string_len = strlen(argv[1]);
    char* id_string = (char*) malloc((id_string_len + 1) * sizeof(char));

    memcpy(id_string, argv[1], id_string_len);
    id_string[id_string_len] = '\0';
    id_string_len++;

    /* trimitem id-ul clientului nostru pentru autentificare */
    create_id_pass_task(id_string, id_string_len);
    free(id_string);

    while (1) {
        struct epoll_event ev;

        /* asteptam eventimente */
		res = w_epoll_wait_infinite(epollfd, &ev);
		DIE(res < 0, "w_epoll_wait_infinite");

        if (ev.data.fd == socket_desc) {
            print_debug("New message.\n");
            if (ev.events & EPOLLIN) {
                print_debug("It's a message for the client.\n");
                res = handle_recv();
                if (res < 0) {
                    return 0;
                }
            }
            if (ev.events & EPOLLOUT) {
                print_debug("It's a message for the server.\n");
                res = handle_send();
                if (res < 0) {
                    return 0;
                }
            }
        } else if (ev.data.fd == STDIN_FILENO) {
            print_debug("It's a command from STDIN.\n");
            res = handle_command();
            if (res < 0) {
                return 0;
            }
        }
    }
        
    return 0;
}
