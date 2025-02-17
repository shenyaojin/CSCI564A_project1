//
// This file contains all of the implementations of the replacement_policy
// constructors from the replacement_policies.h file.
//
// It also contains stubs of all of the functions that are added to each
// replacement_policy struct at construction time.
//
// ============================================================================
// NOTE: It is recommended that you read the comments in the
// replacement_policies.h file for further context on what each function is
// for.
// ============================================================================
//
// Modified by Shenyao Jin, shenyaojin@mines.edu

#include "replacement_policies.h"
#include <stdlib.h>
#include "memory_system.h"
#include <sodium.h>

// LRU Replacement Policy
// ============================================================================
// ============================================================================

/**
 * This structure stores per-set metadata for LRU tracking.
 * - num_sets: number of sets in the cache
 * - associativity: number of lines per set
 * - order[set_idx]: an array of size `associativity` that keeps track
 *   of the “age” order of cache lines from most recently used (MRU) at index 0
 *   to least recently used (LRU) at index `associativity - 1`.
 */
struct lru_metadata {
    uint32_t num_sets;
    uint32_t associativity;
    // order[set_idx] is an array of size associativity
    // order[set_idx][i] is the index of the cache line in the set
    uint32_t **order;
};

/**
 * LRU strategy for eviction:
 * Return the index of the least recently used cache line, i.e., the line stored
 * at the tail of the order array for the given set.
 */
static uint32_t lru_eviction_index(struct replacement_policy *replacement_policy,
                                   struct cache_system *cache_system,
                                   uint32_t set_idx)
{
    struct lru_metadata *metadata = (struct lru_metadata *)replacement_policy->data;
    // Return the index of the least recently used cache line (LRU is at the tail)
    return metadata->order[set_idx][metadata->associativity - 1];
}

/**
 * Whenever a cache line is accessed, it should be moved to the MRU position (index 0).
 */
static void lru_cache_access(struct replacement_policy *replacement_policy,
                             struct cache_system *cache_system,
                             uint32_t set_idx,
                             uint32_t tag)
{
    struct lru_metadata *metadata = (struct lru_metadata *)replacement_policy->data;

    // Find the accessed line in the set
    uint32_t set_start = set_idx * cache_system->associativity;
    struct cache_line *set_lines = &cache_system->cache_lines[set_start];

    int accessed_line_idx = -1;
    for (uint32_t i = 0; i < cache_system->associativity; i++) {
        if (set_lines[i].tag == tag && set_lines[i].status != INVALID) {
            accessed_line_idx = i;
            break;
        }
    }

    // Defensive check: theoretically this should never fail if the line was accessed or inserted
    if (accessed_line_idx < 0) {
        return; // Should not happen if the logic is correct
    }

    // Find the position p of accessed_line_idx in order[set_idx]
    int p = -1;
    for (uint32_t i = 0; i < metadata->associativity; i++) {
        if (metadata->order[set_idx][i] == (uint32_t)accessed_line_idx) {
            p = i;
            break;
        }
    }
    if (p < 0) {
        // Defensive check
        return;
    }

    // Shift [0...p-1] one position toward the tail, then move the current line to index 0 (MRU).
    for (int i = p; i > 0; i--) {
        metadata->order[set_idx][i] = metadata->order[set_idx][i - 1];
    }
    metadata->order[set_idx][0] = accessed_line_idx;
}

/**
 * Cleanup function for LRU: frees all memory allocated in the metadata structure.
 */
static void lru_replacement_policy_cleanup(struct replacement_policy *replacement_policy)
{
    struct lru_metadata *metadata = (struct lru_metadata *)replacement_policy->data;
    if (!metadata) return;

    // First, free the order array for each set
    for (uint32_t i = 0; i < metadata->num_sets; i++) {
        free(metadata->order[i]);
    }
    // Then free the pointer to the order array
    free(metadata->order);
    // Finally, free the metadata structure itself
    free(metadata);
    replacement_policy->data = NULL;
}

/**
 * Constructor for the LRU replacement policy.
 * 1. Allocate an array of size `associativity` for each set to store the LRU order.
 * 2. Initialize it to [0, 1, 2, ..., associativity-1].
 * 3. Assign the function pointers in `policy`.
 */
