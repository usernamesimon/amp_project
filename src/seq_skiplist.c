#include "../inc/seq_skiplist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

seq_list* seq_skiplist_init(uint8_t levels, double prob, keyrange_t keyrange, long int random_seed) {
    seq_list* skiplist = (seq_list*)malloc(sizeof(seq_list));
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

void seq_skiplist_destroy(seq_list* list) {
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
bool find_predecessors(seq_list* list, int key, node** preds) {
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

node* seq_skiplist_contains(seq_list* list, int key) {
    node** preds = (node**)malloc(sizeof(node*) * list->levels);
    if (!preds) return NULL;

    node* result = NULL;
    if (find_predecessors(list, key, preds)) {
        result = preds[0]->next[0];
    }
    free(preds);
    return result;
}

bool seq_skiplist_add(seq_list* list, int key, void* data) {
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

bool seq_skiplist_remove(seq_list* list, int key, void** data_out) {
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

bool check_bit(int* vector, int position) {
    int word = position / (sizeof(int)*8);
    int bit = position % (sizeof(int)*8);
    int mask = 1 << bit;
    return (vector[word] & mask) > 0;
}
void set_bit(int* vector, int position) {
    int word = position / (sizeof(int)*8);
    int bit = position % (sizeof(int)*8);
    int mask = 1 << bit;
    vector[word] = vector[word] | mask;
}

struct bench_result *seq_skiplist_benchmark(uint16_t time_interval, uint16_t n_prefill,
    operations_mix_t operations_mix, selection_strategy strat,
    unsigned int r_seed, keyrange_t keyrange, uint8_t levels, double prob)
{
    int range = keyrange.max - keyrange.min;
    struct bench_result *result = malloc(sizeof(struct bench_result));
    memset(&result->counters, 0, sizeof(result->counters));
    result->time = 0.0;

    seq_list *skiplist = seq_skiplist_init(levels, prob, keyrange, r_seed);
    if (!skiplist)
        return NULL;
    
    /* To keep track of the keys we used so far we use a huge bit vector.
        If a key k was already used, the (k - key_range.min)'th bit in used_keys will be set
    */
    size_t used_keys_size = range/(sizeof(int)*8) + 1;
    int* used_keys = (int*)malloc(sizeof(int)*used_keys_size);
    if (!used_keys) return 0;
    memset(used_keys, 0, used_keys_size);
    int n_used_key = 0;

    /* initialize random state for key selection */
    struct drand48_data *random_state = (struct drand48_data *)malloc(6);
    srand48_r(r_seed + 1, random_state);

    /* prefill the skiplist */
    double die;
    int key = n_prefill + 1;
    
    /* Warning: this will not terminate if n_prefill > keyrange */
    if (strat == RANDOM || strat == UNIQUE)
    {
        uint16_t prefill_remaining = n_prefill;
        while (prefill_remaining > 0)
        {
            drand48_r(random_state, &die);
            key = (int)(die * range);
            if (check_bit(used_keys, key)) continue; // key already contained

            set_bit(used_keys, key);
            n_used_key++;
            prefill_remaining--;
            seq_skiplist_add(skiplist, key + keyrange.min, NULL);            
        }
    }
    else
    {
        for (int i = 0; i < n_prefill; i++)
        {
            seq_skiplist_add(skiplist, keyrange.min + i + 1, NULL);
        }
    }

    clock_t endtime = clock() + time_interval * CLOCKS_PER_SEC;
    clock_t interval;
    bool res;
    while (clock() < endtime)
    {
        /* determine next key */
        if (strat == RANDOM)
        {
            drand48_r(random_state, &die);
            key = (int)(die * range + keyrange.min);
        }
        else if (strat == UNIQUE)
        {
            /* Need to start anew if we used all possible keys */
            if (n_used_key >= range) {
                memset(used_keys, 0, used_keys_size);
            }

            do
            {
                drand48_r(random_state, &die);
                key = (int)(die * range);
            } while (check_bit(used_keys, key));
            set_bit(used_keys, key);
            n_used_key++;
            key = key + keyrange.min;
            
        }
        else if (strat == SUCCESIVE)
        {
            key++;
            if (key > keyrange.max)
                key = keyrange.min;
        }

        /* determine next operation */
        drand48_r(random_state, &die);
        if (die < operations_mix.insert_p)
        {
            interval = clock();
            res = seq_skiplist_add(skiplist, key, NULL);
            result->time += (float)(clock() - interval) / CLOCKS_PER_SEC;
            result->counters.successful_adds += res;
            result->counters.failed_adds += !res;
        }
        else if (die < operations_mix.insert_p + operations_mix.contain_p)
        {
            interval = clock();
            res = seq_skiplist_contains(skiplist, key) != NULL;
            result->time += (float)(clock() - interval) / CLOCKS_PER_SEC;
            result->counters.succesfull_contains += res;
            result->counters.failed_contains += !res;
        }
        else
        {
            interval = clock();
            res = seq_skiplist_remove(skiplist, key, NULL);
            result->time += (float)(clock() - interval) / CLOCKS_PER_SEC;
            result->counters.succesfull_removes += res;
            result->counters.failed_removes += !res;
        }
    }

    free(used_keys);
    seq_skiplist_destroy(skiplist);
    return result;
}

#ifdef DEBUG
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
#endif

/* Main Function */
int main(int argc, char const* argv[]) {
    if (argc < 11) {
        fprintf(stderr, "Usage: %s <time_interval> <n_prefill> <insert_p> <delete_p> <contain_p> <key_min> <key_max> <levels> <prob> <strategy>\n", argv[0]);
        return EXIT_FAILURE;
    }

    uint16_t time_interval = atoi(argv[1]);
    uint16_t n_prefill = atoi(argv[2]);
    operations_mix_t operations_mix = {atof(argv[3]), atof(argv[4]), atof(argv[5])};
    keyrange_t keyrange = {atoi(argv[6]), atoi(argv[7])};
    uint8_t levels = atoi(argv[8]);
    double prob = atof(argv[9]);
    selection_strategy strat;

    if (strcmp(argv[10],"random") == 0) strat = RANDOM;
    else if (strcmp(argv[10],"unique") == 0) strat = UNIQUE;
    else strat = SUCCESIVE;
    

    struct bench_result* result = seq_skiplist_benchmark(time_interval, n_prefill, operations_mix,
        strat, 12345, keyrange, levels, prob);

    if (!result) {
        fprintf(stderr, "Error: Benchmark failed.\n");
        return EXIT_FAILURE;
    }

    float total_ops = result->counters.successful_adds + result->counters.failed_adds +
                      result->counters.succesfull_contains + result->counters.failed_contains +
                      result->counters.succesfull_removes + result->counters.failed_removes;

    printf("Average Time Per Experiment: %.2f seconds\n", result->time);
    printf("Total Operations: %.0f\n", total_ops);
    printf("Insertions: %d successful / %d attempted\n",
        result->counters.successful_adds, result->counters.successful_adds + result->counters.failed_adds);
    printf("Deletions: %d successful / %d attempted\n",
        result->counters.succesfull_removes, result->counters.succesfull_removes + result->counters.failed_removes);
    printf("Contains: %d successful / %d attempted\n",
        result->counters.succesfull_contains, result->counters.succesfull_contains + result->counters.failed_contains);
    printf("Throughput: %.3e ops/sec\n", total_ops / result->time);

    free(result);
    return 0;
}

