/*
 * Demo application drawing rectangles screen using DRM/KMS
 *
 * This demo shows how to use the DRM/KMS API to draw to the screen
 * using double buffering with page flip which is synced to vertical
 * blanking period. Tested on Colibri VF50/VF61
 *
 * Copyright (c) 2015, Toradex AG
 * Copyright (c) 2012, Wayne Wolf (kms-pageflip.c)
 *
 * This project is licensed under the terms of the MIT license (see
 * LICENSE)
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm/drm_fourcc.h>
#include <libkms.h>
#include <cairo.h>

static volatile sig_atomic_t cancel = 0;

struct buffer {
	int handle;
	int size;
	int pitch;

	void *ptr;
	cairo_surface_t *buf_surface;
	cairo_t *buf_ctx;

	int fb_id;
};

struct flip_context {
	struct buffer *buffers[2];
	struct buffer *current_buffer;
	cairo_surface_t *surface;
	cairo_t *ctx;
	int width;
	int height;
	int crtc_id;
	struct timeval start;
	int swap_count;
};

void signal_handler(int signum)
{
	cancel = 1;
}

static int create_bo(int fd, struct buffer *buf, int w, int h, int bpp)
{
	struct drm_mode_create_dumb argc;
	struct drm_mode_map_dumb argm;
	int ret;
	void *map;

	/* use DRM dumb buffers */
	memset(&argc, 0, sizeof(argc));
	argc.bpp = bpp;
	argc.width = w;
	argc.height = h;
	ret = drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &argc);
	if (ret) {
		fprintf(stderr, "failed to create dumb buffer: %s\n",
				strerror(errno));
		return ret;
	}

	buf->handle = argc.handle;
	buf->size = argc.size;
	buf->pitch = argc.pitch;

	/* map the bo to user space buffer */
	memset(&argm, 0, sizeof(argm));
	argm.handle = buf->handle;
	ret = drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &argm);
	if (ret)
		return ret;

	map = mmap(NULL, buf->size, PROT_READ | PROT_WRITE, MAP_SHARED,
			fd, argm.offset);
	if (map == MAP_FAILED)
		return -EINVAL;

	buf->ptr = map;

	/* create a cairo surface with the right format */
	buf->buf_surface = cairo_image_surface_create_for_data(buf->ptr,
		CAIRO_FORMAT_ARGB32, w, h, buf->pitch);
	buf->buf_ctx = cairo_create(buf->buf_surface);

	return 0;
}

void draw_overlay(struct buffer *buf)
{
	cairo_surface_t *image;
	cairo_t *cr = buf->buf_ctx;
/*
	cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
	cairo_rectangle(cr, 0, 0, 320, 240);
	cairo_stroke_preserve(cr);
	cairo_fill(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
*/
	/*
	cairo_set_source_rgba(cr, 0.0, 0.321, 0.533, 0.8);
	cairo_rectangle(cr, 0, 0, 320, 120);
	cairo_fill(cr);
	cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(cr, 32.0);
	cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.8);
	cairo_move_to(cr, 4, 36);
	cairo_show_text(cr, "Toradex");
	*/
	image = cairo_image_surface_create_from_png ("toradex.png");
	cairo_set_source_surface (cr, image, 0, 0);
	cairo_paint(cr);
	cairo_surface_destroy(image);
}

void page_flip_handler(int fd, unsigned int frame,
		  unsigned int sec, unsigned int usec, void *data)
{
	struct flip_context *context;
	struct buffer *next_buffer;
	struct timeval end;
	double t;
	float r, g, b;
	int startx, starty, sizex, sizey;
	cairo_t *cr;

	context = data;
	if (context->current_buffer == context->buffers[0])
		next_buffer = context->buffers[1];
	else
		next_buffer = context->buffers[0];

	/* Draw a new rectangle */
	r = (rand() % 100) / 100.0;
	g = (rand() % 100) / 100.0;
	b = (rand() % 100) / 100.0;
	startx = rand() % context->width;
	starty = rand() % context->height;
	sizex = rand() % (context->width - startx);
	sizey = rand() % (context->height - starty);

	cr = context->ctx;
	cairo_set_source_rgb(cr, r, g, b);
	cairo_rectangle(cr, startx, starty, sizex, sizey);
	cairo_stroke_preserve(cr);
	cairo_fill(cr);

	/* Draw to next buffer */
	cairo_set_source_surface(next_buffer->buf_ctx, context->surface, 0, 0);
	cairo_paint(next_buffer->buf_ctx);

	drmModePageFlip(fd, context->crtc_id, next_buffer->fb_id,
			DRM_MODE_PAGE_FLIP_EVENT, context);
	context->current_buffer = next_buffer;
	context->swap_count++;
	if (context->swap_count == 60) {
		gettimeofday(&end, NULL);
		t = end.tv_sec + end.tv_usec * 1e-6 -
			(context->start.tv_sec + context->start.tv_usec * 1e-6);
		fprintf(stderr, "freq: %.02fHz\n", context->swap_count / t);
		context->swap_count = 0;
		context->start = end;
	}
}