struct replacement_policy *lru_replacement_policy_new(uint32_t sets, uint32_t associativity)
{
    struct replacement_policy *policy =
        (struct replacement_policy *)malloc(sizeof(struct replacement_policy));
    if (!policy) {
        return NULL;
    }

    // Allocate the metadata structure
    struct lru_metadata *metadata =
        (struct lru_metadata *)malloc(sizeof(struct lru_metadata));
    if (!metadata) {
        free(policy);
        return NULL;
    }

    metadata->num_sets = sets;
    metadata->associativity = associativity;

    // Allocate metadata->order[sets][associativity], then initialize
    metadata->order = (uint32_t **)malloc(sizeof(uint32_t *) * sets);
    for (uint32_t s = 0; s < sets; s++) {
        metadata->order[s] = (uint32_t *)malloc(sizeof(uint32_t) * associativity);
        // Initialize the order array to [0, 1, 2, ...].
        for (uint32_t i = 0; i < associativity; i++) {
            metadata->order[s][i] = i;
        }
    }

    // Attach metadata to policy->data
    policy->data = metadata;

    // Assign the three function pointers
    policy->eviction_index = lru_eviction_index;
    policy->cache_access   = lru_cache_access;
    policy->cleanup        = lru_replacement_policy_cleanup;

    return policy;
}

// LRU_PREFER_CLEAN Replacement Policy
// ============================================================================
/**
 * Eviction function for LRU_PREFER_CLEAN:
 *  1. From the least recently used end (the tail of the order array), find the first "clean" line
 *     (i.e., a line whose status == EXCLUSIVE) and return its index.
 *  2. If no clean line is found, evict the true LRU line at the tail.
 */
static uint32_t lru_prefer_clean_eviction_index(struct replacement_policy *replacement_policy,
                                                struct cache_system *cache_system,
                                                uint32_t set_idx)
{
    struct lru_metadata *md = (struct lru_metadata *)replacement_policy->data;
    uint32_t assoc = md->associativity;

    // order[set_idx] is sorted from MRU to LRU (index 0 to index assoc-1).
    // We iterate from the tail (LRU) backward to find the first EXCLUSIVE (clean) line.
    for (int i = assoc - 1; i >= 0; i--) {
        uint32_t line_idx_in_set = md->order[set_idx][i];
        // Convert local index to the global cache_lines index
        uint32_t global_idx = set_idx * assoc + line_idx_in_set;
        struct cache_line *cl = &cache_system->cache_lines[global_idx];
        // EXCLUSIVE is considered a “clean” line here
        if (cl->status == EXCLUSIVE) {
            return line_idx_in_set;
        }
    }

    // If no clean line is found, evict the true LRU line (array tail)
    return md->order[set_idx][assoc - 1];
}

/**
 * On access, update order identically to standard LRU:
 *   Move the accessed line to index 0 (MRU) in the order array for that set.
 */
static void lru_prefer_clean_cache_access(struct replacement_policy *replacement_policy,
                                          struct cache_system *cache_system,
                                          uint32_t set_idx,
                                          uint32_t tag)
{
    struct lru_metadata *md = (struct lru_metadata *)replacement_policy->data;

    // Find the accessed line in the set
    uint32_t set_start = set_idx * cache_system->associativity;
    struct cache_line *lines = &cache_system->cache_lines[set_start];
    int accessed_line_idx = -1;

    for (uint32_t i = 0; i < cache_system->associativity; i++) {
        if (lines[i].tag == tag && lines[i].status != INVALID) {
            accessed_line_idx = i;
            break;
        }
    }
    if (accessed_line_idx < 0) {
        // Defensive check
        return;
    }

    // Find the position p in the order array
    int p = -1;
    for (uint32_t i = 0; i < md->associativity; i++) {
        if (md->order[set_idx][i] == (uint32_t)accessed_line_idx) {
            p = i;
            break;
        }
    }
    if (p < 0) {
        // Defensive check
        return;
    }

    // Shift [0...p-1] one position toward the tail; place the accessed line at index 0 (MRU).
    for (int i = p; i > 0; i--) {
        md->order[set_idx][i] = md->order[set_idx][i - 1];
    }
    md->order[set_idx][0] = accessed_line_idx;
}

/**
 * Cleanup for LRU_PREFER_CLEAN: free the memory allocated in the metadata structure.
 */
