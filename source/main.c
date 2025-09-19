#include <3ds.h>
#include <citro3d.h>
#include <tex3ds.h>
#include <string.h>
#include "lod0_program_shbin.h"
#include "lod1_program_shbin.h"
#include "atlas_t3x.h"

#define MIN(x,y) ((x)<(y)?(x):(y))
#define MAX(x,y) ((x)>(y)?(x):(y))

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
		assert(addr != NULL, __LINE__);
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


static const u16 index_list[] = 
{
	0,1,2,3,4,5,6,7
};

C3D_FVec face_normals[6] = {
	{.x = 0.0f, .y = 0.0f, .z = +1.0f},
	{.x = 0.0f, .y = 0.0f, .z = -1.0f},
	{.x = +1.0f, .y = 0.0f, .z = 0.0f},
	{.x = -1.0f, .y = 0.0f, .z = 0.0f},
	{.x = 0.0f, .y = +1.0f, .z = 0.0f},
	{.x = 0.0f, .y = -1.0f, .z = 0.0f},
};

// vertexes of a cube 

static const lod0_vertex vertex_list[] =
{
	{ {1, 1, 1},  1}, // 0
	{ {2, 1, 1},  2}, // 1
	{ {1, 2, 1},  4}, // 2
	{ {2, 2, 1},  5}, // 3
	{ {1, 1, 2},  6}, // 4
	{ {2, 1, 2},  3}, // 5
	{ {1, 2, 2},  1}, // 6
	{ {2, 2, 2},  1}, // 7

	// First face (PZ)
	// First triangle
	
	/*
	{ {-0.5f, -0.5f, +0.5f},  },
	{ {+0.5f, -0.5f, +0.5f},  },
	{ {+0.5f, +0.5f, +0.5f},  },
	{ {-0.5f, +0.5f, +0.5f},  },

	// Second face (MZ)
	{ {-0.5f, -0.5f, -0.5f},  },
	{ {-0.5f, +0.5f, -0.5f},  },
	{ {+0.5f, +0.5f, -0.5f},  },
	{ {+0.5f, -0.5f, -0.5f},  },

	// Third face (PX)
	{ {+0.5f, -0.5f, -0.5f},  },
	{ {+0.5f, +0.5f, -0.5f},  },
	{ {+0.5f, +0.5f, +0.5f},  },
	{ {+0.5f, -0.5f, +0.5f},  },

	// Fourth face (MX)
	// First triangle
	{ {-0.5f, -0.5f, -0.5f},  },
	{ {-0.5f, -0.5f, +0.5f},  },
	{ {-0.5f, +0.5f, +0.5f},  },
	{ {-0.5f, +0.5f, -0.5f},  },

	// Fifth face (PY)
	// First triangle
	{ {-0.5f, +0.5f, -0.5f},  },
	{ {-0.5f, +0.5f, +0.5f},  },
	{ {+0.5f, +0.5f, +0.5f},  },
	{ {+0.5f, +0.5f, -0.5f},  },

	// Sixth face (MY)
	// First triangle
	{ {-0.5f, -0.5f, -0.5f},  },
	{ {+0.5f, -0.5f, -0.5f},  },
	{ {+0.5f, -0.5f, +0.5f},  },
	{ {-0.5f, -0.5f, +0.5f},  },
	 */
	

	
	// unique copy of each of the 8 vertexes
};


#define CHUNK_SIZE 31
float chunk_size = 31.0f;

static lod0_vertex lod0_full_vertex_list[(CHUNK_SIZE+1)*(CHUNK_SIZE+1)*(CHUNK_SIZE+1)];
static lod1_vertex lod1_full_vertex_list[(CHUNK_SIZE+1)*(CHUNK_SIZE+1)*(CHUNK_SIZE+1)];
static lod1_vertex lod2_full_vertex_list[(CHUNK_SIZE+1)*(CHUNK_SIZE+1)*(CHUNK_SIZE+1)];


static u16 lod0_full_index_list[CHUNK_SIZE*CHUNK_SIZE*CHUNK_SIZE*8];
static u16 lod1_full_index_list[CHUNK_SIZE*CHUNK_SIZE*CHUNK_SIZE*8];
static u16 lod2_full_index_list[CHUNK_SIZE*CHUNK_SIZE*CHUNK_SIZE*8];

#define CHUNK_SIZE_BITS 5

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

