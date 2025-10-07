#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef struct patch_section {
	char *path;
	char *data;
	size_t length;
	size_t capacity;
} patch_section;

typedef bool (*patch_section_callback)(const patch_section *section,
				       void *userdata);

bool patch_section_init(patch_section *section, const char *path);
void patch_section_reset(patch_section *section);
void patch_section_dispose(patch_section *section);
bool patch_section_append(patch_section *section, const char *data, size_t len);

bool patch_parse(FILE *input, patch_section_callback cb, void *userdata);
