#include "sll.h"

void init_list(TList **list)
{
    (*list) = (TList *) malloc(sizeof(TList));
    (*list)->len = 0;
    (*list)->head = NULL;
}

int add(TList *list, int n, void* data)
{
    int i;

    if(n < 0 || n > list->len)
        return -1;
    else
    {
        node *nod_nou, *nod_aux;
        nod_nou = (node *) malloc(sizeof(node));

        if(n == 0)
        {
            nod_nou->next = list->head;
            list->head = nod_nou;
            nod_nou->data = data;
        }
        else
        {
            nod_aux = list->head;
            for(i = 1; i < n; i++)
                nod_aux = nod_aux->next;
            
            nod_nou->next = nod_aux->next;
            nod_aux->next = nod_nou;
            nod_nou->data = data;
        }

        list->len++;

        return 1;
    }
}

node* remove_node_client (TList *list, int key)
{
    node *nod_aux = list->head;
    node *prev;

    if (nod_aux != NULL && ((Client*) nod_aux->data)->sockfd == key) {
        list->head = nod_aux->next;
        list->len--;
        return nod_aux;
    }

    while (nod_aux != NULL && ((Client*) nod_aux->data)->sockfd != key) {
        prev = nod_aux;
        nod_aux = nod_aux->next;
    }

    if (nod_aux == NULL)
        return NULL;
    
    prev->next = nod_aux->next;

    list->len--;
    return nod_aux;
}

node* remove_node_topic (TList *list, int key)
{
    node *nod_aux = list->head;
    node *prev;

    if (nod_aux != NULL && ((Subs*) nod_aux->data)->c->sockfd == key) {
        list->head = nod_aux->next;
        list->len--;
        return nod_aux;
    }

    while (nod_aux != NULL && ((Subs*) nod_aux->data)->c->sockfd != key) {
        prev = nod_aux;
        nod_aux = nod_aux->next;
    }

    if (nod_aux == NULL)
        return NULL;
    
    prev->next = nod_aux->next;

    list->len--;
    return nod_aux;
}

int length(TList *list)
{
    return list->len;
}

void print_int_list(TList *list)
{
    int i;
    node *nod_aux;

    nod_aux = list->head;
    for(i = 0; i < list->len; i++)
    {
        printf("%d ", *((int *) nod_aux->data));
        nod_aux = nod_aux->next;
    }
    printf("\n");
}

void print_string_list(TList *list)
{
    int i;
    node *nod_aux;

    nod_aux = list->head;
    for(i = 0; i < list->len; i++)
    {
        printf("%s ", ((char *) nod_aux->data));
        nod_aux = nod_aux->next;
    }
    printf("\n");
}

void free_list_subs(TList **list)
{
    int i;
    node *aux, *aux_next;

    aux = (*list)->head;
    for(i = 0; i < length(*list); i++)
    {
        aux_next = aux->next;
        free((Subs*) aux->data);
        free(aux);
        aux = aux_next;
    }
    free(*list);
}

void free_list_client(TList **list)
{
    int i;
    node *aux, *aux_next;

    aux = (*list)->head;
    for(i = 0; i < length(*list); i++)
    {
        aux_next = aux->next;
        free(aux);
        aux = aux_next;
    }
    free(*list);
}