#define CHUNKS_X 6
#define CHUNKS_Z 6
//int num_vertexes_to_draw[4] = { 0,0,0,0 };
int lod0_num_vertexes_to_draw = 0;
int lod1_num_vertexes_to_draw = 0;

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
			heightmap_table[z*CHUNK_SIZE+x] = height*32;
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


				lod1_full_vertex_list[idx].color[0] = random();
				lod1_full_vertex_list[idx].color[1] = 0x00;
				lod1_full_vertex_list[idx].color[2] = 0x00;
				lod1_full_vertex_list[idx].position[0] = x*2;
				lod1_full_vertex_list[idx].position[1] = y;
				lod1_full_vertex_list[idx].position[2] = z*2;



				//lod2_full_vertex_list[idx].color[0] = 0x00;
				//lod2_full_vertex_list[idx].color[1] = random();
				//lod2_full_vertex_list[idx].color[2] = 0x00;
				//lod2_full_vertex_list[idx].position[0] = x*4;
				//lod2_full_vertex_list[idx].position[1] = y;
				//lod2_full_vertex_list[idx].position[2] = z*4;
			}
		}
	}

	//for(int chunk_x = 0; chunk_x < 2; chunk_x++) { // CHUNKS_X
	//	for(int chunk_z = 0; chunk_z < 2; chunk_z++) { // < CHUNKS_Z
			//int index_idx = (chunk_z&1);


	int lod0_idx = 0;
	int lod1_idx = 0;

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

				int lod1_tex_idx0 = (x_border || z_border) ? 3 : (random()%7)+1;
				int lod1_tex_idx1 = (x_border || z_border) ? 3 : (random()%7)+1;
				int lod1_tex_idx2 = (x_border || z_border) ? 3 : (random()%7)+1;
				int lod1_tex_idx3 = (x_border || z_border) ? 3 : (random()%7)+1;
				int lod1_r = ((texture_colors[lod1_tex_idx0][0] + texture_colors[lod1_tex_idx1][0] + texture_colors[lod1_tex_idx2][0] + texture_colors[lod1_tex_idx3][0]) / 4.0f);
				int lod1_g = ((texture_colors[lod1_tex_idx0][1] + texture_colors[lod1_tex_idx1][1] + texture_colors[lod1_tex_idx2][1] + texture_colors[lod1_tex_idx3][1]) / 4.0f);
				int lod1_b = ((texture_colors[lod1_tex_idx0][2] + texture_colors[lod1_tex_idx1][2] + texture_colors[lod1_tex_idx2][2] + texture_colors[lod1_tex_idx3][2]) / 4.0f);

				lod0_full_vertex_list[v0_idx].material = tex_idx; //(x+y+z)%7)+1;

				lod1_full_vertex_list[v0_idx].color[0] = lod1_r;
				lod1_full_vertex_list[v0_idx].color[1] = lod1_g;
				lod1_full_vertex_list[v0_idx].color[2] = lod1_b;

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

					lod1_full_index_list[lod1_idx++] = v0_idx;
					lod1_full_index_list[lod1_idx++] = v1_idx;
					lod1_full_index_list[lod1_idx++] = v2_idx;
					lod1_full_index_list[lod1_idx++] = v3_idx;
					lod1_full_index_list[lod1_idx++] = v4_idx;
					lod1_full_index_list[lod1_idx++] = v5_idx;
					lod1_full_index_list[lod1_idx++] = v6_idx;
					lod1_full_index_list[lod1_idx++] = v7_idx;
				}

				//if(!(z == 0 || z == CHUNK_SIZE-1) &&
				//!(y == 0 || y == CHUNK_SIZE-1) &&
				//!(x == 0 || x == CHUNK_SIZE-1)) { continue; }

				//if(chunk_x == 0 && x == CHUNK_SIZE-1 && !y_border && !z_border) { continue; }
				//if(chunk_x == 1 && x == 0 && !y_border && !z_border) { continue; }
				//if(chunk_z == 0 && z == CHUNK_SIZE-1 && !y_border && !x_border) { continue; }
				//if(chunk_z == 1 && z == 0 && !y_border && !x_border) { continue; }
				
				if (0) {
					int lod2_tex_idx0 = (x_border || z_border) ? 3 : (random()%7)+1;
					int lod2_tex_idx1 = (x_border || z_border) ? 3 : (random()%7)+1;
					int lod2_tex_idx2 = (x_border || z_border) ? 3 : (random()%7)+1;
					int lod2_tex_idx3 = (x_border || z_border) ? 3 : (random()%7)+1;
					int lod2_tex_idx4 = (x_border || z_border) ? 3 : (random()%7)+1;
					int lod2_tex_idx5 = (x_border || z_border) ? 3 : (random()%7)+1;
					int lod2_tex_idx6 = (x_border || z_border) ? 3 : (random()%7)+1;
					int lod2_tex_idx7 = (x_border || z_border) ? 3 : (random()%7)+1;
					int lod2_tex_idx8 = (x_border || z_border) ? 3 : (random()%7)+1;
					int lod2_tex_idx9 = (x_border || z_border) ? 3 : (random()%7)+1;
					int lod2_tex_idx10 = (x_border || z_border) ? 3 : (random()%7)+1;
					int lod2_tex_idx11 = (x_border || z_border) ? 3 : (random()%7)+1;
					int lod2_tex_idx12 = (x_border || z_border) ? 3 : (random()%7)+1;
					int lod2_tex_idx13 = (x_border || z_border) ? 3 : (random()%7)+1;
					int lod2_tex_idx14 = (x_border || z_border) ? 3 : (random()%7)+1;
					int lod2_tex_idx15 = (x_border || z_border) ? 3 : (random()%7)+1;
					int lod2_r = ((texture_colors[lod2_tex_idx0][0] + texture_colors[lod2_tex_idx1][0] + texture_colors[lod2_tex_idx2][0] + texture_colors[lod2_tex_idx3][0] + 
									texture_colors[lod2_tex_idx4][0] + texture_colors[lod2_tex_idx5][0] + texture_colors[lod2_tex_idx6][0] + texture_colors[lod2_tex_idx7][0] + 
									texture_colors[lod2_tex_idx8][0] + texture_colors[lod2_tex_idx9][0] + texture_colors[lod2_tex_idx10][0] + texture_colors[lod2_tex_idx11][0] + 
									texture_colors[lod2_tex_idx12][0] + texture_colors[lod2_tex_idx13][0] + texture_colors[lod2_tex_idx14][0] + texture_colors[lod2_tex_idx15][0])
						/ 16.0f);
					int lod2_g = ((texture_colors[lod2_tex_idx0][1] + texture_colors[lod2_tex_idx1][1] + texture_colors[lod2_tex_idx2][1] + texture_colors[lod2_tex_idx3][1] + 
									texture_colors[lod2_tex_idx4][1] + texture_colors[lod2_tex_idx5][1] + texture_colors[lod2_tex_idx6][1] + texture_colors[lod2_tex_idx7][1] + 
									texture_colors[lod2_tex_idx8][1] + texture_colors[lod2_tex_idx9][1] + texture_colors[lod2_tex_idx10][1] + texture_colors[lod2_tex_idx11][1] + 
									texture_colors[lod2_tex_idx12][1] + texture_colors[lod2_tex_idx13][1] + texture_colors[lod2_tex_idx14][1] + texture_colors[lod2_tex_idx15][1])
						/ 16.0f);
					int lod2_b = ((texture_colors[lod2_tex_idx0][2] + texture_colors[lod2_tex_idx1][2] + texture_colors[lod2_tex_idx2][2] + texture_colors[lod2_tex_idx3][2] + 
									texture_colors[lod2_tex_idx4][2] + texture_colors[lod2_tex_idx5][2] + texture_colors[lod2_tex_idx6][2] + texture_colors[lod2_tex_idx7][2] + 
									texture_colors[lod2_tex_idx8][2] + texture_colors[lod2_tex_idx9][2] + texture_colors[lod2_tex_idx10][2] + texture_colors[lod2_tex_idx11][2] + 
									texture_colors[lod2_tex_idx12][2] + texture_colors[lod2_tex_idx13][2] + texture_colors[lod2_tex_idx14][2] + texture_colors[lod2_tex_idx15][2])
						/ 16.0f);
					

					lod2_full_vertex_list[v0_idx].color[0] = lod2_r;
					lod2_full_vertex_list[v0_idx].color[1] = lod2_g;
					lod2_full_vertex_list[v0_idx].color[2] = lod2_b;
				}

				this_height = heightmap_table[z*CHUNK_SIZE+x];
				left_height = x == 0 ? 0 : heightmap_table[z*CHUNK_SIZE+x-1];
				right_height = x == CHUNK_SIZE-1 ? 0 : heightmap_table[z*CHUNK_SIZE+x+1];
				back_height = z == 0 ? 0 : heightmap_table[(z-1)*CHUNK_SIZE+x];
				forward_height = z == CHUNK_SIZE-1 ? 0 : heightmap_table[(z+1)*CHUNK_SIZE+x];

				left_exposed = y >= left_height;
				right_exposed = y >= right_height;
				forward_exposed = y >= forward_height;
				back_exposed = y >= back_height;
				center_exposed = y == this_height;
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
	//num_vertexes_to_draw[chunk_z*2+chunk_x] = i;
	lod0_num_vertexes_to_draw = lod0_idx;
	lod1_num_vertexes_to_draw = lod1_idx;

		//}
	//}
}


