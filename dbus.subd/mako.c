#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "criteria.h"
#include "dbus.h"
#include "mako.h"
#include "notification.h"
#include "wayland.h"

static const char *service_path = "/fr/emersion/Mako";
static const char *service_interface = "fr.emersion.Mako";

static bool handle_dismiss_all_notifications(DBusConnection *conn,
		DBusMessage *msg, void *data, DBusError *err) {
	struct mako_state *state = data;

	close_all_notifications(state, MAKO_NOTIFICATION_CLOSE_DISMISSED);
	send_frame(state);

	return subd_reply_empty_str_method_return(conn, msg, err);
}

static bool handle_dismiss_last_notification(DBusConnection *conn,
		DBusMessage *msg, void *data, DBusError *err) {
	struct mako_state *state = data;

	if (wl_list_empty(&state->notifications)) {
		goto done;
	}

	struct mako_notification *notif =
		wl_container_of(state->notifications.next, notif, link);
	close_notification(notif, MAKO_NOTIFICATION_CLOSE_DISMISSED);
	send_frame(state);

done:
	return subd_reply_empty_str_method_return(conn, msg, err);
}

static bool handle_invoke_action(DBusConnection *conn,
		DBusMessage *msg, void *data, DBusError *err) {
	struct mako_state *state = data;

	const char *action_key;
	dbus_message_get_args(msg, err,
			DBUS_TYPE_STRING, &action_key,
			DBUS_TYPE_INVALID);
	if (dbus_error_is_set(err)) {
		return false;
	}

	if (wl_list_empty(&state->notifications)) {
		goto done;
	}

	struct mako_notification *notif =
		wl_container_of(state->notifications.next, notif, link);
	struct mako_action *action;
	wl_list_for_each(action, &notif->actions, link) {
		if (strcmp(action->key, action_key) == 0) {
			notify_action_invoked(action);
			break;
		}
	}

done:
	return subd_reply_empty_str_method_return(conn, msg, err);
}

static bool handle_reload(DBusConnection *conn,
		DBusMessage *msg, void *data, DBusError *err) {
	struct mako_state *state = data;

	if (reload_config(&state->config, state->argc, state->argv) != 0) {
		dbus_set_error_const(err,
			"fr.emersion.Mako.InvalidConfig",
			"Unable to parse configuration file");
		return false;
	}

	struct mako_notification *notif;
	wl_list_for_each(notif, &state->notifications, link) {
		finish_style(&notif->style);
		init_empty_style(&notif->style);
		apply_each_criteria(&state->config.criteria, notif);
	}

	send_frame(state);

	return subd_reply_empty_str_method_return(conn, msg, err);
}

static const struct subd_member members[] = {
	{SUBD_METHOD, .m = {"DismissAllNotifications", handle_dismiss_all_notifications, "", ""}},
	{SUBD_METHOD, .m = {"DismissLastNotification", handle_dismiss_last_notification, "", ""}},
	{SUBD_METHOD, .m = {"InvokeAction", handle_invoke_action, "s", ""}},
	{SUBD_METHOD, .m = {"Reload", handle_reload, "", ""}},
	{SUBD_MEMBERS_END, .e=0},
};

void init_dbus_mako(struct mako_state *state, DBusError *err) {
	subd_add_object_vtable(state->bus, service_path, service_interface, members,
		state, err);
}
