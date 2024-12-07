#include "../inc/seq_skiplist.h"
#include <stdlib.h>
#include <time.h>

seq_list* seq_skiplist_init(uint8_t levels, double prob, keyrange_t keyrange,
  long int random_seed)
{
  seq_list* skiplist = (seq_list*)malloc(sizeof(seq_list));
  if (!skiplist) return NULL;
  skiplist->levels = levels;
  skiplist->prob = prob;
  skiplist->keyrange.min = keyrange.min;
  skiplist->keyrange.max = keyrange.max;

  /* initialize random state */
  skiplist->random_state = (struct drand48_data*)malloc(6);
  srand48_r(random_seed, skiplist->random_state);

  /* Create head node */
  skiplist->head = (node*)malloc(sizeof(node));
  if (!skiplist->head) return NULL;
  skiplist->head->next = (node**)malloc(sizeof(node*)*skiplist->levels);
  for (size_t i = 0; i < skiplist->levels; i++)
  {
    skiplist->head->next[i] = NULL;
  }
  skiplist->head->key = skiplist->keyrange.min;
  return skiplist;
}

void seq_skiplist_destroy(seq_list* list) {
  
  /* free nodes space */
  node* current = list->head;
  while (current->next[0])
  {
    node* next = current->next[0];
    free(current->next);
    free(current);
    current = next;
  }
  free(current->next);
  free(current);

  free(list->random_state);
}

/* Find the predecessors of 'key' for each level in 'list' and writes them to 'preds'
  If 'key' is contained in 'list', 'pred' will contain a pointer to the node with 
  key = 'key' for each level it was present in
  Returns true if key was found, false otherwise */
bool find_predecessors(seq_list* list, int key, node** preds) {
  node* current = list->head;
  /* Start at top level to try to find key */
  for (int i = list->levels-1; i >= 0; i--)
  {
    
    node* next = current->next[i];
    /* if key is bigger go right */
    while (next && key > next->key)
    {
      current = next;
      next = current->next[i];
    }
    /* Found key, fill lower levels with predecessor of key node and return */
    if (next && key == next->key) {
      for (;i >= 0; i--)
      {
        preds[i] = current;
      }
      return true;
    };
    /* found larger key or end of list, go down a level by doing the next iteration
      of the for loop. Save the predecessor of the current level to preds
    */
    preds[i] = current;
  }
  return false;
}

node* seq_skiplist_contains(seq_list* list, int key) {
  node** result = (node**)malloc(sizeof(node*)*list->levels);
  node* temp = NULL;
  if (find_predecessors(list, key, result)) temp = result[0]->next[0];
  free(result);
  return temp;  
}

bool seq_skiplist_add(seq_list* list, int key, void* data){
  if (key <= list->keyrange.min || key > list->keyrange.max) return false;
  node** preds = (node**)malloc(sizeof(node*)*list->levels);
  /* key already contained */
  if (find_predecessors(list, key, preds)) {
    free(preds);
    return false;
  }
  /* create new node and link it in in level 0 */
  node* new = (node*)malloc(sizeof(node));
  if (!new) return false;
  new->next = (node**)malloc(sizeof(node*)*list->levels);

  if (!new->next) return false;
  new->key = key;
  new->data = data;
  new->next[0] = preds[0]->next[0];
  preds[0]->next[0] = new;

  /* for higher levels decide by chance if it is linked in */
  size_t i = 1;
  for (; i < list->levels; i++)
  {
    double die;
    drand48_r(list->random_state, &die);
    if (die > list->prob) break;
    new->next[i] = preds[i]->next[i];
    preds[i]->next[i] = new;
  }
  for (; i < list->levels; i++)
  {
    new->next[i] = NULL;
  }
  free(preds);  
  return true;
}

bool seq_skiplist_remove(seq_list* list, int key, void** data_out) {
  node** preds = (node**)malloc(sizeof(node*)*list->levels);
  /* key not contained */
  if (!find_predecessors(list, key, preds)) {
    free(preds);
    return false;
  }
  node* delete = preds[0]->next[0];
  for (size_t i = 0; i < list->levels; i++)
  {
    if(preds[i]->next[i]==delete)
      preds[i]->next[i] = delete->next[i];
  }
  if(data_out) *data_out = delete->data;
  free(delete->next);
  free(delete);
  free(preds);
  return true;
}

