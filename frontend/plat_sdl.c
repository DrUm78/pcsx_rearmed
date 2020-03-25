/*
 * (C) Gražvydas "notaz" Ignotas, 2011-2013
 *
 * This work is licensed under the terms of any of these licenses
 * (at your option):
 *  - GNU GPL, version 2 or later.
 *  - GNU LGPL, version 2.1 or later.
 * See the COPYING file in the top-level directory.
 */

#include <stdio.h>
#include <math.h>
#include <SDL.h>
#include <SDL/SDL_ttf.h>

#include "libpicofe/input.h"
#include "libpicofe/in_sdl.h"
#include "libpicofe/menu.h"
#include "libpicofe/fonts.h"
#include "libpicofe/plat_sdl.h"
#include "libpicofe/gl.h"
#include "cspace.h"
#include "plugin_lib.h"
#include "plugin.h"
#include "main.h"
#include "menu.h"
#include "plat.h"
#include "revision.h"


#define AVERAGE(z, x) ((((z) & 0xF7DEF7DE) >> 1) + (((x) & 0xF7DEF7DE) >> 1))
#define AVERAGEHI(AB) ((((AB) & 0xF7DE0000) >> 1) + (((AB) & 0xF7DE) << 15))
#define AVERAGELO(CD) ((((CD) & 0xF7DE) >> 1) + (((CD) & 0xF7DE0000) >> 17))

// Support math
#define Half(A) (((A) >> 1) & 0x7BEF)
#define Quarter(A) (((A) >> 2) & 0x39E7)
// Error correction expressions to piece back the lower bits together
#define RestHalf(A) ((A) & 0x0821)
#define RestQuarter(A) ((A) & 0x1863)

// Error correction expressions for quarters of pixels
#define Corr1_3(A, B)     Quarter(RestQuarter(A) + (RestHalf(B) << 1) + RestQuarter(B))
#define Corr3_1(A, B)     Quarter((RestHalf(A) << 1) + RestQuarter(A) + RestQuarter(B))

// Error correction expressions for halves
#define Corr1_1(A, B)     ((A) & (B) & 0x0821)

// Quarters
#define Weight1_3(A, B)   (Quarter(A) + Half(B) + Quarter(B) + Corr1_3(A, B))
#define Weight3_1(A, B)   (Half(A) + Quarter(A) + Quarter(B) + Corr3_1(A, B))

// Halves
#define Weight1_1(A, B)   (Half(A) + Half(B) + Corr1_1(A, B))


static const struct in_default_bind in_sdl_defbinds[] = {
  { SDLK_u,	     IN_BINDTYPE_PLAYER12, DKEY_UP },
  { SDLK_d, 	   IN_BINDTYPE_PLAYER12, DKEY_DOWN },
  { SDLK_l,   	 IN_BINDTYPE_PLAYER12, DKEY_LEFT },
  { SDLK_r,  	   IN_BINDTYPE_PLAYER12, DKEY_RIGHT },
  { SDLK_x,      IN_BINDTYPE_PLAYER12, DKEY_TRIANGLE },
  { SDLK_b,      IN_BINDTYPE_PLAYER12, DKEY_CROSS },
  { SDLK_a,      IN_BINDTYPE_PLAYER12, DKEY_CIRCLE },
  { SDLK_y,      IN_BINDTYPE_PLAYER12, DKEY_SQUARE },
  { SDLK_s,      IN_BINDTYPE_PLAYER12, DKEY_START },
  { SDLK_k,      IN_BINDTYPE_PLAYER12, DKEY_SELECT },
  { SDLK_m,      IN_BINDTYPE_PLAYER12, DKEY_L1 },
  { SDLK_n,      IN_BINDTYPE_PLAYER12, DKEY_R1 },
  { SDLK_v,      IN_BINDTYPE_PLAYER12, DKEY_L2 },
  { SDLK_o,      IN_BINDTYPE_PLAYER12, DKEY_R2 },
  //{ SDLK_ESCAPE, IN_BINDTYPE_EMU, SACTION_ENTER_MENU },
  { SDLK_q, 	   IN_BINDTYPE_EMU, SACTION_ENTER_MENU },
  //{ SDLK_p,      IN_BINDTYPE_EMU, SACTION_PRE_SAVE_STATE },
  { SDLK_F1,      IN_BINDTYPE_EMU, SACTION_PRE_SAVE_STATE },
  { SDLK_F2,     IN_BINDTYPE_EMU, SACTION_LOAD_STATE },
  { SDLK_F3,     IN_BINDTYPE_EMU, SACTION_PREV_SSLOT },
  { SDLK_F4,     IN_BINDTYPE_EMU, SACTION_NEXT_SSLOT },
  { SDLK_F5,     IN_BINDTYPE_EMU, SACTION_TOGGLE_FSKIP },
  { SDLK_F6,     IN_BINDTYPE_EMU, SACTION_SCREENSHOT },
  { SDLK_F7,     IN_BINDTYPE_EMU, SACTION_TOGGLE_FPS },
  { SDLK_F11,    IN_BINDTYPE_EMU, SACTION_TOGGLE_FULLSCREEN },
  { SDLK_BACKSPACE, IN_BINDTYPE_EMU, SACTION_FAST_FORWARD },
  { SDLK_h,      IN_BINDTYPE_EMU, SACTION_SWITCH_DISPMODE },
  { SDLK_j,      IN_BINDTYPE_EMU, SACTION_ASPECT_RATIO_FACTOR_DECREASE },
  { SDLK_i,      IN_BINDTYPE_EMU, SACTION_ASPECT_RATIO_FACTOR_INCREASE },
  { SDLK_c,      IN_BINDTYPE_EMU, SACTION_VOLUME_UP },
  { SDLK_e,      IN_BINDTYPE_EMU, SACTION_VOLUME_DOWN },
  { SDLK_g,      IN_BINDTYPE_EMU, SACTION_BRIGHTNESS_UP },
  { SDLK_w,      IN_BINDTYPE_EMU, SACTION_BRIGHTNESS_DOWN },
  { 0, 0, 0 }
};

const struct menu_keymap in_sdl_key_map[] =
{
  /*{ SDLK_UP,     PBTN_UP },
  { SDLK_DOWN,   PBTN_DOWN },
  { SDLK_LEFT,   PBTN_LEFT },
  { SDLK_RIGHT,  PBTN_RIGHT },
  { SDLK_RETURN, PBTN_MOK },
  { SDLK_ESCAPE, PBTN_MBACK },
  { SDLK_SEMICOLON,    PBTN_MA2 },
  { SDLK_QUOTE,        PBTN_MA3 },
  { SDLK_LEFTBRACKET,  PBTN_L },
  { SDLK_RIGHTBRACKET, PBTN_R },*/
  { SDLK_u,     PBTN_UP },
  { SDLK_d,   PBTN_DOWN },
  { SDLK_l,   PBTN_LEFT },
  { SDLK_r,  PBTN_RIGHT },
  { SDLK_a, PBTN_MOK },
  { SDLK_b, PBTN_MBACK },
  { SDLK_x,    PBTN_MA2 },
  { SDLK_y,        PBTN_MA3 },
  { SDLK_m,  PBTN_L },
  { SDLK_n, PBTN_R },
};

const struct menu_keymap in_sdl_joy_map[] =
{
  /*{ SDLK_UP,    PBTN_UP },
  { SDLK_DOWN,  PBTN_DOWN },
  { SDLK_LEFT,  PBTN_LEFT },
  { SDLK_RIGHT, PBTN_RIGHT },*/
  { SDLK_u,    PBTN_UP },
  { SDLK_d,  PBTN_DOWN },
  { SDLK_l,  PBTN_LEFT },
  { SDLK_r, PBTN_RIGHT },
  /* joystick */
  { SDLK_WORLD_0, PBTN_MOK },
  { SDLK_WORLD_1, PBTN_MBACK },
  { SDLK_WORLD_2, PBTN_MA2 },
  { SDLK_WORLD_3, PBTN_MA3 },
};

static const struct in_pdata in_sdl_platform_data = {
  .defbinds  = in_sdl_defbinds,
  .key_map   = in_sdl_key_map,
  .kmap_size = sizeof(in_sdl_key_map) / sizeof(in_sdl_key_map[0]),
  .joy_map   = in_sdl_joy_map,
  .jmap_size = sizeof(in_sdl_joy_map) / sizeof(in_sdl_joy_map[0]),
};

static int psx_w, psx_h;
static void *shadow_fb, *menubg_img;
static int in_menu;

SDL_Surface * hw_screen = NULL;
SDL_Surface *virtual_hw_screen = NULL;


void clear_hw_screen(SDL_Surface *screen, uint16_t color)
{
  if(screen){
    uint16_t *dest_ptr = (uint16_t *)screen->pixels;
    uint32_t x, y;

    for(y = 0; y < screen->h; y++)
    {
      for(x = 0; x < screen->w; x++, dest_ptr++)
      {
        *dest_ptr = color;
      }
    }
  }
}

static int change_video_mode(int force)
{
  int w, h;

  if (in_menu) {
    //printf("In change_video_mode, setting w=%d, h=%d\n", g_menuscreen_w, g_menuscreen_h);
    w = g_menuscreen_w;
    h = g_menuscreen_h;
  }
  else {
    w = psx_w;
    h = psx_h;
  }

  clear_hw_screen(virtual_hw_screen, 0);
  return plat_sdl_change_video_mode(w, h, force);
}

static void resize_cb(int w, int h)
{
  // used by some plugins..
  pl_rearmed_cbs.screen_w = w;
  pl_rearmed_cbs.screen_h = h;
  pl_rearmed_cbs.gles_display = gl_es_display;
  pl_rearmed_cbs.gles_surface = gl_es_surface;
  plugin_call_rearmed_cbs();
}

static void quit_cb(void)
{
  emu_core_ask_exit();
}

static void get_layer_pos(int *x, int *y, int *w, int *h)
{
  // always fill entire SDL window
  *x = *y = 0;
  *w = pl_rearmed_cbs.screen_w;
  *h = pl_rearmed_cbs.screen_h;
}

