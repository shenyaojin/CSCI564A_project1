//
// This file defines the structs, enums, and the function signatures necessary
// for managing a cache system and storing statistics about the cache
// performance.
//

#ifndef MEMORY_SYSTEM_H
#define MEMORY_SYSTEM_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

struct replacement_policy;
#include "replacement_policies.h"

// This struct contains statistics about the cache performance.
struct cache_system_stats {
    uint32_t accesses;        // Total number of cache accesses
    uint32_t hits;            // Total number of cache hits
    uint32_t misses;          // Total number of cache misses
    uint32_t dirty_evictions; // Total number of cache evictions requiring write-back
};

// This enum keeps track of the status of each cache line in a set.
enum cache_status {
    INVALID,   // The cache line is invalid.
    EXCLUSIVE, // The cache line is valid, and held exclusively by the current processor (no
               // multi-processors).
    MODIFIED,  // The cache line is valid, and modified (requires write-back).
};
struct cache_line {
    uint32_t tag;
    enum cache_status status;
};

// This struct contains the data related to a cache system.
struct cache_system {
    struct cache_system_stats stats;
    struct replacement_policy *replacement_policy;

    // The cache state
    uint32_t line_size, num_sets, associativity;
    uint32_t index_bits, tag_bits, offset_bits;
    struct cache_line *cache_lines; // Storing the cache lines in a flat array.

    // Masks and shifts
    uint32_t offset_mask, set_index_mask;
};

// Create a new cache system.
struct cache_system *cache_system_new(uint32_t line_size, uint32_t sets, uint32_t associativity);
void cache_system_cleanup(struct cache_system *cache_system);

// Perform updates to access memory
int cache_system_mem_access(struct cache_system *cache_system, uint32_t address, char rw);

// Returns a pointer to the cache line within the given set that has the given
// tag. If no such element exists, then return NULL.
struct cache_line *cache_system_find_cache_line(struct cache_system *cache_system, uint32_t set_idx,
                                                uint32_t tag);

#endif
