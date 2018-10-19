/**
 * @file subd.h
 * @brief Subd is a small set of convenience wrappers around the libdbus API.
 * @author sghctoma
 * @date 15 October 2018
 *
 * Subd is by no means a complete wrapper around libdbus, it just provides some
 * functions that make using the low-level libdbus API more convenient. The
 * scope of subd is very limited currently, it basically provides helpers only
 * in areas I needed to work with:
 *
 *  - Opening a connection to the bus.
 *  - Reading and sending messages.
 *  - Implementing the Introspectable interface.
 *  - Dispatching handlers for method type members.
 *  - Handling watches.
 * 
 * @see https://github.com/sghctoma/subd
 * @see https://dbus.freedesktop.org/doc/api/html/group__DBus.html
 */

#ifndef _SUBD_H
#define _SUBD_H

#include <dbus/dbus.h>
#include <semaphore.h>
#include <stdbool.h>

struct pollfd;

/**
 * @brief Initializes a connection to the session bus.
 *
 * This function initializes the internal DBus locks, connects to the session
 * bus, and requests the name @p service_name. In case of error, @p error is
 * filled, and @c NULL is returned.
 * @param service_name The requested service name.
 * @param error Will contain error information in case of failure.
 * @return Pointer to created the DBus connection, or NULL.
 */
DBusConnection *subd_open_session(const char *service_name, DBusError *error);

/**
 * @brief Sends a signal message.
 *
 * This function creates a signal message from the fields passed as the variable
 * argument list, and sends it. The same rules apply regarding this list as with
 * @c dbus_message_append_args (i.e. the message either consists of a list of
 * basic type values, or a fixed-length array of basic type values).
 *
 * @param connection A pointer to the DBus connection.
 * @param path The path to the object emitting the signal.
 * @param interface The interface the signal is emitted from.
 * @param name The name of the signal.
 * @param error Will contain error information in case of failure.
 * @param va_list The fields to construct the signal from.
 * @return A @c bool that represents success or failure.
 * @see https://dbus.freedesktop.org/doc/api/html/group__DBusMessage.html#ga591f3aab5dd2c87e56e05423c2a671d9
 */
bool subd_emit_signal(DBusConnection *connection, const char *path,
	const char *interface, const char *name, DBusError *error,  ...);

/**
 * @brief Sends a reply to method call.
 *
 * This function creates a reply to a given @p message from the fields passed as
 * the variable argument list, and sends it. The same rules apply regarding this
 * list as with @c dbus_message_append_args (i.e. the message either consists of
 * a list of basic type values, or a fixed-length array of basic type values).
 * @param connection A pointer to the DBus connection.
 * @param message The message to reply to.
 * @param error Will contain error information in case of failure.
 * @param va_list The fields to construct the reply from. This variable argument
 *                list should match that of @c dbus_message_append_args.
 * @return A @c bool that represents success or failure.
 * @see https://dbus.freedesktop.org/doc/api/html/group__DBusMessage.html#ga591f3aab5dd2c87e56e05423c2a671d9
 */
bool subd_reply_method_return(DBusConnection *connnection, DBusMessage *message,
	DBusError *error, ...);

/**
 * @brief Reads values of basic types.
 *
 * This function uses a message iterator to read the message. It can be used to
 * conveniently read multiple basic values from a message, while advancing an
 * iterator, so that reading can be continued later.
 * @param iter The previously initialized message iterator
 * @param error Will contain error information in case of failure.
 * @param va_list Pointers to basic types to read values into.
 * @return A @c bool that represents success or failure.
 */
bool subd_message_read(DBusMessageIter *iter, DBusError *error, ...);

/**
 * @brief A storage for DBus waches
 *
 * This struct stores the DBus watches, and their corresponging file
 * descriptors. This will be auto-updated whenever DBus needs additional file
 * descriptors to watch. You can also store other, not DBus-related file
 * descriptors here, so you can directly use @p fds as a parameter for @c poll().
 * If you add non-watch file descriptors, make sure to set their corresponding
 * watch to @c NULL, so that #subd_process_watches knows to skip them.
 */
struct subd_watches {
	struct pollfd *fds;			/**< Array of @c pollfd structs */
	struct DBusWatch **watches;	/**< Arrah of DBus watches */
	int capacity;				/**< Currently allocated size in item number */
	int length;					/**< Number of currently allocated items */
	sem_t mutex;				/**< Lock for safe watch addition/removal */
};