struct bench_result* seq_skiplist_benchmark(uint16_t time_intervall, uint16_t n_prefill, 
  operations_mix_t operations_mix, selection_strategy strat,
  unsigned int r_seed, keyrange_t keyrange, uint8_t levels, double prob) 
{
  struct bench_result* result = malloc(sizeof(struct bench_result));
  result->counters.failed_adds = 0;
  result->counters.failed_contains = 0;
  result->counters.failed_removes = 0;
  result->counters.successful_adds = 0;
  result->counters.succesfull_contains = 0;
  result->counters.succesfull_removes = 0;
  result->time = 0.0;
  seq_list* list = seq_skiplist_init(levels, prob, keyrange, r_seed);
  int range = keyrange.max - keyrange.min;

  /* initialize random state for key selection */
  struct drand48_data* random_state = (struct drand48_data*)malloc(6);
  srand48_r(r_seed+1, random_state);

  /* prefill the skiplist */
  double die;
  int key = n_prefill + 1;
  if (strat == RANDOM || strat == UNIQUE) {
    uint16_t prefill_remaining = n_prefill;
    while (prefill_remaining > 0)
    {
      drand48_r(random_state, &die);
      key = (int)(die*range + keyrange.min);
      if (seq_skiplist_add(list, key, NULL)) prefill_remaining--;
    }
  } else {
    for (int i = 0; i < n_prefill; i++)
    {
      seq_skiplist_add(list, keyrange.min + i + 1, NULL);
    }
  }

  clock_t endtime = clock() + time_intervall*CLOCKS_PER_SEC;
  clock_t interval;
  bool res;
  while(clock()<endtime) 
  {
    /* determine next key */
    if (strat == RANDOM) {
      drand48_r(random_state, &die);
      key = (int)(die*range + keyrange.min);
    } else if (strat == SUCCESIVE) {
      key++;
      if (key > keyrange.max) key = keyrange.min;
    }
    //TODO unique strategy

    /* determine next operation */
    drand48_r(random_state, &die);
    if (die < operations_mix.insert_p)
    {
      interval = clock();
      res = seq_skiplist_add(list, key, NULL);
      result->time += (float)(clock() - interval)/CLOCKS_PER_SEC;
      result->counters.successful_adds += res;
      result->counters.failed_adds += !res;
    } else if (die < operations_mix.insert_p + operations_mix.contain_p) {
      interval = clock();
      res = seq_skiplist_contains(list, key) != NULL;
      result->time += (float)(clock() - interval)/CLOCKS_PER_SEC;
      result->counters.succesfull_contains += res;
      result->counters.failed_contains += !res;
    } else {
      interval = clock();
      res = seq_skiplist_remove(list, key, NULL);
      result->time += (float)(clock() - interval)/CLOCKS_PER_SEC;
      result->counters.succesfull_removes += res;
      result->counters.failed_removes += !res;
    }
    
  }
  return result;
}

#include <stdio.h>
#include <string.h>
#define LINELEN (1024)
#define TOKENLEN 3
/* BE MINDFUL OF LINELEN AND TOKENLEN LIMITS! */
void print_list(seq_list* list) {
  char print[list->levels][LINELEN];
  
  char temp[TOKENLEN+2];
  char empty[TOKENLEN+2];
  sprintf(empty, " ");
  for (int i = 0; i < TOKENLEN; i++)
  {
    strcat(empty, "-");
  }
  
  /* print head sentinel node */
  node** current = (node**)malloc(sizeof(node*)*list->levels);
  for (size_t i = 0; i < list->levels; i++)
  {
    current[i] = list->head;
    snprintf(print[i], TOKENLEN+1, "%*d", TOKENLEN, list->head->key);
  }
  /* print the levels simultaneously:
    Walk the current[0] pointer trough level 0 and compare the 
    higher level current pointers against its next key. If they match
    print and advance the higher level
  */
  while (current[0]->next && current[0]->next[0])
  {
    /* set the temp buffer and print level 0 */
    int next_key = current[0]->next[0]->key;
    snprintf(temp, TOKENLEN+2, " %*d", TOKENLEN, next_key);
    strcat(print[0], temp);
    /* compare higher leveled pointers */
    for (size_t i = 1; i < list->levels; i++)
    {
      if (current[i]->next[i] && current[i]->next[i]->key == next_key)
      {
        snprintf(temp, TOKENLEN+2, " %*d", TOKENLEN, next_key);
        current[i] = current[i]->next[i];
      } else
      {
        snprintf(temp, TOKENLEN+2, "%s", empty);
      }
      strcat(print[i], temp);     
    }
    current[0] = current[0]->next[0];    
  }
  for (int i = list->levels-1; i >= 0; i--)
  {
    printf("Level %d: %s\n", i, print[i]);
  }
  printf("\n");
}

int main(int argc, char const *argv[])
{
  keyrange_t keyrange;
  keyrange.min = 0;
  keyrange.max = 19;
  operations_mix_t operations_mix;
  operations_mix.insert_p = 0.1;
  operations_mix.delete_p = 0.1;
  operations_mix.contain_p = 0.8;

  struct bench_result* result = 
    seq_skiplist_benchmark(1, 8, operations_mix, SUCCESIVE, 1, keyrange, 4, 0.5);
  float failed_ops = result->counters.failed_adds + result->counters.failed_contains + result->counters.failed_removes;
  float succ_ops = result->counters.successful_adds + result->counters.succesfull_contains + result->counters.succesfull_removes;
  printf("Succesfull OPS: %e\n", succ_ops);
  printf("Failed OPS: %e\n", failed_ops);
  printf("Throughput: %e / s\n", (failed_ops+succ_ops)/result->time);
  return 0;
}
