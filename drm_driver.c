// SPDX-License-Identifier: MIT
// Author:  Giovanni Santini
// Mail:    giovanni.santini@proton.me
// Github:  @San7o

#include <drm_fourcc.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <stdint.h>

#include "drm_driver.h"

struct drm_driver drm_driver = {0};

/*
 * Get the human-readable string from a DRM connector type.
 * This is compatible with Weston's connector naming.
 */
const char *conn_str(uint32_t conn_type)
{
	switch (conn_type) {
	case DRM_MODE_CONNECTOR_Unknown:     return "Unknown";
	case DRM_MODE_CONNECTOR_VGA:         return "VGA";
	case DRM_MODE_CONNECTOR_DVII:        return "DVI-I";
	case DRM_MODE_CONNECTOR_DVID:        return "DVI-D";
	case DRM_MODE_CONNECTOR_DVIA:        return "DVI-A";
	case DRM_MODE_CONNECTOR_Composite:   return "Composite";
	case DRM_MODE_CONNECTOR_SVIDEO:      return "SVIDEO";
	case DRM_MODE_CONNECTOR_LVDS:        return "LVDS";
	case DRM_MODE_CONNECTOR_Component:   return "Component";
	case DRM_MODE_CONNECTOR_9PinDIN:     return "DIN";
	case DRM_MODE_CONNECTOR_DisplayPort: return "DP";
	case DRM_MODE_CONNECTOR_HDMIA:       return "HDMI-A";
	case DRM_MODE_CONNECTOR_HDMIB:       return "HDMI-B";
	case DRM_MODE_CONNECTOR_TV:          return "TV";
	case DRM_MODE_CONNECTOR_eDP:         return "eDP";
	case DRM_MODE_CONNECTOR_VIRTUAL:     return "Virtual";
	case DRM_MODE_CONNECTOR_DSI:         return "DSI";
	default:                             return "Unknown";
	}
}

/*
 * Calculate an accurate refresh rate from 'mode'.
 * The result is in mHz.
 */
int refresh_rate(drmModeModeInfo *mode)
{
	int res = (mode->clock * 1000000LL / mode->htotal + mode->vtotal / 2) / mode->vtotal;

	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		res *= 2;

	if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
		res /= 2;

	if (mode->vscan > 1)
		res /= mode->vscan;

	return res;
}

static uint32_t find_crtc(int drm_fd, drmModeRes *res, drmModeConnector *conn,
		uint32_t *taken_crtcs)
{
	for (int i = 0; i < conn->count_encoders; ++i) {
		drmModeEncoder *enc = drmModeGetEncoder(drm_fd, conn->encoders[i]);
		if (!enc)
			continue;

		for (int i = 0; i < res->count_crtcs; ++i) {
			uint32_t bit = 1 << i;
			// Not compatible
			if ((enc->possible_crtcs & bit) == 0)
				continue;

			// Already taken
			if (*taken_crtcs & bit)
				continue;

			drmModeFreeEncoder(enc);
			*taken_crtcs |= bit;
			return res->crtcs[i];
		}

		drmModeFreeEncoder(enc);
	}

	return 0;
}

static bool create_fb(int drm_fd, uint32_t width, uint32_t height,
		struct dumb_framebuffer *fb)
{
	int ret;

	struct drm_mode_create_dumb create = {
		.width = width,
		.height = height,
		.bpp = 32,
	};

	ret = drmIoctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create);
	if (ret < 0) {
		perror("DRM_IOCTL_MODE_CREATE_DUMB");
		return false;
	}

	fb->height = height;
	fb->width = width;
	fb->stride = create.pitch;
	fb->handle = create.handle;
	fb->size = create.size;

	uint32_t handles[4] = { fb->handle };
	uint32_t strides[4] = { fb->stride };
	uint32_t offsets[4] = { 0 };

	ret = drmModeAddFB2(drm_fd, width, height, DRM_FORMAT_XRGB8888,
		handles, strides, offsets, &fb->id, 0);
	if (ret < 0) {
		perror("drmModeAddFB2");
		goto error_dumb;
	}

	struct drm_mode_map_dumb map = { .handle = fb->handle };
	ret = drmIoctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &map);
	if (ret < 0) {
		perror("DRM_IOCTL_MODE_MAP_DUMB");
		goto error_fb;
	}

	fb->data = mmap(0, fb->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		drm_fd, map.offset);
	if (!fb->data) {
		perror("mmap");
		goto error_fb;
	}

  memset(fb->data, 0x00, fb->size);
  
	return true;

