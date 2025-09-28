#include <3ds.h>
#include <citro3d.h>
#include <math.h>
#include <string.h>

#include "alloc.h"
#include "chunk.h"


chunk chunks[64] = {
    {.blocks = {AIR}},{.blocks = {AIR}},{.blocks = {AIR}},{.blocks = {AIR}},
    {.blocks = {AIR}},{.blocks = {AIR}},{.blocks = {AIR}},{.blocks = {AIR}},
    {.blocks = {AIR}},{.blocks = {AIR}},{.blocks = {AIR}},{.blocks = {AIR}},
    {.blocks = {AIR}},{.blocks = {AIR}},{.blocks = {AIR}},{.blocks = {AIR}},
    {.blocks = {AIR}},{.blocks = {AIR}},{.blocks = {AIR}},{.blocks = {AIR}},
    {.blocks = {AIR}},{.blocks = {AIR}},{.blocks = {AIR}},{.blocks = {AIR}},
    {.blocks = {AIR}},{.blocks = {AIR}},{.blocks = {AIR}},{.blocks = {AIR}},
    {.blocks = {AIR}},{.blocks = {AIR}},{.blocks = {AIR}},{.blocks = {AIR}},
    {.blocks = {AIR}},{.blocks = {AIR}},{.blocks = {AIR}},{.blocks = {AIR}},
    {.blocks = {AIR}},{.blocks = {AIR}},{.blocks = {AIR}},{.blocks = {AIR}},
    {.blocks = {AIR}},{.blocks = {AIR}},{.blocks = {AIR}},{.blocks = {AIR}},
    {.blocks = {AIR}},{.blocks = {AIR}},{.blocks = {AIR}},{.blocks = {AIR}},
    {.blocks = {AIR}},{.blocks = {AIR}},{.blocks = {AIR}},{.blocks = {AIR}},
    {.blocks = {AIR}},{.blocks = {AIR}},{.blocks = {AIR}},{.blocks = {AIR}},
    {.blocks = {AIR}},{.blocks = {AIR}},{.blocks = {AIR}},{.blocks = {AIR}},
    {.blocks = {AIR}},{.blocks = {AIR}},{.blocks = {AIR}},{.blocks = {AIR}},
};



lod0_vertex lod0_full_vertex_list[CHUNK_VERTEX_SIZE*CHUNK_VERTEX_SIZE*CHUNK_VERTEX_SIZE];
lod1_vertex lod1_full_vertex_list[CHUNK_VERTEX_SIZE*LOD1_CHUNK_Y_VERTEX_SIZE*CHUNK_VERTEX_SIZE];


u16 *lod0_per_face_index_lists[5];
u16 *lod1_per_face_index_lists[5];
u16 lod0_indexes_per_face[5] = {0,0,0,0,0};
u16 lod1_indexes_per_face[5] = {0,0,0,0,0};

#define CHUNK_SIZE_BITS 6

// 6 bits x,y,z
int get_vertex_idx(int x, int y, int z) {
	return (x*CHUNK_VERTEX_SIZE*CHUNK_VERTEX_SIZE) + (y*CHUNK_VERTEX_SIZE)+z;
	return (x<<12)|(y<<6)|z;
	int low_z = z & 0b11;
	int low_y = y & 0b11;
	int low_x = x & 0b11;
	int low_idx = (low_z<<4)|(low_y<<2)|low_x;
	int high_z = (z & ~0b11)>>2; // 4 bits
	int high_y = (y & ~0b11)>>2; // 4 bits
	int high_x = (x & ~0b11)>>2; // 4 bits
	return (((high_z << ((CHUNK_SIZE_BITS-2)*2)) | (high_y << (CHUNK_SIZE_BITS-2)) | (high_x)) << 6) | low_idx;
}

// 6 bits x,z ; 5 bits y (0->16)
int get_lod1_vertex_idx(int x, int y, int z) {
	return (x*LOD1_CHUNK_Y_VERTEX_SIZE*CHUNK_VERTEX_SIZE) + (y*CHUNK_VERTEX_SIZE)+z;
	int low_z = z & 0b11; // 2 bits 
	int low_y = y & 0b11; // 2 bits 
	int low_x = x & 0b11; // 2 bits
	int low_idx = (low_z<<4)|(low_y<<2)|low_x;
	int high_z = (z & ~0b11)>>2; // 4 bits 
	int high_y = (y & ~0b11)>>2; // 3 bits
	int high_x = (x & ~0b11)>>2; // 4 bits 
	return (((high_z << 7) | (high_y << 4) | (high_x)) << 6) | low_idx;
}




