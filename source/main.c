#include <3ds.h>
#include <citro3d.h>
#include <tex3ds.h>
#include <string.h>

#include "lod0_program_shbin.h"
#include "lod1_program_shbin.h"
#include "vshader_shbin.h"
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

#define CLEAR_COLOR 0x68B0D8FF

#define DISPLAY_TRANSFER_FLAGS \
	(GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) | \
	GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
	GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

typedef struct { u8 position[3]; u8 material; } lod0_vertex;
typedef struct { u8 position[3]; u8 color[3]; } lod1_vertex;

// 565 colors are 0-32 or 0-64
// rg = floor(rgb/32.0);
// r = floor(rg/32.0);
// b = rgb - rg*32.0;
// g = rg - r * 32.0;



uint16_t random(void) {
	//static uint16_t start_state = 0xACE1u;  /* Any nonzero start state will work. */
    static uint16_t lfsr = 0xACE1u;
    uint16_t bit;                    /* Must be 16-bit to allow bit<<15 later in the code */

    
    /* taps: 16 14 13 11; feedback polynomial: x^16 + x^14 + x^13 + x^11 + 1 */
	bit = ((lfsr >> 0) ^ (lfsr >> 2) ^ (lfsr >> 3) ^ (lfsr >> 5)) & 1u;
	lfsr = (lfsr >> 1) | (bit << 15);
	return lfsr;

}

int VRAM_TOTAL = 0;
void mmLogVRAM() {
	printf("VRAM: %lu / %i kB\n", (VRAM_TOTAL - vramSpaceFree()) / 1024, VRAM_TOTAL / 1024);
}

bool mmIsVRAM(void *addr) {
	u32 vaddr = (u32)addr;
	return vaddr >= 0x1F000000 && vaddr < 0x1F600000;
}


void assert(int i, int line) {
	if(!i) {
		printf("Assertion failed on line %i\n", line);
		while(1) { }
	}
}

#define ASSERT(v) assert((v), __LINE__)



void mmCopy(void *dst, void *src, size_t size) {
	if (mmIsVRAM(dst)) {
		GSPGPU_FlushDataCache(src, size);
		GX_RequestDma((u32*)src, (u32*)dst, size);
		gspWaitForDMA();
	} else {
		memcpy(dst, src, size);
		GSPGPU_FlushDataCache(dst, size);
	}
}

void* mmAlloc(size_t size) {
	printf("Allocating %i bytes of vram\n",  size);
	void *addr = vramAlloc(size);
	if (!addr) {
		printf("! OUT OF VRAM %lu < %i\n", vramSpaceFree() / 1024, size / 1024);
		addr = linearAlloc(size);
		ASSERT(addr != NULL);
	} else {
		mmLogVRAM();
	}
	return addr;
}

void mmFree(void* addr) {
	if(mmIsVRAM(addr)) {
		vramFree(addr);
	} else {
		linearFree(addr);
	}
}

// first face
// - - +
// + - +
// + + +
// - + +

// second face 
// - - -
// - + -
// + + -
// + - -

// third face 
// + - -
// + + -
// + + +
// + - +

// fourth face
// - - -
// - - + 
// - + +
// - + -

// fifth face
// - + -
// - + +
// + + +
// + + -

// sixth face
// - - -
// + - -
// + - +
// - - +



// vertexes of a cube 


#define CHUNK_SIZE 31
float chunk_size = 31.0f;

static lod0_vertex lod0_full_vertex_list[(CHUNK_SIZE+1)*(CHUNK_SIZE+1)*(CHUNK_SIZE+1)];
static lod1_vertex lod1_full_vertex_list[(CHUNK_SIZE+1)*(CHUNK_SIZE/2+1)*(CHUNK_SIZE+1)];
static lod1_vertex lod2_full_vertex_list[(CHUNK_SIZE+1)*(CHUNK_SIZE/2+1)*(CHUNK_SIZE+1)];


static u16 lod0_full_index_list[CHUNK_SIZE*CHUNK_SIZE*CHUNK_SIZE*8];
static u16 lod1_full_index_list[CHUNK_SIZE*CHUNK_SIZE/2*CHUNK_SIZE*8];
static u16 lod2_full_index_list[CHUNK_SIZE*CHUNK_SIZE/2*CHUNK_SIZE*8];

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


#define CHUNKS_X 6
#define CHUNKS_Z 6
//int num_vertexes_to_draw[4] = { 0,0,0,0 };
int lod0_num_vertexes_to_draw = 0;
int lod1_num_vertexes_to_draw = 0;
int lod2_num_vertexes_to_draw = 0;

