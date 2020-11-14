#ifndef __PLAT_H__
#define __PLAT_H__

#include "stdint.h"

#define RES_HW_SCREEN_HORIZONTAL  240
#define RES_HW_SCREEN_VERTICAL    240

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#define ABS(x) (((x) < 0) ? (-x) : (x))

void plat_init(void);
void plat_finish(void);
void plat_minimize(void);
void *plat_prepare_screenshot(int *w, int *h, int *bpp);

// indirectly called from GPU plugin
void  plat_gvideo_open(int is_pal);
void *plat_gvideo_set_mode(int *w, int *h, int *bpp);
void *plat_gvideo_flip(void);
void  plat_gvideo_close(void);

void SDL_Copy_Rotate_270(uint16_t *source_pixels, uint16_t *dest_pixels,
                int src_w, int src_h, int dst_w, int dst_h);

#endif // __PLAT_H__
