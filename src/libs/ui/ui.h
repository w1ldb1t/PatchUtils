#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct {
	const char *label;
	const char *description;
	bool selected;
} ui_list_item;

int ui_initialize(void);
void ui_shutdown(void);
int ui_multiselect(const char *title, const char *prompt, ui_list_item *items,
		   size_t count);
int ui_menu_select(const char *title, const char *prompt,
		   const char *const *options, size_t count);
bool ui_confirm(const char *title, const char *question);

int ui_prompt_string(const char *title, const char *prompt,
		     const char *initial_value, char *buffer,
		     size_t buffer_size);
void ui_show_message(const char *title, const char *message);
void ui_show_error(const char *title, const char *message);
