#include "../inc/lock_free_skiplist.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <sched.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#define YIELD() sched_yield()
#define MEMORY_ORDER_RELAXED __ATOMIC_RELAXED
#define ATOMIC_GET(var) (var)
#define ATOMIC_LOAD(var, val) __atomic_load(&(var), &(val), MEMORY_ORDER_RELAXED)
#define ATOMIC_STORE(var, val) __atomic_store(&(var), &(val), MEMORY_ORDER_RELAXED)
#define ATOMIC_COMPARE_AND_SWAP(var, exp, val) \
    __atomic_compare_exchange(&(var), &(exp), &(val), 1, MEMORY_ORDER_RELAXED, MEMORY_ORDER_RELAXED)
#define ATOMIC_FETCH_ADD(var, val) __atomic_fetch_add(&(var), (val), MEMORY_ORDER_RELAXED)
#define ATOMIC_FETCH_SUB(var, val) __atomic_fetch_sub(&(var), (val), MEMORY_ORDER_RELAXED)
#define ALLOCATE_MEMORY(type, var, count) \
    (var) = (type *)calloc(count, sizeof(type))
#define FREE_MEMORY(var) free(var)

// Initialize a skiplist node with the specified top layer
static inline void skiplist_init_internal(skiplist_node *node, size_t top_layer)
{
    // Ensure top_layer does not exceed the maximum value for uint8_t
    if (top_layer > UINT8_MAX)
    {
        top_layer = UINT8_MAX;
    }

    // Initialize node state to false
    bool initial_state = false;
    ATOMIC_STORE(node->is_fully_linked, initial_state);
    ATOMIC_STORE(node->being_modified, initial_state);
    ATOMIC_STORE(node->removed, initial_state);

    // Update node's top_layer and allocate memory for next pointers if needed
    if (node->top_layer != top_layer || node->next == NULL)
    {
        node->top_layer = top_layer;

        // Free existing memory if next pointers are already allocated
        if (node->next)
        {
            FREE_MEMORY(node->next);
        }

        // Allocate memory for next pointers
        ALLOCATE_MEMORY(atm_node_ptr, node->next, top_layer + 1);
    }
}

// Initialize a skiplist with the specified comparison function
void lock_free_skiplist_init(skiplist_raw *slist, skiplist_cmp_t *cmp_func)
{
    // Initialize comparison function and auxiliary data to NULL
    slist->cmp_func = NULL;
    slist->aux = NULL;

    // Set default branching_factor and maximum levels values
    slist->branching_factor = 3;
    slist->max_levels = 16;
    slist->total_nodes = 0;

    // Allocate memory for layer entries
    ALLOCATE_MEMORY(atm_uint32_t, slist->layer_entries, slist->max_levels);
    slist->top_layer = 0;

    // Initialize head and tail nodes
    lock_free_skiplist_init_node(&slist->head);
    lock_free_skiplist_init_node(&slist->tail);

    // Initialize head and tail nodes with maximum layer
    skiplist_init_internal(&slist->head, slist->max_levels);
    skiplist_init_internal(&slist->tail, slist->max_levels);

    // Link head to tail for each layer
    for (size_t layer = 0; layer < slist->max_levels; ++layer)
    {
        slist->head.next[layer] = &slist->tail;
        slist->tail.next[layer] = NULL;
    }

    // Mark head and tail as fully linked
    bool fully_linked = true;
    ATOMIC_STORE(slist->head.is_fully_linked, fully_linked);
    ATOMIC_STORE(slist->tail.is_fully_linked, fully_linked);

    // Set the comparison function
    slist->cmp_func = cmp_func;
}

void lock_free_skiplist_destroy(skiplist_raw *slist)
{
    //Destroy head and tail nodes
    lock_free_skiplist_destroy_node(&slist->head);
    lock_free_skiplist_destroy_node(&slist->tail);
    
    FREE_MEMORY(slist->layer_entries);
    slist->layer_entries = NULL;

    slist->aux = NULL;
    slist->cmp_func = NULL;
}

// Initialize a skiplist node to its default state
void lock_free_skiplist_init_node(skiplist_node *node)
{
    // Set the next pointer to NULL
    node->next = NULL;

    // Initialize boolean flags to false
    bool bool_false = false;
    ATOMIC_STORE(node->is_fully_linked, bool_false);
    ATOMIC_STORE(node->being_modified, bool_false);
    ATOMIC_STORE(node->removed, bool_false);

    // Initialize other node attributes to default values
    node->accessing_next = 0;
    node->top_layer = 0;
    node->ref_count = 0;
}