#define vertex_list_count (sizeof(vertex_list)/sizeof(vertex_list[0]))
#define index_list_count (sizeof(index_list)/sizeof(index_list[0]))

static DVLB_s *lod0_program_dvlb, *lod1_program_dvlb;
static shaderProgram_s lod0_program, lod1_program;
static int uLoc_projection, uLoc_modelView;
static int lod0_uLoc_mvp, lod1_uLoc_mvp, lod2_uLoc_mvp;
static int lod0_uLoc_chunkOffset, lod1_uLoc_chunkOffset, lod2_uLoc_chunkOffset;
static C3D_Mtx projection;


static void *lod0_vbo_data, *lod1_vbo_data, *lod2_vbo_data;
static u16 *lod0_ibo_data, *lod1_ibo_data, *lod2_ibo_data;

static C3D_Tex kitten_tex, atlas_tex;
static float angleX = 0.0, angleY = 0.0;

static image_buffer[16384]; // hope this is enough


// fuck you tex3ds
typedef struct __attribute__((packed)) {
	u16 numSubTextures;
	u8  width_log2  : 3;
	u8  height_log2 : 3;
	u8  type        : 1;
	u8  format;
	u8  mipmapLevels;
} Tex3DSi_Header;


// Helper function for loading a texture from memory
static bool loadTextureFromMem(C3D_Tex* tex, C3D_TexCube* cube, const void* data, size_t size)
{	

	printf("Texture is %i bytes\n", size);

	memcpy(image_buffer, data, size);


	Tex3DS_Texture t3x = Tex3DS_TextureImport(data, size, tex, cube, true);
	if (!t3x)
		return false;

	// Delete the t3x object since we don't need it
	Tex3DS_TextureFree(t3x);
	return true;
}

