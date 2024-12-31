#ifndef SEQ_SKIPLIST_H
#define SEQ_SKIPLIST_H

#include <stdint.h>
#include <stdbool.h>
#include "common.h"

typedef struct _seq_node {
  /* array of next pointers, next[0] holds next element
  in level 0, next[1] the next element in level 1 if it
  exists or null if this node is not present in level 1
  and above */
  struct _seq_node** next;

  /* the key this node is identified with */
  int key;
  void* data;
} seq_node;

typedef struct _seq_list {
  /* Head node of the skip list */
  seq_node* head;

  /* Number of levels in the skip list */
  uint8_t levels;

  /* Probability of a node being present in higher levels */
  double prob;

  /* Key range for the skip list */
  keyrange_t keyrange;

  /* Random state for probabilistic decisions */
  struct drand48_data* random_state;
} seq_list;

/* Initialize an instance of a sequential skip list 
    levels -> number of levels of express lanes
    prob -> probability that an element is inserted in levels > 0
    keyrange -> range for keys to be used
    random_seed -> seed for the random number generator
*/
seq_list* seq_skiplist_init(uint8_t levels, double prob, keyrange_t keyrange,
  long int random_seed);

/* Reclaim memory used by the skip list */
void seq_skiplist_destroy(seq_list* list);

/* Search for an element in the list.
  Return a node pointer to the element if key is found in list,
  otherwise return NULL */
seq_node* seq_skiplist_contains(seq_list* list, int key);

/* Add an element with key and data to the list.
  Return TRUE if inserted or FALSE if insertion failed */
bool seq_skiplist_add(seq_list* list, int key, void* data);

/* Remove a node with the specified 'key' from 'list'.
  Returns true if removal was successful and sets 'data_out'
  to the data element it contained.
  Returns false if key was not found. */
bool seq_skiplist_remove(seq_list* list, int key, void** data_out);

#endif // SEQ_SKIPLIST_H
