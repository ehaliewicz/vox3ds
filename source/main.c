#include <3ds.h>
#include <citro3d.h>
#include <tex3ds.h>
#include <string.h>

#include "alloc.h"
#include "common.h"
#include "types.h"


#include "lod0_program_shbin.h"
#include "lod1_program_shbin.h"
#include "mixed_program_shbin.h"
#include "per_face_program_shbin.h"
#include "skybox_shbin.h"

#include "atlas_t3x.h"
#include "atlas_mip1_t3x.h"
#include "atlas_mip2_t3x.h"
#include "atlas_mip3_t3x.h"
#include "render_target_tex_t3x.h"
#include "skybox_t3x.h"


#define MIN(x,y) ((x)<(y)?(x):(y))
#define MAX(x,y) ((x)>(y)?(x):(y))
#define CLAMP(a,x,y) MAX(x,MIN(a,y))

#define NEAR_PLANE_DIST 0.1f
#define FAR_PLANE_DIST 1000.0f
//150.0f

#define HFOV_DEGREES 90.0f
#define HFOV 1.57f
//(C3D_AngleFromDegrees(HFOV_DEGREES))
#define VFOV (2.0f * atanf(tanf(HFOV*0.5f) / C3D_AspectRatioTop))



#define CLEAR_COLOR 0x68B0D8FF

#define DISPLAY_TRANSFER_FLAGS \
	(GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) | \
	GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
	GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))



uint16_t random(void) {
	//static uint16_t start_state = 0xACE1u;  /* Any nonzero start state will work. */
    static uint16_t lfsr = 0xACE1u;
    uint16_t bit;                    /* Must be 16-bit to allow bit<<15 later in the code */

    
    /* taps: 16 14 13 11; feedback polynomial: x^16 + x^14 + x^13 + x^11 + 1 */
	bit = ((lfsr >> 0) ^ (lfsr >> 2) ^ (lfsr >> 3) ^ (lfsr >> 5)) & 1u;
	lfsr = (lfsr >> 1) | (bit << 15);
	return lfsr;

}
uint16_t random_texture(void) {
	uint16_t v = (random()&7);
	//if(v == 2) { v = 3; }
	return v;
}



// vertexes of a cube 


static lod0_vertex lod0_full_vertex_list[(CHUNK_SIZE+1)*(CHUNK_SIZE+1)*(CHUNK_SIZE+1)];
static lod1_vertex lod1_full_vertex_list[(CHUNK_SIZE+1)*(CHUNK_SIZE/2+1)*(CHUNK_SIZE+1)];
static lod1_vertex lod2_full_vertex_list[(CHUNK_SIZE+1)*(CHUNK_SIZE/4+1)*(CHUNK_SIZE+1)];


static u16 lod0_full_index_list[CHUNK_SIZE*CHUNK_SIZE*CHUNK_SIZE*8];
static u16 lod1_full_index_list[CHUNK_SIZE*CHUNK_SIZE/2*CHUNK_SIZE*8];
static u16 lod2_full_index_list[CHUNK_SIZE*CHUNK_SIZE/4*CHUNK_SIZE*8];

typedef enum {
	FRONT=0,
	BACK=1,
	TOP=2,
	BOTTOM=3,
	LEFT=4,
	RIGHT=5
} face;

// each chunk has 31*31*31 voxels, 
static u16 *lod0_per_face_index_lists[6];
static u16 *lod1_per_face_index_lists[6];
static u16 lod0_indexes_per_face[6] = {0,0,0,0,0,0};
static u16 lod1_indexes_per_face[6] = {0,0,0,0,0,0};

#define CHUNK_SIZE_BITS 5

// 5 bits x,y,z
int get_vertex_idx(int x, int y, int z) {
	int low_z = z & 0b11;
	int low_y = y & 0b11;
	int low_x = x & 0b11;
	int low_idx = (low_z<<4)|(low_y<<2)|low_x;
	int high_z = (z & ~0b11)>>2; 
	int high_y = (y & ~0b11)>>2;
	int high_x = (x & ~0b11)>>2;
	return (((high_z << ((CHUNK_SIZE_BITS-2)*2)) | (high_y << (CHUNK_SIZE_BITS-2)) | (high_x)) << 6) | low_idx;
}

// 5 bits x,z ; 4 bits y (0->16)
int get_lod1_vertex_idx(int x, int y, int z) {
	int low_z = z & 0b11; // 2 bits 
	int low_y = y & 0b11; // 2 bits 
	int low_x = x & 0b11; // 2 bits
	int low_idx = (low_z<<4)|(low_y<<2)|low_x;
	int high_z = (z & ~0b11)>>2; // 3 bits 
	int high_y = (y & ~0b11)>>2; // 2 bits
	int high_x = (x & ~0b11)>>2; // 3 bits 
	return (((high_z << 5) | (high_y << 3) | (high_x)) << 6) | low_idx;
}

// 5 bits x,z ; 3 bits y (0->8)
int get_lod2_vertex_idx(int x, int y, int z) {
	int low_z = z & 0b11; // 2 bits 
	int low_y = y & 0b11; // 2 bits 
	int low_x = x & 0b11; // 2 bits
	int low_idx = (low_z<<4)|(low_y<<2)|low_x;
	int high_z = (z & ~0b11)>>2; // 3 bits 
	int high_y = (y & ~0b11)>>2; // 1 bits
	int high_x = (x & ~0b11)>>2; // 3 bits 
	return (((high_z << 4) | (high_y << 3) | (high_x)) << 6) | low_idx;
}



#define CHUNKS_X 6
#define CHUNKS_Z 6

u8 texture_colors[16][3] = {
	
	{165,134,87},
	{23,19,31},
	//{15,12,20},
	{100,100,100},
	{107,99,89},
	{54,39,18},
	{74,54,24},
	{100,100,100},
	{100,100,100},
	{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},
};


int sharp_heightmap_table[CHUNK_SIZE*CHUNK_SIZE];
int heightmap_table[CHUNK_SIZE*CHUNK_SIZE];
void build_heightmap_table() {
	int center_x = (CHUNK_SIZE+1)/2;
	int center_z = (CHUNK_SIZE+2)/2;				
	for(int z = 0; z < CHUNK_SIZE; z++) {
		for(int x = 0; x < CHUNK_SIZE; x++) {

			int dx = x - center_x;
			int dz = z - center_z;
			float rad = sqrtf(dx*dx + dz*dz);

			// Radial falloff: peak at center, 0 at edges
			float height = (1.0 - (rad / 16.0f));  // from 1 at center → 0 at edge
			float sharp_height = height*height;
			sharp_height = MAX(sharp_height, 0.0f);          // clamp to non-negative
			height = MAX(height, 0.0f);
			sharp_heightmap_table[z*CHUNK_SIZE+x] = sharp_height*32;
			heightmap_table[z*CHUNK_SIZE+x] = height*12;


		}
	}
}

