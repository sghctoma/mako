#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#include "event-loop.h"

#ifdef HAS_SDBUS
void init_event_loop(struct mako_event_loop *loop, sd_bus *bus,
		struct wl_display *display) {
	loop->fds[MAKO_EVENT_DBUS] = (struct pollfd){
		.fd = sd_bus_get_fd(bus),
		.events = POLLIN,
	};

	loop->fds[MAKO_EVENT_WAYLAND] = (struct pollfd){
		.fd = wl_display_get_fd(display),
		.events = POLLIN,
	};

	loop->fds[MAKO_EVENT_TIMER] = (struct pollfd){
		.fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC),
		.events = POLLIN,
	};

	loop->bus = bus;
	loop->display = display;
	wl_list_init(&loop->timers);
}
#else
bool init_event_loop(struct mako_event_loop *loop, DBusConnection *connection,
		struct wl_display *display) {

	struct pollfd pollfds[2];
	pollfds[MAKO_EVENT_WAYLAND] = (struct pollfd){
		.fd = wl_display_get_fd(display),
		.events = POLLIN,
	};
	pollfds[MAKO_EVENT_TIMER] = (struct pollfd){
		.fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC),
		.events = POLLIN,
	};

	DBusError error;
	dbus_error_init(&error);
	loop->watches = subd_init_watches(connection, pollfds, MAKO_EVENT_COUNT,
		&error);
	if (loop->watches == NULL) {
		fprintf(stderr, "failed to initialize loop: %s\n", error.message);
		return false;
	}

	loop->fds = loop->watches->fds;
	loop->bus = connection;
	loop->display = display;
	wl_list_init(&loop->timers);

	return true;
}
#endif

void finish_event_loop(struct mako_event_loop *loop) {
	close(loop->fds[MAKO_EVENT_TIMER].fd);
	loop->fds[MAKO_EVENT_TIMER].fd = -1;

	struct mako_timer *timer, *tmp;
	wl_list_for_each_safe(timer, tmp, &loop->timers, link) {
		destroy_timer(timer);
	}
}

static int poll_event_loop(struct mako_event_loop *loop) {
#ifdef HAS_SDBUS
	return poll(loop->fds, MAKO_EVENT_COUNT, -1);
#else
	return poll(loop->fds, loop->watches->length, -1);
#endif
}

static void timespec_add(struct timespec *t, int delta_ms) {
	static const long ms = 1000000, s = 1000000000;

	int delta_ms_low = delta_ms % 1000;
	int delta_s_high = delta_ms / 1000;

	t->tv_sec += delta_s_high;

	t->tv_nsec += (long)delta_ms_low * ms;
	if (t->tv_nsec >= s) {
		t->tv_nsec -= s;
		++t->tv_sec;
	}
}

static bool timespec_less(struct timespec *t1, struct timespec *t2) {
	if (t1->tv_sec != t2->tv_sec) {
		return t1->tv_sec < t2->tv_sec;
	}
	return t1->tv_nsec < t2->tv_nsec;
}

static void update_event_loop_timer(struct mako_event_loop *loop) {
	int timer_fd = loop->fds[MAKO_EVENT_TIMER].fd;
	if (timer_fd < 0) {
		return;
	}

	bool updated = false;
	struct mako_timer *timer;
	wl_list_for_each(timer, &loop->timers, link) {
		if (loop->next_timer == NULL ||
				timespec_less(&timer->at, &loop->next_timer->at)) {
			loop->next_timer = timer;
			updated = true;
		}
	}

	if (updated) {
		struct itimerspec delay = { .it_value = loop->next_timer->at };
		errno = 0;
		int ret = timerfd_settime(timer_fd, TFD_TIMER_ABSTIME, &delay, NULL);
		if (ret < 0) {
			fprintf(stderr, "failed to timerfd_settime(): %s\n",
				strerror(errno));
		}
	}
}

struct mako_timer *add_event_loop_timer(struct mako_event_loop *loop,
		int delay_ms, mako_event_loop_timer_func_t func, void *data) {
	struct mako_timer *timer = calloc(1, sizeof(struct mako_timer));
	if (timer == NULL) {
		fprintf(stderr, "allocation failed\n");
		return NULL;
	}
	timer->event_loop = loop;
	timer->func = func;
	timer->user_data = data;
	wl_list_insert(&loop->timers, &timer->link);

	clock_gettime(CLOCK_MONOTONIC, &timer->at);
	timespec_add(&timer->at, delay_ms);

	update_event_loop_timer(loop);
	return timer;
}

void destroy_timer(struct mako_timer *timer) {
	if (timer == NULL) {
		return;
	}
	struct mako_event_loop *loop = timer->event_loop;

	if (loop->next_timer == timer) {
		loop->next_timer = NULL;
	}

	wl_list_remove(&timer->link);
	free(timer);

	update_event_loop_timer(loop);
}

static void handle_event_loop_timer(struct mako_event_loop *loop) {
	int timer_fd = loop->fds[MAKO_EVENT_TIMER].fd;
	uint64_t expirations;
	ssize_t n = read(timer_fd, &expirations, sizeof(expirations));
	if (n < 0) {
		fprintf(stderr, "failed to read from timer FD\n");
		return;
	}

	struct mako_timer *timer = loop->next_timer;
	if (timer == NULL) {
		return;
	}

	mako_event_loop_timer_func_t func = timer->func;
	void *user_data = timer->user_data;
	destroy_timer(timer);

	func(user_data);
}

int run_event_loop(struct mako_event_loop *loop) {
	loop->running = true;

	int ret = 0;
	while (loop->running) {
		while (wl_display_prepare_read(loop->display) != 0) {
			wl_display_dispatch_pending(loop->display);
		}
		wl_display_flush(loop->display);

		ret = poll_event_loop(loop);
		if (!loop->running) {
			wl_display_cancel_read(loop->display);
			ret = 0;
			break;
		}
		if (ret < 0) {
			wl_display_cancel_read(loop->display);
			fprintf(stderr, "failed to poll(): %s\n", strerror(-ret));
			break;
		}

		if (!(loop->fds[MAKO_EVENT_WAYLAND].revents & POLLIN)) {
			wl_display_cancel_read(loop->display);
		}

#ifdef HAS_SDBUS
		if (loop->fds[MAKO_EVENT_DBUS].revents & POLLIN) {
			while (1) {
				ret = sd_bus_process(loop->bus, NULL);
				if (ret < 0) {
					fprintf(stderr, "failed to process bus: %s\n",
						strerror(-ret));
					break;
				}
				if (ret == 0) {
					break;
				}
				// We processed a request, try to process another one, right-away
			}

			if (ret < 0) {
				break;
			}
		}
#else
		subd_process_watches(loop->bus, loop->watches);
#endif

		if (loop->fds[MAKO_EVENT_WAYLAND].revents & POLLIN) {
			ret = wl_display_read_events(loop->display);
			if (ret < 0) {
				fprintf(stderr, "failed to read Wayland events: %s\n",
					strerror(errno));
				break;
			}
			wl_display_dispatch_pending(loop->display);
		}

		if (loop->fds[MAKO_EVENT_TIMER].revents & POLLIN) {
			handle_event_loop_timer(loop);
		}
	}
	return ret;
}

static void handle_stop_event_loop_timer(void *data) {
	// No-op
}

void stop_event_loop(struct mako_event_loop *loop) {
	loop->running = false;

	// Wake up the event loop
	add_event_loop_timer(loop, 0, handle_stop_event_loop_timer, NULL);
}
