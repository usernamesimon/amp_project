#ifndef COARSE_SKIPLIST_H
#define COARSE_SKIPLIST_H

#include <stdint.h>
#include <stdbool.h>
#include "common.h"

typedef struct _node {
  /* array of next pointers, next[0] holds next element
  in level 0, next[1] the next element in level 1 if it
  exists or null if this node is not present in level 1
  and above */
  struct _node** next;

  /* the key this node is identified with */
  int key;
  void* data;
} node;

typedef struct _coarse_list {
  /* Head node of the skip list */
  node* head;

  /* Number of levels in the skip list */
  uint8_t levels;

  /* Probability of a node being present in higher levels */
  double prob;

  /* Key range for the skip list */
  keyrange_t keyrange;

  /* Random state for probabilistic decisions */
  struct drand48_data* random_state;
} coarse_list;

/* Initialize an instance of a sequential skip list 
    levels -> number of levels of express lanes
    prob -> probability that an element is inserted in levels > 0
    keyrange -> range for keys to be used
    random_seed -> seed for the random number generator
*/
coarse_list* coarse_skiplist_init(uint8_t levels, double prob, keyrange_t keyrange,
  long int random_seed);

/* Reclaim memory used by the skip list */
void coarse_skiplist_destroy(coarse_list* list);

/* Search for an element in the list.
  Return a node pointer to the element if key is found in list,
  otherwise return NULL */
node* coarse_skiplist_contains(coarse_list* list, int key);

/* Add an element with key and data to the list.
  Return TRUE if inserted or FALSE if insertion failed */
bool coarse_skiplist_add(coarse_list* list, int key, void* data);

/* Remove a node with the specified 'key' from 'list'.
  Returns true if removal was successful and sets 'data_out'
  to the data element it contained.
  Returns false if key was not found. */
bool coarse_skiplist_remove(coarse_list* list, int key, void** data_out);

/* Execute a benchmark with the following parameters:
    time_interval -> time to do throughput measurement (in seconds)
    n_prefill -> Number of prefill items
    operations_mix -> Percentage of inserts, deletes, and contains
    strat -> Selection strategy for keys (RANDOM, UNIQUE, SUCCESIVE)
    r_seed -> Seed for random number generator
    keyrange -> Range for keys to be used
    levels -> Number of levels in the skip list
    prob -> Probability of a node being in higher levels
    num_threads -> Number of threads for concurrent execution
    repetitions -> Number of repetitions of the benchmark
    range_type -> Key range type (COMMON, DISJOINT, PER_THREAD)
*/
struct bench_result* coarse_skiplist_benchmark(uint16_t num_threads, uint16_t time_interval, uint16_t n_prefill, 
  operations_mix_t operations_mix, selection_strategy strat, key_overlap overlap,
  unsigned int r_seed, keyrange_t keyrange, uint8_t levels, double prob);

#endif // SEQ_SKIPLIST_H
