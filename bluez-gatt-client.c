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
 * This files deals with all the bluez GATT related stuff.
 * We look for some specific GATT characteristics and register events.
 */

#include "bluez-gatt-client.h"
#include "uhid.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define KEY_PRESS_SVC       "0000ffe0-0000-1000-8000-00805f9b34fb"
#define KEY_PRESS_CHAR_DATA "0000ffe1-0000-1000-8000-00805f9b34fb"

static gchar *notification_charac = NULL;
static gchar *notification_device_path = NULL;
static gint key_pressed_sub_id = 0;

static GVariant *bluez_get_objects(GDBusConnection *connection) {
    GError *error = NULL;
    GVariant *objects;

    objects = g_dbus_connection_call_sync(connection, "org.bluez", "/",
                                          "org.freedesktop.DBus.ObjectManager",
                                          "GetManagedObjects", NULL, NULL,
                                          G_DBUS_CALL_FLAGS_NONE, -1, NULL,
                                          &error);
    if (error) {
        printf("Cannot get bluez objects : %s\n", error->message);
        g_error_free(error);
    }

    return objects;
}

static gboolean bluez_start_notify(GDBusConnection *connection,
                                                    gchar *charac) {
    GError *error = NULL;
    gboolean ret = FALSE;
    g_dbus_connection_call_sync(connection, "org.bluez", charac,
                                                "org.bluez.GattCharacteristic1",
                                                "StartNotify", NULL, NULL,
                                                G_DBUS_CALL_FLAGS_NONE, -1,
                                                NULL, &error);

    if (error) {
        printf("Cannot start notify on charac %s : %s\n", charac, error->message);
        g_error_free(error);
    } else {
        notification_charac = g_strdup(charac);
        ret = TRUE;
        printf("Started notifications on %s\n", charac);
    }

    return ret;
}

static gboolean bluez_stop_notify(GDBusConnection *connection,
                                                gchar *charac) {
    GError *error = NULL;
    gboolean ret = FALSE;
    g_dbus_connection_call_sync(connection, "org.bluez", charac,
                                                "org.bluez.GattCharacteristic1",
                                                "StopNotify", NULL, NULL,
                                                G_DBUS_CALL_FLAGS_NONE, -1,
                                                NULL, &error);

    if (error) {
        printf("Cannot stop notify on charac %s : %s\n", charac, error->message);
        g_error_free(error);
    } else {
        printf("Stopped notifications on %s\n", charac);
        ret = TRUE;
    }

    return ret;

}

static void key_event_cb(uint8_t evt) {
    int left_down = 0, right_down = 0;

    if (evt & 0x01)
        left_down = 1;
    if (evt & 0x02)
        right_down = 1;

    uhid_event(left_down, right_down);
}

static void on_key_pressed(GDBusConnection *connection,
                           const gchar *sender_name,
                           const gchar *object_path,
                           const gchar *interface_name,
                           const gchar *signal_name, GVariant *parameters,
                           gpointer user_data) {

    GVariant *arr_prop = g_variant_get_child_value(parameters, 1);

    GVariantIter prop_iter;
    GVariant *prop_val;
    gchar *prop_name;
    const uint8_t *byte_array;
    gsize nb_elems;

    g_variant_iter_init(&prop_iter, arr_prop);
    while (g_variant_iter_loop(&prop_iter, "{sv}", &prop_name, &prop_val)) {

        if (!g_strcmp0(prop_name, "Value")) {

            byte_array = g_variant_get_fixed_array(prop_val, &nb_elems, sizeof(uint8_t));
            if (nb_elems != 1) {
                printf("Unexpected number of elems ( %zu )\n", nb_elems);
            } else {
                key_event_cb(byte_array[0]);
            }
        }
    }

}

