#ifndef _TREAP_H_
#define _TREAP_H_

#include <stdlib.h>
#include "sll.h"

typedef struct Treap {
    char* key;
    int priority;
    void* data;
    struct Treap *left, *right;
} Treap;

void init(Treap **tree);
Treap* rotateRight (Treap *tree);
Treap* rotateLeft (Treap *tree);
Treap* insert(Treap **tree, char* key, void* data, int priority);
Treap* search(Treap *tree, char* key);
Treap* delete_key_topic(Treap **tree, char* key);
void free_treap_topic(Treap **tree);
void free_treap_client(Treap **tree);
void in_order(Treap *tree);

#endif