void mesh_chunk() {
	build_heightmap_table(); 

	for(int z = 0; z < CHUNK_SIZE+1; z++) {
		for(int y = 0; y < CHUNK_SIZE+1; y++) {
			for(int x = 0; x < CHUNK_SIZE+1; x++) {
				int idx = get_vertex_idx(x,y,z);
				lod0_full_vertex_list[idx].material = 0;
				lod0_full_vertex_list[idx].position[0] = x;
				lod0_full_vertex_list[idx].position[1] = y;
				lod0_full_vertex_list[idx].position[2] = z;
			}
		}
	}

	
	for(int z = 0; z < CHUNK_SIZE+1; z++) {
		for(int y = 0; y < CHUNK_SIZE/2+1; y++) {
			for(int x = 0; x < CHUNK_SIZE+1; x++) {
				int idx = get_lod1_vertex_idx(x,y,z);
				lod1_full_vertex_list[idx].position[0] = x*2;
				lod1_full_vertex_list[idx].position[1] = y*2;
				lod1_full_vertex_list[idx].position[2] = z*2;
			}
		}
	}

	for(int z = 0; z < CHUNK_SIZE+1; z++) {
		for(int y = 0; y < CHUNK_SIZE/4+1; y++) {
			for(int x = 0; x < CHUNK_SIZE+1; x++) {
				int idx = get_lod2_vertex_idx(x,y,z);
				lod2_full_vertex_list[idx].position[0] = x*4;
				lod2_full_vertex_list[idx].position[1] = y*4;
				lod2_full_vertex_list[idx].position[2] = z*4;
			}
		}
	}


	int lod0_idx = 0;
	int lod1_idx = 0;
	int lod2_idx = 0;

	int front_faces = 0;
	int back_faces = 0;
	int top_faces = 0;
	int bot_faces = 0;
	int left_faces = 0;
	int right_faces = 0;
	

	// TODO: allocate only half?
	lod0_per_face_index_lists[0] = linearAlloc(sizeof(u16)*CHUNK_SIZE*CHUNK_SIZE*CHUNK_SIZE*5);
	lod0_per_face_index_lists[1] = linearAlloc(sizeof(u16)*CHUNK_SIZE*CHUNK_SIZE*CHUNK_SIZE*5);
	lod0_per_face_index_lists[2] = linearAlloc(sizeof(u16)*CHUNK_SIZE*CHUNK_SIZE*CHUNK_SIZE*5);
	lod0_per_face_index_lists[3] = linearAlloc(sizeof(u16)*CHUNK_SIZE*CHUNK_SIZE*CHUNK_SIZE*5);
	lod0_per_face_index_lists[4] = linearAlloc(sizeof(u16)*CHUNK_SIZE*CHUNK_SIZE*CHUNK_SIZE*5);
	lod0_per_face_index_lists[5] = linearAlloc(sizeof(u16)*CHUNK_SIZE*CHUNK_SIZE*CHUNK_SIZE*5);

	lod1_per_face_index_lists[0] = linearAlloc(sizeof(u16)*CHUNK_SIZE*CHUNK_SIZE/2*CHUNK_SIZE*5);
	lod1_per_face_index_lists[1] = linearAlloc(sizeof(u16)*CHUNK_SIZE*CHUNK_SIZE/2*CHUNK_SIZE*5);
	lod1_per_face_index_lists[2] = linearAlloc(sizeof(u16)*CHUNK_SIZE*CHUNK_SIZE/2*CHUNK_SIZE*5);
	lod1_per_face_index_lists[3] = linearAlloc(sizeof(u16)*CHUNK_SIZE*CHUNK_SIZE/2*CHUNK_SIZE*5);
	lod1_per_face_index_lists[4] = linearAlloc(sizeof(u16)*CHUNK_SIZE*CHUNK_SIZE/2*CHUNK_SIZE*5);
	lod1_per_face_index_lists[5] = linearAlloc(sizeof(u16)*CHUNK_SIZE*CHUNK_SIZE/2*CHUNK_SIZE*5);

	for(int z = 0; z < CHUNK_SIZE; z++) {
		int z_border = (z == 0 || z == CHUNK_SIZE-1);
		for(int y = 0; y < CHUNK_SIZE; y++) {
			int y_border = y == 0 || y == CHUNK_SIZE-1;
			//if(y != 0) { continue; }
			for(int x = 0; x < CHUNK_SIZE; x++) {
				int x_border = x == 0 || x == CHUNK_SIZE-1;

				// output the indexes for each voxel
					// - + +
				int v0_idx = get_vertex_idx(x,  y,   z);
				int v1_idx = get_vertex_idx(x+1,y,   z);
				int v2_idx = get_vertex_idx(x,  y+1, z);
				int v3_idx = get_vertex_idx(x+1,y+1, z);
				int v4_idx = get_vertex_idx(x,  y,   z+1);
				int v5_idx = get_vertex_idx(x+1,y,   z+1);
				int v6_idx = get_vertex_idx(x,  y+1, z+1);
				int v7_idx = get_vertex_idx(x+1,y+1, z+1);
				int tex_idx = (x_border || z_border) ? 1 : random_texture();

				lod0_full_vertex_list[v0_idx].material = tex_idx; //(x+y+z)%7)+1;

				int this_height = sharp_heightmap_table[z*CHUNK_SIZE+x];
				int left_height = x == 0 ? 0 : sharp_heightmap_table[z*CHUNK_SIZE+x-1];
				int right_height = x == CHUNK_SIZE-1 ? 0 : sharp_heightmap_table[z*CHUNK_SIZE+x+1];
				int back_height = z == 0 ? 0 : sharp_heightmap_table[(z-1)*CHUNK_SIZE+x];
				int forward_height = z == CHUNK_SIZE-1 ? 0 : sharp_heightmap_table[(z+1)*CHUNK_SIZE+x];
				int left_exposed = y > left_height;
				int right_exposed = y > right_height;
				int forward_exposed = y > forward_height;
				int back_exposed = y > back_height;
				int center_exposed = y == this_height;

				int bot_exposed = y == 0;
				
				if((y<=this_height) && back_exposed) {
					lod0_per_face_index_lists[BACK][back_faces++] = v0_idx;
					lod0_per_face_index_lists[BACK][back_faces++] = v1_idx;
					lod0_per_face_index_lists[BACK][back_faces++] = v2_idx;
					lod0_per_face_index_lists[BACK][back_faces++] = v3_idx;
					lod0_per_face_index_lists[BACK][back_faces++] = v7_idx;
				}

				if((y<=this_height) && forward_exposed) {
					lod0_per_face_index_lists[FRONT][front_faces++] = v5_idx;
					lod0_per_face_index_lists[FRONT][front_faces++] = v4_idx;
					lod0_per_face_index_lists[FRONT][front_faces++] = v7_idx;
					lod0_per_face_index_lists[FRONT][front_faces++] = v6_idx; 
					lod0_per_face_index_lists[FRONT][front_faces++] = v7_idx;
				}

				if(center_exposed) {
					lod0_per_face_index_lists[TOP][top_faces++] = v3_idx;
					lod0_per_face_index_lists[TOP][top_faces++] = v7_idx;
					lod0_per_face_index_lists[TOP][top_faces++] = v2_idx;
					lod0_per_face_index_lists[TOP][top_faces++] = v6_idx;
					lod0_per_face_index_lists[TOP][top_faces++] = v7_idx;
				}

				if((y<=this_height) && left_exposed) {
					lod0_per_face_index_lists[LEFT][left_faces++] = v4_idx;
					lod0_per_face_index_lists[LEFT][left_faces++] = v0_idx;
					lod0_per_face_index_lists[LEFT][left_faces++] = v6_idx;
					lod0_per_face_index_lists[LEFT][left_faces++] = v2_idx;
					lod0_per_face_index_lists[LEFT][left_faces++] = v7_idx;
				}
				if((y<=this_height) && right_exposed) {
					lod0_per_face_index_lists[RIGHT][right_faces++] = v1_idx;
					lod0_per_face_index_lists[RIGHT][right_faces++] = v5_idx;
					lod0_per_face_index_lists[RIGHT][right_faces++] = v3_idx;
					lod0_per_face_index_lists[RIGHT][right_faces++] = v7_idx;
					lod0_per_face_index_lists[RIGHT][right_faces++] = v7_idx;
				}

			
				if(y == 0) {
					lod0_per_face_index_lists[BOTTOM][bot_faces++] = v0_idx;
					lod0_per_face_index_lists[BOTTOM][bot_faces++] = v4_idx;
					lod0_per_face_index_lists[BOTTOM][bot_faces++] = v1_idx;
					lod0_per_face_index_lists[BOTTOM][bot_faces++] = v5_idx;
					lod0_per_face_index_lists[BOTTOM][bot_faces++] = v7_idx;
				}
				

			}
		}
	}

	lod0_indexes_per_face[FRONT] = front_faces;
	lod0_indexes_per_face[BACK] = back_faces;
	lod0_indexes_per_face[TOP] = top_faces;
	lod0_indexes_per_face[BOTTOM] = bot_faces;
	lod0_indexes_per_face[LEFT] = left_faces;
	lod0_indexes_per_face[RIGHT] = right_faces;

	back_faces = front_faces = top_faces = bot_faces = left_faces = right_faces = 0;
	// create LOD1 mesh
	for(int z = 0; z < CHUNK_SIZE; z++) {
		int z_border = (z == 0 || z == CHUNK_SIZE-1);
		for(int y = 0; y < CHUNK_SIZE/2; y++) {
			for(int x = 0; x < CHUNK_SIZE; x++) {
				int x_border = x == 0 || x == CHUNK_SIZE-1;

				int v0_idx = get_lod1_vertex_idx(x,  y,   z);
				int v1_idx = get_lod1_vertex_idx(x+1,y,   z);
				int v2_idx = get_lod1_vertex_idx(x,  y+1, z);
				int v3_idx = get_lod1_vertex_idx(x+1,y+1, z);
				int v4_idx = get_lod1_vertex_idx(x,  y,   z+1);
				int v5_idx = get_lod1_vertex_idx(x+1,y,   z+1);
				int v6_idx = get_lod1_vertex_idx(x,  y+1, z+1);
				int v7_idx = get_lod1_vertex_idx(x+1,y+1, z+1);
				int lod1_tex_idx0 = (x_border || z_border) ? 1 : random_texture();
				int lod1_tex_idx1 = (x_border || z_border) ? 1 : random_texture();
				int lod1_tex_idx2 = (x_border || z_border) ? 1 : random_texture();
				int lod1_tex_idx3 = (x_border || z_border) ? 1 : random_texture();

				int lod1_r = ((texture_colors[lod1_tex_idx0][0] + texture_colors[lod1_tex_idx1][0] + texture_colors[lod1_tex_idx2][0] + texture_colors[lod1_tex_idx3][0]) / 4.0f);
				int lod1_g = ((texture_colors[lod1_tex_idx0][1] + texture_colors[lod1_tex_idx1][1] + texture_colors[lod1_tex_idx2][1] + texture_colors[lod1_tex_idx3][1]) / 4.0f);
				int lod1_b = ((texture_colors[lod1_tex_idx0][2] + texture_colors[lod1_tex_idx1][2] + texture_colors[lod1_tex_idx2][2] + texture_colors[lod1_tex_idx3][2]) / 4.0f);

				lod1_full_vertex_list[v0_idx].color[0] = lod1_r;
				lod1_full_vertex_list[v0_idx].color[1] = lod1_g;
				lod1_full_vertex_list[v0_idx].color[2] = lod1_b;

				int lod1_z = (z*2)%CHUNK_SIZE;
				int lod1_fz = ((z+1)*2)%CHUNK_SIZE;
				int lod1_bz = ((z-1)*2)%CHUNK_SIZE;
				int lod1_x = (x*2)%CHUNK_SIZE;
				int lod1_rx = ((x+1)*2)%CHUNK_SIZE;
				int lod1_lx = ((x-1)*2)%CHUNK_SIZE;
				int this_height = sharp_heightmap_table[lod1_z*CHUNK_SIZE+lod1_x];
				int left_height = lod1_x == 0 ? 0 : sharp_heightmap_table[lod1_z*CHUNK_SIZE+lod1_lx];
				int right_height = lod1_x == CHUNK_SIZE-1 ? 0 : sharp_heightmap_table[lod1_z*CHUNK_SIZE+lod1_rx];
				int back_height = lod1_z == 0 ? 0 : sharp_heightmap_table[lod1_bz*CHUNK_SIZE+lod1_x];
				int forward_height = lod1_z == CHUNK_SIZE-1 ? 0 : sharp_heightmap_table[lod1_fz*CHUNK_SIZE+lod1_x];

				int left_exposed = y*2 >= left_height;
				int right_exposed = y*2 >= right_height;
				int forward_exposed = y*2 >= forward_height;
				int back_exposed = y*2 >= back_height;
				int center_exposed = y*2 == this_height || y*2+1 == this_height;

				
				//if(center_exposed || ((y*2<=this_height) && (left_exposed || right_exposed || back_exposed || forward_exposed))) {
				//	lod1_full_index_list[lod1_idx++] = v0_idx;
				//	lod1_full_index_list[lod1_idx++] = v1_idx;
				//	lod1_full_index_list[lod1_idx++] = v2_idx;
				//	lod1_full_index_list[lod1_idx++] = v3_idx;
				//	lod1_full_index_list[lod1_idx++] = v4_idx;
				//	lod1_full_index_list[lod1_idx++] = v5_idx;
				//	lod1_full_index_list[lod1_idx++] = v6_idx;
				//	lod1_full_index_list[lod1_idx++] = v7_idx;
				//}

				if((y*2 <= this_height) && back_exposed) {
					lod1_per_face_index_lists[BACK][back_faces++] = v0_idx;
					lod1_per_face_index_lists[BACK][back_faces++] = v1_idx;
					lod1_per_face_index_lists[BACK][back_faces++] = v2_idx;
					lod1_per_face_index_lists[BACK][back_faces++] = v3_idx;
					lod1_per_face_index_lists[BACK][back_faces++] = v7_idx;
				}

				if((y*2<=this_height) && forward_exposed) {
					lod1_per_face_index_lists[FRONT][front_faces++] = v5_idx;
					lod1_per_face_index_lists[FRONT][front_faces++] = v4_idx;
					lod1_per_face_index_lists[FRONT][front_faces++] = v7_idx;
					lod1_per_face_index_lists[FRONT][front_faces++] = v6_idx; 
					lod1_per_face_index_lists[FRONT][front_faces++] = v7_idx;
				}

				if(center_exposed) {
					lod1_per_face_index_lists[TOP][top_faces++] = v3_idx;
					lod1_per_face_index_lists[TOP][top_faces++] = v7_idx;
					lod1_per_face_index_lists[TOP][top_faces++] = v2_idx;
					lod1_per_face_index_lists[TOP][top_faces++] = v6_idx;
					lod1_per_face_index_lists[TOP][top_faces++] = v7_idx;
				}

				
				if((y*2<=this_height) && left_exposed) {
					lod1_per_face_index_lists[LEFT][left_faces++] = v4_idx;
					lod1_per_face_index_lists[LEFT][left_faces++] = v0_idx;
					lod1_per_face_index_lists[LEFT][left_faces++] = v6_idx;
					lod1_per_face_index_lists[LEFT][left_faces++] = v2_idx;
					lod1_per_face_index_lists[LEFT][left_faces++] = v7_idx;
				}
				if((y*2<=this_height) && right_exposed) {
					lod1_per_face_index_lists[RIGHT][right_faces++] = v1_idx;
					lod1_per_face_index_lists[RIGHT][right_faces++] = v5_idx;
					lod1_per_face_index_lists[RIGHT][right_faces++] = v3_idx;
					lod1_per_face_index_lists[RIGHT][right_faces++] = v7_idx;
					lod1_per_face_index_lists[RIGHT][right_faces++] = v7_idx;
				}
				if(y == 0) {
					lod1_per_face_index_lists[BOTTOM][bot_faces++] = v0_idx;
					lod1_per_face_index_lists[BOTTOM][bot_faces++] = v4_idx;
					lod1_per_face_index_lists[BOTTOM][bot_faces++] = v1_idx;
					lod1_per_face_index_lists[BOTTOM][bot_faces++] = v5_idx;
					lod1_per_face_index_lists[BOTTOM][bot_faces++] = v7_idx;
				}
			}
		}
	}

	
	lod1_indexes_per_face[FRONT] = front_faces;
	lod1_indexes_per_face[BACK] = back_faces;
	lod1_indexes_per_face[TOP] = top_faces;
	lod1_indexes_per_face[BOTTOM] = bot_faces;
	lod1_indexes_per_face[LEFT] = left_faces;
	lod1_indexes_per_face[RIGHT] = right_faces;
				



}


