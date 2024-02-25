#ifndef _SLL_H_
#define _SLL_H_

#include <stdio.h>
#include <stdlib.h>
#include "serv_header.h"

typedef struct node{
    void* data;
    struct node *next;
}node;

typedef struct TList {
    node* head;
    int len;
}TList;

typedef struct topic {
    char name[MAX_TOPIC_LEN];
    TList* subs;
}Topic;

/*
aloca memorie pentru lista si initializeaza campurile
*/
void init_list(TList **list); 
/* 
adauga un nod pe pozitia n, intoarce 1 daca s-a realizat inserarea, -1 daca
a fost vreo eroare (n este negativ, n e mai mare decat dimensiunea listei) 
*/
int add (TList *list, int n, void* data);
/*
sterge nodul de pe pozitia n si il intoarce
*/
node* remove_node_client (TList *list, int key);
node* remove_node_topic (TList *list, int key);
/*
intoarce lungimea listei
*/
int length(TList *list);
/*
afiseaza o lista de numere intregi
*/
void print_int_list(TList *list);
/*
afiseaza o lista de stringuri
*/
void print_string_list(TList *list);
/*
sterge toate elementele din lista si elibereaza memoria pentru lista
*/
void free_list_client(TList **list);
void free_list_subs(TList **list);

#endif