static int free_bo(int fd, struct buffer *buf)
{
	struct drm_mode_destroy_dumb arg;
	int ret;

	cairo_surface_destroy(buf->buf_surface);
	cairo_destroy(buf->buf_ctx);
	munmap(buf->ptr, buf->size);

	printf("drmModeRmFB %d\n", buf->fb_id);
	drmModeRmFB(fd, buf->fb_id);

	memset(&arg, 0, sizeof(arg));
	arg.handle = buf->handle;

	ret = drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &arg);
	if (ret) {
		fprintf(stderr, "failed to destroy dumb buffer: %s\n",
				strerror(errno));
		return ret;
	}

	return 0;
}

static int alloc_bo(int fd, int width, int height, struct buffer *buf)
{
	int handles[4] = { 0 }, pitches[4] = { 0 }, offsets[4] = { 0 };
	int ret;

	ret = create_bo(fd, buf, width, height, 32);
	if (ret)
		return ret;

	/* add bo object as FB */
	handles[0] = buf->handle;
	pitches[0] = buf->pitch;
	ret = drmModeAddFB2(fd, width, height, DRM_FORMAT_ARGB8888, handles,
			    pitches, offsets, &buf->fb_id, 0);
	if(ret){
		fprintf(stderr, "drmModeAddFB2 failed (%ux%u): %s\n",
			width, height, strerror(errno));
		goto free;
	}

	return 0;

free:
	free_bo(fd, buf);

	return ret;
}