u8 texture_colors[8][3] = {
	{0,0,0},
	{54,39,18},
	{107,99,89},
	{23,19,31},
	{74,54,24},
	{100,100,100},
	{100,100,100},
	{100,100,100},
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



				
				//lod2_full_vertex_list[idx].color[0] = random();
				//lod2_full_vertex_list[idx].color[1] = 0x00;
				//lod2_full_vertex_list[idx].color[2] = 0x00;
				//lod2_full_vertex_list[idx].position[0] = x*4;
				//lod2_full_vertex_list[idx].position[1] = y;
				//lod2_full_vertex_list[idx].position[2] = z*4;

			}
		}
	}

	
	for(int z = 0; z < CHUNK_SIZE+1; z++) {
		for(int y = 0; y < CHUNK_SIZE/2+1; y++) {
			for(int x = 0; x < CHUNK_SIZE+1; x++) {
				int idx = get_lod1_vertex_idx(x,y,z);
				//lod1_full_vertex_list[idx].color[0] = random();
				lod1_full_vertex_list[idx].color[1] = 0x00;
				lod1_full_vertex_list[idx].color[2] = 0x00;
				lod1_full_vertex_list[idx].position[0] = x*2;
				lod1_full_vertex_list[idx].position[1] = y*2;
				lod1_full_vertex_list[idx].position[2] = z*2;


			}
		}
	}


	int lod0_idx = 0;
	int lod1_idx = 0;
	int lod2_idx = 0;

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
				int tex_idx = (x_border || z_border) ? 3 : (random()%7)+1;

				lod0_full_vertex_list[v0_idx].material = tex_idx; //(x+y+z)%7)+1;

				int this_height = sharp_heightmap_table[z*CHUNK_SIZE+x];
				int left_height = x == 0 ? 0 : sharp_heightmap_table[z*CHUNK_SIZE+x-1];
				int right_height = x == CHUNK_SIZE-1 ? 0 : sharp_heightmap_table[z*CHUNK_SIZE+x+1];
				int back_height = z == 0 ? 0 : sharp_heightmap_table[(z-1)*CHUNK_SIZE+x];
				int forward_height = z == CHUNK_SIZE-1 ? 0 : sharp_heightmap_table[(z+1)*CHUNK_SIZE+x];
				int left_exposed = y >= left_height;
				int right_exposed = y >= right_height;
				int forward_exposed = y >= forward_height;
				int back_exposed = y >= back_height;
				int center_exposed = y == this_height;

				
				if(center_exposed || ((y<=this_height) && (left_exposed || right_exposed || back_exposed || forward_exposed))) {
					// skip anything above 0 for closest LOD
					
					//lod2_full_vertex_list[v0_idx].material = (x_border || z_border) ? 3 : (random()%7)+1; //(x+y+z)%7)+1;
					lod0_full_index_list[lod0_idx++] = v0_idx;
					lod0_full_index_list[lod0_idx++] = v1_idx;
					lod0_full_index_list[lod0_idx++] = v2_idx;
					lod0_full_index_list[lod0_idx++] = v3_idx;
					lod0_full_index_list[lod0_idx++] = v4_idx;
					lod0_full_index_list[lod0_idx++] = v5_idx;
					lod0_full_index_list[lod0_idx++] = v6_idx;
					lod0_full_index_list[lod0_idx++] = v7_idx;
				}
			}
		}
	}

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
				int lod1_tex_idx0 = (x_border || z_border) ? 3 : (random()%7)+1;
				int lod1_tex_idx1 = (x_border || z_border) ? 3 : (random()%7)+1;
				int lod1_tex_idx2 = (x_border || z_border) ? 3 : (random()%7)+1;
				int lod1_tex_idx3 = (x_border || z_border) ? 3 : (random()%7)+1;

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

				int left_exposed = y*2 >= left_height/2;
				int right_exposed = y*2 >= right_height/2;
				int forward_exposed = y*2 >= forward_height/2;
				int back_exposed = y*2 >= back_height;
				int center_exposed = y*2 == this_height;
				if(center_exposed || ((y*2<=this_height) && (left_exposed || right_exposed || back_exposed || forward_exposed))) {

					lod1_full_index_list[lod1_idx++] = v0_idx;
					lod1_full_index_list[lod1_idx++] = v1_idx;
					lod1_full_index_list[lod1_idx++] = v2_idx;
					lod1_full_index_list[lod1_idx++] = v3_idx;
					lod1_full_index_list[lod1_idx++] = v4_idx;
					lod1_full_index_list[lod1_idx++] = v5_idx;
					lod1_full_index_list[lod1_idx++] = v6_idx;
					lod1_full_index_list[lod1_idx++] = v7_idx;
				}

			}
		}
	}
				




	/*
	for(int z = 0; z < CHUNK_SIZE; z++) {
		int z_border = (z == 0 || z == CHUNK_SIZE-1);
		for(int y = 0; y < CHUNK_SIZE/4; y++) {
			for(int x = 0; x < CHUNK_SIZE; x++) {
				int x_border = x == 0 || x == CHUNK_SIZE-1;

				int v0_idx = get_lod2_vertex_idx(x,  y,   z);
				int v1_idx = get_lod2_vertex_idx(x+1,y,   z);
				int v2_idx = get_lod2_vertex_idx(x,  y+1, z);
				int v3_idx = get_lod2_vertex_idx(x+1,y+1, z);
				int v4_idx = get_lod2_vertex_idx(x,  y,   z+1);
				int v5_idx = get_lod2_vertex_idx(x+1,y,   z+1);
				int v6_idx = get_lod2_vertex_idx(x,  y+1, z+1);
				int v7_idx = get_lod2_vertex_idx(x+1,y+1, z+1);
				
				int lod1_tex_idx0 = (x_border || z_border) ? 3 : (random()%7)+1;
				int lod1_tex_idx1 = (x_border || z_border) ? 3 : (random()%7)+1;
				int lod1_tex_idx2 = (x_border || z_border) ? 3 : (random()%7)+1;
				int lod1_tex_idx3 = (x_border || z_border) ? 3 : (random()%7)+1;

				int lod1_r = ((texture_colors[lod1_tex_idx0][0] + texture_colors[lod1_tex_idx1][0] + texture_colors[lod1_tex_idx2][0] + texture_colors[lod1_tex_idx3][0]) / 4.0f);
				int lod1_g = ((texture_colors[lod1_tex_idx0][1] + texture_colors[lod1_tex_idx1][1] + texture_colors[lod1_tex_idx2][1] + texture_colors[lod1_tex_idx3][1]) / 4.0f);
				int lod1_b = ((texture_colors[lod1_tex_idx0][2] + texture_colors[lod1_tex_idx1][2] + texture_colors[lod1_tex_idx2][2] + texture_colors[lod1_tex_idx3][2]) / 4.0f);

				lod2_full_vertex_list[v0_idx].color[0] = lod1_r;
				lod2_full_vertex_list[v0_idx].color[1] = lod1_g;
				lod2_full_vertex_list[v0_idx].color[2] = lod1_b;


				int lod2_z = (z*4)%CHUNK_SIZE;
				int lod2_fz = ((z+1)*4)%CHUNK_SIZE;
				int lod2_bz = ((z-1)*4)%CHUNK_SIZE;
				int lod2_x = (x*4)%CHUNK_SIZE;
				int lod2_rx = ((x+1)*4)%CHUNK_SIZE;
				int lod2_lx = ((x-1)*4)%CHUNK_SIZE;
				int this_height = sharp_heightmap_table[lod2_z*CHUNK_SIZE+lod2_x];
				int left_height = lod2_x == 0 ? 0 : sharp_heightmap_table[lod2_z*CHUNK_SIZE+lod2_lx];
				int right_height = lod2_x == CHUNK_SIZE-1 ? 0 : sharp_heightmap_table[lod2_z*CHUNK_SIZE+lod2_rx];
				int back_height = lod2_z == 0 ? 0 : sharp_heightmap_table[lod2_bz*CHUNK_SIZE+lod2_x];
				int forward_height = lod2_z == CHUNK_SIZE-1 ? 0 : sharp_heightmap_table[lod2_fz*CHUNK_SIZE+lod2_x];

				
				int left_exposed = y >= left_height;
				int right_exposed = y >= right_height;
				int forward_exposed = y >= forward_height;
				int back_exposed = y >= back_height;
				int center_exposed = y == this_height;
				if(center_exposed || ((y<=this_height) && (left_exposed || right_exposed || back_exposed || forward_exposed))) {

					lod2_full_index_list[lod2_idx++] = v0_idx;
					lod2_full_index_list[lod2_idx++] = v1_idx;
					lod2_full_index_list[lod2_idx++] = v2_idx;
					lod2_full_index_list[lod2_idx++] = v3_idx;
					lod2_full_index_list[lod2_idx++] = v4_idx;
					lod2_full_index_list[lod2_idx++] = v5_idx;
					lod2_full_index_list[lod2_idx++] = v6_idx;
					lod2_full_index_list[lod2_idx++] = v7_idx;
				}
			}
		}
	}
	*/
	lod0_num_vertexes_to_draw = lod0_idx;
	lod1_num_vertexes_to_draw = lod1_idx;
	lod2_num_vertexes_to_draw = 0;


}


