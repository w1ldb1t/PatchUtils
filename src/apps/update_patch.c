#include "libs/git/git.h"
#include "libs/patch/patch.h"
#include "libs/ui/ui.h"
#include "libs/util/util.h"

#include <errno.h>
#include <fcntl.h>
#include <git2.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct {
	patch_section *section;
	char *path;
	bool owns_path;
	bool mark_for_update;
	bool is_original;
} patch_entry;

typedef struct {
	patch_entry *items;
	size_t count;
	size_t capacity;
} patch_entry_list;

typedef struct {
	patch_section *sections;
	size_t count;
	size_t capacity;
} patch_sections;

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

static void patch_sections_free(patch_sections *out)
{
	if (!out) {
		return;
	}
	for (size_t i = 0; i < out->count; ++i) {
		patch_section_reset(&out->sections[i]);
	}
	free(out->sections);
}

static bool collect_sections_callback(const patch_section *section,
				      void *userdata)
{
	patch_sections *dest = (patch_sections *)userdata;
	if (!utils_array_reserve((void **)&dest->sections, &dest->capacity,
				 dest->count + 1U, sizeof(*dest->sections))) {
		return false;
	}

	patch_section *slot = &dest->sections[dest->count++];
	if (!patch_section_init(slot, section->path)) {
		--dest->count;
		return false;
	}

	if (!patch_section_append(slot, section->data, section->length)) {
		patch_section_reset(slot);
		--dest->count;
		return false;
	}

	return true;
}

static int parse_patch_file(const char *path, patch_sections *out)
{
	FILE *input = fopen(path, "r");
	if (!input) {
		fprintf(stderr, "Error: unable to open %s: %s\n", path,
			strerror(errno));
		return -1;
	}

	bool ok = patch_parse(input, collect_sections_callback, out);
	fclose(input);

	if (!ok || out->count == 0U) {
		patch_sections_free(out);
		if (ok) {
			fprintf(stderr, "Error: no diff sections found in %s\n",
				path);
		}
		return -1;
	}

	return 0;
}

static void patch_entry_list_free(patch_entry_list *list)
{
	if (!list) {
		return;
	}
	for (size_t i = 0; i < list->count; ++i) {
		if (list->items[i].owns_path) {
			free(list->items[i].path);
		}
	}
	free(list->items);
	list->items = nullptr;
	list->count = 0;
	list->capacity = 0;
}

static bool patch_entry_list_reserve(patch_entry_list *list, size_t additional)
{
	const size_t required = list->count + additional;
	return utils_array_reserve((void **)&list->items, &list->capacity,
				   required, sizeof(*list->items));
}

static bool patch_entry_list_append(patch_entry_list *list, patch_entry entry)
{
	if (!patch_entry_list_reserve(list, 1U)) {
		return false;
	}

	list->items[list->count++] = entry;
	return true;
}

static patch_entry *patch_entry_list_find(patch_entry_list *list,
					  const char *path)
{
	if (!list || !path) {
		return nullptr;
	}
	for (size_t i = 0; i < list->count; ++i) {
		if (strcmp(list->items[i].path, path) == 0) {
			return &list->items[i];
		}
	}
	return nullptr;
}

static void patch_entry_list_remove_at(patch_entry_list *list, size_t index)
{
	if (!list || index >= list->count) {
		return;
	}

	if (list->items[index].owns_path) {
		free(list->items[index].path);
	}

	for (size_t i = index + 1; i < list->count; ++i) {
		list->items[i - 1] = list->items[i];
	}
	--list->count;
}

static int collect_patch_entries(patch_section *sections, size_t section_count,
				 patch_entry_list *entries)
{
	if (!patch_entry_list_reserve(entries, section_count)) {
		return -1;
	}

	for (size_t i = 0; i < section_count; ++i) {
		patch_entry entry = {
			.section = &sections[i],
			.path = sections[i].path,
			.owns_path = false,
			.mark_for_update = false,
			.is_original = true,
		};
		entries->items[entries->count++] = entry;
	}

	return 0;
}

