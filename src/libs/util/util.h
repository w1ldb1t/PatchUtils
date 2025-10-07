#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef struct {
	char *buffer;
	size_t size;
} message_buf;

void utils_format_message(message_buf buf, const char *format, ...);

#define FORMAT_MSG(name, size, ...)                                         \
	char name[size];                                                       \
	utils_format_message((message_buf){name, size}, __VA_ARGS__)

#define FORMAT_MSG_INTO(buffer, ...)                                        \
	utils_format_message((message_buf){(buffer), sizeof(buffer)},          \
			     __VA_ARGS__)

#define ZeroMemory(out) memset((out), 0, sizeof(typeof(*(out))))

bool utils_append_bytes(char **buffer, size_t *length, size_t *capacity,
			const char *data, size_t count);
bool utils_array_reserve(void **data, size_t *capacity, size_t min_elements,
			 size_t element_size);
const char *utils_parse_token(const char *input, char *buffer,
			      size_t buffer_size);
