#include "../inc/fine_skiplist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <omp.h>

fine_node* create_node(fine_list* list, int key) {
    /* allocate memory */
    fine_node* node = (fine_node*)malloc(sizeof(fine_node));
    if(!node) return NULL;
    node->lock = (omp_nest_lock_t*)malloc(sizeof(omp_nest_lock_t));
    if(!node->lock) {
        free(node);
        return NULL;
    }
    node->next = (fine_node**)malloc(sizeof(fine_node*) * list->levels);
    if (!node->next) {
        free(node->lock);
        free(node);
        return NULL;
    }
    memset(node->next, 0, sizeof(fine_node*) * list->levels);

    /* init fields */
    node->fully_linked = false;
    node->marked = false;
    omp_init_nest_lock(node->lock);
    node->k = 0;
    node->key = key;
    return node;
}

void destroy_node(fine_node* node) {
    free(node->next);
    omp_destroy_nest_lock(node->lock);
    free(node->lock);
    free(node);
}

fine_list* fine_skiplist_init(uint8_t levels, double prob, keyrange_t keyrange) {
    fine_list* skiplist = (fine_list*)malloc(sizeof(fine_list));
    if (!skiplist) return NULL;
    skiplist->levels = levels;
    skiplist->prob = prob;
    skiplist->keyrange.min = keyrange.min;
    skiplist->keyrange.max = keyrange.max;

    /* Create head node */
    skiplist->head = create_node(skiplist, keyrange.min - 1);
    fine_node* tail = create_node(skiplist, keyrange.max + 1);
    if(!skiplist->head||!tail) {
        free(skiplist);
        return NULL;
    }
    for (size_t i = 0; i < skiplist->levels; i++) {
        skiplist->head->next[i] = tail;
    }

    return skiplist;
}

void fine_skiplist_destroy(fine_list* list) {
    fine_node* current = list->head;
    while (current) {
        fine_node* next = current->next[0];
        destroy_node(current);
        current = next;
    }
    free(list);
}

/* Find the predecessors and successors of 'key' for each level in 'list' and writes them to 'preds' and 'succs'.
  If 'key' is contained in 'list', 'preds' will contain a pointer to the node with 
  key = 'key' for each level it was present in, similarly for 'succs'.
  Returns highest level the node was linked in, -1 if it was not found */
static int find_neighbours(fine_list* list, int key, fine_node** preds, fine_node** succs) {
    fine_node* current = list->head;
    int l = -1;
    for (int i = list->levels - 1; i >= 0; i--) {
        fine_node* next = current->next[i];
        while (next && key > next->key) {
            current = next;
            next = current->next[i];
        }
        preds[i] = current;
        succs[i] = next;

        // check if we found the element if it was not already found in higher level
        if (next && l < 0 && next->key == key) l = i;
    }
    return l;
}

fine_node* fine_skiplist_contains(fine_list* list, int key) {
    fine_node** preds = (fine_node**)malloc(sizeof(fine_node*) * list->levels);
    if (!preds) return NULL;
    fine_node** succs = (fine_node**)malloc(sizeof(fine_node*) * list->levels);
    if (!succs) { free(preds); return NULL;}

    fine_node* result = NULL;
    if (find_neighbours(list, key, preds, succs) >= 0) {
        result = preds[0]->next[0];
    }
    free(preds);
    free(succs);
    return result;
}

