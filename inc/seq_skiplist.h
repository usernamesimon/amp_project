#include <stdint.h>
#include <stdbool.h>
#include "common.h"

typedef struct _node
{
  /* array of next pointers, next[0] holds next element
  in level 0, next[1] the next element in level 1 if it
  exists or null if this node is not present in level 1
  and above */
  struct _node** next;

  /* the key this node is identified with */
  int key;
  void* data;
} node;

typedef struct _seq_list
{
  node* head;
  uint8_t levels;
  double prob;
  keyrange_t keyrange;
  struct drand48_data* random_state;
} seq_list;

/* Initialize an instance of a sequential skip list 
    levels -> number of levels of express lanes
    prob -> probability that an element is inserted in levels > 0
    keyrange -> range for keys to be used
    */
seq_list* seq_skiplist_init(uint8_t levels, double prob, keyrange_t keyrange,
  long int random_seed);

/* Reclaim memory */
void seq_skiplist_destroy(seq_list* list);

/* Search for an element in the list.
  Return a node pointer to the element if key is found in list,
  otherwise return NULL*/
node* seq_skiplist_contains(seq_list* list, int key);

/* Add an element with key and data to list
  Return TRUE if inserted or FALSE if insertion failed*/
bool seq_skiplist_add(seq_list* list, int key, void* data);

/* Remove node 'key' from 'list'. 
  Returns true if removal succefull and sets 'data_out' to the data element it contained
  Returns false if key was not found
*/
bool seq_skiplist_remove(seq_list* list, int key, void** data_out);

/* Execute a benchmark with following parameters
    time_intervall -> time to do throughput measurement
    n_prefill -> Number of prefill items
    operations_mic -> percentage of inserts, deletes
      and contains
    r_seed -> seed for random number generator
    keyrange -> range for keys to be used
    */
struct bench_result* seq_skiplist_benchmark(uint16_t time_intervall, uint16_t n_prefill, 
  operations_mix_t operations_mix, selection_strategy strat,
  unsigned int r_seed, keyrange_t keyrange, uint8_t levels, double prob) ;