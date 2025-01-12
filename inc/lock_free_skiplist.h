#include <stddef.h>
#include <stdint.h>

#define SKIPLIST_max_levels (32)

struct _skiplist_node;

// Define atomic types for C compatibility
typedef struct _skiplist_node*         atm_node_ptr;
typedef uint8_t                        atm_bool;
typedef uint8_t                        atm_uint8_t;
typedef uint16_t                       atm_uint16_t;
typedef uint32_t                       atm_uint32_t;

typedef struct _skiplist_node {
    atm_node_ptr *next;
    atm_bool is_fully_linked;
    atm_bool being_modified;
    atm_bool removed;
    uint8_t top_layer; // 0: bottom
    atm_uint16_t ref_count;
    atm_uint32_t accessing_next;
} skiplist_node;

// *a  < *b : return negative
// *a == *b : return 0
// *a  > *b : return positive
typedef int skiplist_cmp_t(skiplist_node *a, skiplist_node *b, void *aux);

typedef struct {
    size_t prob;
    size_t maxLayer;
    void *aux;
} skiplist_raw_config;

typedef struct {
    skiplist_node head;
    skiplist_node tail;
    skiplist_cmp_t *cmp_func;
    void *aux;
    atm_uint32_t total_nodes;
    atm_uint32_t* layer_entries;
    atm_uint8_t top_layer;
    uint8_t prob;
    uint8_t levels;
} skiplist_raw;

#ifndef _get_entry
#define _get_entry(ELEM, STRUCT, MEMBER)                              \
        ((STRUCT *) ((uint8_t *) (ELEM) - offsetof (STRUCT, MEMBER)))
#endif

skiplist_raw* lock_free_skiplist_init(uint8_t levels, uint8_t prob, skiplist_cmp_t* cmp_func);
void lock_free_skiplist_destroy(skiplist_raw* slist);

void lock_free_skiplist_init_node(skiplist_node* node);
void lock_free_skiplist_destroy_node(skiplist_node* node);

size_t skiplist_get_size(skiplist_raw* slist);

skiplist_raw_config skiplist_get_default_config();
skiplist_raw_config skiplist_get_config(skiplist_raw* slist);

void skiplist_set_config(skiplist_raw* slist,
                         skiplist_raw_config config);

int lock_free_skiplist_insert(skiplist_raw* slist,
                    skiplist_node* node,unsigned short int random_state[3]);
int skiplist_insert_unique(skiplist_raw *slist,
                          skiplist_node *node);

skiplist_node* lock_free_skiplist_find(skiplist_raw* slist,
                             skiplist_node* query);
skiplist_node* skiplist_find_smaller_or_equal(skiplist_raw* slist,
                                              skiplist_node* query);
skiplist_node* skiplist_find_greater_or_equal(skiplist_raw* slist,
                                              skiplist_node* query);

int skiplist_erase_node_passive(skiplist_raw* slist,
                                skiplist_node* node);
int skiplist_erase_node(skiplist_raw *slist,
                        skiplist_node *node);
int lock_free_skiplist_erase(skiplist_raw* slist,
                   skiplist_node* query);

int skiplist_is_valid_node(skiplist_node* node);
int skiplist_is_safe_to_free(skiplist_node* node);
void skiplist_wait_for_free(skiplist_node* node);

void skiplist_grab_node(skiplist_node* node);
void lock_free_skiplist_release_node(skiplist_node* node);

skiplist_node* skiplist_next(skiplist_raw* slist,
                             skiplist_node* node);
skiplist_node* skiplist_prev(skiplist_raw* slist,
                             skiplist_node* node);
skiplist_node* skiplist_begin(skiplist_raw* slist);
skiplist_node* skiplist_end(skiplist_raw* slist);

