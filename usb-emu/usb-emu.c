// SPDX-License-Identifier: MIT
/*
 * usb-emu: emulate a USB HID tablet with Linux raw-gadget + dummy_hcd.
 *
 * The emulated device is enumerated by the host side of dummy_hcd and bound
 * by usbhid, so USB-only paths (string descriptors read by hid-uclogic or by
 * HID-BPF programs) can be tested without hardware.
 *
 * Usage: usb-emu [-v] <description file> [script file|-]
 *
 * See README.md for the description and script formats.
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <linux/types.h>
#include <linux/usb/ch9.h>
#include <linux/hid.h>

/* Copy of the raw-gadget UAPI so we don't depend on installed headers. */
#define UDC_NAME_LENGTH_MAX 128
struct usb_raw_init {
	__u8 driver_name[UDC_NAME_LENGTH_MAX];
	__u8 device_name[UDC_NAME_LENGTH_MAX];
	__u8 speed;
};
enum usb_raw_event_type {
	USB_RAW_EVENT_INVALID = 0,
	USB_RAW_EVENT_CONNECT = 1,
	USB_RAW_EVENT_CONTROL = 2,
	USB_RAW_EVENT_SUSPEND = 3,
	USB_RAW_EVENT_RESUME = 4,
	USB_RAW_EVENT_RESET = 5,
	USB_RAW_EVENT_DISCONNECT = 6,
};
struct usb_raw_event {
	__u32 type;
	__u32 length;
	__u8 data[];
};
struct usb_raw_ep_io {
	__u16 ep;
	__u16 flags;
	__u32 length;
	__u8 data[];
};
#define USB_RAW_EPS_NUM_MAX 30
#define USB_RAW_EP_NAME_MAX 16
#define USB_RAW_EP_ADDR_ANY 0xff
struct usb_raw_ep_caps {
	__u32 type_control : 1;
	__u32 type_iso : 1;
	__u32 type_bulk : 1;
	__u32 type_int : 1;
	__u32 dir_in : 1;
	__u32 dir_out : 1;
};
struct usb_raw_ep_limits {
	__u16 maxpacket_limit;
	__u16 max_streams;
	__u32 reserved;
};
struct usb_raw_ep_info {
	__u8 name[USB_RAW_EP_NAME_MAX];
	__u32 addr;
	struct usb_raw_ep_caps caps;
	struct usb_raw_ep_limits limits;
};
struct usb_raw_eps_info {
	struct usb_raw_ep_info eps[USB_RAW_EPS_NUM_MAX];
};
#define USB_RAW_IOCTL_INIT _IOW('U', 0, struct usb_raw_init)
#define USB_RAW_IOCTL_RUN _IO('U', 1)
#define USB_RAW_IOCTL_EVENT_FETCH _IOR('U', 2, struct usb_raw_event)
#define USB_RAW_IOCTL_EP0_WRITE _IOW('U', 3, struct usb_raw_ep_io)
#define USB_RAW_IOCTL_EP0_READ _IOWR('U', 4, struct usb_raw_ep_io)
#define USB_RAW_IOCTL_EP_ENABLE _IOW('U', 5, struct usb_endpoint_descriptor)
#define USB_RAW_IOCTL_EP_WRITE _IOW('U', 7, struct usb_raw_ep_io)
#define USB_RAW_IOCTL_CONFIGURE _IO('U', 9)
#define USB_RAW_IOCTL_VBUS_DRAW _IOW('U', 10, __u32)
#define USB_RAW_IOCTL_EPS_INFO _IOR('U', 11, struct usb_raw_eps_info)
#define USB_RAW_IOCTL_EP0_STALL _IO('U', 12)

#define MAX_IFACES 8
#define MAX_STRINGS 64
#define MAX_RDESC 4096
#define MAX_REPORT 1024

struct string_entry {
	int index;
	__u8 data[256]; /* full descriptor incl. bLength/bDescriptorType */
	int len;
	char mode[32]; /* switch to this mode when read, "" if none */
};

struct iface {
	int number;
	int cls, subclass, protocol;
	int ep_in, ep_out; /* addresses, ep_out 0 if none */
	int maxpacket, interval;
	__u8 rdesc[MAX_RDESC];
	int rdesc_len;
	int ep_handle; /* raw-gadget handle of the IN endpoint, -1 if disabled */
};

