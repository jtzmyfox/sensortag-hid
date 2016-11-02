/*
 * MIT License
 *
 * Copyright (c) 2016 Maxime Chevallier
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *
 * Sensortag HID
 *
 * This software converts the TI sensortag ( CC2650 version ) key events into
 * HID events ( right and left key presses ).
 *
 * This is a demo software, using the BlueZ 5 DBus GATT API to get events, and
 * uhid to converts them into HID events.
 *
 * This file deals with the mainloop init, the DBus setup and cleanup.
 */


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <glib.h>
#include <gio/gio.h>

#include "bluez-gatt-client.h"
#include "uhid.h"

#define OWN_BUS_NAME "demo.sensortag.hid"
#define BLUEZ_BUS_NAME "org.bluez"

static gint owner_id = 0;
static gint bluez_id = 0;
GMainLoop *loop = NULL;
GDBusConnection *c = NULL;

/**
 * Called at exit
 * */
void cleanup() {

	if (c) {
		bluez_cleanup(c);
		c = NULL;
	}

	if (owner_id) {
		g_bus_unown_name(owner_id);
		owner_id = 0;
	}

	if (bluez_id) {
		g_bus_unwatch_name(bluez_id);
		bluez_id = 0;
	}

	uhid_cleanup();

	if (loop && g_main_loop_is_running(loop))
		g_main_loop_quit(loop);
}

static void sig_handler(int signo) {
	if (signo == SIGINT || signo == SIGTERM)
		cleanup();
}

static void on_bluez_appeared(GDBusConnection *connection, const gchar *name,
								  const gchar *name_owner, gpointer user_data) {

	c = connection;
	if (!bluez_setup(connection)) {
		printf("Unable to setup bluez watchers\n");
		cleanup();
	}

	if (!uhid_init()) {
		printf("Unable to init uhid\n");
		cleanup();
	}
}

static void on_bluez_vanished(GDBusConnection *connection, const gchar *name,
														  gpointer user_data) {
	
	bluez_cleanup(connection);
}

static void on_name_acquired(GDBusConnection *connection, const gchar *name,
														 gpointer user_data) {

	bluez_id = g_bus_watch_name(G_BUS_TYPE_SYSTEM, BLUEZ_BUS_NAME,
								G_BUS_NAME_WATCHER_FLAGS_AUTO_START,
								on_bluez_appeared, on_bluez_vanished,
								NULL, NULL);

}

static void on_name_lost(GDBusConnection *connection, const gchar *name,
													 gpointer user_data) {

	printf("Name lost, terminating.\n");
	exit(1);

}

int main(int argc, char **argv) {

	if (atexit(cleanup)) {
		printf("Cannot register cleanup callback\n");
		return 1;
	}
	
	if (signal(SIGINT, &sig_handler) == SIG_ERR ||
		signal(SIGTERM, &sig_handler) == SIG_ERR) {
		printf("Cannot register sig handler\n");
		return 1;
	}

	loop = g_main_loop_new(NULL, FALSE);
	
	owner_id = g_bus_own_name(G_BUS_TYPE_SYSTEM, OWN_BUS_NAME,
							  G_BUS_NAME_OWNER_FLAGS_NONE, NULL,
							  on_name_acquired, on_name_lost,
							  NULL, NULL);

	g_main_loop_run(loop);

	return 0;
}