void lock_free_skiplist_destroy_node(skiplist_node *node)
{
    FREE_MEMORY(node->next);
    node->next = NULL;
}

size_t skiplist_get_size(skiplist_raw *slist)
{
    uint32_t val;
    ATOMIC_LOAD(slist->total_nodes, val);
    return val;
}

/* skiplist_raw_config skiplist_get_default_config()
{
    skiplist_raw_config ret;
    ret.branching_factor = 3;
    ret.maxLayer = 16;
    ret.aux = NULL;
    return ret;
}
 */
skiplist_raw_config skiplist_get_config(skiplist_raw *slist)
{
    skiplist_raw_config ret;
    ret.branching_factor = slist->branching_factor;
    ret.maxLayer = slist->max_levels;
    ret.aux = slist->aux;
    return ret;
}

void skiplist_set_config(skiplist_raw *slist,
                         skiplist_raw_config config)
{
    slist->branching_factor = config.branching_factor;

    slist->max_levels = config.maxLayer;
    if (slist->layer_entries)
        FREE_MEMORY(slist->layer_entries);
    ALLOCATE_MEMORY(atm_uint32_t, slist->layer_entries, slist->max_levels);

    slist->aux = config.aux;
}

static inline int skiplist_compare(skiplist_raw *slist,
                                   skiplist_node *a,
                                   skiplist_node *b)
{
    if (a == b)
        return 0;
    if (a == &slist->head || b == &slist->tail)
        return -1;
    if (a == &slist->tail || b == &slist->head)
        return 1;
    return slist->cmp_func(a, b, slist->aux);
}

static inline bool skiplist_node_isvalid(skiplist_node *node)
{
    bool is_fully_linked = false;
    ATOMIC_LOAD(node->is_fully_linked, is_fully_linked);
    return is_fully_linked;
}

static inline void skiplist_read_lock(skiplist_node *node)
{
    for (;;)
    {
        // Wait for active writer to release the lock
        uint32_t accessing_next = 0;
        ATOMIC_LOAD(node->accessing_next, accessing_next);
        while (accessing_next & 0xfff00000)
        {
            YIELD();
            ATOMIC_LOAD(node->accessing_next, accessing_next);
        }

        ATOMIC_FETCH_ADD(node->accessing_next, 0x1);
        ATOMIC_LOAD(node->accessing_next, accessing_next);
        if ((accessing_next & 0xfff00000) == 0)
        {
            return;
        }

        ATOMIC_FETCH_SUB(node->accessing_next, 0x1);
    }
}

static inline void skiplist_read_unlock(skiplist_node *node)
{
    ATOMIC_FETCH_SUB(node->accessing_next, 0x1);
}

static inline void skiplist_write_lock(skiplist_node *node)
{
    for (;;)
    {
        // Wait for active writer to release the lock
        uint32_t accessing_next = 0;
        ATOMIC_LOAD(node->accessing_next, accessing_next);
        while (accessing_next & 0xfff00000)
        {
            YIELD();
            ATOMIC_LOAD(node->accessing_next, accessing_next);
        }

        ATOMIC_FETCH_ADD(node->accessing_next, 0x100000);
        ATOMIC_LOAD(node->accessing_next, accessing_next);
        if ((accessing_next & 0xfff00000) == 0x100000)
        {
            // Wait until there's no more readers
            while (accessing_next & 0x000fffff)
            {
                YIELD();
                ATOMIC_LOAD(node->accessing_next, accessing_next);
            }
            return;
        }

        ATOMIC_FETCH_SUB(node->accessing_next, 0x100000);
    }
}

static inline void skiplist_write_unlock(skiplist_node *node)
{
    ATOMIC_FETCH_SUB(node->accessing_next, 0x100000);
}