static int handle_add_files(git_repository *repo, patch_entry_list *entries)
{
	gitutils_status_list status_list = {0};
	int rc = gitutils_collect_status(repo, &status_list);
	if (rc < 0) {
		report_git_error("Failed to gather repository status", rc);
		return rc;
	}

	size_t available = 0;
	ui_list_item *items = calloc(status_list.count, sizeof(*items));
	if (!items && status_list.count > 0) {
		gitutils_status_list_free(&status_list);
		return GIT_ERROR;
	}

	for (size_t i = 0; i < status_list.count; ++i) {
		gitutils_status_entry *entry = &status_list.entries[i];
		if (patch_entry_list_find(entries, entry->path)) {
			continue;
		}
		items[available].label = entry->path;
		items[available].description =
			entry->status == GITUTILS_STATUS_UNTRACKED ? "Untracked"
								   : "Modified";
		items[available].selected = false;
		++available;
	}

	if (available == 0) {
		free(items);
		gitutils_status_list_free(&status_list);
		ui_show_message("Add Files",
				"No modified or untracked files available.");
		return 0;
	}

	int selection = ui_multiselect(
		"Add Files", "Select files to add to the patch:", items,
		available);
	if (selection < 0) {
		free(items);
		gitutils_status_list_free(&status_list);
		return 0;
	}

	bool added_any = false;
	for (size_t j = 0; j < available; ++j) {
		if (!items[j].selected) {
			continue;
		}
		if (patch_entry_list_find(entries, items[j].label)) {
			continue;
		}
		patch_entry new_entry = {
			.section = nullptr,
			.path = strdup(items[j].label),
			.owns_path = true,
			.mark_for_update = false,
			.is_original = false,
		};
		if (!new_entry.path ||
		    !patch_entry_list_append(entries, new_entry)) {
			free(new_entry.path);
			free(items);
			gitutils_status_list_free(&status_list);
			return GIT_ERROR;
		}
		added_any = true;
	}

	if (added_any) {
		ui_show_message("Add Files", "Files added to patch list.");
	}

	free(items);
	gitutils_status_list_free(&status_list);
	return 0;
}

static void handle_remove_files(patch_entry_list *entries)
{
	if (entries->count == 0) {
		ui_show_message("Remove Files",
				"No files available to remove.");
		return;
	}

	ui_list_item *items = calloc(entries->count, sizeof(*items));
	if (!items) {
		ui_show_message("Remove Files", "Unable to allocate memory.");
		return;
	}

	for (size_t i = 0; i < entries->count; ++i) {
		items[i].label = entries->items[i].path;
		items[i].description =
			entries->items[i].is_original ? "In patch" : "New";
		items[i].selected = false;
	}

	int selection = ui_multiselect(
		"Remove Files", "Select files to remove from the patch:", items,
		entries->count);
	if (selection >= 0) {
		bool removed_any = false;
		for (ssize_t idx = (ssize_t)entries->count - 1; idx >= 0;
		     --idx) {
			if (items[idx].selected) {
				patch_entry_list_remove_at(entries,
							   (size_t)idx);
				removed_any = true;
			}
		}
		if (removed_any) {
			ui_show_message("Remove Files",
					"Selected files removed.");
		}
	}

	free(items);
}

static void handle_update_flags(patch_entry_list *entries)
{
	if (entries->count == 0) {
		ui_show_message("Update Files",
				"No files available to update.");
		return;
	}

	ui_list_item *items = calloc(entries->count, sizeof(*items));
	if (!items) {
		ui_show_message("Update Files", "Unable to allocate memory.");
		return;
	}

	for (size_t i = 0; i < entries->count; ++i) {
		items[i].label = entries->items[i].path;
		items[i].description =
			entries->items[i].is_original ? "From patch" : "New";
		items[i].selected = entries->items[i].mark_for_update;
	}

	int selection = ui_multiselect(
		"Update Files",
		"Select files to refresh from current changes:", items,
		entries->count);
	if (selection >= 0) {
		for (size_t i = 0; i < entries->count; ++i) {
			entries->items[i].mark_for_update = items[i].selected;
		}
		ui_show_message("Update Files", "Update selection recorded.");
	}

	free(items);
}

