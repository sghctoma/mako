#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "list.h"
#include "subd.h"

struct vtable_userdata {
	struct list_t *interfaces;
	void *userdata;
};

struct interface {
	const char *name;
	const struct subd_member *members;
};

struct path {
	const char *path;
	char *introspection_data;
	struct list_t *interfaces;
};

static struct list_t *paths = NULL;

static bool add_args(FILE *stream, const char *sig, const char *end) {
	DBusSignatureIter iter;
	if (!dbus_signature_validate(sig, NULL)) {
		return false;
	}

	if (strlen(sig) > 0) {
		dbus_signature_iter_init(&iter, sig);
		do {
			char *s = dbus_signature_iter_get_signature(&iter);
			fprintf(stream, "   <arg type=\"%s\" %s />\n", s, end);
			dbus_free(s);
		} while (dbus_signature_iter_next(&iter));
	}

	return true;
}

static const char *generate_introspection_data(struct path *path) {
	free(path->introspection_data);

	size_t size;
	FILE *stream = open_memstream(&path->introspection_data, &size);
	if (stream == NULL) {
		return NULL;
	}

	// Write the DOCTYPE entity, and start the <node> element.
	fputs(DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE, stream);
	fputs("<node>\n", stream);

	// Iterate through the path's interfaces, ...
	for (struct node *n = path->interfaces->head; n != NULL; n = n->next) {
		struct interface *interface = n->data;
		fprintf(stream, " <interface name=\"%s\">\n", interface->name);

		const struct subd_member *member = interface->members;
		while (member->type != SUBD_MEMBERS_END) {
			switch (member->type) {
			case SUBD_METHOD:
				fprintf(stream, "  <method name=\"%s\">\n", member->m.name);
				add_args(stream, member->m.input_signature, "direction=\"in\"");
				add_args(stream, member->m.output_signature, "direction=\"out\"");
				fprintf(stream, "  </method>\n");
				break;
			case SUBD_SIGNAL:
				fprintf(stream, "  <signal name=\"%s\">\n", member->s.name);
				add_args(stream, member->s.signature, "");
				fprintf(stream, "  </signal>\n");
				break;
			case SUBD_PROPERTY:
				fprintf(stream, "  <property name=\"%s\" type=\"%s\" ",
					member->p.name, member->p.signature);
				switch (member->p.access) {
				case SUBD_PROPERTY_READ:
					fprintf(stream, "\"read\" />\n");
					break;
				case SUBD_PROPERTY_WRITE:
					fprintf(stream, "\"write\" />\n");
					break;
				case SUBD_PROPERTY_READWRITE:
					fprintf(stream, "\"readwrite\" />\n");
					break;
				}
				break;
			case SUBD_MEMBERS_END:
				// not gonna happen
				break;
			}
			member++;
		}
		fprintf(stream, " </interface>\n");
	}
	fprintf(stream, "</node>");
	fclose(stream);

	return path->introspection_data;
}

static bool handle_introspect(DBusConnection *conn, DBusMessage *msg,
		void *data, DBusError *error) {
	// Find the path the message was sent to, so we can access its list of
	// implemented interfaces.
	const char *path_name = dbus_message_get_path(msg);
	struct path *path = NULL;
	for (struct node *n = paths->head; n != NULL; n = n->next) {
		if (strcmp(((struct path *)n->data)->path, path_name) == 0) {
			path = n->data;
			break;
		}
	}

	if (path == NULL) {
		// Something is seriously weird here. Path was not found, but then how
		// was the method handler called??
		dbus_set_error(error, DBUS_ERROR_INVALID_ARGS,
			"Path was not found in paths list.");
		return false;
	}

	if (path->introspection_data == NULL) {
		// This shouldn't really happen, introspection data is generated when
		// the vtable is registered.
		if (generate_introspection_data(path) == NULL) {
			dbus_set_error(error, DBUS_ERROR_NO_MEMORY,
				"Introspection data could not be generated.");
			return false;
		}
	}

	return subd_reply_method_return(conn, msg, error,
		DBUS_TYPE_STRING, &path->introspection_data,
		DBUS_TYPE_INVALID);
}

static const struct subd_member introspectable_members[] = {
	{SUBD_METHOD, .m = {"Introspect", handle_introspect, "", "s"}},
	{SUBD_MEMBERS_END, .e=0},
};

/**
 * Helper function for vtable_dispatch that returns wither NULL, or the method
 * object member with the name "name".
 */
static const struct subd_member *find_member(const struct subd_member *members,
		const char *name) {
	const struct subd_member *member = members;
	while (member->type != SUBD_MEMBERS_END) {
		if (member->type == SUBD_METHOD && strcmp(member->m.name, name) == 0) {
			return member;
		}
		++member;
	}
	return NULL;
}

