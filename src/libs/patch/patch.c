#include "patch.h"

#include "util/util.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

bool patch_section_init(patch_section *section, const char *path)
{
	assert(section);
	assert(path);

	ZeroMemory(section);

	section->path = strdup(path);
	if (!section->path) {
		return false;
	}

	return true;
}

void patch_section_reset(patch_section *section)
{
	assert(section);

	free(section->path);
	free(section->data);
	ZeroMemory(section);
}

bool patch_section_append(patch_section *section, const char *data, size_t len)
{
	assert(section);

	return utils_append_bytes(&section->data, &section->length,
				  &section->capacity, data, len);
}

bool patch_parse(FILE *input, patch_section_callback cb, void *userdata)
{
	assert(input);
	assert(cb);

	patch_section current = {0};

	char *line = NULL;
	size_t line_cap = 0U;
	ssize_t line_len;
	bool ok = true;

	while ((line_len = getline(&line, &line_cap, input)) != -1) {
		// check if we are at the start of a new patch section
		if (strncmp(line, "diff --git", 10) == 0) {
			// it isn't our first patch?
			if (current.path) {
				// inform the callback that we got a new patch
				if (!cb(&current, userdata)) {
					ok = false;
					break;
				}
				patch_section_reset(&current);
			}

			// get the file names
			const char *cursor = line + 10;
			char old_path[512];
			char new_path[512];
			cursor = utils_parse_token(cursor, old_path,
						   sizeof(old_path));
			cursor = utils_parse_token(cursor, new_path,
						   sizeof(new_path));

			const char *target = new_path[0] ? new_path : old_path;
			if (strncmp(target, "a/", 2) == 0 ||
			    strncmp(target, "b/", 2) == 0) {
				target += 2;
			}

			// initialize |current|
			if (!patch_section_init(&current, target)) {
				ok = false;
				break;
			}

			// append the first line
			if (!patch_section_append(&current, line,
						  (size_t)line_len)) {
				ok = false;
				break;
			}
			continue;
		}

		// consume one more line of the current patch
		if (current.path) {
			if (!patch_section_append(&current, line,
						  (size_t)line_len)) {
				ok = false;
				break;
			}
		}
	}

	free(line);

	if (ok && current.path) {
		ok = cb(&current, userdata);
	}

	patch_section_reset(&current);
	return ok;
}