#define vertex_list_count (sizeof(vertex_list)/sizeof(vertex_list[0]))
#define index_list_count (sizeof(index_list)/sizeof(index_list[0]))

static DVLB_s *lod0_program_dvlb, *lod1_program_dvlb, 
				*tex_tri_program_dvlb, *skybox_vshader_dvlb, 
				*mixed_program_dvlb, *per_face_program_dvlb;
static shaderProgram_s lod0_program, lod1_program, tex_tri_program, skybox_program, mixed_program, per_face_program;

static int lod0_uLoc_mvp, lod1_uLoc_mvp, mixed_uLoc_mvp, per_face_uLoc_mvp;
static int lod0_uLoc_chunkOffset, lod1_uLoc_chunkOffset, mixed_uLoc_chunkOffset, per_face_uLoc_chunk_offset;
static int tex_tri_proj_uLoc;
static int skybox_uLoc_projection, skybox_uLoc_modelView;
static int mixed_uLoc_isLODChunk, mixed_uLoc_skip_x_plus, mixed_uLoc_skip_x_minus, mixed_uLoc_skip_z_plus, mixed_uLoc_skip_z_minus;
static int per_face_uLoc_draw_top_uvs_offset, per_face_uLoc_is_lod_chunk_offset;
static C3D_Mtx projection, ortho_projection, modelView, mvpMatrix;