static struct {
	int vid, pid, bcd_device, bcd_usb, attrs, power_ma;
	struct string_entry strings[MAX_STRINGS];
	int nstrings;
	struct iface ifaces[MAX_IFACES];
	int nifaces;
} dev = {
	.bcd_usb = 0x0200,
	.bcd_device = 0x0100,
	.attrs = 0xa0,
	.power_ma = 100,
};

static int fd = -1;
static bool verbose;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static bool configured;
static char mode[32] = "default";

static void logmsg(const char *fmt, ...)
{
	struct timespec ts;
	char buf[1024];
	va_list ap;
	int n;

	/* Format the whole line first: the ep0 and replay threads both log. */
	clock_gettime(CLOCK_MONOTONIC, &ts);
	n = snprintf(buf, sizeof(buf), "usb-emu [%5ld.%03ld] ", (long)ts.tv_sec,
		     ts.tv_nsec / 1000000);
	va_start(ap, fmt);
	n += vsnprintf(buf + n, sizeof(buf) - n - 1, fmt, ap);
	va_end(ap);
	if (n > (int)sizeof(buf) - 2)
		n = sizeof(buf) - 2;
	buf[n++] = '\n';
	if (write(STDERR_FILENO, buf, n) < 0) {
		/* nothing sensible to do */
	}
}

static void die(const char *fmt, ...)
{
	va_list ap;

	fputs("usb-emu: ", stderr);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(1);
}

/* ---------------------------------------------------------------- parsing */

static int parse_hex_bytes(const char *s, __u8 *out, int max)
{
	int n = 0;

	while (*s) {
		while (*s && (isspace((unsigned char)*s) || *s == ','))
			s++;
		if (!*s || *s == '#')
			break;
		if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
			s += 2;
		if (!isxdigit((unsigned char)s[0]) ||
		    !isxdigit((unsigned char)s[1]))
			die("bad hex byte near '%s'", s);
		if (n >= max)
			die("too many bytes");
		char b[3] = { s[0], s[1], 0 };
		out[n++] = strtoul(b, NULL, 16);
		s += 2;
	}
	return n;
}

static long kv(const char *line, const char *key, long def)
{
	char pat[64];
	const char *p;

	snprintf(pat, sizeof(pat), "%s=", key);
	p = line;
	while ((p = strstr(p, pat))) {
		if (p == line || isspace((unsigned char)p[-1]))
			return strtol(p + strlen(pat), NULL, 0);
		p++;
	}
	return def;
}

static struct string_entry *string_get(int index, bool create)
{
	for (int i = 0; i < dev.nstrings; i++)
		if (dev.strings[i].index == index)
			return &dev.strings[i];
	if (!create)
		return NULL;
	if (dev.nstrings >= MAX_STRINGS)
		die("too many strings");
	struct string_entry *s = &dev.strings[dev.nstrings++];

	memset(s, 0, sizeof(*s));
	s->index = index;
	return s;
}

static struct iface *iface_get(int number)
{
	for (int i = 0; i < dev.nifaces; i++)
		if (dev.ifaces[i].number == number)
			return &dev.ifaces[i];
	return NULL;
}

