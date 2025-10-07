#pragma once

#include <git2.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef enum {
	GITUTILS_STATUS_MODIFIED,
	GITUTILS_STATUS_UNTRACKED
} gitutils_file_status;

typedef struct {
	char *path;
	gitutils_file_status status;
} gitutils_status_entry;

typedef struct {
	gitutils_status_entry *entries;
	size_t count;
} gitutils_status_list;

int gitutils_open_repository(git_repository **out_repo, const char *path);
int gitutils_collect_status(git_repository *repo, gitutils_status_list *list);
void gitutils_status_list_free(gitutils_status_list *list);
int gitutils_write_diff_for_path(git_repository *repo, const char *path,
				 FILE *output, bool *out_has_changes);