// Note: it increases the `ref_count` of returned node.
//       Caller is responsible to decrease it.
static inline skiplist_node *skiplist_next_internal(skiplist_raw *slist,
                                           skiplist_node *cur_node,
                                           int layer,
                                           skiplist_node *node_to_find,
                                           bool *found)
{
    skiplist_node *next_node = NULL;

    // Turn on `accessing_next` to ensure `cur_node` is not removable
    // and `cur_node->next` remains consistent until `accessing_next` is cleared.
    skiplist_read_lock(cur_node);
    {
        if (!skiplist_node_isvalid(cur_node))
        {
            skiplist_read_unlock(cur_node);
            return NULL;
        }

        ATOMIC_LOAD(cur_node->next[layer], next_node);

        ATOMIC_FETCH_ADD(next_node->ref_count, 1);

    }
    skiplist_read_unlock(cur_node);

    size_t num_nodes = 0;
    skiplist_node *nodes[256];

    while ((next_node && !skiplist_node_isvalid(next_node)) || next_node == node_to_find)
    {
        if (found && node_to_find == next_node)
        {
            *found = true;
        }

        skiplist_node *temp = next_node;
        skiplist_read_lock(temp);
        {
            if (!skiplist_node_isvalid(temp))
            {
                skiplist_read_unlock(temp);
                ATOMIC_FETCH_SUB(temp->ref_count, 1);
                next_node = NULL;
                break;
            }

            ATOMIC_LOAD(temp->next[layer], next_node);
            ATOMIC_FETCH_ADD(next_node->ref_count, 1);
            nodes[num_nodes++] = temp;

        }
        skiplist_read_unlock(temp);
    }

    for (size_t ii = 0; ii < num_nodes; ++ii)
    {
        ATOMIC_FETCH_SUB(nodes[ii]->ref_count, 1);
    }

    return next_node;
}

static inline size_t skiplist_determine_top_layer(skiplist_raw *slist)
{
    size_t layer = 0;
    while (layer + 1 < slist->max_levels)
    {
        // coin filp
        if (rand() % slist->branching_factor == 0)
        {
            // grow: 1/branching_factor probability
            layer++;
        }
        else
        {
            // stop: 1 - 1/branching_factor probability
            break;
        }
    }
    return layer;
}

static inline void skiplist_reset_flags(skiplist_node **node_arr,
                                        int start_layer,
                                        int top_layer)
{
    int layer;
    for (layer = start_layer; layer <= top_layer; ++layer)
    {
        if (layer == top_layer ||
            node_arr[layer] != node_arr[layer + 1])
        {

            bool exp = true;
            bool bool_false = false;
            if (!ATOMIC_COMPARE_AND_SWAP(node_arr[layer]->being_modified, exp, bool_false))
            {
              // Print an error message if the compare-and-swap operation fails
    fprintf(stderr, "Error: Failed to set being_modified flag for node at layer %d\n", layer);
            }
        }
    }
}

static inline bool skiplist_are_adjacent_nodes_valid(skiplist_node *prev,
                                                     skiplist_node *next)
{
    return skiplist_node_isvalid(prev) && skiplist_node_isvalid(next);
}

static inline void initialize_node(skiplist_node *node, int top_layer)
{
    skiplist_init_internal(node, top_layer);
    skiplist_write_lock(node);
}

static inline void finalize_insertion(skiplist_raw *slist, skiplist_node *node, int top_layer, skiplist_node **prevs, int tid_hash)
{
    bool bool_true = true;

    for (int layer = 0; layer <= top_layer; ++layer)
    {
        skiplist_write_lock(prevs[layer]);
        skiplist_node *exp = node->next[layer];
        if (!ATOMIC_COMPARE_AND_SWAP(prevs[layer]->next[layer], exp, node))
        {
    fprintf(stderr, "%02x ASSERT ins %p[%d] -> %p (expected %p)\n",
            (int)tid_hash, (void*)prevs[layer], layer,
            (void*)ATOMIC_GET(prevs[layer]->next[layer]), (void*)node->next[layer]);
        }

        skiplist_write_unlock(prevs[layer]);
    }

    ATOMIC_STORE(node->is_fully_linked, bool_true);
    skiplist_write_unlock(node);



    ATOMIC_FETCH_ADD(slist->total_nodes, 1);
    ATOMIC_FETCH_ADD(slist->layer_entries[node->top_layer], 1);
    for (int ii = slist->max_levels - 1; ii >= 0; --ii)
    {
        if (slist->layer_entries[ii] > 0)
        {
            slist->top_layer = ii;
            break;
        }
    }

    skiplist_reset_flags(prevs, 0, top_layer);
}

