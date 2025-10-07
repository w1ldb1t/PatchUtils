#include "libs/git/git.h"
#include "libs/ui/ui.h"
#include "libs/util/util.h"

#include <assert.h>
#include <errno.h>
#include <git2.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
	git_repository *repo;
	gitutils_status_list status_list;
	gitutils_status_entry **ordered;
	ui_list_item *items;
	FILE *patch_file;
	bool ui_active;
	bool created_patch;
	char patch_name[512];
} Context;

static int compare_entries(const void *a, const void *b)
{
	const gitutils_status_entry *ea =
		*(const gitutils_status_entry *const *)a;
	const gitutils_status_entry *eb =
		*(const gitutils_status_entry *const *)b;
	return strcmp(ea->path, eb->path);
}

static void report_git_error(const char *context, int error_code)
{
	const git_error *err = git_error_last();
	if (err && err->message) {
		fprintf(stderr, "%s: %s (code %d)\n", context, err->message,
			error_code);
	} else {
		fprintf(stderr, "%s: libgit2 error %d\n", context, error_code);
	}
}

static int finalize(Context *ctx, bool git_ready, int exit_code)
{
	if (ctx->patch_file) {
		fclose(ctx->patch_file);
		ctx->patch_file = nullptr;
	}

	if (!ctx->created_patch && ctx->patch_name[0] != '\0') {
		int ret = remove(ctx->patch_name);
		assert(ret);
	}

	free(ctx->items);
	free(ctx->ordered);
	gitutils_status_list_free(&ctx->status_list);

	if (ctx->ui_active) {
		ui_shutdown();
		ctx->ui_active = false;
	}

	git_repository_free(ctx->repo);
	ctx->repo = nullptr;

	if (git_ready) {
		git_libgit2_shutdown();
	}

	return exit_code;
}