static void parse_description(const char *path)
{
	FILE *f = fopen(path, "r");
	char line[8192];
	int lineno = 0;

	if (!f)
		die("cannot open %s: %m", path);

	while (fgets(line, sizeof(line), f)) {
		char *p = line, *word;
		int idx;

		lineno++;
		while (isspace((unsigned char)*p))
			p++;
		if (!*p || *p == '#')
			continue;
		p[strcspn(p, "\n")] = 0;
		word = p;
		p += strcspn(p, " \t");
		if (*p)
			*p++ = 0;
		while (isspace((unsigned char)*p))
			p++;

		if (!strcmp(word, "device")) {
			dev.vid = kv(p, "vid", 0);
			dev.pid = kv(p, "pid", 0);
			dev.bcd_device = kv(p, "bcd", dev.bcd_device);
			dev.bcd_usb = kv(p, "usb", dev.bcd_usb);
			dev.attrs = kv(p, "attrs", dev.attrs);
			dev.power_ma = kv(p, "power", dev.power_ma);
		} else if (!strcmp(word, "string")) {
			/* string <index> "text" */
			idx = strtol(p, &p, 0);
			char *q1 = strchr(p, '"'), *q2 = q1 ? strrchr(q1 + 1, '"') : NULL;

			if (!q1 || !q2)
				die("%s:%d: string needs a quoted value", path, lineno);
			struct string_entry *s = string_get(idx, true);
			int n = 0;

			for (char *c = q1 + 1; c < q2 && n < 126; c++, n++) {
				s->data[2 + 2 * n] = (__u8)*c;
				s->data[3 + 2 * n] = 0;
			}
			s->len = 2 + 2 * n;
			s->data[0] = s->len;
			s->data[1] = USB_DT_STRING;
		} else if (!strcmp(word, "string-raw")) {
			/* string-raw <index> <hex bytes, sent verbatim> */
			idx = strtol(p, &p, 0);
			struct string_entry *s = string_get(idx, true);

			s->len = parse_hex_bytes(p, s->data, sizeof(s->data));
		} else if (!strcmp(word, "on-read")) {
			/* on-read <index> mode <name> */
			char name[32];

			if (sscanf(p, "%i mode %31s", &idx, name) != 2)
				die("%s:%d: expected 'on-read <index> mode <name>'",
				    path, lineno);
			struct string_entry *s = string_get(idx, false);

			if (!s)
				die("%s:%d: string %d not defined", path, lineno, idx);
			snprintf(s->mode, sizeof(s->mode), "%s", name);
		} else if (!strcmp(word, "interface")) {
			if (dev.nifaces >= MAX_IFACES)
				die("too many interfaces");
			struct iface *i = &dev.ifaces[dev.nifaces++];

			memset(i, 0, sizeof(*i));
			i->number = strtol(p, NULL, 0);
			i->cls = kv(p, "class", 3);
			i->subclass = kv(p, "subclass", 0);
			i->protocol = kv(p, "protocol", 0);
			i->ep_in = kv(p, "ep", 0x81 + i->number);
			i->ep_out = kv(p, "out", 0);
			i->maxpacket = kv(p, "maxpacket", 64);
			i->interval = kv(p, "interval", 1);
			i->ep_handle = -1;
		} else if (!strcmp(word, "rdesc")) {
			/* rdesc <iface> <hex...>  (may be repeated, appends) */
			idx = strtol(p, &p, 0);
			struct iface *i = iface_get(idx);

			if (!i)
				die("%s:%d: interface %d not defined", path, lineno, idx);
			i->rdesc_len += parse_hex_bytes(p, i->rdesc + i->rdesc_len,
							MAX_RDESC - i->rdesc_len);
		} else {
			die("%s:%d: unknown keyword '%s'", path, lineno, word);
		}
	}
	fclose(f);
	if (!dev.vid || !dev.nifaces)
		die("%s: need a 'device' line and at least one interface", path);
}

/* ------------------------------------------------------------ descriptors */

static int build_device_desc(__u8 *b)
{
	struct usb_device_descriptor d = {
		.bLength = USB_DT_DEVICE_SIZE,
		.bDescriptorType = USB_DT_DEVICE,
		.bcdUSB = __cpu_to_le16(dev.bcd_usb),
		/* dummy_hcd's ep0 is 64 bytes, whatever the real device says */
		.bMaxPacketSize0 = 64,
		.idVendor = __cpu_to_le16(dev.vid),
		.idProduct = __cpu_to_le16(dev.pid),
		.bcdDevice = __cpu_to_le16(dev.bcd_device),
		.iManufacturer = string_get(1, false) ? 1 : 0,
		.iProduct = string_get(2, false) ? 2 : 0,
		.iSerialNumber = string_get(3, false) ? 3 : 0,
		.bNumConfigurations = 1,
	};

	memcpy(b, &d, sizeof(d));
	return sizeof(d);
}

