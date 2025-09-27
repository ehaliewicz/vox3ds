#ifndef TYPES_H
#define TYPES_H

typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef signed short s16;
typedef unsigned long u32;
typedef signed long s32;

typedef struct { u8 position[3]; u8 material; } lod0_vertex;
typedef struct { u8 position[3]; u8 color[3]; } lod1_vertex;

#endif