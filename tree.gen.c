/* Batch no. 13
2014A7PS130P: Eklavya Sharma
2014A7PS023P: Daivik Nema */
#include <stdio.h>
#include <stdlib.h>

TYPED(tree_node)* TYPED(tree_get_node)(TYPE value)
{
    TYPED(tree_node)* new_node = malloc(sizeof(TYPED(tree_node)));
    if(new_node == NULL){
        fprintf(stderr, "tree: No memory!\n");
    }else{
        new_node->value         = value;
        new_node->parent        = NULL;
        new_node->first_child   = NULL;
        new_node->next_sibling  = NULL;
        new_node->prev_sibling  = NULL;
        new_node->last_child    = NULL;
    }
    return new_node;
}

TYPED(tree_node)* TYPED(tree_insert)(TYPED(tree_node)* parent, TYPE value){
    TYPED(tree_node)* new_node = TYPED(tree_get_node)(value);
    new_node->parent = parent;
    if(parent->first_child == NULL && parent->last_child == NULL){
        parent->first_child = new_node;
    }else{
        new_node->prev_sibling = parent->last_child;
        parent->last_child->next_sibling = new_node;
    }
    parent->last_child = new_node;
    return new_node;
}

void TYPED(tree_destroy)(TYPED(tree_node)* root)
{
    if(root != NULL)
    {
        TYPED(tree_node)* sibling = root->next_sibling;
        TYPED(tree_node)* child = root->first_child;
        TYPED(destroy)(root->value);
        free(root);
        TYPED(tree_destroy)(child);
        TYPED(tree_destroy)(sibling);
    }
}