static inline bool lock_prev_node(skiplist_node *prev_node, bool *expected, bool bool_true)
{
    return ATOMIC_COMPARE_AND_SWAP(prev_node->being_modified, *expected, bool_true);
}

static inline void reset_flags_and_retry(skiplist_node **prevs, int locked_layer, int top_layer, skiplist_node *cur_node)
{
    skiplist_reset_flags(prevs, locked_layer, top_layer);
    ATOMIC_FETCH_SUB(cur_node->ref_count, 1);
    YIELD();
}

static inline int handle_insertion(skiplist_raw *slist, skiplist_node *node, bool no_dup, int top_layer, int tid_hash)
{
    skiplist_node *prevs[SKIPLIST_max_levels];
    skiplist_node *nexts[SKIPLIST_max_levels];

    int comparison_result = 0, current_level = 0;
    skiplist_node *cur_node = &slist->head;
    ATOMIC_FETCH_ADD(cur_node->ref_count, 1);

    int sl_top_layer = slist->top_layer;
    if (top_layer > sl_top_layer)
        sl_top_layer = top_layer;

    for (current_level = sl_top_layer; current_level >= 0; --current_level)
    {
        do
        {


            skiplist_node *next_node = skiplist_next_internal(slist, cur_node, current_level, NULL, NULL);
            if (!next_node)
            {
                skiplist_reset_flags(prevs, current_level + 1, top_layer);
                ATOMIC_FETCH_SUB(cur_node->ref_count, 1);
                YIELD();
                return -1;
            }

            comparison_result = skiplist_compare(slist, node, next_node);
            if (comparison_result > 0)
            {
                // cur_node < next_node < node => move to next node
                skiplist_node *temp = cur_node;
                cur_node = next_node;
                ATOMIC_FETCH_SUB(temp->ref_count, 1);
                continue;
            }
            else
            {
                // otherwise: cur_node < node <= next_node
                ATOMIC_FETCH_SUB(next_node->ref_count, 1);
            }

            if (no_dup && comparison_result == 0)
            {
                // Duplicate key is not allowed
                skiplist_reset_flags(prevs, current_level + 1, top_layer);
                ATOMIC_FETCH_SUB(cur_node->ref_count, 1);
                return -1;
            }

            if (current_level <= top_layer)
            {
                prevs[current_level] = cur_node;
                nexts[current_level] = next_node;

                int error_code = 0;
                int locked_layer = current_level + 1;

                // Check if prev node is duplicated with upper layer
                if (current_level < top_layer && prevs[current_level] == prevs[current_level + 1])
                {
                    // Duplicate, do nothing
                }
                else
                {
                    bool expected = false;
                    if (lock_prev_node(prevs[current_level], &expected, true))
                    {
                        locked_layer = current_level;
                    }
                    else
                    {
                        error_code = -1;
                    }
                }

                if (error_code == 0 && !skiplist_are_adjacent_nodes_valid(prevs[current_level], nexts[current_level]))
                {
                    error_code = -2;
                }

                if (error_code != 0)
                {

                    reset_flags_and_retry(prevs, locked_layer, top_layer, cur_node);
                    return -1;
                }

                // Set current node's pointers
                ATOMIC_STORE(node->next[current_level], nexts[current_level]);

                // Check if `cur_node->next` has been changed from `next_node`
                skiplist_node *next_node_again = skiplist_next_internal(slist, cur_node, current_level, NULL, NULL);
                ATOMIC_FETCH_SUB(next_node_again->ref_count, 1);
                if (next_node_again != next_node)
                {

                    reset_flags_and_retry(prevs, current_level, top_layer, cur_node);
                    return -1;
                }
            }

            if (current_level)
            {
                // Non-bottom layer => go down
                break;
            }

            // Bottom layer => insertion succeeded
            finalize_insertion(slist, node, top_layer, prevs, tid_hash);
            ATOMIC_FETCH_SUB(cur_node->ref_count, 1);

            return 0;
        } while (cur_node != &slist->tail);
    }
    return 0;
}

static inline int skiplist_add(skiplist_raw *slist, skiplist_node *node, bool no_dup)
{
    pthread_t tid = pthread_self();
    size_t tid_hash = ((size_t)tid) % 256;

    int top_layer = skiplist_determine_top_layer(slist);

    // Initialize node before insertion
    initialize_node(node, top_layer);


    while (true)
    {
        int result = handle_insertion(slist, node, no_dup, top_layer, tid_hash);
        if (result != -1)
        {
            return result;
        }
        // Retry insertion if result is -1
    }
}