static void *lod0_vbo_data, *lod1_vbo_data;
static u16 *lod0_ibo_data, *lod1_ibo_data;

static C3D_Tex atlas_tex, skybox_tex;
static C3D_TexCube skybox_cube;



// fuck you vscode c extension that can't handle basic shit like this
#ifdef __GNUC__
#define PACK( __Declaration__ ) __Declaration__ __attribute__((__packed__))
#endif

#ifdef _MSC_VER
#define PACK( __Declaration__ ) __pragma( pack(push, 1) ) __Declaration__ __pragma( pack(pop))
#endif



// Helper function for loading a texture from memory
static bool loadTextureFromMem(C3D_Tex* tex, C3D_TexCube* cube, const void* data, size_t size) {

	Tex3DS_Texture t3x = Tex3DS_TextureImport(data, size, tex, cube, false);

	if (!t3x) {
		return false;
	}

	// Delete the t3x object since we don't need it
	Tex3DS_TextureFree(t3x);
	return true;
}

static const u8 cube_idx_list[] = {
	0,
	1,
	2,
	3,
	4,
	1,
	5,
	0,
	6,
	2,
	7,
	4,
	6, 
	5
};

static const u8 cube_vert_list[8*3] = {
	// First face (PZ)
	// First triangle
	
	0, 1, 1,    // Front-top-left
    1, 1, 1,      // Front-top-right
    0, 0, 1,    // Front-bottom-left
    1, 0, 1,     // Front-bottom-right
    1, 0, 0,    // Back-bottom-right
    //1.f, 1.f, 1.f, //1.0f,      // Front-top-ri#ght
    1, 1, 0,     // Back-top-right
    //0.f, 1.f, 1.f, //1.0f,    // Front-top-left
    0, 1, 0,    // Back-top-left
   // 0.f, 0.f, 1.f, //1.0f,    // Front-bottom-left
    0, 0, 0,   // Back-bottom-left
    //1.f, 0.f, 0.f, //1.0f,    // Back-bottom-right
    //0.f, 1.f, 0.f, //1.0f,    // Back-top-left
    //1.f, 1.f, 0.f, //1.0f,      // Back-top-right

};

static void *skybox_vbo, *skybox_ibo;


int within(float a, float x, float b) {
	return x >= a && x <= b;
}
static float fogDensity = 1.0f;
static float fogEnd = FAR_PLANE_DIST; //.0f;
static float fogStart = .75f;

static float GetFogValue(float c) {

	//printf("c %.2f\n", c);
	//if(c )
	//if (fogMode == FOG_LINEAR) {

		//float adj_fe = fogEnd-fogStart;
		//float adj_c = c-fogStart;
		//return (adj_fe - adj_c) / adj_fe;
		
		// 0 is no fog, 1 is full fog
		return (fogEnd - c) / fogEnd;

		//(150 - (i)/150)

		//150/150
		//149/150
		//148/150


	//} else if (fogMode == FOG_EXP) {
	//	return expf(-(fogDensity * c));
	//} else {
	//	return expf(-(fogDensity * c) * (fogDensity * c));
	//}
}

