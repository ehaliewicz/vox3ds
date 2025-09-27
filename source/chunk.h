#ifndef CHUNK
#define CHUNK

#include "common.h"
#include "types.h"

typedef enum {
    SAND = 0,
    OBSIDIAN = 1,
    COBBLE = 2,
    STONE= 3,
    DIRT = 4,
    WOOD = 5,
    DIAMOND = 6,
    REDSTONE = 7,
    AIR = 8,
} block_type;

typedef struct {
    // 16kB of data per chunk
    // only 1 MB of data total for all 64 chunks
    u8 blocks[(CHUNK_SIZE*CHUNK_SIZE*CHUNK_SIZE)/2];
} chunk;

extern chunk chunks[64];

#endif