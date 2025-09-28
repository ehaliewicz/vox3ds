#include <3ds.h>
#include <citro3d.h>
#include <tex3ds.h>
#include <string.h>

#include "alloc.h"
#include "chunk.h"
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


#define DISPLAY_TRANSFER_FLAGS \
	(GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) | \
	GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
	GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))




static DVLB_s *lod0_program_dvlb, *lod1_program_dvlb, *skybox_vshader_dvlb, 
				*mixed_program_dvlb, *per_face_program_dvlb;
static shaderProgram_s lod0_program, lod1_program, skybox_program, mixed_program, per_face_program;

static int per_face_uLoc_mvp, per_face_uLoc_chunk_offset, per_face_uLoc_is_lod_chunk_offset, per_face_uLoc_draw_top_uvs_offset;
static int skybox_uLoc_projection, skybox_uLoc_modelView;
static C3D_Mtx projection, ortho_projection, modelView, mvpMatrix;


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

static void sceneInit(void)
{

	skybox_vbo = mmLinearAlloc(sizeof(cube_vert_list));
	memcpy(skybox_vbo, cube_vert_list, sizeof(cube_vert_list));

	skybox_ibo = mmLinearAlloc(sizeof(cube_idx_list));
	memcpy(skybox_ibo, cube_idx_list, sizeof(cube_idx_list));

	// Load the vertex and geometry shader, create a shader program and bind it
	skybox_vshader_dvlb = DVLB_ParseFile((u32*)skybox_shbin, skybox_shbin_size);
	shaderProgramInit(&skybox_program);
	shaderProgramSetVsh(&skybox_program, &skybox_vshader_dvlb->DVLE[0]);

	per_face_program_dvlb = DVLB_ParseFile((u32*)per_face_program_shbin, per_face_program_shbin_size);
	shaderProgramInit(&per_face_program);
	shaderProgramSetVsh(&per_face_program, &per_face_program_dvlb->DVLE[0]);
	shaderProgramSetGsh(&per_face_program, &per_face_program_dvlb->DVLE[1], 10);


	// Get the location of the uniforms
	skybox_uLoc_projection = shaderInstanceGetUniformLocation(skybox_program.vertexShader, "projection");
	skybox_uLoc_modelView  = shaderInstanceGetUniformLocation(skybox_program.vertexShader, "modelView");

	per_face_uLoc_mvp = shaderInstanceGetUniformLocation(per_face_program.vertexShader, "mvp");
	per_face_uLoc_chunk_offset = shaderInstanceGetUniformLocation(per_face_program.vertexShader, "chunk_offset");
	per_face_uLoc_draw_top_uvs_offset = shaderInstanceGetUniformLocation(per_face_program.geometryShader, "draw_top_uvs");
	per_face_uLoc_is_lod_chunk_offset = shaderInstanceGetUniformLocation(per_face_program.geometryShader, "is_lod_chunk");


	chunks_init(); 
	
	
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
	C3D_AlphaTest(true, GPU_GEQUAL, 0x7F);
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_MAX, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	//C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_ONE, GPU_ZERO);
}


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


	chunk_init_lod_table(camX, camY, camZ);


	C3D_BindProgram(&per_face_program);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, per_face_uLoc_mvp, &mvpMatrix);

	/*
	
		draw LOD0 chunks

	*/

	set_lod0_attr_info();
	bind_lod0_vbo();
	set_lod0_texenv();
	C3D_BoolUnifSet(GPU_GEOMETRY_SHADER, per_face_uLoc_is_lod_chunk_offset, false);


	//int verts = 0;

	//int total_lod0_meshes = 0;
	//int drawn_lod0_meshes = 0;
	draw_lod0_chunks(&mvpMatrix, per_face_uLoc_chunk_offset, per_face_uLoc_draw_top_uvs_offset, camX, camY, camZ);

	

	bind_lod1_vbo();
	set_lod1_attr_info();
	set_lod1_texenv();
	C3D_BoolUnifSet(GPU_GEOMETRY_SHADER, per_face_uLoc_is_lod_chunk_offset, true);

	
	draw_lod1_chunks(&mvpMatrix, per_face_uLoc_chunk_offset, camX, camY, camZ);


	//static int max_lod0_meshes = 0;
	//static int max_lod1_meshes = 0;

	//max_lod0_meshes = MAX(drawn_lod0_meshes, max_lod0_meshes);
	//max_lod1_meshes = MAX(drawn_lod1_meshes, max_lod1_meshes);
	

	//printf("max %i LOD0 meshes, max LOD1 meshes %i\n", max_lod0_meshes, max_lod1_meshes);
	//printf("polys %i\n", verts/3);

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

	printf("mem type %li\n", osGetApplicationMemType());
	//printf("linear heap size %i\n", envGetLinearHeapSize());
	printf("heap size %li\n", envGetHeapSize());
	mmInitAlloc();

	//C3D_Tex renderTexture;
	// Initialize the render target
	//loadTextureFromMem(&renderTexture, NULL, render_target_tex_t3x, render_target_tex_t3x_size);
	//C3D_RenderTarget* texRenderTarget = C3D_RenderTargetCreateFromTex(&renderTexture, GPU_TEXFACE_2D, 0, GPU_RB_DEPTH24_STENCIL8);
	//C3D_EarlyDepthTest(true, GPU_EARLYDEPTH_LESS, 0xFFFFFF);

	
	C3D_RenderTarget* screenTarget = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
	C3D_RenderTargetSetOutput(screenTarget, GSP_SCREEN_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);

	sceneInit();

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
		float speed = 0.15f; //


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

		float gpu_time = C3D_GetDrawingTime()*6.0f;
		float cpu_time = C3D_GetProcessingTime()*6.0f;
		float max_time = MAX(gpu_time, cpu_time);
		float fps = 1000.0f/(max_time*16.0f/100.0f);

		//printf("cpu %.2f%% gpu %.2f%% %.2f fps\n", cpu_time, gpu_time, fps);
		//printf("angX %.2f angY %.2f cam %.2f,%.2f,%.2f\n", angleX*57.29f, angleY*57.29f, camX, camY, camZ);
		//printf("camx %f\n", camX);
	}

	// Deinitialize the scene
	sceneExit();

	// Deinitialize graphics
	C3D_Fini();
	gfxExit();
	return 0;
}