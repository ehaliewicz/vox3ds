#ifndef COMMON_H
#define COMMON_H


#define CHUNK_SIZE 32

#define LOD0_SZ (CHUNK_SIZE)
#define LOD1_SZ (CHUNK_SIZE*2)


void assert(int i, int line);

#define ASSERT(v) assert((v), __LINE__)



#define MIN(x,y) ((x)<(y)?(x):(y))
#define MAX(x,y) ((x)>(y)?(x):(y))
#define CLAMP(a,x,y) MAX(x,MIN(a,y))


#define NEAR_PLANE_DIST 0.1f
#define FAR_PLANE_DIST 1000.0f

#define HFOV_DEGREES 90.0f
#define HFOV 1.57f
#define VFOV (2.0f * atanf(tanf(HFOV*0.5f) / C3D_AspectRatioTop))



#define CLEAR_COLOR 0x68B0D8FF

#endif 