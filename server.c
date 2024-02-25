#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>
#include <time.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>

#include "w_epoll.h"
#include "util.h"
#include "treap.h"
#include "list.h"

int sockfd_udp, sockfd_tcp;
uint16_t server_port;
struct sockaddr_in server_addr;
static int epollfd;

int debug_enabled;
Database *all_clients;
Treap *tree_clients = NULL;
Treap *tree_topics = NULL;
TList *all_clients_list;

void print_debug(char* string) {
    if(debug_enabled == 0)
        return;
    
    printf("(DEBUG) %s", string);
}

void add_topic(char* name) {
    /* cream un nou topic */
    Topic* new_t = (Topic*) malloc(sizeof(Topic));

    /* initializam atributele */
    memset(new_t->name, 0, MAX_TOPIC_LEN);
    strcpy(new_t->name, name);
    init_list(&new_t->subs);
    
    /* alocam cheia prin care vom identifica topicul */
    int topic_len = strlen(name);
    char* topic_copy = (char*) malloc((topic_len + 1) * sizeof(char));
    memcpy(topic_copy, name, topic_len);
    topic_copy[topic_len] = '\0';

    /* il adaugam la structura ce retine topicuri */
    insert(&tree_topics, topic_copy, new_t, rand() % 100);
}

void add_subs(char* topic, Client* c, uint8_t sf) {
    /* parametrul 'topic' este terminat cu \0 */

    Treap* aux_treap;
    aux_treap = search(tree_topics, topic);

    /* verificam daca exista abonati la topic */
    if (aux_treap == NULL) {
        add_topic(topic);
        print_debug("Added new topic to database.\n");
    } else {    
        print_debug("Topic already present in database.\n");
    }

    Treap* topics_node;
    topics_node = search(tree_topics, topic);

    if (topics_node == NULL) {
        print_debug("ERROR: Topic should be in database.\n");
        return;
    }

    /* adaugam abonamentul */
    Topic* crt_topic = ((Topic*) topics_node->data);

    Subs* new_sub = (Subs*) malloc(sizeof(Subs));
    new_sub->c = c;
    new_sub->sf = sf;

    add(crt_topic->subs, 0, new_sub);
}

int remove_subs(char* topic, Client* c) {
    /* parametrul 'topic' este terminat cu \0 */

    Treap* aux_treap;
    aux_treap = search(tree_topics, topic);

    if (aux_treap == NULL) {
        print_debug("Topic doesn't exist. Can't remove subscription.\n");
        return -1;
    }

    Topic* crt_topic = ((Topic*) aux_treap->data);
    node* removed_node;
    removed_node = remove_node_topic(crt_topic->subs, c->sockfd);

    if (removed_node == NULL) {
        print_debug("Client not subscribed to this topic. Can't remove any subscription.\n");
        return -1;
    }

    /* eliminam abonamentul */
    free((Subs*) removed_node->data);
    free(removed_node);

    /* daca nu mai exista abonati la topic, il stergem */
    if (length(crt_topic->subs) == 0)
        delete_key_topic(&tree_topics, topic);

    return 0;
}

void disconn_client(Client *c) {
    int res;

    c->state = STATE_DISCONNECTED;

    /* eliminam sockfd din epoll */
    res = w_epoll_remove_fd(epollfd, c->sockfd);
    DIE(res < 0, "w_epoll_remove_fd");

    /*
     * inchidem socketul si setam sockfd = -1 in structura clientului
     * pentru a se putea refolosi file descriptorul pentru alta conexiune
     */
    close(c->sockfd);
    c->sockfd = -1;

    printf("Client %s disconnected.\n", c->id);
}