int lock_free_skiplist_insert(skiplist_raw *slist,
                    skiplist_node *node)
{
    return skiplist_add(slist, node, false);
}

int skiplist_insert_unique(skiplist_raw *slist,
                           skiplist_node *node)
{
    return skiplist_add(slist, node, true);
}

typedef enum
{
    LESS_THAN = -2,
    LESS_THAN_OR_EQUAL = -1,
    EQUAL = 0,
    GREATER_THAN_OR_EQUAL = 1,
    GREATER_THAN = 2
} skiplist_find_mode;

// Note: it increases the `ref_count` of returned node.
//       Caller is responsible to decrease it.
static inline skiplist_node *skiplist_find_node(skiplist_raw *slist,
                                                skiplist_node *query,
                                                skiplist_find_mode mode)
{

find_retry:
    (void)mode;
    int comparison_result = 0;
    int current_level = 0;
    skiplist_node *cur_node = &slist->head;
    ATOMIC_FETCH_ADD(cur_node->ref_count, 1);



    uint8_t sl_top_layer = slist->top_layer;
    for (current_level = sl_top_layer; current_level >= 0; --current_level)
    {
        do
        {


            skiplist_node *next_node = skiplist_next_internal(slist, cur_node, current_level,
                                                     NULL, NULL);
            if (!next_node)
            {
                ATOMIC_FETCH_SUB(cur_node->ref_count, 1);
                YIELD();
                goto find_retry;
            }
            comparison_result = skiplist_compare(slist, query, next_node);
            if (comparison_result > 0)
            {
                // cur_node < next_node < query
                // => move to next node
                skiplist_node *temp = cur_node;
                cur_node = next_node;
                ATOMIC_FETCH_SUB(temp->ref_count, 1);
                continue;
            }
            else if (-1 <= mode && mode <= 1 && comparison_result == 0)
            {
                // cur_node < query == next_node .. return
                ATOMIC_FETCH_SUB(cur_node->ref_count, 1);
                return next_node;
            }

            // otherwise: cur_node < query < next_node
            if (current_level)
            {
                // non-bottom layer => go down
                ATOMIC_FETCH_SUB(next_node->ref_count, 1);
                break;
            }

            // bottom layer
            if (mode < 0 && cur_node != &slist->head)
            {
                // smaller mode
                ATOMIC_FETCH_SUB(next_node->ref_count, 1);
                return cur_node;
            }
            else if (mode > 0 && next_node != &slist->tail)
            {
                // greater mode
                ATOMIC_FETCH_SUB(cur_node->ref_count, 1);
                return next_node;
            }
            // otherwise: exact match mode OR not found
            ATOMIC_FETCH_SUB(cur_node->ref_count, 1);
            ATOMIC_FETCH_SUB(next_node->ref_count, 1);
            return NULL;
        } while (cur_node != &slist->tail);
    }

    return NULL;
}

skiplist_node *lock_free_skiplist_find(skiplist_raw *slist,
                             skiplist_node *query)
{
    return skiplist_find_node(slist, query, EQUAL);
}

skiplist_node *skiplist_find_smaller_or_equal(skiplist_raw *slist,
                                              skiplist_node *query)
{
    return skiplist_find_node(slist, query, LESS_THAN_OR_EQUAL);
}

skiplist_node *skiplist_find_greater_or_equal(skiplist_raw *slist,
                                              skiplist_node *query)
{
    return skiplist_find_node(slist, query, GREATER_THAN_OR_EQUAL);
}

