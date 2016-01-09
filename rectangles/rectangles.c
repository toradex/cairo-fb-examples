/*
 * Demo application drawing rectangles on screen using fbdev
 *
 * This demo shows how to use the fbdev API to sync to vertical blank
 * on Colibri VF50/VF61
 *
 * Copyright (c) 2015, Toradex AG
 *
 * This project is licensed under the terms of the MIT license (see
 * LICENSE)
 */
#include <cairo/cairo.h>
#include <fcntl.h>
#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stropts.h>
#include <sys/mman.h>
#include <tslib.h>
#include <time.h>

static volatile sig_atomic_t cancel = 0;

typedef struct _cairo_linuxfb_device {
	int fb_fd;
	unsigned char *fb_data;
	long fb_screensize;
	struct fb_var_screeninfo fb_vinfo;
	struct fb_fix_screeninfo fb_finfo;
} cairo_linuxfb_device_t;

void signal_handler(int signum)
{
	cancel = 1;
}

/*
 * Flip framebuffer, return the next buffer id which will be used
 */
int flip_buffer(cairo_linuxfb_device_t *device, int vsync, int bufid)
{
	int dummy = 0;

	/* Pan the framebuffer */
	device->fb_vinfo.yoffset = device->fb_vinfo.yres * bufid;
	if (ioctl(device->fb_fd, FBIOPAN_DISPLAY, &device->fb_vinfo)) {
		perror("Error panning display");
		return -1;
	}

	if (vsync) {
		if (ioctl(device->fb_fd, FBIO_WAITFORVSYNC, &dummy)) {
			perror("Error waiting for VSYNC");
			return -1;
		}
	}

	return 0;
}


/* Destroy a cairo surface */
void cairo_linuxfb_surface_destroy(void *device)
{
	cairo_linuxfb_device_t *dev = (cairo_linuxfb_device_t *)device;

	if (dev == NULL)
		return;

	munmap(dev->fb_data, dev->fb_screensize);
	close(dev->fb_fd);
	free(dev);
}

/* Create a cairo surface using the specified framebuffer */
cairo_surface_t *cairo_linuxfb_surface_create(cairo_linuxfb_device_t *device, const char *fb_name)
{
	cairo_surface_t *surface;

	// Open the file for reading and writing
	device->fb_fd = open(fb_name, O_RDWR);
	if (device->fb_fd == -1) {
		perror("Error: cannot open framebuffer device");
		goto handle_allocate_error;
	}

	// Get variable screen information
	if (ioctl(device->fb_fd, FBIOGET_VSCREENINFO, &device->fb_vinfo) == -1) {
		perror("Error: reading variable information");
		goto handle_ioctl_error;
	}

	/* Set virtual display size double the width for double buffering */
	device->fb_vinfo.yoffset = 0;
	device->fb_vinfo.yres_virtual = device->fb_vinfo.yres * 2;
	if (ioctl(device->fb_fd, FBIOPUT_VSCREENINFO, &device->fb_vinfo)) {
		perror("Error setting variable screen info from fb");
		goto handle_ioctl_error;
	}

	// Get fixed screen information
	if (ioctl(device->fb_fd, FBIOGET_FSCREENINFO, &device->fb_finfo) == -1) {
		perror("Error reading fixed information");
		goto handle_ioctl_error;
	}

	// Map the device to memory
	device->fb_data = (unsigned char *)mmap(0, device->fb_finfo.smem_len,
			PROT_READ | PROT_WRITE, MAP_SHARED,
			device->fb_fd, 0);
	if ((int)device->fb_data == -1) {
		perror("Error: failed to map framebuffer device to memory");
		goto handle_ioctl_error;
	}


	/* Create the cairo surface which will be used to draw to */
	surface = cairo_image_surface_create_for_data(device->fb_data,
			CAIRO_FORMAT_RGB16_565,
			device->fb_vinfo.xres,
			device->fb_vinfo.yres_virtual,
			cairo_format_stride_for_width(CAIRO_FORMAT_RGB16_565,
				device->fb_vinfo.xres));
	cairo_surface_set_user_data(surface, NULL, device,
			&cairo_linuxfb_surface_destroy);

	return surface;

handle_ioctl_error:
	close(device->fb_fd);
handle_allocate_error:
	free(device);
	exit(1);
}

