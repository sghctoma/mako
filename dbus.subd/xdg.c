#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "criteria.h"
#include "dbus.h"
#include "mako.h"
#include "notification.h"
#include "wayland.h"

static const char *service_path = "/org/freedesktop/Notifications";
static const char *service_interface = "org.freedesktop.Notifications";

static bool handle_get_capabilities(DBusConnection *conn,
		DBusMessage *msg, void *data, DBusError *err) {
	struct mako_state *state = data;
	const char *error_name = NULL;

	DBusMessage *reply = dbus_message_new_method_return(msg);
	if (reply == NULL) {
		error_name = DBUS_ERROR_NO_MEMORY;
		goto error;
	}

	DBusMessageIter iter, sub;
	dbus_message_iter_init_append(reply, &iter);
	if (!dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "s", &sub)) {
		error_name = DBUS_ERROR_NO_MEMORY;
		goto error;
	}

	if (strstr(global_criteria(&state->config)->style.format, "%b") != NULL) {
		const char *body = "body";
		if (!dbus_message_iter_append_basic(&sub, DBUS_TYPE_STRING, &body)) {
			error_name = DBUS_ERROR_NO_MEMORY;
			goto error;
		}
	}

	if (global_criteria(&state->config)->style.markup) {
		const char *body_markup = "body-markup";
		if (!dbus_message_iter_append_basic(&sub, DBUS_TYPE_STRING, &body_markup)) {
			error_name = DBUS_ERROR_NO_MEMORY;
			goto error;
		}
	}

	if (global_criteria(&state->config)->style.actions) {
		const char *actions = "actions";
		if (!dbus_message_iter_append_basic(&sub, DBUS_TYPE_STRING, &actions)) {
			error_name = DBUS_ERROR_NO_MEMORY;
			goto error;
		}
	}

	if (!dbus_message_iter_close_container(&iter, &sub)) {
		error_name = DBUS_ERROR_NO_MEMORY;
		goto error;
	}

	if (!dbus_connection_send(conn, reply, NULL)) {
		error_name = DBUS_ERROR_NO_MEMORY;
		goto error;
	}

	dbus_message_unref(reply);
	return true;

error:
	dbus_set_error(err, error_name, NULL);
	dbus_message_unref(reply);
	return false;
}

static void handle_notification_timer(void *data) {
	struct mako_notification *notif = data;
	notif->timer = NULL;

	struct mako_state *state = notif->state;

	close_notification(notif, MAKO_NOTIFICATION_CLOSE_EXPIRED);
	send_frame(state);
}

static bool handle_notify(DBusConnection *conn,
		DBusMessage *msg, void *data, DBusError *err) {
	struct mako_state *state = data;

	struct mako_notification *notif = create_notification(state);
	if (notif == NULL) {
		return false;
	}

	const char *app_name, *app_icon, *summary, *body;
	uint32_t replaces_id;
	DBusMessageIter iter;
	dbus_message_iter_init(msg, &iter);
	subd_message_read(&iter, err, &app_name, &replaces_id, &app_icon, &summary,
		&body, NULL);

	notif->app_name = strdup(app_name);
	notif->app_icon = strdup(app_icon);
	notif->summary = strdup(summary);
	notif->body = strdup(body);

	if (replaces_id > 0) {
		struct mako_notification *replaces =
			get_notification(state, replaces_id);
		if (replaces) {
			close_notification(replaces, MAKO_NOTIFICATION_CLOSE_REQUEST);
		}
	}

	DBusMessageIter actions_iter;
	dbus_message_iter_recurse(&iter, &actions_iter);

	while (dbus_message_iter_get_arg_type(&actions_iter) != DBUS_TYPE_INVALID) {
		const char *action_key, *action_title;

		if (!dbus_message_iter_has_next(&actions_iter)) {
			break;
		}
		subd_message_read(&actions_iter, err, &action_key, &action_title, NULL);

		struct mako_action *action = calloc(1, sizeof(struct mako_action));
		if (action == NULL) {
			return false;
		}
		action->notification = notif;
		action->key = strdup(action_key);
		action->title = strdup(action_title);
		wl_list_insert(&notif->actions, &action->link);
	}

	dbus_message_iter_next(&iter);
	DBusMessageIter array_iter;
	dbus_message_iter_recurse(&iter, &array_iter);

	while (dbus_message_iter_get_arg_type(&array_iter) != DBUS_TYPE_INVALID) {
		DBusMessageIter struct_iter;
		dbus_message_iter_recurse(&array_iter, &struct_iter);

		const char *hint = NULL;
		dbus_message_iter_get_basic(&struct_iter, &hint);
		dbus_message_iter_next(&struct_iter);
		DBusMessageIter variant_iter;
		dbus_message_iter_recurse(&struct_iter, &variant_iter);

		if (strcmp(hint, "urgency") == 0) {
			// Should be a byte but some clients (Chromium) send an uint32_t
			int type = dbus_message_iter_get_arg_type(&variant_iter);
			if (type == DBUS_TYPE_UINT32) {
				uint32_t urgency = 0;
				dbus_message_iter_get_basic(&variant_iter, &urgency);
				notif->urgency = urgency;
			} else {
				uint8_t urgency = 0;
				dbus_message_iter_get_basic(&variant_iter, &urgency);
				notif->urgency = urgency;
			}
		} else if (strcmp(hint, "category") == 0) {
			const char *category = NULL;
			notif->category = strdup(category);
		} else if (strcmp(hint, "desktop-entry") == 0) {
			const char *desktop_entry = NULL;
			dbus_message_iter_get_basic(&variant_iter, &desktop_entry);
			notif->desktop_entry = strdup(desktop_entry);
		}

		dbus_message_iter_next(&array_iter);
	}

	dbus_message_iter_next(&iter);

	int32_t requested_timeout;
	dbus_message_iter_get_basic(&iter, &requested_timeout);
	notif->requested_timeout = requested_timeout;

	int match_count = apply_each_criteria(&state->config.criteria, notif);
	if (match_count == -1) {
		// We encountered an allocation failure or similar while applying
		// criteria. The notification may be partially matched, but the worst
		// case is that it has an empty style, so bail.
		fprintf(stderr, "Failed to apply criteria\n");
		return -1;
	} else if (match_count == 0) {
		// This should be impossible, since the global criteria is always
		// present in a mako_config and matches everything.
		fprintf(stderr, "Notification matched zero criteria?!\n");
		return -1;
	}

	int32_t expire_timeout = notif->requested_timeout;
	if (expire_timeout < 0 || notif->style.ignore_timeout) {
		expire_timeout = notif->style.default_timeout;
	}

	insert_notification(state, notif);
	if (expire_timeout > 0) {
		notif->timer = add_event_loop_timer(&state->event_loop, expire_timeout,
			handle_notification_timer, notif);
	}

	send_frame(state);

	return subd_reply_method_return(conn, msg, err,
		DBUS_TYPE_UINT32, &notif->id,
		DBUS_TYPE_INVALID);
}