static int build_config_desc(__u8 *b)
{
	int n = USB_DT_CONFIG_SIZE;

	for (int k = 0; k < dev.nifaces; k++) {
		struct iface *i = &dev.ifaces[k];
		__u8 *p = b + n;

		/* interface */
		p[0] = USB_DT_INTERFACE_SIZE; p[1] = USB_DT_INTERFACE;
		p[2] = i->number; p[3] = 0; p[4] = i->ep_out ? 2 : 1;
		p[5] = i->cls; p[6] = i->subclass; p[7] = i->protocol; p[8] = 0;
		p += 9;
		/* HID descriptor */
		p[0] = 9; p[1] = HID_DT_HID; p[2] = 0x11; p[3] = 0x01; p[4] = 0;
		p[5] = 1; p[6] = HID_DT_REPORT;
		p[7] = i->rdesc_len & 0xff; p[8] = i->rdesc_len >> 8;
		p += 9;
		/* endpoints */
		p[0] = USB_DT_ENDPOINT_SIZE; p[1] = USB_DT_ENDPOINT;
		p[2] = i->ep_in; p[3] = USB_ENDPOINT_XFER_INT;
		p[4] = i->maxpacket & 0xff; p[5] = i->maxpacket >> 8;
		p[6] = i->interval;
		p += 7;
		if (i->ep_out) {
			p[0] = USB_DT_ENDPOINT_SIZE; p[1] = USB_DT_ENDPOINT;
			p[2] = i->ep_out; p[3] = USB_ENDPOINT_XFER_INT;
			p[4] = i->maxpacket & 0xff; p[5] = i->maxpacket >> 8;
			p[6] = i->interval;
			p += 7;
		}
		n = p - b;
	}

	struct usb_config_descriptor c = {
		.bLength = USB_DT_CONFIG_SIZE,
		.bDescriptorType = USB_DT_CONFIG,
		.wTotalLength = __cpu_to_le16(n),
		.bNumInterfaces = dev.nifaces,
		.bConfigurationValue = 1,
		.iConfiguration = 0,
		.bmAttributes = dev.attrs,
		.bMaxPower = dev.power_ma / 2,
	};

	memcpy(b, &c, sizeof(c));
	return n;
}

/* -------------------------------------------------------------- raw-gadget */

struct ep0_buf {
	struct usb_raw_ep_io io;
	__u8 data[MAX_RDESC + 64];
};

static void ep0_write(const __u8 *data, int len)
{
	struct ep0_buf b = { 0 };

	b.io.length = len;
	memcpy(b.io.data, data, len);
	if (ioctl(fd, USB_RAW_IOCTL_EP0_WRITE, &b) < 0)
		logmsg("ep0 write failed: %m");
}

static int ep0_read(__u8 *data, int len)
{
	struct ep0_buf b = { 0 };
	int rc;

	b.io.length = len;
	rc = ioctl(fd, USB_RAW_IOCTL_EP0_READ, &b);
	if (rc < 0) {
		logmsg("ep0 read failed: %m");
		return -1;
	}
	if (data && rc > 0)
		memcpy(data, b.io.data, rc);
	return rc;
}

static void ep0_stall(void)
{
	if (ioctl(fd, USB_RAW_IOCTL_EP0_STALL, 0) < 0)
		logmsg("ep0 stall failed: %m");
}

static void enable_endpoints(void)
{
	struct usb_raw_eps_info info;
	int neps;

	memset(&info, 0, sizeof(info));
	neps = ioctl(fd, USB_RAW_IOCTL_EPS_INFO, &info);
	if (neps < 0)
		die("EPS_INFO: %m");

	for (int k = 0; k < dev.nifaces; k++) {
		struct iface *i = &dev.ifaces[k];
		struct usb_endpoint_descriptor d = {
			.bLength = USB_DT_ENDPOINT_SIZE,
			.bDescriptorType = USB_DT_ENDPOINT,
			.bEndpointAddress = i->ep_in,
			.bmAttributes = USB_ENDPOINT_XFER_INT,
			.wMaxPacketSize = __cpu_to_le16(i->maxpacket),
			.bInterval = i->interval,
		};

		if (i->ep_handle >= 0)
			continue;
		i->ep_handle = ioctl(fd, USB_RAW_IOCTL_EP_ENABLE, &d);
		if (i->ep_handle < 0)
			logmsg("interface %d: enabling IN endpoint 0x%02x failed: %m",
			       i->number, i->ep_in);
		else if (verbose)
			logmsg("interface %d: IN endpoint 0x%02x -> handle %d",
			       i->number, i->ep_in, i->ep_handle);
	}
}

