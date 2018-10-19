#include <errno.h>
#include <stdio.h>

#include "dbus.h"
#include "mako.h"

static const char *service_name = "org.freedesktop.Notifications";

bool init_dbus(struct mako_state *state) {
	DBusError error;
	dbus_error_init(&error);
	state->bus = subd_open_session(service_name, &error);
	if (state->bus == NULL) {
		fprintf(stderr, "Failed to open DBus session: %s", error.message);
	}

	init_dbus_xdg(state, &error);
	if (dbus_error_is_set(&error)) {
		fprintf(stderr, "Failed to initialize XDG interface: %s\n", error.message);
		goto error;
	}

	init_dbus_mako(state, &error);
	if (dbus_error_is_set(&error)) {
		fprintf(stderr, "Failed to initialize Mako interface: %s\n", error.message);
		goto error;
	}

	return true;

error:
	finish_dbus(state);
	return false;
}

void finish_dbus(struct mako_state *state) {
	dbus_connection_flush(state->bus);
	dbus_connection_unref(state->bus);
}