#define vertex_list_count (sizeof(vertex_list)/sizeof(vertex_list[0]))
#define index_list_count (sizeof(index_list)/sizeof(index_list[0]))

static DVLB_s *lod0_program_dvlb, *lod1_program_dvlb, *tex_tri_program_dvlb, *skybox_vshader_dvlb;
static shaderProgram_s lod0_program, lod1_program, tex_tri_program, skybox_program;

static int lod0_uLoc_mvp, lod1_uLoc_mvp, lod2_uLoc_mvp;
static int lod0_uLoc_chunkOffset, lod1_uLoc_chunkOffset, lod2_uLoc_chunkOffset;
static int tex_tri_proj_uLoc;
static int skybox_uLoc_projection, skybox_uLoc_modelView;

static C3D_Mtx projection, ortho_projection, modelView, mvpMatrix;




static void *lod0_vbo_data, *lod1_vbo_data, *lod2_vbo_data;
static u16 *lod0_ibo_data, *lod1_ibo_data, *lod2_ibo_data;

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
    //1.f, 1.f, 1.f, //1.0f,      // Front-top-right
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
	return x >= a && b >= x;
}

static void sceneInit(void)
{

	skybox_vbo = linearAlloc(sizeof(cube_vert_list));
	memcpy(skybox_vbo, cube_vert_list, sizeof(cube_vert_list));

	skybox_ibo = linearAlloc(sizeof(cube_idx_list));
	memcpy(skybox_ibo, cube_idx_list, sizeof(cube_idx_list));

	// Load the vertex and geometry shader, create a shader program and bind it
	lod0_program_dvlb = DVLB_ParseFile((u32*)lod0_program_shbin, lod0_program_shbin_size);
	shaderProgramInit(&lod0_program);
	shaderProgramSetVsh(&lod0_program, &lod0_program_dvlb->DVLE[0]);
	shaderProgramSetGsh(&lod0_program, &lod0_program_dvlb->DVLE[1], 16);
	//C3D_BindProgram(&lod0_program);

	lod1_program_dvlb = DVLB_ParseFile((u32*)lod1_program_shbin, lod1_program_shbin_size);
	shaderProgramInit(&lod1_program);
	shaderProgramSetVsh(&lod1_program, &lod1_program_dvlb->DVLE[0]);
	shaderProgramSetGsh(&lod1_program, &lod1_program_dvlb->DVLE[1], 16);

	

	tex_tri_program_dvlb = DVLB_ParseFile((u32*)vshader_shbin, vshader_shbin_size);
	shaderProgramInit(&tex_tri_program);
	shaderProgramSetVsh(&tex_tri_program, &tex_tri_program_dvlb->DVLE[0]);

	
	skybox_vshader_dvlb = DVLB_ParseFile((u32*)skybox_shbin, skybox_shbin_size);
	shaderProgramInit(&skybox_program);
	shaderProgramSetVsh(&skybox_program, &skybox_vshader_dvlb->DVLE[0]);


	// Get the location of the uniforms
	lod0_uLoc_mvp    = shaderInstanceGetUniformLocation(lod0_program.vertexShader, "mvp");
	lod0_uLoc_chunkOffset  = shaderInstanceGetUniformLocation(lod0_program.vertexShader, "chunk_offset");

	lod1_uLoc_mvp    = shaderInstanceGetUniformLocation(lod1_program.vertexShader, "mvp");
	lod1_uLoc_chunkOffset  = shaderInstanceGetUniformLocation(lod1_program.vertexShader, "chunk_offset");
	
	lod2_uLoc_mvp = lod1_uLoc_mvp;
	lod2_uLoc_chunkOffset = lod1_uLoc_chunkOffset;

	skybox_uLoc_projection = shaderInstanceGetUniformLocation(skybox_program.vertexShader, "projection");
	skybox_uLoc_modelView  = shaderInstanceGetUniformLocation(skybox_program.vertexShader, "modelView");

	tex_tri_proj_uLoc = shaderInstanceGetUniformLocation(tex_tri_program.vertexShader, "projection");

	
	mesh_chunk();

	// Create the VBO (vertex buffer object)

	lod0_vbo_data = mmAlloc(sizeof(lod0_full_vertex_list));
	mmCopy(lod0_vbo_data, lod0_full_vertex_list, sizeof(lod0_full_vertex_list));

	lod1_vbo_data = mmAlloc(sizeof(lod1_full_vertex_list));
	mmCopy(lod1_vbo_data, lod1_full_vertex_list, sizeof(lod1_full_vertex_list));

	lod2_vbo_data = mmAlloc(sizeof(lod2_full_vertex_list));
	mmCopy(lod2_vbo_data, lod2_full_vertex_list, sizeof(lod2_full_vertex_list));

	lod0_ibo_data = mmAlloc(sizeof(lod0_full_index_list));
	mmCopy(lod0_ibo_data, lod0_full_index_list, sizeof(lod0_full_index_list));

	lod1_ibo_data = mmAlloc(sizeof(lod1_full_index_list));
	mmCopy(lod1_ibo_data, lod1_full_index_list, sizeof(lod1_full_index_list));

	lod2_ibo_data = mmAlloc(sizeof(lod2_full_index_list));
	mmCopy(lod2_ibo_data, lod2_full_index_list, sizeof(lod2_full_index_list));
	
	

	// Compute the projection matrix
	Mtx_PerspTilt(&projection, C3D_AngleFromDegrees(80.0f), C3D_AspectRatioTop, 0.1f, 128.0f, false);

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

	//static C3D_FogLut lut_fog;
	//FogLut_Exp(&lut_fog, 0.002f, 1.0f, 0.4f, 64.0f);
	//C3D_Fog
	//C3D_FogGasMode(GPU_FOG,  GPU_PLAIN_DENSITY, false);
	//C3D_FogColor(0xFFD8B06A); //CLEAR_COLOR);
	//C3D_FogLutBind(&lut_fog);

}