static gboolean bluez_setup_gatt_client(GDBusConnection *connection,
                                                gchar *charac_path) {

    if (bluez_start_notify(connection, charac_path)) {
        key_pressed_sub_id = g_dbus_connection_signal_subscribe(connection,
                                                            "org.bluez",
                                                            "org.freedesktop.DBus.Properties",
                                                            "PropertiesChanged",
                                                            charac_path,
                                                            "org.bluez.GattCharacteristic1",
                                                            G_DBUS_SIGNAL_FLAGS_NONE,
                                                            on_key_pressed,
                                                            NULL, NULL);
        printf("Subscribed to key press events on %s\n", charac_path);
        return TRUE;
    }
    return FALSE;
}

/** ----------------------------------------------------------------------------
 * Expects a "a{sa{sv}}"
 * Scans each interface array, to find an UUID list, and if found, returns
 * TRUE if any of the UUID is the same as the one given as a parameter.
 * returns FALSE else.
 */
static gboolean bluez_obj_has_UUID(GVariant *objects, const gchar *uuid) {
    if (!objects)
        return FALSE;

    if (!g_variant_is_of_type( objects, G_VARIANT_TYPE("a{sa{sv}}"))) {
        printf("Wrong variant type, expected a{sa{sv}}, got %s\n",
                                g_variant_get_type_string(objects));
        return FALSE;
    }

    gboolean has_uuid = FALSE;
    GVariantIter iface_iter, prop_iter;
    GVariant *props, *prop_value;
    gchar *iface_name, *prop_name;
    const gchar *prop_uuid;

    g_variant_iter_init(&iface_iter, objects);
    while (g_variant_iter_loop(&iface_iter, "{s@a{sv}}", &iface_name, &props)) {
        
        /* We are looking for the interface "org.bluez.Device1" */
        if (g_strcmp0(iface_name, "org.bluez.GattCharacteristic1"))
            continue;

        g_variant_iter_init(&prop_iter, props);
        while (g_variant_iter_loop(&prop_iter, "{sv}", &prop_name, &prop_value)) {
            /* We are looking for "UUIDs" property */
            if (g_strcmp0(prop_name, "UUID"))
                continue;
            
            prop_uuid = g_variant_get_string(prop_value, NULL);
            if (!g_strcmp0(prop_uuid, uuid))
                has_uuid = TRUE;
        }
    }
    
    return has_uuid;
}

static gchar *bluez_charac_get_device(const gchar *charac_path) {
    int i = strlen(charac_path);

    /* Dirty, we should be getting the device path via dbus, by finding the
     * device that has the SVC UUID for the GATT service we want.
     * This is a dirty way to get the device path : 
     * the charac path is : 
     * [variable prefix]/{hci0,hci1,...}/dev_XX_XX_XX_XX_XX_XX/serviceXX/charYYYY
     *
     * What we do is we remove the 2 last fieds from the charac path to get the
     * device path. */
    while(charac_path[i--] != '/');
    while(charac_path[i--] != '/');
    i++;
    
    gchar *dev_path = g_malloc( (i+1) * sizeof(gchar));
    strncpy(dev_path, charac_path, i);
    dev_path[i] = '\0';

    return dev_path;
}

static gboolean bluez_device_is_connected(GDBusConnection *connection,
                                          const gchar *device_path) {
    GError *error = NULL;
    gboolean ret = FALSE;
    GVariant *prop = g_dbus_connection_call_sync(connection, "org.bluez",
                                                device_path,
                                                "org.freedesktop.DBus.Properties",
                                                "Get", g_variant_new("(ss)", "org.bluez.Device1", "Connected"),
                                                NULL, G_DBUS_CALL_FLAGS_NONE,
                                                -1, NULL, &error);

    if (error) {
        printf("Error getting Connected property for device %s\n", device_path);
        g_error_free(error);
    } else {
        GVariant *tmp = NULL;
        g_variant_get(prop, "(v)", &tmp);
        ret = g_variant_get_boolean(tmp);
        g_variant_unref(prop);
    }

    return ret;
}

