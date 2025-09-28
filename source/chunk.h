#ifndef CHUNK
#define CHUNK

#include <citro3d.h>

#include "common.h"
#include "types.h"

typedef enum {
    SAND = 0,
    OBSIDIAN = 1,
    LEAVES = 2,
    STONE= 3,
    DIRT = 4,
    WOOD = 5,
    DIAMOND = 6,
    REDSTONE = 7,
    AIR = 8,
} block_type;

typedef enum {
	FRONT=0,
	BACK=1,
	TOP_BOTTOM=2,
	LEFT=3,
	RIGHT=4
} chunk_face;

typedef struct {
    // 16kB of data per chunk
    // only 1 MB of data total for all 64 chunks
    u8 blocks[(CHUNK_SIZE*CHUNK_SIZE*CHUNK_SIZE)/2];
} chunk;

#define CHUNK_VERTEX_SIZE (CHUNK_SIZE+1)
#define LOD1_CHUNK_Y_VERTEX_SIZE ((CHUNK_SIZE/2)+1)



extern lod0_vertex lod0_full_vertex_list[];
extern lod1_vertex lod1_full_vertex_list[];


extern u16 *lod0_per_face_index_lists[5];
extern u16 *lod1_per_face_index_lists[5];
extern u16 lod0_indexes_per_face[5];
extern u16 lod1_indexes_per_face[5];


extern void *lod0_vbo_data, *lod1_vbo_data;

extern chunk chunks[64];

void chunks_init();
void chunk_init_lod_table(float camX, float camY, float camZ);

void draw_lod0_chunks(C3D_Mtx* mvpMatrix, int chunk_offset_uniform_loc, int draw_top_uvs_uniform_loc, float camX, float camY, float camZ);
void draw_lod1_chunks(C3D_Mtx* mvpMatrix, int chunk_offset_uniform_loc, float camX, float camY, float camZ);
#endif