static float angleX = 0.0, angleY = 0.0;
float camX = 128.0f;
float camY = 16.0f;
float camZ = 128.0f;

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


#define LOD0_SZ 31
#define LOD1_SZ 62
#define LOD2_SZ 124


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

void set_lod2_attr_info() { set_lod1_attr_info(); }

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

void bind_lod2_vbo() {
	// Configure buffers
	C3D_BufInfo* bufInfo = C3D_GetBufInfo();
	BufInfo_Init(bufInfo);
	BufInfo_Add(bufInfo, lod2_vbo_data, sizeof(lod1_vertex), 2, 0x10);
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
	C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, 0, 0);
	C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
}
void set_lod2_texenv() {
	set_lod1_texenv();
}

void bind_lod0_program() {
	C3D_BindProgram(&lod0_program);
}
void bind_lod1_program() {
	C3D_BindProgram(&lod1_program);
}
void bind_lod2_program() {
	bind_lod1_program();
}

// loop over LOD1 chunks
// if center is within a certain distance, draw four LOD0 chunks instead

int draw_as_lod2(float cam_x, float cam_y, float cam_z, float chunk_center_x, float chunk_center_y, float chunk_center_z) {
	float dx = chunk_center_x-cam_x;
	float dy = chunk_center_y-cam_y;
	float dz = chunk_center_z-cam_z;
	return LOD2_SZ <= sqrtf(dx*dx+dz*dz+dy*dy);
}


