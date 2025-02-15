//
// This file defines the function signatures necessary for creating the three
// replacement policies and defines the replacement_policy struct.
//

#ifndef REPLACEMENT_POLICIES_H
#define REPLACEMENT_POLICIES_H

#include <stdint.h>
#include <stdlib.h>
#include <time.h>

struct cache_system;
#include "memory_system.h"

// This struct describes the functionality of a replacement policy. The
// function pointers describe the three functions that every replacement policy
// must implement. Arbitrary data can be stored in the data pointer and can be
// used to store the state of the replacement policy between calls to
// eviction_index and cache_access.
//
// For those of you who are unfamiliar with function pointers, they take the
// form:
//
//      return_type (*function_name)(list of arguments)
struct replacement_policy {
    // This function is called when a set is full but a new cache line needs to
    // be added to the set.
    //
    // This function should determine which index within the set to evict.
    //
    // Argruments:
    //  * replacement_policy: the instance of the replacement_policy
    //  * cache_system: pretty self-explanatory, this is a pointer to the cache
    //    system. This pointer should be treated as readonly.
    //  * set_idx: the index of the set that needs a cache line evicted.
    // Returns: the index within the set to evict
    uint32_t (*eviction_index)(struct replacement_policy *replacement_policy,
                               struct cache_system *cache_system, uint32_t set_idx);

    // This function is called whenever a cache line is accessed and can be
    // used to update the state of the replacement policy.
    //
    // Argruments:
    //  * replacement_policy: the instance of the replacement_policy
    //  * cache_system: pretty self-explanatory, this is a pointer to the cache
    //    system. This pointer should be treated as readonly.
    //  * set_idx: the index of the set that is being accessed.
    //  * tag: the tag within the set that is being accessed.
    void (*cache_access)(struct replacement_policy *replacement_policy,
                         struct cache_system *cache_system, uint32_t set_idx, uint32_t tag);

    // This function is called right before the replacement policy is
    // deallocated. You should perform any necessary cleanup operations here.
    // (This is where you should free the replacement_policy->data, for
    // example.)
    //
    // Arguments:
    //  * replacement_policy: the instance of replacement_policy to clean up
    void (*cleanup)(struct replacement_policy *replacement_policy);

    // Use this pointer to store any data for the replacement policy.
    void *data;
};

// Constructors for each of the replacement policies.
struct replacement_policy *lru_replacement_policy_new(uint32_t sets, uint32_t associativity);
struct replacement_policy *rand_replacement_policy_new(uint32_t sets, uint32_t associativity);
struct replacement_policy *lru_prefer_clean_replacement_policy_new(uint32_t sets,
                                                                   uint32_t associativity);

#endif
