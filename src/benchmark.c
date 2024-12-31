#include "../inc/common.h"
#include "../inc/seq_skiplist.h"
#include "../inc/coarse_skiplist.h"


#include <time.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

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
struct bench_result* seq_skiplist_benchmark(uint16_t time_interval, uint16_t n_prefill, 
  operations_mix_t operations_mix, selection_strategy strat,
  unsigned int r_seed, keyrange_t keyrange, uint8_t levels, double prob);

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

unique_keyarray_t* unique_keys_init(int max) {
    unique_keyarray_t* keys = (unique_keyarray_t*)malloc(sizeof(unique_keyarray_t));
    if(!keys) return NULL;
    keys->size = max;
    keys->array = (int*)malloc(keys->size*sizeof(int));
    if(!keys->array) {
        free(keys);
        return NULL;
    }
    keys->current = keys->array;
    keys->shuffled = keys->array;
    for (int i = 0; i < max; i++)
    {
        keys->array[i] = i;
    }
    return keys;
}

int unique_keys_next(unique_keyarray_t* keys, struct drand48_data* random_state) {
    int* array = keys->array;
    int* current = keys->current;
    int* shuffled = keys->shuffled;

    /* Current position is already randomized */
    if (current < shuffled)
    {
        return *current++;
    /* Need to randomize the current position */
    } else if (current < (array + keys->size))
    {
        assert(current == shuffled);
        int range = keys->size - (shuffled - array);
        double die;
        drand48_r(random_state, &die);
        int swapi = (int)(die * range);
        int* swap = shuffled + swapi;
        int tmp = *current;
        *current = *swap;
        *swap = tmp;
        shuffled++;
        return *current++;
    /* We are passed the end of the array, wrap around */     
    } else {
        current = array;
        return *current++;
    }    
}

void unique_keys_destroy(unique_keyarray_t* keys) {
    if(!keys) return;
    if(keys->array) free(keys->array);
    free(keys);
}

struct bench_result *seq_skiplist_benchmark(uint16_t time_interval, uint16_t n_prefill,
    operations_mix_t operations_mix, selection_strategy strat,
    unsigned int r_seed, keyrange_t keyrange, uint8_t levels, double prob)
{
    printf("Executing benchmark of sequential skiplist\n");
    #ifdef DEBUG
    printf("Parameters\n");
    printf("> Time interval for measurment: %u\n", time_interval);
    printf("> Number of prefilled items: %u\n", n_prefill);
    printf("> Operations mix:\n>\t>Insertions: %f\n>\t>Contains: %f\n", operations_mix.insert_p, operations_mix.contain_p);
    printf("> Selection strategy: %d\n", strat);
    printf("> Random seed: %u", r_seed);
    printf("> Keyrange:\n>\t>Min: %d\n>\t>Max: %d\n", keyrange.min, keyrange.max);
    printf("> Levels of skiplist: %d\n", levels);
    printf("> Probability for levels: %f\n", prob);
    #endif
    int range = keyrange.max - keyrange.min;
    struct bench_result *result = malloc(sizeof(struct bench_result));
    memset(&result->counters, 0, sizeof(result->counters));
    result->time = 0.0;

    seq_list *skiplist = seq_skiplist_init(levels, prob, keyrange, r_seed);
    if (!skiplist)
        return NULL;
    
    /* initialize random state for key selection */
    struct drand48_data *random_state = (struct drand48_data *)malloc(6);
    srand48_r(r_seed + 1, random_state);

    /* prefill the skiplist */
    double die;
    int key = n_prefill + 1;

    unique_keyarray_t* unique_keys;
    
    /* Prefill list */
    if (strat == UNIQUE)
    {
        unique_keys = unique_keys_init(range);
        if (unique_keys == NULL)
            return NULL;
        for (size_t i = 0; i < n_prefill; i++)
        {
            seq_skiplist_add(skiplist, 
                keyrange.min + unique_keys_next(unique_keys, random_state), NULL);
        }
        
    }
    else if (strat == RANDOM) {
        unique_keys = unique_keys_init(range);
        if (unique_keys == NULL)
            return NULL;
        for (size_t i = 0; i < n_prefill; i++)
        {
            seq_skiplist_add(skiplist, 
                keyrange.min + unique_keys_next(unique_keys, random_state), NULL);
        }
        unique_keys_destroy(unique_keys);
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
            key = unique_keys_next(unique_keys, random_state);
            
        }
        else if (strat == SUCCESSIVE)
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
            result->counters.successfull_adds += res;
            result->counters.failed_adds += !res;
        }
        else if (die < operations_mix.insert_p + operations_mix.contain_p)
        {
            interval = clock();
            res = seq_skiplist_contains(skiplist, key) != NULL;
            result->time += (float)(clock() - interval) / CLOCKS_PER_SEC;
            result->counters.successfull_contains += res;
            result->counters.failed_contains += !res;
        }
        else
        {
            interval = clock();
            res = seq_skiplist_remove(skiplist, key, NULL);
            result->time += (float)(clock() - interval) / CLOCKS_PER_SEC;
            result->counters.successfull_removes += res;
            result->counters.failed_removes += !res;
        }
    }

    if (strat == UNIQUE) unique_keys_destroy(unique_keys);
    seq_skiplist_destroy(skiplist);
    return result;
}