//lod0_vertex chunks[20][CHUNK_VERTEX_SIZE];




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
	int center_z = (CHUNK_SIZE+1)/2;				
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

	int front_faces = 0;
	int back_faces = 0;
	int top_bot_faces = 0;
	int left_faces = 0;
	int right_faces = 0;
	
	// TODO: allocate only half?
	// NOPE, because of transparent voxels, we can't do that
	lod0_per_face_index_lists[FRONT] = linearAlloc(sizeof(u16)*CHUNK_SIZE*CHUNK_SIZE*CHUNK_SIZE*5);
	lod0_per_face_index_lists[BACK] = linearAlloc(sizeof(u16)*CHUNK_SIZE*CHUNK_SIZE*CHUNK_SIZE*5);
	lod0_per_face_index_lists[TOP_BOTTOM] = linearAlloc(sizeof(u16)*CHUNK_SIZE*CHUNK_SIZE*CHUNK_SIZE*5);
	lod0_per_face_index_lists[LEFT] = linearAlloc(sizeof(u16)*CHUNK_SIZE*CHUNK_SIZE*CHUNK_SIZE*5);
	lod0_per_face_index_lists[RIGHT] = linearAlloc(sizeof(u16)*CHUNK_SIZE*CHUNK_SIZE*CHUNK_SIZE*5);

	lod1_per_face_index_lists[FRONT] = linearAlloc(sizeof(u16)*CHUNK_SIZE*CHUNK_SIZE/2*CHUNK_SIZE*5);
	lod1_per_face_index_lists[BACK] = linearAlloc(sizeof(u16)*CHUNK_SIZE*CHUNK_SIZE/2*CHUNK_SIZE*5);
	lod1_per_face_index_lists[TOP_BOTTOM] = linearAlloc(sizeof(u16)*CHUNK_SIZE*CHUNK_SIZE/2*CHUNK_SIZE*5);
	lod1_per_face_index_lists[LEFT] = linearAlloc(sizeof(u16)*CHUNK_SIZE*CHUNK_SIZE/2*CHUNK_SIZE*5);
	lod1_per_face_index_lists[RIGHT] = linearAlloc(sizeof(u16)*CHUNK_SIZE*CHUNK_SIZE/2*CHUNK_SIZE*5);
	//return;

	for(int z = 0; z < CHUNK_SIZE; z++) {
		int z_border = (z == 0 || z == CHUNK_SIZE-1);
		for(int y = 0; y < CHUNK_SIZE; y++) {
			for(int x = 0; x < CHUNK_SIZE; x++) {
				int x_border = x == 0 || x == CHUNK_SIZE-1;

				// output the indexes for each voxel
					// - + +
				int v0_idx = get_vertex_idx(x,  y,   z);
				int tex_idx = (x_border || z_border) ? 1 : random_texture();
				// make leaves rarer
				while(tex_idx == LEAVES) {
					tex_idx = random_texture();
				}

				lod0_full_vertex_list[v0_idx].material = tex_idx;
			}
		}
	}

	// we need a visibility flood fill pass, most likely


	for(int z = 0; z < CHUNK_SIZE; z++) {
		for(int y = 0; y < CHUNK_SIZE; y++) {
			for(int x = 0; x < CHUNK_SIZE; x++) {

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
				int tex_idx = lod0_full_vertex_list[v0_idx].material;

				int this_height = sharp_heightmap_table[z*CHUNK_SIZE+x];
				int left_height = x == 0 ? -1 : sharp_heightmap_table[z*CHUNK_SIZE+x-1];
				int right_height = x == CHUNK_SIZE-1 ? -1 : sharp_heightmap_table[z*CHUNK_SIZE+x+1];
				int back_height = z == 0 ? -1 : sharp_heightmap_table[(z-1)*CHUNK_SIZE+x];
				int front_height = z == CHUNK_SIZE-1 ? -1 : sharp_heightmap_table[(z+1)*CHUNK_SIZE+x];
				int left_exposed = (y<=this_height) && y > left_height;
				int right_exposed = (y<=this_height) && y > right_height;
				int front_exposed = (y<=this_height) && y > front_height;
				int back_exposed = (y<=this_height) && y > back_height;
				int top_exposed = y == this_height;

				int bot_exposed = y == 0;

				// check for leaves

				// to reduce extraneous added faces do to these tests
				// we need to calculate actual visilibility for each face in a previous pass
				// and then check it here

			
				if(!front_exposed && z < CHUNK_SIZE-1 && (y <= this_height) && lod0_full_vertex_list[get_vertex_idx(x,y,z+1)].material == LEAVES) {
					front_exposed = 1;
				}
				if(!back_exposed && z > 0 && (y <= this_height) && lod0_full_vertex_list[get_vertex_idx(x,y,z-1)].material == LEAVES) {
					back_exposed = 1;
				}
				if(!left_exposed && x > 0 && (y <= this_height) && lod0_full_vertex_list[get_vertex_idx(x-1,y,z)].material == LEAVES) {
					left_exposed = 1;
				}
				if(!right_exposed && x < CHUNK_SIZE-1 && (y <= this_height) && lod0_full_vertex_list[get_vertex_idx(x+1,y,z)].material == LEAVES) {
					right_exposed = 1;
				}

				if(!top_exposed && y < CHUNK_SIZE-1 && (y <= this_height) && lod0_full_vertex_list[get_vertex_idx(x,y+1,z)].material == LEAVES) {
						top_exposed = 1;
				}
				if(!bot_exposed && y > 0 && (y <= this_height) && lod0_full_vertex_list[get_vertex_idx(x,y-1,z)].material == LEAVES) {
					bot_exposed = 1;
				}

				
				if(back_exposed) {
					lod0_per_face_index_lists[BACK][back_faces++] = v0_idx;
					lod0_per_face_index_lists[BACK][back_faces++] = v1_idx;
					lod0_per_face_index_lists[BACK][back_faces++] = v2_idx;
					lod0_per_face_index_lists[BACK][back_faces++] = v3_idx;
					lod0_per_face_index_lists[BACK][back_faces++] = v0_idx;

					// generate back faces w/ opposide winding order
					// this is OOF-y
					if(tex_idx == LEAVES) {
						lod0_per_face_index_lists[BACK][back_faces++] = v1_idx;
						lod0_per_face_index_lists[BACK][back_faces++] = v0_idx;
						lod0_per_face_index_lists[BACK][back_faces++] = v3_idx;
						lod0_per_face_index_lists[BACK][back_faces++] = v2_idx;
						lod0_per_face_index_lists[BACK][back_faces++] = v0_idx;
					}
				}

				if(front_exposed) {
					lod0_per_face_index_lists[FRONT][front_faces++] = v5_idx;
					lod0_per_face_index_lists[FRONT][front_faces++] = v4_idx;
					lod0_per_face_index_lists[FRONT][front_faces++] = v7_idx;
					lod0_per_face_index_lists[FRONT][front_faces++] = v6_idx; 
					lod0_per_face_index_lists[FRONT][front_faces++] = v0_idx;
					if(tex_idx == LEAVES) {
						lod0_per_face_index_lists[FRONT][front_faces++] = v4_idx;
						lod0_per_face_index_lists[FRONT][front_faces++] = v5_idx;
						lod0_per_face_index_lists[FRONT][front_faces++] = v6_idx;
						lod0_per_face_index_lists[FRONT][front_faces++] = v7_idx; 
						lod0_per_face_index_lists[FRONT][front_faces++] = v0_idx;
					}
				}

				if(top_exposed) {
					lod0_per_face_index_lists[TOP_BOTTOM][top_bot_faces++] = v3_idx;
					lod0_per_face_index_lists[TOP_BOTTOM][top_bot_faces++] = v7_idx;
					lod0_per_face_index_lists[TOP_BOTTOM][top_bot_faces++] = v2_idx;
					lod0_per_face_index_lists[TOP_BOTTOM][top_bot_faces++] = v6_idx;
					lod0_per_face_index_lists[TOP_BOTTOM][top_bot_faces++] = v0_idx;
					if(tex_idx == LEAVES) {
						lod0_per_face_index_lists[TOP_BOTTOM][top_bot_faces++] = v7_idx;
						lod0_per_face_index_lists[TOP_BOTTOM][top_bot_faces++] = v3_idx;
						lod0_per_face_index_lists[TOP_BOTTOM][top_bot_faces++] = v6_idx;
						lod0_per_face_index_lists[TOP_BOTTOM][top_bot_faces++] = v2_idx;
						lod0_per_face_index_lists[TOP_BOTTOM][top_bot_faces++] = v0_idx;
					}
				}
				if(bot_exposed) {
					lod0_per_face_index_lists[TOP_BOTTOM][top_bot_faces++] = v0_idx;
					lod0_per_face_index_lists[TOP_BOTTOM][top_bot_faces++] = v4_idx;
					lod0_per_face_index_lists[TOP_BOTTOM][top_bot_faces++] = v1_idx;
					lod0_per_face_index_lists[TOP_BOTTOM][top_bot_faces++] = v5_idx;
					lod0_per_face_index_lists[TOP_BOTTOM][top_bot_faces++] = v0_idx;
					if(tex_idx == LEAVES) {
						lod0_per_face_index_lists[TOP_BOTTOM][top_bot_faces++] = v4_idx;
						lod0_per_face_index_lists[TOP_BOTTOM][top_bot_faces++] = v0_idx;
						lod0_per_face_index_lists[TOP_BOTTOM][top_bot_faces++] = v5_idx;
						lod0_per_face_index_lists[TOP_BOTTOM][top_bot_faces++] = v1_idx;
						lod0_per_face_index_lists[TOP_BOTTOM][top_bot_faces++] = v0_idx;
					}
				}

				if(left_exposed) {
					lod0_per_face_index_lists[LEFT][left_faces++] = v4_idx;
					lod0_per_face_index_lists[LEFT][left_faces++] = v0_idx;
					lod0_per_face_index_lists[LEFT][left_faces++] = v6_idx;
					lod0_per_face_index_lists[LEFT][left_faces++] = v2_idx;
					lod0_per_face_index_lists[LEFT][left_faces++] = v0_idx;
					if(tex_idx == LEAVES) {
						lod0_per_face_index_lists[LEFT][left_faces++] = v0_idx;
						lod0_per_face_index_lists[LEFT][left_faces++] = v4_idx;
						lod0_per_face_index_lists[LEFT][left_faces++] = v2_idx;
						lod0_per_face_index_lists[LEFT][left_faces++] = v6_idx;
						lod0_per_face_index_lists[LEFT][left_faces++] = v0_idx;
					}
				}
				if(right_exposed) {
					lod0_per_face_index_lists[RIGHT][right_faces++] = v1_idx;
					lod0_per_face_index_lists[RIGHT][right_faces++] = v5_idx;
					lod0_per_face_index_lists[RIGHT][right_faces++] = v3_idx;
					lod0_per_face_index_lists[RIGHT][right_faces++] = v7_idx;
					lod0_per_face_index_lists[RIGHT][right_faces++] = v0_idx;
					if(tex_idx == LEAVES) {
						lod0_per_face_index_lists[RIGHT][right_faces++] = v5_idx;
						lod0_per_face_index_lists[RIGHT][right_faces++] = v1_idx;
						lod0_per_face_index_lists[RIGHT][right_faces++] = v7_idx;
						lod0_per_face_index_lists[RIGHT][right_faces++] = v3_idx;
						lod0_per_face_index_lists[RIGHT][right_faces++] = v0_idx;
					}
				}

			
				

			}
		}
	}
	lod0_indexes_per_face[FRONT] = front_faces;
	lod0_indexes_per_face[BACK] = back_faces;
	lod0_indexes_per_face[TOP_BOTTOM] = top_bot_faces;
	lod0_indexes_per_face[LEFT] = left_faces;
	lod0_indexes_per_face[RIGHT] = right_faces;

	back_faces = front_faces = top_bot_faces = left_faces = right_faces = 0;
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
				int left_height = lod1_x == 0 ? -1 : sharp_heightmap_table[lod1_z*CHUNK_SIZE+lod1_lx];
				int right_height = lod1_x == CHUNK_SIZE-1 ? -1 : sharp_heightmap_table[lod1_z*CHUNK_SIZE+lod1_rx];
				int back_height = lod1_z == 0 ? -1 : sharp_heightmap_table[lod1_bz*CHUNK_SIZE+lod1_x];
				int front_height = lod1_z == CHUNK_SIZE-1 ? -1 : sharp_heightmap_table[lod1_fz*CHUNK_SIZE+lod1_x];

				int left_exposed = y*2 >= left_height;
				int right_exposed = y*2 >= right_height;
				int front_exposed = y*2 >= front_height;
				int back_exposed = y*2 >= back_height;
				int center_exposed = y*2 == this_height || y*2+1 == this_height;
				// check for transparent


				if((y*2 <= this_height) && back_exposed) {
					lod1_per_face_index_lists[BACK][back_faces++] = v0_idx;
					lod1_per_face_index_lists[BACK][back_faces++] = v1_idx;
					lod1_per_face_index_lists[BACK][back_faces++] = v2_idx;
					lod1_per_face_index_lists[BACK][back_faces++] = v3_idx;
					lod1_per_face_index_lists[BACK][back_faces++] = v0_idx;
				}

				if((y*2<=this_height) && front_exposed) {
					lod1_per_face_index_lists[FRONT][front_faces++] = v5_idx;
					lod1_per_face_index_lists[FRONT][front_faces++] = v4_idx;
					lod1_per_face_index_lists[FRONT][front_faces++] = v7_idx;
					lod1_per_face_index_lists[FRONT][front_faces++] = v6_idx; 
					lod1_per_face_index_lists[FRONT][front_faces++] = v0_idx;
				}

				if(center_exposed) {
					lod1_per_face_index_lists[TOP_BOTTOM][top_bot_faces++] = v3_idx;
					lod1_per_face_index_lists[TOP_BOTTOM][top_bot_faces++] = v7_idx;
					lod1_per_face_index_lists[TOP_BOTTOM][top_bot_faces++] = v2_idx;
					lod1_per_face_index_lists[TOP_BOTTOM][top_bot_faces++] = v6_idx;
					lod1_per_face_index_lists[TOP_BOTTOM][top_bot_faces++] = v0_idx;
				}
				if(y == 0) {
					lod1_per_face_index_lists[TOP_BOTTOM][top_bot_faces++] = v0_idx;
					lod1_per_face_index_lists[TOP_BOTTOM][top_bot_faces++] = v4_idx;
					lod1_per_face_index_lists[TOP_BOTTOM][top_bot_faces++] = v1_idx;
					lod1_per_face_index_lists[TOP_BOTTOM][top_bot_faces++] = v5_idx;
					lod1_per_face_index_lists[TOP_BOTTOM][top_bot_faces++] = v0_idx;
				}

				
				if((y*2<=this_height) && left_exposed) {
					lod1_per_face_index_lists[LEFT][left_faces++] = v4_idx;
					lod1_per_face_index_lists[LEFT][left_faces++] = v0_idx;
					lod1_per_face_index_lists[LEFT][left_faces++] = v6_idx;
					lod1_per_face_index_lists[LEFT][left_faces++] = v2_idx;
					lod1_per_face_index_lists[LEFT][left_faces++] = v0_idx;
				}
				if((y*2<=this_height) && right_exposed) {
					lod1_per_face_index_lists[RIGHT][right_faces++] = v1_idx;
					lod1_per_face_index_lists[RIGHT][right_faces++] = v5_idx;
					lod1_per_face_index_lists[RIGHT][right_faces++] = v3_idx;
					lod1_per_face_index_lists[RIGHT][right_faces++] = v7_idx;
					lod1_per_face_index_lists[RIGHT][right_faces++] = v0_idx;
				}
			}
		}
	}
	
	lod1_indexes_per_face[FRONT] = front_faces;
	lod1_indexes_per_face[BACK] = back_faces;
	lod1_indexes_per_face[TOP_BOTTOM] = top_bot_faces;
	lod1_indexes_per_face[LEFT] = left_faces;
	lod1_indexes_per_face[RIGHT] = right_faces;
}


