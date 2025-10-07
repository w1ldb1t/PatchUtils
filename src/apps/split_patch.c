#include "libs/patch/patch.h"
#include "libs/util/util.h"

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *prog)
{
	fprintf(stderr, "Usage: %s <patch-file>\n", prog);
}

static void sanitize_filename(const char *input, char *output, size_t size,
			      size_t fallback_index)
{
	size_t out_idx = 0;
	for (size_t i = 0; input[i] != '\0' && out_idx + 1 < size; ++i) {
		unsigned char ch = (unsigned char)input[i];
		if (ch == '/' || ch == '\\' || ch == '.') {
			output[out_idx++] = '_';
		} else if (isalnum(ch) || ch == '-' || ch == '_') {
			output[out_idx++] = (char)ch;
		} else {
			output[out_idx++] = '_';
		}
	}

	if (out_idx == 0) {
		FORMAT_MSG_INTO(output, "section_%zu", fallback_index);
	} else {
		output[out_idx] = '\0';
	}
}

static bool write_section_callback(const patch_section *section, void *userdata)
{
	size_t *section_counter = (size_t *)userdata;

	char sanitized[512];
	sanitize_filename(section->path, sanitized, sizeof(sanitized),
			  *section_counter);
	++(*section_counter);

	char filename[512];
	FORMAT_MSG_INTO(filename, "%s.patch", sanitized);

	FILE *output = fopen(filename, "w");
	if (!output) {
		fprintf(stderr, "Error: unable to create %s: %s\n", filename,
			strerror(errno));
		return false;
	}

	printf("Extracting: %s\n", filename);

	size_t written = fwrite(section->data, 1, section->length, output);
	fclose(output);

	if (written != section->length) {
		fprintf(stderr, "Error: failed to write full contents to %s\n",
			filename);
		return false;
	}

	return true;
}

int main(int argc, char **argv)
{
	if (argc != 2) {
		usage(argv[0]);
		return 1;
	}

	const char *input_path = argv[1];
	FILE *input = fopen(input_path, "r");
	if (!input) {
		fprintf(stderr, "Error: unable to open %s: %s\n", input_path,
			strerror(errno));
		return 1;
	}

	size_t counter = 0U;
	bool ok = patch_parse(input, write_section_callback, &counter);
	fclose(input);

	if (!ok || counter == 0U) {
		if (ok) {
			fprintf(stderr, "No patch sections were found in %s\n",
				input_path);
		}
		return 1;
	}

	return 0;
}
