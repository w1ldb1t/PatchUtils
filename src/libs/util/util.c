#include "util.h"

#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdckdint.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool grow_buffer(char **buffer, size_t *capacity, size_t required)
{
	if (*capacity >= required) {
		return true;
	}

	size_t new_capacity = *capacity ? *capacity : 128U;
	while (new_capacity < required) {
		size_t doubled;
		if (ckd_mul(&doubled, new_capacity, 2U) != 0) {
			return false;
		}
		new_capacity = doubled;
	}

	char *resized = realloc(*buffer, new_capacity);
	if (!resized) {
		return false;
	}

	*buffer = resized;
	*capacity = new_capacity;
	return true;
}

bool utils_append_bytes(char **buffer, size_t *length, size_t *capacity,
			const char *data, size_t count)
{
	if (!buffer || !length || !capacity) {
		return false;
	}

	size_t required;
	if (ckd_add(&required, *length, count) != 0 ||
	    ckd_add(&required, required, 1U) != 0) {
		return false;
	}

	if (!grow_buffer(buffer, capacity, required)) {
		return false;
	}

	if (count > 0 && data) {
		memcpy(*buffer + *length, data, count);
	}

	*length += count;
	(*buffer)[*length] = '\0';
	return true;
}

bool utils_array_reserve(void **data, size_t *capacity, size_t min_elements,
			 size_t element_size)
{
	if (!data || !capacity || element_size == 0U) {
		return false;
	}

	if (*capacity >= min_elements) {
		return true;
	}

	size_t new_capacity = *capacity ? *capacity : 8U;
	while (new_capacity < min_elements) {
		size_t doubled;
		if (ckd_mul(&doubled, new_capacity, 2U) != 0) {
			return false;
		}
		new_capacity = doubled;
	}

	size_t bytes;
	if (ckd_mul(&bytes, new_capacity, element_size) != 0) {
		return false;
	}

	void *resized = realloc(*data, bytes);
	if (!resized) {
		return false;
	}

	*data = resized;
	*capacity = new_capacity;
	return true;
}

const char *utils_parse_token(const char *input, char *buffer,
			      size_t buffer_size)
{
	if (!buffer || buffer_size == 0U) {
		return input;
	}

	if (!input) {
		buffer[0] = '\0';
		return input;
	}

	while (*input && isspace((unsigned char)*input) != 0) {
		++input;
	}

	size_t idx = 0U;
	if (*input == '\"') {
		++input;
		while (*input && *input != '\"' && idx + 1U < buffer_size) {
			if (*input == '\\' && input[1] != '\0') {
				buffer[idx++] = input[1];
				input += 2;
			} else {
				buffer[idx++] = *input++;
			}
		}
		if (*input == '\"') {
			++input;
		}
	} else {
		while (*input && isspace((unsigned char)*input) == 0 &&
		       idx + 1U < buffer_size) {
			buffer[idx++] = *input++;
		}
	}

	buffer[idx] = '\0';

	while (*input && isspace((unsigned char)*input) == 0) {
		++input;
	}

	return input;
}

void utils_format_message(message_buf buf, const char *format, ...)
{
	if (!buf.buffer || buf.size == 0U) {
		return;
	}
	va_list args;
	va_start(args, format);
	const int written = vsnprintf(buf.buffer, buf.size, format, args);
	va_end(args);

	if (written < 0) {
		buf.buffer[0] = '\0';
		return;
	}

	if ((size_t)written >= buf.size && buf.size > 3U) {
		buf.buffer[buf.size - 4U] = '.';
		buf.buffer[buf.size - 3U] = '.';
		buf.buffer[buf.size - 2U] = '.';
		buf.buffer[buf.size - 1U] = '\0';
	}
}