error_fb:
	drmModeRmFB(drm_fd, fb->id);
error_dumb:
	;
	struct drm_mode_destroy_dumb destroy = { .handle = fb->handle };
	drmIoctl(drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
	return false;
}

static void destroy_fb(int drm_fd, struct dumb_framebuffer *fb)
{
	munmap(fb->data, fb->size);
	drmModeRmFB(drm_fd, fb->id);
	struct drm_mode_destroy_dumb destroy = { .handle = fb->handle };
	drmIoctl(drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
}

/* Called each frame */
void page_flip_handler(int drm_fd, unsigned sequence, unsigned tv_sec,
                       unsigned tv_usec, void *data)
{
	(void)sequence;
	(void)tv_sec;
	(void)tv_usec;

	struct connector *conn = data;

	conn->colour[conn->inc] += 15;
	conn->colour[conn->dec] -= 15;

	if (conn->colour[conn->dec] == 0) {
		conn->dec = conn->inc;
		conn->inc = (conn->inc + 2) % 3;
	}

	struct dumb_framebuffer *fb = conn->back;

	if (drmModePageFlip(drm_fd, conn->crtc_id, fb->id,
			DRM_MODE_PAGE_FLIP_EVENT, conn) < 0) {
		perror("drmModePageFlip");
	}

	// Comment these two lines out to remove double buffering
	conn->back = conn->front;
	conn->front = fb;
}

int drm_driver_init(void)
{
  memset(&drm_driver, 0, sizeof(drm_driver));
  
	/* We just take the first GPU that exists. */
	drm_driver.fd = open("/dev/dri/card0", O_RDWR | O_NONBLOCK);
	if (drm_driver.fd < 0) {
		perror("/dev/dri/card0");
		return 1;
	}

  drm_driver.input_fd = open("/dev/input/event0", O_NONBLOCK | O_RDONLY);
  if (drm_driver.input_fd < 0) {
    perror("Open /dev/input/even0");
    return 1;
  }

	drmModeRes *res = drmModeGetResources(drm_driver.fd);
	if (!res) {
		perror("drmModeGetResources");
		return 1;
	}

	drm_driver.conn_list = NULL;
	uint32_t taken_crtcs = 0;

	for (int i = 0; i < res->count_connectors; ++i) {
		drmModeConnector *drm_conn = drmModeGetConnector(drm_driver.fd, res->connectors[i]);
		if (!drm_conn) {
			perror("drmModeGetConnector");
			continue;
		}

		struct connector *conn = malloc(sizeof *conn);
		if (!conn) {
			perror("malloc");
			goto cleanup;
		}

		conn->id = drm_conn->connector_id;
		snprintf(conn->name, sizeof conn->name, "%s-%"PRIu32,
			conn_str(drm_conn->connector_type),
			drm_conn->connector_type_id);
		conn->connected = drm_conn->connection == DRM_MODE_CONNECTED;

		conn->next = drm_driver.conn_list;
		drm_driver.conn_list = conn;

		printf("Found display %s\n", conn->name);

		if (!conn->connected) {
			printf("  Disconnected\n");
			goto cleanup;
		}

		if (drm_conn->count_modes == 0) {
			printf("No valid modes\n");
			conn->connected = false;
			goto cleanup;
		}

		conn->crtc_id = find_crtc(drm_driver.fd, res, drm_conn, &taken_crtcs);
		if (!conn->crtc_id) {
			fprintf(stderr, "Could not find CRTC for %s\n", conn->name);
			conn->connected = false;
			goto cleanup;
		}

		printf("  Using CRTC %"PRIu32"\n", conn->crtc_id);

		// [0] is the best mode, so we'll just use that.
		conn->mode = drm_conn->modes[0];

		conn->width = conn->mode.hdisplay;
		conn->height = conn->mode.vdisplay;
		conn->rate = refresh_rate(&conn->mode);

		printf("  Using mode %"PRIu32"x%"PRIu32"@%"PRIu32"\n",
			conn->width, conn->height, conn->rate);

		if (!create_fb(drm_driver.fd, conn->width, conn->height, &conn->fb[0])) {
			conn->connected = false;
			goto cleanup;
		}

		printf("  Created frambuffer with ID %"PRIu32"\n", conn->fb[0].id);

		if (!create_fb(drm_driver.fd, conn->width, conn->height, &conn->fb[1])) {
			destroy_fb(drm_driver.fd, &conn->fb[0]);
			conn->connected = false;
			goto cleanup;
		}

		printf("  Created frambuffer with ID %"PRIu32"\n", conn->fb[1].id);

		conn->front = &conn->fb[0];
		conn->back = &conn->fb[1];

		conn->colour[0] = 0x00; // B
		conn->colour[1] = 0x00; // G
		conn->colour[2] = 0xff; // R
		conn->colour[3] = 0x00; // X
		conn->inc = 1;
		conn->dec = 2;

		// Save the previous CRTC configuration
		conn->saved = drmModeGetCrtc(drm_driver.fd, conn->crtc_id);

		// Perform the modeset
		int ret = drmModeSetCrtc(drm_driver.fd, conn->crtc_id, conn->front->id, 0, 0,
			&conn->id, 1, &conn->mode);
		if (ret < 0) {
			perror("drmModeSetCrtc");
			goto cleanup;
		}

		// Start the page flip cycle
		if (drmModePageFlip(drm_driver.fd, conn->crtc_id, conn->front->id,
				DRM_MODE_PAGE_FLIP_EVENT, conn) < 0) {
			perror("drmModePageFlip");
		}

cleanup:
		drmModeFreeConnector(drm_conn);
	}

	drmModeFreeResources(res);
  return 1;
}

void drm_driver_draw(pixel_t *buff)
{
if (!buff) return; // Basic safety

  struct connector *conn = drm_driver.conn_list;
  while (conn)
  {
    if (!conn->connected || !conn->back) {
        conn = conn->next;
        continue;
    }

    struct dumb_framebuffer *fb = conn->back;

    // Use the actual Doom resolution constants
    uint32_t doom_w = DOOMGENERIC_RESX; 
    uint32_t doom_h = DOOMGENERIC_RESY;

    // Only draw what fits in both buffers
    uint32_t draw_h = (fb->height < doom_h) ? fb->height : doom_h;
    uint32_t draw_w = (fb->width < doom_w) ? fb->width : doom_w;

    for (uint32_t y = 0; y < draw_h; ++y) {
      // row is the start of the line in the GPU memory
      uint32_t *dst_row = (uint32_t*)(fb->data + (y * fb->stride));
      // src is the start of the line in the Doom buffer
      uint32_t *src_row = &buff[y * doom_w];
      
      for (uint32_t x = 0; x < draw_w; ++x) {
          dst_row[x] = src_row[x]; 
      }
    }
    
    conn = conn->next;
  }
}

void drm_driver_cleanup(void)
{
	// Cleanup
	struct connector *conn = drm_driver.conn_list;
	while (conn) {
		if (conn->connected) {
			destroy_fb(drm_driver.fd, &conn->fb[0]);
			destroy_fb(drm_driver.fd, &conn->fb[1]);

			// Restore the old CRTC
			drmModeCrtc *crtc = conn->saved;
			if (crtc) {
				drmModeSetCrtc(drm_driver.fd, crtc->crtc_id, crtc->buffer_id,
					crtc->x, crtc->y, &conn->id, 1, &crtc->mode);
				drmModeFreeCrtc(crtc);
			}
		}

		struct connector *tmp = conn->next;
		free(conn);
		conn = tmp;
	}

  close(drm_driver.input_fd);
	close(drm_driver.fd);
}
