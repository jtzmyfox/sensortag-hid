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
 */

/* 
 * This code is derivated from the Linux sample code for uhid,
 * Copyright (c) 2012-2013 David Herrmann <dh.herrmann@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <linux/uhid.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include "uhid.h"

static int dev_fd = -1;

static unsigned char rdesc[] = {
    0x05, 0x01,     /* USAGE_PAGE (Generic Desktop) */
    0x09, 0x02,     /* USAGE (Mouse) */
    0xa1, 0x01,     /* COLLECTION (Application) */
    0x09, 0x01,             /* USAGE (Pointer) */
    0xa1, 0x00,             /* COLLECTION (Physical) */
    0x05, 0x09,                     /* USAGE_PAGE (Button) */
    0x19, 0x01,                     /* USAGE_MINIMUM (Button 1) */
    0x29, 0x03,                     /* USAGE_MAXIMUM (Button 3) */
    0x15, 0x00,                     /* LOGICAL_MINIMUM (0) */
    0x25, 0x01,                     /* LOGICAL_MAXIMUM (1) */
    0x95, 0x03,                     /* REPORT_COUNT (3) */
    0x75, 0x01,                     /* REPORT_SIZE (1) */
    0x81, 0x02,                     /* INPUT (Data,Var,Abs) */
    0x95, 0x01,                     /* REPORT_COUNT (1) */
    0x75, 0x05,                     /* REPORT_SIZE (5) */
    0x81, 0x01,                     /* INPUT (Cnst,Var,Abs) */
    0x05, 0x01,                     /* USAGE_PAGE (Generic Desktop) */
    0x09, 0x30,                     /* USAGE (X) */
    0x09, 0x31,                     /* USAGE (Y) */
    0x09, 0x38,                     /* USAGE (WHEEL) */
    0x15, 0x81,                     /* LOGICAL_MINIMUM (-127) */
    0x25, 0x7f,                     /* LOGICAL_MAXIMUM (127) */
    0x75, 0x08,                     /* REPORT_SIZE (8) */
    0x95, 0x03,                     /* REPORT_COUNT (3) */
    0x81, 0x06,                     /* INPUT (Data,Var,Rel) */
    0xc0,                   /* END_COLLECTION */
    0xc0,           /* END_COLLECTION */
};

static int uhid_write(int fd, const struct uhid_event *ev) {
    ssize_t ret;

    ret = write(fd, ev, sizeof(*ev));
    if (ret < 0) {
        fprintf(stderr, "Cannot write to uhid: %m\n");
        return -errno;
    } else if (ret != sizeof(*ev)) {
        fprintf(stderr, "Wrong size written to uhid: %ld != %lu\n",
                ret, sizeof(ev));
        return -EFAULT;
    } else {
        return 0;
    }
}

static int create(int fd) {
    struct uhid_event ev;

    memset(&ev, 0, sizeof(ev));
    ev.type = UHID_CREATE;
    strcpy((char*)ev.u.create.name, "demo-sensortag-uhid");
    ev.u.create.rd_data = rdesc;
    ev.u.create.rd_size = sizeof(rdesc);
    ev.u.create.bus = BUS_USB;
    ev.u.create.vendor = 0x15d9;
    ev.u.create.product = 0x0a37;
    ev.u.create.version = 0;
    ev.u.create.country = 0;

    return uhid_write(fd, &ev);
}

static void destroy(int fd) {
    struct uhid_event ev;

    memset(&ev, 0, sizeof(ev));
    ev.type = UHID_DESTROY;

    uhid_write(fd, &ev);
}

static int send_event(int fd, int left_down, int right_down) {
    struct uhid_event ev;

    memset(&ev, 0, sizeof(ev));
    ev.type = UHID_INPUT;
    ev.u.input.size = 4;

    //ev.u.input.data[0] = 0x1;
    if (left_down)
            ev.u.input.data[0] |= 0x1;
    if (right_down)
            ev.u.input.data[0] |= 0x2;

    return uhid_write(fd, &ev);
}

gboolean uhid_event(gboolean left_down, gboolean right_down) {
    if (dev_fd < 0) {
        printf("uhid not initialized\n");
        return FALSE;
    } else {
        printf("event : left:%d right:%d\n", left_down, right_down);
        if (send_event(dev_fd, left_down, right_down)) {
            printf("Cannot send event\n");
            return FALSE;
        }
        return TRUE;
    }
}

gboolean uhid_init() {
    const char *path = "/dev/uhid";
    int fd = fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        printf("Cannot open %s\n", path);
        return FALSE;
    }

    if (create(fd)) {
        printf("Cannot initialize uhid dev\n");
        close(fd);
        return FALSE;
    }

    dev_fd = fd;

    return TRUE;
}

gboolean uhid_cleanup() {
    if (dev_fd >= 0) {
        destroy(dev_fd);
        dev_fd = -1;
    }
    return TRUE;
}