void *lod0_vbo_data, *lod1_vbo_data;


void chunks_init() {
    
	mesh_chunk();

	lod0_vbo_data = mmAlloc(sizeof(lod0_full_vertex_list));
	mmCopy(lod0_vbo_data, lod0_full_vertex_list, sizeof(lod0_full_vertex_list));

	lod1_vbo_data = mmAlloc(sizeof(lod1_full_vertex_list));
	mmCopy(lod1_vbo_data, lod1_full_vertex_list, sizeof(lod1_full_vertex_list));
}

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
}




int lod_table[16]; // 1 -> draw as a single LOD1, else draw as 4 LOD0
// each element in the lod table represents what granularity it's "parent" 128x128 LOD2 chunk is broken up into.


void chunk_init_lod_table(float camX, float camY, float camZ) {
    
	memset(lod_table, -1, sizeof(lod_table));

	for(int z = 0; z < 4; z++) {
		for(int x = 0; x < 4; x++) {
			float min_x = x*LOD1_SZ;
			float min_y = 0.0f;
			float min_z = z*LOD1_SZ;

			float max_x = min_x+LOD1_SZ;
			float max_y = min_y+LOD1_SZ;
			float max_z = min_z+LOD1_SZ;

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

			float min_rel_dist = sqrtf(min_rel_dx*min_rel_dx + min_rel_dy*min_rel_dy + min_rel_dz*min_rel_dz);

			if(min_rel_dist > FAR_PLANE_DIST) {
				lod_table[z*4+x] = -1;
			}

			// previously was using center d ist, min dist might be better
			// to prevent blocky geometry from getting too close
			if(min_rel_dist >= LOD1_SZ) {
				lod_table[z*4+x] = 1; // 3 is LOD0 but with just textures
			} else {
				lod_table[z*4+x] = 0;
			}

		}
	}
}


