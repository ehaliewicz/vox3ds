#include <3ds.h>
#include <stdio.h>
#include "alloc.h"
#include "chunk.h"
#include "common.h"
#include "types.h"
// we should pre-allocate all meshes

typedef enum {
    ALLOCATED_IN_RAM,
    ALLOCATED_IN_VRAM
} mesh_alloc_type;

typedef enum {
    UNINITIALIZED,
    INITIALIZED
} state;


typedef struct {
    void* vertex_buffer;
    u16* index_buffer;
    u16 num_verts;
    u8 in_use; u8 index_loc; u8 vertex_loc;
} mesh;



#define LOD0_VERT_BUF_SZ ((CHUNK_SIZE+1)*(CHUNK_SIZE+1)*(CHUNK_SIZE+1))
#define LOD1_VERT_BUF_SZ ((CHUNK_SIZE+1)*(CHUNK_SIZE/2+1)*(CHUNK_SIZE+1))
#define LOD2_VERT_BUF_SZ ((CHUNK_SIZE+1)*(CHUNK_SIZE/4+1)*(CHUNK_SIZE+1))

#define LOD0_IDX_BUF_SZ (CHUNK_SIZE*CHUNK_SIZE*CHUNK_SIZE*8)
#define LOD1_IDX_BUF_SZ (CHUNK_SIZE*CHUNK_SIZE/2*CHUNK_SIZE*8)
#define LOD2_IDX_BUF_SZ (CHUNK_SIZE*CHUNK_SIZE/4*CHUNK_SIZE*8)

//static lod0_vertex lod0_vert_staging[(CHUNK_SIZE+1)*(CHUNK_SIZE+1)*(CHUNK_SIZE+1)];
//static lod1_vertex lod1_full_vertex_list[(CHUNK_SIZE+1)*(CHUNK_SIZE/2+1)*(CHUNK_SIZE+1)];
//static lod1_vertex lod2_full_vertex_list[(CHUNK_SIZE+1)*(CHUNK_SIZE/4+1)*(CHUNK_SIZE+1)];


//static u16 lod0_full_index_list[CHUNK_SIZE*CHUNK_SIZE*CHUNK_SIZE*8];
//static u16 lod1_full_index_list[CHUNK_SIZE*CHUNK_SIZE/2*CHUNK_SIZE*8];
//static u16 lod2_full_index_list[CHUNK_SIZE*CHUNK_SIZE/4*CHUNK_SIZE*8];


#define MAX_LOD1_MESHES 32
mesh lod1_meshes[MAX_LOD1_MESHES];
#define MAX_LOD0_MESHES 32
mesh lod0_meshes[MAX_LOD0_MESHES];

void reset_meshes() {
    for(int i = 0; i < MAX_LOD0_MESHES; i++) {
        lod0_meshes[i].in_use = 0;
        lod0_meshes[i].index_buffer = mmLinearAlloc(LOD0_IDX_BUF_SZ*sizeof(u16));
        lod0_meshes[i].index_loc = ALLOCATED_IN_RAM;
        lod0_meshes[i].vertex_buffer = mmLinearAlloc(LOD0_VERT_BUF_SZ*sizeof(lod0_vertex));
        lod0_meshes[i].vertex_loc = ALLOCATED_IN_RAM;
    }

    for(int i = 0; i < MAX_LOD1_MESHES; i++) {
        lod1_meshes[i].in_use = 0;
        lod1_meshes[i].index_buffer = mmLinearAlloc(LOD1_IDX_BUF_SZ*sizeof(u16));
        lod1_meshes[i].index_loc = ALLOCATED_IN_RAM;
        lod1_meshes[i].vertex_buffer = mmLinearAlloc(LOD1_VERT_BUF_SZ*sizeof(lod1_vertex));
        lod1_meshes[i].vertex_loc = ALLOCATED_IN_RAM;
    }
    
}

// meshes needs to do the following

// receive a hierarchical bitmap like the following



// toplevel bitmap, 2 means draw at level 2
// 1 means check next bitmap

// in lower level bitmap, 2 means already drawn, 1 means draw this as 1 LOD1 mesh
// 0 means draw as 4 LOD1 meshes

// 21   2211
// 11   2211
//      1100
//      1100

// so this bitmap should be interpreted as draw 1 LOD2 chunk, 8 LOD1 chunks, and 16 LOD0 chunks

// at this point, we 


void mesh_chunks(u8 top_bitmap[4], u8 bot_bitmap[16], chunk chunks[64]) {

}