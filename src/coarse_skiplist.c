#include "../inc/coarse_skiplist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <omp.h>

coarse_list* coarse_skiplist_init(uint8_t levels, double prob, keyrange_t keyrange, long int random_seed) {
    coarse_list* skiplist = (coarse_list*)malloc(sizeof(coarse_list));
    if (!skiplist) return NULL;
    skiplist->levels = levels;
    skiplist->prob = prob;
    skiplist->keyrange.min = keyrange.min;
    skiplist->keyrange.max = keyrange.max;

    /* Initialize random state */
    skiplist->random_state = (struct drand48_data*)malloc(6);
    srand48_r(random_seed, skiplist->random_state);

    /* Create head node */
    skiplist->head = (node*)malloc(sizeof(node));
    if (!skiplist->head) return NULL;
    skiplist->head->next = (node**)malloc(sizeof(node*) * skiplist->levels);
    for (size_t i = 0; i < skiplist->levels; i++) {
        skiplist->head->next[i] = NULL;
    }
    skiplist->head->key = skiplist->keyrange.min;
    return skiplist;
}

void coarse_skiplist_destroy(coarse_list* list) {
    node* current = list->head;
    while (current) {
        node* next = current->next[0];
        if (current->next) free(current->next);
        free(current);
        current = next;
    }
    free(list->random_state);
    free(list);
}

/* Find the predecessors of 'key' for each level in 'list' and writes them to 'preds'
  If 'key' is contained in 'list', 'pred' will contain a pointer to the node with 
  key = 'key' for each level it was present in
  Returns true if key was found, false otherwise */
static bool find_predecessors(coarse_list* list, int key, node** preds) {
    node* current = list->head;
    for (int i = list->levels - 1; i >= 0; i--) {
        node* next = current->next[i];
        while (next && key > next->key) {
            current = next;
            next = current->next[i];
        }
        preds[i] = current;
    }
    return preds[0]->next[0] && preds[0]->next[0]->key == key;
}

node* coarse_skiplist_contains(coarse_list* list, int key) {
    node** preds = (node**)malloc(sizeof(node*) * list->levels);
    if (!preds) return NULL;

    node* result = NULL;
    if (find_predecessors(list, key, preds)) {
        result = preds[0]->next[0];
    }
    free(preds);
    return result;
}

bool coarse_skiplist_add(coarse_list* list, int key, void* data) {
    if (key < list->keyrange.min || key > list->keyrange.max) return false;

    node** preds = (node**)malloc(sizeof(node*) * list->levels);
    if (!preds) return false;

    if (find_predecessors(list, key, preds)) {
        free(preds);
        return false; /* Key already exists */
    }

    /* Create new node */
    node* new_node = (node*)malloc(sizeof(node));
    if (!new_node) {
        free(preds);
        return false;
    }
    new_node->next = (node**)malloc(sizeof(node*) * list->levels);
    if (!new_node->next) {
        free(new_node);
        free(preds);
        return false;
    }
    memset(new_node->next, 0, sizeof(node*) * list->levels);
    new_node->key = key;
    new_node->data = data;

    /* Link at level 0 */
    new_node->next[0] = preds[0]->next[0];
    preds[0]->next[0] = new_node;

    /* Probabilistic linking for higher levels */
    for (size_t i = 1; i < list->levels; i++) {
        double die;
        drand48_r(list->random_state, &die);
        if (die > list->prob) break;

        new_node->next[i] = preds[i]->next[i];
        preds[i]->next[i] = new_node;
    }

    free(preds);
    return true;
}

bool coarse_skiplist_remove(coarse_list* list, int key, void** data_out) {
    node** preds = (node**)malloc(sizeof(node*) * list->levels);
    if (!find_predecessors(list, key, preds)) {
        free(preds);
        return false; /* Key not found */
    }

    node* target = preds[0]->next[0];
    for (size_t i = 0; i < list->levels; i++) {
        if (preds[i]->next[i] == target) {
            preds[i]->next[i] = target->next[i];
        }
    }

    if (data_out) *data_out = target->data;
    free(target->next);
    free(target);
    free(preds);
    return true;
}