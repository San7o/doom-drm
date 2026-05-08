// SPDX-License-Identifier: MIT
// Author:  Giovanni Santini
// Mail:    giovanni.santini@proton.me
// Github:  @San7o

#ifndef DRM_DRIVER_H
#define DRM_DRIVER_H

#include <xf86drm.h>
#include <xf86drmMode.h>

#include "doomgeneric.h"

struct dumb_framebuffer {
	uint32_t id;     // DRM object ID
	uint32_t width;
	uint32_t height;
	uint32_t stride;
	uint32_t handle; // driver-specific handle
	uint64_t size;   // size of mapping

	uint8_t *data;   // mmapped data we can write to
};

struct connector {
	uint32_t id;
	char name[16];
	bool connected;

	drmModeCrtc *saved;

	uint32_t crtc_id;
	drmModeModeInfo mode;

	uint32_t width;
	uint32_t height;
	uint32_t rate;

	struct dumb_framebuffer fb[2];
	struct dumb_framebuffer *front;
	struct dumb_framebuffer *back;

	uint8_t colour[4]; // B G R X
	int inc;
	int dec;

	struct connector *next;
};

struct drm_driver {
  int fd;
  int input_fd;
  struct connector *conn_list;
};

extern struct drm_driver drm_driver;

int drm_driver_init(void);
void drm_driver_cleanup(void);
void drm_driver_draw(pixel_t* buff);
void page_flip_handler(int drm_fd, unsigned sequence, unsigned tv_sec,
                       unsigned tv_usec, void *data);
  
#endif // DRM_DRIVER_H