/* functie care va fi folosita in cazul reconectarii unui client */
void reconn_client(Client* old_c, Client* same_c) {
    int res;

    old_c->ip = same_c->ip;
    old_c->port = same_c->port;
    old_c->sockfd = same_c->sockfd;
    old_c->state = STATE_CONNECTED;

    /* verificam daca avem ceva de trimis acestui client */
    if (!queue_empty(old_c->task_q)) {
        /* in caz afirmativ, activam EPOLLOUT */
        res = w_epoll_update_fd_inout(epollfd, old_c->sockfd);
		DIE(res < 0, "w_epoll_update_fd_inout");
    }
}

void free_client(Client* c) {
    /* eliberam resursele alocate clientului */

    /* il eliminam din baza de date */
    node* node_aux;
    node_aux = remove_node_client(all_clients_list, c->sockfd);

    /* verificam daca putem da free (preventiv) */
    if (node_aux != NULL)
        free(node_aux);

    /* eliberam coada de task-uri din structura asociata */
    free(c->task_q);
    /* eliberam structura */
    free(c);
}

void handle_id_pass(Client *c) {
    int res;
    struct tcp_hdr *hdr = (struct tcp_hdr*) c->recv_buf;

    if (hdr->size != c->recv_len) {
        print_debug("ID_PASS: Size of structure doesn't match with given size.\n");
        return;
    }

    uint16_t id_size = hdr->size - sizeof(struct tcp_hdr);

    /* copiem ID-ul */
    char* id = (char*) malloc(id_size * sizeof(char));
    memcpy(id, c->recv_buf + sizeof(struct tcp_hdr), id_size);

    Treap *aux_treap;
    aux_treap = search(tree_clients, id);

    /*
     * verificam daca ID-ul exista deja in treap
     * iar clientul este conectat
     */
    if (aux_treap != NULL &&
        ((Client*) aux_treap->data)->state == STATE_CONNECTED) {

        /* in caz afirmativ, noului client nu i se permite conectarea */
        printf("Client %s already connected.\n", id);

        res = w_epoll_remove_fd(epollfd, c->sockfd);
        DIE(res < 0, "w_epoll_add_fd_in");

        close(c->sockfd);

        /* eliberam resursele clientului */
        free_client(c);

        free(id);
        return;

    /* altfel, verificam daca e un client care se reconecteaza */
    } else if (aux_treap != NULL &&
               ((Client*) aux_treap->data)->state == STATE_DISCONNECTED) {
        
        /* copiem noile date ale clientului (ip, port, sockfd) */
        reconn_client(((Client*) aux_treap->data), c);

        struct in_addr ip;
        ip.s_addr = ((Client*) aux_treap->data)->ip;

        printf("New client %s connected from %s:%u.\n", ((Client*) aux_treap->data)->id,
                                                        inet_ntoa(ip),
                                                        ((Client*) aux_treap->data)->port);

        /*
         * eliberam resursele clientului "nou",
         * pentru ca era deja prezent in baza de date
         */
        free_client(c);

        free(id);
        return;
    }
    
    /* altfel, e un client nou */
    if (c->state == STATE_ID_NEEDED) {
        memset(c->id, 0, MAX_ID_SIZE);
        strcpy(c->id, id);
        c->state = STATE_CONNECTED;

        /* adaugam clientul la treap, structura e acum completa */
        char* key = (char*) malloc(sizeof(c->id));
        memcpy(key, c->id, sizeof(c->id));
        insert(&tree_clients, key, c, rand() % 100);

        struct in_addr ip;
        ip.s_addr = c->ip;

        printf("New client %s connected from %s:%u.\n", c->id,
                                                        inet_ntoa(ip),
                                                        c->port);
    }

    free(id);
}