static void sceneInit(void)
{
	// Load the vertex and geometry shader, create a shader program and bind it
	lod0_program_dvlb = DVLB_ParseFile((u32*)lod0_program_shbin, lod0_program_shbin_size);
	shaderProgramInit(&lod0_program);
	shaderProgramSetVsh(&lod0_program, &lod0_program_dvlb->DVLE[0]);
	shaderProgramSetGsh(&lod0_program, &lod0_program_dvlb->DVLE[1], 16);
	C3D_BindProgram(&lod0_program);

	lod1_program_dvlb = DVLB_ParseFile((u32*)lod1_program_shbin, lod1_program_shbin_size);
	shaderProgramInit(&lod1_program);
	shaderProgramSetVsh(&lod1_program, &lod1_program_dvlb->DVLE[0]);
	shaderProgramSetGsh(&lod1_program, &lod1_program_dvlb->DVLE[1], 16);

	// Get the location of the uniforms
	//uLoc_projection   = shaderInstanceGetUniformLocation(program.vertexShader, "projection");
	//uLoc_modelView    = shaderInstanceGetUniformLocation(program.vertexShader, "modelView");

	lod0_uLoc_mvp    = shaderInstanceGetUniformLocation(lod0_program.vertexShader, "mvp");
	lod0_uLoc_chunkOffset  = shaderInstanceGetUniformLocation(lod0_program.vertexShader, "chunk_offset");

	lod1_uLoc_mvp    = shaderInstanceGetUniformLocation(lod1_program.vertexShader, "mvp");
	lod1_uLoc_chunkOffset  = shaderInstanceGetUniformLocation(lod1_program.vertexShader, "chunk_offset");
	
	lod2_uLoc_mvp = lod1_uLoc_mvp;
	lod2_uLoc_chunkOffset = lod1_uLoc_chunkOffset;

	mesh_chunk();

	// Create the VBO (vertex buffer object)

	lod0_vbo_data = mmAlloc(sizeof(lod0_full_vertex_list));
	mmCopy(lod0_vbo_data, lod0_full_vertex_list, sizeof(lod0_full_vertex_list));

	lod1_vbo_data = mmAlloc(sizeof(lod1_full_vertex_list));
	mmCopy(lod1_vbo_data, lod1_full_vertex_list, sizeof(lod1_full_vertex_list));

	//lod2_vbo_data = mmAlloc(sizeof(lod2_full_vertex_list));
	//mmCopy(lod2_vbo_data, lod2_full_vertex_list, sizeof(lod2_full_vertex_list));

	lod0_ibo_data = mmAlloc(sizeof(lod0_full_index_list));
	mmCopy(lod0_ibo_data, lod0_full_index_list, sizeof(lod0_full_index_list));

	lod1_ibo_data = mmAlloc(sizeof(lod1_full_index_list));
	mmCopy(lod1_ibo_data, lod1_full_index_list, sizeof(lod1_full_index_list));

	//lod2_ibo_data = mmAlloc(sizeof(lod2_full_index_list));
	//mmCopy(lod2_ibo_data, lod2_full_index_list, sizeof(lod2_full_index_list));
	
	


	printf("meshed chunk\n");

	// Compute the projection matrix
	Mtx_PerspTilt(&projection, C3D_AngleFromDegrees(80.0f), C3D_AspectRatioTop, 0.1f, 4000.0f, false);

	if (!loadTextureFromMem(&atlas_tex, NULL, atlas_t3x, atlas_t3x_size)) {
		svcBreak(USERBREAK_PANIC);
	}

	C3D_TexSetFilter(&atlas_tex, GPU_NEAREST, GPU_NEAREST);
	C3D_TexSetFilterMipmap(&atlas_tex, GPU_LINEAR);
	C3D_TexBind(0, &atlas_tex);


}

