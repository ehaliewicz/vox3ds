#ifndef ALLOC_H
#define ALLOC_H


void mmLogVRAM();
void mmLogRAM();
bool mmIsVRAM(void *addr);
void mmInitAlloc();
void mmCopy(void *dst, void *src, size_t size);
void* mmAlloc(size_t size);
void mmFree(void* addr);
void* mmLinearAlloc(size_t size);

#endif 