void draw_rectangles(cairo_t *fbcr, struct tsdev *ts, cairo_linuxfb_device_t *device)
{
	int bufid = 1; /* start drawing into second buffer */
	float r, g, b;
	int fbsizex = device->fb_vinfo.xres;
	int fbsizey = device->fb_vinfo.yres;
	int startx, starty, sizex, sizey;
	struct ts_sample sample;
	cairo_surface_t *surface;
	cairo_t *cr;
	float scale = 1.0f;

	surface = cairo_image_surface_create(CAIRO_FORMAT_RGB16_565,
			fbsizex, fbsizey);
	cr = cairo_create(surface);

	/* 
	 * We clear the cairo surface here before drawing
	 * This is required in case something was drawn on this surface
	 * previously, the previous contents would not be cleared without this.
	 */
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);

	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	srand(time(NULL));

	while (!cancel) {
		r = (rand() % 100) / 100.0;
		g = (rand() % 100) / 100.0;
		b = (rand() % 100) / 100.0;
		startx = rand() % fbsizex;
		starty = rand() % fbsizey;
		sizex = rand() % (fbsizex - startx);
		sizey = rand() % (fbsizey - starty);

		cairo_identity_matrix(cr);
		if (ts) {
			int pressed = 0;

			/* Pressure is our identication whether we act on axis... */
			while (ts_read(ts, &sample, 1)) {
				if (sample.pressure > 0)
					pressed = 1;
			}

			if (pressed) {
				scale *= 1.05f;
				cairo_translate(cr, sample.x, sample.y);
				cairo_scale(cr, scale, scale);
				//r = g = b = 0;
				startx = -5;
				starty = -5;
				sizex = 10;
				sizey = 10;
			} else {
				scale = 1.0f;
			}
		}

		cairo_set_source_rgb(cr, r, g, b);
		cairo_rectangle(cr, startx, starty, sizex, sizey);
		cairo_stroke_preserve(cr);
		cairo_fill(cr);

		/* Draw to framebuffer at y offset according to current buffer.. */
		cairo_set_source_surface(fbcr, surface, 0, bufid * fbsizey);
		cairo_paint(fbcr);
		flip_buffer(device, 1, bufid);

		/* Switch buffer ID for next draw */
		bufid = !bufid;
		usleep(20000);
	}

	/* Make sure we leave with buffer 0 enabled */
	flip_buffer(device, 1, 0);

	/* Destroy and release all cairo related contexts */
	cairo_destroy(cr);
	cairo_surface_destroy(surface);
}

int main(int argc, char *argv[]) {
	char fb_node[16] = "/dev/fb0";
	char *tsdevice = NULL;
	struct tsdev *ts;
	struct sigaction action;
	cairo_linuxfb_device_t *device;
	cairo_surface_t *fbsurface;
	cairo_t *fbcr;

	if (argc > 1) {
		strcpy(fb_node, argv[1]);
	}

	printf("Frame buffer node is: %s\n", fb_node);

	if( (tsdevice = getenv("TSLIB_TSDEVICE")) != NULL )
		ts = ts_open(tsdevice, 1);
	else
		ts = ts_open("/dev/input/event0", 1);

	if (ts_config(ts)) {
		perror("ts_config");
		exit(1);
	}

	device = malloc(sizeof(*device));
	if (!device) {
		perror("Error: cannot allocate memory\n");
		exit(1);
	}

	memset(&action, 0, sizeof(struct sigaction));
	action.sa_handler = signal_handler;
	sigaction(SIGTERM, &action, NULL);
	sigaction(SIGINT, &action, NULL);

	fbsurface = cairo_linuxfb_surface_create(device, fb_node);
	fbcr = cairo_create(fbsurface);

	draw_rectangles(fbcr, ts, device);

	cairo_destroy(fbcr);
	cairo_surface_destroy(fbsurface);

	return 0;
}
