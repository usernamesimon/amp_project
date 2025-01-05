#ifndef COARSE_SKIPLIST_H
#define COARSE_SKIPLIST_H

#include <stdint.h>
#include <stdbool.h>
#include <omp.h>

#include "common.h"

typedef struct _coarse_node {
  /* array of next pointers, next[0] holds next element
  in level 0, next[1] the next element in level 1 if it
  exists or null if this node is not present in level 1
  and above */
  struct _coarse_node** next;

  /* the key this node is identified with */
  int key;
  void* data;
} coarse_node;

typedef struct _coarse_list {
  /* Head node of the skip list */
  coarse_node* head;

  /* Number of levels in the skip list */
  uint8_t levels;

  /* Probability of a node being present in higher levels */
  double prob;

  /* Key range for the skip list */
  keyrange_t keyrange;

  /* one BIG lock for whole list */
  omp_lock_t* lock;
} coarse_list;

/* Initialize an instance of a sequential skip list 
    levels -> number of levels of express lanes
    prob -> probability that an element is inserted in levels > 0
    keyrange -> range for keys to be used
*/
coarse_list* coarse_skiplist_init(uint8_t levels, double prob, keyrange_t keyrange);

/* Reclaim memory used by the skip list */
void coarse_skiplist_destroy(coarse_list* list);

/* Search for an element in the list.
  Return a node pointer to the element if key is found in list,
  otherwise return NULL */
coarse_node* coarse_skiplist_contains(coarse_list* list, int key);

/* Add an element with key and data to the list.
  Return TRUE if inserted or FALSE if insertion failed
  Because we want randomness per thread, supply random_state for choosing levels to link */
bool coarse_skiplist_add(coarse_list* list, int key, void* data, unsigned short int random_state[3]);

/* Remove a node with the specified 'key' from 'list'.
  Returns true if removal was successful and sets 'data_out'
  to the data element it contained.
  Returns false if key was not found. */
bool coarse_skiplist_remove(coarse_list* list, int key, void** data_out);


#endif // SEQ_SKIPLIST_H