static gboolean bluez_device_connect(GDBusConnection *connection,
                                     const gchar *device_path) {
    GError *error = NULL;
    gboolean ret = FALSE;
    g_dbus_connection_call_sync(connection, "org.bluez", device_path,
                                "org.bluez.Device1", "Connect", NULL,
                                NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);

    if (error) {
        printf("Error connecting device %s\n", device_path);
        g_error_free(error);
    } else {
        ret = TRUE;
    }

    return ret;

}

static gboolean bluez_device_disconnect(GDBusConnection *connection,
                                     const gchar *device_path) {
    GError *error = NULL;
    gboolean ret = FALSE;
    g_dbus_connection_call_sync(connection, "org.bluez", device_path,
                                "org.bluez.Device1", "Disconnect", NULL,
                                NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);

    if (error) {
        printf("Error disconnecting device %s\n", device_path);
        g_error_free(error);
    } else {
        ret = TRUE;
    }

    return ret;

}
static gboolean bluez_setup_init(GDBusConnection *connection) {
    gchar *char_path = NULL;
    gchar *device_path = NULL;
    GVariant *objects;
    gboolean res = TRUE;

    objects = bluez_get_objects(connection);

    if (!objects) {
        printf("Cannot get bluez objects\n");
        return FALSE;
    }

    /* root elem : a{oa{sa{sv}}} ( We get it out if its variant wrapper )*/
    GVariant *root_elem = g_variant_get_child_value(objects, 0);

    /* Iterate on root_elem. Each elem is oa{sa{sv}}, where :
     *
     * 'o' is the object path
     * 'a{sa{sv}}' is an array of the interfaces supported by this object
     *
     * for each interface ( sa{sv} ), we have :
     * 's' : The interface name
     * 'a{sv}' : The properties of this interface
     *
     * for each property ( sv ), we have :
     * 's' : the property name
     * 'v' : the property value ( it is a variant, the underlying type depends
     *       on the property, see the bluez doc for each property type )
     *
     * */
    
    GVariantIter obj_iter;
    GVariant *ifaces;
    gchar *path;

    g_variant_iter_init(&obj_iter, root_elem);
    while (g_variant_iter_loop(&obj_iter, "{o@a{sa{sv}}}", &path, &ifaces)) {
        if (bluez_obj_has_UUID(ifaces, KEY_PRESS_CHAR_DATA)) {

            /* We might want to add support for multiple devices as HID later */
            if (char_path) {
                printf("Found multiple key pressed characteristics. Ignoring %s\n", path);
                continue;
            } else {
                printf("Found key pressed characteristic : %s\n", path);
                char_path = g_strdup(path);
                device_path = bluez_charac_get_device(path);
            }
        }
    }

    g_variant_unref(root_elem);
    g_variant_unref(objects);

    if (!char_path) {
        printf("No device found with key pressed service\n");
        return FALSE;
    }

    printf("Device : %s\n", device_path);
    if (!bluez_device_is_connected(connection, device_path)) {
        printf("Device %s is not connected, trying to connect...\n", device_path);
        if (!bluez_device_connect(connection, device_path)) {
            printf("Cannot connect to device\n");
            res = FALSE;
        } else {
            printf("Connected successfully\n");
        }
    }

    notification_device_path = device_path;

    if (res)
        res = bluez_setup_gatt_client(connection, char_path);

    g_free(char_path);

    return res;
}

gboolean bluez_setup(GDBusConnection *connection) {
    return bluez_setup_init(connection);
}

void bluez_cleanup(GDBusConnection *connection) {
    if (notification_charac) {
        if (!bluez_stop_notify(connection, notification_charac)) {
            printf("Error stopping notifications\n");
        }
        g_free(notification_charac);
        notification_charac = NULL;
    }

    if (notification_device_path) {
        bluez_device_disconnect(connection, notification_device_path);
        g_free(notification_device_path);
    }
}