int draw_as_lod1(float cam_x, float cam_y, float cam_z, float chunk_center_x, float chunk_center_y, float chunk_center_z) {
	float dx = chunk_center_x-cam_x;
	float dy = chunk_center_y-cam_y;
	float dz = chunk_center_z-cam_z;
	float dist = sqrtf(dx*dx+dy*dy+dz*dz);
	return LOD1_SZ <= dist && dist < LOD2_SZ;
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




static void sceneRender(void) {
	// Calculate the modelView matrix
	Mtx_Identity(&modelView);
	Mtx_RotateX(&modelView, -C3D_AngleFromDegrees(angleX), true);
	Mtx_RotateY(&modelView, -C3D_AngleFromDegrees(angleY), true);
	
	Mtx_Translate(&modelView, -camX, -camY, -camZ, true); 

	// Rotate the cube each frame
	//angleX += M_PI / 180;
	//angleY += M_PI / 180;

	Mtx_Multiply(&mvpMatrix, &projection, &modelView);

	//C3D_CullFace(GPU_CULL_BACK_CCW);

	int mvp_offset, chunk_off_offset;
	int num_verts = 0; // default to 0 in case of LOD2 somehow
	void* index_buffer = NULL;


	memset(lod2_table, -1, sizeof(lod2_table));
	memset(lod1_table, -1, sizeof(lod1_table));

	// setup LOD tables
	for(int z = 0; z < 2; z++) {
		for(int x = 0; x < 2; x++) {
			float min_x = x*LOD2_SZ;
			float min_y = 0.0f;
			float min_z = z*LOD2_SZ;
			
			float center_x = min_x + (LOD2_SZ/2.0f);
			float center_y = min_y + (LOD2_SZ/2.0f);
			float center_z = min_z + (LOD2_SZ/2.0f);


			float dx = center_x-camX;
			float dy = center_y-camY;
			float dz = center_z-camZ;
			float dist = sqrt(dx*dx + dy*dy + dz*dz);

			// if the distance is less than 128 voxels, mark it as split into LOD1 chunks
			// and process those to check if we want to split them further
			if(dist >= LOD2_SZ) {
				lod2_table[z*2+x] = 2;
			} else {
				lod2_table[z*2+x] = 1;

				// we've got something that's broken up into LOD1 chunks
				// let's test them separately
				for(int zz = z*2; zz < z*2+2; zz++) {
					for(int xx = x*2; xx < x*2+2; xx++) {
						float min_x = xx*LOD1_SZ;
						float min_y = 0.0f;
						float min_z = zz*LOD1_SZ;

						float center_x = min_x + (LOD1_SZ/2.0f);
						float center_y = min_y + (LOD1_SZ/2.0f);
						float center_z = min_z + (LOD1_SZ/2.0f);

						float dx = center_x-camX;
						float dy = center_y-camY;
						float dz = center_z-camZ;

						float dist = sqrtf(dx*dx + dy*dy + dz*dz);
						if(dist >= LOD1_SZ) {
							lod1_table[zz*4+xx] = 1;
						} else {
							lod1_table[zz*4+xx] = 0;
						}

					}
				}
			}
		}
	}

	/*
	
		draw LOD0 chunks

	*/

	bind_lod0_program();
	bind_lod0_vbo();
	set_lod0_attr_info();
	set_lod0_texenv();
	mvp_offset = lod0_uLoc_mvp;
	chunk_off_offset = lod0_uLoc_chunkOffset;
	num_verts = lod0_num_vertexes_to_draw;
	index_buffer = lod0_ibo_data;

	//printf("cam xyz: %.2f %.2f %.2f\n", camX, camY, camZ);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, mvp_offset, &mvpMatrix);
	//printf("%i%i\n", lod2_table[0], lod2_table[1]);
	//printf("%i%i\n", lod2_table[2], lod2_table[3]);

	//printf("%i%i%i%i\n", lod1_table[0], lod1_table[1], lod1_table[2], lod1_table[3]);
	//printf("%i%i%i%i\n", lod1_table[4], lod1_table[5], lod1_table[6], lod1_table[7]);
	//printf("%i%i%i%i\n", lod1_table[8], lod1_table[9], lod1_table[10], lod1_table[11]);
	//printf("%i%i%i%i\n", lod1_table[12], lod1_table[13], lod1_table[14], lod1_table[15]);
	
	// loop over the 8 corners of the AABB
	int inside[18] = {
		// -y
		//-x,x,+x
		0,0,0, // -z
		0,0,0, // z
		0,0,0, // +z
		
		
		// +y
		0,0,0,
		0,0,0,
		0,0,0
	};
	int total_lod0_chunks = 0;
	int drawn_lod0_chunks = 0;
	for(int z = 0; z < 4; z++) {
		for(int x = 0; x < 4; x++) {
			if(lod1_table[z*4+x] != 0) { continue; }

			float min_x = x * LOD1_SZ;
			float min_y = 0.0f;
			float min_z = z * LOD1_SZ;



				
			total_lod0_chunks += 4;

			int idx = 0;
			// 3 * 3 * 3
			for(int y = min_y; y <= min_y+LOD0_SZ; y += LOD0_SZ) {
				for(int z = min_z; z <= min_z+LOD1_SZ; z += LOD0_SZ) {
					for(int x = min_x; x <= min_x+LOD1_SZ; x += LOD0_SZ) {
						C3D_FVec corner = Mtx_MultiplyFVec4(&mvpMatrix, FVec4_New(x, y, z, 1.0));
						int corner_inside = (
							    within(-corner.w, corner.x, corner.w) &&
								within(-corner.w, corner.y, corner.w) &&
								within(-corner.w, corner.z, corner.w)
							);
						inside[idx++] = corner_inside;
					}
				}
			}
			for(int z = 0; z <= 1; z++) {
				for(int x = 0; x <= 1; x++) {
					float cur_aabb_min_x = min_x + x*LOD0_SZ;
					float cur_aabb_min_y = min_y;
					float cur_aabb_min_z = min_z + z*LOD0_SZ;
					int aabb_inside = inside[z*3+x] || inside[z*3+x+1] || inside[(z+1)*3+x] || inside[(z+1)*3+x+1];
					if(!aabb_inside) {
						aabb_inside = inside[z*3+x+9] || inside[z*3+x+1+9] || inside[(z+1)*3+x+9] || inside[(z+1*2+x+1+9)];
					}

					if(!aabb_inside) { 
						// if all 8 corners are outside of the frustum,
						// do one last double check if the camera itself is fully inside this aabb
						// if so, draw it
						if (!(within(cur_aabb_min_x , camX, cur_aabb_min_x + LOD0_SZ) &&
						     within(cur_aabb_min_y , camY, cur_aabb_min_y + LOD0_SZ) &&
							 within(cur_aabb_min_z , camZ, cur_aabb_min_y + LOD0_SZ))) {
							continue;
						}
					}
					
					drawn_lod0_chunks++;
					C3D_FVUnifSet(GPU_VERTEX_SHADER, chunk_off_offset, cur_aabb_min_x, cur_aabb_min_y, cur_aabb_min_z, 0.0f);
					C3D_DrawElements(GPU_GEOMETRY_PRIM, num_verts, C3D_UNSIGNED_SHORT, index_buffer);
					
				}
			}

		}
	}

	/*
	
		draw LOD1 chunks
		
	*/

	bind_lod1_program();
	bind_lod1_vbo();
	set_lod1_attr_info();
	set_lod1_texenv();
	mvp_offset = lod1_uLoc_mvp;
	chunk_off_offset = lod1_uLoc_chunkOffset;
	num_verts = lod1_num_vertexes_to_draw;
	index_buffer = lod1_ibo_data;
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, mvp_offset, &mvpMatrix);

	
	int drawn_lod1_chunks = 0;
	int total_lod1_chunks = 0;
	for(int z = 0; z < 4; z++) {
		for(int x = 0; x < 4; x++) {
			
			if(lod1_table[z*4+x] != 1) { continue; }
			float min_x = x*LOD1_SZ;
			float min_y = 0;
			float min_z = z*LOD1_SZ;

			float max_x = min_x + LOD1_SZ;
			float max_y = min_y + LOD1_SZ;
			float max_z = min_z + LOD1_SZ;
			
			int inside = 0;

			// loop over the 8 corners of the AABB
			for(int z = 0; z <= LOD1_SZ; z += LOD1_SZ) {
				for(int y = 0; y <= LOD1_SZ; y += LOD1_SZ) {
					for(int x = 0; x <= LOD1_SZ; x += LOD1_SZ) {
						C3D_FVec corner = Mtx_MultiplyFVec4(&mvpMatrix, FVec4_New(min_x+x, min_y+y, min_z+z, 1.0));
						int corner_inside = (
							    within(-corner.w, corner.x, corner.w) &&
								within(-corner.w, corner.y, corner.w) &&
								within(-corner.w, corner.z, corner.w)
							);
						inside = inside || corner_inside;
						
						//if(inside) { break; }
					}
				}
			}
			total_lod1_chunks += 1;
			if(!inside) { 
				if(!(within(min_x, camX, max_x) && within(min_y, camY, max_y) && within(min_z, camZ, max_z)))
				continue;
			}
			drawn_lod1_chunks += 1;
			

			//C3D_FVec frd = Mtx_MultiplyFVec4(&mvpMatrix, FVec4_New(max_x, min_y, min_z, 1.0));
			
			C3D_FVUnifSet(GPU_VERTEX_SHADER, chunk_off_offset, min_x, min_y, min_z, 0.0f);
			C3D_DrawElements(GPU_GEOMETRY_PRIM, num_verts, C3D_UNSIGNED_SHORT, index_buffer);
		}
	}

	/*
	
		draw LOD2 chunks
		
	*/
	int drawn_lod2_chunks = 0;
	int total_lod2_chunks = 0;
	/*
	bind_lod2_program();
	bind_lod2_vbo();
	set_lod2_attr_info();
	set_lod2_texenv();
	mvp_offset = lod1_uLoc_mvp;
	chunk_off_offset = lod1_uLoc_chunkOffset;
	num_verts = lod2_num_vertexes_to_draw;
	index_buffer = lod2_ibo_data;
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, mvp_offset, &mvpMatrix);

	
	for(int z = 0; z < 2; z++) {
		for(int x = 0; x < 2; x++) {
			
			if(lod2_table[z*2+x] != 2) { continue; }
			float min_x = x*LOD2_SZ;
			float min_y = 0;
			float min_z = z*LOD2_SZ;
			
			float center_x = min_x + (LOD2_SZ/2.0f);
			float center_y = min_y + (LOD2_SZ/2.0f);
			float center_z = min_z + (LOD2_SZ/2.0f);
			

			float max_x = min_x + LOD2_SZ;
			float max_y = min_y + LOD2_SZ;
			float max_z = min_z + LOD2_SZ;

			
			int inside = 0;

			// loop over the 8 corners of the AABB
			for(int z = 0; z <= LOD2_SZ; z += LOD2_SZ) {
				for(int y = 0; y <= LOD2_SZ; y += LOD2_SZ) {
					for(int x = 0; x <= LOD2_SZ; x += LOD2_SZ) {
						C3D_FVec corner = Mtx_MultiplyFVec4(&mvpMatrix, FVec4_New(min_x+x, min_y+y, min_z+z, 1.0));
						int corner_inside = (
							    within(-corner.w, corner.x, corner.w) &&
								within(-corner.w, corner.y, corner.w) &&
								within(-corner.w, corner.z, corner.w)
							);
						inside = inside || corner_inside;
						
						//if(inside) { break; }
					}
				}
			}
			total_lod2_chunks += 1;
			if(!inside) { 
				if(!(within(min_x, camX, max_x) && within(min_y, camY, max_y) && within(min_z, camZ, max_z)))
				continue;
			}
			drawn_lod2_chunks += 1;
			
			
			C3D_FVUnifSet(GPU_VERTEX_SHADER, chunk_off_offset, min_x, min_y, min_z, 0.0f);
			C3D_DrawElements(GPU_GEOMETRY_PRIM, num_verts, C3D_UNSIGNED_SHORT, index_buffer);
		}
	}
	*/
	

	printf("LOD0: %i/%i LOD1: %i/%i LOD2: %i/%i\n", 
		drawn_lod0_chunks, total_lod0_chunks, 
		drawn_lod1_chunks, total_lod1_chunks,
		drawn_lod2_chunks, total_lod2_chunks);
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
	Mtx_RotateX(&modelView, -C3D_AngleFromDegrees(angleX), true);
	Mtx_RotateY(&modelView, -C3D_AngleFromDegrees(angleY), true);

	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, skybox_uLoc_projection, &projection);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, skybox_uLoc_modelView,  &modelView);
	

	//C3D_DrawArrays(GPU_TRIANGLE_STRIP, 0, 14);
	C3D_DrawElements(GPU_TRIANGLE_STRIP, 14, C3D_UNSIGNED_BYTE, skybox_ibo);
	//float* ptr = (float*)cube_vert_list;
	//for(int i = 0; i < 36; i++) {
	//	C3D_ImmSendAttrib(cube_vert_list[i][0], cube_vert_list[i][1], cube_vert_list[i][2], 1.0f);
	//}
	C3D_ImmDrawEnd();
}