int skiplist_erase_node_passive(skiplist_raw *slist,
                                skiplist_node *node)
{

    int top_layer = node->top_layer;
    bool bool_true = true, bool_false = false;
    bool removed = false;
    bool is_fully_linked = false;

    ATOMIC_LOAD(node->removed, removed);
    if (removed)
    {
        // already removed
        return -1;
    }

    skiplist_node *prevs[SKIPLIST_max_levels];
    skiplist_node *nexts[SKIPLIST_max_levels];

    bool expected = false;
    if (!ATOMIC_COMPARE_AND_SWAP(node->being_modified, expected, bool_true))
    {
        // already being modified .. cannot work on this node for now.

        return -2;
    }

    // set removed flag first, so that reader cannot read this node.
    ATOMIC_STORE(node->removed, bool_true);


erase_node_retry:
    ATOMIC_LOAD(node->is_fully_linked, is_fully_linked);
    if (!is_fully_linked)
    {
        // already unlinked .. remove is done by other thread
        ATOMIC_STORE(node->removed, bool_false);
        ATOMIC_STORE(node->being_modified, bool_false);
        return -3;
    }

    int comparison_result = 0;
    bool found_node_to_erase = false;
    (void)found_node_to_erase;
    skiplist_node *cur_node = &slist->head;
    ATOMIC_FETCH_ADD(cur_node->ref_count, 1);



    int current_level = slist->top_layer;
    for (; current_level >= 0; --current_level)
    {
        do
        {

            bool node_found = false;
            skiplist_node *next_node = skiplist_next_internal(slist, cur_node, current_level,
                                                     node, &node_found);
            if (!next_node)
            {
                skiplist_reset_flags(prevs, current_level + 1, top_layer);
                ATOMIC_FETCH_SUB(cur_node->ref_count, 1);
                YIELD();
                goto erase_node_retry;
            }

            // Note: unlike insert(), we should find exact position of `node`.
            comparison_result = skiplist_compare(slist, node, next_node);
            if (comparison_result > 0 || (current_level <= top_layer && !node_found))
            {
                // cur_node <= next_node < node
                // => move to next node
                skiplist_node *temp = cur_node;
                cur_node = next_node;
                if (comparison_result > 0) {
                    int cmp2 = skiplist_compare(slist, cur_node, node);
                    if (cmp2 > 0) {
                        // node < cur_node <= next_node: not found.
                        skiplist_reset_flags(prevs, current_level + 1, top_layer);
                        ATOMIC_FETCH_SUB(temp->ref_count, 1);
                        ATOMIC_FETCH_SUB(next_node->ref_count, 1);

                    }
                }
                ATOMIC_FETCH_SUB(temp->ref_count, 1);
                continue;
            }
            else
            {
                // otherwise: cur_node <= node <= next_node
                ATOMIC_FETCH_SUB(next_node->ref_count, 1);
            }

            if (current_level <= top_layer)
            {
                prevs[current_level] = cur_node;
                // note: 'next_node' and 'node' should not be the same, as 'removed' flag is already set.     
                nexts[current_level] = next_node;

                // check if prev node duplicates with upper layer
                int error_code = 0;
                int locked_layer = current_level + 1;
                if (current_level < top_layer &&
                    prevs[current_level] == prevs[current_level + 1])
                {
                    // Duplicate with the upper layer
                    // This means that the 'being_modified' flag is already true
                    // No action needed
                }
                else
                {
                    expected = false;
                    if (ATOMIC_COMPARE_AND_SWAP(prevs[current_level]->being_modified,
                                                expected, bool_true))
                    {
                        locked_layer = current_level;
                    }
                    else
                    {
                        error_code = -1;
                    }
                }

                if (error_code == 0 &&
                    !skiplist_are_adjacent_nodes_valid(prevs[current_level], nexts[current_level]))
                {
                    error_code = -2;
                }

                if (error_code != 0)
                {
                    skiplist_reset_flags(prevs, locked_layer, top_layer);
                    ATOMIC_FETCH_SUB(cur_node->ref_count, 1);
                    YIELD();
                    goto erase_node_retry;
                }

                skiplist_node *next_node_again =
                    skiplist_next_internal(slist, cur_node, current_level, node, NULL);
                ATOMIC_FETCH_SUB(next_node_again->ref_count, 1);
                if (next_node_again != nexts[current_level])
                {
                    // `next` pointer has been changed, retry.

                    skiplist_reset_flags(prevs, current_level, top_layer);
                    ATOMIC_FETCH_SUB(cur_node->ref_count, 1);
                    YIELD();
                    goto erase_node_retry;
                }
            }
            if (current_level == 0)
                found_node_to_erase = true;
            // go down
            break;
        } while (cur_node != &slist->tail);
    }
    // bottom layer => removal succeeded.

    // mark this node unlinked
    skiplist_write_lock(node);
    {
        ATOMIC_STORE(node->is_fully_linked, bool_false);
    }
    skiplist_write_unlock(node);

    // change prev nodes' next pointer from 0 ~ top_layer
    for (current_level = 0; current_level <= top_layer; ++current_level)
    {
        skiplist_write_lock(prevs[current_level]);
        skiplist_node *exp = node;

        if (!ATOMIC_COMPARE_AND_SWAP(prevs[current_level]->next[current_level],
                                     exp, nexts[current_level]))
        {
        fprintf(stderr, "Failed to update next pointer at level %d\n", current_level);
        }

        skiplist_write_unlock(prevs[current_level]);
    }



    ATOMIC_FETCH_SUB(slist->total_nodes, 1);
    ATOMIC_FETCH_SUB(slist->layer_entries[node->top_layer], 1);
    for (int ii = slist->max_levels - 1; ii >= 0; --ii)
    {
        if (slist->layer_entries[ii] > 0)
        {
            slist->top_layer = ii;
            break;
        }
    }

    // modification is done for all layers
    skiplist_reset_flags(prevs, 0, top_layer);
    ATOMIC_FETCH_SUB(cur_node->ref_count, 1);

    ATOMIC_STORE(node->being_modified, bool_false);

    return 0;
}