/**
 * Helper function for vtable_dispatch that calls the method object member's
 * handler function, and sends an error message if necessary.
 */
static bool call_method(const struct subd_member *member, DBusConnection *conn,
		DBusMessage *msg, void *userdata) {
	DBusError error;
	dbus_error_init(&error);
	if (!member->m.handler(conn, msg, userdata, &error)) {
		DBusMessage *error_message =
			dbus_message_new_error(msg, error.name, error.message);
		dbus_connection_send(conn, error_message, 0);
		dbus_error_free(&error);
		return false;
	}
	return true;
}

/**
 * This function receives a list of interfaces implemented by the destination
 * object in "userdata->interfaces", and iterates through them and their members
 * to find the called method. When the method is found, its handler function is
 * called with "data" set to "userdata->userdata".
 */
static DBusHandlerResult vtable_dispatch(DBusConnection *connection,
		DBusMessage *message, void *userdata) {
	struct vtable_userdata *data = userdata;
	const char *interface_name = dbus_message_get_interface(message);
	const char *member_name = dbus_message_get_member(message);
	if (interface_name == NULL || member_name == NULL) {
		// something is wrong, no need to try with other handlers
		return DBUS_HANDLER_RESULT_HANDLED;
	}

	struct list_t *interfaces = data->interfaces;
	for (struct node *n = interfaces->head; n != NULL; n = n->next) {
		struct interface *interface = n->data;
		if (strcmp(interface->name, interface_name) == 0) {
			const struct subd_member *member =
				find_member(interface->members, member_name);
			if (member != NULL) {
				call_method(member, connection, message, data->userdata);
				return DBUS_HANDLER_RESULT_HANDLED;
			}
		}
	}

	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static const DBusObjectPathVTable vtable = {
	.message_function = vtable_dispatch,
	.unregister_function = NULL,
};

bool subd_add_object_vtable(DBusConnection *connection, const char *path_name,
		const char *interface, const struct subd_member *members, void *userdata,
		DBusError *error) {
	if (paths == NULL) {
		paths = list_create();
	}

	// See if this path is already registered. If it is, it must have at least
	// one interface, so get a pointer to the interfaces list.
	struct list_t *interfaces = NULL;
	struct path *path = NULL;
	for (struct node *n = paths->head; n != NULL; n = n->next) {
		struct path *p = n->data;
		if (strcmp(path_name, p->path) == 0) {
			path = p;
			interfaces = p->interfaces;
			break;
		}
	}

	if (interfaces == NULL) {
		// Path was not registered before, so first we create an interface list,
		// and append Introspectable to it. We want every path to implement
		// org.freedesktop.DBus.Introspectable.
		// TODO: Also implement the following:
		//        - org.freedesktop.DBus.Peer
		//        - org.freedesktop.DBus.Properties
		interfaces = list_create();
		struct interface *new_interface = malloc(sizeof(struct interface));
		if (interface == NULL) {
			dbus_set_error(error, DBUS_ERROR_NO_MEMORY, NULL);
			return false;
		}
		new_interface->name = strdup("org.freedesktop.DBus.Introspectable");
		new_interface->members = introspectable_members;
		list_append(interfaces, new_interface);

		// Create the path with the new interface list, append it to the
		// list of paths, ...
		struct path *new_path = malloc(sizeof(struct path));
		if (new_path == NULL) {
			dbus_set_error(error, DBUS_ERROR_NO_MEMORY, NULL);
			return false;
		}
		new_path->path = strdup(path_name);
		new_path->interfaces = interfaces;
		new_path->introspection_data = NULL; // This will be set later.
		list_append(paths, new_path);
		path = new_path;

		// ... and also register it.
		// We merge userdata with the interfaces list in a struct
		// vtable_userdata here, so vtable_dispatch will know what interfaces to
		// traverse, and what userdata to pass to the actual handler functions.
		struct vtable_userdata *data = malloc(sizeof(struct vtable_userdata));
		if (data == NULL) {
			dbus_set_error(error, DBUS_ERROR_NO_MEMORY, NULL);
			return false;
		}
		data->interfaces = interfaces;
		data->userdata = userdata;
		if (!dbus_connection_try_register_object_path(connection, path_name,
				&vtable, data, error)) {
			return false;
		}
	}

	//TODO: Decide what to do if interface is already registered. Replace
	//memeber list? Append new members to list? Throw an error?

	// Append the new interface to the path's interface list.
	struct interface *new_interface = malloc(sizeof(struct interface));
	if (new_interface == NULL) {
		dbus_set_error(error, DBUS_ERROR_NO_MEMORY, NULL);
		return false;
	}
	new_interface->name = strdup(interface);
	new_interface->members = members;
	list_append(interfaces, new_interface);

	// (Re)generate introspection XML for this path.
	generate_introspection_data(path);

	return true;
}