static void sceneExit(void)
{
	// Free the texture
	C3D_TexDelete(&atlas_tex);

	// Free the VBO
	mmFree(lod0_vbo_data);
	mmFree(lod1_vbo_data);
	mmFree(lod2_vbo_data);
	mmFree(lod0_ibo_data);
	mmFree(lod1_ibo_data);
	mmFree(lod2_ibo_data);

	// Free the shader program
	shaderProgramFree(&lod0_program);
	DVLB_Free(lod0_program_dvlb);
	shaderProgramFree(&lod1_program);
	DVLB_Free(lod1_program_dvlb);
}

int main()
{
	vramFree(vramAlloc(0));
	VRAM_TOTAL = vramSpaceFree();
	// Initialize graphics
	gfxInitDefault();
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);

	consoleInit(GFX_BOTTOM, NULL);

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
			angleY += 2.0f;
		} else if(kHeld & KEY_CSTICK_RIGHT) {
			angleY -= 2.0f;
		}
		if (kHeld & KEY_CSTICK_UP) {
			angleX += 2.0f;
		} else if(kHeld & KEY_CSTICK_DOWN) {
			angleX -= 2.0f;
		}

		
		if (cpos.dx < 0) {
			float dx = cpos.dx / -154.0f;
			dx = dx > 1.0f ? 1.0f : dx;
			angleY += lerp(0, 1.0f, dx);
		} else if(cpos.dx > 0) {
			float dx = cpos.dx / 154.0f;
			dx = dx > 1.0f ? 1.0f : dx;
			angleY -= lerp(0, 1.0f, dx);
		}

		// Compute forward vector from angles
		float cosPitch = cosf((angleX*.017f)); //*6.28f/360.0f);
		float sinPitch = sinf(angleX*6.28f/360.0f);
		float cosYaw   = cosf(angleY*6.28f/360.0f);
		float sinYaw   = sinf(angleY*6.28f/360.0f);


		float forwardX = -cosPitch * sinYaw;
		float forwardY = sinPitch;
		float forwardZ = -cosPitch * cosYaw;
		float speed = 0.3f; //last_frame_ms/80.0f; //0.2f at 60fps, scale for larger frame t imes


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
		printf("cpu %.2f%% gpu %.2f%%\n", cpu_time, gpu_time);
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