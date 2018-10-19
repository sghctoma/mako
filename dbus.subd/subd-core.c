#include "subd.h"

DBusConnection *subd_open_session(const char* service_name, DBusError *error) {
	if (!dbus_threads_init_default()) {
		dbus_set_error(error, DBUS_ERROR_NO_MEMORY, NULL);
		return NULL;
	}

	DBusConnection *connection = dbus_bus_get(DBUS_BUS_SESSION, error);
	if (connection == NULL) {
		return NULL;
	}

	if (!dbus_bus_request_name(connection, service_name, 0, error)) {
		dbus_connection_unref(connection);
		return NULL;
	}

	return connection;
}

bool subd_emit_signal(DBusConnection *connection, const char *path,
		const char *interface, const char *name, DBusError *error, ...) {
	DBusMessage *signal = NULL;
	if ((signal = dbus_message_new_signal(path, interface, name)) == NULL) {
		goto error;
	}

	va_list ap;
	va_start(ap, error);
	if (!dbus_message_append_args_valist(signal, va_arg(ap, int), ap)) {
		goto error;
	}
	va_end(ap);

	if (!dbus_connection_send(connection, signal, NULL)) {
		goto error;
	}

	dbus_message_unref(signal);
	return true;

error:
	dbus_set_error(error, DBUS_ERROR_NO_MEMORY, NULL);
	dbus_message_unref(signal);
	return false;
}

bool subd_reply_method_return(DBusConnection *conn, DBusMessage *msg,
		DBusError *error, ...) {
	DBusMessage *reply = NULL;
	if ((reply = dbus_message_new_method_return(msg)) == NULL) {
		goto error;
	}

	va_list ap;
	va_start(ap, error);
	if (!dbus_message_append_args_valist(reply, va_arg(ap, int), ap)) {
		goto error;
	}
	va_end(ap);

	if (!dbus_connection_send(conn, reply, NULL)) {
		goto error;
	}

	dbus_message_unref(reply);
	return true;

error:
	dbus_set_error(error, DBUS_ERROR_NO_MEMORY, NULL);
	dbus_message_unref(reply);
	return false;
}

bool subd_message_read(DBusMessageIter *iter, DBusError *error, ...) {
	va_list ap;
	va_start(ap, error);
	DBusBasicValue *ptr = NULL;
	while ((ptr = va_arg(ap, DBusBasicValue *)) != NULL) {
		dbus_message_iter_get_basic(iter, ptr);
		if (!dbus_message_iter_next(iter)) {
			dbus_set_error(error, DBUS_ERROR_INVALID_ARGS,
				"Message has less fields than requested");
			return false;
		}
	}
	va_end(ap);

	return true;
}