static void lru_prefer_clean_replacement_policy_cleanup(struct replacement_policy *replacement_policy)
{
    struct lru_metadata *md = (struct lru_metadata *)replacement_policy->data;
    if (!md) return;

    for (uint32_t s = 0; s < md->num_sets; s++) {
        free(md->order[s]);
    }
    free(md->order);
    free(md);
    replacement_policy->data = NULL;
}

/**
 * Constructor for LRU_PREFER_CLEAN:
 * Identical to standard LRU except for the specialized eviction function
 * lru_prefer_clean_eviction_index.
 */
struct replacement_policy *lru_prefer_clean_replacement_policy_new(uint32_t sets,
                                                                   uint32_t associativity)
{
    struct replacement_policy *policy =
        (struct replacement_policy *)malloc(sizeof(struct replacement_policy));
    if (!policy) {
        return NULL;
    }

    struct lru_metadata *md =
        (struct lru_metadata *)malloc(sizeof(struct lru_metadata));
    if (!md) {
        free(policy);
        return NULL;
    }

    md->num_sets = sets;
    md->associativity = associativity;

    // Allocate and initialize the order array [0, 1, 2, ..., associativity - 1]
    md->order = (uint32_t **)malloc(sizeof(uint32_t *) * sets);
    for (uint32_t s = 0; s < sets; s++) {
        md->order[s] = (uint32_t *)malloc(sizeof(uint32_t) * associativity);
        for (uint32_t i = 0; i < associativity; i++) {
            md->order[s][i] = i;
        }
    }

    policy->data = md;

    // Assign the function pointers
    policy->eviction_index = lru_prefer_clean_eviction_index;
    policy->cache_access   = lru_prefer_clean_cache_access;
    policy->cleanup        = lru_prefer_clean_replacement_policy_cleanup;

    return policy;
}

// RAND Replacement Policy
// ============================================================================
// Additional comment: This simple random replacement policy selects a cache
// line to evict randomly among all lines in the set.
//
// rand_metadata here is optional. We use it to store the associativity, etc.,
// but the policy logic doesn’t require any sophisticated data structure.

struct rand_metadata {
    uint32_t num_sets;
    uint32_t associativity;
};

/**
 * Select a cache line to evict randomly among [0, associativity - 1].
 */
static uint32_t rand_eviction_index(struct replacement_policy *replacement_policy,
                                    struct cache_system *cache_system,
                                    uint32_t set_idx)
{
    (void)cache_system; // Not used for random
    (void)set_idx;      // Not used for random

    struct rand_metadata *md = (struct rand_metadata *)replacement_policy->data;
    uint32_t assoc = md->associativity;

    // arc4random_uniform
    // return arc4random_uniform(assoc); // Only work on my Mac. :(
    return randombytes_uniform(assoc);
    // return rand() % assoc;
}

/**
 * On access, do nothing for RAND policy.
 * We do not track usage order at all.
 */
static void rand_cache_access(struct replacement_policy *replacement_policy,
                              struct cache_system *cache_system,
                              uint32_t set_idx,
                              uint32_t tag)
{
    (void)replacement_policy; // Avoid unused-parameter warnings
    (void)cache_system;
    (void)set_idx;
    (void)tag;
}

/**
 * Cleanup for RAND policy: if we allocated any metadata, free it here.
 */
static void rand_replacement_policy_cleanup(struct replacement_policy *replacement_policy)
{
    if (!replacement_policy || !replacement_policy->data) {
        return;
    }
    free(replacement_policy->data);
    replacement_policy->data = NULL;
}

/**
 * Constructor for the RAND replacement policy.
 */
struct replacement_policy *rand_replacement_policy_new(uint32_t sets, uint32_t associativity)
{
    struct replacement_policy *policy =
        (struct replacement_policy *)malloc(sizeof(struct replacement_policy));
    if (!policy) {
        return NULL;
    }

    struct rand_metadata *md =
        (struct rand_metadata *)malloc(sizeof(struct rand_metadata));
    if (!md) {
        free(policy);
        return NULL;
    }

    md->num_sets = sets;
    md->associativity = associativity;

    policy->data = md;

    // (Optional) Seed the random generator. For reproducible tests, you could do srand(0) or similar.
    srand(time(NULL));

    // Assign the function pointers
    policy->eviction_index = rand_eviction_index;
    policy->cache_access   = rand_cache_access;
    policy->cleanup        = rand_replacement_policy_cleanup;

    return policy;
}