#include "../inc/common.h"
#include "../inc/seq_skiplist.h"
#include "../inc/coarse_skiplist.h"
#include "../inc/fine_skiplist.h"
#include "../inc/lock_free_skiplist.h"


#include <unistd.h>
#include <time.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <omp.h>

//#define DEBUG

const char *implementation_strings[] = {"sequential skiplist", "coarse lock skiplist"};

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
struct bench_result *seq_skiplist_benchmark(uint16_t time_interval, uint16_t n_prefill,
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
struct bench_result *parallel_skiplist_benchmark(uint16_t num_threads, uint16_t time_interval, uint16_t n_prefill,
                                                 operations_mix_t operations_mix, selection_strategy strat, key_overlap overlap,
                                                 unsigned int r_seed, keyrange_t keyrange, uint8_t levels, double prob, implementation imp);

/* Definitions for lock free skiplist */
// Define a node that contains key and value pair.
struct my_node {
    // Metadata for skiplist node.
    skiplist_node snode;
    // My data here: {int, int} pair.
    int key;
    void* value;
};

// Define a comparison function for `my_node`.
static int my_cmp(skiplist_node* a, skiplist_node* b, void* aux) {
    // Get `my_node` from skiplist node `a` and `b`.
    struct my_node *aa, *bb;
    aa = _get_entry(a, struct my_node, snode);
    bb = _get_entry(b, struct my_node, snode);

    // aa  < bb: return neg
    // aa == bb: return 0
    // aa  > bb: return pos
    if (aa->key < bb->key) return -1;
    if (aa->key > bb->key) return 1;
    return 0;
}

unique_keyarray_t *unique_keys_init(int max)
{
    unique_keyarray_t *keys = (unique_keyarray_t *)malloc(sizeof(unique_keyarray_t));
    if (!keys)
        return NULL;
    keys->size = max;
    keys->array = (int *)malloc(keys->size * sizeof(int));
    if (!keys->array)
    {
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

int unique_keys_next(unique_keyarray_t *keys, struct drand48_data *random_state)
{
    int *array = keys->array;
    int *current = keys->current;
    int *shuffled = keys->shuffled;

    /* Current position is already randomized */
    if (current < shuffled)
    {
        return *current++;
    }
    /* Need to randomize the current position */
    else if (current < (array + keys->size))
    {
        assert(current == shuffled);
        int range = keys->size - (shuffled - array);
        double die;
        drand48_r(random_state, &die);
        int swapi = (int)(die * range);
        int *swap = shuffled + swapi;
        int tmp = *current;
        *current = *swap;
        *swap = tmp;
        shuffled++;
        return *current++;
    }
    /* We are passed the end of the array, wrap around */
    else
    {
        current = array;
        return *current++;
    }
}

void unique_keys_destroy(unique_keyarray_t *keys)
{
    if (!keys)
        return;
    if (keys->array)
        free(keys->array);
    free(keys);
}

struct bench_result *seq_skiplist_benchmark(uint16_t time_interval, uint16_t n_prefill,
                                            operations_mix_t operations_mix, selection_strategy strat,
                                            unsigned int r_seed, keyrange_t keyrange, uint8_t levels, double prob)
{
#ifdef DEBUG
    printf("Executing benchmark of sequential skiplist\n");
    printf("Parameters\n");
    printf("> Time interval for measurment: %u\n", time_interval);
    printf("> Number of prefilled items: %u\n", n_prefill);
    printf("> Operations mix:\n>\t>Insertions: %f\n>\t>Contains: %f\n", operations_mix.insert_p, operations_mix.contain_p);
    printf("> Selection strategy: %d\n", strat);
    printf("> Random seed: %u\n", r_seed);
    printf("> Keyrange:\n>\t>Min: %d\n>\t>Max: %d\n", keyrange.min, keyrange.max);
    printf("> Levels of skiplist: %d\n", levels);
    printf("> Probability for levels: %f\n", prob);
#endif
    int range = keyrange.max - keyrange.min;
    struct bench_result *result = malloc(sizeof(struct bench_result));
    memset(&result->counters, 0, sizeof(result->counters));
    result->cpu_time = 0.0;

    seq_list *skiplist = seq_skiplist_init(levels, prob, keyrange, r_seed);
    if (!skiplist)
        return NULL;

    /* initialize random state for key selection */
    struct drand48_data *random_state = (struct drand48_data *)malloc(6);
    srand48_r(r_seed + 1, random_state);

    /* prefill the skiplist */
    double die;
    int key = n_prefill + 1;

    unique_keyarray_t *unique_keys;

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
    else if (strat == RANDOM)
    {
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
            result->cpu_time += (float)(clock() - interval) / CLOCKS_PER_SEC;
            result->counters.successfull_adds += res;
            result->counters.failed_adds += !res;
        }
        else if (die < operations_mix.insert_p + operations_mix.contain_p)
        {
            interval = clock();
            res = seq_skiplist_contains(skiplist, key) != NULL;
            result->cpu_time += (float)(clock() - interval) / CLOCKS_PER_SEC;
            result->counters.successfull_contains += res;
            result->counters.failed_contains += !res;
        }
        else
        {
            interval = clock();
            res = seq_skiplist_remove(skiplist, key, NULL);
            result->cpu_time += (float)(clock() - interval) / CLOCKS_PER_SEC;
            result->counters.successfull_removes += res;
            result->counters.failed_removes += !res;
        }
    }

    if (strat == UNIQUE)
        unique_keys_destroy(unique_keys);
    seq_skiplist_destroy(skiplist);
    return result;
}



void *skiplist_init(uint8_t levels, double prob, keyrange_t keyrange, implementation imp)
{
    switch (imp)
    {
    case COARSE:
        return (void *)coarse_skiplist_init(levels, prob, keyrange);
        break;

    case FINE:
        return (void *)fine_skiplist_init(levels, prob, keyrange);

    case LOCK_FREE:
        return (void *)lock_free_skiplist_init(levels, prob, my_cmp);
        break;

    default:
        return NULL;
        break;
    }
}
bool skiplist_add(void *skiplist, int key, void *data, implementation imp, unsigned short int r_state[3])
{
    switch (imp)
    {
    case COARSE:
        return coarse_skiplist_add((coarse_list *)skiplist, key, data, r_state);
        break;

    case FINE:
        return fine_skiplist_add((fine_list*)skiplist, key, data, r_state);
        break;


    case LOCK_FREE:
        ;
        struct my_node* node = (struct my_node*)malloc(sizeof(struct my_node));
        node->key = key;
        node->value = data;
        lock_free_skiplist_init_node(&node->snode);
        int result = lock_free_skiplist_insert((skiplist_raw*)skiplist, &node->snode, r_state);
        if (result < 0) {
            free(node);
            return false;
        } else return true;
        break;

    default:
        return false;
        break;
    }
}
bool skiplist_contains(void *skiplist, int key, implementation imp)
{
    switch (imp)
    {
    case COARSE:
        return coarse_skiplist_contains((coarse_list *)skiplist, key) != NULL;
        break;

    case FINE:
        return fine_skiplist_contains((fine_list*)skiplist, key) != NULL;
        break;

    case LOCK_FREE:
        ;
        struct my_node* node = (struct my_node*)malloc(sizeof(struct my_node));
        node->key = key;
        lock_free_skiplist_init_node(&node->snode);
        skiplist_node* found = lock_free_skiplist_find((skiplist_raw*)skiplist, &node->snode);
        bool result = false;
        if (found != NULL) {
            result = true;
            lock_free_skiplist_release_node(found);
        }
        free(node);
        return result;
        break;

    default:
        return NULL;
        break;
    }
}
bool skiplist_remove(void *skiplist, int key, implementation imp)
{
    switch (imp)
    {
    case COARSE:
        return coarse_skiplist_remove((coarse_list *)skiplist, key, NULL);
        break;

    case FINE:
        return fine_skiplist_remove((fine_list*)skiplist, key, NULL);
        break;

    case LOCK_FREE:
        ;
        struct my_node* node = (struct my_node*)malloc(sizeof(struct my_node));
        node->key = key;
        lock_free_skiplist_init_node(&node->snode);
        bool result = (lock_free_skiplist_erase((skiplist_raw*)skiplist, &node->snode) == 0);
        free(node);
        return result;
        break;

    default:
        return false;
        break;
    }
}
void skiplist_destroy(void *skiplist, implementation imp)
{
    switch (imp)
    {
    case COARSE:
        coarse_skiplist_destroy((coarse_list *)skiplist);
        break;

    case FINE:
        fine_skiplist_destroy((fine_list*)skiplist);
        break;

    case LOCK_FREE:
        lock_free_skiplist_destroy((skiplist_raw*)skiplist);

    default:
        break;
    }
}
struct timespec;

bool time1_bigger(struct timespec* time1, struct timespec* time2) {
    bool temp1 = (time1->tv_sec > time2->tv_sec);
    bool temp2 = (time1->tv_sec == time2->tv_sec);
    bool temp3 = (time1->tv_nsec > time2->tv_nsec);
    return temp1 || (temp2 && temp3);
}

uint64_t time_diff(struct timespec* start, struct timespec* finish) {
    uint64_t sec = (finish->tv_sec - start->tv_sec) * 1e9;
    uint64_t nsec = finish->tv_nsec - start->tv_nsec;
    return sec + nsec;
}

struct bench_result *parallel_skiplist_benchmark(uint16_t num_threads, uint16_t time_interval, uint16_t n_prefill,
                                                 operations_mix_t operations_mix, selection_strategy strat, key_overlap overlap,
                                                 unsigned int r_seed, keyrange_t keyrange, uint8_t levels, double prob, implementation imp)
{

#ifdef DEBUG
    printf("Executing benchmark of %s with %u threads\n", implementation_strings[imp], num_threads);
    printf("Parameters\n");
    printf("> Time interval for measurment: %u\n", time_interval);
    printf("> Number of prefilled items: %u\n", n_prefill);
    printf("> Operations mix:\n>\t>Insertions: %f\n>\t>Contains: %f\n", operations_mix.insert_p, operations_mix.contain_p);
    printf("> Selection strategy: %d\n", strat);
    printf("> Key overlap option: %d\n", overlap);
    printf("> Random seed: %u\n", r_seed);
    printf("> Keyrange:\n>\t>Min: %d\n>\t>Max: %d\n", keyrange.min, keyrange.max);
    printf("> Levels of skiplist: %d\n", levels);
    printf("> Probability for levels: %f\n", prob);
#endif

    void *skiplist = skiplist_init(levels, prob, keyrange, imp);
    if (!skiplist) return NULL;

    int range = keyrange.max - keyrange.min;

    /* initialize random state for key selection */
    unsigned short int *random_state = (unsigned short int*)malloc(6);
    if (!random_state) return NULL;
    srand48_r(r_seed + 1, (struct drand48_data *)random_state);

    /* prefill the skiplist */
    double die;

    unique_keyarray_t *unique_keys;

    /* Prefill list */
    if (strat == RANDOM || strat == UNIQUE)
    {
        unique_keys = unique_keys_init(range);
        if (unique_keys == NULL)
            return NULL;
        for (size_t i = 0; i < n_prefill; i++)
        {
            skiplist_add(skiplist,
                         keyrange.min + unique_keys_next(unique_keys, (struct drand48_data *)random_state), NULL, imp, random_state);
        }
        unique_keys_destroy(unique_keys);
    }
    else
    {
        for (int i = 0; i < n_prefill; i++)
        {
            skiplist_add(skiplist, keyrange.min + i + 1, NULL, imp, random_state);
        }
    }
    free(random_state);

    int successfull_adds = 0;
    int failed_adds = 0;
    int successfull_contains = 0;
    int failed_contains = 0;
    int successfull_removes = 0;
    int failed_removes = 0;
    uint64_t thread_time_ns = 0;

#pragma omp parallel default(none) num_threads(num_threads)                         \
    shared(skiplist) \
    firstprivate(keyrange, range, strat, imp, overlap, time_interval, num_threads, r_seed, operations_mix, n_prefill) \
    private(die)                      \
    reduction(+ : successfull_adds, failed_adds, successfull_contains, failed_contains) \
    reduction(+: successfull_removes, failed_removes) \
    reduction(max: thread_time_ns)
    {
        int thread_num = omp_get_thread_num();
        /* initialize random state for thread */
        unsigned short int* thread_random = (unsigned short int*)malloc(6);
        srand48_r(r_seed + thread_num, (struct drand48_data *)thread_random);

        int thread_range;
        switch (overlap)
        {
        case DISJOINT:
            keyrange.min = 1.0*range / num_threads * thread_num + keyrange.min;
            keyrange.max = 1.0*range / num_threads * (thread_num+1) + keyrange.min - 1;
            thread_range = keyrange.max - keyrange.min;
            break;
        case COMMON:
        default:
            thread_range = range;
            break;
        }

        unique_keyarray_t* thread_keys;
        if (strat == UNIQUE) thread_keys = unique_keys_init(thread_range);

        struct timespec start, end, endtime, now;
        clock_gettime(CLOCK_REALTIME, &endtime);
        endtime.tv_sec += time_interval;
        clock_gettime(CLOCK_REALTIME, &now);
        bool res;
        int key = n_prefill;

        while (time1_bigger(&endtime, &now))
        {
            /* determine next key */
            if (strat == RANDOM)
            {
                drand48_r((struct drand48_data *)thread_random, &die);
                key = (int)(die * thread_range + keyrange.min);
            }
            else if (strat == UNIQUE)
            {
                key = unique_keys_next(thread_keys, (struct drand48_data *)thread_random);
            }
            else if (strat == SUCCESSIVE)
            {
                key++;
                if (key > keyrange.max)
                    key = keyrange.min;
            }

            /* determine next operation */
            drand48_r((struct drand48_data *)thread_random, &die);
            if (die < operations_mix.insert_p)
            {
                clock_gettime(CLOCK_REALTIME, &start);
                res = skiplist_add(skiplist, key, NULL, imp, thread_random);
                clock_gettime(CLOCK_REALTIME, &end);
                thread_time_ns += time_diff(&start, &end);
                successfull_adds += res;
                failed_adds += !res;
            }
            else if (die < operations_mix.insert_p + operations_mix.contain_p)
            {
                clock_gettime(CLOCK_REALTIME, &start);
                res = skiplist_contains(skiplist, key, imp);
                clock_gettime(CLOCK_REALTIME, &end);
                thread_time_ns += time_diff(&start, &end);
                successfull_contains += res;
                failed_contains += !res;
            }
            else
            {
                clock_gettime(CLOCK_REALTIME, &start);
                res = skiplist_remove(skiplist, key, imp);
                clock_gettime(CLOCK_REALTIME, &end);
                thread_time_ns += time_diff(&start, &end);
                successfull_removes += res;
                failed_removes += !res;
            }
            clock_gettime(CLOCK_REALTIME, &now);
        }
        free(thread_random);
    }

    struct bench_result *result = malloc(sizeof(struct bench_result));
    if (!result) return NULL;
    result->counters.successfull_adds=successfull_adds;
    result->counters.failed_adds=failed_adds;
    result->counters.successfull_contains=successfull_contains;
    result->counters.failed_contains=failed_contains;
    result->counters.successfull_removes=successfull_removes;
    result->counters.failed_removes=failed_removes;
    result->cpu_time = 1.0*thread_time_ns/1e9;

    skiplist_destroy(skiplist, imp);
    return result;
}


int main(void)
{
    uint16_t num_threads = 4;
    uint16_t time_interval = 5;
    uint16_t n_prefill = 10000;
    operations_mix_t operations_mix = {0.1, 0.8};
    keyrange_t keyrange = {0, 100000};
    uint8_t levels = 4;
    double prob = 0.5;
    selection_strategy strat = UNIQUE;
    key_overlap overlap = COMMON;
    implementation imp = FINE;

    struct bench_result* result = parallel_skiplist_benchmark(num_threads, time_interval, n_prefill, operations_mix,
        strat, overlap, 12345, keyrange, levels, prob, imp);

    if (!result) {
        fprintf(stderr, "Error: Benchmark failed.\n");
        return EXIT_FAILURE;
    }

    float total_ops = result->counters.successfull_adds + result->counters.failed_adds +
                      result->counters.successfull_contains + result->counters.failed_contains +
                      result->counters.successfull_removes + result->counters.failed_removes;

    printf("Total CPU time: %.2f seconds\n", result->cpu_time);
    printf("Total Operations: %.0f\n", total_ops);
    printf("Insertions: %d successful / %d attempted\n",
        result->counters.successfull_adds, result->counters.successfull_adds + result->counters.failed_adds);
    printf("Deletions: %d successful / %d attempted\n",
        result->counters.successfull_removes, result->counters.successfull_removes + result->counters.failed_removes);
    printf("Contains: %d successful / %d attempted\n",
        result->counters.successfull_contains, result->counters.successfull_contains + result->counters.failed_contains);
    printf("Throughput: %.3e ops/sec\n", total_ops / result->cpu_time);

    free(result);
    return 0;
}