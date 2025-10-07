#include "ui.h"

#include <assert.h>
#include <ctype.h>
#include <ncurses.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static bool ui_ready = false;

static size_t bounded_strlen(const char *str, size_t max_len)
{
	assert(str);

	size_t len = 0U;
	while (len < max_len && str[len] != '\0') {
		++len;
	}
	return len;
}

static int compute_list_height(void)
{
	int rows, cols;
	getmaxyx(stdscr, rows, cols);
	const int raw_height = rows - 7;
	return raw_height < 1 ? 1 : raw_height;
}

static void draw_centered(WINDOW *win, int y, const char *text, int attrs)
{
	assert(win);
	assert(text);

	int width = getmaxx(win);
	int len = (int)strlen(text);
	int x = (width - len) / 2;
	if (x < 0) {
		x = 0;
	}

	if (attrs) {
		wattron(win, attrs);
	}
	mvwprintw(win, y, x, "%s", text);
	if (attrs) {
		wattroff(win, attrs);
	}
}

int ui_initialize(void)
{
	if (ui_ready) {
		return 0;
	}

	if (initscr() == nullptr) {
		return -1;
	}

	if (cbreak() == ERR || noecho() == ERR) {
		endwin();
		return -1;
	}

	keypad(stdscr, TRUE);
	curs_set(0);
#if defined(NCURSES_VERSION)
	set_escdelay(25);
#endif
	ui_ready = true;
	return 0;
}

void ui_shutdown(void)
{
	if (!ui_ready) {
		return;
	}

	curs_set(1);
	endwin();
	ui_ready = false;
}

static void render_multiselect(const char *title, const char *prompt,
			       ui_list_item *items, size_t count,
			       size_t current_index, size_t top_index)
{
	erase();

	int rows, cols;
	getmaxyx(stdscr, rows, cols);

	draw_centered(stdscr, 0, title, A_BOLD);

	if (prompt) {
		mvprintw(2, 2, "%s", prompt);
	}

	int list_start = 4;
	int list_height = rows - list_start - 3;
	if (list_height < 1) {
		list_height = 1;
	}

	for (int i = 0; i < list_height && (top_index + (size_t)i) < count;
	     ++i) {
		size_t idx = top_index + (size_t)i;
		ui_list_item *item = &items[idx];
		const char *marker = item->selected ? "[x]" : "[ ]";
		if (top_index + (size_t)i == current_index) {
			attron(A_REVERSE);
		}
		if (item->description && *item->description) {
			mvprintw(list_start + i, 4, "%s %s <=> %s", marker,
				 item->label ? item->label : "",
				 item->description);
		} else {
			mvprintw(list_start + i, 4, "%s %s", marker,
				 item->label ? item->label : "");
		}
		if (top_index + (size_t)i == current_index) {
			attroff(A_REVERSE);
		}
	}

	mvprintw(rows - 2, 2,
		 "Use Up/Down to navigate, Space to toggle, Enter to confirm, "
		 "Esc "
		 "to cancel");
	refresh();
}

int ui_multiselect(const char *title, const char *prompt, ui_list_item *items,
		   size_t count)
{
	if (!ui_ready) {
		return -1;
	}

	if (count == 0) {
		erase();
		draw_centered(stdscr, 0, title ? title : "", A_BOLD);
		mvprintw(2, 2, "%s", prompt ? prompt : "No entries available.");
		mvprintw(4, 2, "Press any key to continue.");
		refresh();
		getch();
		return 0;
	}

	size_t current_index = 0U;
	size_t top_index = 0U;

	render_multiselect(title, prompt, items, count, current_index,
			   top_index);

	while (1) {
		int ch = getch();
		switch (ch) {
		case KEY_UP:
		case 'k':
			if (current_index > 0) {
				--current_index;
				if (current_index < top_index) {
					top_index = current_index;
				}
			}
			break;
		case KEY_DOWN:
		case 'j':
			if (current_index + 1 < count) {
				++current_index;
				const size_t list_height =
					(size_t)compute_list_height();
				if (current_index >= top_index + list_height) {
					top_index = current_index -
						    list_height + 1U;
				}
			}
			break;
		case ' ':
			items[current_index].selected =
				!items[current_index].selected;
			break;
		case 'a':
		case 'A': {
			bool all_selected = true;
			for (size_t i = 0; i < count; ++i) {
				if (!items[i].selected) {
					all_selected = false;
					break;
				}
			}
			for (size_t i = 0; i < count; ++i) {
				items[i].selected = !all_selected;
			}
			break;
		}
		case '\n':
		case KEY_ENTER:
			goto done;
		case 27: // Escape
		case 'q':
		case 'Q':
			return -1;
		case KEY_RESIZE:
			// No state change needed; fallthrough to redraw
			break;
		default:
			break;
		}

		const size_t list_height = (size_t)compute_list_height();
		if (current_index < top_index) {
			top_index = current_index;
		} else if (current_index >= top_index + list_height) {
			top_index = current_index - list_height + 1U;
		}

		render_multiselect(title, prompt, items, count, current_index,
				   top_index);
	}

done: {
	int selected = 0;
	for (size_t i = 0; i < count; ++i) {
		if (items[i].selected) {
			++selected;
		}
	}
	return selected;
}
}

static void render_menu(const char *title, const char *prompt,
			const char *const *options, size_t count,
			size_t current_index)
{
	erase();

	draw_centered(stdscr, 0, title, A_BOLD);
	if (prompt) {
		mvprintw(2, 2, "%s", prompt);
	}

	for (size_t i = 0; i < count; ++i) {
		if (i == current_index) {
			attron(A_REVERSE);
		}
		mvprintw(4 + (int)i, 4, "%zu) %s", i + 1, options[i]);
		if (i == current_index) {
			attroff(A_REVERSE);
		}
	}

	mvprintw(5 + (int)count, 2,
		 "Use ↑/↓ to navigate, Enter to select, Esc to cancel");
	refresh();
}