static bool handle_close_notification(DBusConnection *conn,
		DBusMessage *msg, void *data, DBusError *err) {
	struct mako_state *state = data;

	uint32_t id;
	if (!dbus_message_get_args(msg, err,
			DBUS_TYPE_UINT32, &id, DBUS_TYPE_INVALID)) {
		return false;
	}

	// TODO: check client
	struct mako_notification *notif = get_notification(state, id);
	if (notif) {
		close_notification(notif, MAKO_NOTIFICATION_CLOSE_REQUEST);
		send_frame(state);
	}

	return subd_reply_empty_str_method_return(conn, msg, err);
}

static bool handle_get_server_information(DBusConnection *conn,
		DBusMessage *msg, void *data, DBusError *err) {
	const char *name = "mako";
	const char *vendor = "emersion";
	const char *version = "0.0.0";
	const char *spec_version = "1.2";

	return subd_reply_method_return(conn, msg, err,
		DBUS_TYPE_STRING, &name,
		DBUS_TYPE_STRING, &vendor,
		DBUS_TYPE_STRING, &version,
		DBUS_TYPE_STRING, &spec_version,
		DBUS_TYPE_INVALID);
}

static const struct subd_member members[] = {
	{SUBD_METHOD, .m = {"GetCapabilities", handle_get_capabilities, "", "as"}},
	{SUBD_METHOD, .m = {"Notify", handle_notify, "susssasa{sv}i", "u"}},
	{SUBD_METHOD, .m = {"CloseNotification", handle_close_notification, "u", ""}},
	{SUBD_METHOD, .m = {"GetServerInformation", handle_get_server_information, "", "ssss"}},
	{SUBD_SIGNAL, .s = {"ActionInvoked", "us"}},
	{SUBD_SIGNAL, .s = {"NotificationClosed", "uu"}},
	{SUBD_MEMBERS_END, .e=0},
};

void init_dbus_xdg(struct mako_state *state, DBusError *err) {
	subd_add_object_vtable(state->bus, service_path, service_interface, members,
			state, err);
}

void notify_notification_closed(struct mako_notification *notif,
		enum mako_notification_close_reason reason) {
	struct mako_state *state = notif->state;

	subd_emit_signal(state->bus, service_path, service_interface,
		"NotificationClosed", NULL,
		DBUS_TYPE_UINT32, &notif->id,
		DBUS_TYPE_UINT32, &reason,
		DBUS_TYPE_INVALID);
}

void notify_action_invoked(struct mako_action *action) {
	struct mako_state *state = action->notification->state;

	subd_emit_signal(state->bus, service_path, service_interface,
		"ActionInvoked", NULL,
		DBUS_TYPE_UINT32, &action->notification->id,
		DBUS_TYPE_STRING, &action->key,
		DBUS_TYPE_INVALID);
}
