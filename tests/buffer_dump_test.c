#include "clunk_c.h"

#include <stdio.h>
#include <string.h>

static int check_capacity(clunk_buffer *buffer, const char *expected, size_t capacity)
{
	const unsigned char guard = 0xa5;
	const size_t length = strlen(expected);
	const size_t copied = capacity == 0 ? 0 :
		(length < capacity - 1 ? length : capacity - 1);
	unsigned char *storage = (unsigned char *)malloc(capacity + 2);
	char *out;
	size_t i;
	int ok = 1;
	if (storage == NULL)
		return 0;
	memset(storage, guard, capacity + 2);
	out = (char *)(storage + 1);
	clunk_buffer_dump(buffer, out, capacity);

	if (storage[0] != guard || storage[capacity + 1] != guard)
		ok = 0;
	if (capacity > 0) {
		if (memcmp(out, expected, copied) != 0 || out[copied] != '\0')
			ok = 0;
		/* A short dump must not overwrite the unused part of the buffer. */
		for (i = copied + 1; i < capacity; ++i) {
			if (storage[i + 1] != guard)
				ok = 0;
		}
	}
	if (!ok)
		fprintf(stderr, "buffer dump failed: capacity=%lu, length=%lu\n",
			(unsigned long)capacity, (unsigned long)length);
	free(storage);
	return ok;
}

static int check_dump(clunk_buffer *buffer, const char *expected)
{
	size_t capacity;
	int ok = 1;
	clunk_buffer_dump(buffer, NULL, 0);
	/* Include zero, one byte, every truncation point, exact fit and spare room. */
	for (capacity = 0; capacity <= strlen(expected) + 8; ++capacity) {
		if (!check_capacity(buffer, expected, capacity))
			ok = 0;
	}
	return ok;
}

int main(void)
{
	const unsigned char data[] = {
		0x00, 0x1f, 0x20, 0x41, 0x7e, 0x7f, 0x80, 0xff,
		0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37
	};
	const char expected[] =
		"-[memory dump]-[size: 16]---\n000000\t"
		"00 1f 20 41 7e 7f 80 ff  30 31 32 33 34 35 36 37 \t\t"
		".. A~... 01234567";
	clunk_buffer *buffer = clunk_buffer_create();
	char saved[sizeof(expected)];
	int ok = check_dump(buffer, "empty memory buffer");
	clunk_buffer_set_data(buffer, data, sizeof(data));
	if (!check_dump(buffer, expected))
		ok = 0;
	clunk_buffer_dump(buffer, saved, sizeof(saved));
	clunk_buffer_fill(buffer, 0);
	if (!check_dump(buffer,
		"-[memory dump]-[size: 16]---\n000000\t"
		"00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00 \t\t"
		"........ ........"))
		ok = 0;
	clunk_buffer_destroy(buffer);
	/* The caller's copy must survive another dump, mutation and destruction. */
	if (strcmp(saved, expected) != 0) {
		fprintf(stderr, "buffer dump did not preserve the caller's copy\n");
		ok = 0;
	}
	return ok ? 0 : 1;
}