static const char *req_name(const struct usb_ctrlrequest *c)
{
	int type = c->bRequestType & USB_TYPE_MASK;

	if (type == USB_TYPE_STANDARD) {
		switch (c->bRequest) {
		case USB_REQ_GET_STATUS: return "GET_STATUS";
		case USB_REQ_GET_DESCRIPTOR: return "GET_DESCRIPTOR";
		case USB_REQ_SET_CONFIGURATION: return "SET_CONFIGURATION";
		case USB_REQ_GET_CONFIGURATION: return "GET_CONFIGURATION";
		case USB_REQ_SET_INTERFACE: return "SET_INTERFACE";
		case USB_REQ_SET_FEATURE: return "SET_FEATURE";
		case USB_REQ_CLEAR_FEATURE: return "CLEAR_FEATURE";
		}
	} else if (type == USB_TYPE_CLASS) {
		switch (c->bRequest) {
		case HID_REQ_GET_REPORT: return "HID_GET_REPORT";
		case HID_REQ_GET_IDLE: return "HID_GET_IDLE";
		case HID_REQ_GET_PROTOCOL: return "HID_GET_PROTOCOL";
		case HID_REQ_SET_REPORT: return "HID_SET_REPORT";
		case HID_REQ_SET_IDLE: return "HID_SET_IDLE";
		case HID_REQ_SET_PROTOCOL: return "HID_SET_PROTOCOL";
		}
	}
	return "UNKNOWN";
}

static void set_mode(const char *m)
{
	pthread_mutex_lock(&lock);
	if (strcmp(mode, m)) {
		snprintf(mode, sizeof(mode), "%s", m);
		logmsg("MODE %s", mode);
	}
	pthread_cond_broadcast(&cond);
	pthread_mutex_unlock(&lock);
}

static void handle_control(const struct usb_ctrlrequest *c)
{
	__u16 value = __le16_to_cpu(c->wValue);
	__u16 index = __le16_to_cpu(c->wIndex);
	__u16 length = __le16_to_cpu(c->wLength);
	int type = c->bRequestType & USB_TYPE_MASK;
	int recip = c->bRequestType & USB_RECIP_MASK;
	bool in = c->bRequestType & USB_DIR_IN;
	__u8 buf[MAX_RDESC + 64];
	int len = -1; /* -1: stall */
	char detail[128] = "";

	if (type == USB_TYPE_STANDARD && c->bRequest == USB_REQ_GET_DESCRIPTOR) {
		int dtype = value >> 8, didx = value & 0xff;

		if (recip == USB_RECIP_DEVICE && dtype == USB_DT_DEVICE) {
			len = build_device_desc(buf);
			snprintf(detail, sizeof(detail), "DEVICE");
		} else if (recip == USB_RECIP_DEVICE && dtype == USB_DT_CONFIG) {
			len = build_config_desc(buf);
			snprintf(detail, sizeof(detail), "CONFIG");
		} else if (recip == USB_RECIP_DEVICE && dtype == USB_DT_STRING) {
			if (didx == 0) {
				static const __u8 langs[] = { 4, USB_DT_STRING, 0x09, 0x04 };

				memcpy(buf, langs, sizeof(langs));
				len = sizeof(langs);
				snprintf(detail, sizeof(detail), "STRING index=0 (language table)");
			} else {
				struct string_entry *s = string_get(didx, false);

				if (s) {
					memcpy(buf, s->data, s->len);
					len = s->len;
				}
				snprintf(detail, sizeof(detail),
					 "STRING index=%d (0x%02x) langid=0x%04x%s",
					 didx, didx, index, s ? "" : " (undefined)");
				if (s && s->mode[0])
					set_mode(s->mode);
			}
		} else if (recip == USB_RECIP_INTERFACE &&
			   (dtype == HID_DT_REPORT || dtype == HID_DT_HID)) {
			struct iface *i = iface_get(index);

			if (i && dtype == HID_DT_REPORT) {
				memcpy(buf, i->rdesc, i->rdesc_len);
				len = i->rdesc_len;
			}
			snprintf(detail, sizeof(detail), "%s interface=%d",
				 dtype == HID_DT_REPORT ? "HID_REPORT" : "HID", index);
		} else {
			snprintf(detail, sizeof(detail), "type=0x%02x index=%d", dtype, didx);
		}
	} else if (type == USB_TYPE_STANDARD) {
		switch (c->bRequest) {
		case USB_REQ_SET_CONFIGURATION:
			if (value) {
				enable_endpoints();
				if (ioctl(fd, USB_RAW_IOCTL_VBUS_DRAW, dev.power_ma / 2) < 0)
					logmsg("VBUS_DRAW: %m");
				if (ioctl(fd, USB_RAW_IOCTL_CONFIGURE, 0) < 0)
					logmsg("CONFIGURE: %m");
				pthread_mutex_lock(&lock);
				configured = true;
				pthread_cond_broadcast(&cond);
				pthread_mutex_unlock(&lock);
			}
			len = 0;
			snprintf(detail, sizeof(detail), "value=%d", value);
			break;
		case USB_REQ_GET_CONFIGURATION:
			buf[0] = configured ? 1 : 0;
			len = 1;
			break;
		case USB_REQ_GET_STATUS:
			buf[0] = buf[1] = 0;
			len = 2;
			break;
		case USB_REQ_SET_INTERFACE:
		case USB_REQ_SET_FEATURE:
		case USB_REQ_CLEAR_FEATURE:
			len = 0;
			break;
		}
	} else if (type == USB_TYPE_CLASS && recip == USB_RECIP_INTERFACE) {
		switch (c->bRequest) {
		case HID_REQ_SET_IDLE:
		case HID_REQ_SET_PROTOCOL:
			len = 0;
			break;
		case HID_REQ_GET_IDLE:
		case HID_REQ_GET_PROTOCOL:
			buf[0] = c->bRequest == HID_REQ_GET_PROTOCOL ? 1 : 0;
			len = 1;
			break;
		case HID_REQ_SET_REPORT:
			len = 0; /* data stage read below */
			break;
		case HID_REQ_GET_REPORT:
		default:
			break; /* stall */
		}
		snprintf(detail, sizeof(detail), "interface=%d report=0x%04x", index, value);
	}

	logmsg("CTRL %s %s bRequestType=0x%02x bRequest=0x%02x wValue=0x%04x wIndex=0x%04x wLength=%u %s%s",
	       req_name(c), in ? "IN" : "OUT", c->bRequestType, c->bRequest,
	       value, index, length, detail,
	       len < 0 ? " -> STALL" : "");

	if (len < 0) {
		ep0_stall();
		return;
	}

	if (in) {
		if (len > length)
			len = length;
		ep0_write(buf, len);
		if (verbose)
			logmsg("     -> %d bytes", len);
	} else {
		/* OUT: read the data stage (or ack with a zero-length read) */
		int rc = ep0_read(buf, length);

		if (length && rc > 0) {
			char hex[3 * 64 + 1] = "";

			for (int k = 0; k < rc && k < 64; k++)
				sprintf(hex + 3 * k, "%02x ", buf[k]);
			logmsg("     data: %s", hex);
		}
	}
}