void handle_subs(Client *c) {
    struct tcp_hdr *hdr = (struct tcp_hdr*) c->recv_buf;
    if (hdr->size != c->recv_len) {
        print_debug("SUBS: Size of structure doesn't match with given size.\n");
        return;
    }

    int topic_len = hdr->size - sizeof(struct tcp_hdr) - sizeof(uint8_t);
    int offset = sizeof(struct tcp_hdr) + sizeof(uint8_t);

    /* copiem topicul la care se aboneaza */
    char* topic = malloc(topic_len * sizeof(char));
    memset(topic, 0, topic_len);
    memcpy(topic, c->recv_buf + offset, topic_len);

    uint8_t sf = 0;
    memcpy(&sf, c->recv_buf + sizeof(struct tcp_hdr), sizeof(uint8_t));

    /*
     * pentru a preveni dubla subscriptie la
     * acelasi topic pentru acelasi client
     */
    remove_subs(topic, c);

    /* realizam abonarea */
    add_subs(topic, c, sf);

    free(topic);
}

void handle_unsubs(Client *c) {
    int res;
    struct tcp_hdr *hdr = (struct tcp_hdr*) c->recv_buf;
    if (hdr->size != c->recv_len) {
        print_debug("UNSUBS: Size of structure doesn't match with given size.\n");
        return;
    }

    int topic_len = hdr->size - sizeof(struct tcp_hdr);
    int offset = sizeof(struct tcp_hdr);

    /* copiem topicul de la care se dezaboneaza */
    char* topic = malloc(topic_len * sizeof(char));
    memset(topic, 0, topic_len);
    memcpy(topic, c->recv_buf + offset, topic_len);

    /* eliminam abonamentul */
    res = remove_subs(topic, c);
    if (res == 0) {
        //printf("S-a eliminat vechea abonare la acest topic.\n");
    } else {
        //printf("Nu s-a eliminat vreo abonare.\n");
    }

    free(topic);

    return;
}

void interpret_recv_message(Client *c) {

    if (c->recv_len < sizeof(struct tcp_hdr)) {
        print_debug("ERROR: Received message is too short. Can't interpret.");
        return;
    }

    /* apelam functia corespunzatoare tipului de mesaj */
    struct tcp_hdr *hdr = (struct tcp_hdr*) c->recv_buf;

    switch (hdr->op_type) {
        case ID_PASS_TYPE:
            handle_id_pass(c);
            break;
        case SUBS_TYPE:
            handle_subs(c);
            break;
        case UNSUBS_TYPE:
            handle_unsubs(c);
            break;
        default:
            break;
    }
}

void init_database() {
    init_list(&all_clients_list);
}

void add_client_list(int sockfd, in_addr_t ip, in_port_t port) {
    /* cream o structura noua asociata unui client */
    Client* c = (Client*) malloc(sizeof(Client));

    c->sockfd = sockfd;
    c->ip = ip;
    c->port = port;

    /* marcam faptul ca nu stim inca ID-ul clientului */
    c->state = STATE_ID_NEEDED;
    /* variabila pentru citirea de comenzi de la acest client */
    c->recv_DLE = 0;

    /*
     * variabila pentru a sti daca s-a terminat o secventa 
     * (pentru cazul in care se trimit bytes in plus intre
     * secvente delimitate)
     */
    c->recv_seq_end = 0;
    /* initializam coada de task-uri aferenta clientului */
    c->task_q = queue_create();

    /* initializam bufferul de intrare al clientului */
    memset(c->recv_buf, 0, BUF_LEN);
    c->recv_len = 0;
    
    /* adaugam clientul la lista de clienti a serverului */
    add(all_clients_list, 0, (void*) c);
}

Client* find_by_sockfd(int sockfd) {
    /*
     * cautam in baza de date structura clientului
     * cu sockfd dat
     */
    node* node_aux = all_clients_list->head;
    while(node_aux != NULL) {
        if (((Client*) node_aux->data)->sockfd == sockfd) {
            return ((Client*) node_aux->data);
        }
        
        node_aux = node_aux->next;
    }

    return NULL;
}