static void sceneInit(void)
{

	skybox_vbo = mmLinearAlloc(sizeof(cube_vert_list));
	memcpy(skybox_vbo, cube_vert_list, sizeof(cube_vert_list));

	skybox_ibo = mmLinearAlloc(sizeof(cube_idx_list));
	memcpy(skybox_ibo, cube_idx_list, sizeof(cube_idx_list));

	// Load the vertex and geometry shader, create a shader program and bind it
	lod0_program_dvlb = DVLB_ParseFile((u32*)lod0_program_shbin, lod0_program_shbin_size);
	shaderProgramInit(&lod0_program);
	shaderProgramSetVsh(&lod0_program, &lod0_program_dvlb->DVLE[0]);
	shaderProgramSetGsh(&lod0_program, &lod0_program_dvlb->DVLE[1], 16);

	lod1_program_dvlb = DVLB_ParseFile((u32*)lod1_program_shbin, lod1_program_shbin_size);
	shaderProgramInit(&lod1_program);
	shaderProgramSetVsh(&lod1_program, &lod1_program_dvlb->DVLE[0]);
	shaderProgramSetGsh(&lod1_program, &lod1_program_dvlb->DVLE[1], 16);

	
	mixed_program_dvlb = DVLB_ParseFile((u32*)mixed_program_shbin, mixed_program_shbin_size);
	shaderProgramInit(&mixed_program);
	shaderProgramSetVsh(&mixed_program, &mixed_program_dvlb->DVLE[0]);
	shaderProgramSetGsh(&mixed_program, &mixed_program_dvlb->DVLE[1], 16);
	

	//tex_tri_program_dvlb = DVLB_ParseFile((u32*)vshader_shbin, vshader_shbin_size);
	//shaderProgramInit(&tex_tri_program);
	//shaderProgramSetVsh(&tex_tri_program, &tex_tri_program_dvlb->DVLE[0]);

	
	skybox_vshader_dvlb = DVLB_ParseFile((u32*)skybox_shbin, skybox_shbin_size);
	shaderProgramInit(&skybox_program);
	shaderProgramSetVsh(&skybox_program, &skybox_vshader_dvlb->DVLE[0]);

	per_face_program_dvlb = DVLB_ParseFile((u32*)per_face_program_shbin, per_face_program_shbin_size);
	shaderProgramInit(&per_face_program);
	shaderProgramSetVsh(&per_face_program, &per_face_program_dvlb->DVLE[0]);
	shaderProgramSetGsh(&per_face_program, &per_face_program_dvlb->DVLE[1], 10);


	// Get the location of the uniforms
	lod0_uLoc_mvp    = shaderInstanceGetUniformLocation(lod0_program.vertexShader, "mvp");
	lod0_uLoc_chunkOffset  = shaderInstanceGetUniformLocation(lod0_program.vertexShader, "chunk_offset");

	lod1_uLoc_mvp    = shaderInstanceGetUniformLocation(lod1_program.vertexShader, "mvp");
	lod1_uLoc_chunkOffset  = shaderInstanceGetUniformLocation(lod1_program.vertexShader, "chunk_offset");
	
	skybox_uLoc_projection = shaderInstanceGetUniformLocation(skybox_program.vertexShader, "projection");
	skybox_uLoc_modelView  = shaderInstanceGetUniformLocation(skybox_program.vertexShader, "modelView");

	mixed_uLoc_mvp = shaderInstanceGetUniformLocation(mixed_program.vertexShader, "mvp");
	mixed_uLoc_chunkOffset = shaderInstanceGetUniformLocation(mixed_program.vertexShader, "chunk_offset");
	mixed_uLoc_isLODChunk = shaderInstanceGetUniformLocation(mixed_program.geometryShader, "isLODChunk");
	mixed_uLoc_skip_x_plus = shaderInstanceGetUniformLocation(mixed_program.geometryShader, "skip_x_plus_face");
	mixed_uLoc_skip_x_minus = shaderInstanceGetUniformLocation(mixed_program.geometryShader, "skip_x_minus_face");
	mixed_uLoc_skip_z_plus = shaderInstanceGetUniformLocation(mixed_program.geometryShader, "skip_z_plus_face");
	mixed_uLoc_skip_z_minus = shaderInstanceGetUniformLocation(mixed_program.geometryShader, "skip_z_minus_face");


	per_face_uLoc_mvp = shaderInstanceGetUniformLocation(per_face_program.vertexShader, "mvp");
	per_face_uLoc_chunk_offset = shaderInstanceGetUniformLocation(per_face_program.vertexShader, "chunk_offset");
	per_face_uLoc_draw_top_uvs_offset = shaderInstanceGetUniformLocation(per_face_program.geometryShader, "draw_top_uvs");
	per_face_uLoc_is_lod_chunk_offset = shaderInstanceGetUniformLocation(per_face_program.geometryShader, "is_lod_chunk");

	//tex_tri_proj_uLoc = shaderInstanceGetUniformLocation(tex_tri_program.vertexShader, "projection");

	
	mesh_chunk();

	// Create the VBO (vertex buffer object)
	//printf("lod0 vert list ");
	lod0_vbo_data = mmAlloc(sizeof(lod0_full_vertex_list));
	mmCopy(lod0_vbo_data, lod0_full_vertex_list, sizeof(lod0_full_vertex_list));

	//printf("lod1 vert list ");
	lod1_vbo_data = mmAlloc(sizeof(lod1_full_vertex_list));
	mmCopy(lod1_vbo_data, lod1_full_vertex_list, sizeof(lod1_full_vertex_list));

	//printf("lod1 idx list ");
	lod1_ibo_data = mmAlloc(sizeof(lod1_full_index_list));
	mmCopy(lod1_ibo_data, lod1_full_index_list, sizeof(lod1_full_index_list));

	
	
	// Compute the projection matrix
	Mtx_PerspTilt(&projection, HFOV, C3D_AspectRatioTop, NEAR_PLANE_DIST, FAR_PLANE_DIST, false);

	// for immediate mode stuff..
	Mtx_OrthoTilt(&ortho_projection, 0.0, 400.0, 0.0, 240.0, 0.0, 1.0, true);

	if (!loadTextureFromMem(&atlas_tex, NULL, atlas_t3x, atlas_t3x_size)) {
		svcBreak(USERBREAK_PANIC);
	}

	if (!loadTextureFromMem(&skybox_tex, &skybox_cube, skybox_t3x, skybox_t3x_size)) {
		svcBreak(USERBREAK_PANIC);
	}

	C3D_TexSetFilter(&atlas_tex, GPU_NEAREST, GPU_NEAREST);
	C3D_TexSetFilter(&skybox_tex, GPU_LINEAR, GPU_LINEAR);

	C3D_TexSetFilterMipmap(&atlas_tex, GPU_LINEAR);

	C3D_TexSetWrap(&atlas_tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
	C3D_TexSetWrap(&skybox_tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);

	C3D_TexBind(0, &skybox_tex);
	C3D_TexBind(1, &atlas_tex);


	float near = 0.1f;
	float far  = FAR_PLANE_DIST;
	float values[129];


	for (int i = 0; i <= 128; i ++)
	{	

		float c   = FogLut_CalcZ(i / 128.0f, 0.001, far);
		values[i] = GetFogValue(c);
	}
	//printf("max C %f min C %f\n", maxC, minC);
	//ApplyFog(values);
	float data[256];

	for (int i = 0; i <= 128; i ++)
	{
		float val = values[i];
		if (i < 128) data[i]       = val;
		if (i > 0)   data[i + 127] = val - data[i-1];
	}


	static C3D_FogLut fog_lut;

	//FogLut_FromArray(&fog_lut, data);
	//C3D_FogGasMode(GPU_FOG,  GPU_PLAIN_DENSITY, false);
	//C3D_FogColor(0x00FFdcca); //CLEAR_COLOR);
	//C3D_FogLutBind(&fog_lut);

}

static float angleX = 0.0f, angleY = 180.0f, angleZ = 0.0f;
float camX = 0.0f;
float camY = 20.0f;
float camZ = 0.0f;

float lerp(float a, float b, float f) 
{
    return (a * (1.0 - f)) + (b * f);
}



typedef struct {
	int x;
	int y;
} lod_el;

typedef struct {
	int lod_level;
	int num_els;
	lod_el els[16];
} lod_list;



void set_lod0_attr_info() {
	// Configure attributes for use with the vertex shader
	C3D_AttrInfo* attrInfo = C3D_GetAttrInfo();
	AttrInfo_Init(attrInfo);
	
	AttrInfo_AddLoader(attrInfo, 0, GPU_UNSIGNED_BYTE, 3); // v0=position
	AttrInfo_AddLoader(attrInfo, 1, GPU_UNSIGNED_BYTE, 1); // v1=material
}

void set_lod1_attr_info() {
	// Configure attributes for use with the vertex shader
	C3D_AttrInfo* attrInfo = C3D_GetAttrInfo();
	AttrInfo_Init(attrInfo);
	
	AttrInfo_AddLoader(attrInfo, 0, GPU_UNSIGNED_BYTE, 3); // v0=position
	AttrInfo_AddLoader(attrInfo, 1, GPU_UNSIGNED_BYTE, 3); // v1=color
}


void bind_lod0_vbo() {
	// Configure buffers
	C3D_BufInfo* bufInfo = C3D_GetBufInfo();
	BufInfo_Init(bufInfo);
	BufInfo_Add(bufInfo, lod0_vbo_data, sizeof(lod0_vertex), 2, 0x10);
}

void bind_lod1_vbo() {
	// Configure buffers
	C3D_BufInfo* bufInfo = C3D_GetBufInfo();
	BufInfo_Init(bufInfo);
	BufInfo_Add(bufInfo, lod1_vbo_data, sizeof(lod1_vertex), 2, 0x10);
}

void set_lod0_texenv() {
	C3D_TexEnv* env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE1, 0, 0);
	C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
}
void set_lod1_texenv() {
	C3D_TexEnv* env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, 0, 0); // GPU_PRIMARY_COLOR
	C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
}

// loop over LOD1 chunks
// if center is within a certain distance, draw four LOD0 chunks instead




int draw_as_lod1(float cam_x, float cam_y, float cam_z, float chunk_center_x, float chunk_center_y, float chunk_center_z) {
	float dx = chunk_center_x-cam_x;
	float dy = chunk_center_y-cam_y;
	float dz = chunk_center_z-cam_z;
	float dist = sqrtf(dx*dx+dy*dy+dz*dz);
	return LOD1_SZ <= dist;
}

int draw_as_lod0(float cam_x, float cam_y, float cam_z, float chunk_center_x, float chunk_center_y, float chunk_center_z) {
	float dx = chunk_center_x-cam_x;
	float dy = chunk_center_y-cam_y;
	float dz = chunk_center_z-cam_z;
	return sqrtf(dx*dx+dz*dz+dy*dy) < LOD1_SZ;
}



int lod2_table[4]; // 2 -> draw as a single LOD2, else, check 4 LOD1 table children
int lod1_table[16]; // 1 -> draw as a single LOD1, else draw as 4 LOD0
// each element in the lod table represents what granularity it's "parent" 128x128 LOD2 chunk is broken up into.
typedef enum {
	OFF_LEFT =  0b000001,
	OFF_RIGHT = 0b000010,
	OFF_TOP =   0b000100,
	OFF_BOT =   0b001000,
	OFF_NEAR =  0b010000,
	OFF_FAR =   0b100000
} clip_code;

int aabb_on_screen_clip_space(const C3D_FVec min_vec, const C3D_FVec max_vec, const C3D_Mtx *matrix) {
	u8 full_bmp = 0b11111111;
	C3D_FVec dvec = FVec3_Subtract(max_vec, min_vec);


	for(int z = 0; z < 2; z++) {
		for(int y = 0 ; y < 2 ; y++) {
			for(float x = 0 ; x < 2; x++) {
			C3D_FVec corner = Mtx_MultiplyFVec4(
				matrix, 
				FVec4_New(min_vec.x + (x*dvec.x), min_vec.y + (y*dvec.y), min_vec.z + (z*dvec.z),1.0f));
				u8 bmp = ((corner.x < -corner.w) ? OFF_LEFT : 0);
				bmp |= ((corner.x > corner.w) ? OFF_RIGHT : 0);
				bmp |= ((corner.y < -corner.w) ? OFF_TOP : 0);
				bmp |= ((corner.y > corner.w) ? OFF_BOT : 0);
				bmp |= ((corner.z < -corner.w) ? OFF_NEAR : 0);
				bmp |= ((corner.z > corner.w ) ? OFF_FAR : 0);
				full_bmp &= bmp;
				// we can short circuit some multiplies this way
				if(full_bmp == 0) { return 1; }
			}
		}
	}
	return 0;
	//return full_bmp == 0;
}


