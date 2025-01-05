#include "../inc/coarse_skiplist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <omp.h>

coarse_list* coarse_skiplist_init(uint8_t levels, double prob, keyrange_t keyrange) {
    coarse_list* skiplist = (coarse_list*)malloc(sizeof(coarse_list));
    if (!skiplist) return NULL;
    skiplist->levels = levels;
    skiplist->prob = prob;
    skiplist->keyrange.min = keyrange.min;
    skiplist->keyrange.max = keyrange.max;

    /* Create head node */
    skiplist->head = (coarse_node*)malloc(sizeof(coarse_node));
    if (!skiplist->head) return NULL;
    skiplist->head->next = (coarse_node**)malloc(sizeof(coarse_node*) * skiplist->levels);
    for (size_t i = 0; i < skiplist->levels; i++) {
        skiplist->head->next[i] = NULL;
    }
    skiplist->head->key = skiplist->keyrange.min;

    skiplist->lock = (omp_lock_t*)malloc(sizeof(omp_lock_t));
    if (!skiplist->lock) return NULL;
    omp_init_lock(skiplist->lock);

    return skiplist;
}

void coarse_skiplist_destroy(coarse_list* list) {
    coarse_node* current = list->head;
    while (current) {
        coarse_node* next = current->next[0];
        if (current->next) free(current->next);
        free(current);
        current = next;
    }
    omp_destroy_lock(list->lock);
    free(list->lock);
    free(list);
}

/* Find the predecessors of 'key' for each level in 'list' and writes them to 'preds'
  If 'key' is contained in 'list', 'pred' will contain a pointer to the node with 
  key = 'key' for each level it was present in
  Returns true if key was found, false otherwise */
static bool find_predecessors(coarse_list* list, int key, coarse_node** preds) {
    coarse_node* current = list->head;
    for (int i = list->levels - 1; i >= 0; i--) {
        coarse_node* next = current->next[i];
        while (next && key > next->key) {
            current = next;
            next = current->next[i];
        }
        preds[i] = current;
    }
    return preds[0]->next[0] && preds[0]->next[0]->key == key;
}

coarse_node* coarse_skiplist_contains(coarse_list* list, int key) {
    coarse_node** preds = (coarse_node**)malloc(sizeof(coarse_node*) * list->levels);
    if (!preds) return NULL;

    coarse_node* result = NULL;
    omp_set_lock(list->lock);
    if (find_predecessors(list, key, preds)) {
        result = preds[0]->next[0];
    }
    omp_unset_lock(list->lock);
    free(preds);
    return result;
}

bool coarse_skiplist_add(coarse_list* list, int key, void* data, unsigned short int random_state[3]) {
    if (key < list->keyrange.min || key > list->keyrange.max) return false;

    coarse_node** preds = (coarse_node**)malloc(sizeof(coarse_node*) * list->levels);
    if (!preds) return false;

    /* Create new node */
    coarse_node* new_node = (coarse_node*)malloc(sizeof(coarse_node));
    if (!new_node) {
        free(preds);
        return false;
    }
    new_node->next = (coarse_node**)malloc(sizeof(coarse_node*) * list->levels);
    if (!new_node->next) {
        free(new_node);
        free(preds);
        return false;
    }
    memset(new_node->next, 0, sizeof(coarse_node*) * list->levels);
    new_node->key = key;
    new_node->data = data;

    uint8_t linking_levels = 1;
    /* Cast die until it decides against more levels */
    for (size_t i = 1; i < list->levels; i++) {
        double die;
        drand48_r((struct drand48_data*)random_state, &die);
        if (die > list->prob) break;
        linking_levels++;
    }

    omp_set_lock(list->lock);
    if (find_predecessors(list, key, preds)) {
        omp_unset_lock(list->lock);
        free(new_node->next);
        free(new_node);
        free(preds);        
        return false; /* Key already exists */
    }
    
    /* Link up to pre-computed level */
    for (uint8_t i = 0; i < linking_levels; i++) {
        new_node->next[i] = preds[i]->next[i];
        preds[i]->next[i] = new_node;
    }
    omp_unset_lock(list->lock);

    free(preds);
    return true;
}

bool coarse_skiplist_remove(coarse_list* list, int key, void** data_out) {
    coarse_node* target;
    coarse_node** preds;

    preds = (coarse_node**)malloc(sizeof(coarse_node*) * list->levels);

    /* Critical Section */
    omp_set_lock(list->lock);
    if (!find_predecessors(list, key, preds)) {
        omp_unset_lock(list->lock);
        free(preds);
        return false; /* Key not found */
    }

    /* Unlink */
    target = preds[0]->next[0];
    for (size_t i = 0; i < list->levels; i++) {
        if (preds[i]->next[i] == target) {
            preds[i]->next[i] = target->next[i];
        }
    }
    omp_unset_lock(list->lock);
    
    if (data_out) *data_out = target->data;
    //free(target->next);
    //free(target);
    free(preds);
    return true;
}

/*
int main(int argc, char const *argv[])
{
    keyrange_t keyrange = {0, 10};
    coarse_list* list = coarse_skiplist_init(4, 0.5, keyrange);

    unsigned short int *random_state = (unsigned short int*)malloc(6);
    if (!random_state) return NULL;
    srand48_r(1430, (struct drand48_data *)random_state);

    coarse_skiplist_add(list, keyrange.min + 1, NULL, (struct drand48_data *)random_state);

    return 0;
}
*/