void handle_tcp_recv(Client* c) {
    int i;
    char buf[MAXBUFSIZE];
    int buf_len;

    buf_len = recv(c->sockfd, buf, MAXBUFSIZE, 0);

    if (buf_len < 0) {
        print_debug("ERROR: Error in communication. Close client.\n");

        /*
         * eliminam structura clientului doar daca era in
         * state STATE_ID_NEEDED (client inca necunoscut)
         */
        if (c->state == STATE_ID_NEEDED)
            free_client(c);
        else
            /* altfel, retinem datele clientului in continuare */
            disconn_client(c);

        return;
    }
    if (buf_len == 0) {
        print_debug("Connection closed from client.\n");
        
        /* analog ca mai sus */
        if (c->state == STATE_ID_NEEDED) {
            free_client(c);
        } else {
            /* altfel, retinem datele clientului in continuare */
            disconn_client(c);
        }

        return;
    }

    char charConv[2];
    charConv[1] = '\0';
    char byte;

    i = 0;
    while (i < buf_len) {
        
        if (c->recv_DLE == 1) {
            /* luam ultimul byte din bufferul primit anterior la recv */
            byte = DLE;
            /* pentru a incepe cu primul byte din bufferul primit curent */
            i = -1;
        } else {
            byte = buf[i];
        }
        
        if (byte == DLE) {
            /*
             * am citit un caracter de escape, mai citim inca unul,
             * daca exista, pentru a sti cum sa il interpretam
             */

            i++;
            /*
             * daca am ajuns la finalul bufferului primit si ultimul
             * caracter intalnit e DLE - retinem acest lucru
             */
            if (i == buf_len) {
                c->recv_DLE = 1;
                break;
            }

            byte = buf[i];
            c->recv_DLE = 0;

            if (byte == STX) {
                /*
                 * se marcheaza inceputul unui mesaj,
                 * reinitializam bufferul
                 */
                memset(c->recv_buf, 0, BUF_LEN);
                c->recv_len = 0;
                c->recv_seq_end = 0;
            } else if (byte == ETX) {
                /*
                 * se marcheaza finalul unui mesaj,
                 * trimitem bufferul la interpretare
                 */
                interpret_recv_message(c);
                c->recv_seq_end = 1;
            } else if (byte == DLE) {
                
                /*
                 * daca ne aflam in interiorul unui mesaj
                 * si am primit de doua ori caracterul DLE,
                 * retinem un singur caracter in bufferul destinatie
                 */
                if (c->recv_seq_end == 0) {
                    charConv[0] = byte;
                    memcpy(c->recv_buf + c->recv_len, charConv, 1);
                    c->recv_len = c->recv_len + 1;
                }
            } else {
                print_debug("ERROR: Something was wrong with message delimiters.\n");
            }
        } else {
            /*
             * ne aflam in interiorul mesajului si am primit un caracter normal;
             * il adaugam la bufferul destinatie
             */
            if (c->recv_seq_end == 0) {
                charConv[0] = byte;
                memcpy(c->recv_buf + c->recv_len, charConv, 1);
                c->recv_len = c->recv_len + 1;
            }
        }

        i++;
    }
}

void close_clients_sock() {
    int res;
    node* node_aux = all_clients_list->head;

    /* parcurgem lista de clienti */
    while(node_aux != NULL) {
        /* inchidem socketii clientilor conectati */
        if (((Client*) node_aux->data)->state != STATE_DISCONNECTED) {
            res = close(((Client*) node_aux->data)->sockfd);
            DIE(res < 0, "close");
        }

        node_aux = node_aux->next;
    }
}

void close_sockets() {
    int res;

    /* inchidem socketii TCP si UDP ai serverului */
    res = close(sockfd_udp);
    DIE(res < 0, "close");
    res = close(sockfd_tcp);
    DIE(res < 0, "close");

    /* inchidem socketii clientilor TCP */
    close_clients_sock();

    /* inchidem epoll */
    res = close(epollfd);
    DIE(res < 0, "close");
}

void fill_serv_addr() {
    memset(&server_addr, 0, sizeof(server_addr));

    /* completam cu informatia serverului */
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(server_port);
}

