#ifndef FFI_H
#define FFI_H

#include <stdint.h>

void* new_muxer(const char *filename, int width, int height, int is265, int framerate);

int write_video_frame(void *muxer, const uint8_t *data, int len, int64_t pts_ms, int key);

int write_tail(void *muxer);

void free_muxer(void *muxer);

#endif