float camX = 256.0f; //(CHUNKS_X/2.0f) * CHUNK_SIZE; // 256.0f;
float camY = 33.0f;
float camZ = 256.0f; //(CHUNKS_Z/2.0f) * CHUNK_SIZE; // 256.0f

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

#define LOD0_BASE (LOD2_SZ+LOD1_SZ)
#define LOD1_BASE LOD2_SZ
#define LOD2_BASE 0


int num_lod_lists = 2;
lod_list lod_lists[3] = {
	{.lod_level = 0, .num_els = 16, 
		.els = {
			{.x=LOD0_BASE+LOD0_SZ*0,.y=LOD0_BASE+LOD0_SZ*0}, 
			{.x=LOD0_BASE+LOD0_SZ*1,.y=LOD0_BASE+LOD0_SZ*0}, 
			{.x=LOD0_BASE+LOD0_SZ*2,.y=LOD0_BASE+LOD0_SZ*0}, 
			{.x=LOD0_BASE+LOD0_SZ*3,.y=LOD0_BASE+LOD0_SZ*0},

			{.x=LOD0_BASE+LOD0_SZ*0,.y=LOD0_BASE+LOD0_SZ*1}, 
			{.x=LOD0_BASE+LOD0_SZ*1,.y=LOD0_BASE+LOD0_SZ*1}, 
			{.x=LOD0_BASE+LOD0_SZ*2,.y=LOD0_BASE+LOD0_SZ*1}, 
			{.x=LOD0_BASE+LOD0_SZ*3,.y=LOD0_BASE+LOD0_SZ*1},
			
			{.x=LOD0_BASE+LOD0_SZ*0,.y=LOD0_BASE+LOD0_SZ*2}, 
			{.x=LOD0_BASE+LOD0_SZ*1,.y=LOD0_BASE+LOD0_SZ*2}, 
			{.x=LOD0_BASE+LOD0_SZ*2,.y=LOD0_BASE+LOD0_SZ*2}, 
			{.x=LOD0_BASE+LOD0_SZ*3,.y=LOD0_BASE+LOD0_SZ*2},
			
			{.x=LOD0_BASE+LOD0_SZ*0,.y=LOD0_BASE+LOD0_SZ*3}, 
			{.x=LOD0_BASE+LOD0_SZ*1,.y=LOD0_BASE+LOD0_SZ*3}, 
			{.x=LOD0_BASE+LOD0_SZ*2,.y=LOD0_BASE+LOD0_SZ*3}, 
			{.x=LOD0_BASE+LOD0_SZ*3,.y=LOD0_BASE+LOD0_SZ*3},
		}
	},
	{.lod_level = 1, .num_els = 12,
	.els = {
		{.x = LOD1_BASE+LOD1_SZ*0, .y = LOD1_BASE+LOD1_SZ*0},
		{.x = LOD1_BASE+LOD1_SZ*1, .y = LOD1_BASE+LOD1_SZ*0},
		{.x = LOD1_BASE+LOD1_SZ*2, .y = LOD1_BASE+LOD1_SZ*0},
		{.x = LOD1_BASE+LOD1_SZ*3, .y = LOD1_BASE+LOD1_SZ*0},

		{.x = LOD1_BASE+LOD1_SZ*0, .y = LOD1_BASE+LOD1_SZ*1},
		{.x = LOD1_BASE+LOD1_SZ*3, .y = LOD1_BASE+LOD1_SZ*1},

		{.x = LOD1_BASE+LOD1_SZ*0, .y = LOD1_BASE+LOD1_SZ*2},
		{.x = LOD1_BASE+LOD1_SZ*3, .y = LOD1_BASE+LOD1_SZ*2},

		{.x = LOD1_BASE+LOD1_SZ*0, .y = LOD1_BASE+LOD1_SZ*3},
		{.x = LOD1_BASE+LOD1_SZ*1, .y = LOD1_BASE+LOD1_SZ*3},
		{.x = LOD1_BASE+LOD1_SZ*2, .y = LOD1_BASE+LOD1_SZ*3},
		{.x = LOD1_BASE+LOD1_SZ*3, .y = LOD1_BASE+LOD1_SZ*3},
	}},
	{.lod_level = 2, .num_els = 12,
	.els = {
		{.x = LOD2_BASE+LOD2_SZ*0, .y = LOD2_BASE+LOD2_SZ*0},
		{.x = LOD2_BASE+LOD2_SZ*1, .y = LOD2_BASE+LOD2_SZ*0},
		{.x = LOD2_BASE+LOD2_SZ*2, .y = LOD2_BASE+LOD2_SZ*0},
		{.x = LOD2_BASE+LOD2_SZ*3, .y = LOD2_BASE+LOD2_SZ*0},

		
		{.x = LOD2_BASE+LOD2_SZ*0, .y = LOD2_BASE+LOD2_SZ*1},
		{.x = LOD2_BASE+LOD2_SZ*3, .y = LOD2_BASE+LOD2_SZ*1},
		
		{.x = LOD2_BASE+LOD2_SZ*0, .y = LOD2_BASE+LOD2_SZ*2},
		{.x = LOD2_BASE+LOD2_SZ*3, .y = LOD2_BASE+LOD2_SZ*2},

		{.x = LOD2_BASE+LOD2_SZ*0, .y = LOD2_BASE+LOD2_SZ*3},
		{.x = LOD2_BASE+LOD2_SZ*1, .y = LOD2_BASE+LOD2_SZ*3},
		{.x = LOD2_BASE+LOD2_SZ*2, .y = LOD2_BASE+LOD2_SZ*3},
		{.x = LOD2_BASE+LOD2_SZ*3, .y = LOD2_BASE+LOD2_SZ*3},
	}}
};