void draw_lod0_chunks(C3D_Mtx* mvpMatrix, int chunk_offset_uniform_loc, int draw_top_uvs_uniform_loc, float camX, float camY, float camZ) {

    	for(int z = 0; z < 4; z++) {
		for(int x = 0; x < 4; x++) {
			if (lod_table[z*4+x] != 0) {
				continue;
			}

			float min_x = x * LOD1_SZ;
			float min_y = 0.0f;
			float min_z = z * LOD1_SZ;

			
			//total_lod0_meshes += 4;


			for(int z = 0; z <= 1; z++) {
				for(int x = 0; x <= 1; x++) {
					float cur_aabb_min_x = min_x + x*LOD0_SZ;
					float cur_aabb_min_y = min_y;
					float cur_aabb_min_z = min_z + z*LOD0_SZ;
					float cur_aabb_max_x = cur_aabb_min_x + LOD0_SZ;
					float cur_aabb_max_y = cur_aabb_min_y + LOD0_SZ;
					float cur_aabb_max_z = cur_aabb_min_z + LOD0_SZ;

					//if (clip) { continue; }
					C3D_FVec min_vec = FVec4_New(cur_aabb_min_x, cur_aabb_min_y, cur_aabb_min_z, 1.0f);
					C3D_FVec max_vec = FVec4_New(cur_aabb_max_x, cur_aabb_max_y, cur_aabb_max_z, 1.0f);

					if(!aabb_on_screen_clip_space(min_vec, max_vec, mvpMatrix)) {
						continue;
					}

					//drawn_lod0_meshes++;
					
					C3D_FVUnifSet(GPU_VERTEX_SHADER, chunk_offset_uniform_loc, cur_aabb_min_x, cur_aabb_min_y, cur_aabb_min_z, 0.0f);
					C3D_BoolUnifSet(GPU_GEOMETRY_SHADER, draw_top_uvs_uniform_loc, false);
					if(cur_aabb_min_z <= camZ) {
						C3D_DrawElements(GPU_GEOMETRY_PRIM, lod0_indexes_per_face[FRONT], C3D_UNSIGNED_SHORT, (void*)(lod0_per_face_index_lists[FRONT]));
						//verts += lod0_indexes_per_face[FRONT];
					}
					if(cur_aabb_max_z  >= camZ) {
						C3D_DrawElements(GPU_GEOMETRY_PRIM, lod0_indexes_per_face[BACK], C3D_UNSIGNED_SHORT, (void*)(lod0_per_face_index_lists[BACK]));
						//verts += lod0_indexes_per_face[BACK];
					}
					if(cur_aabb_max_x >= camX) {
						C3D_DrawElements(GPU_GEOMETRY_PRIM, lod0_indexes_per_face[LEFT], C3D_UNSIGNED_SHORT, lod0_per_face_index_lists[LEFT]);
						//verts += lod0_indexes_per_face[LEFT];
					}
					if(cur_aabb_min_x <= camX) {
						C3D_DrawElements(GPU_GEOMETRY_PRIM, lod0_indexes_per_face[RIGHT], C3D_UNSIGNED_SHORT, lod0_per_face_index_lists[RIGHT]);
						//verts += lod0_indexes_per_face[RIGHT];
					}
					C3D_BoolUnifSet(GPU_GEOMETRY_SHADER, draw_top_uvs_uniform_loc, true);
					C3D_DrawElements(GPU_GEOMETRY_PRIM, lod0_indexes_per_face[TOP_BOTTOM], C3D_UNSIGNED_SHORT, (void*)(lod0_per_face_index_lists[TOP_BOTTOM]));

					//verts += lod0_indexes_per_face[TOP_BOTTOM];
					
				}
			}

		}
	}
}