static int write_final_patch(const char *patch_path, git_repository *repo,
			     patch_entry_list *entries)
{
	if (entries->count == 0) {
		ui_show_message("Finalize Patch",
				"No files selected. Patch not updated.");
		return -1;
	}

	char directory[PATH_MAX];
	char temp_template[PATH_MAX];

	const char *slash = strrchr(patch_path, '/');
	if (slash) {
		size_t dir_len = (size_t)(slash - patch_path);
		if (dir_len >= sizeof(directory)) {
			ui_show_message("Finalize Patch",
					"Patch path is too long.");
			return -1;
		}
		memcpy(directory, patch_path, dir_len);
		directory[dir_len] = '\0';
	} else {
		strcpy(directory, ".");
	}

	FORMAT_MSG_INTO(temp_template, "%s/.patchutilsXXXXXX", directory);

	int temp_fd = mkstemp(temp_template);
	if (temp_fd < 0) {
		FORMAT_MSG(msg, 512, "Unable to create temporary file: %s",
			   strerror(errno));
		ui_show_message("Finalize Patch", msg);
		return -1;
	}

	FILE *temp_file = fdopen(temp_fd, "w");
	if (!temp_file) {
		FORMAT_MSG(msg, 512, "Unable to open temporary file: %s",
			   strerror(errno));
		close(temp_fd);
		unlink(temp_template);
		ui_show_message("Finalize Patch", msg);
		return -1;
	}

	size_t written_sections = 0;
	size_t skipped_updates = 0;

	for (size_t i = 0; i < entries->count; ++i) {
		patch_entry *entry = &entries->items[i];
		bool wrote = false;

		if (!entry->is_original || entry->mark_for_update) {
			bool has_changes = false;
			int rc = gitutils_write_diff_for_path(
				repo, entry->path, temp_file, &has_changes);
			if (rc < 0 && rc != GIT_ENOTFOUND) {
				FORMAT_MSG(msg, 512, "Failed to diff %s",
					   entry->path);
				fclose(temp_file);
				unlink(temp_template);
				ui_show_message("Finalize Patch", msg);
				return -1;
			}

			if (has_changes) {
				wrote = true;
			} else if (!entry->is_original) {
				FORMAT_MSG(msg, 512,
					   "No current changes for new file %s",
					   entry->path);
				fclose(temp_file);
				unlink(temp_template);
				ui_show_message("Finalize Patch", msg);
				return -1;
			} else if (entry->mark_for_update) {
				++skipped_updates;
			}
		} else if (entry->section) {
			if (entry->section->length > 0) {
				size_t written = fwrite(entry->section->data, 1,
							entry->section->length,
							temp_file);
				if (written != entry->section->length) {
					fclose(temp_file);
					unlink(temp_template);
					ui_show_message("Finalize Patch",
							"Failed to write to "
							"temporary file.");
					return -1;
				}
				wrote = true;
			}
		}

		if (wrote) {
			++written_sections;
		}
	}

	fflush(temp_file);
	if (fclose(temp_file) != 0) {
		FORMAT_MSG(msg, 512, "Failed to close temporary file: %s",
			   strerror(errno));
		unlink(temp_template);
		ui_show_message("Finalize Patch", msg);
		return -1;
	}

	if (written_sections == 0) {
		unlink(temp_template);
		ui_show_message("Finalize Patch",
				"No content generated for patch.");
		return -1;
	}

	if (rename(temp_template, patch_path) < 0) {
		FORMAT_MSG(msg, 512, "Failed to replace patch file: %s",
			   strerror(errno));
		unlink(temp_template);
		ui_show_message("Finalize Patch", msg);
		return -1;
	}

	if (skipped_updates > 0) {
		FORMAT_MSG(
			msg, 512,
			"%zu file(s) had no changes and were left untouched.",
			skipped_updates);
		ui_show_message("Finalize Patch", msg);
	} else {
		ui_show_message("Finalize Patch",
				"Patch file updated successfully.");
	}

	return 0;
}

static int run_update_patch(const char *patch_path)
{
	patch_sections sections = {0};
	if (parse_patch_file(patch_path, &sections) < 0) {
		return 1;
	}

	patch_entry_list entries = {0};
	if (collect_patch_entries(sections.sections, sections.count, &entries) <
	    0) {
		patch_sections_free(&sections);
		fprintf(stderr, "Error: unable to load patch entries.\n");
		return 1;
	}

	int rc = git_libgit2_init();
	if (rc < 0) {
		report_git_error("Failed to initialize libgit2", rc);
		patch_entry_list_free(&entries);
		patch_sections_free(&sections);
		return 1;
	}

	git_repository *repo = nullptr;
	rc = gitutils_open_repository(&repo, ".");
	if (rc < 0) {
		report_git_error("Not inside a git repository", rc);
		patch_entry_list_free(&entries);
		patch_sections_free(&sections);
		git_libgit2_shutdown();
		return 1;
	}

	if (ui_initialize() != 0) {
		fprintf(stderr, "Failed to initialize terminal UI\n");
		patch_entry_list_free(&entries);
		patch_sections_free(&sections);
		git_repository_free(repo);
		git_libgit2_shutdown();
		return 1;
	}

	const char *menu_options[] = {
		"Add Files to Patch", "Remove Files from Patch",
		"Update Existing Files", "Finalize Patch"};

	bool done = false;
	while (!done) {
		int choice =
			ui_menu_select("Patch Update Menu",
				       "Choose an option:", menu_options, 4);
		if (choice < 0) {
			ui_shutdown();
			patch_entry_list_free(&entries);
			patch_sections_free(&sections);
			git_repository_free(repo);
			git_libgit2_shutdown();
			fprintf(stderr, "Operation cancelled\n");
			return 0;
		}

		switch (choice) {
		case 0:
			handle_add_files(repo, &entries);
			break;
		case 1:
			handle_remove_files(&entries);
			break;
		case 2:
			handle_update_flags(&entries);
			break;
		case 3:
			done = true;
			break;
		default:
			break;
		}
	}

	int ret = write_final_patch(patch_path, repo, &entries);

	ui_shutdown();
	patch_entry_list_free(&entries);
	patch_sections_free(&sections);
	git_repository_free(repo);
	git_libgit2_shutdown();

	return ret == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <patch-file>\n", argv[0]);
		return 1;
	}

	const char *patch_path = argv[1];
	struct stat st;
	if (stat(patch_path, &st) != 0 || !S_ISREG(st.st_mode)) {
		fprintf(stderr, "Error: %s is not a readable patch file.\n",
			patch_path);
		return 1;
	}

	return run_update_patch(patch_path);
}