void set_lod0_attr_info() {
	// Configure attributes for use with the vertex shader
	C3D_AttrInfo* attrInfo = C3D_GetAttrInfo();
	AttrInfo_Init(attrInfo);
	
	AttrInfo_AddLoader(attrInfo, 0, GPU_UNSIGNED_BYTE, 3); // v0=position
	AttrInfo_AddLoader(attrInfo, 1, GPU_UNSIGNED_BYTE, 1); // v1=material
}

void bind_lod0_vbo() {
	// Configure buffers
	C3D_BufInfo* bufInfo = C3D_GetBufInfo();
	BufInfo_Init(bufInfo);
	BufInfo_Add(bufInfo, lod0_vbo_data, sizeof(lod0_vertex), 2, 0x10);
}

void set_lod0_texenv() {
	C3D_TexEnv* env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, 0, 0);
	C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
}

void bind_lod0_program() {
	C3D_BindProgram(&lod0_program);
}

void set_lod1_attr_info() {
	// Configure attributes for use with the vertex shader
	C3D_AttrInfo* attrInfo = C3D_GetAttrInfo();
	AttrInfo_Init(attrInfo);
	
	AttrInfo_AddLoader(attrInfo, 0, GPU_UNSIGNED_BYTE, 3); // v0=position
	AttrInfo_AddLoader(attrInfo, 1, GPU_UNSIGNED_BYTE, 3); // v1=color
}

void bind_lod1_vbo() {
	
	// Configure buffers
	C3D_BufInfo* bufInfo = C3D_GetBufInfo();
	BufInfo_Init(bufInfo);
	BufInfo_Add(bufInfo, lod1_vbo_data, sizeof(lod1_vertex), 2, 0x10);
}

void set_lod1_texenv() {
	C3D_TexEnv* env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, 0, 0);
	C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
}

void bind_lod1_program() {
	C3D_BindProgram(&lod1_program);
}

void set_lod2_attr_info() {
	set_lod1_attr_info();
}

void bind_lod2_vbo() {
	C3D_BufInfo* bufInfo = C3D_GetBufInfo();
	BufInfo_Init(bufInfo);
	BufInfo_Add(bufInfo, lod2_vbo_data, sizeof(lod1_vertex), 2, 0x10);
}

void set_lod2_texenv() {
	set_lod1_texenv();
}

void bind_lod2_program() {
	bind_lod1_program();
}