int skiplist_erase_node(skiplist_raw *slist,
                        skiplist_node *node)
{
    int ret = 0;
    do
    {
        ret = skiplist_erase_node_passive(slist, node);
        // if ret == -2, other thread is accessing the same node at the same time. try again.
    } while (ret == -2);
    return ret;
}

int lock_free_skiplist_erase(skiplist_raw *slist,
                   skiplist_node *query)
{
    skiplist_node *found = lock_free_skiplist_find(slist, query);
    if (!found)
    {
        // key not found
        return -4;
    }

    int ret = 0;
    do
    {
        ret = skiplist_erase_node_passive(slist, found);
        // if ret == -2, other thread is accessing the same node at the same time. try again.
    } while (ret == -2);

    ATOMIC_FETCH_SUB(found->ref_count, 1);
    return ret;
}

int skiplist_is_valid_node(skiplist_node *node)
{
    return skiplist_node_isvalid(node);
}

int skiplist_is_safe_to_free(skiplist_node *node)
{
    if (node->accessing_next)
        return 0;
    if (node->being_modified)
        return 0;
    if (!node->removed)
        return 0;

    uint16_t ref_count = 0;
    ATOMIC_LOAD(node->ref_count, ref_count);
    if (ref_count)
        return 0;
    return 1;
}

void skiplist_wait_for_free(skiplist_node *node)
{
    while (!skiplist_is_safe_to_free(node))
    {
        YIELD();
    }
}

void skiplist_grab_node(skiplist_node *node)
{
    ATOMIC_FETCH_ADD(node->ref_count, 1);
}

void lock_free_skiplist_release_node(skiplist_node *node)
{

    ATOMIC_FETCH_SUB(node->ref_count, 1);
}

skiplist_node *skiplist_next(skiplist_raw *slist,
                             skiplist_node *node)
{
// If a node in a skiplist is removed and becomes unreachable while its next node 
// is also removed and released, the link update to the removed node will not 
// propagate, leaving it pointing to the released node. As a result, calling 
// `skiplist_next(node)` may access invalid or corrupted memory. 
// To address this, the traversal should restart from the top layer to find 
// a valid link, ensuring correctness.

    skiplist_node *next = skiplist_next_internal(slist, node, 0, NULL, NULL);
    if (!next)
        next = skiplist_find_node(slist, node, GREATER_THAN);

    if (next == &slist->tail)
        return NULL;
    return next;
}

skiplist_node *skiplist_prev(skiplist_raw *slist,
                             skiplist_node *node)
{
    skiplist_node *prev = skiplist_find_node(slist, node, LESS_THAN);
    if (prev == &slist->head)
        return NULL;
    return prev;
}

skiplist_node *skiplist_begin(skiplist_raw *slist)
{
    skiplist_node *next = NULL;
    while (!next)
    {
        next = skiplist_next_internal(slist, &slist->head, 0, NULL, NULL);
    }
    if (next == &slist->tail)
        return NULL;
    return next;
}

skiplist_node *skiplist_end(skiplist_raw *slist)
{
    return skiplist_prev(slist, &slist->tail);
}