void create_udp_sock() {
    int res;

    /* creare socket UDP */
    sockfd_udp = socket(AF_INET, SOCK_DGRAM, 0);
    DIE(sockfd_udp < 0, "socket");

    /* legare socket la adresa */
    res = bind(sockfd_udp, (const struct sockaddr *) &server_addr, sizeof(server_addr));
    DIE(res < 0, "bind");
}

void create_tcp_sock() {
    int res;

    /* creare socket TCP */
    sockfd_tcp = socket(AF_INET, SOCK_STREAM, 0);
    DIE(sockfd_tcp < 0, "socket");

    /* setare optiuni socket */
    int optval = 1;
    res = setsockopt(sockfd_tcp, SOL_SOCKET, TCP_NODELAY, &optval, sizeof(optval));
    DIE(res < 0, "setsockopt");

    res = setsockopt(sockfd_tcp, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    DIE(res < 0, "setsockopt");

    /* legare socket la adresa */
    res = bind(sockfd_tcp, (const struct sockaddr *) &server_addr, sizeof(server_addr));
    DIE(res < 0, "bind");
    
    /* ascutare pentru conexiuni */
    res = listen(sockfd_tcp, BACKLOG);
    DIE(res < 0, "listen");
}

Datagram* prepare_datagram(char* buf, int buf_len) {
    Datagram* new_d = (Datagram*) malloc(sizeof(Datagram));
    int ct = 0;

    memset(new_d->buf, 0, MAX_TCP_MESS);

    new_d->buf[ct] = DLE;
    ct++;
    new_d->buf[ct] = STX;
    ct++;

    for (int i = 0; i < buf_len; i++) {
        /* escape la escape */
        if (buf[i] == DLE) {
            new_d->buf[ct] = DLE;
            ct++;
        }
            
        new_d->buf[ct] = buf[i];
        ct++;
    }

    new_d->buf[ct] = DLE;
    ct++;
    new_d->buf[ct] = ETX;
    ct++;

    /* setam lungimea mesajului delimitat */
    new_d->len = ct;
    /* se va seta dupa numararea clientilor */
    new_d->ref_count = 0;

    return new_d;
}

void pack_datagram(char* res, int topic_len, int content_len, char* buf, struct sockaddr_in addr) {
    struct udp_message* mess = (struct udp_message*) buf;
    int pos = 0;

    /* construim mesajul dupa protocolul descris in README */

    /* network order */
    uint32_t ip = (uint32_t) addr.sin_addr.s_addr;
    memcpy(res + pos, &ip, sizeof(uint32_t));
    pos = pos + sizeof(uint32_t);

    uint16_t port = (uint16_t) addr.sin_port;
    memcpy(res + pos, &port, sizeof(uint16_t));
    pos = pos + sizeof(uint16_t);

    memcpy(res + pos, &(mess->data_type), sizeof(mess->data_type));
    pos = pos + sizeof(mess->data_type);

    uint16_t content_len_16 = (uint16_t) content_len;
    memcpy(res + pos, &content_len_16, sizeof(uint16_t));
    pos = pos + sizeof(uint16_t);

    memcpy(res + pos, mess->content, content_len);
    pos = pos + content_len;

    uint16_t topic_len_16 = (uint16_t) topic_len;
    memcpy(res + pos, &topic_len_16, sizeof(uint16_t));
    pos = pos + sizeof(uint16_t);

    memcpy(res + pos, mess->topic, topic_len);
    pos = pos + topic_len;
}

int create_tasks(Datagram* new_d, char* topic) {
    int res;
    int task_count = 0;

    Treap *aux_treap;
    aux_treap = search(tree_topics, topic);
    if (aux_treap == NULL) {
        print_debug("ERROR: Can't create tasks. Topic not present.\n");
        return 0;
    }

    Client* crt_client;
    uint8_t crt_sf;

    /* lista de abonati la topicul 'topic' */
    TList* subs_list = ((Topic*) aux_treap->data)->subs;
    node* node_aux = subs_list->head;

    /* parcurgem lista de abonati */
    while(node_aux != NULL) {
        /* clientul curent si atributul store-and-forward */
        crt_client = ((Subs*) node_aux->data)->c;
        crt_sf = ((Subs*) node_aux->data)->sf;

        /* daca e conectat, ii trimitem mesajul indiferent de valoarea lui SF */
        if (crt_client->state == STATE_CONNECTED) {
            /* cream structura noului task */
            Task* new_t = (Task*) malloc(sizeof(Task));
            new_t->d = new_d;
            new_t->bytes_sent = 0;

            /* o adaugam la coada clientului */
            queue_enq(crt_client->task_q, new_t);
            task_count++;

            /* adaugam EPOLLOUT pentru trimitere */
            res = w_epoll_update_fd_inout(epollfd, crt_client->sockfd);
			DIE(res < 0, "w_epoll_update_fd_inout");

        /* daca e deconectat, cream task doar pentru SF = 1 */
        } else if (crt_client->state == STATE_DISCONNECTED && (crt_sf == 1)) {
            Task* new_t = (Task*) malloc(sizeof(Task));
            new_t->d = new_d;
            new_t->bytes_sent = 0;

            queue_enq(crt_client->task_q, new_t);
            task_count++;

            /* vom adauga EPOLLOUT pentru trimitere la reconectare */
        }

        node_aux = node_aux->next;
    }

    /* intoarcem numarul de task-uri create */
    return task_count;
}

void handle_udp_mess() {
    struct sockaddr_in udp_client_addr;
    socklen_t udp_addr_len = sizeof(udp_client_addr);
    char buf[MAXBUFSIZE];
    int buf_len;

    memset(&udp_client_addr, 0, sizeof(udp_client_addr));

    /* citim mesajul de la clientul UDP */
    buf_len = recvfrom(sockfd_udp, 
                       (char *) buf, 
                       MAXBUFSIZE, 
                       MSG_WAITALL, 
                       (struct sockaddr *) &udp_client_addr, 
                       &udp_addr_len);
    DIE(buf_len < 0, "recvfrom");

    buf[buf_len] = '\0';

    /* facem o copie a topicului */
    struct udp_message* mess = (struct udp_message*) buf;
    int topic_len = strlen(mess->topic);

    char* topic = malloc((topic_len + 1) * sizeof(char));
    DIE(topic == NULL, "malloc");

    memcpy(topic, mess->topic, topic_len);
    topic[topic_len] = '\0';

    /* verificam daca exista abonati la acest topic */
    Treap *aux_treap;
    aux_treap = search(tree_topics, topic);
    if (aux_treap == NULL) {
        print_debug("No subscribers for this topic. Throw away message.\n");
        free(topic);
        memset(buf, 0, MAXBUFSIZE);
        return;
    }

    /*
     * pregatim mesajul astfel incat sa ocupe strict spatiul necesar,
     * pentru a fi transmis in aceasta forma catre clientii abonati
     */

    /* calculam lungimea continutului */
    int content_len = buf_len - sizeof(mess->topic) - sizeof(mess->data_type);
    /* calculam lungimea totala a mesajului restrans */
    int packed_len = sizeof(uint32_t) +
                     sizeof(uint16_t) +
                     sizeof(mess->data_type) +
                     sizeof(uint16_t) +
                     content_len +
                     sizeof(uint16_t) +
                     topic_len;
    
    /* construim mesajul restrans */
    char* packed_datagram = (char*) malloc(packed_len * sizeof(char));
    pack_datagram(packed_datagram, topic_len, content_len, buf, udp_client_addr);

    /* pregatim mesajul pentru transmisie */
    Datagram *new_d;
    new_d = prepare_datagram(packed_datagram, packed_len);

    free(packed_datagram);

    /*
     * cream task-uri pentru fiecare client
     * caruia trebuie sa ii trimitem acest mesaj
     */
    new_d->ref_count = create_tasks(new_d, topic);

    /*
     * daca nu avem de transmis niciunui client
     * mesajul, eliberam structura alocata acestuia
     */
    if (new_d->ref_count == 0) {
        free(new_d);
    }

    free(topic);
    memset(buf, 0, MAXBUFSIZE);
}

void handle_new_client() {
    int res;
    int sockfd_client;
    struct sockaddr_in tcp_client_addr;
    socklen_t tcp_addr_len = sizeof(tcp_client_addr);

    /* stabilim conexiunea */
    sockfd_client = accept(sockfd_tcp, (struct sockaddr*) &tcp_client_addr, &tcp_addr_len);
    DIE(sockfd_client < 0, "accept");

    /* setare optiuni socket */
    int optval = 1;
    res = setsockopt(sockfd_client, SOL_SOCKET, TCP_NODELAY, &optval, sizeof(optval));
    DIE(res < 0, "setsockopt");

    /* adaugam clientul la baza de date (momentan fara un ID asociat) */
    add_client_list(sockfd_client, (in_addr_t) tcp_client_addr.sin_addr.s_addr, tcp_client_addr.sin_port);

    /* adaugam socketul clientului la epoll pentru operatii de input */
    res = w_epoll_add_fd_in(epollfd, sockfd_client);
	DIE(res < 0, "w_epoll_add_fd_in");
}

int handle_new_command() {
    char *command = NULL;
    size_t comm_len = 0;
    ssize_t comm_size = 0;

    /* comanda va fi terminata cu '\n' */
    comm_size = getline(&command, &comm_len, stdin);
    if (comm_size < 0)
        print_debug("ERROR: Failed reading from STDIN.\n");

    if (strcmp(command, "exit\n") == 0) {
        free(command);
        return 1;
    }
    else
        print_debug("ERROR: The only command accepted is 'exit'.\n");

    free(command);

    return 0;
}

void handle_tcp_send(Client *c) {
    int res;

    /* verificam daca coada de task-uri asociata clientului este vida */
    if (queue_empty(c->task_q)) {
        /* am incheiat trimiterea datelor */
        print_debug("Client's task queue is empty. Remove EPOLLOUT.\n");
        res = w_epoll_update_fd_in(epollfd, c->sockfd);
		DIE(res < 0, "w_epoll_update_fd_in");

        return;
    }

    Task* crt_task = queue_deq(c->task_q);
    int num_bytes;
    int offset = crt_task->bytes_sent;
    int bytes_left = crt_task->d->len - crt_task->bytes_sent;

    /* incercam sa trimitem restul de bytes netrimisi */
    num_bytes = send(c->sockfd, (crt_task->d->buf + offset), bytes_left, 0);

    if (num_bytes < 0) {
        print_debug("ERROR: Error in communication.\n");

        /* reintroducem in coada task-ul, deoarece nu l-am finalizat */
        queue_enq(c->task_q, crt_task);

        /* deconectam clientul */
        disconn_client(c);

        return;
    }
    if (num_bytes == 0) {
        print_debug("Connection closed from client.\n");
        queue_enq(c->task_q, crt_task);
        disconn_client(c);

        return;
    }
    if (num_bytes < bytes_left) {

        /* actualizam numarul de bytes trimisi */
        crt_task->bytes_sent = crt_task->bytes_sent + num_bytes;

        /* nu am terminat task-ul, il reintroducem in coada */
        queue_enq(c->task_q, crt_task);
    }

    /* am finalizat task-ul */
    crt_task->bytes_sent = crt_task->d->len;
    /* actualizam numarul de referinte la structura Datagram */
    crt_task->d->ref_count--;

    /* verificam daca structura Datagram poate fi eliberata */
    if (crt_task->d->ref_count == 0) {
        free(crt_task->d);
    }

    free(crt_task);
}

void free_server_data() {
    /* dealoca resursele serverului, inainte de inchidere */
    close_sockets();

    /* eliberam structurile clientilor din baza de date */
    node* node_aux = all_clients_list->head;
    while(node_aux != NULL) {
        /* eliberam coada de task-uri din structura clientului */
        if (((Client*) node_aux->data)->task_q != NULL)
            free(((Client*) node_aux->data)->task_q);
        /* eliberam structura */
        if (((Client*) node_aux->data) != NULL)
            free(((Client*) node_aux->data));

        node_aux = node_aux->next;
    }

    /* eliberam baza de date */
    if (all_clients_list != NULL)
        free_list_client(&all_clients_list);

    /* eliberam treap-urile */
    if (tree_clients != NULL)
        free_treap_client(&tree_clients);

    if (tree_topics != NULL)
        free_treap_topic(&tree_topics);
}

int main(int argc, char** argv) {

    /* dezactivam bufferingul la afisare */
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    /* verificam daca am primit un numar valid de argumente */
    DIE(argc < 2, "Please provide minimum number of arguments!\n");

    /* verificam daca ne aflam in modul de debug */
    if (argc == 3) {
        if (strcmp(argv[2], "--debug") == 0)
            debug_enabled = 1;
    } else {
        debug_enabled = 0;
    }
    
    int res;
    /* folosim epoll pentru multiplexare */
	epollfd = w_epoll_create();
	DIE(epollfd < 0, "w_epoll_create");

    server_port = (uint16_t) atoi(argv[1]);
    /* setam adresa serverului */
    fill_serv_addr();
    /* cream socketul udp */
    create_udp_sock();
    /* cream socketul tcp */
    create_tcp_sock();

    /* adaugam file descriptorii stdin si cei ai socketilor udp, tcp la epoll */
    res = w_epoll_add_fd_in(epollfd, sockfd_udp);
	DIE(res < 0, "w_epoll_add_fd_in");

    res = w_epoll_add_fd_in(epollfd, sockfd_tcp);
	DIE(res < 0, "w_epoll_add_fd_in");

    res = w_epoll_add_fd_in(epollfd, STDIN_FILENO);
	DIE(res < 0, "w_epoll_add_fd_in");

    print_debug("Waiting for connections...\n");

    /* initializam vectorul de clienti */
    init_database();
    Client* current_client = NULL;

    while (1) {
        struct epoll_event ev;

        /* asteptam eventimente */
		res = w_epoll_wait_infinite(epollfd, &ev);
		DIE(res < 0, "w_epoll_wait_infinite");

        if (ev.data.fd == sockfd_udp) {
            print_debug("New UDP message.\n");
            if (ev.events & EPOLLIN)
                /* mesaj de la clientul UDP */
                handle_udp_mess();
        } else if (ev.data.fd == sockfd_tcp) {
            print_debug("New TCP connection.\n");
            if (ev.events & EPOLLIN)
                /* client TCP posibil nou */
                handle_new_client();
        } else if (ev.data.fd == STDIN_FILENO) {
            print_debug("New message from STDIN.\n");
            if (ev.events & EPOLLIN) {
                /* comanda de la tastatura */
                res = handle_new_command();
                if (res == 1) {
                    /* daca s-a dat comanda 'exit' */
                    free_server_data();
                    print_debug("Server closed.\n");
                    return 0;
                }
            }
        } else {
            /* mesaj de la un client deja conectat */

            /* cautam clientul in lista de clienti */
            current_client = find_by_sockfd(ev.data.fd);

            if(current_client != NULL) {
                print_debug("New TCP communication.\n");

                /* verificam tipul de operatie pe socket */
                if (ev.events & EPOLLIN) {
                    print_debug("It's a TCP message from client.\n");
                    handle_tcp_recv(current_client);
                }

                if (ev.events & EPOLLOUT) {
                    print_debug("It's a TCP message for the client.\n");
                    handle_tcp_send(current_client);
                }
            } else {
                print_debug("ERROR: Active socket not present in database!\n");
            }
        }
    }

    return 0;
}