void draw_scene(C3D_Mtx* matrix) {
	for(int lod = 0; lod < num_lod_lists; lod++) {
		int mvp_offset, chunk_off_offset;
		int num_verts = 0; // default to 0 in case of LOD2 somehow
		void* index_buffer = NULL;
		switch(lod) {
			case 0:
				//printf("lod 0\n");
				bind_lod0_program();
				bind_lod0_vbo();
				set_lod0_attr_info();
				set_lod0_texenv();
				mvp_offset = lod0_uLoc_mvp;
				chunk_off_offset = lod0_uLoc_chunkOffset;
				num_verts = lod0_num_vertexes_to_draw;
				index_buffer = lod0_ibo_data;
				break;
			case 1:
				//printf("lod 1\n");
				bind_lod1_program();
				bind_lod1_vbo();
				set_lod1_attr_info();
				set_lod1_texenv();
				mvp_offset = lod1_uLoc_mvp;
				chunk_off_offset = lod1_uLoc_chunkOffset;
				num_verts = lod1_num_vertexes_to_draw;
				index_buffer = lod1_ibo_data;
				break;
			case 2:
				bind_lod2_program();
				bind_lod2_vbo();
				set_lod2_attr_info();
				set_lod2_texenv();
				mvp_offset = lod2_uLoc_mvp;
				chunk_off_offset = lod2_uLoc_chunkOffset;
				index_buffer = lod1_ibo_data;
				num_verts = 0;
				break;
		}
		//continue;
		//printf("LOD %i -> %i vertexes\n", lod, num_verts);
		//printf(" index buffer %i\n", index_buffer);
		//printf("chunks in lod: %i\n", lod_lists[lod].num_els);
		//num_verts = 0;

		C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, mvp_offset, matrix);
		for(int i = 0; i < lod_lists[lod].num_els; i++) {
			// hacky hack
			
			//printf("drawing chunk at %i,%i\n", lod_lists[lod].els[i].x, lod_lists[lod].els[i].y);
			C3D_FVUnifSet(GPU_VERTEX_SHADER, chunk_off_offset, lod_lists[lod].els[i].x, 0.0f, lod_lists[lod].els[i].y, 0.0f);
			//C3D_FVUnifSet(GPU_VERTEX_SHADER, chunk_off_offset, 0.0f, 0.0f, 0.0f, 0.0f);
			C3D_DrawElements(GPU_GEOMETRY_PRIM, num_verts, C3D_UNSIGNED_SHORT, index_buffer);
		}
	}
}

static void sceneRender(void)
{
	// Calculate the modelView matrix
	C3D_Mtx modelView;	
	Mtx_Identity(&modelView);
	Mtx_RotateX(&modelView, -C3D_AngleFromDegrees(angleX), true);
	Mtx_RotateY(&modelView, -C3D_AngleFromDegrees(angleY), true);
	
	Mtx_Translate(&modelView, -camX, -camY, -camZ, true); 

	// Rotate the cube each frame
	//angleX += M_PI / 180;
	//angleY += M_PI / 180;

	C3D_Mtx modelViewProjection;
	Mtx_Multiply(&modelViewProjection, &projection, &modelView);


	draw_scene(&modelViewProjection);
	return;
	
	//bind_lod0_program();
	//bind_lod0_vbo();
	//set_lod0_attr_info();
	//set_lod0_texenv();

	for(int chunk_x = 0; chunk_x < CHUNKS_X; chunk_x++) {
		for(int chunk_z = 0; chunk_z < CHUNKS_Z; chunk_z++) {
			int center_chunk_x = chunk_x*chunk_size + (chunk_size/2.0f);
			int center_chunk_z = chunk_z*chunk_size + (chunk_size/2.0f);
			
			float dx = fabsf((center_chunk_x - camX));
			float dz = fabsf((center_chunk_z - camZ));
			int within_range = ((dx <= CHUNK_SIZE*1.5) && (dz <= CHUNK_SIZE*1.5)) ? 1 : 0;

			//C3D_FVec base_vec = FVec3_New(chunk_x * chunk_size, 0.0f, chunk_z * chunk_size);
			//C3D_FVec max_vec = FVec3_New(chunk_x * chunk_size + chunk_size + 1.0f, chunk_size, chunk_z * chunk_size + chunk_size + 1.0f);
			//C3D_FVec mid_vec = FVec3_Scale(FVec3_Add(base_vec, max_vec), 0.5f);
	        //C3D_FVec to_center = FVec3_Subtract(FVec3_Scale(FVec3_Add(base_vec, max_vec), 1.0/2.0f), cam_pos_vec);

			C3D_FVUnifSet(GPU_VERTEX_SHADER, lod0_uLoc_chunkOffset, chunk_x*chunk_size, 0.0f, chunk_z*chunk_size, 0.0f);
			C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, lod0_uLoc_mvp, &modelViewProjection);
			//for(int face = 0; face < 6; face++) {
			//	float d = FVec3_Dot(to_center, 
			//						FVec3_New(face_normals[face].x, face_normals[face].y, face_normals[face].z));
				//if(d <= 0) { 

					//chunk_faces_drawn += 1;
			int chunk_z_idx = chunk_z & 1;
			int chunk_x_idx = chunk_x & 1;
			//printf("drawing %i verts\n", num_vertexes_to_draw[chunk_z_idx*2+chunk_x_idx]);
			//if(within_range) {
			//	C3D_DrawElements(GPU_GEOMETRY_PRIM, num_vertexes_to_draw[chunk_z_idx*2+chunk_x_idx], C3D_UNSIGNED_SHORT, ibo_data_list[chunk_z_idx*2+chunk_x_idx]);
			//} else {
				//C3D_DrawElements(GPU_GEOMETRY_PRIM, num_vertexes_to_draw[chunk_z_idx*2+chunk_x_idx], C3D_UNSIGNED_SHORT, ibo_data_list[chunk_z_idx*2+chunk_x_idx]);
			//}

		}
	}
	//printf("%i/%i faces backface culled\n", (total_chunk_faces-chunk_faces_drawn), total_chunk_faces);
}