static void *ep0_loop(void *arg)
{
	struct {
		struct usb_raw_event ev;
		struct usb_ctrlrequest ctrl;
		__u8 pad[64];
	} e;

	(void)arg;
	for (;;) {
		memset(&e, 0, sizeof(e));
		e.ev.length = sizeof(e.ctrl) + sizeof(e.pad);
		if (ioctl(fd, USB_RAW_IOCTL_EVENT_FETCH, &e) < 0)
			die("EVENT_FETCH: %m");

		switch (e.ev.type) {
		case USB_RAW_EVENT_CONNECT:
			logmsg("EVENT connect");
			break;
		case USB_RAW_EVENT_CONTROL:
			handle_control(&e.ctrl);
			break;
		case USB_RAW_EVENT_RESET:
			logmsg("EVENT reset");
			break;
		case USB_RAW_EVENT_DISCONNECT:
			logmsg("EVENT disconnect");
			break;
		case USB_RAW_EVENT_SUSPEND:
			logmsg("EVENT suspend");
			break;
		case USB_RAW_EVENT_RESUME:
			logmsg("EVENT resume");
			break;
		default:
			logmsg("EVENT type %u", e.ev.type);
			break;
		}
	}
	return NULL;
}

/* ----------------------------------------------------------------- replay */

static bool wait_for(bool (*pred)(void), int timeout_ms)
{
	struct timespec ts;
	bool ok;

	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += timeout_ms / 1000;
	ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
	if (ts.tv_nsec >= 1000000000L) {
		ts.tv_sec++;
		ts.tv_nsec -= 1000000000L;
	}
	pthread_mutex_lock(&lock);
	while (!(ok = pred()))
		if (pthread_cond_timedwait(&cond, &lock, &ts) == ETIMEDOUT) {
			ok = pred();
			break;
		}
	pthread_mutex_unlock(&lock);
	return ok;
}

static bool is_configured(void)
{
	return configured;
}

static char wanted_mode[32];
static bool mode_reached(void)
{
	return !strcmp(mode, wanted_mode);
}

