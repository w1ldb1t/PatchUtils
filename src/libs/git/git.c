#include "git.h"

#include "util/util.h"

#include <stdlib.h>
#include <string.h>

static int status_entry_should_include(unsigned int status)
{
	const unsigned int modified_flags =
		GIT_STATUS_INDEX_NEW | GIT_STATUS_INDEX_MODIFIED |
		GIT_STATUS_INDEX_DELETED | GIT_STATUS_INDEX_TYPECHANGE |
		GIT_STATUS_INDEX_RENAMED | GIT_STATUS_WT_MODIFIED |
		GIT_STATUS_WT_DELETED | GIT_STATUS_WT_TYPECHANGE |
		GIT_STATUS_WT_RENAMED;

	if ((status & GIT_STATUS_WT_NEW) != 0) {
		return GITUTILS_STATUS_UNTRACKED;
	}

	if ((status & modified_flags) != 0) {
		return GITUTILS_STATUS_MODIFIED;
	}

	return -1;
}

int gitutils_open_repository(git_repository **out_repo, const char *path)
{
	if (!out_repo) {
		return GIT_ERROR;
	}

	return git_repository_open_ext(out_repo, path ? path : ".",
				       GIT_REPOSITORY_OPEN_CROSS_FS, nullptr);
}

int gitutils_collect_status(git_repository *repo, gitutils_status_list *list)
{
	if (!repo || !list) {
		return GIT_ERROR;
	}

	list->entries = nullptr;
	list->count = 0U;

	git_status_options options;
	git_status_options_init(&options, GIT_STATUS_OPTIONS_VERSION);
	options.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
	options.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED |
			GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS |
			GIT_STATUS_OPT_DISABLE_PATHSPEC_MATCH |
			GIT_STATUS_OPT_INCLUDE_UNMODIFIED;

	git_status_list *status_list = nullptr;
	gitutils_status_entry *entries = nullptr;
	size_t out_index = 0U;

	int error = git_status_list_new(&status_list, repo, &options);
	if (error < 0) {
		return error;
	}

	const size_t count = git_status_list_entrycount(status_list);
	if (count > 0U) {
		entries = calloc(count, sizeof(*entries));
		if (!entries) {
			error = GIT_ERROR;
			goto cleanup;
		}
	}

	for (size_t i = 0; i < count; ++i) {
		const git_status_entry *entry =
			git_status_byindex(status_list, i);
		if (!entry) {
			continue;
		}

		const int include = status_entry_should_include(entry->status);
		if (include < 0) {
			continue;
		}

		const char *path = nullptr;
		if (entry->index_to_workdir &&
		    entry->index_to_workdir->new_file.path) {
			path = entry->index_to_workdir->new_file.path;
		} else if (entry->head_to_index &&
			   entry->head_to_index->new_file.path) {
			path = entry->head_to_index->new_file.path;
		}

		if (!path) {
			continue;
		}

		char *path_copy = strdup(path);
		if (!path_copy) {
			error = GIT_ERROR;
			goto cleanup;
		}

		entries[out_index].path = path_copy;
		entries[out_index].status = (gitutils_file_status)include;
		++out_index;
	}

	if (out_index == 0U) {
		free(entries);
		entries = nullptr;
	}

	list->entries = entries;
	list->count = out_index;

cleanup:
	if (error < 0) {
		if (entries) {
			for (size_t i = 0; i < out_index; ++i) {
				free(entries[i].path);
			}
			free(entries);
		}
	}

	git_status_list_free(status_list);

	return error < 0 ? error : 0;
}

void gitutils_status_list_free(gitutils_status_list *list)
{
	if (!list || !list->entries) {
		return;
	}

	for (size_t i = 0; i < list->count; ++i) {
		free(list->entries[i].path);
	}
	free(list->entries);
	list->entries = nullptr;
	list->count = 0;
}

int gitutils_write_diff_for_path(git_repository *repo, const char *path,
				 FILE *output, bool *out_has_changes)
{
	if (out_has_changes) {
		*out_has_changes = false;
	}

	if (!repo || !path || !output) {
		return GIT_ERROR;
	}

	git_diff_options diff_opts;
	git_diff_options_init(&diff_opts, GIT_DIFF_OPTIONS_VERSION);
	diff_opts.flags = GIT_DIFF_INCLUDE_UNTRACKED |
			  GIT_DIFF_SHOW_UNTRACKED_CONTENT |
			  GIT_DIFF_RECURSE_UNTRACKED_DIRS |
			  GIT_DIFF_DISABLE_PATHSPEC_MATCH;

	char *pathspec[1] = {(char *)path};
	diff_opts.pathspec.strings = pathspec;
	diff_opts.pathspec.count = 1U;

	git_diff *diff = nullptr;
	int error = git_diff_index_to_workdir(&diff, repo, nullptr, &diff_opts);
	if (error < 0) {
		return error;
	}

	const size_t deltas = git_diff_num_deltas(diff);
	bool wrote_any = false;

	for (size_t i = 0; i < deltas; ++i) {
		git_patch *patch = nullptr;
		error = git_patch_from_diff(&patch, diff, i);
		if (error < 0) {
			break;
		}

		git_buf buf = GIT_BUF_INIT;
		error = git_patch_to_buf(&buf, patch);
		if (error == 0 && buf.ptr && buf.size > 0U) {
			const size_t written =
				fwrite(buf.ptr, 1U, buf.size, output);
			if (written != buf.size) {
				error = GIT_ERROR;
			} else {
				wrote_any = true;
				if (buf.ptr[buf.size - 1U] != '\n') {
					fputc('\n', output);
				}
			}
		}

		git_buf_dispose(&buf);
		git_patch_free(patch);

		if (error < 0) {
			break;
		}
	}

	git_diff_free(diff);

	if (error == 0) {
		if (out_has_changes) {
			*out_has_changes = wrote_any;
		}
		if (!wrote_any) {
			return GIT_ENOTFOUND;
		}
		return 0;
	}

	return error;
}