typedef struct { float x,y,z; } Vec3;
typedef struct { Vec3 n; float d; } Plane;
typedef struct { float m[4][4]; } Mat4;


static void sceneRender(void) {
	// Calculate the modelView matrix
	Mtx_Identity(&modelView);
	Mtx_RotateX(&modelView, -angleX, true);
	Mtx_RotateY(&modelView, -angleY, true);
	Mtx_RotateZ(&modelView, -angleZ, true);
	
	Mtx_Translate(&modelView, -camX, -camY, -camZ, true); 

	// Rotate the cube each frame
	//angleX += M_PI / 180;
	//angleY += M_PI / 180;

	Mtx_Multiply(&mvpMatrix, &projection, &modelView);

	int mvp_offset, chunk_off_offset;
	int num_verts = 0; // default to 0 in case of LOD2 somehow
	void* index_buffer = NULL;


	memset(lod1_table, -1, sizeof(lod1_table));

	for(int z = 0; z < 4; z++) {
		for(int x = 0; x < 4; x++) {
			float min_x = x*LOD1_SZ;
			float min_y = 0.0f;
			float min_z = z*LOD1_SZ;

			//float center_x = min_x + (LOD1_SZ/2.0f);
			//float center_y = min_y + (LOD1_SZ/2.0f);
			//float center_z = min_z + (LOD1_SZ/2.0f);

			float max_x = min_x+LOD1_SZ;
			float max_y = min_y+LOD1_SZ;
			float max_z = min_z+LOD1_SZ;

			//float dx = center_x-camX;
			//float dy = center_y-camY;
			//float dz = center_z-camZ;

			// these are minimum/maximum in absolute terms, not relative to the camera
			float min_dx = fabsf(min_x - camX);
			float min_dy = fabsf(min_y - camY);
			float min_dz = fabsf(min_z - camZ);
			float max_dx = fabsf(max_x - camX);
			float max_dy = fabsf(max_y - camY);
			float max_dz = fabsf(max_z - camZ);

			// find the closest and furthest corners and the distances to them
			float min_rel_dx = MIN(min_dx, max_dx);
			float min_rel_dy = MIN(min_dy, max_dy);
			float min_rel_dz = MIN(min_dz, max_dz);
			//float max_rel_dx = MAX(min_dx, max_dx);
			//float max_rel_dy = MAX(min_dy, max_dy);
			//float max_rel_dz = MAX(min_dz, max_dz);

			//float center_dist = sqrtf(dx*dx + dy*dy + dz*dz);
			float min_rel_dist = sqrtf(min_rel_dx*min_rel_dx + min_rel_dy*min_rel_dy + min_rel_dz*min_rel_dz);
			//float max_rel_dist = sqrtf(max_rel_dx*max_rel_dx + max_rel_dy*max_rel_dy + max_rel_dz*max_rel_dz);

			if(min_rel_dist > FAR_PLANE_DIST) {
				//printf("skipping LOD2 chunk and children due to far plane\n");
				lod2_table[z*2+x] = -1; // DO NOT DRAW
				for(int zz = z*2; zz < z*2+2; zz++) {
					for(int xx = x*2; xx < x*2+2; xx++) {
						lod1_table[zz*4+xx] = -1;
					}
				}
				continue;
			}

			// previously was using center d ist, min dist might be better
			// to prevent blocky geometry from getting too close
			if(min_rel_dist >= LOD1_SZ) {
				lod1_table[z*4+x] = 1; // 3 is LOD0 but with just textures
			} else {
				lod1_table[z*4+x] = 0;
				
			}
			// TEMPORARILY FORCING LOD1
			//lod1_table[z*4+x] = 1;

		}
	}



	C3D_BindProgram(&per_face_program);
	mvp_offset = per_face_uLoc_mvp;
	chunk_off_offset = per_face_uLoc_chunk_offset;
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, mvp_offset, &mvpMatrix);


	/*
	
		draw LOD0 chunks

	*/

	set_lod0_attr_info();
	bind_lod0_vbo();
	set_lod0_texenv();
	C3D_BoolUnifSet(GPU_GEOMETRY_SHADER, per_face_uLoc_is_lod_chunk_offset, false);


	//printf("cam xyz: %.2f %.2f %.2f\n", camX, camY, camZ);
	//printf("%i%i\n", lod2_table[0], lod2_table[1]);
	//printf("%i%i\n", lod2_table[2], lod2_table[3]);

	//printf("%i%i%i%i\n", lod1_table[0], lod1_table[1], lod1_table[2], lod1_table[3]);
	//printf("%i%i%i%i\n", lod1_table[4], lod1_table[5], lod1_table[6], lod1_table[7]);
	//printf("%i%i%i%i\n", lod1_table[8], lod1_table[9], lod1_table[10], lod1_table[11]);
	//printf("%i%i%i%i\n", lod1_table[12], lod1_table[13], lod1_table[14], lod1_table[15]);
	
	// loop over the 8 corners of the AABB


	int verts = 0;


	int total_lod0_meshes = 0;
	int drawn_lod0_meshes = 0;
	for(int z = 0; z < 4; z++) {
		for(int x = 0; x < 4; x++) {
			if (lod1_table[z*4+x] == 0) {
				set_lod0_texenv();
			} else {
				continue;
			}

			float min_x = x * LOD1_SZ;
			float min_y = 0.0f;
			float min_z = z * LOD1_SZ;

			
			total_lod0_meshes += 4;

			/*
			int idx = 0;
			// 3 * 3 * 3
			for(int y = min_y; y <= min_y+LOD0_SZ; y += LOD0_SZ) {
				for(int z = min_z; z <= min_z+LOD1_SZ; z += LOD0_SZ) {
					for(int x = min_x; x <= min_x+LOD1_SZ; x += LOD0_SZ) {
						C3D_FVec corner = Mtx_MultiplyFVec4(&mvpMatrix, FVec4_New(x, y, z, 1.0));
						u8 bmp = 0;
						bmp |= corner.x < -corner.w ? OFF_LEFT : 0;
						bmp |= corner.x > corner.w ? OFF_RIGHT : 0;
						bmp |= corner.y < -corner.w ? OFF_TOP : 0;
						bmp |= corner.y > corner.w ? OFF_BOT : 0;
						bmp |= corner.z < -corner.w ? OFF_NEAR : 0;
						bmp |= corner.z > corner.w ? OFF_FAR : 0;
						clip_results[idx++] = bmp;

					}
				}
			}
			*/

			for(int z = 0; z <= 1; z++) {
				for(int x = 0; x <= 1; x++) {
					float cur_aabb_min_x = min_x + x*LOD0_SZ;
					float cur_aabb_min_y = min_y;
					float cur_aabb_min_z = min_z + z*LOD0_SZ;
					float cur_aabb_max_x = cur_aabb_min_x + LOD0_SZ;
					float cur_aabb_max_y = cur_aabb_min_y + LOD0_SZ;
					float cur_aabb_max_z = cur_aabb_min_z + LOD0_SZ;

					//u8 clip = (clip_results[z*3+x] & clip_results[z*3+x+1] &
					//		   clip_results[(z+1)*3+x] & clip_results[(z+1)*3+x+1] &
					//		   clip_results[z*3+x+9] & clip_results[z*3+x+1+9] &
					//		   clip_results[(z+1)*3+x+9] & clip_results[(z+1*2+x+1+9)]);

					//if (clip) { continue; }
					C3D_FVec min_vec = FVec4_New(cur_aabb_min_x, cur_aabb_min_y, cur_aabb_min_z, 1.0f);
					C3D_FVec max_vec = FVec4_New(cur_aabb_max_x, cur_aabb_max_y, cur_aabb_max_z, 1.0f);

					if(!aabb_on_screen_clip_space(min_vec, max_vec, &mvpMatrix)) {
					   //!aabb_on_screen_world_space(min_vec, max_vec, HFOV, VFOV, NEAR_PLANE_DIST, FAR_PLANE_DIST, &modelView, (min_vec.x == 0 && min_vec.y == 0 && min_vec.z == 0))) {
						continue;
					}

					drawn_lod0_meshes++;
					
					C3D_FVUnifSet(GPU_VERTEX_SHADER, chunk_off_offset, cur_aabb_min_x, cur_aabb_min_y, cur_aabb_min_z, 0.0f);
					C3D_BoolUnifSet(GPU_GEOMETRY_SHADER, per_face_uLoc_draw_top_uvs_offset, false);
					if(cur_aabb_min_z <= camZ) {
						C3D_DrawElements(GPU_GEOMETRY_PRIM, lod0_indexes_per_face[FRONT], C3D_UNSIGNED_SHORT, (void*)(lod0_per_face_index_lists[FRONT]));
						verts += lod0_indexes_per_face[FRONT];
					}
					if(cur_aabb_max_z  >= camZ) {
						C3D_DrawElements(GPU_GEOMETRY_PRIM, lod0_indexes_per_face[BACK], C3D_UNSIGNED_SHORT, (void*)(lod0_per_face_index_lists[BACK]));
						verts += lod0_indexes_per_face[BACK];
					}
					if(cur_aabb_max_x >= camX) {
						C3D_DrawElements(GPU_GEOMETRY_PRIM, lod0_indexes_per_face[LEFT], C3D_UNSIGNED_SHORT, lod0_per_face_index_lists[LEFT]);
						verts += lod0_indexes_per_face[LEFT];
					}
					if(cur_aabb_min_x <= camX) {
						C3D_DrawElements(GPU_GEOMETRY_PRIM, lod0_indexes_per_face[RIGHT], C3D_UNSIGNED_SHORT, lod0_per_face_index_lists[RIGHT]);
						verts += lod0_indexes_per_face[RIGHT];
					}
					C3D_BoolUnifSet(GPU_GEOMETRY_SHADER, per_face_uLoc_draw_top_uvs_offset, true);
					C3D_DrawElements(GPU_GEOMETRY_PRIM, lod0_indexes_per_face[TOP], C3D_UNSIGNED_SHORT, (void*)(lod0_per_face_index_lists[TOP]));
					C3D_DrawElements(GPU_GEOMETRY_PRIM, lod0_indexes_per_face[BOTTOM], C3D_UNSIGNED_SHORT, lod0_per_face_index_lists[BOTTOM]);
					verts += lod0_indexes_per_face[TOP];
					verts += lod0_indexes_per_face[BOTTOM];
					
				}
			}

		}
	}

	/*
	
		draw LOD1 chunks
		
	*/

	
	//C3D_BindProgram(&mixed_program);
	//mvp_offset = mixed_uLoc_mvp;
	//chunk_off_offset = mixed_uLoc_chunkOffset;
	//C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, mvp_offset, &mvpMatrix);


	bind_lod1_vbo();
	set_lod1_attr_info();
	set_lod1_texenv();
	//C3D_BoolUnifSet(GPU_GEOMETRY_SHADER, mixed_uLoc_isLODChunk, true);
	C3D_BoolUnifSet(GPU_GEOMETRY_SHADER, per_face_uLoc_is_lod_chunk_offset, true);

	
	int drawn_lod1_meshes = 0;
	int total_lod1_meshes = 0;
	for(int z = 0; z < 4; z++) {
		for(int x = 0; x < 4; x++) {
			
			if(lod1_table[z*4+x] != 1) { continue; }
			float min_x = x*LOD1_SZ;
			float min_y = 0;
			float min_z = z*LOD1_SZ;

			float max_x = min_x + LOD1_SZ;
			float max_y = min_y + LOD0_SZ;
			float max_z = min_z + LOD1_SZ;
			
			// loop over the 8 corners of the AABB
			total_lod1_meshes += 1;
			if(!aabb_on_screen_clip_space(
				FVec3_New(min_x, min_y, min_z),FVec3_New(max_x, max_y, max_z),&mvpMatrix
			)) { 
				continue;
			}
	
			drawn_lod1_meshes += 1;
			
			C3D_FVUnifSet(GPU_VERTEX_SHADER, chunk_off_offset, min_x, min_y, min_z, 0.0f);

			if(min_z <= camZ) {
				C3D_DrawElements(GPU_GEOMETRY_PRIM, lod1_indexes_per_face[FRONT], C3D_UNSIGNED_SHORT, (void*)(lod1_per_face_index_lists[FRONT]));
				verts += lod1_indexes_per_face[FRONT];
			}
			if(max_z >= camZ) {
				C3D_DrawElements(GPU_GEOMETRY_PRIM, lod1_indexes_per_face[BACK], C3D_UNSIGNED_SHORT, (void*)(lod1_per_face_index_lists[BACK]));
				verts += lod1_indexes_per_face[BACK];
			}
			if(max_x >= camX) {
				C3D_DrawElements(GPU_GEOMETRY_PRIM, lod1_indexes_per_face[LEFT], C3D_UNSIGNED_SHORT, lod1_per_face_index_lists[LEFT]);
				verts += lod1_indexes_per_face[LEFT];
			}
			if(min_x <= camX) {
				C3D_DrawElements(GPU_GEOMETRY_PRIM, lod1_indexes_per_face[RIGHT], C3D_UNSIGNED_SHORT, lod1_per_face_index_lists[RIGHT]);
				verts += lod1_indexes_per_face[RIGHT];
			}

			C3D_DrawElements(GPU_GEOMETRY_PRIM, lod1_indexes_per_face[TOP], C3D_UNSIGNED_SHORT, (void*)(lod1_per_face_index_lists[TOP]));
			C3D_DrawElements(GPU_GEOMETRY_PRIM, lod1_indexes_per_face[BOTTOM], C3D_UNSIGNED_SHORT, lod1_per_face_index_lists[BOTTOM]);
			verts += lod1_indexes_per_face[TOP];
			verts += lod1_indexes_per_face[BOTTOM];
			//verts += num_verts;
		}
	}

	

	//printf("%i LOD0 meshes, LOD1 meshes %i LOD2 meshes\n", )
	//printf("LOD0: %i/%i LOD1: %i/%i LOD2: %i/%i\n", 
	//	drawn_lod0_meshes, total_lod0_meshes, 
	//	drawn_lod1_meshes, total_lod1_meshes,
	//	drawn_lod2_meshes, total_lod2_meshes);
	printf("polys %i\n", verts/3);
	//(lod0_num_vertexes_to_draw*drawn_lod0_meshes +lod1_num_vertexes_to_draw*drawn_lod1_meshes+lod2_num_vertexes_to_draw*drawn_lod2_meshes)/3);

}