static void send_report(int ifnum, const __u8 *data, int len)
{
	struct iface *i = iface_get(ifnum);
	struct {
		struct usb_raw_ep_io io;
		__u8 data[MAX_REPORT];
	} b = { 0 };

	if (!i || i->ep_handle < 0) {
		logmsg("SEND interface %d: no enabled endpoint, dropped", ifnum);
		return;
	}
	b.io.ep = i->ep_handle;
	b.io.length = len;
	memcpy(b.io.data, data, len);
	/* Blocks until the host polls the endpoint, i.e. until the HID
	 * device has been opened (e.g. its event node is open). */
	if (ioctl(fd, USB_RAW_IOCTL_EP_WRITE, &b) < 0)
		logmsg("SEND interface %d failed: %m", ifnum);
	else if (verbose)
		logmsg("SEND interface %d: %d bytes", ifnum, len);
}

static void run_script(FILE *f)
{
	char line[4096];
	__u8 data[MAX_REPORT];

	if (!wait_for(is_configured, 30000))
		die("host did not configure the device within 30 s");
	logmsg("SCRIPT start (mode %s)", mode);

	while (fgets(line, sizeof(line), f)) {
		char *p = line;
		int n;

		while (isspace((unsigned char)*p))
			p++;
		if (!*p || *p == '#')
			continue;
		p[strcspn(p, "\n")] = 0;

		if (!strncmp(p, "sleep", 5)) {
			usleep(atoi(p + 5) * 1000);
		} else if (!strncmp(p, "wait-mode", 9)) {
			int timeout = 30000;

			if (sscanf(p + 9, "%31s %d", wanted_mode, &timeout) < 1)
				die("wait-mode needs a mode name");
			if (!wait_for(mode_reached, timeout))
				die("timed out waiting for mode '%s' (still '%s')",
				    wanted_mode, mode);
			logmsg("SCRIPT reached mode %s", wanted_mode);
		} else if (!strncmp(p, "wait-file", 9)) {
			char path[256];
			int timeout = 30000;

			if (sscanf(p + 9, "%255s %d", path, &timeout) < 1)
				die("wait-file needs a path");
			while (access(path, F_OK) != 0) {
				if (timeout <= 0)
					die("timed out waiting for %s", path);
				usleep(50000);
				timeout -= 50;
			}
			logmsg("SCRIPT found %s", path);
		} else if (!strncmp(p, "log", 3)) {
			logmsg("SCRIPT %s", p + 4);
		} else if (!strncmp(p, "send", 4)) {
			int ifnum = strtol(p + 4, &p, 0);

			n = parse_hex_bytes(p, data, sizeof(data));
			send_report(ifnum, data, n);
		} else {
			die("script: unknown command '%s'", p);
		}
	}
	logmsg("SCRIPT done");
}

int main(int argc, char **argv)
{
	struct usb_raw_init init = { 0 };
	pthread_t thread;
	int opt;

	while ((opt = getopt(argc, argv, "v")) != -1) {
		if (opt == 'v')
			verbose = true;
		else
			die("usage: usb-emu [-v] <description> [script|-]");
	}
	if (optind >= argc)
		die("usage: usb-emu [-v] <description> [script|-]");

	parse_description(argv[optind]);

	fd = open("/dev/raw-gadget", O_RDWR);
	if (fd < 0)
		die("cannot open /dev/raw-gadget: %m (CONFIG_USB_RAW_GADGET?)");

	snprintf((char *)init.driver_name, sizeof(init.driver_name), "dummy_udc");
	snprintf((char *)init.device_name, sizeof(init.device_name), "dummy_udc.0");
	init.speed = USB_SPEED_HIGH;
	if (ioctl(fd, USB_RAW_IOCTL_INIT, &init) < 0)
		die("INIT: %m (CONFIG_USB_DUMMY_HCD?)");
	if (ioctl(fd, USB_RAW_IOCTL_RUN, 0) < 0)
		die("RUN: %m");
	logmsg("emulating %04x:%04x with %d interface(s)", dev.vid, dev.pid,
	       dev.nifaces);

	if (pthread_create(&thread, NULL, ep0_loop, NULL))
		die("pthread_create failed");

	if (optind + 1 < argc) {
		const char *path = argv[optind + 1];
		FILE *f = strcmp(path, "-") ? fopen(path, "r") : stdin;

		if (!f)
			die("cannot open %s: %m", path);
		run_script(f);
	}

	/* Stay connected until killed. */
	pthread_join(thread, NULL);
	return 0;
}
