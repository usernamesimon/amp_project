#ifndef FINE_SKIPLIST_H
#define FINE_SKIPLIST_H

#include <stdint.h>
#include <stdbool.h>
#include <omp.h>

#include "common.h"

typedef struct _fine_node {
  /* array of next pointers, next[0] holds next element
  in level 0, next[1] the next element in level 1 if it
  exists or null if this node is not present in level 1
  and above */
  struct _fine_node** next;

  /* the key this node is identified with */
  int key;

  /* possible data*/
  void* data;

  /* element is in list if marked is false and fully_linked is true*/
  bool marked;
  bool fully_linked;

  omp_nest_lock_t* lock;

  /* node is linked up to level k */
  int k;
} fine_node;

typedef struct _fine_list {
  /* Head node of the skip list */
  fine_node* head;

  /* Number of levels in the skip list */
  uint8_t levels;

  /* Probability of a node being present in higher levels */
  double prob;

  /* Key range for the skip list */
  keyrange_t keyrange;
} fine_list;

/* Initialize an instance of a sequential skip list 
    levels -> number of levels of express lanes
    prob -> probability that an element is inserted in levels > 0
    keyrange -> range for keys to be used
*/
fine_list* fine_skiplist_init(uint8_t levels, double prob, keyrange_t keyrange);

/* Reclaim memory used by the skip list */
void fine_skiplist_destroy(fine_list* list);

/* Search for an element in the list.
  Return a node pointer to the element if key is found in list,
  otherwise return NULL */
fine_node* fine_skiplist_contains(fine_list* list, int key);

/* Add an element with key and data to the list.
  Return TRUE if inserted or FALSE if insertion failed
  Because we want randomness per thread, supply random_state for choosing levels to link */
bool fine_skiplist_add(fine_list* list, int key, void* data, unsigned short int random_state[3]);

/* Remove a node with the specified 'key' from 'list'.
  Returns true if removal was successful and sets 'data_out'
  to the data element it contained.
  Returns false if key was not found. */
bool fine_skiplist_remove(fine_list* list, int key, void** data_out);


#endif // SEQ_SKIPLIST_H
