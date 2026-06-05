#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

typedef enum {
    DISPLAY_NAV_KEY_LEFT = 0,
    DISPLAY_NAV_KEY_RIGHT = 1,
    DISPLAY_NAV_KEY_UP = 2,
    DISPLAY_NAV_KEY_DOWN = 3,
} display_nav_key_t;

struct syscall_gui_event;
struct syscall_gui_flush_req;
struct syscall_gui_create_req;
struct syscall_gui_info;
struct syscall_gui_window_info;

/* Initialize the active kernel display surface.  The current backend is
 * VGA text mode; future framebuffer support should plug in here without
 * forcing callers to care about the backend choice.
 */
void display_init(void);

/* Finish display initialization after the memory map, paging, and heap
 * are ready.  This is where non-VGA backends can claim mapped resources.
 */
void display_late_init(void);

/* Enter/leave the active console surface critical section. */
uint32_t display_console_enter_critical(void);
void display_console_leave_critical(uint32_t saved_flags);

/* Write to the active display surface. */
void display_putc(char c);
void display_puts(const char *str);

/* Returns non-zero if a framebuffer window is present and mapped, even
 * if the active console output still targets VGA.
 */
int display_framebuffer_ready(void);

/* Route a GUI navigation key to the framebuffer shell chrome. Returns
 * non-zero when the key was consumed by the GUI layer.
 */
int display_handle_navigation_key(display_nav_key_t key);

/* Enable or query GUI compositor mode on the framebuffer backend. */
void display_gui_enable(void);
int display_gui_mode_active(void);

/* GUI window manager hooks used by syscalls, input drivers, and task lifecycle. */
int display_gui_create_window(const struct syscall_gui_create_req *req, int owner_pid, int *out_window_id);
int display_gui_flush_window(const struct syscall_gui_flush_req *req, int owner_pid);
int display_gui_poll_event(int window_id, int owner_pid, struct syscall_gui_event *out_event);
int display_gui_poll_events(int window_id,
                            int owner_pid,
                            struct syscall_gui_event *out_events,
                            uint32_t event_cap,
                            uint32_t *out_count);
int display_gui_destroy_window(int window_id, int owner_pid);
int display_gui_collect_info(struct syscall_gui_info *out_info);
int display_gui_collect_window_info(int window_id, int owner_pid, struct syscall_gui_window_info *out_info);
void display_gui_notify_task_exit(int owner_pid);
int display_gui_handle_key_event(uint32_t keycode, uint32_t ch, uint32_t modifiers, int pressed);
void display_gui_handle_mouse_motion(int32_t dx, int32_t dy);
void display_gui_handle_mouse_buttons(uint32_t buttons);

#endif /* DISPLAY_H */
