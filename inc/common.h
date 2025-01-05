#ifndef H_COMMON
#define H_COMMON

/* These structs should to match the definition in benchmark.py
 */
struct counters {
    int failed_adds;
    int successfull_adds;
    int failed_removes;
    int successfull_removes;
    int failed_contains;
    int successfull_contains;
};
struct bench_result {
    float time;
    struct counters counters;
};

typedef struct _operations_mix{
    float insert_p;
    // delete_p is implied by 1 - (insert_p + contain_p)
    float contain_p;
} operations_mix_t;

typedef struct _keyrange{
    int min;
    int max;
} keyrange_t;


/* Key range types for multithreaded benchmarks */
typedef enum _key_overlap{
  COMMON,     /* All threads share the same key range */
  DISJOINT,   /* Each thread has a distinct key range */
} key_overlap;

typedef enum _sel_strat{RANDOM, UNIQUE, SUCCESSIVE} selection_strategy;

/* Structure to loop through a random permutation of keys when doing benchmarks. 
    Initialize the array with array[i] = i. Then swap current with a random
    index of the not yet shuffled ones and increment current and shuffled */
typedef struct _unique_keyarray{
    int* array;     /* Physical start of array */
    int* current;   /* Iterator for next key, wraps around if all keys used */
    int* shuffled;  /* Keeps track up to which point the array has been shuffled */
    int size;       /* size in number of ints */
} unique_keyarray_t;

typedef enum _implementation{SEQUENTIAL, COARSE, FINE, LOCK_FREE} implementation;

#endif