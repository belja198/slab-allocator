#include "helper.h"

unsigned nearest_power_of_two(unsigned int v) {
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v++;
	return v;
}

size_t highest_active_bit(size_t n) {
	size_t m = 1U << 31;
	size_t i;
	for (i = 0; i < 32; i++) {
		if (m & n)
			break;
		m >>= 1;
	}
//	return 31 - i;*/
	size_t res = 31 - i;
		//(size_t)trunc(log(n) / log(2));
	return res;
}
/*
int main() {
	highest_active_bit(1000);
	return 0;
}
*/