bool fine_skiplist_add(fine_list* list, int key, void* data, unsigned short int random_state[3]) {
    if (key < list->keyrange.min || key > list->keyrange.max) return false;

    fine_node** preds = (fine_node**)malloc(sizeof(fine_node*) * list->levels);
    if (!preds) return NULL;
    fine_node** succs = (fine_node**)malloc(sizeof(fine_node*) * list->levels);
    if (!succs) { free(preds); return false;}

    int highest_link;
    /* Cast die until it decides against more levels */
    for (highest_link = 0; highest_link < list->levels - 1; highest_link++) {
        double die;
        drand48_r((struct drand48_data*)random_state, &die);
        if (die > list->prob) break;
    }

    while(true) {
        int f = find_neighbours(list, key, preds, succs);
        if (f >= 0)
        {
            /* key already exists */
            fine_node* found = succs[f];
            if(!found->marked) {
                while(!found->fully_linked);
                free(preds);
                free(succs);
                return false;
            }
            /* key marked for deletion */
            continue;
        }

        int highlock = -1;
        fine_node *pred, *succ;
        bool valid = true;

        for (int l = 0; valid && (l <= highest_link); l++)
        {
            pred = preds[l];
            succ = succs[l];
            omp_set_nest_lock(pred->lock);
            highlock = l;
            valid = !pred->marked&&!succ->marked&&(pred->next[l] == succ);
        }
        /* failed, retry */
        if (!valid) {
            for (int l = 0; l <= highlock; l++)
            {
                omp_unset_nest_lock(preds[l]->lock);
            }  
            continue;
        }
        /* Create new node */
        fine_node* new_node = create_node(list, key);
        new_node->data = data;
        new_node->k = highest_link;

        /* Link up to pre-computed level */
        for (int i = 0; i <= highest_link; i++) {
            new_node->next[i] = succs[i];
            preds[i]->next[i] = new_node;
        }
        new_node->fully_linked = true;
        for (int l = 0; l <= highlock; l++)
        {
            omp_unset_nest_lock(preds[l]->lock);
        }

    }

    free(preds);
    free(succs);
    return true;
}

bool fine_skiplist_remove(fine_list* list, int key, void** data_out) {
    if (key < list->keyrange.min || key > list->keyrange.max) return false;

    fine_node** preds = (fine_node**)malloc(sizeof(fine_node*) * list->levels);
    if (!preds) return NULL;
    fine_node** succs = (fine_node**)malloc(sizeof(fine_node*) * list->levels);
    if (!succs) { free(preds); return false;}

    fine_node* victim = NULL;
    bool marked = false;
    int k = -1;

    while (true) {
        int f = find_neighbours(list, key, preds, succs);
        if (f>=0) victim = succs[f];
        if (marked || 
        ((f >= 0)&&victim->fully_linked&&victim->k==f)) {
            if (!marked) {
                k = victim->k;
                omp_set_nest_lock(victim->lock);
                if (victim->marked) {
                    omp_unset_nest_lock(victim->lock);
                    free(preds);
                    free(succs);
                    return false;
                }
                victim->marked = true;
                marked = true;
            } 
            int highlock = -1;
            fine_node *pred;
            bool valid = true;
            for (int l = 0; valid&&(l<=k); l++)
            {
                pred = preds[l];
                omp_set_nest_lock(pred->lock);
                highlock = l;
                valid = !pred->marked&&(pred->next[l]==victim);
            }
            /* failed, retry */
            if (!valid) {
                for (int l = 0; l <= highlock; l++)
                {
                    omp_unset_nest_lock(preds[l]->lock);
                }  
                continue;
            }
            /* unlink */
            for (int l = k; l >= 0; l--)
            {
                preds[l]->next[l] = victim->next[l];
            }
            /* unlock */
            omp_unset_nest_lock(victim->lock);
            for (int l = 0; l <= highlock; l++)
            {
                omp_unset_nest_lock(preds[l]->lock);
            }
            if (data_out) *data_out = victim->data;
            free(preds);
            free(succs);
            return true;           
        } else {
                free(preds);
                free(succs);
                return false;
        }
    }
}

/*
int main(int argc, char const *argv[])
{
    keyrange_t keyrange = {0, 10};
    fine_list* list = fine_skiplist_init(4, 0.5, keyrange);

    unsigned short int *random_state = (unsigned short int*)malloc(6);
    if (!random_state) return -1;
    srand48_r(1430, (struct drand48_data *)random_state);

    fine_skiplist_add(list, keyrange.min + 1, NULL, random_state);

    return 0;
}
*/