void draw_lod1_chunks(C3D_Mtx* mvpMatrix, int chunk_offset_uniform_loc, float camX, float camY, float camZ) {
	for(int z = 0; z < 4; z++) {
		for(int x = 0; x < 4; x++) {
			
			if(lod_table[z*4+x] != 1) { continue; }
			float min_x = x*LOD1_SZ;
			float min_y = 0;
			float min_z = z*LOD1_SZ;

			float max_x = min_x + LOD1_SZ;
			float max_y = min_y + LOD0_SZ;
			float max_z = min_z + LOD1_SZ;
			
			// loop over the 8 corners of the AABB
			//total_lod1_meshes += 1;
			if(!aabb_on_screen_clip_space(
				FVec3_New(min_x, min_y, min_z),FVec3_New(max_x, max_y, max_z), mvpMatrix
			)) { 
				continue;
			}
	
			
			C3D_FVUnifSet(GPU_VERTEX_SHADER, chunk_offset_uniform_loc, min_x, min_y, min_z, 0.0f);

			if(min_z <= camZ) {
				C3D_DrawElements(GPU_GEOMETRY_PRIM, lod1_indexes_per_face[FRONT], C3D_UNSIGNED_SHORT, (void*)(lod1_per_face_index_lists[FRONT]));
				//verts += lod1_indexes_per_face[FRONT];
			}
			if(max_z >= camZ) {
				C3D_DrawElements(GPU_GEOMETRY_PRIM, lod1_indexes_per_face[BACK], C3D_UNSIGNED_SHORT, (void*)(lod1_per_face_index_lists[BACK]));
				//verts += lod1_indexes_per_face[BACK];
			}
			if(max_x >= camX) {
				C3D_DrawElements(GPU_GEOMETRY_PRIM, lod1_indexes_per_face[LEFT], C3D_UNSIGNED_SHORT, lod1_per_face_index_lists[LEFT]);
				//verts += lod1_indexes_per_face[LEFT];
			}
			if(min_x <= camX) {
				C3D_DrawElements(GPU_GEOMETRY_PRIM, lod1_indexes_per_face[RIGHT], C3D_UNSIGNED_SHORT, lod1_per_face_index_lists[RIGHT]);
				//verts += lod1_indexes_per_face[RIGHT];
			}

			C3D_DrawElements(GPU_GEOMETRY_PRIM, lod1_indexes_per_face[TOP_BOTTOM], C3D_UNSIGNED_SHORT, (void*)(lod1_per_face_index_lists[TOP_BOTTOM]));
			//verts += lod1_indexes_per_face[TOP_BOTTOM];
		}
	}
}