/**
 * @brief Initializes and registers a subd_watches structure
 *
 * This function creates a subd_watches instance, and pre-populates it with
 * the non-DBus file descriptors passed as @p fds. It also registers the add,
 * remove and toggle functions that will handle automatic file descriptor
 * additions/removals.
 * @param connection A pointer to the DBus connection.
 * @param fds Array of non-dbus file descriptors.
 * @param size Size of @p fds
 * @param error Will contain error information in case of failure
 * @return A pointer to the created subd_watches structure, or NULL.
 * @see https://dbus.freedesktop.org/doc/api/html/group__DBusConnection.html#gaebf031eb444b4f847606aa27daa3d8e6
 */
struct subd_watches *subd_init_watches(struct DBusConnection *connection,
	struct pollfd *fds, int size, DBusError *error);

/**
 * @brief Handles DBus watches
 *
 * This function should be called from the event loop after a successful @c poll
 * to handle the DBus watches that need to be handled (= the watches whose file
 * descriptor returned an event).
 * @param connection A pointer to the DBus connection.
 * @param watches A pointer to the subd_watches structure.
 */
void subd_process_watches(DBusConnection *connection,
	struct subd_watches *watches);

/**
 * @brief Represents the three DBus member types.
 */
enum subd_member_type {
	SUBD_METHOD,		/**< Method type member. */
	SUBD_SIGNAL,		/**< Signal type member. */
	SUBD_PROPERTY,		/**< Property type member. */
	SUBD_MEMBERS_END,	/**< Signals the end of a member list. */
};

/**
 * @brief Represents the three possible access types for DBus properties.
 */
enum subd_property_access {
	SUBD_PROPERTY_READ,			/**< "read" access */
	SUBD_PROPERTY_WRITE,		/**< "write* access */
	SUBD_PROPERTY_READWRITE,	/**< "readwrite" access */
};

/**
 * @brief Represents a DBus member object.
 *
 * This struct represents a DBus object member, and serves two purpose:
 *
 *  - If the member is a method, it has a pointer to the method's handler
 *    function, so that the vtable dispatcher knows where to dispatch execution.
 *  - It has the metadata for the member (name, output and/or input signatures,
 *    access) that introspection data can be built from.
 *
 * Example member array that contains one method, one signal, and one property:
 *
 * @code{.c}
 * struct subd_member members[] = {
 *   {SUBD_METHOD, .m = {"SomeMethod", some_handler, "u", "as"}},
 *   {SUBD_SIGNAL, .s = {"SomeSignal", "u"}},
 *   {SUBD_PROPERTY, .p = {"SomeProperty", "s", SUBD_PROPERTY_READ}},
 *   {SUBD_MEMBERS_END, .e=0},
 * };
 * @endcode
 */
struct subd_member {
	enum subd_member_type type;
	union {
		struct {
			const char *name;
			bool (*handler)(DBusConnection *, DBusMessage *, void *, DBusError *);
			const char *input_signature;
			const char *output_signature;
		} m;
		struct {
			const char *name;
			const char *signature;
		} s;
		struct {
			const char *name;
			const char *signature;
			enum subd_property_access access;
		} p;
		int e;
	};
};

/**
 * @brief Registers method handlers.
 *
 * This functions registers members and their handler functions for one
 * interface of one path. You should create a separate array of subd_members
 * for all of your interfaces you want vtable handlers for, and register them
 * with DBus using this function. The last element of the array always should be
 * SUBD_MEMBERS_END.
 * @param connection A pointer to the DBus connection.
 * @param path The DBus object path to register @p interface to.
 * @param interface The DBus interface that are to be registered to @p path.
 * @param members The members of @p interface.
 * @param userdate Arbitrary data to pass to method handlers.
 * @param error Will contain error information in case of failure.
 * @return A @c bool that represents success or failure.
 * @see https://dbus.freedesktop.org/doc/api/html/group__DBusConnection.html#ga708b1e108feed18f5775ff404c9dda4b
 */
bool subd_add_object_vtable(DBusConnection *connection, const char *path,
	const char *interface, const struct subd_member *members, void *userdata,
	DBusError *error);

#endif
