#include "config.h"

#ifndef HAVE_TDESTROY

#include <search.h>
#include <stdlib.h>

typedef struct node
{
    char         *key;
    struct node  *llink, *rlink;
} node_t;

static void tdestroy_recurse(node_t* root, void (*free_action)(void *nodep))
{
    if (root->llink != NULL)
        tdestroy_recurse(root->llink, free_action);
    if (root->rlink != NULL)
        tdestroy_recurse(root->rlink, free_action);

    (*free_action) ((void *) root->key);
    free(root);
}


void tdestroy(void *vroot, void (*free_action)(void *nodep))
{
    node_t *root = (node_t *) vroot;

    if (root != NULL)
        tdestroy_recurse(root, free_action);
}

#endif
