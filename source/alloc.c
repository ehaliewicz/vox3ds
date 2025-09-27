#include <3ds.h>
#include <citro3d.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "common.h"
#include "types.h"

static int VRAM_TOTAL = 0;
static int RAM_TOTAL = 0;

void mmLogVRAM() {
	//printf("VRAM: %lu / %i kB\n", (VRAM_TOTAL - vramSpaceFree()) / 1024, VRAM_TOTAL / 1024);
}
void mmLogRAM() {
	//printf("RAM: %lu / %i kB\n", (RAM_TOTAL - linearSpaceFree()) / 1024, RAM_TOTAL / 1024);
}

bool mmIsVRAM(void *addr) {
	u32 vaddr = (u32)addr;
	return vaddr >= 0x1F000000 && vaddr < 0x1F600000;
}


void mmInitAlloc() {
	vramFree(vramAlloc(0));
	VRAM_TOTAL = vramSpaceFree();
    RAM_TOTAL = linearSpaceFree();
    printf("ram total is %i\n", RAM_TOTAL);
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


void* mmLinearAlloc(size_t size) {
	//printf("Allocating %i bytes of ram\n",  size);
    void* addr = linearAlloc(size);
    ASSERT(addr != NULL);
    mmLogRAM();
    return addr;
}

void* mmAlloc(size_t size) {
	//printf("Allocating %i bytes of vram\n",  size);
	void *addr = vramAlloc(size);
	if (!addr) {
		printf("! OUT OF VRAM %lu < %i\n", vramSpaceFree() / 1024, size / 1024);
		addr = mmLinearAlloc(size);
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