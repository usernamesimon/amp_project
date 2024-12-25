#include "../inc/coarse_skiplist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <omp.h>

coarse_list* coarse_skiplist_init(uint8_t levels, double prob, keyrange_t keyrange, long int random_seed) {
    coarse_list* skiplist = (coarse_list*)malloc(sizeof(coarse_list));
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

void coarse_skiplist_destroy(coarse_list* list) {
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
bool find_predecessors(coarse_list* list, int key, node** preds) {
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

node* coarse_skiplist_contains(coarse_list* list, int key) {
    node** preds = (node**)malloc(sizeof(node*) * list->levels);
    if (!preds) return NULL;

    node* result = NULL;
    if (find_predecessors(list, key, preds)) {
        result = preds[0]->next[0];
    }
    free(preds);
    return result;
}

bool coarse_skiplist_add(coarse_list* list, int key, void* data) {
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

bool coarse_skiplist_remove(coarse_list* list, int key, void** data_out) {
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

/* Benchmark Function */
struct bench_result* coarse_skiplist_benchmark(uint16_t num_threads, uint16_t time_interval, uint16_t n_prefill, 
  operations_mix_t operations_mix, selection_strategy strat, key_overlap overlap,
  unsigned int r_seed, keyrange_t keyrange, uint8_t levels, double prob) {
    
    struct bench_result* result = malloc(sizeof(struct bench_result));
    memset(&result->counters, 0, sizeof(result->counters));
    result->time = 0.0;

    /* Thread-local counters */
    struct counters* thread_counters = calloc(num_threads, sizeof(struct counters));

    coarse_list* skiplist = coarse_skiplist_init(levels, prob, keyrange, r_seed);
    if (!skiplist) return NULL;

    /* Prefill skip list */
    for (int i = 0; i < n_prefill; i++) {
        coarse_skiplist_add(skiplist, keyrange.min + i, NULL);
    }

    clock_t start_time = clock();
    #pragma omp parallel num_threads(num_threads)
    {
        int thread_id = omp_get_thread_num();
        unsigned int thread_seed = r_seed + thread_id;
        struct drand48_data *random_state = (struct drand48_data *)malloc(6);
        srand48_r(thread_seed, random_state);

        int local_range_min, local_range_max;

        /* Determine key range based on range type */
        if (overlap == COMMON) {
            local_range_min = keyrange.min;
            local_range_max = keyrange.max;
        } else if (overlap == DISJOINT) {
            int range_per_thread = (keyrange.max - keyrange.min + 1) / num_threads;
            local_range_min = keyrange.min + thread_id * range_per_thread;
            local_range_max = local_range_min + range_per_thread - 1;
        } else if (overlap == PER_THREAD) {
            local_range_min = thread_id * 1000;
            local_range_max = local_range_min + 999;
        }

        while ((clock() - start_time) / CLOCKS_PER_SEC < time_interval) {
            int key;
            if (strat == RANDOM) {
                double op;
                drand48_r(random_state, &op);
                key = local_range_min + (int)(op * (local_range_max - local_range_min + 1));
            } else if (strat == UNIQUE) {
                static int counter = 0;
                key = local_range_min + (counter++ % (local_range_max - local_range_min + 1));
            } else if (strat == SUCCESIVE) {
                key = local_range_min++;
                if (key > local_range_max) key = local_range_min;
            }

            double op;
            drand48_r(random_state, &op);

            if (op < operations_mix.insert_p) {
                if (coarse_skiplist_add(skiplist, key, NULL)) {
                    thread_counters[thread_id].successful_adds++;
                } else {
                    thread_counters[thread_id].failed_adds++;
                }
            } else if (op < operations_mix.insert_p + operations_mix.contain_p) {
                if (coarse_skiplist_contains(skiplist, key)) {
                    thread_counters[thread_id].succesfull_contains++;
                } else {
                    thread_counters[thread_id].failed_contains++;
                }
            } else {
                if (coarse_skiplist_remove(skiplist, key, NULL)) {
                    thread_counters[thread_id].succesfull_removes++;
                } else {
                    thread_counters[thread_id].failed_removes++;
                }
            }
        }
    }
    result->time += (double)(clock() - start_time) / CLOCKS_PER_SEC;
    coarse_skiplist_destroy(skiplist);

    
    /* Report Operations Per Thread */
    for (int i = 0; i < num_threads; i++) {
        int total_thread_ops = thread_counters[i].successful_adds + thread_counters[i].failed_adds +
                               thread_counters[i].succesfull_contains + thread_counters[i].failed_contains +
                               thread_counters[i].succesfull_removes + thread_counters[i].failed_removes;
        printf("Thread %d: %d operations (Adds: %d successful / %d attempted, Deletes: %d successful / %d attempted, Contains: %d successful / %d attempted)\n",
            i,
            total_thread_ops,
            thread_counters[i].successful_adds, thread_counters[i].successful_adds + thread_counters[i].failed_adds,
            thread_counters[i].succesfull_removes, thread_counters[i].succesfull_removes + thread_counters[i].failed_removes,
            thread_counters[i].succesfull_contains, thread_counters[i].succesfull_contains + thread_counters[i].failed_contains);
    }
/* Aggregate thread-local counters into global counters */
    for (int i = 0; i < num_threads; i++) {
        result->counters.successful_adds += thread_counters[i].successful_adds;
        result->counters.failed_adds += thread_counters[i].failed_adds;
        result->counters.succesfull_contains += thread_counters[i].succesfull_contains;
        result->counters.failed_contains += thread_counters[i].failed_contains;
        result->counters.succesfull_removes += thread_counters[i].succesfull_removes;
        result->counters.failed_removes += thread_counters[i].failed_removes;
    }

    free(thread_counters);
    return result;
}
/* Main Function */
int main(int argc, char const* argv[]) {
    if (argc < 13) {
        fprintf(stderr, "Usage: %s <time_interval> <n_prefill> <insert_p> <delete_p> <contain_p> <key_min> <key_max> <levels> <prob> <num_threads> <repetitions> <range_type>\n", argv[0]);
        return EXIT_FAILURE;
    }

    uint16_t time_interval = atoi(argv[1]);
    uint16_t n_prefill = atoi(argv[2]);
    operations_mix_t operations_mix = {atof(argv[3]), atof(argv[4]), atof(argv[5])};
    keyrange_t keyrange = {atoi(argv[6]), atoi(argv[7])};
    uint8_t levels = atoi(argv[8]);
    double prob = atof(argv[9]);
    int num_threads = atoi(argv[10]);
    int repetitions = atoi(argv[11]);
    key_overlap range_type;

    if (strcmp(argv[12], "common") == 0) {
        range_type = COMMON;
    } else if (strcmp(argv[12], "disjoint") == 0) {
        range_type = DISJOINT;
    } else if (strcmp(argv[12], "per_thread") == 0) {
        range_type = PER_THREAD;
    } else {
        fprintf(stderr, "Error: Invalid range_type. Use 'common', 'disjoint', or 'per_thread'.\n");
        return EXIT_FAILURE;
    }

    struct bench_result* result = coarse_skiplist_benchmark(num_threads, time_interval, n_prefill, operations_mix,
        RANDOM, range_type, 12345, keyrange, levels, prob);

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
    printf("Throughput: %.2f ops/sec\n", total_ops / result->time);

    free(result);
    return 0;
}