int ui_menu_select(const char *title, const char *prompt,
		   const char *const *options, size_t count)
{
	if (!ui_ready) {
		return -1;
	}

	if (!options || count == 0U) {
		return -1;
	}

	size_t current_index = 0U;
	render_menu(title, prompt, options, count, current_index);

	while (1) {
		int ch = getch();
		switch (ch) {
		case KEY_UP:
		case 'k':
			if (current_index > 0) {
				--current_index;
			}
			break;
		case KEY_DOWN:
		case 'j':
			if (current_index + 1 < count) {
				++current_index;
			}
			break;
		case '\n':
		case KEY_ENTER:
			return (int)current_index;
		case 27:
		case 'q':
		case 'Q':
			return -1;
		default:
			break;
		}
		render_menu(title, prompt, options, count, current_index);
	}
}

static void draw_input_window(WINDOW *win, const char *title,
			      const char *prompt, const char *buffer)
{
	werase(win);
	box(win, 0, 0);
	if (title) {
		wattron(win, A_BOLD);
		mvwprintw(win, 0, 2, " %s ", title);
		wattroff(win, A_BOLD);
	}
	if (prompt) {
		mvwprintw(win, 1, 2, "%s", prompt);
	}
	mvwprintw(win, 3, 2, "%s", buffer);
	mvwprintw(win, getmaxy(win) - 2, 2, "Enter to confirm, Esc to cancel");
	wrefresh(win);
}

int ui_prompt_string(const char *title, const char *prompt,
		     const char *initial_value, char *buffer,
		     size_t buffer_size)
{
	if (!ui_ready || !buffer || buffer_size == 0) {
		return -1;
	}

	size_t len = 0U;
	if (initial_value) {
		len = bounded_strlen(initial_value, buffer_size - 1U);
		memcpy(buffer, initial_value, len);
	}
	buffer[len] = '\0';
	size_t cursor = len;

	int rows, cols;
	getmaxyx(stdscr, rows, cols);
	int width = cols > 50 ? 50 : cols - 2;
	if (width < 20) {
		width = cols > 10 ? cols - 2 : 20;
	}
	const int height = 7;
	int start_y = (rows - height) / 2;
	int start_x = (cols - width) / 2;
	if (start_y < 0)
		start_y = 0;
	if (start_x < 0)
		start_x = 0;

	WINDOW *win = newwin(height, width, start_y, start_x);
	keypad(win, TRUE);
	curs_set(1);

	while (1) {
		draw_input_window(win, title, prompt, buffer);
		wmove(win, 3, 2 + (int)cursor);
		int ch = wgetch(win);
		if (ch == 27) {
			delwin(win);
			curs_set(0);
			return -1;
		}
		if (ch == '\n' || ch == KEY_ENTER) {
			delwin(win);
			curs_set(0);
			return 0;
		}
		if (ch == KEY_LEFT) {
			if (cursor > 0U) {
				--cursor;
			}
			continue;
		}
		if (ch == KEY_RIGHT) {
			if (cursor < len) {
				++cursor;
			}
			continue;
		}
		if (ch == KEY_DC) {
			if (cursor < len) {
				memmove(buffer + cursor, buffer + cursor + 1,
					len - cursor);
				--len;
				buffer[len] = '\0';
			}
			continue;
		}
		if (ch == KEY_BACKSPACE || ch == 127 || ch == '\b') {
			if (cursor > 0U) {
				memmove(buffer + cursor - 1, buffer + cursor,
					len - cursor + 1);
				--cursor;
				--len;
			}
			continue;
		}
		if (isprint(ch)) {
			if (len + 1U < buffer_size) {
				memmove(buffer + cursor + 1, buffer + cursor,
					len - cursor + 1);
				buffer[cursor] = (char)ch;
				++cursor;
				++len;
			} else {
				beep();
			}
		}
	}
}

static void show_message_box(const char *title, const char *message)
{
	int rows, cols;
	getmaxyx(stdscr, rows, cols);

	int width = cols > 60 ? 60 : cols - 2;
	if (width < 20) {
		width = cols > 10 ? cols - 2 : 20;
	}

	int height = 7;
	int start_y = (rows - height) / 2;
	int start_x = (cols - width) / 2;
	if (start_y < 0) {
		start_y = 0;
	}
	if (start_x < 0) {
		start_x = 0;
	}

	WINDOW *win = newwin(height, width, start_y, start_x);
	box(win, 0, 0);
	if (title) {
		wattron(win, A_BOLD);
		mvwprintw(win, 0, 2, " %s ", title);
		wattroff(win, A_BOLD);
	}
	if (message) {
		mvwprintw(win, 2, 2, "%s", message);
	}
	mvwprintw(win, height - 2, 2, "Press Enter to continue");
	wrefresh(win);

	int ch;
	while ((ch = wgetch(win)) != '\n' && ch != KEY_ENTER) {
		if (ch == 27) {
			break;
		}
	}

	delwin(win);
}

void ui_show_message(const char *title, const char *message)
{
	if (!ui_ready) {
		return;
	}

	curs_set(0);
	show_message_box(title, message);
}

void ui_show_error(const char *title, const char *message)
{
	if (!ui_ready) {
		return;
	}

	curs_set(0);
	beep();
	show_message_box(title, message);
}

bool ui_confirm(const char *title, const char *question)
{
	if (!ui_ready) {
		return false;
	}

	const char *options[] = {"Yes", "No"};
	int choice = ui_menu_select(title, question, options, 2);
	return choice == 0;
}