int main(int argc, char *argv[])
{
	int fd;
	drmModeRes *resources;
	drmModePlaneRes *planeres;
	drmModeConnector *connector;
	drmModeEncoder *encoder;
	drmModeModeInfo mode;
	drmModeCrtcPtr orig_crtc;
	struct buffer buf1 = { 0 }, buf2 = { 0 }, bufovr = { 0 };
	struct sigaction action;
	int ret, i;
	bool use_overlay;
	
	fd = open("/dev/dri/card0", O_RDWR);
	if (fd < 0){
		fprintf(stderr, "drmOpen failed: %s\n", strerror(errno));
		goto out;
	}

	resources = drmModeGetResources(fd);
	if (resources == NULL) {
		fprintf(stderr, "drmModeGetResources failed: %s\n", strerror(errno));
		goto close_fd;
	}

	/* find the first available connector with modes */
	for (i = 0; i < resources->count_connectors; ++i){
		connector = drmModeGetConnector(fd, resources->connectors[i]);
		if(connector != NULL){
			fprintf(stderr, "connector %d found\n", connector->connector_id);
			if(connector->connection == DRM_MODE_CONNECTED
				&& connector->count_modes > 0)
				break;
			drmModeFreeConnector(connector);
		}
		else
			fprintf(stderr, "get a null connector pointer\n");
	}
	if(i == resources->count_connectors){
		fprintf(stderr, "No active connector found.\n");
		goto free_drm_res;
	}

	planeres = drmModeGetPlaneResources(fd);
	if (planeres == NULL) {
		fprintf(stderr, "drmModeGetPlaneResources failed: %s\n", strerror(errno));
		goto close_fd;
	}

	printf("Driver supports %d planes\n", planeres->count_planes);
	for (i = 0; i < planeres->count_planes; i++) {
		drmModePlane *p;
		p = drmModeGetPlane(fd, planeres->planes[i]);
		if (!p) {
			fprintf(stderr, "drmModeGetPlane failed: %s\n", strerror(errno));
			goto close_fd;
		}

		printf("found plane id %d\n", p->plane_id);
		use_overlay = true;
	}

	mode = connector->modes[0];
	fprintf(stderr, "(%dx%d)\n", mode.hdisplay, mode.vdisplay);

	/* find the encoder matching the first available connector */
	for (i = 0; i < resources->count_encoders; ++i){
		encoder = drmModeGetEncoder(fd, resources->encoders[i]);
		if(encoder != NULL){
			fprintf(stderr, "encoder %d found\n", encoder->encoder_id);
			if(encoder->encoder_id == connector->encoder_id);
				break;
			drmModeFreeEncoder(encoder);
		} else
			fprintf(stderr, "get a null encoder pointer\n");
	}
	if (i == resources->count_encoders){
		fprintf(stderr, "No matching encoder with connector, shouldn't happen\n");
		goto free_drm_res;
	}

	/* init DRM dumb buffers */
	ret = alloc_bo(fd, mode.hdisplay, mode.vdisplay, &buf1);
	if (ret)
		goto free_drm_res;

	orig_crtc = drmModeGetCrtc(fd, encoder->crtc_id);
	if (orig_crtc == NULL)
		goto free_first_buf;

	/* kernel mode setting, wow! */
	ret = drmModeSetCrtc(fd, encoder->crtc_id, buf1.fb_id,
				0, 0, 	/* x, y */
				&connector->connector_id,
				1, 	/* element count of the connectors array above*/
				&mode);
	if (ret) {
		fprintf(stderr, "drmModeSetCrtc failed: %s\n", strerror(errno));
		goto free_first_buf;
	}

	ret = alloc_bo(fd, mode.hdisplay, mode.vdisplay, &buf2);
	if (ret)
		goto free_first_buf;

	struct flip_context flip_context;
	memset(&flip_context, 0, sizeof(flip_context));
	ret = drmModePageFlip(fd, encoder->crtc_id, buf2.fb_id,
				DRM_MODE_PAGE_FLIP_EVENT, &flip_context);
	
	if (ret) {
		fprintf(stderr, "failed to page flip: %s\n", strerror(errno));
		goto free_second_buf;
	}

	/* Create a buffer for the overlay plane if the driver supports it */
	if (use_overlay) {
		int sizex = 500;//mode.hdisplay / 2;
		int sizey = 115;//mode.vdisplay / 2;
		ret = alloc_bo(fd, sizex, sizey, &bufovr);
		if (ret)
			goto free_second_buf;

		draw_overlay(&bufovr);
		ret = drmModeSetPlane(fd, planeres->planes[0], encoder->crtc_id,
				bufovr.fb_id, 0, 10, 20, sizex, sizey,
				0 << 16, 0 << 16, sizex << 16, sizey << 16);
		if (ret) {
			printf("init overlay buffer failed\n");
			goto free_overlay_buf;
		}
	}

	flip_context.buffers[0] = &buf1;
	flip_context.buffers[1] = &buf2;
	flip_context.current_buffer = &buf2;
	flip_context.width = mode.hdisplay;
	flip_context.height = mode.vdisplay;
	flip_context.surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
			mode.hdisplay, mode.vdisplay);
	flip_context.ctx = cairo_create(flip_context.surface);
	flip_context.crtc_id = encoder->crtc_id;
	flip_context.swap_count = 0;
	gettimeofday(&flip_context.start, NULL);

	drmEventContext evctx;
	memset(&evctx, 0, sizeof evctx);
	evctx.version = DRM_EVENT_CONTEXT_VERSION;
	evctx.vblank_handler = NULL;
	evctx.page_flip_handler = page_flip_handler;

	memset(&action, 0, sizeof(struct sigaction));
	action.sa_handler = signal_handler;
	sigaction(SIGTERM, &action, NULL);
	sigaction(SIGINT, &action, NULL);

	while (!cancel) {
		struct timeval timeout = { 
			.tv_sec = 3, 
			.tv_usec = 0 
		};
		fd_set fds;

		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		ret = select(fd + 1, &fds, NULL, NULL, &timeout);

		if (ret <= 0) {
			continue;
		} else {
			/* drm device fd data ready */
			ret = drmHandleEvent(fd, &evctx);
			if (ret != 0) {
				fprintf(stderr, "drmHandleEvent failed: %s\n", strerror(errno));
				break;
			}
		}
	}

	if (use_overlay)
		ret = drmModeSetPlane(fd, planeres->planes[0], encoder->crtc_id, 0,
				0, 0, 0, 0, 0, 0, 0, 0, 0);

	ret = drmModeSetCrtc(fd, orig_crtc->crtc_id, orig_crtc->buffer_id,
					orig_crtc->x, orig_crtc->y,
					&connector->connector_id, 1, &orig_crtc->mode);
	if (ret) {
		fprintf(stderr, "drmModeSetCrtc() restore original crtc failed: %m\n");
	}

free_overlay_buf:
	if (bufovr.handle)
		free_bo(fd, &bufovr);

free_second_buf:
	free_bo(fd, &buf2);
	
free_first_buf:
	free_bo(fd, &buf1);

free_drm_res:
	drmModeFreeResources(resources);

close_fd:
	drmClose(fd);
	
out:
	if (ret)
		return EXIT_FAILURE;
	return EXIT_SUCCESS;
}

