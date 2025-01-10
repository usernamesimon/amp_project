#include "../inc/lock_free_skiplist.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>

// Define a node that contains key and value pair.
struct my_node {
    // Metadata for skiplist node.
    skiplist_node snode;
    // My data here: {int, int} pair.
    int key;
    int value;
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

struct bench_result {
    float time;
    int successful_adds;
    int failed_adds;
    int successful_contains;
    int failed_contains;
    int successful_removes;
    int failed_removes;
};

struct thread_data {
    skiplist_raw* list;
    uint16_t time_interval;
    float insert_p;
    float delete_p;
    float contain_p;
    int key_min;
    int key_max;
    struct bench_result* result;
    pthread_mutex_t* result_lock;
};

void* benchmark_thread(void* arg) {
    struct thread_data* data = (struct thread_data*)arg;
    int range = data->key_max - data->key_min + 1;
    clock_t start_time = clock();
    clock_t end_time = start_time + data->time_interval * CLOCKS_PER_SEC;

    while (clock() < end_time) {
        struct my_node* node = (struct my_node*)malloc(sizeof(struct my_node));
        node->key = data->key_min + (rand() % range);
        node->value = rand() % 1000;
        lock_free_skiplist_init_node(&node->snode);

        double operation = (double)rand() / RAND_MAX;
        pthread_mutex_lock(data->result_lock);
        if (operation < data->insert_p) {
            if (lock_free_skiplist_insert(data->list, &node->snode) == 0) {
                data->result->successful_adds++;
            } else {
                data->result->failed_adds++;
                free(node);
            }
        } else if (operation < data->insert_p + data->contain_p) {
            skiplist_node* found = lock_free_skiplist_find(data->list, &node->snode);
            if (found) {
                data->result->successful_contains++;
                lock_free_skiplist_release_node(found);
            } else {
                data->result->failed_contains++;
            }
            free(node);
        } else {
            if (lock_free_skiplist_erase(data->list, &node->snode) == 0) {
                data->result->successful_removes++;
            } else {
                data->result->failed_removes++;
            }
            free(node);
        }
        pthread_mutex_unlock(data->result_lock);
    }

    return NULL;
}

struct bench_result benchmark(skiplist_raw* list, uint16_t time_interval, uint16_t n_prefill,
                               float insert_p, float delete_p, float contain_p,
                               int key_min, int key_max, uint8_t levels, double prob, int strategy, int num_threads) {
    struct bench_result result = {0};
    pthread_t threads[num_threads];
    struct thread_data thread_args[num_threads];
    pthread_mutex_t result_lock;
    pthread_mutex_init(&result_lock, NULL);

    // Prefill skiplist
    for (int i = 0; i < n_prefill; i++) {
        struct my_node* node = (struct my_node*)malloc(sizeof(struct my_node));
        node->key = key_min + (i % (key_max - key_min + 1));
        node->value = rand() % 1000;
        lock_free_skiplist_init_node(&node->snode);
        lock_free_skiplist_insert(list, &node->snode);
    }

    // Launch threads
    for (int i = 0; i < num_threads; i++) {
        thread_args[i].list = list;
        thread_args[i].time_interval = time_interval;
        thread_args[i].insert_p = insert_p;
        thread_args[i].delete_p = delete_p;
        thread_args[i].contain_p = contain_p;
        thread_args[i].key_min = key_min;
        thread_args[i].key_max = key_max;
        thread_args[i].result = &result;
        thread_args[i].result_lock = &result_lock;
        pthread_create(&threads[i], NULL, benchmark_thread, &thread_args[i]);
    }

    // Join threads
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    pthread_mutex_destroy(&result_lock);

    result.time = (float)time_interval;
    return result;
}

int main(int argc, char const* argv[]) {
    if (argc < 12) {
        fprintf(stderr, "Usage: %s <time_interval> <n_prefill> <insert_p> <delete_p> <contain_p> <key_min> <key_max> <levels> <prob> <strategy> <num_threads>\n", argv[0]);
        return EXIT_FAILURE;
    }

    uint16_t time_interval = atoi(argv[1]);
    uint16_t n_prefill = atoi(argv[2]);
    float insert_p = atof(argv[3]);
    float delete_p = atof(argv[4]);
    float contain_p = atof(argv[5]);
    int key_min = atoi(argv[6]);
    int key_max = atoi(argv[7]);
    uint8_t levels = atoi(argv[8]);
    double prob = atof(argv[9]);
    int strategy = atoi(argv[10]);
    int num_threads = atoi(argv[11]);

    if (insert_p + delete_p + contain_p > 1.0f) {
        fprintf(stderr, "Error: The sum of insert_p, delete_p, and contain_p must not exceed 1.0\n");
        return EXIT_FAILURE;
    }

    skiplist_raw list;
    lock_free_skiplist_init(&list, my_cmp);

    struct bench_result result = benchmark(&list, time_interval, n_prefill, insert_p, delete_p, contain_p,
                                           key_min, key_max, levels, prob, strategy, num_threads);

    printf("Benchmark Results:\n");
    printf("  Time: %.2f seconds\n", result.time);
    printf("  Successful Adds: %d\n", result.successful_adds);
    printf("  Failed Adds: %d\n", result.failed_adds);
    printf("  Successful Contains: %d\n", result.successful_contains);
    printf("  Failed Contains: %d\n", result.failed_contains);
    printf("  Successful Removes: %d\n", result.successful_removes);
    printf("  Failed Removes: %d\n", result.failed_removes);

    lock_free_skiplist_destroy(&list);
    return 0;
}
