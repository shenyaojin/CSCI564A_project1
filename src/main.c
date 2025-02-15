//
// This is the cachesim main file. It is the entrypoint of the simulator.
//
// This file handles all of the argument and input parsing as well as the
// output printing. It calls the active cache system for each of the memory
// accesses received via stdin.
//

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory_system.h"
#include "replacement_policies.h"

int main(int argc, char **argv)
{
    // Parse the arguments.
    if (argc != 5) {
        fprintf(stderr, "Incorrect number of arguments.\n");
        return 1;
    }
    char *replacement_policy_str = argv[1];
    char *endptr;
    size_t cache_size = strtol(argv[2], &endptr, 10);
    size_t cache_lines = strtol(argv[3], &endptr, 10);
    size_t associativity = strtol(argv[4], &endptr, 10);

    // TODO: calculate the line size and number of sets.
    int line_size = 0;
    int sets = 0;

    // Print out some parameter info
    printf("Parameter Info\n");
    printf("==============\n");
    printf("Replacement Policy: %s\n", replacement_policy_str);
    printf("Cache Size: %ld\n", cache_size);
    printf("Cache Lines: %ld\n", cache_lines);
    printf("Associativity: %ld\n", associativity);
    printf("Line Size: %dB\n", line_size);
    printf("Number of Sets: %d\n", sets);

    // Instantiate the cache system.
    struct cache_system *cache_system = cache_system_new(line_size, sets, associativity);

    // Instantiate the replacement policy
    struct replacement_policy *replacement_policy;
    if (!strcmp("LRU", replacement_policy_str)) {
        replacement_policy =
            lru_replacement_policy_new(cache_system->num_sets, cache_system->associativity);
    } else if (!strcmp("RAND", replacement_policy_str)) {
        replacement_policy =
            rand_replacement_policy_new(cache_system->num_sets, cache_system->associativity);
    } else if (!strcmp("LRU_PREFER_CLEAN", replacement_policy_str)) {
        replacement_policy = lru_prefer_clean_replacement_policy_new(cache_system->num_sets,
                                                                     cache_system->associativity);
    } else {
        fprintf(stderr, "Unknown replacement policy %s", replacement_policy_str);
        return 1;
    }

    cache_system->replacement_policy = replacement_policy;

    // Read the input and call the cache system mem_access function.
    char rw = 0;
    uint32_t address = 0;
    while (scanf("%c %x\n", &rw, &address) >= 0) {
        printf("%s at 0x%x\n", (rw == 'R' ? "read" : "write"), address);
        if (cache_system_mem_access(cache_system, address, rw) != 0) {
            return 1;
        }
    }

    // Print the statistics
    printf("\n\nStatistics\n");
    printf("==========\n");
    printf("OUTPUT ACCESSES %d\n", cache_system->stats.accesses);
    printf("OUTPUT HITS %d\n", cache_system->stats.hits);
    printf("OUTPUT MISSES %d\n", cache_system->stats.misses);
    printf("OUTPUT DIRTY EVICTIONS %d\n", cache_system->stats.dirty_evictions);
    printf("OUTPUT HIT RATIO %.8f\n",
           (double)cache_system->stats.hits / cache_system->stats.accesses);

    // Clean everything up.
    cache_system_cleanup(cache_system);
    free(cache_system);

    return 0;
}