void plat_init(void)
{
  int shadow_size;
  int ret;

  plat_sdl_quit_cb = quit_cb;
  plat_sdl_resize_cb = resize_cb;

  ret = plat_sdl_init();
  if (ret != 0)
    exit(1);

  if(TTF_Init())
  {
        fprintf(stderr, "Error TTF_Init: %s\n", TTF_GetError());
        exit(EXIT_FAILURE);
  }

  hw_screen = SDL_SetVideoMode(RES_HW_SCREEN_HORIZONTAL, RES_HW_SCREEN_VERTICAL, 16, SDL_FULLSCREEN | SDL_HWSURFACE | SDL_DOUBLEBUF);
  if(hw_screen == NULL)
  {
        printf("Error SDL_SetVideoMode: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
  }

  virtual_hw_screen = SDL_CreateRGBSurface(SDL_SWSURFACE, RES_HW_SCREEN_HORIZONTAL, RES_HW_SCREEN_VERTICAL, 16, 0xFFFF, 0xFFFF, 0xFFFF, 0);
  if(virtual_hw_screen == NULL)
  {
        printf("Error creating virtual_hw_screen: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
  }

  in_menu = 1;
  SDL_WM_SetCaption("PCSX-ReARMed " REV, NULL);

  shadow_size = g_menuscreen_w * g_menuscreen_h * 2;
  if (shadow_size < 640 * 512 * 2)
    shadow_size = 640 * 512 * 2;

  shadow_fb = malloc(shadow_size);
  menubg_img = malloc(shadow_size);
  if (shadow_fb == NULL || menubg_img == NULL) {
    fprintf(stderr, "OOM\n");
    exit(1);
  }

  in_sdl_init(&in_sdl_platform_data, plat_sdl_event_handler);
  in_probe();
  pl_rearmed_cbs.only_16bpp = 1;
  pl_rearmed_cbs.pl_get_layer_pos = get_layer_pos;

  bgr_to_uyvy_init();

  init_menu_SDL();
  //init_menu_zones();
  //init_menu_system_values();
}

void plat_finish(void)
{
  SDL_FreeSurface(virtual_hw_screen);
  deinit_menu_SDL();
  free(shadow_fb);
  shadow_fb = NULL;
  free(menubg_img);
  menubg_img = NULL;
  TTF_Quit();
  plat_sdl_finish();
}

void plat_gvideo_open(int is_pal)
{
}

static void uyvy_to_rgb565(void *d, const void *s, int pixels)
{
  unsigned short *dst = d;
  const unsigned int *src = s;
  int v;

  // no colors, for now
  for (; pixels > 0; src++, dst += 2, pixels -= 2) {
    v = (*src >> 8) & 0xff;
    v = (v - 16) * 255 / 219 / 8;
    dst[0] = (v << 11) | (v << 6) | v;

    v = (*src >> 24) & 0xff;
    v = (v - 16) * 255 / 219 / 8;
    dst[1] = (v << 11) | (v << 6) | v;
  }
}

static void overlay_blit(int doffs, const void *src_, int w, int h,
                         int sstride, int bgr24)
{
  const unsigned short *src = src_;
  unsigned short *dst;
  int dstride = plat_sdl_overlay->w;

  SDL_LockYUVOverlay(plat_sdl_overlay);
  dst = (void *)plat_sdl_overlay->pixels[0];

  dst += doffs;
  if (bgr24) {
    for (; h > 0; dst += dstride, src += sstride, h--)
      bgr888_to_uyvy(dst, src, w);
  }
  else {
    for (; h > 0; dst += dstride, src += sstride, h--)
      bgr555_to_uyvy(dst, src, w);
  }

  SDL_UnlockYUVOverlay(plat_sdl_overlay);
}

static void overlay_hud_print(int x, int y, const char *str, int bpp)
{
  SDL_LockYUVOverlay(plat_sdl_overlay);
  basic_text_out_uyvy_nf(plat_sdl_overlay->pixels[0], plat_sdl_overlay->w, x, y, str);
  SDL_UnlockYUVOverlay(plat_sdl_overlay);
}

void *plat_gvideo_set_mode(int *w, int *h, int *bpp)
{
  psx_w = *w;
  psx_h = *h;
  change_video_mode(0);
  if (plat_sdl_overlay != NULL) {
    pl_plat_clear = plat_sdl_overlay_clear;
    pl_plat_blit = overlay_blit;
    pl_plat_hud_print = overlay_hud_print;
    return NULL;
  }
  else {
    pl_plat_clear = NULL;
    pl_plat_blit = NULL;
    pl_plat_hud_print = NULL;
    if (plat_sdl_gl_active)
      return shadow_fb;
    else
      return plat_sdl_screen->pixels;
  }
}


// Nearest neighboor
void flip_NN(SDL_Surface *virtual_screen, SDL_Surface *hardware_screen, int new_w, int new_h){
  int w2=new_w;
  int h2=new_h;
  int x_ratio = (int)((virtual_screen->w<<16)/w2) +1;
  int y_ratio = (int)((virtual_screen->h<<16)/h2) +1;
  //int x_ratio = (int)((w1<<16)/w2) ;
  //int y_ratio = (int)((h1<<16)/h2) ;
  //printf("virtual_screen->w=%d, virtual_screen->h=%d\n", virtual_screen->w, virtual_screen->h);
  int x2, y2 ;
  for (int i=0;i<h2;i++) {
    if(i>=RES_HW_SCREEN_VERTICAL){
      continue;
    }
    //printf("\n\ny=%d\n", i);
    for (int j=0;j<w2;j++) {
      if(j>=RES_HW_SCREEN_HORIZONTAL){
        continue;
      }
      //printf("x=%d, ",j);
      x2 = ((j*x_ratio)>>16) ;
      y2 = ((i*y_ratio)>>16) ;

      //printf("y=%d, x=%d, y2=%d, x2=%d, (y2*virtual_screen->w)+x2=%d\n", i, j, y2, x2, (y2*virtual_screen->w)+x2);
      *(uint16_t*)(hardware_screen->pixels+(i* ((w2>RES_HW_SCREEN_HORIZONTAL)?RES_HW_SCREEN_HORIZONTAL:w2 ) +j)*sizeof(uint16_t)) =
      *(uint16_t*)(virtual_screen->pixels + ((y2*virtual_screen->w)+x2) *sizeof(uint16_t)) ;
    }
  }
}

// Nearest neighboor with possible out of screen coordinates (for cropping)
void flip_NN_AllowOutOfScreen(SDL_Surface *virtual_screen, SDL_Surface *hardware_screen, int new_w, int new_h){
  int w2=new_w;
  int h2=new_h;
  int x_ratio = (int)((virtual_screen->w<<16)/w2) +1;
  int y_ratio = (int)((virtual_screen->h<<16)/h2) +1;
  //int x_ratio = (int)((w1<<16)/w2) ;
  //int y_ratio = (int)((h1<<16)/h2) ;
  //printf("virtual_screen->w=%d, virtual_screen->h=%d\n", virtual_screen->w, virtual_screen->h);
  int x2, y2 ;

  /// --- Compute padding for centering when out of bounds ---
  int x_padding = 0;
  if(w2>RES_HW_SCREEN_HORIZONTAL){
    x_padding = (w2-RES_HW_SCREEN_HORIZONTAL)/2 + 1;
  }

  for (int i=0;i<h2;i++) {
    if(i>=RES_HW_SCREEN_VERTICAL){
      continue;
    }
    //printf("\n\ny=%d\n", i);
    for (int j=0;j<w2;j++) {
      if(j>=RES_HW_SCREEN_HORIZONTAL){
        continue;
      }
      //printf("x=%d, ",j);
      x2 = ((j*x_ratio)>>16) ;
      y2 = ((i*y_ratio)>>16) ;

      //printf("y=%d, x=%d, y2=%d, x2=%d, (y2*virtual_screen->w)+x2=%d\n", i, j, y2, x2, (y2*virtual_screen->w)+x2);
      *(uint16_t*)(hardware_screen->pixels+(i* ((w2>RES_HW_SCREEN_HORIZONTAL)?RES_HW_SCREEN_HORIZONTAL:w2 ) +j)*sizeof(uint16_t)) =
      *(uint16_t*)(virtual_screen->pixels + ((y2*virtual_screen->w)+x2 + x_padding) *sizeof(uint16_t)) ;
    }
  }
}

/// Nearest neighboor optimized with possible out of screen coordinates (for cropping)
void flip_NNOptimized_AllowOutOfScreen(SDL_Surface *virtual_screen, SDL_Surface *hardware_screen, int new_w, int new_h){
  int w1=virtual_screen->w;
  //int h1=virtual_screen->h;
  int w2=new_w;
  int h2=new_h;
  int x_ratio = (int)((virtual_screen->w<<16)/w2);
  int y_ratio = (int)((virtual_screen->h<<16)/h2);

  int y_padding = (RES_HW_SCREEN_VERTICAL-new_h)/2;
  //int x_ratio = (int)((virtual_screen->w<<16)/w2);
  //int y_ratio = (int)((virtual_screen->h<<16)/h2);
  int x2, y2 ;

  /// --- Compute padding for centering when out of bounds ---
  int x_padding = 0;
  if(w2>RES_HW_SCREEN_HORIZONTAL){
    x_padding = (w2-RES_HW_SCREEN_HORIZONTAL)/2 + 1;
  }
  int x_padding_ratio = x_padding*w1/w2;
  //printf("virtual_screen->h=%d, h2=%d\n", virtual_screen->h, h2);

  for (int i=0;i<h2;i++)
  {
    if(i>=RES_HW_SCREEN_VERTICAL){
      continue;
    }

    uint16_t* t = (uint16_t*)(hardware_screen->pixels+((i+y_padding)* ((w2>RES_HW_SCREEN_HORIZONTAL)?RES_HW_SCREEN_HORIZONTAL:w2) )*sizeof(uint16_t));
    y2 = ((i*y_ratio)>>16);
    uint16_t* p = (uint16_t*)(virtual_screen->pixels + (y2*w1 + x_padding_ratio) *sizeof(uint16_t));
    int rat = 0;
    for (int j=0;j<w2;j++)
    {
      if(j>=RES_HW_SCREEN_HORIZONTAL){
        continue;
      }
      x2 = (rat>>16);
#ifdef BLACKER_BLACKS
      *t++ = p[x2] & 0xFFDF; /// Optimization for blacker blacks
#else
      *t++ = p[x2]; /// Optimization for blacker blacks
#endif
      rat += x_ratio;
      //printf("y=%d, x=%d, y2=%d, x2=%d, (y2*virtual_screen->w)+x2=%d\n", i, j, y2, x2, (y2*virtual_screen->w)+x2);
    }
  }
}


/// Nearest neighboor with 2D bilinear and interp by the number of pixel diff, not 2
void flip_NNOptimized_MissingPixelsBilinear(SDL_Surface *virtual_screen, SDL_Surface *hardware_screen, int new_w, int new_h){
  int w1=virtual_screen->w;
  int h1=virtual_screen->h;
  int w2=new_w;
  int h2=new_h;
  int y_padding = (RES_HW_SCREEN_VERTICAL-new_h)/2;
  //int x_ratio = (int)((w1<<16)/w2) +1;
  //int y_ratio = (int)((h1<<16)/h2) +1;
  int x_ratio = (int)((w1<<16)/w2);
  int y_ratio = (int)((h1<<16)/h2);
  int x1, y1;
  /*int cnt_yes_x_yes_y, cnt_yes_x_no_y, cnt_no_x_yes_y, cnt_no_x_no_y;
  cnt_yes_x_yes_y= cnt_yes_x_no_y= cnt_no_x_yes_y= cnt_no_x_no_y = 0;*/
  for (int i=0;i<h2;i++)
  {
    uint16_t* t = (uint16_t*)(hardware_screen->pixels+((i+y_padding)*w2)*sizeof(uint16_t));
    y1 = ((i*y_ratio)>>16);
    int px_diff_next_y = MAX( (((i+1)*y_ratio)>>16) - y1, 1);
    //printf("px_diff_next_y:%d\n", px_diff_next_y);
    uint16_t* p = (uint16_t*)(virtual_screen->pixels + (y1*w1) *sizeof(uint16_t));
    int rat = 0;
    for (int j=0;j<w2;j++)
    {
      // ------ current x value ------
      x1 = (rat>>16);
      int px_diff_next_x = MAX( ((rat+x_ratio)>>16) - x1, 1);

      // ------ optimized bilinear (to put in function) -------
      uint16_t * cur_p;
      int cur_y_offset;
      uint32_t red_comp = 0;
      uint32_t green_comp = 0;
      uint32_t blue_comp = 0;
      for(int cur_px_diff_y=0; cur_px_diff_y<px_diff_next_y; cur_px_diff_y++){
        cur_y_offset = (y1+cur_px_diff_y<h1)?(w1*cur_px_diff_y):0;
        for(int cur_px_diff_x=0; cur_px_diff_x<px_diff_next_x; cur_px_diff_x++){
          cur_p = (x1+cur_px_diff_x<w1)?(p+x1+cur_px_diff_x+cur_y_offset):(p+x1+cur_y_offset);
          red_comp += (*cur_p)&0xF800;
          green_comp += (*cur_p)&0x07E0;
          blue_comp += (*cur_p)&0x001F;
        }
      }
      red_comp = (red_comp / (px_diff_next_x*px_diff_next_y) )&0xF800;
#ifdef BLACKER_BLACKS
      /// Optimization for blacker blacks (our screen do not handle green value of 1 very well)
      green_comp = (green_comp / (px_diff_next_x*px_diff_next_y) )&0x07C0;
#else
      green_comp = (green_comp / (px_diff_next_x*px_diff_next_y) )&0x07E0;
#endif
      blue_comp = (blue_comp / (px_diff_next_x*px_diff_next_y) )&0x001F;
      *t++ = red_comp+green_comp+blue_comp;

      // ------ next pixel ------
      rat += x_ratio;
    }
  }
}



/// Interpolation with left, right pixels, pseudo gaussian weighting for downscaling
void flip_Downscale_LeftRightGaussianFilter(SDL_Surface *src_surface, SDL_Surface *dst_surface, int new_w, int new_h){
  int w1=src_surface->w;
  int h1=src_surface->h;
  int w2=dst_surface->w;
  int h2=dst_surface->h;
  int x_ratio = (int)((w1<<16)/w2);
  int y_ratio = (int)((h1<<16)/h2);
  int y_padding = (RES_HW_SCREEN_VERTICAL-h2)/2;
  int x1, y1;
  uint16_t *src_screen = (uint16_t *)src_surface->pixels;
  uint16_t *dst_screen = (uint16_t *)dst_surface->pixels;

  /// --- Compute padding for centering when out of bounds ---
  int x_padding = 0;
  if(w2>RES_HW_SCREEN_HORIZONTAL){
    x_padding = (w2-RES_HW_SCREEN_HORIZONTAL)/2 + 1;
  }
  int x_padding_ratio = x_padding*w1/w2;

  /// --- Interp params ---
  int px_diff_prev_x = 0;
  int px_diff_next_x = 0;
  uint32_t ponderation_factor;
  uint8_t left_px_missing, right_px_missing;

  uint16_t * cur_p;
  uint16_t * cur_p_left;
  uint16_t * cur_p_right;
  uint32_t red_comp, green_comp, blue_comp;


  for (int i=0;i<h2;i++)
  {
    if(i>=RES_HW_SCREEN_VERTICAL){
      continue;
    }
    uint16_t* t = (uint16_t*)(dst_screen +
      (i+y_padding)*((w2>RES_HW_SCREEN_HORIZONTAL)?RES_HW_SCREEN_HORIZONTAL:w2) );
    // ------ current and next y value ------
    y1 = ((i*y_ratio)>>16);
    uint16_t* p = (uint16_t*)(src_screen + (y1*w1+x_padding_ratio) );
    int rat = 0;
    for (int j=0;j<w2;j++)
    {
      if(j>=RES_HW_SCREEN_HORIZONTAL){
        continue;
      }

      // ------ current x value ------
      x1 = (rat>>16);
      px_diff_next_x = ((rat+x_ratio)>>16) - x1;

      // ------ adapted bilinear with 3x3 gaussian blur -------
      cur_p = p+x1;
      if(px_diff_prev_x > 1 || px_diff_next_x > 1 ){
        red_comp=((*cur_p)&0xF800) << 1;
        green_comp=((*cur_p)&0x07E0) << 1;
        blue_comp=((*cur_p)&0x001F) << 1;

        left_px_missing = (px_diff_prev_x > 1 && x1>0);
        right_px_missing = (px_diff_next_x > 1 && x1+1<w1);
        ponderation_factor = 2 + left_px_missing + right_px_missing;

        // ---- Interpolate current and left ----
        if(left_px_missing){
            cur_p_left = p+x1-1;
            red_comp += ((*cur_p_left)&0xF800);
            green_comp += ((*cur_p_left)&0x07E0);
            blue_comp += ((*cur_p_left)&0x001F);
        }

        // ---- Interpolate current and right ----
        if(right_px_missing){
          cur_p_right = p+x1+1;
            red_comp += ((*cur_p_right)&0xF800);
            green_comp += ((*cur_p_right)&0x07E0);
            blue_comp += ((*cur_p_right)&0x001F);
        }

        /// --- Compute new px value ---
        if(ponderation_factor==4){
            red_comp = (red_comp >> 2)&0xF800;
            green_comp = (green_comp >> 2)&0x07C0;
            blue_comp = (blue_comp >> 2)&0x001F;
        }
        else if(ponderation_factor==2){
            red_comp = (red_comp >> 1)&0xF800;
            green_comp = (green_comp >> 1)&0x07C0;
            blue_comp = (blue_comp >> 1)&0x001F;
        }
        else{
            red_comp = (red_comp / ponderation_factor )&0xF800;
            green_comp = (green_comp / ponderation_factor )&0x07C0;
            blue_comp = (blue_comp / ponderation_factor )&0x001F;
        }

        /// --- write pixel ---
        *t++ = red_comp+green_comp+blue_comp;
      }
      else{
        /// --- copy pixel ---
        *t++ = (*cur_p);

        /// Debug
        //occurence_pond[1] += 1;
      }

      /// save number of pixels to interpolate
      px_diff_prev_x = px_diff_next_x;

      // ------ next pixel ------
      rat += x_ratio;
    }
  }
}


/// Nearest neighbor with 2D bilinear and interpolation with left and right pixels, pseudo gaussian weighting
void flip_NNOptimized_LeftAndRightBilinear(SDL_Surface *virtual_screen, SDL_Surface *hardware_screen, int new_w, int new_h){
  int w1=virtual_screen->w;
  int h1=virtual_screen->h;
  int w2=new_w;
  int h2=new_h;
  int y_padding = (RES_HW_SCREEN_VERTICAL-new_h)/2;
  //int x_ratio = (int)((w1<<16)/w2) +1;
  //int y_ratio = (int)((h1<<16)/h2) +1;
  int x_ratio = (int)((w1<<16)/w2);
  int y_ratio = (int)((h1<<16)/h2);
  int x1, y1;

#ifdef BLACKER_BLACKS
      /// Optimization for blacker blacks (our screen do not handle green value of 1 very well)
      uint16_t green_mask = 0x07C0;
#else
      uint16_t green_mask = 0x07E0;
#endif

  /// --- Compute padding for centering when out of bounds ---
  int x_padding = 0;
  if(w2>RES_HW_SCREEN_HORIZONTAL){
    x_padding = (w2-RES_HW_SCREEN_HORIZONTAL)/2 + 1;
  }
  int x_padding_ratio = x_padding*w1/w2;

  /// --- Interp params ---
  int px_diff_prev_x = 0;
  int px_diff_next_x = 0;
  uint32_t ponderation_factor;
  uint16_t * cur_p;
  uint16_t * cur_p_left;
  uint16_t * cur_p_right;
  uint32_t red_comp, green_comp, blue_comp;
  //int cnt_interp = 0; int cnt_no_interp = 0;
  //printf("virtual_screen->w=%d, virtual_screen->w=%d\n", virtual_screen->w, virtual_screen->h);

  for (int i=0;i<h2;i++)
  {
    if(i>=RES_HW_SCREEN_VERTICAL){
      continue;
    }
    uint16_t* t = (uint16_t*)(hardware_screen->pixels+( (i+y_padding)*((w2>RES_HW_SCREEN_HORIZONTAL)?RES_HW_SCREEN_HORIZONTAL:w2))*sizeof(uint16_t));
    y1 = ((i*y_ratio)>>16);
    uint16_t* p = (uint16_t*)(virtual_screen->pixels + (y1*w1 + x_padding_ratio) *sizeof(uint16_t));
    int rat = 0;
    for (int j=0;j<w2;j++)
    {
      if(j>=RES_HW_SCREEN_HORIZONTAL){
        continue;
      }
      // ------ current x value ------
      x1 = (rat>>16);
      px_diff_next_x = ((rat+x_ratio)>>16) - x1;

      // ------ adapted bilinear with 3x3 gaussian blur -------
      cur_p = p+x1;
      if(px_diff_prev_x > 1 || px_diff_next_x > 1){
        red_comp=((*cur_p)&0xF800) << 1;
        green_comp=((*cur_p)&0x07E0) << 1;
        blue_comp=((*cur_p)&0x001F) << 1;
        ponderation_factor = 2;

        // ---- Interpolate current and left ----
        if(px_diff_prev_x > 1 && x1>0){
          cur_p_left = p+x1-1;

          red_comp += ((*cur_p_left)&0xF800);
          green_comp += ((*cur_p_left)&0x07E0);
          blue_comp += ((*cur_p_left)&0x001F);
          ponderation_factor++;
        }

        // ---- Interpolate current and right ----
        if(px_diff_next_x > 1 && x1+1<w1){
          cur_p_right = p+x1+1;

          red_comp += ((*cur_p_right)&0xF800);
          green_comp += ((*cur_p_right)&0x07E0);
          blue_comp += ((*cur_p_right)&0x001F);
          ponderation_factor++;
        }

        /// --- Compute new px value ---
        if(ponderation_factor==4){
          red_comp = (red_comp >> 2)&0xF800;
          green_comp = (green_comp >> 2)&green_mask;
          blue_comp = (blue_comp >> 2)&0x001F;
        }
        else if(ponderation_factor==2){
          red_comp = (red_comp >> 1)&0xF800;
          green_comp = (green_comp >> 1)&green_mask;
          blue_comp = (blue_comp >> 1)&0x001F;
        }
        else{
          red_comp = (red_comp / ponderation_factor )&0xF800;
          green_comp = (green_comp / ponderation_factor )&green_mask;
          blue_comp = (blue_comp / ponderation_factor )&0x001F;
        }

        /// --- write pixel ---
        *t++ = red_comp+green_comp+blue_comp;
      }
      else{
        /// --- copy pixel ---
#ifdef BLACKER_BLACKS
        /// Optimization for blacker blacks (our screen do not handle green value of 1 very well)
        *t++ = (*cur_p)&0xFFDF;
#else
        *t++ = (*cur_p);
#endif
      }

      /// save number of pixels to interpolate
      px_diff_prev_x = px_diff_next_x;

      // ------ next pixel ------
      rat += x_ratio;
    }
  }
  //printf("cnt_interp = %d, int cnt_no_interp = %d\n", cnt_interp, cnt_no_interp);
}

/// Nearest neighbor with 2D bilinear and interpolation with left, right, up and down pixels, pseudo gaussian weighting
void flip_NNOptimized_LeftRightUpDownBilinear(SDL_Surface *virtual_screen, SDL_Surface *hardware_screen, int new_w, int new_h){
  int w1=virtual_screen->w;
  int h1=virtual_screen->h;
  int w2=new_w;
  int h2=new_h;
  int y_padding = (RES_HW_SCREEN_VERTICAL-new_h)/2;
  //int x_ratio = (int)((w1<<16)/w2) +1;
  //int y_ratio = (int)((h1<<16)/h2) +1;
  int x_ratio = (int)((w1<<16)/w2);
  int y_ratio = (int)((h1<<16)/h2);
  int x1, y1;

#ifdef BLACKER_BLACKS
      /// Optimization for blacker blacks (our screen do not handle green value of 1 very well)
      uint16_t green_mask = 0x07C0;
#else
      uint16_t green_mask = 0x07E0;
#endif

  /// --- Compute padding for centering when out of bounds ---
  int x_padding = 0;
  if(w2>RES_HW_SCREEN_HORIZONTAL){
    x_padding = (w2-RES_HW_SCREEN_HORIZONTAL)/2 + 1;
  }
  int x_padding_ratio = x_padding*w1/w2;

  /// --- Interp params ---
  int px_diff_prev_x = 0;
  int px_diff_next_x = 0;
  int px_diff_prev_y = 0;
  int px_diff_next_y = 0;
  uint32_t ponderation_factor;
  uint16_t * cur_p;
  uint16_t * cur_p_left;
  uint16_t * cur_p_right;
  uint16_t * cur_p_up;
  uint16_t * cur_p_down;
  uint32_t red_comp, green_comp, blue_comp;
  //int cnt_interp = 0; int cnt_no_interp = 0;
  //printf("virtual_screen->w=%d, virtual_screen->w=%d\n", virtual_screen->w, virtual_screen->h);

  ///Debug

  for (int i=0;i<h2;i++)
  {
    if(i>=RES_HW_SCREEN_VERTICAL){
      continue;
    }
    uint16_t* t = (uint16_t*)(hardware_screen->pixels+( (i+y_padding)*((w2>RES_HW_SCREEN_HORIZONTAL)?RES_HW_SCREEN_HORIZONTAL:w2))*sizeof(uint16_t));
    // ------ current and next y value ------
    y1 = ((i*y_ratio)>>16);
    px_diff_next_y = MAX( (((i+1)*y_ratio)>>16) - y1, 1);
    uint16_t* p = (uint16_t*)(virtual_screen->pixels + (y1*w1+x_padding_ratio) *sizeof(uint16_t));
    int rat = 0;
    for (int j=0;j<w2;j++)
    {
      if(j>=RES_HW_SCREEN_HORIZONTAL){
        continue;
      }
      // ------ current x value ------
      x1 = (rat>>16);
      px_diff_next_x = ((rat+x_ratio)>>16) - x1;

      // ------ adapted bilinear with 3x3 gaussian blur -------
      cur_p = p+x1;
      if(px_diff_prev_x > 1 || px_diff_next_x > 1 || px_diff_prev_y > 1 || px_diff_next_y > 1){
        red_comp=((*cur_p)&0xF800) << 1;
        green_comp=((*cur_p)&0x07E0) << 1;
        blue_comp=((*cur_p)&0x001F) << 1;
        ponderation_factor = 2;

        // ---- Interpolate current and left ----
        if(px_diff_prev_x > 1 && x1>0){
          cur_p_left = p+x1-1;

          red_comp += ((*cur_p_left)&0xF800);
          green_comp += ((*cur_p_left)&0x07E0);
          blue_comp += ((*cur_p_left)&0x001F);
          ponderation_factor++;
        }

        // ---- Interpolate current and right ----
        if(px_diff_next_x > 1 && x1+1<w1){
          cur_p_right = p+x1+1;

          red_comp += ((*cur_p_right)&0xF800);
          green_comp += ((*cur_p_right)&0x07E0);
          blue_comp += ((*cur_p_right)&0x001F);
          ponderation_factor++;
        }

        // ---- Interpolate current and up ----
        if(px_diff_prev_y > 1 && y1 > 0){
          cur_p_up = p+x1-w1;

          red_comp += ((*cur_p_up)&0xF800);
          green_comp += ((*cur_p_up)&0x07E0);
          blue_comp += ((*cur_p_up)&0x001F);
          ponderation_factor++;
        }

        // ---- Interpolate current and down ----
        if(px_diff_next_y > 1 && y1 + 1 < h1){
          cur_p_down = p+x1+w1;

          red_comp += ((*cur_p_down)&0xF800);
          green_comp += ((*cur_p_down)&0x07E0);
          blue_comp += ((*cur_p_down)&0x001F);
          ponderation_factor++;
        }

        /// --- Compute new px value ---
        if(ponderation_factor==4){
          red_comp = (red_comp >> 2)&0xF800;
          green_comp = (green_comp >> 2)&green_mask;
          blue_comp = (blue_comp >> 2)&0x001F;
        }
        else if(ponderation_factor==2){
          red_comp = (red_comp >> 1)&0xF800;
          green_comp = (green_comp >> 1)&green_mask;
          blue_comp = (blue_comp >> 1)&0x001F;
        }
        else{
          red_comp = (red_comp / ponderation_factor )&0xF800;
          green_comp = (green_comp / ponderation_factor )&green_mask;
          blue_comp = (blue_comp / ponderation_factor )&0x001F;
        }

        /// --- write pixel ---
        *t++ = red_comp+green_comp+blue_comp;
      }
      else{
        /// --- copy pixel ---
#ifdef BLACKER_BLACKS
        /// Optimization for blacker blacks (our screen do not handle green value of 1 very well)
        *t++ = (*cur_p)&0xFFDF;
#else
        *t++ = (*cur_p);
#endif
      }

      /// save number of pixels to interpolate
      px_diff_prev_x = px_diff_next_x;

      // ------ next pixel ------
      rat += x_ratio;
    }
    px_diff_prev_y = px_diff_next_y;
  }
  //printf("cnt_interp = %d, int cnt_no_interp = %d\n", cnt_interp, cnt_no_interp);
}



/// Nearest neighbor with 2D bilinear and interpolation with left, right, up and down pixels, pseudo gaussian weighting
void flip_NNOptimized_LeftRightUpDownBilinear_Optimized4(SDL_Surface *virtual_screen, SDL_Surface *hardware_screen, int new_w, int new_h){
  int w1=virtual_screen->w;
  int h1=virtual_screen->h;
  int w2=new_w;
  int h2=new_h;
  int y_padding = (RES_HW_SCREEN_VERTICAL-new_h)/2;
  int x_ratio = (int)((w1<<16)/w2);
  int y_ratio = (int)((h1<<16)/h2);
  int x1, y1;

#ifdef BLACKER_BLACKS
      /// Optimization for blacker blacks (our screen do not handle green value of 1 very well)
      uint16_t green_mask = 0x07C0;
#else
      uint16_t green_mask = 0x07E0;
#endif

  /// --- Compute padding for centering when out of bounds ---
  int x_padding = 0;
  if(w2>RES_HW_SCREEN_HORIZONTAL){
    x_padding = (w2-RES_HW_SCREEN_HORIZONTAL)/2 + 1;
  }
  int x_padding_ratio = x_padding*w1/w2;

  /// --- Interp params ---
  int px_diff_prev_x = 0;
  int px_diff_next_x = 0;
  int px_diff_prev_y = 0;
  int px_diff_next_y = 0;
  uint32_t ponderation_factor;
  uint8_t left_px_missing, right_px_missing, up_px_missing, down_px_missing;
  int supposed_pond_factor;

  uint16_t * cur_p;
  uint16_t * cur_p_left;
  uint16_t * cur_p_right;
  uint16_t * cur_p_up;
  uint16_t * cur_p_down;
  uint32_t red_comp, green_comp, blue_comp;
  //printf("virtual_screen->w=%d, virtual_screen->w=%d\n", virtual_screen->w, virtual_screen->h);

  ///Debug
  /*int occurence_pond[7];
  memset(occurence_pond, 0, 7*sizeof(int));*/

  for (int i=0;i<h2;i++)
  {
    if(i>=RES_HW_SCREEN_VERTICAL){
      continue;
    }
    uint16_t* t = (uint16_t*)(hardware_screen->pixels+( (i+y_padding)*((w2>RES_HW_SCREEN_HORIZONTAL)?RES_HW_SCREEN_HORIZONTAL:w2))*sizeof(uint16_t));
    // ------ current and next y value ------
    y1 = ((i*y_ratio)>>16);
    px_diff_next_y = MAX( (((i+1)*y_ratio)>>16) - y1, 1);
    uint16_t* p = (uint16_t*)(virtual_screen->pixels + (y1*w1+x_padding_ratio) *sizeof(uint16_t));
    int rat = 0;
    for (int j=0;j<w2;j++)
    {
      if(j>=RES_HW_SCREEN_HORIZONTAL){
        continue;
      }
      // ------ current x value ------
      x1 = (rat>>16);
      px_diff_next_x = ((rat+x_ratio)>>16) - x1;

      // ------ adapted bilinear with 3x3 gaussian blur -------
      cur_p = p+x1;
      if(px_diff_prev_x > 1 || px_diff_next_x > 1 || px_diff_prev_y > 1 || px_diff_next_y > 1){
        red_comp=((*cur_p)&0xF800) << 1;
        green_comp=((*cur_p)&0x07E0) << 1;
        blue_comp=((*cur_p)&0x001F) << 1;
        ponderation_factor = 2;
        left_px_missing = (px_diff_prev_x > 1 && x1>0);
        right_px_missing = (px_diff_next_x > 1 && x1+1<w1);
        up_px_missing = (px_diff_prev_y > 1 && y1 > 0);
        down_px_missing = (px_diff_next_y > 1 && y1 + 1 < h1);
        supposed_pond_factor = 2 + left_px_missing + right_px_missing +
                                       up_px_missing + down_px_missing;

        // ---- Interpolate current and up ----
        if(up_px_missing){
          cur_p_up = p+x1-w1;

          if(supposed_pond_factor==3){
            red_comp += ((*cur_p_up)&0xF800) << 1;
            green_comp += ((*cur_p_up)&0x07E0) << 1;
            blue_comp += ((*cur_p_up)&0x001F) << 1;
            ponderation_factor+=2;
          }
          else if(supposed_pond_factor==4 ||
                  (supposed_pond_factor==5 && !down_px_missing )){
            red_comp += ((*cur_p_up)&0xF800);
            green_comp += ((*cur_p_up)&0x07E0);
            blue_comp += ((*cur_p_up)&0x001F);
            ponderation_factor++;
          }
        }

        // ---- Interpolate current and left ----
        if(left_px_missing){
          cur_p_left = p+x1-1;

          if(supposed_pond_factor==3){
            red_comp += ((*cur_p_left)&0xF800) << 1;
            green_comp += ((*cur_p_left)&0x07E0) << 1;
            blue_comp += ((*cur_p_left)&0x001F) << 1;
            ponderation_factor+=2;
          }
          else if(supposed_pond_factor==4 ||
                  (supposed_pond_factor==5 && !right_px_missing )){
            red_comp += ((*cur_p_left)&0xF800);
            green_comp += ((*cur_p_left)&0x07E0);
            blue_comp += ((*cur_p_left)&0x001F);
            ponderation_factor++;
          }
        }

        // ---- Interpolate current and down ----
        if(down_px_missing){
          cur_p_down = p+x1+w1;

          if(supposed_pond_factor==3){
            red_comp += ((*cur_p_down)&0xF800) << 1;
            green_comp += ((*cur_p_down)&0x07E0) << 1;
            blue_comp += ((*cur_p_down)&0x001F) << 1;
            ponderation_factor+=2;
          }
          else if(supposed_pond_factor>=4){
            red_comp += ((*cur_p_down)&0xF800);
            green_comp += ((*cur_p_down)&0x07E0);
            blue_comp += ((*cur_p_down)&0x001F);
            ponderation_factor++;
          }
        }

        // ---- Interpolate current and right ----
        if(right_px_missing){
          cur_p_right = p+x1+1;

          if(supposed_pond_factor==3){
            red_comp += ((*cur_p_right)&0xF800) << 1;
            green_comp += ((*cur_p_right)&0x07E0) << 1;
            blue_comp += ((*cur_p_right)&0x001F) << 1;
            ponderation_factor+=2;
          }
          else if(supposed_pond_factor>=4){
            red_comp += ((*cur_p_right)&0xF800);
            green_comp += ((*cur_p_right)&0x07E0);
            blue_comp += ((*cur_p_right)&0x001F);
            ponderation_factor++;
          }
        }

        /// --- Compute new px value ---
        if(ponderation_factor==4){
          red_comp = (red_comp >> 2)&0xF800;
          green_comp = (green_comp >> 2)&green_mask;
          blue_comp = (blue_comp >> 2)&0x001F;
        }
        else if(ponderation_factor==2){
          red_comp = (red_comp >> 1)&0xF800;
          green_comp = (green_comp >> 1)&green_mask;
          blue_comp = (blue_comp >> 1)&0x001F;
        }
        else{
          red_comp = (red_comp / ponderation_factor )&0xF800;
          green_comp = (green_comp / ponderation_factor )&green_mask;
          blue_comp = (blue_comp / ponderation_factor )&0x001F;
        }

        /// Debug
        //occurence_pond[ponderation_factor] += 1;

        /// --- write pixel ---
        *t++ = red_comp+green_comp+blue_comp;
      }
      else{
        /// --- copy pixel ---
#ifdef BLACKER_BLACKS
        /// Optimization for blacker blacks (our screen do not handle green value of 1 very well)
        *t++ = (*cur_p)&0xFFDF;
#else
        *t++ = (*cur_p);
#endif

        /// Debug
        //occurence_pond[1] += 1;
      }

      /// save number of pixels to interpolate
      px_diff_prev_x = px_diff_next_x;

      // ------ next pixel ------
      rat += x_ratio;
    }
    px_diff_prev_y = px_diff_next_y;
  }
  /// Debug
  /*printf("pond: [%d, %d, %d, %d, %d, %d]\n", occurence_pond[1], occurence_pond[2], occurence_pond[3],
                                              occurence_pond[4], occurence_pond[5], occurence_pond[6]);*/
}



/// Nearest neighbor with 2D bilinear and interpolation with left, right, up and down pixels, pseudo gaussian weighting
void flip_NNOptimized_LeftRightUpDownBilinear_Optimized8(SDL_Surface *virtual_screen, SDL_Surface *hardware_screen, int new_w, int new_h){
  int w1=virtual_screen->w;
  int h1=virtual_screen->h;
  int w2=new_w;
  int h2=new_h;
  int y_padding = (RES_HW_SCREEN_VERTICAL-new_h)/2;
  //int x_ratio = (int)((w1<<16)/w2) +1;
  //int y_ratio = (int)((h1<<16)/h2) +1;
  int x_ratio = (int)((w1<<16)/w2);
  int y_ratio = (int)((h1<<16)/h2);
  int x1, y1;

#ifdef BLACKER_BLACKS
      /// Optimization for blacker blacks (our screen do not handle green value of 1 very well)
      uint16_t green_mask = 0x07C0;
#else
      uint16_t green_mask = 0x07E0;
#endif

  /// --- Compute padding for centering when out of bounds ---
  int x_padding = 0;
  if(w2>RES_HW_SCREEN_HORIZONTAL){
    x_padding = (w2-RES_HW_SCREEN_HORIZONTAL)/2 + 1;
  }
  int x_padding_ratio = x_padding*w1/w2;

  /// --- Interp params ---
  int px_diff_prev_x = 0;
  int px_diff_next_x = 0;
  int px_diff_prev_y = 0;
  int px_diff_next_y = 0;
  uint32_t ponderation_factor;
  uint8_t left_px_missing, right_px_missing, up_px_missing, down_px_missing;
  int supposed_pond_factor;

  uint16_t * cur_p;
  uint16_t * cur_p_left;
  uint16_t * cur_p_right;
  uint16_t * cur_p_up;
  uint16_t * cur_p_down;
  uint32_t red_comp, green_comp, blue_comp;
  //printf("virtual_screen->w=%d, virtual_screen->w=%d\n", virtual_screen->w, virtual_screen->h);

  ///Debug
  /*int occurence_pond[9];
  memset(occurence_pond, 0, 9*sizeof(int));*/

  for (int i=0;i<h2;i++)
  {
    if(i>=RES_HW_SCREEN_VERTICAL){
      continue;
    }
    uint16_t* t = (uint16_t*)(hardware_screen->pixels+( (i+y_padding)*((w2>RES_HW_SCREEN_HORIZONTAL)?RES_HW_SCREEN_HORIZONTAL:w2))*sizeof(uint16_t));
    // ------ current and next y value ------
    y1 = ((i*y_ratio)>>16);
    px_diff_next_y = MAX( (((i+1)*y_ratio)>>16) - y1, 1);
    uint16_t* p = (uint16_t*)(virtual_screen->pixels + (y1*w1+x_padding_ratio) *sizeof(uint16_t));
    int rat = 0;
    for (int j=0;j<w2;j++)
    {
      if(j>=RES_HW_SCREEN_HORIZONTAL){
        continue;
      }
      // ------ current x value ------
      x1 = (rat>>16);
      px_diff_next_x = ((rat+x_ratio)>>16) - x1;

      // ------ adapted bilinear with 3x3 gaussian blur -------
      cur_p = p+x1;
      if(px_diff_prev_x > 1 || px_diff_next_x > 1 || px_diff_prev_y > 1 || px_diff_next_y > 1){
        red_comp=((*cur_p)&0xF800) << 1;
        green_comp=((*cur_p)&0x07E0) << 1;
        blue_comp=((*cur_p)&0x001F) << 1;
        ponderation_factor = 2;
        left_px_missing = (px_diff_prev_x > 1 && x1>0);
        right_px_missing = (px_diff_next_x > 1 && x1+1<w1);
        up_px_missing = (px_diff_prev_y > 1 && y1 > 0);
        down_px_missing = (px_diff_next_y > 1 && y1 + 1 < h1);
        supposed_pond_factor = 2 + left_px_missing + right_px_missing +
                                       up_px_missing + down_px_missing;

        // ---- Interpolate current and up ----
        if(up_px_missing){
          cur_p_up = p+x1-w1;

          if(supposed_pond_factor==3){
            red_comp += ((*cur_p_up)&0xF800) << 1;
            green_comp += ((*cur_p_up)&0x07E0) << 1;
            blue_comp += ((*cur_p_up)&0x001F) << 1;
            ponderation_factor+=2;
          }
          else if(supposed_pond_factor == 4 ||
                  (supposed_pond_factor == 5 && !down_px_missing) ||
                  supposed_pond_factor == 6 ){
            red_comp += ((*cur_p_up)&0xF800);
            green_comp += ((*cur_p_up)&0x07E0);
            blue_comp += ((*cur_p_up)&0x001F);
            ponderation_factor++;
          }
        }

        // ---- Interpolate current and left ----
        if(left_px_missing){
          cur_p_left = p+x1-1;

          if(supposed_pond_factor==3){
            red_comp += ((*cur_p_left)&0xF800) << 1;
            green_comp += ((*cur_p_left)&0x07E0) << 1;
            blue_comp += ((*cur_p_left)&0x001F) << 1;
            ponderation_factor+=2;
          }
          else if(supposed_pond_factor == 4 ||
                  (supposed_pond_factor == 5 && !right_px_missing) ||
                  supposed_pond_factor == 6 ){
            red_comp += ((*cur_p_left)&0xF800);
            green_comp += ((*cur_p_left)&0x07E0);
            blue_comp += ((*cur_p_left)&0x001F);
            ponderation_factor++;
          }
        }

        // ---- Interpolate current and down ----
        if(down_px_missing){
          cur_p_down = p+x1+w1;

          if(supposed_pond_factor==3 || supposed_pond_factor==6){
            red_comp += ((*cur_p_down)&0xF800) << 1;
            green_comp += ((*cur_p_down)&0x07E0) << 1;
            blue_comp += ((*cur_p_down)&0x001F) << 1;
            ponderation_factor+=2;
          }
          else if(supposed_pond_factor >= 4 && supposed_pond_factor != 6){
            red_comp += ((*cur_p_down)&0xF800);
            green_comp += ((*cur_p_down)&0x07E0);
            blue_comp += ((*cur_p_down)&0x001F);
            ponderation_factor++;
          }
        }

        // ---- Interpolate current and right ----
        if(right_px_missing){
          cur_p_right = p+x1+1;

          if(supposed_pond_factor==3 || supposed_pond_factor==6){
            red_comp += ((*cur_p_right)&0xF800) << 1;
            green_comp += ((*cur_p_right)&0x07E0) << 1;
            blue_comp += ((*cur_p_right)&0x001F) << 1;
            ponderation_factor+=2;
          }
          else if(supposed_pond_factor >= 4 && supposed_pond_factor != 6){
            red_comp += ((*cur_p_right)&0xF800);
            green_comp += ((*cur_p_right)&0x07E0);
            blue_comp += ((*cur_p_right)&0x001F);
            ponderation_factor++;
          }
        }

        /// --- Compute new px value ---
        if(ponderation_factor==8){
          red_comp = (red_comp >> 3)&0xF800;
          green_comp = (green_comp >> 3)&green_mask;
          blue_comp = (blue_comp >> 3)&0x001F;
        }
        else if(ponderation_factor==4){
          red_comp = (red_comp >> 2)&0xF800;
          green_comp = (green_comp >> 2)&green_mask;
          blue_comp = (blue_comp >> 2)&0x001F;
        }
        else if(ponderation_factor==2){
          red_comp = (red_comp >> 1)&0xF800;
          green_comp = (green_comp >> 1)&green_mask;
          blue_comp = (blue_comp >> 1)&0x001F;
        }
        else{
          red_comp = (red_comp / ponderation_factor )&0xF800;
          green_comp = (green_comp / ponderation_factor )&green_mask;
          blue_comp = (blue_comp / ponderation_factor )&0x001F;
        }

        /// Debug
        //occurence_pond[ponderation_factor] += 1;

        /// --- write pixel ---
        *t++ = red_comp+green_comp+blue_comp;
      }
      else{
        /// --- copy pixel ---
#ifdef BLACKER_BLACKS
        /// Optimization for blacker blacks (our screen do not handle green value of 1 very well)
        *t++ = (*cur_p)&0xFFDF;
#else
        *t++ = (*cur_p);
#endif

        /// Debug
        //occurence_pond[1] += 1;
      }

      /// save number of pixels to interpolate
      px_diff_prev_x = px_diff_next_x;

      // ------ next pixel ------
      rat += x_ratio;
    }
    px_diff_prev_y = px_diff_next_y;
  }
  /// Debug
  /*printf("pond: [%d, %d, %d, %d, %d, %d, %d, %d]\n", occurence_pond[1], occurence_pond[2], occurence_pond[3],
                                              occurence_pond[4], occurence_pond[5], occurence_pond[6],
                                              occurence_pond[7], occurence_pond[8]);*/
}


/// Nearest neighbor with full 2D uniform bilinear  (interpolation with missing left, right, up and down pixels)
void flip_NNOptimized_FullBilinear_Uniform(SDL_Surface *virtual_screen, SDL_Surface *hardware_screen, int new_w, int new_h){
  int w1=virtual_screen->w;
  int h1=virtual_screen->h;
  int w2=new_w;
  int h2=new_h;
  int y_padding = (RES_HW_SCREEN_VERTICAL-new_h)/2;
  //int x_ratio = (int)((w1<<16)/w2) +1;
  //int y_ratio = (int)((h1<<16)/h2) +1;
  int x_ratio = (int)((w1<<16)/w2);
  int y_ratio = (int)((h1<<16)/h2);
  int x1, y1;
  int px_diff_prev_x = 1;
  int px_diff_prev_y = 1;
  //int cnt_interp = 0; int cnt_no_interp = 0;
  //printf("virtual_screen->w=%d, virtual_screen->w=%d\n", virtual_screen->w, virtual_screen->h);

  /// ---- Compute padding for centering when out of bounds ----
  int x_padding = 0;
  if(w2>RES_HW_SCREEN_HORIZONTAL){
    x_padding = (w2-RES_HW_SCREEN_HORIZONTAL)/2 + 1;
  }
  int x_padding_ratio = x_padding*w1/w2;

  /// ---- Copy and interpolate pixels ----
  for (int i=0;i<h2;i++)
  {
    if(i>=RES_HW_SCREEN_VERTICAL){
      continue;
    }

    uint16_t* t = (uint16_t*)(hardware_screen->pixels+( (i+y_padding)*((w2>RES_HW_SCREEN_HORIZONTAL)?RES_HW_SCREEN_HORIZONTAL:w2))*sizeof(uint16_t));

    // ------ current and next y value ------
    y1 = ((i*y_ratio)>>16);
    int px_diff_next_y = MAX( (((i+1)*y_ratio)>>16) - y1, 1);

    uint16_t* p = (uint16_t*)(virtual_screen->pixels + (y1*w1 + x_padding_ratio) *sizeof(uint16_t));
    int rat = 0;
    for (int j=0;j<w2;j++)
    {
      if(j>=RES_HW_SCREEN_HORIZONTAL){
        continue;
      }

      // ------ current and next x value ------
      x1 = (rat>>16);
      int px_diff_next_x = MAX( ((rat+x_ratio)>>16) - x1, 1);

      // ------ bilinear uniformly weighted --------
      uint32_t red_comp=0, green_comp=0, blue_comp=0, ponderation_factor=0;
      uint16_t * cur_p;
      int cur_y_offset;

      //printf("\npx_diff_prev_y=%d, px_diff_prev_x=%d, px_diff_next_y=%d, px_diff_next_x=%d, interp_px=", px_diff_prev_y, px_diff_prev_x, px_diff_next_y, px_diff_next_x);

      for(int cur_px_diff_y=-(px_diff_prev_y-1); cur_px_diff_y<px_diff_next_y; cur_px_diff_y++){
        if(y1 + cur_px_diff_y >= h1 || y1 < -cur_px_diff_y){
          continue;
        }
        cur_y_offset = w1*cur_px_diff_y;
        //printf("cur_diff_y=%d-> ", cur_px_diff_y);

        for(int cur_px_diff_x=-(px_diff_prev_x-1); cur_px_diff_x<px_diff_next_x; cur_px_diff_x++){
          if(x1 + cur_px_diff_x >= w1 || x1 < -cur_px_diff_x){
            continue;
          }
          cur_p = (p+cur_y_offset+x1+cur_px_diff_x);
          //printf("{y=%d,x=%d}, ", y1+cur_px_diff_y, x1+cur_px_diff_x);
          red_comp += ((*cur_p)&0xF800);
          green_comp += ((*cur_p)&0x07E0);
          blue_comp += ((*cur_p)&0x001F);
          ponderation_factor++;
        }
      }
      //printf("\n");

      /// ------ Ponderation -------
      red_comp = (red_comp / ponderation_factor )&0xF800;
#ifdef BLACKER_BLACKS
      /// Optimization for blacker blacks (our screen do not handle green value of 1 very well)
      green_comp = (green_comp / ponderation_factor )&0x07C0;
#else
      green_comp = (green_comp / ponderation_factor )&0x07E0;
#endif
      blue_comp = (blue_comp / ponderation_factor )&0x001F;
      *t++ = red_comp+green_comp+blue_comp;

      /// ------ x Interpolation values -------
      px_diff_prev_x = px_diff_next_x;

      // ------ next pixel ------
      rat += x_ratio;
    }

    /// ------ y Interpolation values -------
    px_diff_prev_y = px_diff_next_y;
  }
  //printf("cnt_interp = %d, int cnt_no_interp = %d\n", cnt_interp, cnt_no_interp);
}


/// Nearest neighbor with full 2D uniform bilinear  (interpolation with missing left, right, up and down pixels)
void flip_NNOptimized_FullBilinear_GaussianWeighted(SDL_Surface *virtual_screen, SDL_Surface *hardware_screen, int new_w, int new_h){
  int w1=virtual_screen->w;
  int h1=virtual_screen->h;
  int w2=new_w;
  int h2=new_h;
  //printf("virtual_screen->w=%d, virtual_screen->w=%d\n", virtual_screen->w, virtual_screen->h);
  int y_padding = (RES_HW_SCREEN_VERTICAL-new_h)/2;
  int x_ratio = (int)((w1<<16)/w2);
  int y_ratio = (int)((h1<<16)/h2);
  int x1, y1;
  int px_diff_prev_x = 1;
  int px_diff_prev_y = 1;
  //int cnt_interp = 0; int cnt_no_interp = 0;

  /// ---- Compute padding for centering when out of bounds ----
  int x_padding = 0;
  if(w2>RES_HW_SCREEN_HORIZONTAL){
    x_padding = (w2-RES_HW_SCREEN_HORIZONTAL)/2 + 1;
  }
  int x_padding_ratio = x_padding*w1/w2;

  /// ---- Interpolation params ----
  uint32_t max_pix_interpolate = 3;
  if(max_pix_interpolate > 3 || max_pix_interpolate<1){
    printf("ERROR cannot interpolate more than 3x3 px in flip_NNOptimized_FullBilinear_GaussianWeighted\n");
    return;
  }

  /// ---- Convolutional mask ----
  int mask_weight_5x5[] = {36, 24, 6,   24, 16, 4,    6, 4, 1};
  int mask_weight_3x3[] = {4, 2,  2, 1};
  int mask_weight_1x1[] = {1};
  int *mask_weight;
  if(max_pix_interpolate==3){
    mask_weight = mask_weight_5x5;
  }
  else if(max_pix_interpolate==2){
    mask_weight = mask_weight_3x3;
  }
  else{
    mask_weight = mask_weight_1x1;
  }

  /// ---- Copy and interpolate pixels ----
  for (int i=0;i<h2;i++)
  {
    if(i>=RES_HW_SCREEN_VERTICAL){
      continue;
    }

    uint16_t* t = (uint16_t*)(hardware_screen->pixels+( (i+y_padding)*((w2>RES_HW_SCREEN_HORIZONTAL)?RES_HW_SCREEN_HORIZONTAL:w2))*sizeof(uint16_t));

    // ------ current and next y value ------
    y1 = ((i*y_ratio)>>16);
    int px_diff_next_y = MIN( MAX( (((i+1)*y_ratio)>>16) - y1, 1), max_pix_interpolate);

    uint16_t* p = (uint16_t*)(virtual_screen->pixels + (y1*w1 + x_padding_ratio) *sizeof(uint16_t));
    int rat = 0;
    for (int j=0;j<w2;j++)
    {
      if(j>=RES_HW_SCREEN_HORIZONTAL){
        continue;
      }

      // ------ current and next x value ------
      x1 = (rat>>16);
      int px_diff_next_x = MIN( MAX( ((rat+x_ratio)>>16) - x1, 1), max_pix_interpolate); //we interpolate max "max_pix_interpolate" pix in each dim

      // ------ bilinear uniformly weighted --------
      uint32_t red_comp=0, green_comp=0, blue_comp=0;
      int ponderation_factor=0;
      uint16_t * cur_p;
      int cur_y_offset;

      //printf("\npx_diff_prev_y=%d, px_diff_prev_x=%d, px_diff_next_y=%d, px_diff_next_x=%d, interp_px=", px_diff_prev_y, px_diff_prev_x, px_diff_next_y, px_diff_next_x);

      for(int cur_px_diff_y=-(px_diff_prev_y-1); cur_px_diff_y<px_diff_next_y; cur_px_diff_y++){
        if(y1 + cur_px_diff_y >= h1 || y1 < -cur_px_diff_y){
          continue;
        }
        cur_y_offset = w1*cur_px_diff_y;
        //printf("cur_diff_y=%d-> ", cur_px_diff_y);

        for(int cur_px_diff_x=-(px_diff_prev_x-1); cur_px_diff_x<px_diff_next_x; cur_px_diff_x++){
          if(x1 + cur_px_diff_x >= w1 || x1 < -cur_px_diff_x){
            continue;
          }
          cur_p = (p+cur_y_offset+x1+cur_px_diff_x);
          int weight = mask_weight[ABS(cur_px_diff_y)*max_pix_interpolate+ABS(cur_px_diff_x)];

          red_comp += ((*cur_p)&0xF800) * weight;
          green_comp += ((*cur_p)&0x07E0) * weight;
          blue_comp += ((*cur_p)&0x001F) * weight;
          ponderation_factor += weight;
        }
      }
      //printf("\n");

      /// ------ Ponderation -------
      red_comp = (red_comp / ponderation_factor) & 0xF800;
#ifdef BLACKER_BLACKS
      /// Optimization for blacker blacks (our screen do not handle green value of 1 very well)
      green_comp = (green_comp / ponderation_factor )&0x07C0;
#else
      green_comp = (green_comp / ponderation_factor )&0x07E0;
#endif
      blue_comp = (blue_comp / ponderation_factor) & 0x001F;
      *t++ = red_comp+green_comp+blue_comp;

      /// ------ x Interpolation values -------
      px_diff_prev_x = px_diff_next_x;

      // ------ next pixel ------
      rat += x_ratio;
    }

    /// ------ y Interpolation values -------
    px_diff_prev_y = px_diff_next_y;
  }
  //printf("cnt_interp = %d, int cnt_no_interp = %d\n", cnt_interp, cnt_no_interp);
}


/// Downscaling with full 2D Gaussian weight on horizontal and NN on vertical
void flip_NNOptimized_LeftRightBilinear_GaussianWeighted(SDL_Surface *virtual_screen, SDL_Surface *hardware_screen, int new_w, int new_h){
  int w1=virtual_screen->w;
  int h1=virtual_screen->h;
  int w2=new_w;
  int h2=new_h;
  //printf("virtual_screen->w=%d, virtual_screen->w=%d\n", virtual_screen->w, virtual_screen->h);
  int y_padding = (RES_HW_SCREEN_VERTICAL-new_h)/2;
  int x_ratio = (int)((w1<<16)/w2);
  int y_ratio = (int)((h1<<16)/h2);
  int x1, y1;
  //int cnt_interp = 0; int cnt_no_interp = 0;

  /// ---- Compute padding for centering when out of bounds ----
  int x_padding = 0;
  if(w2>RES_HW_SCREEN_HORIZONTAL){
    x_padding = (w2-RES_HW_SCREEN_HORIZONTAL)/2 + 1;
  }
  int x_padding_ratio = x_padding*w1/w2;

  /// ---- Interpolation params ----
  uint32_t max_pix_interpolate = 3;
  if(max_pix_interpolate > 3 || max_pix_interpolate<1){
    printf("ERROR cannot interpolate more than 3x3 px in flip_NNOptimized_FullBilinear_GaussianWeighted\n");
    return;
  }

  /// ---- Convolutional mask ----
  int mask_weight_5x5[] = {6, 4, 1};
  int mask_weight_3x3[] = {2, 1};
  int mask_weight_1x1[] = {1};
  int *mask_weight;
  if(max_pix_interpolate==3){
    mask_weight = mask_weight_5x5;
  }
  else if(max_pix_interpolate==2){
    mask_weight = mask_weight_3x3;
  }
  else{
    mask_weight = mask_weight_1x1;
  }

  /// ---- Copy and interpolate pixels ----
  for (int i=0;i<h2;i++)
  {
    if(i>=RES_HW_SCREEN_VERTICAL){
      continue;
    }

    uint16_t* t = (uint16_t*)(hardware_screen->pixels+( (i+y_padding)*((w2>RES_HW_SCREEN_HORIZONTAL)?RES_HW_SCREEN_HORIZONTAL:w2))*sizeof(uint16_t));

    // ------ current and next y value ------
    y1 = ((i*y_ratio)>>16);

    uint16_t* p = (uint16_t*)(virtual_screen->pixels + (y1*w1 + x_padding_ratio) *sizeof(uint16_t));
    int rat = 0;
    for (int j=0;j<w2;j++)
    {
      if(j>=RES_HW_SCREEN_HORIZONTAL){
        continue;
      }

      // ------ current and next x value ------
      x1 = (rat>>16);
      int px_diff_next_x = MIN( MAX( ((rat+x_ratio)>>16) - x1, 1), max_pix_interpolate); //we interpolate max "max_pix_interpolate" pix in each dim

      // ------ bilinear uniformly weighted --------
      uint32_t red_comp=0, green_comp=0, blue_comp=0;
      int ponderation_factor=0;
      uint16_t * cur_p;

      //printf("\npx_diff_prev_y=%d, px_diff_prev_x=%d, px_diff_next_y=%d, px_diff_next_x=%d, interp_px=", px_diff_prev_y, px_diff_prev_x, px_diff_next_y, px_diff_next_x);



        for(int cur_px_diff_x=0; cur_px_diff_x<px_diff_next_x; cur_px_diff_x++){
          if(x1 + cur_px_diff_x >= w1 || x1 < -cur_px_diff_x){
            continue;
          }
          cur_p = (p+x1+cur_px_diff_x);
          int weight = mask_weight[ABS(cur_px_diff_x)];

          red_comp += ((*cur_p)&0xF800) * weight;
          green_comp += ((*cur_p)&0x07E0) * weight;
          blue_comp += ((*cur_p)&0x001F) * weight;
          ponderation_factor += weight;
        }
      //printf("\n");

      /// ------ Ponderation -------
      red_comp = (red_comp / ponderation_factor) & 0xF800;
      green_comp = (green_comp / ponderation_factor )&0x07E0;
      blue_comp = (blue_comp / ponderation_factor) & 0x001F;
      *t++ = red_comp+green_comp+blue_comp;

      // ------ next pixel ------
      rat += x_ratio;
    }
  }
  //printf("cnt_interp = %d, int cnt_no_interp = %d\n", cnt_interp, cnt_no_interp);
}




/// Interpolation with left, right pixels, pseudo gaussian weighting for downscaling - operations on 16bits
void flip_Downscale_LeftRightGaussianFilter_Optimized(SDL_Surface *src_surface, SDL_Surface *dst_surface, int new_w, int new_h){
  int w1=src_surface->w;
  int h1=src_surface->h;
  int w2=dst_surface->w;
  int h2=dst_surface->h;
  //printf("src = %dx%d\n", w1, h1);
  int x_ratio = (int)((w1<<16)/w2);
  int y_ratio = (int)((h1<<16)/h2);
  int y_padding = (RES_HW_SCREEN_VERTICAL-h2)/2;
  int x1, y1;
  uint16_t *src_screen = (uint16_t *)src_surface->pixels;
  uint16_t *dst_screen = (uint16_t *)dst_surface->pixels;

  /// --- Compute padding for centering when out of bounds ---
  int x_padding = 0;
  if(w2>RES_HW_SCREEN_HORIZONTAL){
    x_padding = (w2-RES_HW_SCREEN_HORIZONTAL)/2 + 1;
  }
  int x_padding_ratio = x_padding*w1/w2;

  /// --- Interp params ---
  int px_diff_prev_x = 0;
  int px_diff_next_x = 0;
  uint8_t left_px_missing, right_px_missing;

  uint16_t * cur_p;
  uint16_t * cur_p_left;
  uint16_t * cur_p_right;


  for (int i=0;i<h2;i++)
  {
    if(i>=RES_HW_SCREEN_VERTICAL){
      continue;
    }
    uint16_t* t = (uint16_t*)(dst_screen +
      (i+y_padding)*((w2>RES_HW_SCREEN_HORIZONTAL)?RES_HW_SCREEN_HORIZONTAL:w2) );
    // ------ current and next y value ------
    y1 = ((i*y_ratio)>>16);
    uint16_t* p = (uint16_t*)(src_screen + (y1*w1+x_padding_ratio) );
    int rat = 0;

    for (int j=0;j<w2;j++)
    {
      if(j>=RES_HW_SCREEN_HORIZONTAL){
        continue;
      }

      // ------ current x value ------
      x1 = (rat>>16);
      px_diff_next_x = ((rat+x_ratio)>>16) - x1;

      //printf("x1=%d, px_diff_prev_x=%d, px_diff_next_x=%d\n", x1, px_diff_prev_x, px_diff_next_x);

      // ------ adapted bilinear with 3x3 gaussian blur -------
      cur_p = p+x1;
      if(px_diff_prev_x > 1 || px_diff_next_x > 1 ){

        left_px_missing = (px_diff_prev_x > 1 && x1>0);
        right_px_missing = (px_diff_next_x > 1 && x1+1<w1);
        cur_p_left = cur_p-1;
        cur_p_right = cur_p+1;

        // ---- Interpolate current and left ----
        if(left_px_missing && !right_px_missing){
          *t++ = Weight1_1(*cur_p, *cur_p_left);
          //*t++ = Weight1_1(*cur_p, Weight1_3(*cur_p, *cur_p_left));
        }
        // ---- Interpolate current and right ----
        else if(right_px_missing && !left_px_missing){
          *t++ = Weight1_1(*cur_p, *cur_p_right);
          //*t++ = Weight1_1(*cur_p, Weight1_3(*cur_p, *cur_p_right));
        }
        // ---- Interpolate with Left and right pixels
        else{
          *t++ = Weight1_1(Weight1_1(*cur_p, *cur_p_left), Weight1_1(*cur_p, *cur_p_right));
        }

      }
      else{
        /// --- copy pixel ---
        *t++ = (*cur_p);

        /// Debug
        //occurence_pond[1] += 1;
      }

      /// save number of pixels to interpolate
      px_diff_prev_x = px_diff_next_x;

      // ------ next pixel ------
      rat += x_ratio;
    }
  }
}







/// Interpolation with left, right pixels, pseudo gaussian weighting for downscaling - operations on 16bits - forced on all pixels
void flip_Downscale_LeftRight_Optimized_Forced(SDL_Surface *src_surface, SDL_Surface *dst_surface, int new_w, int new_h){
  int w1=src_surface->w;
  int h1=src_surface->h;
  int w2=dst_surface->w;
  int h2=dst_surface->h;
  int x_ratio = (int)((w1<<16)/w2);
  int y_ratio = (int)((h1<<16)/h2);
  int y_padding = (RES_HW_SCREEN_VERTICAL-h2)/2;
  int x1, y1;
  uint16_t *src_screen = (uint16_t *)src_surface->pixels;
  uint16_t *dst_screen = (uint16_t *)dst_surface->pixels;

  /// --- Compute padding for centering when out of bounds ---
  int x_padding = 0;
  if(w2>RES_HW_SCREEN_HORIZONTAL){
    x_padding = (w2-RES_HW_SCREEN_HORIZONTAL)/2 + 1;
  }
  int x_padding_ratio = x_padding*w1/w2;

  /// --- Interp params ---
  uint16_t * cur_p;
  uint16_t * cur_p_left;
  uint16_t * cur_p_right;
  uint16_t* t;
  uint16_t* p;
  int rat;


  for (int i=0;i<h2;i++)
  {
    t = (uint16_t*)(dst_screen +
      (i+y_padding)*((w2>RES_HW_SCREEN_HORIZONTAL)?RES_HW_SCREEN_HORIZONTAL:w2) );

    // ------ current and next y value ------
    y1 = ((i*y_ratio)>>16);
    p = (uint16_t*)(src_screen + (y1*w1+x_padding_ratio) );
    rat = 0;

    for (int j=0;j<w2;j++)
    {

      // ------ current x value ------
      x1 = (rat>>16);

      // ------ adapted bilinear with 3x3 gaussian blur -------
      cur_p = p+x1+1;
      cur_p_left = cur_p - 1;
      cur_p_right = cur_p + 1;

      // ---- Interpolate with Left and right pixels
      *t++ = Weight1_1(Weight1_1(*cur_p, *cur_p_left), Weight1_1(*cur_p, *cur_p_right));

      // ------ next pixel ------
      rat += x_ratio;
    }
  }
}







/// Interpolation with left, right pixels, pseudo gaussian weighting for downscaling - operations on 16bits
void flip_Downscale_LeftRightGaussianFilter_OptimizedWidth320(SDL_Surface *src_surface, SDL_Surface *dst_surface, int new_w, int new_h){
  int w1=src_surface->w;
  int h1=src_surface->h;
  int w2=dst_surface->w;
  int h2=dst_surface->h;

  if(w1!=320){
    printf("src_surface->w (%d) != 320\n", src_surface->w);
    return;
  }

  //printf("src = %dx%d\n", w1, h1);
  int y_ratio = (int)((h1<<16)/h2);
  int y_padding = (RES_HW_SCREEN_VERTICAL-h2)/2;
  int y1;
  uint16_t *src_screen = (uint16_t *)src_surface->pixels;
  uint16_t *dst_screen = (uint16_t *)dst_surface->pixels;

  /* Interpolation */
  for (int i=0;i<h2;i++)
  {
    if(i>=RES_HW_SCREEN_VERTICAL){
      continue;
    }
    uint16_t* t = (uint16_t*)(dst_screen +
      (i+y_padding)*((w2>RES_HW_SCREEN_HORIZONTAL)?RES_HW_SCREEN_HORIZONTAL:w2) );

    // ------ current and next y value ------
    y1 = ((i*y_ratio)>>16);
    uint16_t* p = (uint16_t*)(src_screen + (y1*w1) );

    for (int j=0;j<80;j++)
    {
      /* Horizontaly:
       * Before(4):
       * (a)(b)(c)(d)
       * After(3):
       * (aaab)(bc)(cddd)
       */
      uint16_t _a = *(p    );
      uint16_t _b = *(p + 1);
      uint16_t _c = *(p + 2);
      uint16_t _d = *(p + 3);
      *(t    ) = Weight3_1( _a, _b );
      *(t + 1) = Weight1_1( _b, _c );
      *(t + 2) = Weight1_3( _c, _d );

      // ------ next dst pixel ------
      t+=3;
      p+=4;
    }
  }
}





/// Interpolation with left, right pixels, pseudo gaussian weighting for downscaling - operations on 16bits
void flip_Downscale_OptimizedWidth320_mergeUpDown(SDL_Surface *src_surface, SDL_Surface *dst_surface, int new_w, int new_h){
  int w1=src_surface->w;
  int h1=src_surface->h;
  int w2=dst_surface->w;
  int h2=dst_surface->h;

  if(w1!=320){
    printf("src_surface->w (%d) != 320\n", src_surface->w);
    return;
  }

  //printf("src = %dx%d\n", w1, h1);
  int y_ratio = (int)((h1<<16)/h2);
  int y_padding = (RES_HW_SCREEN_VERTICAL-h2)/2;
  int y1=0, prev_y1=-1, prev_prev_y1=-2;
  uint16_t *src_screen = (uint16_t *)src_surface->pixels;
  uint16_t *dst_screen = (uint16_t *)dst_surface->pixels;

  uint16_t *prev_t, *t_init=dst_screen;

  /* Interpolation */
  for (int i=0;i<h2;i++)
  {
    if(i>=RES_HW_SCREEN_VERTICAL){
      continue;
    }

    prev_t = t_init;
    t_init = (uint16_t*)(dst_screen +
      (i+y_padding)*((w2>RES_HW_SCREEN_HORIZONTAL)?RES_HW_SCREEN_HORIZONTAL:w2) );
    uint16_t *t = t_init;

    // ------ current and next y value ------
    prev_prev_y1 = prev_y1;
    prev_y1 = y1;
    y1 = ((i*y_ratio)>>16);

    uint16_t* p = (uint16_t*)(src_screen + (y1*w1) );

    for (int j=0;j<80;j++)
    {
      /* Horizontaly:
       * Before(4):
       * (a)(b)(c)(d)
       * After(3):
       * (aaab)(bc)(cddd)
       */
      uint16_t _a = *(p    );
      uint16_t _b = *(p + 1);
      uint16_t _c = *(p + 2);
      uint16_t _d = *(p + 3);
      *(t    ) = Weight3_1( _a, _b );
      *(t + 1) = Weight1_1( _b, _c );
      *(t + 2) = Weight1_3( _c, _d );

      if(prev_y1 == prev_prev_y1 && y1 != prev_y1){
        //printf("we are here %d\n", ++count);
        *(prev_t    ) = Weight1_1(*(t    ), *(prev_t    ));
        *(prev_t + 1) = Weight1_1(*(t + 1), *(prev_t + 1));
        *(prev_t + 2) = Weight1_1(*(t + 2), *(prev_t + 2));
      }


      // ------ next dst pixel ------
      t+=3;
      prev_t+=3;
      p+=4;
    }
  }
}





void SDL_Copy_Rotate_270(uint16_t *source_pixels, uint16_t *dest_pixels,
                int src_w, int src_h, int dst_w, int dst_h){
  int i, j;

    /// --- Checking if same dimensions ---
    if(dst_w != src_w || dst_h != src_h){
      printf("Error in SDL_Rotate_270, dest_pixels (%dx%d) and source_pixels (%dx%d) have different dimensions\n",
        dst_w, dst_h, src_w, src_h);
      return;
    }

  /// --- Pixel copy and rotation (270) ---
  uint16_t *cur_p_src, *cur_p_dst;
  for(i=0; i<src_h; i++){
    for(j=0; j<src_w; j++){
      cur_p_src = source_pixels + i*src_w + j;
      cur_p_dst = dest_pixels + (dst_h-1-j)*dst_w + i;
      *cur_p_dst = *cur_p_src;
    }
  }
}


void *plat_gvideo_flip(void)
{
  if (plat_sdl_overlay != NULL) {
    SDL_Rect dstrect = { 0, 0, plat_sdl_screen->w, plat_sdl_screen->h };
    SDL_DisplayYUVOverlay(plat_sdl_overlay, &dstrect);
    return NULL;
  }
  else if (plat_sdl_gl_active) {
    gl_flip(shadow_fb, psx_w, psx_h);
    return shadow_fb;
  }
  else {

    if ( SDL_MUSTLOCK(hw_screen) ) {
        SDL_UnlockSurface(hw_screen);
    }

    //printf("w:%d,h:%d\n", plat_sdl_screen->w, plat_sdl_screen->h);

    /* Lock the screen for direct access to the pixels */
    /*if ( SDL_MUSTLOCK(hw_screen) ) {
        if ( SDL_LockSurface(hw_screen) < 0 ) {
            fprintf(stderr, "Can't lock screen: %s\n", SDL_GetError());
            return NULL;
        }
    }*/

    /// --------------Optimized Flip depending on aspect ratio -------------
    static int prev_aspect_ratio;
    if(prev_aspect_ratio != aspect_ratio || need_screen_cleared){
      clear_hw_screen(hw_screen, 0);
      prev_aspect_ratio = aspect_ratio;
      need_screen_cleared = 0;
    }


#define DEBUG_RES
#ifdef DEBUG_RES
    static int prev_w = 0;
    static int prev_h = 0;
    if(prev_w != plat_sdl_screen->w || prev_h != plat_sdl_screen->h){
      printf("New game resolution: %dx%d\n", plat_sdl_screen->w, plat_sdl_screen->h);
      prev_w = plat_sdl_screen->w;
      prev_h = plat_sdl_screen->h;
    }
#endif

    switch(aspect_ratio){
      case ASPECT_RATIOS_TYPE_STRECHED:
      /*flip_NNOptimized_AllowOutOfScreen(plat_sdl_screen, hw_screen,
        RES_HW_SCREEN_HORIZONTAL, RES_HW_SCREEN_VERTICAL);*/
      /*flip_NNOptimized_LeftRightUpDownBilinear_Optimized8(plat_sdl_screen, hw_screen,
        RES_HW_SCREEN_HORIZONTAL, RES_HW_SCREEN_VERTICAL);*/
      /*flip_NNOptimized_LeftAndRightBilinear(plat_sdl_screen, hw_screen,
        RES_HW_SCREEN_HORIZONTAL, RES_HW_SCREEN_VERTICAL);*/
      /*flip_Downscale_LeftRightGaussianFilter(plat_sdl_screen, hw_screen,
        RES_HW_SCREEN_HORIZONTAL, RES_HW_SCREEN_VERTICAL);*/
      /*flip_Downscale_LeftRightGaussianFilter_Optimized(plat_sdl_screen, hw_screen,
        RES_HW_SCREEN_HORIZONTAL, RES_HW_SCREEN_VERTICAL);*/
      /*flip_NNOptimized_LeftRightBilinear_GaussianWeighted(plat_sdl_screen, hw_screen,
        RES_HW_SCREEN_HORIZONTAL, RES_HW_SCREEN_VERTICAL);*/

      if(plat_sdl_screen->w == 320 && plat_sdl_screen->h < RES_HW_SCREEN_VERTICAL){
        flip_Downscale_OptimizedWidth320_mergeUpDown(plat_sdl_screen, hw_screen,
          RES_HW_SCREEN_HORIZONTAL, RES_HW_SCREEN_VERTICAL);
      }
      else if(plat_sdl_screen->w == 320){
        flip_Downscale_LeftRightGaussianFilter_OptimizedWidth320(plat_sdl_screen, hw_screen,
          RES_HW_SCREEN_HORIZONTAL, RES_HW_SCREEN_VERTICAL);
      }
      else{
        flip_Downscale_LeftRightGaussianFilter_Optimized(plat_sdl_screen, hw_screen,
          RES_HW_SCREEN_HORIZONTAL, RES_HW_SCREEN_VERTICAL);
        /*flip_Downscale_LeftRightGaussianFilter(plat_sdl_screen, hw_screen,
          RES_HW_SCREEN_HORIZONTAL, RES_HW_SCREEN_VERTICAL);*/
      }
      break;
      case ASPECT_RATIOS_TYPE_MANUAL:
      ;uint32_t h_scaled = MIN(plat_sdl_screen->h*RES_HW_SCREEN_HORIZONTAL/plat_sdl_screen->w,
                              RES_HW_SCREEN_VERTICAL);
      uint32_t h_zoomed = MIN(h_scaled + aspect_ratio_factor_percent*(RES_HW_SCREEN_VERTICAL - h_scaled)/100,
                              RES_HW_SCREEN_VERTICAL);
      flip_NNOptimized_LeftRightUpDownBilinear_Optimized8(plat_sdl_screen, hw_screen,
          MAX(plat_sdl_screen->w*h_zoomed/plat_sdl_screen->h, RES_HW_SCREEN_HORIZONTAL),
          MIN(h_zoomed, RES_HW_SCREEN_VERTICAL));
      break;
      case ASPECT_RATIOS_TYPE_CROPPED:
      flip_NNOptimized_AllowOutOfScreen(plat_sdl_screen, hw_screen,
        MAX(plat_sdl_screen->w*RES_HW_SCREEN_VERTICAL/plat_sdl_screen->h, RES_HW_SCREEN_HORIZONTAL),
        RES_HW_SCREEN_VERTICAL);
      break;
      case ASPECT_RATIOS_TYPE_SCALED:
      flip_NNOptimized_LeftRightUpDownBilinear_Optimized8(plat_sdl_screen, hw_screen,
        RES_HW_SCREEN_HORIZONTAL,
        MIN(plat_sdl_screen->h*RES_HW_SCREEN_HORIZONTAL/plat_sdl_screen->w, RES_HW_SCREEN_VERTICAL));
      break;
      default:
      printf("Wrong aspect ratio value: %d\n", aspect_ratio);
      aspect_ratio = ASPECT_RATIOS_TYPE_STRECHED;
      flip_NNOptimized_LeftRightUpDownBilinear_Optimized8(plat_sdl_screen, hw_screen,
        RES_HW_SCREEN_HORIZONTAL, RES_HW_SCREEN_VERTICAL);
      break;
    }

    /// Copy pixels and rotate
    /*memcpy(hw_screen->pixels, virtual_hw_screen->pixels, hw_screen->h*hw_screen->w*sizeof(uint16_t));*/
    /*SDL_Copy_Rotate_270(virtual_hw_screen->pixels, hw_screen->pixels,
                        virtual_hw_screen->w, virtual_hw_screen->h, hw_screen->w, hw_screen->h);*/

    /// Flip Display
    SDL_Flip(hw_screen);

    if ( SDL_MUSTLOCK(hw_screen) ) {
        SDL_LockSurface(hw_screen);
    }

    return plat_sdl_screen->pixels;
  }
}

void plat_gvideo_close(void)
{
}

void plat_video_menu_enter(int is_rom_loaded)
{
  int force_mode_change = 0;

  in_menu = 1;

  /* surface will be lost, must adjust pl_vout_buf for menu bg */
  if (plat_sdl_overlay != NULL)
    uyvy_to_rgb565(menubg_img, plat_sdl_overlay->pixels[0], psx_w * psx_h);
  else if (plat_sdl_gl_active)
    memcpy(menubg_img, shadow_fb, psx_w * psx_h * 2);
  else
    memcpy(menubg_img, plat_sdl_screen->pixels, psx_w * psx_h * 2);
  pl_vout_buf = menubg_img;

  /* gles plugin messes stuff up.. */
  if (pl_rearmed_cbs.gpu_caps & GPU_CAP_OWNS_DISPLAY)
    force_mode_change = 1;

  change_video_mode(force_mode_change);
}

void plat_video_menu_begin(void)
{
  if (plat_sdl_overlay != NULL || plat_sdl_gl_active) {
    g_menuscreen_ptr = shadow_fb;
  }
  else {
    SDL_LockSurface(plat_sdl_screen);
    g_menuscreen_ptr = plat_sdl_screen->pixels;
  }
}

void plat_video_menu_end(void)
{
  if (plat_sdl_overlay != NULL) {
    printf("In plat_video_menu_end and plat_sdl_overlay != NULL\n");
    SDL_Rect dstrect = { 0, 0, plat_sdl_screen->w, plat_sdl_screen->h };

    SDL_LockYUVOverlay(plat_sdl_overlay);
    rgb565_to_uyvy(plat_sdl_overlay->pixels[0], shadow_fb,
      g_menuscreen_w * g_menuscreen_h);
    SDL_UnlockYUVOverlay(plat_sdl_overlay);

    SDL_DisplayYUVOverlay(plat_sdl_overlay, &dstrect);
  }
  else if (plat_sdl_gl_active) {
    gl_flip(g_menuscreen_ptr, g_menuscreen_w, g_menuscreen_h);
  }
  else {
    SDL_UnlockSurface(plat_sdl_screen);

    printf("In plat_video_menu_end - else\n");

    /// Nearest optimized
    int w1=plat_sdl_screen->w;
    //int h1=plat_sdl_screen->h;
    int w2=RES_HW_SCREEN_HORIZONTAL;
    int h2=RES_HW_SCREEN_VERTICAL;
    int x_ratio = (int)((plat_sdl_screen->w<<16)/w2) +1;
    int y_ratio = (int)((plat_sdl_screen->h<<16)/h2) +1;
    //int x_ratio = (int)((plat_sdl_screen->w<<16)/w2);
    //int y_ratio = (int)((plat_sdl_screen->h<<16)/h2);
    int x2, y2 ;
    for (int i=0;i<h2;i++){
      uint16_t* t = (uint16_t*)(hw_screen->pixels+(i*w2)*sizeof(uint16_t));
      y2 = ((i*y_ratio)>>16);
      uint16_t* p = (uint16_t*)(plat_sdl_screen->pixels + (y2*w1) *sizeof(uint16_t));
      int rat = 0;
      for (int j=0;j<w2;j++) {
	x2 = (rat>>16);
	*t++ = p[x2];
	rat += x_ratio;
      }
    }

    SDL_Flip(hw_screen);
  }
  g_menuscreen_ptr = NULL;
}

void plat_video_menu_leave(void)
{
  in_menu = 0;
}

/* unused stuff */
void *plat_prepare_screenshot(int *w, int *h, int *bpp)
{
  return 0;
}

void plat_trigger_vibrate(int pad, int low, int high)
{
}

void plat_minimize(void)
{
}

// vim:shiftwidth=2:expandtab















//****************************************************
//************ OTHER INTERPOLATION CODE **************
//****************************************************
/// Nearest neighboor with Bresenham and interp by the number of pixel diff, not 2
#if 0
    int w1=plat_sdl_screen->w;
    int h1=plat_sdl_screen->h;
    int w2=240;
    int h2=240;
    int FractPart_w = w1 % w2;
    int FractPart_h = h1 % h2;
  //int Mid_w2 = w2/2;
  int Mid_w2 = 0; /// Force X interpolation
  int Mid_h2 = h2/2;
  uint8_t y_interp = 0;
    //int x_ratio = (int)((w1<<16)/w2) +1;
  //int y_ratio = (int)((h1<<16)/h2) +1;
    int x_ratio = (int)((w1<<16)/w2);
  int y_ratio = (int)((h1<<16)/h2);
  int x1, y1, y1_below;
  int E_y = 0;
  int E_x = 0;
  /*int cnt_yes_x_yes_y, cnt_yes_x_no_y, cnt_no_x_yes_y, cnt_no_x_no_y;
  cnt_yes_x_yes_y= cnt_yes_x_no_y= cnt_no_x_yes_y= cnt_no_x_no_y = 0;*/
    for (int i=0;i<h2;i++)
  {
    uint16_t* t = (uint16_t*)(hw_screen->pixels+(i*w2)*sizeof(uint16_t));
    y1 = ((i*y_ratio)>>16);
    y1_below = (y1<h1-1)?y1+1:y1;
    uint16_t* p = (uint16_t*)(plat_sdl_screen->pixels + (y1*w1) *sizeof(uint16_t));
    uint16_t* p_below = (uint16_t*)(plat_sdl_screen->pixels + (y1_below*w1) *sizeof(uint16_t));
    int rat = 0;
    E_x = 0;
    if (E_y >= Mid_h2){
      y_interp = 1;
    }
    else{
      y_interp = 0;
    }
    for (int j=0;j<w2;j++)
    {
      // ------ current x value ------
      x1 = (rat>>16);
      int px_diff_next_x = ((rat+x_ratio)>>16) - x1;

      // ------ optimized bilinear (to put in function) -------
      uint32_t c1=*(uint32_t*)((x1<w1-1)?(p+x1):(p+x1-1));
      uint32_t c2=*(uint32_t*)((x1<w1-1)?(p_below+x1):(p_below+x1-1));

      uint32_t adj_px_weight_div = 1;
      if (E_x >= Mid_w2){
        if(y_interp){
          //cnt_yes_x_yes_y++;
          uint32_t red_comp_above=c1&0xF800;
          red_comp_above+=((c1>>16)&0xF800 / adj_px_weight_div);
          uint32_t green_comp_above=c1&0x07E0;
          green_comp_above+=((c1>>16)&0x07E0 / adj_px_weight_div);
          uint32_t blue_comp_above=c1&0x001F;
          blue_comp_above+=((c1>>16)&0x001F / adj_px_weight_div);
          uint32_t red_comp_below=( (c2&0xF800) / adj_px_weight_div);
          red_comp_below+=( ((c2>>16)&0xF800) / adj_px_weight_div);
          uint32_t green_comp_below=( (c2&0x07E0) / adj_px_weight_div);
          green_comp_below+=( ((c2>>16)&0x07E0) / adj_px_weight_div);
          uint32_t blue_comp_below=( (c2&0x001F) / adj_px_weight_div);
          blue_comp_below+=( ((c2>>16)&0x001F) / adj_px_weight_div);
          uint32_t red_comp = ((red_comp_above+red_comp_below)*adj_px_weight_div/(adj_px_weight_div+3))&0xF800;
          uint32_t green_comp = ((green_comp_above+green_comp_below)*adj_px_weight_div/(adj_px_weight_div+3))&0x07E0;
          uint32_t blue_comp = ((blue_comp_above+blue_comp_below)*adj_px_weight_div/(adj_px_weight_div+3))&0x001F;
          *t++ = red_comp+green_comp+blue_comp;
        }
        else{
          //cnt_yes_x_no_y++;
          uint16_t * cur_p;
          uint32_t red_comp = 0;
          uint32_t green_comp = 0;
          uint32_t blue_comp = 0;
          for(int cur_px_diff=0; cur_px_diff<px_diff_next_x; cur_px_diff++){
            cur_p = (x1+cur_px_diff<w1)?(p+x1+cur_px_diff):(p+x1);
            red_comp += (*cur_p)&0xF800;
            green_comp += (*cur_p)&0x07E0;
            blue_comp += (*cur_p)&0x001F;
          }
          red_comp = (red_comp/px_diff_next_x)&0xF800;
          green_comp = (green_comp/px_diff_next_x)&0x07E0;
          blue_comp = (blue_comp/px_diff_next_x)&0x001F;
          *t++ = red_comp+green_comp+blue_comp;
        }
      }
      else{
        if(y_interp){
          //cnt_no_x_yes_y++;
          uint32_t red_comp_above=c1&0xF800;
          uint32_t green_comp_above=c1&0x07E0;
          uint32_t blue_comp_above=c1&0x001F;
          uint32_t red_comp_below=( (c2&0xF800) / adj_px_weight_div);
          uint32_t green_comp_below=( (c2&0x07E0) / adj_px_weight_div);
          uint32_t blue_comp_below=( (c2&0x001F) / adj_px_weight_div);
          uint32_t red_comp = ((red_comp_above+red_comp_below)*adj_px_weight_div/(adj_px_weight_div+1))&0xF800;
          uint32_t green_comp = ((green_comp_above+green_comp_below)*adj_px_weight_div/(adj_px_weight_div+1))&0x07E0;
          uint32_t blue_comp = ((blue_comp_above+blue_comp_below)*adj_px_weight_div/(adj_px_weight_div+1))&0x001F;
          *t++ = red_comp+green_comp+blue_comp;
        }
        else{
          //cnt_no_x_no_y++;
          *t++ = p[x1];
        }
      }

      E_x += FractPart_w;
        if (E_x >= w2) {
          E_x -= w2;
        }


      // ------ next pixel ------
      rat += x_ratio;
    }


    E_y += FractPart_h;
    if (E_y >= h2) {
      E_y -= h2;
    }
  }
  /*printf("w1=%d, h1=%d, E_x=%d, E_y=%d, cnt_yes_x_yes_y=%d, cnt_yes_x_no_y=%d, cnt_no_x_yes_y=%d, cnt_no_x_no_y=%d\n",
    w1, h1, E_x, E_y, cnt_yes_x_yes_y, cnt_yes_x_no_y, cnt_no_x_yes_y, cnt_no_x_no_y);*/
#endif


/// Nearest neighboor with 2D Bresenham
#if 0
    int w1=plat_sdl_screen->w;
    int h1=plat_sdl_screen->h;
    int w2=240;
    int h2=240;
    int FractPart_w = w1 % w2;
    int FractPart_h = h1 % h2;
  int Mid_w2 = w2/2; //threshold (think in ratio) for doing X interpolation
  int Mid_h2 = h2/2;
  uint8_t y_interp = 0;
    //int x_ratio = (int)((w1<<16)/w2) +1;
  //int y_ratio = (int)((h1<<16)/h2) +1;
    int x_ratio = (int)((w1<<16)/w2);
  int y_ratio = (int)((h1<<16)/h2);
  int x1, y1, y1_below;
  int E_y = 0;
  int E_x = 0;
  //int cnt_yes_x_yes_y, cnt_yes_x_no_y, cnt_no_x_yes_y, cnt_no_x_no_y;
  //cnt_yes_x_yes_y= cnt_yes_x_no_y= cnt_no_x_yes_y= cnt_no_x_no_y = 0;
    for (int i=0;i<h2;i++)
  {
    uint16_t* t = (uint16_t*)(hw_screen->pixels+(i*w2)*sizeof(uint16_t));
    y1 = ((i*y_ratio)>>16);
    y1_below = (y1<h1-1)?y1+1:y1;
    uint16_t* p = (uint16_t*)(plat_sdl_screen->pixels + (y1*w1) *sizeof(uint16_t));
    uint16_t* p_below = (uint16_t*)(plat_sdl_screen->pixels + (y1_below*w1) *sizeof(uint16_t));
    int rat = 0;
    E_x = 0;
    if (E_y >= Mid_h2){
      y_interp = 1;
    }
    else{
      y_interp = 0;
    }
    for (int j=0;j<w2;j++)
    {
      // ------ current x value ------
      x1 = (rat>>16);

      // ------ optimized bilinear (to put in function) -------
      uint32_t c1=*(uint32_t*)((x1<w1-1)?(p+x1):(p+x1-1));
      uint32_t c2=*(uint32_t*)((x1<w1-1)?(p_below+x1):(p_below+x1-1));

      uint32_t adj_px_weight_div = 1;
      if (E_x >= Mid_w2){
        if(y_interp){
          //cnt_yes_x_yes_y++;
          uint32_t red_comp_above=c1&0xF800;
          red_comp_above+=((c1>>16)&0xF800 / adj_px_weight_div);
          uint32_t green_comp_above=c1&0x07E0;
          green_comp_above+=((c1>>16)&0x07E0 / adj_px_weight_div);
          uint32_t blue_comp_above=c1&0x001F;
          blue_comp_above+=((c1>>16)&0x001F / adj_px_weight_div);
          uint32_t red_comp_below=( (c2&0xF800) / adj_px_weight_div);
          red_comp_below+=( ((c2>>16)&0xF800) / adj_px_weight_div);
          uint32_t green_comp_below=( (c2&0x07E0) / adj_px_weight_div);
          green_comp_below+=( ((c2>>16)&0x07E0) / adj_px_weight_div);
          uint32_t blue_comp_below=( (c2&0x001F) / adj_px_weight_div);
          blue_comp_below+=( ((c2>>16)&0x001F) / adj_px_weight_div);
          uint32_t red_comp = ((red_comp_above+red_comp_below)*adj_px_weight_div/(adj_px_weight_div+3))&0xF800;
          uint32_t green_comp = ((green_comp_above+green_comp_below)*adj_px_weight_div/(adj_px_weight_div+3))&0x07E0;
          uint32_t blue_comp = ((blue_comp_above+blue_comp_below)*adj_px_weight_div/(adj_px_weight_div+3))&0x001F;
          *t++ = red_comp+green_comp+blue_comp;
        }
        else{
          //cnt_yes_x_no_y++;
          uint32_t red_comp_above=c1&0xF800;
          red_comp_above+=((c1>>16)&0xF800 / adj_px_weight_div);
          uint32_t green_comp_above=c1&0x07E0;
          green_comp_above+=((c1>>16)&0x07E0 / adj_px_weight_div);
          uint32_t blue_comp_above=c1&0x001F;
          blue_comp_above+=((c1>>16)&0x001F / adj_px_weight_div);
          uint32_t red_comp = ((red_comp_above)*adj_px_weight_div/(adj_px_weight_div+1))&0xF800;
          uint32_t green_comp = ((green_comp_above)*adj_px_weight_div/(adj_px_weight_div+1))&0x07E0;
          uint32_t blue_comp = ((blue_comp_above)*adj_px_weight_div/(adj_px_weight_div+1))&0x001F;
          *t++ = red_comp+green_comp+blue_comp;
        }
      }
      else{
        if(y_interp){
          //cnt_no_x_yes_y++;
          uint32_t red_comp_above=c1&0xF800;
          uint32_t green_comp_above=c1&0x07E0;
          uint32_t blue_comp_above=c1&0x001F;
          uint32_t red_comp_below=( (c2&0xF800) / adj_px_weight_div);
          uint32_t green_comp_below=( (c2&0x07E0) / adj_px_weight_div);
          uint32_t blue_comp_below=( (c2&0x001F) / adj_px_weight_div);
          uint32_t red_comp = ((red_comp_above+red_comp_below)*adj_px_weight_div/(adj_px_weight_div+1))&0xF800;
          uint32_t green_comp = ((green_comp_above+green_comp_below)*adj_px_weight_div/(adj_px_weight_div+1))&0x07E0;
          uint32_t blue_comp = ((blue_comp_above+blue_comp_below)*adj_px_weight_div/(adj_px_weight_div+1))&0x001F;
          *t++ = red_comp+green_comp+blue_comp;
        }
        else{
          //cnt_no_x_no_y++;
          *t++ = p[x1];
        }
      }

      E_x += FractPart_w;
        if (E_x >= w2) {
          E_x -= w2;
        }


      // ------ next pixel ------
      rat += x_ratio;
    }


    E_y += FractPart_h;
    if (E_y >= h2) {
      E_y -= h2;
    }
  }
  //printf("w1=%d, h1=%d, E_x=%d, E_y=%d, cnt_yes_x_yes_y=%d, cnt_yes_x_no_y=%d, cnt_no_x_yes_y=%d, cnt_no_x_no_y=%d\n",
  //  w1, h1, E_x, E_y, cnt_yes_x_yes_y, cnt_yes_x_no_y, cnt_no_x_yes_y, cnt_no_x_no_y);
#endif


/// Nearest neighboor with 2D bilinear
#if 0
    int w1=plat_sdl_screen->w;
    int h1=plat_sdl_screen->h;
    int w2=240;
    int h2=240;
    int x_ratio = (int)((plat_sdl_screen->w<<16)/w2) +1;
  int y_ratio = (int)((plat_sdl_screen->h<<16)/h2) +1;
    //int x_ratio = (int)((plat_sdl_screen->w<<16)/w2);
  //int y_ratio = (int)((plat_sdl_screen->h<<16)/h2);
  int x1, y1, y1_below;
    for (int i=0;i<h2;i++)
  {
    uint16_t* t = (uint16_t*)(hw_screen->pixels+(i*w2)*sizeof(uint16_t));
    y1 = ((i*y_ratio)>>16);
    y1_below = (y1<h1-1)?y1+1:y1;
    uint16_t* p = (uint16_t*)(plat_sdl_screen->pixels + (y1*w1) *sizeof(uint16_t));
    uint16_t* p_below = (uint16_t*)(plat_sdl_screen->pixels + (y1_below*w1) *sizeof(uint16_t));
    int rat = 0;
    for (int j=0;j<w2;j++)
    {
      // ------ current x value ------
      x1 = (rat>>16);

      // ------ optimized bilinear (to put in function) -------
      uint32_t c1=*(uint32_t*)((x1<w1-1)?(p+x1):(p+x1-1));
      uint32_t c2=*(uint32_t*)((x1<w1-1)?(p_below+x1):(p_below+x1-1));

      /*//Average the two.
      //uint32_t c=((c1&0xF7DEF7DE)+(c2&0xF7DEF7DE))>>1;
      uint32_t c=((c1&0xF7DEF7DE)>>1)+((c2&0xF7DEF7DE)>>1);
      //The averaging action essentially killed the least significant bit of all colors; if
      //both were one the resulting color should be one more. Compensate for that here.
      //c+=(c1&c1)&0x08210821;
      c+=(c1&c2)&0x08210821;*/

      //Take the various components from the pixels and return the composite.
      /*uint32_t red_comp=c&0xF800;
      uint32_t green_comp=c&0x07E0;
      green_comp+=(c>>16)&0x07E0;
      green_comp=(green_comp/2)&0x07E0;
      uint32_t blue_comp=(c>>16)&0x001F;
      *t++ = red_comp+green_comp+blue_comp;*/

      /*uint32_t red_comp=c&0xF800;
      red_comp+=(c>>16)&0xF800;
      red_comp=(red_comp/2)&0xF800;
      uint32_t green_comp=c&0x07E0;
      green_comp+=(c>>16)&0x07E0;
      green_comp=(green_comp/2)&0x07E0;
      uint32_t blue_comp=(c>>16)&0x001F;
      blue_comp+=(c>>16)&0x001F;
      blue_comp=(blue_comp/2)&0x001F;
      *t++ = red_comp+green_comp+blue_comp;*/

      uint32_t adj_px_weight_div = 1;
      uint32_t red_comp_above=c1&0xF800;
      red_comp_above+=((c1>>16)&0xF800 / adj_px_weight_div);
      uint32_t green_comp_above=c1&0x07E0;
      green_comp_above+=((c1>>16)&0x07E0 / adj_px_weight_div);
      uint32_t blue_comp_above=c1&0x001F;
      blue_comp_above+=((c1>>16)&0x001F / adj_px_weight_div);
      uint32_t red_comp_below=( (c2&0xF800) / adj_px_weight_div);
      red_comp_below+=( ((c2>>16)&0xF800) / adj_px_weight_div);
      uint32_t green_comp_below=( (c2&0x07E0) / adj_px_weight_div);
      green_comp_below+=( ((c2>>16)&0x07E0) / adj_px_weight_div);
      uint32_t blue_comp_below=( (c2&0x001F) / adj_px_weight_div);
      blue_comp_below+=( ((c2>>16)&0x001F) / adj_px_weight_div);
      uint32_t red_comp = ((red_comp_above+red_comp_below)*adj_px_weight_div/(adj_px_weight_div+3))&0xF800;
      uint32_t green_comp = ((green_comp_above+green_comp_below)*adj_px_weight_div/(adj_px_weight_div+3))&0x07E0;
      uint32_t blue_comp = ((blue_comp_above+blue_comp_below)*adj_px_weight_div/(adj_px_weight_div+3))&0x001F;
      *t++ = red_comp+green_comp+blue_comp;


      // ------ next pixel ------
      rat += x_ratio;
    }
  }
#endif




#if 0
    // XXX: no locking, but should be fine with SDL_SWSURFACE?
  // MODIF VINCENT
    if(plat_sdl_screen->w > 240 && plat_sdl_screen->h > 240){
      printf("Not implemented scaling scenario:\n");
      printf("  plat_sdl_screen->w = %d > 240, plat_sdl_screen->h = %d > 240\n", plat_sdl_screen->w, plat_sdl_screen->h);
    }
    else if(plat_sdl_screen->w > 240 && plat_sdl_screen->h <= 240){

      // Nearest neighbohr
      int w2=240;
      int h2=240;
      int x_ratio = (int)((plat_sdl_screen->w<<16)/w2) +1;
      int y_ratio = (int)((plat_sdl_screen->h<<16)/h2) +1;
      //int x_ratio = (int)((w1<<16)/w2) ;
      //int y_ratio = (int)((h1<<16)/h2) ;
      int x2, y2 ;
      for (int i=0;i<h2;i++) {
          for (int j=0;j<w2;j++) {
              x2 = ((j*x_ratio)>>16) ;
              y2 = ((i*y_ratio)>>16) ;
              *(uint16_t*)(hw_screen->pixels+(i*w2+j)*sizeof(uint16_t)) =
              *(uint16_t*)(plat_sdl_screen->pixels + ((y2*plat_sdl_screen->w)+x2) *sizeof(uint16_t)) ;
          }
      }


      /*int x,y;
      int count = 0;
      int last_scaled_x = -1;
      int scaled_x = -1;
      int last_scaled_y = -1;
      int scaled_y = -1;
      int scaled_h = plat_sdl_screen->h * 240 / plat_sdl_screen->w;
      for (y=0; y<plat_sdl_screen->h; y++){
        scaled_y = y*240/plat_sdl_screen->w;
        if(scaled_y != last_scaled_y){
          last_scaled_y = scaled_y;
          for(x=0; x<plat_sdl_screen->w; x++){
              scaled_x = x*240/plat_sdl_screen->w;
            if(scaled_x != last_scaled_x){
              last_scaled_x = scaled_x;
              *(uint16_t*)(hw_screen->pixels+count*sizeof(uint16_t) + 240*(240-scaled_h)/2*sizeof(uint16_t)) =
                *(uint16_t*)(plat_sdl_screen->pixels + (y*plat_sdl_screen->w + x)*sizeof(uint16_t));
              count++;
            }
          }
        }
      }*/


      //memcpy(hw_screen->pixels, plat_sdl_screen->pixels, 240*240*sizeof(uint16_t));
    }
    else{
      printf("Not implemented scaling scenario:\n");
      printf("  plat_sdl_screen->w = %d, plat_sdl_screen->h = %d\n", plat_sdl_screen->w, plat_sdl_screen->h);
    }
 #endif