static void skyboxRender() {
	
	C3D_BindProgram(&skybox_program);

	C3D_AttrInfo* attrInfo = C3D_GetAttrInfo();
	AttrInfo_Init(attrInfo);
	AttrInfo_AddLoader(attrInfo, 0, GPU_UNSIGNED_BYTE, 3); // v0=position
	
	
	C3D_BufInfo* bufInfo = C3D_GetBufInfo();
	BufInfo_Init(bufInfo);
	BufInfo_Add(bufInfo, skybox_vbo, sizeof(u8)*3, 1, 0x0);

	C3D_TexEnv* env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, 0, 0);
	C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
	C3D_TexEnvInit(C3D_GetTexEnv(2));

	
	C3D_Mtx modelView;
	Mtx_Identity(&modelView);
	Mtx_RotateX(&modelView, -angleX, true);
	Mtx_RotateY(&modelView, -angleY, true);

	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, skybox_uLoc_projection, &projection);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, skybox_uLoc_modelView,  &modelView);
	
	C3D_DrawElements(GPU_TRIANGLE_STRIP, 14, C3D_UNSIGNED_BYTE, skybox_ibo);

	C3D_ImmDrawEnd();
}



static void sceneExit(void)
{
	// Free the texture
	C3D_TexDelete(&atlas_tex);

	// Free the VBO
	mmFree(lod0_vbo_data);
	mmFree(lod1_vbo_data);
	mmFree(lod0_ibo_data);
	mmFree(lod1_ibo_data);

	// Free the shader program
	shaderProgramFree(&lod0_program);
	DVLB_Free(lod0_program_dvlb);
	shaderProgramFree(&lod1_program);
	DVLB_Free(lod1_program_dvlb);
}

 