static void sceneExit(void)
{
	// Free the texture
	C3D_TexDelete(&kitten_tex);
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

	// Initialize the render target
	C3D_RenderTarget* target = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
	C3D_RenderTargetSetOutput(target, GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);

	// Initialize the scene
	sceneInit();

	C3D_CullFace(GPU_CULL_BACK_CCW);

	// Main loop

	int last_frame_ms = 16;

	while (aptMainLoop())
	{
		
		u64 msStart = osGetTime();
		hidScanInput();

		// Respond to user input
		u32 kDown = hidKeysDown();
		u32 kHeld = hidKeysHeld();
		if (kDown & KEY_START) {
			break; // break in order to return to hbmenu
		}
		if (kHeld & KEY_CSTICK_LEFT) {
			angleY += last_frame_ms/16.0f;
		} else if(kHeld & KEY_CSTICK_RIGHT) {
			angleY -= last_frame_ms/16.0f;
		}
		
		if (kHeld & KEY_CSTICK_UP) {
			angleX += last_frame_ms/16.0f;
		} else if(kHeld & KEY_CSTICK_DOWN) {
			angleX -= last_frame_ms/16.0f;
		}

		// Compute forward vector from angles
		float cosPitch = cosf(C3D_AngleFromDegrees(angleX));
		float sinPitch = sinf(C3D_AngleFromDegrees(angleX));
		float cosYaw   = cosf(C3D_AngleFromDegrees(angleY));
		float sinYaw   = sinf(C3D_AngleFromDegrees(angleY));

		float forwardX = -cosPitch * sinYaw;
		float forwardY = sinPitch;
		float forwardZ = -cosPitch * cosYaw;
		float speed = last_frame_ms/80.0f; //0.2f at 60fps, scale for larger frame t imes

		circlePosition cpos;
		hidCircleRead(&cpos);

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

		camX += forwardX * lerpSpeed;
		camY += forwardY * lerpSpeed;
		camZ += forwardZ * lerpSpeed;

		if (cpos.dx < 0) {
			float dx = cpos.dx / -154.0f;
			dx = dx > 1.0f ? 1.0f : dx;
			angleY += lerp(0, 1.0f, dx);
		} else if(cpos.dx > 0) {
			float dx = cpos.dx / 154.0f;
			dx = dx > 1.0f ? 1.0f : dx;
			angleY -= lerp(0, 1.0f, dx);
		}

		// Render the scene
		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
			C3D_RenderTargetClear(target, C3D_CLEAR_ALL, CLEAR_COLOR, 0);
			C3D_FrameDrawOn(target);
			sceneRender();
		C3D_FrameEnd(0);

		
		u64 msEnd = osGetTime();	
		s64 elapsed_ms = msEnd - msStart;
		last_frame_ms = elapsed_ms;
		//printf("%i ms\n", elapsed_ms);
		//printf("%i fps\n", elapsed_ms, (int)(1000.0f/elapsed_ms));
	}

	// Deinitialize the scene
	sceneExit();

	// Deinitialize graphics
	C3D_Fini();
	gfxExit();
	return 0;
}