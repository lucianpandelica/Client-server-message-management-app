#include <stdio.h>
#include <string.h>
#include "treap.h"

void in_order(Treap *tree)
{
    if(tree == NULL)
        return;
    
    in_order(tree->left);
    printf("%s (%d)\n", tree->key, tree->priority);
    in_order(tree->right);
}

void init(Treap **tree)
{
    (*tree) = (Treap *) malloc(sizeof(Treap));
    (*tree)->left = NULL;
    (*tree)->right = NULL;
}

Treap* rotateRight (Treap *tree)
{
    Treap *nod_st = tree->left, *nod_st_dr = nod_st->right;

    nod_st->right = tree;
    tree->left = nod_st_dr;

    return nod_st;
}

Treap* rotateLeft (Treap *tree)
{
    Treap *nod_dr = tree->right, *nod_dr_st = nod_dr->left;

    nod_dr->left = tree;
    tree->right = nod_dr_st;

    return nod_dr;
}

Treap* insert(Treap **tree, char* key, void* data, int priority)
{
    if ((*tree) == NULL)
    {
        init(tree);

        /* pointer la ID - va fi alocat inainte */
        (*tree)->key = key;

        /* pointer la structura de date retinuta - va fi alocata inainte */
        (*tree)->data = data;

        (*tree)->priority = priority;

        return (*tree); 
    }

    if(strcmp(key, (*tree)->key) < 0)
    {
        (*tree)->left = insert(&((*tree)->left), key, data, priority);

        if((*tree)->left->priority > (*tree)->priority)
            (*tree) = rotateRight((*tree));
    }
    else
    {
        (*tree)->right = insert(&((*tree)->right), key, data, priority);

        if((*tree)->right->priority > (*tree)->priority)
            (*tree) = rotateLeft((*tree));
    }

    return (*tree);
}

Treap* search(Treap *tree, char* key) {
    if (tree == NULL || strcmp(tree->key, key) == 0)
        return tree;
    
    if (strcmp(tree->key, key) < 0)
        return search(tree->right, key);
    
    return search(tree->left, key);
}

Treap* delete_key_topic(Treap **tree, char* key)
{
    // daca nodul nu exista
    if((*tree) == NULL)
        return (*tree);
    
    if(strcmp(key, (*tree)->key) < 0)
        (*tree)->left = delete_key_topic(&((*tree)->left), key);
    else if(strcmp(key, (*tree)->key) > 0)
        (*tree)->right = delete_key_topic(&((*tree)->right), key);
    // am gasit nodul de sters
    else
    {
        if((*tree)->left == NULL)
        {
            Treap *temp = (*tree)->right;

            free_list_subs(&(((Topic*)(*tree)->data)->subs));
            free((Topic*) (*tree)->data);
            free((*tree)->key);
            free((*tree));

            (*tree) = temp;
        }
        else if((*tree)->right == NULL)
        {
            Treap *temp = (*tree)->left;

            free_list_subs(&(((Topic*)(*tree)->data)->subs));
            free((Topic*) (*tree)->data);
            free((*tree)->key);
            free((*tree));

            (*tree) = temp;
        }
        else if((*tree)->left->priority < (*tree)->right->priority)
        {
            (*tree) = rotateLeft((*tree));
            (*tree)->left = delete_key_topic(&((*tree)->left), key);
        }
        else
        {
            (*tree) = rotateRight((*tree));
            (*tree)->right = delete_key_topic(&((*tree)->right), key);
        }
    }

    return (*tree);
}

void free_treap_topic(Treap **tree)
{
    if((*tree)->left != NULL)
    {
        free_treap_topic(&((*tree)->left));
        (*tree)->left = NULL;
    }
    
    if((*tree)->right != NULL)
    {
        free_treap_topic(&((*tree)->right));
        (*tree)->right = NULL;
    }
        
    if((*tree)->left == NULL && (*tree)->right == NULL) {
        free_list_subs(&(((Topic*)(*tree)->data)->subs));
        free((Topic*) (*tree)->data);
        free((*tree)->key);
        free((*tree));
    }  
}

void free_treap_client(Treap **tree)
{
    if((*tree)->left != NULL)
    {
        free_treap_client(&((*tree)->left));
        (*tree)->left = NULL;
    }
    
    if((*tree)->right != NULL)
    {
        free_treap_client(&((*tree)->right));
        (*tree)->right = NULL;
    }
        
    if((*tree)->left == NULL && (*tree)->right == NULL) {
        free((*tree)->key);
        // am sters deja clientii la momentul apelarii acestei functii
        free((*tree));
    }  
}
