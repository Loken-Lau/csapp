#include "cachelab.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>

typedef struct {
    int valid;
    unsigned long tag;
    int lru_counter;
} cache_line_t;

typedef struct {
    cache_line_t *lines;
} cache_set_t;

typedef struct {
    cache_set_t *sets;
    int S;  // number of sets
    int E;  // lines per set
    int B;  // block size
} cache_t;

int hits = 0;
int misses = 0;
int evictions = 0;
int verbose = 0;
int global_lru = 0;

cache_t* init_cache(int s, int E, int b) {
    int S = 1 << s;
    cache_t *cache = malloc(sizeof(cache_t));
    cache->S = S;
    cache->E = E;
    cache->B = 1 << b;
    cache->sets = malloc(S * sizeof(cache_set_t));

    for (int i = 0; i < S; i++) {
        cache->sets[i].lines = malloc(E * sizeof(cache_line_t));
        for (int j = 0; j < E; j++) {
            cache->sets[i].lines[j].valid = 0;
            cache->sets[i].lines[j].tag = 0;
            cache->sets[i].lines[j].lru_counter = 0;
        }
    }
    return cache;
}

void free_cache(cache_t *cache) {
    for (int i = 0; i < cache->S; i++) {
        free(cache->sets[i].lines);
    }
    free(cache->sets);
    free(cache);
}

void access_cache(cache_t *cache, unsigned long address, int s, int b) {
    unsigned long set_mask = (1UL << s) - 1;
    unsigned long set_index = (address >> b) & set_mask;
    unsigned long tag = address >> (s + b);

    cache_set_t *set = &cache->sets[set_index];

    // Check for hit
    for (int i = 0; i < cache->E; i++) {
        if (set->lines[i].valid && set->lines[i].tag == tag) {
            hits++;
            set->lines[i].lru_counter = global_lru++;
            if (verbose) printf("hit ");
            return;
        }
    }

    // Miss
    misses++;
    if (verbose) printf("miss ");

    // Find empty line or LRU line
    int lru_index = 0;
    int found_empty = 0;

    for (int i = 0; i < cache->E; i++) {
        if (!set->lines[i].valid) {
            lru_index = i;
            found_empty = 1;
            break;
        }
    }

    if (!found_empty) {
        // Find LRU
        int min_lru = set->lines[0].lru_counter;
        for (int i = 1; i < cache->E; i++) {
            if (set->lines[i].lru_counter < min_lru) {
                min_lru = set->lines[i].lru_counter;
                lru_index = i;
            }
        }
        evictions++;
        if (verbose) printf("eviction ");
    }

    // Load into cache
    set->lines[lru_index].valid = 1;
    set->lines[lru_index].tag = tag;
    set->lines[lru_index].lru_counter = global_lru++;
}

void simulate(cache_t *cache, char *trace_file, int s, int b) {
    FILE *fp = fopen(trace_file, "r");
    if (!fp) {
        fprintf(stderr, "Error opening file\n");
        exit(1);
    }

    char op;
    unsigned long address;
    int size;

    while (fscanf(fp, " %c %lx,%d", &op, &address, &size) == 3) {
        if (op == 'I') continue;

        if (verbose) printf("%c %lx,%d ", op, address, size);

        if (op == 'L' || op == 'S') {
            access_cache(cache, address, s, b);
        } else if (op == 'M') {
            access_cache(cache, address, s, b);
            access_cache(cache, address, s, b);
        }

        if (verbose) printf("\n");
    }

    fclose(fp);
}

int main(int argc, char *argv[]) {
    int s = 0, E = 0, b = 0;
    char *trace_file = NULL;
    int opt;

    while ((opt = getopt(argc, argv, "hvs:E:b:t:")) != -1) {
        switch (opt) {
            case 'h':
                printf("Usage: %s [-hv] -s <num> -E <num> -b <num> -t <file>\n", argv[0]);
                printf("Options:\n");
                printf("  -h         Print this help message.\n");
                printf("  -v         Optional verbose flag.\n");
                printf("  -s <num>   Number of set index bits.\n");
                printf("  -E <num>   Number of lines per set.\n");
                printf("  -b <num>   Number of block offset bits.\n");
                printf("  -t <file>  Trace file.\n");
                exit(0);
            case 'v':
                verbose = 1;
                break;
            case 's':
                s = atoi(optarg);
                break;
            case 'E':
                E = atoi(optarg);
                break;
            case 'b':
                b = atoi(optarg);
                break;
            case 't':
                trace_file = optarg;
                break;
            default:
                exit(1);
        }
    }

    if (s == 0 || E == 0 || b == 0 || trace_file == NULL) {
        fprintf(stderr, "Missing required arguments\n");
        exit(1);
    }

    cache_t *cache = init_cache(s, E, b);
    simulate(cache, trace_file, s, b);
    free_cache(cache);

    printSummary(hits, misses, evictions);
    return 0;
}