static int run_create_patch(void)
{
	Context ctx = {
		.repo = nullptr,
		.status_list = {.entries = nullptr, .count = 0U},
		.ordered = nullptr,
		.items = nullptr,
		.patch_file = nullptr,
		.ui_active = false,
		.created_patch = false,
		.patch_name = {0},
	};

	bool git_ready = false;

	int rc = git_libgit2_init();
	if (rc < 0) {
		report_git_error("Failed to initialize libgit2", rc);
		return 1;
	}
	git_ready = true;

	rc = gitutils_open_repository(&ctx.repo, ".");
	if (rc < 0) {
		report_git_error("Not inside a git repository", rc);
		return finalize(&ctx, git_ready, 1);
	}

	rc = gitutils_collect_status(ctx.repo, &ctx.status_list);
	if (rc < 0) {
		report_git_error("Failed to gather repository status", rc);
		return finalize(&ctx, git_ready, 1);
	}

	if (ctx.status_list.count == 0U) {
		printf("No modified or untracked files found\n");
		return finalize(&ctx, git_ready, 0);
	}

	ctx.ordered = calloc(ctx.status_list.count, sizeof(*ctx.ordered));
	if (!ctx.ordered) {
		fprintf(stderr, "Out of memory\n");
		return finalize(&ctx, git_ready, 1);
	}

	for (size_t i = 0; i < ctx.status_list.count; ++i) {
		ctx.ordered[i] = &ctx.status_list.entries[i];
	}
	qsort(ctx.ordered, ctx.status_list.count, sizeof(*ctx.ordered),
	      compare_entries);

	if (ui_initialize() != 0) {
		fprintf(stderr, "Failed to initialize terminal UI\n");
		return finalize(&ctx, git_ready, 1);
	}
	ctx.ui_active = true;

	ctx.items = calloc(ctx.status_list.count, sizeof(*ctx.items));
	if (!ctx.items) {
		fprintf(stderr, "Out of memory\n");
		return finalize(&ctx, git_ready, 1);
	}

	for (size_t i = 0; i < ctx.status_list.count; ++i) {
		const gitutils_status_entry *entry = ctx.ordered[i];
		ctx.items[i] = (ui_list_item){
			.label = entry->path,
			.description =
				entry->status == GITUTILS_STATUS_UNTRACKED
					? "Untracked"
					: "Modified",
			.selected = false,
		};
	}

	const int selected_result = ui_multiselect(
		"Select Files", "Choose files to include in patch:", ctx.items,
		ctx.status_list.count);
	if (selected_result < 0) {
		fprintf(stderr, "Operation cancelled\n");
		return finalize(&ctx, git_ready, 0);
	}

	size_t selected_count = 0U;
	for (size_t i = 0; i < ctx.status_list.count; ++i) {
		selected_count += ctx.items[i].selected ? 1U : 0U;
	}

	if (selected_count == 0U) {
		ui_show_message("No Selection", "No files selected.");
		return finalize(&ctx, git_ready, 0);
	}

	if (ui_prompt_string("Patch Name",
			     "Enter the patch file name:", "changes.patch",
			     ctx.patch_name, sizeof(ctx.patch_name)) < 0) {
		fprintf(stderr, "Operation cancelled\n");
		return finalize(&ctx, git_ready, 0);
	}

	size_t name_len = strlen(ctx.patch_name);
	if (name_len == 0U) {
		ui_show_error("Invalid Name", "Patch name cannot be empty.");
		return finalize(&ctx, git_ready, 1);
	}

	static const char *suffix = ".patch";
	const size_t suffix_len = strlen(suffix);
	if (name_len < suffix_len ||
	    strcmp(ctx.patch_name + name_len - suffix_len, suffix) != 0) {
		if (name_len + suffix_len >= sizeof(ctx.patch_name)) {
			ui_show_error("Invalid Name",
				      "Patch name is too long.");
			return finalize(&ctx, git_ready, 1);
		}
		strncat(ctx.patch_name, suffix,
			sizeof(ctx.patch_name) - name_len - 1U);
		name_len += suffix_len;
	}

	if (access(ctx.patch_name, F_OK) == 0 &&
	    !ui_confirm("Overwrite?", "Patch file exists. Overwrite it?")) {
		fprintf(stderr, "Operation cancelled\n");
		return finalize(&ctx, git_ready, 0);
	}

	ctx.patch_file = fopen(ctx.patch_name, "w");
	if (!ctx.patch_file) {
		FORMAT_MSG(msg, sizeof(ctx.patch_name), "Unable to open %s: %s",
			   ctx.patch_name, strerror(errno));
		ui_show_error("File Error", msg);
		return finalize(&ctx, git_ready, 1);
	}

	size_t written_files = 0U;
	for (size_t i = 0; i < ctx.status_list.count; ++i) {
		if (!ctx.items[i].selected) {
			continue;
		}

		const char *path = ctx.ordered[i]->path;
		bool has_changes = false;
		rc = gitutils_write_diff_for_path(ctx.repo, path,
						  ctx.patch_file, &has_changes);
		if (rc < 0 && rc != GIT_ENOTFOUND) {
			FORMAT_MSG(msg, sizeof(ctx.patch_name),
				   "Failed to diff %s", path);
			ui_show_error("Diff Error", msg);
			return finalize(&ctx, git_ready, 1);
		}

		if (has_changes) {
			++written_files;
		}
	}

	if (fclose(ctx.patch_file) != 0) {
		ctx.patch_file = nullptr;
		ui_show_error("File Error",
			      "Failed to close patch file for writing.");
		return finalize(&ctx, git_ready, 1);
	}
	ctx.patch_file = nullptr;

	if (written_files == 0U) {
		ui_show_error("Empty Patch",
			      "No changes were written to the patch.");
		return finalize(&ctx, git_ready, 1);
	}

	ctx.created_patch = true;

	FORMAT_MSG(success, sizeof(ctx.patch_name),
		   "Patch created successfully: %s", ctx.patch_name);
	ui_show_message("Success", success);

	return finalize(&ctx, git_ready, 0);
}

int main(void)
{
	return run_create_patch();
}
