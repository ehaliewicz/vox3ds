#include <stdio.h>

void assert(int i, int line) {
	if(!i) {
		printf("Assertion failed on line %i\n", line);
		while(1) { }
	}
}