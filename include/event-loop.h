#ifndef _EVENT_LOOP_H
#define _EVENT_LOOP_H

#include <poll.h>
#ifdef HAS_SDBUS
#include <stdbool.h>
#include <systemd/sd-bus.h>
#else
#include <subd.h>
#endif
#include <time.h>
#include <wayland-client.h>

enum mako_event {
#ifdef HAS_SDBUS
	MAKO_EVENT_DBUS,
#endif
	MAKO_EVENT_WAYLAND,
	MAKO_EVENT_TIMER,
	MAKO_EVENT_COUNT, // keep last
};

struct mako_event_loop {
#ifdef HAS_SDBUS
	struct pollfd fds[MAKO_EVENT_COUNT];
	sd_bus *bus;
#else
	struct DBusConnection *bus;
	struct subd_watches *watches;
	// store a pointer to watches->fds to mimimize code diff in event-loop.c
	struct pollfd *fds;
#endif
	struct wl_display *display;

	bool running;
	struct wl_list timers; // mako_timer::link
	struct mako_timer *next_timer;
};

typedef void (*mako_event_loop_timer_func_t)(void *data);

struct mako_timer {
	struct mako_event_loop *event_loop;
	mako_event_loop_timer_func_t func;
	void *user_data;
	struct timespec at;
	struct wl_list link; // mako_event_loop::timers
};

#ifdef HAS_SDBUS
void init_event_loop(struct mako_event_loop *loop, sd_bus *bus,
	struct wl_display *display);
#else
bool init_event_loop(struct mako_event_loop *loop,
	struct DBusConnection *connection, struct wl_display *display);
#endif
void finish_event_loop(struct mako_event_loop *loop);
int run_event_loop(struct mako_event_loop *loop);
void stop_event_loop(struct mako_event_loop *loop);
struct mako_timer *add_event_loop_timer(struct mako_event_loop *loop,
	int delay_ms, mako_event_loop_timer_func_t func, void *data);

void destroy_timer(struct mako_timer *timer);

#endif