int main() {

	// Initialize graphics
	gfxInitDefault();
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);

	consoleInit(GFX_BOTTOM, NULL);

	printf("mem type %i\n", osGetApplicationMemType());
	//printf("linear heap size %i\n", envGetLinearHeapSize());
	printf("heap size %i\n", envGetHeapSize());
	mmInitAlloc();

	//C3D_Tex renderTexture;
	// Initialize the render target
	//loadTextureFromMem(&renderTexture, NULL, render_target_tex_t3x, render_target_tex_t3x_size);

	//C3D_RenderTarget* texRenderTarget = C3D_RenderTargetCreateFromTex(&renderTexture, GPU_TEXFACE_2D, 0, GPU_RB_DEPTH24_STENCIL8);

	
	C3D_RenderTarget* screenTarget = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
	C3D_RenderTargetSetOutput(screenTarget, GSP_SCREEN_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);

	//C3D_EarlyDepthTest(true, GPU_EARLYDEPTH_LESS, 0xFFFFFF);

	
	// Initialize the scene
	sceneInit();


	// Main loop

	s64 last_frame_ms = 16;


	// set up shaders, target, and texenv to draw render texture to screen (with slight scaling)

	u64 msStart = osGetTime();

	while (aptMainLoop())
	{
		hidScanInput();

		circlePosition cpos;
		hidCircleRead(&cpos);

		// Respond to user input
		u32 kDown = hidKeysDown();
		u32 kHeld = hidKeysHeld();
		if (kDown & KEY_START) {
			break; // break in order to return to hbmenu
		}

		if (kHeld & KEY_CSTICK_LEFT) {
			angleY += C3D_AngleFromDegrees(2.0f);
		} else if(kHeld & KEY_CSTICK_RIGHT) {
			angleY -= C3D_AngleFromDegrees(2.0f);
		}
		if (kHeld & KEY_CSTICK_UP) {
			angleX += C3D_AngleFromDegrees(2.0f);
		} else if(kHeld & KEY_CSTICK_DOWN) {
			angleX -= C3D_AngleFromDegrees(2.0f);
		}

		if (kHeld & KEY_L) {
			angleZ += C3D_AngleFromDegrees(2.0f);
		} else if (kHeld & KEY_R) {
			angleZ -= C3D_AngleFromDegrees(2.0f);
		}

		
		if (cpos.dx < 0) {
			float dx = cpos.dx / -154.0f;
			dx = dx > 1.0f ? 1.0f : dx;
			angleY += C3D_AngleFromDegrees(lerp(0, 1.0f, dx));
		} else if(cpos.dx > 0) {
			float dx = cpos.dx / 154.0f;
			dx = dx > 1.0f ? 1.0f : dx;
			angleY -= C3D_AngleFromDegrees(lerp(0, 1.0f, dx));
		}

		// Compute forward vector from angles
		float cosPitch = cosf(angleX); //*6.28f/360.0f);
		float sinPitch = sinf(angleX);
		float cosYaw   = cosf(angleY);
		float sinYaw   = sinf(angleY);


		float forwardX = -sinYaw * cosPitch;
		float forwardY = sinPitch;
		float forwardZ = -cosYaw * cosPitch;
		float speed = 0.15f; //last_frame_ms/80.0f; //0.2f at 60fps, scale for larger frame t imes


		float lerpSpeed = 0.0f;
		if (cpos.dy < 0) {
			float dy = cpos.dy / -154.0f;
			dy = dy > 1.0f ? 1.0f : dy;
			lerpSpeed = -lerp(0, speed, dy);
		} else if (cpos.dy > 0) {
			float dy = cpos.dy / 154.0f;
			dy = dy > 1.0f ? 1.0f : dy;
			lerpSpeed = lerp(0, speed, dy);
		}
		//lerpSpeed = CLAMP(lerpSpeed, -10.0f, 10.0f);

		camX += forwardX * lerpSpeed;
		camY += forwardY * lerpSpeed;
		camZ += forwardZ * lerpSpeed;



		// Render the scene
		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

			//C3D_EarlyDepthTest(true, GPU_GREATER, 0);
			C3D_RenderTargetClear(screenTarget, C3D_CLEAR_ALL, CLEAR_COLOR, 0.0);
			C3D_FrameDrawOn(screenTarget);

			//GPUCMD_AddMaskedWrite(GPUREG_FRAMEBUFFER_BLOCK32, 0x1, 1);
			//C3D_FrameBufClear(&texRenderTarget->frameBuf, C3D_CLEAR_ALL, CLEAR_COLOR, 0);
			//C3D_FrameDrawOn(texRenderTarget);



			C3D_CullFace(GPU_CULL_BACK_CCW);
			C3D_DepthTest(false, GPU_EQUAL, GPU_WRITE_COLOR);
			skyboxRender();

			//C3D_CullFace(GPU_CULL_NONE);
			C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
			sceneRender();


			/*
			
			GPUCMD_AddMaskedWrite(GPUREG_FRAMEBUFFER_BLOCK32, 0x1, 0);
			//C3D_EarlyDepthTest(false, GPU_GREATER, 0);

		    C3D_DepthTest(false, GPU_GEQUAL, GPU_WRITE_COLOR);
			C3D_BindProgram(&tex_tri_program);

			C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, tex_tri_proj_uLoc, &ortho_projection);

			C3D_TexBind(1, &renderTexture);

			C3D_TexEnv* env = C3D_GetTexEnv(0);
			C3D_TexEnvInit(env);
			C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE1, 0, 0);
			C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);

			C3D_AttrInfo* attrInfo = C3D_GetAttrInfo();
			AttrInfo_Init(attrInfo);
			AttrInfo_AddLoader(attrInfo, 0, GPU_FLOAT, 3); // v0=position
			AttrInfo_AddLoader(attrInfo, 1, GPU_FLOAT, 3); // v1=uv


			C3D_FrameDrawOn(screenTarget);
			

			// top right is 1,0?
			C3D_ImmDrawBegin(GPU_TRIANGLE_STRIP);

				// bottom left
				C3D_ImmSendAttrib(0.0f, 0.0f, 0.1f, 0.0f);
				C3D_ImmSendAttrib(0.0f, 1.0f, 0.0f, 0.0f);
				
				// bottom right
				C3D_ImmSendAttrib(400.0f, 0.0f, 0.1f, 0.0f);
				C3D_ImmSendAttrib(0.0f, 0.0f, 0.0f, 0.0f);     // v1=color

				// top left
				C3D_ImmSendAttrib(0.0f, 240.0f, 0.1f, 0.0f); // v0=position
				C3D_ImmSendAttrib(1.0f, 1.0f, 0.0f, 0.0f);
				
				C3D_ImmSendAttrib(400.0f, 240.0f, 0.1f, 0.0f);
				C3D_ImmSendAttrib(1.0f, 0.0f, 0.0f, 1.0f);
			C3D_ImmDrawEnd();
			
			*/

		C3D_FrameEnd(0);

		u64 msEnd = osGetTime();
		u64 elapsed_ms = msEnd - msStart;
		last_frame_ms = elapsed_ms;
		msStart = msEnd;

		float gpu_time = C3D_GetDrawingTime()*6.0f;
		float cpu_time = C3D_GetProcessingTime()*6.0f;
		float max_time = MAX(gpu_time, cpu_time);
		float fps = 1000.0f/(max_time*16.0f/100.0f);

		printf("cpu %.2f%% gpu %.2f%% %.2f fps\n", cpu_time, gpu_time, fps);
		//printf("angX %.2f angY %.2f cam %.2f,%.2f,%.2f\n", angleX*57.29f, angleY*57.29f, camX, camY, camZ);
		//printf("camx %f\n", camX);
		//printf("%i ms\n", elapsed_ms);
		//msStart = msEnd;
	}

	// Deinitialize the scene
	sceneExit();

	// Deinitialize graphics
	C3D_Fini();
	gfxExit();
	return 0;
}