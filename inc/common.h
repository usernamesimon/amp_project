/* These structs should to match the definition in benchmark.py
 */
struct counters {
    int failed_adds;
    int successful_adds;
    int failed_removes;
    int succesfull_removes;
    int failed_contains;
    int succesfull_contains;
};
struct bench_result {
    float time;
    struct counters counters;
};

typedef struct _operations_mix{
    float insert_p;
    float delete_p;
    float contain_p;
} operations_mix_t;

typedef struct _keyrange{
    int min;
    int max;
} keyrange_t;


/* Key range types for multithreaded benchmarks */
typedef enum {
  COMMON,     /* All threads share the same key range */
  DISJOINT,   /* Each thread has a distinct key range */
  PER_THREAD  /* Each thread has its own independent range */
} key_range_overlap;

typedef enum _sel_strat{RANDOM, UNIQUE, SUCCESIVE} selection_strategy;