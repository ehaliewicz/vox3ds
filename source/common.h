#ifndef COMMON_H
#define COMMON_H


#define CHUNK_SIZE 31

#define LOD0_SZ 31
#define LOD1_SZ 62
#define LOD2_SZ 124


void assert(int i, int line);

#define ASSERT(v) assert((v), __LINE__)

#endif 