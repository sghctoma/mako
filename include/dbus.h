#ifndef _MAKO_DBUS_H
#define _MAKO_DBUS_H

#ifdef HAS_SDBUS
#include <stdbool.h>
#include <systemd/sd-bus.h>
#else
#include "subd.h"
#endif

struct mako_state;
struct mako_notification;
struct mako_action;
enum mako_notification_close_reason;

bool init_dbus(struct mako_state *state);
void finish_dbus(struct mako_state *state);
void notify_notification_closed(struct mako_notification *notif,
	enum mako_notification_close_reason reason);

void notify_action_invoked(struct mako_action *action);

#ifdef HAS_SDBUS
int init_dbus_xdg(struct mako_state *state);

int init_dbus_mako(struct mako_state *state);
#else
void init_dbus_xdg(struct mako_state *state, DBusError *error);

void init_dbus_mako(struct mako_state *state, DBusError *error);
#endif

#endif
