/*
 * (C) Gražvydas "notaz" Ignotas, 2010-2015
 *
 * This work is licensed under the terms of any of these licenses
 * (at your option):
 *  - GNU GPL, version 2 or later.
 *  - GNU LGPL, version 2.1 or later.
 * See the COPYING file in the top-level directory.
 */

#define _GNU_SOURCE 1
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <dlfcn.h>
#include <zlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>
#include <SDL/SDL_image.h>

#include "main.h"
#include "menu.h"
#include "configfile_fk.h"
#include "config.h"
#include "plugin.h"
#include "plugin_lib.h"
#include "plat.h"
#include "pcnt.h"
#include "cspace.h"
#include "libpicofe/plat.h"
#include "libpicofe/input.h"
#include "libpicofe/linux/in_evdev.h"
#include "libpicofe/plat.h"
#include "../libpcsxcore/misc.h"
#include "../libpcsxcore/cdrom.h"
#include "../libpcsxcore/cdriso.h"
#include "../libpcsxcore/cheat.h"
#include "../libpcsxcore/new_dynarec/new_dynarec.h"
#include "../plugins/dfinput/externals.h"
#include "../plugins/dfsound/spu_config.h"
#include "psemu_plugin_defs.h"
#include "arm_features.h"
#include "revision.h"

#define REARMED_BIRTHDAY_TIME 1293306830	/* 25 Dec 2010 */

// Nick
static int swap_cd_multidisk(void);

#define array_size(x) (sizeof(x) / sizeof(x[0]))

typedef enum
{
	MA_NONE = 1,
	MA_MAIN_RESUME_GAME,
	MA_MAIN_SAVE_STATE,
	MA_MAIN_LOAD_STATE,
	MA_MAIN_RESET_GAME,
	MA_MAIN_LOAD_ROM,
	MA_MAIN_SWAP_CD,
	MA_MAIN_SWAP_CD_MULTI,
	MA_MAIN_RUN_BIOS,
	MA_MAIN_RUN_EXE,
	MA_MAIN_LOAD_CHEATS,
	MA_MAIN_CHEATS,
	MA_MAIN_CONTROLS,
	MA_MAIN_CREDITS,
	MA_MAIN_EXIT,
	MA_CTRL_PLAYER1,
	MA_CTRL_PLAYER2,
	MA_CTRL_ANALOG,
	MA_CTRL_EMU,
	MA_CTRL_DEV_FIRST,
	MA_CTRL_DEV_NEXT,
	MA_CTRL_NUBS_BTNS,
	MA_CTRL_DEADZONE,
	MA_CTRL_VIBRATION,
	MA_CTRL_DONE,
	MA_OPT_SAVECFG,
	MA_OPT_SAVECFG_GAME,
	MA_OPT_CPU_CLOCKS,
	MA_OPT_SPU_THREAD,
	MA_OPT_DISP_OPTS,
	MA_OPT_VARSCALER,
	MA_OPT_VARSCALER_C,
	MA_OPT_SCALER2,
	MA_OPT_HWFILTER,
	MA_OPT_SWFILTER,
	MA_OPT_GAMMA,
	MA_OPT_VOUT_MODE,
	MA_OPT_SCANLINES,
	MA_OPT_SCANLINE_LEVEL,
} menu_id;

static int last_vout_w, last_vout_h, last_vout_bpp;
static int cpu_clock, cpu_clock_st, volume_boost, frameskip;
static char last_selected_fname[MAXPATHLEN];
static int config_save_counter, region, in_type_sel1, in_type_sel2;
static int psx_clock;
static int memcard1_sel = -1, memcard2_sel = -1;
extern int g_autostateld_opt;
static int menu_iopts[8];
int g_opts, g_scaler, g_gamma = 100;
int scanlines, scanline_level = 20;
int soft_scaling, analog_deadzone; // for Caanoo
int soft_filter;

#ifndef HAVE_PRE_ARMV7
#define DEFAULT_PSX_CLOCK (10000 / CYCLE_MULT_DEFAULT)
#define DEFAULT_PSX_CLOCK_S "57"
#else
#define DEFAULT_PSX_CLOCK 50
#define DEFAULT_PSX_CLOCK_S "50"
#endif

static const char *bioses[24];
static const char *gpu_plugins[16];
static const char *spu_plugins[16];
static const char *memcards[32];
static int bios_sel, gpu_plugsel, spu_plugsel;

#ifndef UI_FEATURES_H
#define MENU_BIOS_PATH "/mnt/FunKey/.pcsx/bios/"
#define MENU_SHOW_VARSCALER 0
#define MENU_SHOW_VOUTMODE 1
#define MENU_SHOW_SCALER2 0
#define MENU_SHOW_NUBS_BTNS 0
#define MENU_SHOW_VIBRATION 0
#define MENU_SHOW_DEADZONE 0
#define MENU_SHOW_MINIMIZE 0
#define MENU_SHOW_FULLSCREEN 1
#define MENU_SHOW_VOLUME 0
#endif

static int min(int x, int y) { return x < y ? x : y; }
static int max(int x, int y) { return x > y ? x : y; }









/// -------------- DEFINES --------------
/*#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
*/
//#define MENU_DEBUG
#define MENU_ERROR

#ifdef MENU_DEBUG
#define MENU_DEBUG_PRINTF(...)   printf(__VA_ARGS__);
#else
#define MENU_DEBUG_PRINTF(...)
#endif //MENU_DEBUG

#ifdef MENU_ERROR
#define MENU_ERROR_PRINTF(...)   printf(__VA_ARGS__);
#else
#define MENU_ERROR_PRINTF(...)
#endif //MENU_ERROR

#define SCREEN_HORIZONTAL_SIZE      240
#define SCREEN_VERTICAL_SIZE        240

#define SCROLL_SPEED_PX             30 //This means no anumations but also no tearing effect
#define FPS_MENU                    50

#define MENU_ZONE_WIDTH             SCREEN_HORIZONTAL_SIZE
#define MENU_ZONE_HEIGHT            SCREEN_VERTICAL_SIZE
#define MENU_BG_SQUARE_WIDTH        180
#define MENU_BG_SQUARE_HEIGHT       140

#define MENU_FONT_NAME_TITLE        "/usr/games/menu_resources/OpenSans-Bold.ttf"
#define MENU_FONT_SIZE_TITLE        22
#define MENU_FONT_NAME_INFO         "/usr/games/menu_resources/OpenSans-Bold.ttf"
#define MENU_FONT_SIZE_INFO         16
#define MENU_FONT_NAME_SMALL_INFO   "/usr/games/menu_resources/OpenSans-Regular.ttf"
#define MENU_FONT_SIZE_SMALL_INFO   13
#define MENU_PNG_BG_PATH            "/usr/games/menu_resources/zone_bg.png"
#define MENU_PNG_ARROW_TOP_PATH     "/usr/games/menu_resources/arrow_top.png"
#define MENU_PNG_ARROW_BOTTOM_PATH  "/usr/games/menu_resources/arrow_bottom.png"

#define GRAY_MAIN_R                 85
#define GRAY_MAIN_G                 85
#define GRAY_MAIN_B                 85
#define WHITE_MAIN_R                236
#define WHITE_MAIN_G                236
#define WHITE_MAIN_B                236

#define MAX_SAVE_SLOTS              9



/// -------------- STATIC VARIABLES --------------
extern SDL_Surface * hw_screen;
extern SDL_Surface * virtual_hw_screen; // this one is not rotated
SDL_Surface * draw_screen;

static int backup_key_repeat_delay, backup_key_repeat_interval;
static SDL_Surface * backup_hw_screen = NULL;

static TTF_Font *menu_title_font = NULL;
static TTF_Font *menu_info_font = NULL;
static TTF_Font *menu_small_info_font = NULL;
static SDL_Surface *img_arrow_top = NULL;
static SDL_Surface *img_arrow_bottom = NULL;
static SDL_Surface ** menu_zone_surfaces = NULL;
static int * idx_menus = NULL;
static int nb_menu_zones = 0;
static int menuItem = 0;
int stop_menu_loop = 0;

static SDL_Color text_color = {GRAY_MAIN_R, GRAY_MAIN_G, GRAY_MAIN_B};
static int padding_y_from_center_menu_zone = 18;
static uint16_t width_progress_bar = 100;
static uint16_t height_progress_bar = 20;
static uint16_t x_volume_bar = 0;
static uint16_t y_volume_bar = 0;
static uint16_t x_brightness_bar = 0;
static uint16_t y_brightness_bar = 0;

int volume_percentage = 0;
int brightness_percentage = 0;

#undef X
#define X(a, b) b,
const char *resume_options_str[] = {RESUME_OPTIONS};

/*static uint8_t idx_save_slot = 0;
static uint8_t idx_load_slot = 0;*/
static int quick_load_slot_chosen = 0;

/// -------------- STATIC FUNCTIONS DECLARATION --------------
static int emu_save_load_game(int load, int unused);

/// -------------- FUNCTIONS IMPLEMENTATION --------------
void init_menu_SDL(){
    /// ----- Loading the fonts -----
    menu_title_font = TTF_OpenFont(MENU_FONT_NAME_TITLE, MENU_FONT_SIZE_TITLE);
    if(!menu_title_font){
        MENU_ERROR_PRINTF("ERROR in init_menu_SDL: Could not open menu font %s, %s\n", MENU_FONT_NAME_TITLE, SDL_GetError());
    }
    menu_info_font = TTF_OpenFont(MENU_FONT_NAME_INFO, MENU_FONT_SIZE_INFO);
    if(!menu_info_font){
        MENU_ERROR_PRINTF("ERROR in init_menu_SDL: Could not open menu font %s, %s\n", MENU_FONT_NAME_INFO, SDL_GetError());
    }
    menu_small_info_font = TTF_OpenFont(MENU_FONT_NAME_SMALL_INFO, MENU_FONT_SIZE_SMALL_INFO);
    if(!menu_small_info_font){
        MENU_ERROR_PRINTF("ERROR in init_menu_SDL: Could not open menu font %s, %s\n", MENU_FONT_NAME_SMALL_INFO, SDL_GetError());
    }

    /// ----- Copy hw_screen at init ------
    backup_hw_screen = SDL_CreateRGBSurface(SDL_SWSURFACE,
        hw_screen->w, hw_screen->h, 16, 0, 0, 0, 0);
    if(backup_hw_screen == NULL){
        MENU_ERROR_PRINTF("ERROR in init_menu_SDL: Could not create backup_hw_screen: %s\n", SDL_GetError());
    }
    /*if(SDL_BlitSurface(hw_screen, NULL, backup_hw_screen, NULL)){
        MENU_ERROR_PRINTF("ERROR Could not copy hw_screen: %s\n", SDL_GetError());
    }*/

    draw_screen = SDL_CreateRGBSurface(SDL_SWSURFACE,
        virtual_hw_screen->w, virtual_hw_screen->h, 16, 0, 0, 0, 0);
    if(draw_screen == NULL){
        MENU_ERROR_PRINTF("ERROR Could not create draw_screen: %s\n", SDL_GetError());
    }

    /// ------ Load arrows imgs -------
    img_arrow_top = IMG_Load(MENU_PNG_ARROW_TOP_PATH);
    if(!img_arrow_top) {
        MENU_ERROR_PRINTF("ERROR IMG_Load: %s\n", IMG_GetError());
    }
    img_arrow_bottom = IMG_Load(MENU_PNG_ARROW_BOTTOM_PATH);
    if(!img_arrow_bottom) {
        MENU_ERROR_PRINTF("ERROR IMG_Load: %s\n", IMG_GetError());
    }

    /// ------ Init menu zones ------
    init_menu_zones();
}

void deinit_menu_SDL(){
    /// ------ Close font -------
    TTF_CloseFont(menu_title_font);
    TTF_CloseFont(menu_info_font);
    TTF_CloseFont(menu_small_info_font);

    /// ------ Free Surfaces -------
    for(int i=0; i < nb_menu_zones; i++){
        SDL_FreeSurface(menu_zone_surfaces[i]);
    }
    SDL_FreeSurface(backup_hw_screen);
    SDL_FreeSurface(draw_screen);

    SDL_FreeSurface(img_arrow_top);
    SDL_FreeSurface(img_arrow_bottom);

    /// ------ Free Menu memory and reset vars -----
    if(idx_menus){
        free(idx_menus);
    }
    idx_menus=NULL;
    nb_menu_zones = 0;
}


void draw_progress_bar(SDL_Surface * surface, uint16_t x, uint16_t y, uint16_t width,
                        uint16_t height, uint8_t percentage, uint16_t nb_bars){
    /// ------ Init Variables ------
    uint16_t line_width = 1; //px
    uint16_t padding_bars_ratio = 3;
    uint16_t nb_full_bars = 0;

    /// ------ Check values ------
    percentage = (percentage > 100)?100:percentage;
    x = (x > (surface->w-1))?(surface->w-1):x;
    y = (y > surface->h-1)?(surface->h-1):y;
    width = (width < line_width*2+1)?(line_width*2+1):width;
    width = (width > surface->w-x-1)?(surface->w-x-1):width;
    height = (height < line_width*2+1)?(line_width*2+1):height;
    height = (height > surface->h-y-1)?(surface->h-y-1):height;
    uint16_t nb_bars_max = ( width * padding_bars_ratio  /  (line_width*2+1) + 1 ) / (padding_bars_ratio+1);
    nb_bars = (nb_bars > nb_bars_max)?nb_bars_max:nb_bars;
    uint16_t bar_width = (width / nb_bars)*padding_bars_ratio/(padding_bars_ratio+1)+1;
    uint16_t bar_padding_x = bar_width/padding_bars_ratio;
    nb_full_bars = nb_bars*percentage/100;

    /// ------ draw full bars ------
    for (int i = 0; i < nb_full_bars; ++i)
    {
        /// ---- draw one bar ----
        //MENU_DEBUG_PRINTF("Drawing filled bar %d\n", i);
        SDL_Rect rect = {x+ i*(bar_width +bar_padding_x),
            y, bar_width, height};
        SDL_FillRect(surface, &rect, SDL_MapRGB(surface->format, GRAY_MAIN_R, GRAY_MAIN_G, GRAY_MAIN_B));
    }

    /// ------ draw full bars ------
    for (int i = 0; i < (nb_bars-nb_full_bars); ++i)
    {
        /// ---- draw one bar ----
        //MENU_DEBUG_PRINTF("Drawing empty bar %d\n", i);
        SDL_Rect rect = {x+ i*(bar_width +bar_padding_x) + nb_full_bars*(bar_width +bar_padding_x),
            y, bar_width, height};
        SDL_FillRect(surface, &rect, SDL_MapRGB(surface->format, GRAY_MAIN_R, GRAY_MAIN_G, GRAY_MAIN_B));

        SDL_Rect rect2 = {x+ i*(bar_width +bar_padding_x) + line_width + nb_full_bars*(bar_width +bar_padding_x),
            y + line_width, bar_width - line_width*2, height - line_width*2};
        SDL_FillRect(surface, &rect2, SDL_MapRGB(surface->format, WHITE_MAIN_R, WHITE_MAIN_R, WHITE_MAIN_R));
    }


}


void add_menu_zone(ENUM_MENU_TYPE menu_type){
	// Nick
	char szText[100];
	
    /// ------ Increase nb of menu zones -------
    nb_menu_zones++;

    /// ------ Realoc idx Menus array -------
    if(!idx_menus){
        idx_menus = malloc(nb_menu_zones*sizeof(int));
        menu_zone_surfaces = malloc(nb_menu_zones*sizeof(SDL_Surface*));
    }
    else{
        int *temp = realloc(idx_menus, nb_menu_zones*sizeof(int));
        idx_menus = temp;
        menu_zone_surfaces = realloc(menu_zone_surfaces, nb_menu_zones*sizeof(SDL_Surface*));
    }
    idx_menus[nb_menu_zones-1] = menu_type;

    /// ------ Reinit menu surface with height increased -------
    menu_zone_surfaces[nb_menu_zones-1] = IMG_Load(MENU_PNG_BG_PATH);
    if(!menu_zone_surfaces[nb_menu_zones-1]) {
        MENU_ERROR_PRINTF("ERROR IMG_Load: %s\n", IMG_GetError());
    }
    /// --------- Init Common Variables --------
    SDL_Surface *text_surface = NULL;
    SDL_Surface *surface = menu_zone_surfaces[nb_menu_zones-1];
    SDL_Rect text_pos;

    /// --------- Add new zone ---------
    switch(menu_type){
    case MENU_TYPE_VOLUME:
        MENU_DEBUG_PRINTF("Init MENU_TYPE_VOLUME\n");
        /// ------ Text ------
        text_surface = TTF_RenderText_Blended(menu_title_font, "VOLUME", text_color);
        text_pos.x = (surface->w - MENU_ZONE_WIDTH)/2 + (MENU_ZONE_WIDTH - text_surface->w)/2;
        text_pos.y = surface->h - MENU_ZONE_HEIGHT/2 - text_surface->h/2 - padding_y_from_center_menu_zone;
        SDL_BlitSurface(text_surface, NULL, surface, &text_pos);

        x_volume_bar = (surface->w - MENU_ZONE_WIDTH)/2 + (MENU_ZONE_WIDTH - width_progress_bar)/2;
        y_volume_bar = surface->h - MENU_ZONE_HEIGHT/2 - height_progress_bar/2 + padding_y_from_center_menu_zone;
        draw_progress_bar(surface, x_volume_bar, y_volume_bar,
            width_progress_bar, height_progress_bar, 0, 100/STEP_CHANGE_VOLUME);
        break;
    case MENU_TYPE_BRIGHTNESS:
        MENU_DEBUG_PRINTF("Init MENU_TYPE_BRIGHTNESS\n");
        /// ------ Text ------
        text_surface = TTF_RenderText_Blended(menu_title_font, "BRIGHTNESS", text_color);
        text_pos.x = (surface->w - MENU_ZONE_WIDTH)/2 + (MENU_ZONE_WIDTH - text_surface->w)/2;
        text_pos.y = surface->h - MENU_ZONE_HEIGHT/2 - text_surface->h/2 - padding_y_from_center_menu_zone;
        SDL_BlitSurface(text_surface, NULL, surface, &text_pos);

        x_brightness_bar = (surface->w - MENU_ZONE_WIDTH)/2 + (MENU_ZONE_WIDTH - width_progress_bar)/2;
        y_brightness_bar = surface->h - MENU_ZONE_HEIGHT/2 - height_progress_bar/2 + padding_y_from_center_menu_zone;
        draw_progress_bar(surface, x_brightness_bar, y_brightness_bar,
            width_progress_bar, height_progress_bar, 0, 100/STEP_CHANGE_BRIGHTNESS);
        break;
    case MENU_TYPE_SAVE:
        MENU_DEBUG_PRINTF("Init MENU_TYPE_SAVE\n");
        /// ------ Text ------
        text_surface = TTF_RenderText_Blended(menu_title_font, "SAVE", text_color);
        text_pos.x = (surface->w - MENU_ZONE_WIDTH)/2 + (MENU_ZONE_WIDTH - text_surface->w)/2;
        text_pos.y = surface->h - MENU_ZONE_HEIGHT/2 - text_surface->h/2 - padding_y_from_center_menu_zone*2;
        SDL_BlitSurface(text_surface, NULL, surface, &text_pos);
        break;
    case MENU_TYPE_LOAD:
        MENU_DEBUG_PRINTF("Init MENU_TYPE_LOAD\n");
        /// ------ Text ------
        text_surface = TTF_RenderText_Blended(menu_title_font, "LOAD", text_color);
        text_pos.x = (surface->w - MENU_ZONE_WIDTH)/2 + (MENU_ZONE_WIDTH - text_surface->w)/2;
        text_pos.y = surface->h - MENU_ZONE_HEIGHT/2 - text_surface->h/2 - padding_y_from_center_menu_zone*2;
        SDL_BlitSurface(text_surface, NULL, surface, &text_pos);
        break;
    case MENU_TYPE_ASPECT_RATIO:
        MENU_DEBUG_PRINTF("Init MENU_TYPE_ASPECT_RATIO\n");
        /// ------ Text ------
        text_surface = TTF_RenderText_Blended(menu_title_font, "ASPECT RATIO", text_color);
        text_pos.x = (surface->w - MENU_ZONE_WIDTH)/2 + (MENU_ZONE_WIDTH - text_surface->w)/2;
        text_pos.y = surface->h - MENU_ZONE_HEIGHT/2 - text_surface->h/2 - padding_y_from_center_menu_zone;
        SDL_BlitSurface(text_surface, NULL, surface, &text_pos);
        break;
    case MENU_TYPE_EXIT:
        MENU_DEBUG_PRINTF("Init MENU_TYPE_EXIT\n");
        /// ------ Text ------
        text_surface = TTF_RenderText_Blended(menu_title_font, "EXIT GAME", text_color);
        text_pos.x = (surface->w - MENU_ZONE_WIDTH)/2 + (MENU_ZONE_WIDTH - text_surface->w)/2;
        text_pos.y = surface->h - MENU_ZONE_HEIGHT/2 - text_surface->h/2;
        SDL_BlitSurface(text_surface, NULL, surface, &text_pos);
        break;
    case MENU_TYPE_POWERDOWN:
        MENU_DEBUG_PRINTF("Init MENU_TYPE_POWERDOWN\n");
        /// ------ Text ------
        /*text_surface = TTF_RenderText_Blended(menu_title_font, "POWERDOWN", text_color);
        text_pos.x = (surface->w - MENU_ZONE_WIDTH)/2 + (MENU_ZONE_WIDTH - text_surface->w)/2;
        text_pos.y = surface->h - MENU_ZONE_HEIGHT/2 - text_surface->h/2;
        SDL_BlitSurface(text_surface, NULL, surface, &text_pos);*/
        break;
    default:
        MENU_DEBUG_PRINTF("Warning - In add_menu_zone, unknown MENU_TYPE: %d\n", menu_type);
        break;
    }

    /// ------ Free Surfaces -------
    SDL_FreeSurface(text_surface);
}

void init_menu_zones(){
    /// Init Volume Menu
    add_menu_zone(MENU_TYPE_VOLUME);
    /// Init Brightness Menu
    add_menu_zone(MENU_TYPE_BRIGHTNESS);
    /// Init Save Menu
    add_menu_zone(MENU_TYPE_SAVE);
    /// Init Load Menu
    add_menu_zone(MENU_TYPE_LOAD);
    /// Init Aspect Ratio Menu
    add_menu_zone(MENU_TYPE_ASPECT_RATIO);
    /// Init Exit Menu
    add_menu_zone(MENU_TYPE_EXIT);
    /// Init Powerdown Menu
    add_menu_zone(MENU_TYPE_POWERDOWN);
}


void init_menu_system_values(){
    FILE *fp;
    char res[100];

    /// ------- Get system volume percentage --------
    fp = popen(SHELL_CMD_VOLUME_GET, "r");
    if (fp == NULL) {
        MENU_ERROR_PRINTF("Failed to run command %s\n", SHELL_CMD_VOLUME_GET );
        volume_percentage = 50; ///wrong value: setting default to 50
    }
    else{
        fgets(res, sizeof(res)-1, fp);
        pclose(fp);

        /// Check if Volume is a number (at least the first char)
        if(res[0] < '0' || res[0] > '9'){
            MENU_ERROR_PRINTF("Wrong return value: %s for volume cmd: %s\n",res, SHELL_CMD_VOLUME_GET);
            volume_percentage = 50; ///wrong value: setting default to 50
        }
        else{
            volume_percentage = atoi(res);
            MENU_DEBUG_PRINTF("System volume = %d%%\n", volume_percentage);
        }
    }

    /// ------- Get system brightness percentage -------
    fp = popen(SHELL_CMD_BRIGHTNESS_GET, "r");
    if (fp == NULL) {
        MENU_ERROR_PRINTF("Failed to run command %s\n", SHELL_CMD_BRIGHTNESS_GET );
        brightness_percentage = 50; ///wrong value: setting default to 50
    }
    else{
        fgets(res, sizeof(res)-1, fp);
        pclose(fp);

        /// Check if brightness is a number (at least the first char)
        if(res[0] < '0' || res[0] > '9'){
            MENU_ERROR_PRINTF("Wrong return value: %s for volume cmd: %s\n",res, SHELL_CMD_BRIGHTNESS_GET);
            brightness_percentage = 50; ///wrong value: setting default to 50
        }
        else{
            brightness_percentage = atoi(res);
            MENU_DEBUG_PRINTF("System brightness = %d%%\n", brightness_percentage);
        }
    }

    /// ------ Save prev key repeat params and set new Key repeat -------
    SDL_GetKeyRepeat(&backup_key_repeat_delay, &backup_key_repeat_interval);
    if(SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL)){
        MENU_ERROR_PRINTF("ERROR with SDL_EnableKeyRepeat: %s\n", SDL_GetError());
    }

    /// Get save slot from game
    state_slot = (state_slot%MAX_SAVE_SLOTS); // security
    /*idx_save_slot = state_slot;
    idx_load_slot = idx_save_slot; */
}

void menu_screen_refresh(int menuItem, int prevItem, int scroll, uint8_t menu_confirmation, uint8_t menu_action){
    /// --------- Vars ---------
    int print_arrows = (scroll==0)?1:0;

    /// --------- Clear HW screen ----------
    //SDL_FillRect(draw_screen, NULL, SDL_MapRGB(draw_screen->format, 255, 0, 0));
    if(SDL_BlitSurface(backup_hw_screen, NULL, draw_screen, NULL)){
        MENU_ERROR_PRINTF("ERROR Could not Clear draw_screen: %s\n", SDL_GetError());
    }

    /// --------- Setup Blit Window ----------
    SDL_Rect menu_blit_window;
    menu_blit_window.x = 0;
    menu_blit_window.w = SCREEN_HORIZONTAL_SIZE;

    /// --------- Blit prev menu Zone going away ----------
    menu_blit_window.y = scroll;
    menu_blit_window.h = SCREEN_VERTICAL_SIZE;
    if(SDL_BlitSurface(menu_zone_surfaces[prevItem], &menu_blit_window, draw_screen, NULL)){
        MENU_ERROR_PRINTF("ERROR Could not Blit surface on draw_screen: %s\n", SDL_GetError());
    }

    /// --------- Blit new menu Zone going in (only during animations) ----------
    if(scroll>0){
        menu_blit_window.y = SCREEN_VERTICAL_SIZE-scroll;
        menu_blit_window.h = SCREEN_VERTICAL_SIZE;
        if(SDL_BlitSurface(menu_zone_surfaces[menuItem], NULL, draw_screen, &menu_blit_window)){
            MENU_ERROR_PRINTF("ERROR Could not Blit surface on draw_screen: %s\n", SDL_GetError());
        }
    }
    else if(scroll<0){
        menu_blit_window.y = SCREEN_VERTICAL_SIZE+scroll;
        menu_blit_window.h = SCREEN_VERTICAL_SIZE;
        if(SDL_BlitSurface(menu_zone_surfaces[menuItem], &menu_blit_window, draw_screen, NULL)){
            MENU_ERROR_PRINTF("ERROR Could not Blit surface on draw_screen: %s\n", SDL_GetError());
        }
    }
    /// --------- No Scroll ? Blitting menu-specific info
    else{
        SDL_Surface * text_surface = NULL;
        char text_tmp[40];
        SDL_Rect text_pos;
        char fname[MAXPATHLEN];
        uint16_t limit_filename_size = 14;
        memset(fname, 0, MAXPATHLEN);

        switch(idx_menus[menuItem]){
        case MENU_TYPE_VOLUME:
            draw_progress_bar(draw_screen, x_volume_bar, y_volume_bar,
                            width_progress_bar, height_progress_bar, volume_percentage, 100/STEP_CHANGE_VOLUME);
            break;

        case MENU_TYPE_BRIGHTNESS:
            draw_progress_bar(draw_screen, x_volume_bar, y_volume_bar,
                            width_progress_bar, height_progress_bar, brightness_percentage, 100/STEP_CHANGE_BRIGHTNESS);
            break;

        case MENU_TYPE_SAVE:
            /// ---- Write slot -----
            sprintf(text_tmp, "IN SLOT   < %d >", state_slot+1);
            text_surface = TTF_RenderText_Blended(menu_info_font, text_tmp, text_color);
            text_pos.x = (draw_screen->w - MENU_ZONE_WIDTH)/2 + (MENU_ZONE_WIDTH - text_surface->w)/2;
            text_pos.y = draw_screen->h - MENU_ZONE_HEIGHT/2 - text_surface->h/2;
            SDL_BlitSurface(text_surface, NULL, draw_screen, &text_pos);

            if(menu_action){
                sprintf(text_tmp, "Saving...");
                text_surface = TTF_RenderText_Blended(menu_info_font, text_tmp, text_color);
            }
            else{
                if(menu_confirmation){
                    sprintf(text_tmp, "Are you sure ?");
                    text_surface = TTF_RenderText_Blended(menu_info_font, text_tmp, text_color);
                }
                else{
                    /// ---- Write current Save state ----
                    get_state_filename(fname, sizeof(fname), state_slot);
                    if (!CheckState(fname)){
                        char *p = strrchr (fname, '/');
                        char *basename = p ? p + 1 : (char *) fname;
                        p = strrchr (basename, '-'); *p = p ? 0 : *p;
                        if(p && p-basename > limit_filename_size){basename[limit_filename_size]=0;} //limiting size
                        char *file_info = p ? p + 1 : (char *) fname;
                        p = strrchr (file_info, '.');
                        char *save_idx = p ? p + 1 : "0";
                        sprintf(text_tmp, "%s - %s", basename, save_idx);
                        text_surface = TTF_RenderText_Blended(menu_small_info_font, text_tmp, text_color);
                    }
                    else{
                        text_surface = TTF_RenderText_Blended(menu_info_font, "Free", text_color);
                    }
                }
            }
            text_pos.x = (draw_screen->w - MENU_ZONE_WIDTH)/2 + (MENU_ZONE_WIDTH - text_surface->w)/2;
            text_pos.y = draw_screen->h - MENU_ZONE_HEIGHT/2 - text_surface->h/2 + 2*padding_y_from_center_menu_zone;
            SDL_BlitSurface(text_surface, NULL, draw_screen, &text_pos);
            break;

        case MENU_TYPE_LOAD:
            /// ---- Write slot -----
            if(quick_load_slot_chosen){
                sprintf(text_tmp, "FROM AUTO SAVE");
            }
            else{
                sprintf(text_tmp, "FROM SLOT   < %d >", state_slot+1);
            }
            text_surface = TTF_RenderText_Blended(menu_info_font, text_tmp, text_color);
            text_pos.x = (draw_screen->w - MENU_ZONE_WIDTH)/2 + (MENU_ZONE_WIDTH - text_surface->w)/2;
            text_pos.y = draw_screen->h - MENU_ZONE_HEIGHT/2 - text_surface->h/2;
            SDL_BlitSurface(text_surface, NULL, draw_screen, &text_pos);

            if(menu_action){
                sprintf(text_tmp, "Loading...");
                text_surface = TTF_RenderText_Blended(menu_info_font, text_tmp, text_color);
            }
            else{
                if(menu_confirmation){
                    sprintf(text_tmp, "Are you sure ?");
                    text_surface = TTF_RenderText_Blended(menu_info_font, text_tmp, text_color);
                }
                else{
                    if(quick_load_slot_chosen){
                        text_surface = TTF_RenderText_Blended(menu_info_font, " ", text_color);
                    }
                    else{
                        /// ---- Write current Save state ----
                        get_state_filename(fname, sizeof(fname), state_slot);
                        if (!CheckState(fname)){
                            char *p = strrchr (fname, '/');
                            char *basename = p ? p + 1 : (char *) fname;
                            p = strrchr (basename, '-'); *p = p ? 0 : *p;
                            if(p && p-basename > limit_filename_size){basename[limit_filename_size]=0;} //limiting size
                            char *file_info = p ? p + 1 : (char *) fname;
                            p = strrchr (file_info, '.');
                            char *save_idx = p ? p + 1 : "0";
                            sprintf(text_tmp, "%s - %s", basename, save_idx);
                            text_surface = TTF_RenderText_Blended(menu_small_info_font, text_tmp, text_color);
                        }
                        else{
                            text_surface = TTF_RenderText_Blended(menu_info_font, "Free", text_color);
                        }
                    }
                }
            }
            text_pos.x = (draw_screen->w - MENU_ZONE_WIDTH)/2 + (MENU_ZONE_WIDTH - text_surface->w)/2;
            text_pos.y = draw_screen->h - MENU_ZONE_HEIGHT/2 - text_surface->h/2 + 2*padding_y_from_center_menu_zone;
            SDL_BlitSurface(text_surface, NULL, draw_screen, &text_pos);
            break;

        case MENU_TYPE_ASPECT_RATIO:
            sprintf(text_tmp, "<   %s   >", aspect_ratio_name[aspect_ratio]);
            text_surface = TTF_RenderText_Blended(menu_info_font, text_tmp, text_color);
            text_pos.x = (draw_screen->w - MENU_ZONE_WIDTH)/2 + (MENU_ZONE_WIDTH - text_surface->w)/2;
            text_pos.y = draw_screen->h - MENU_ZONE_HEIGHT/2 - text_surface->h/2 + padding_y_from_center_menu_zone;
            SDL_BlitSurface(text_surface, NULL, draw_screen, &text_pos);
            break;

        case MENU_TYPE_EXIT:
        case MENU_TYPE_POWERDOWN:
            if(menu_confirmation){
				// Nick
                sprintf(text_tmp, idx_menus[menuItem] == MENU_TYPE_POWERDOWN ? "Change CD?" : "Are you sure ?");
                text_surface = TTF_RenderText_Blended(menu_info_font, text_tmp, text_color);
                text_pos.x = (draw_screen->w - MENU_ZONE_WIDTH)/2 + (MENU_ZONE_WIDTH - text_surface->w)/2;
                text_pos.y = draw_screen->h - MENU_ZONE_HEIGHT/2 - text_surface->h/2 + 2*padding_y_from_center_menu_zone;
                SDL_BlitSurface(text_surface, NULL, draw_screen, &text_pos);
            }
			else
			{ 
				// Nick
				if (idx_menus[menuItem] == MENU_TYPE_POWERDOWN)
				{
					sprintf(text_tmp, "CD: %d/%d", cdrIsoMultidiskSelect+1, cdrIsoMultidiskCount);

					text_surface = TTF_RenderText_Blended(menu_title_font, text_tmp, text_color);
					text_pos.x = (draw_screen->w - MENU_ZONE_WIDTH) / 2 + (MENU_ZONE_WIDTH - text_surface->w) / 2;
					text_pos.y = draw_screen->h - MENU_ZONE_HEIGHT / 2 - text_surface->h / 2;
					SDL_BlitSurface(text_surface, NULL, draw_screen, &text_pos);
				}
			}
            break;
        default:
            break;
        }

        /// ------ Free Surfaces -------
        if(text_surface)
             SDL_FreeSurface(text_surface);
    }

    /// --------- Print arrows --------
    if(print_arrows){
        /// Top arrow
        SDL_Rect pos_arrow_top;
        pos_arrow_top.x = (draw_screen->w - img_arrow_top->w)/2;
        pos_arrow_top.y = (draw_screen->h - MENU_BG_SQUARE_HEIGHT)/4 - img_arrow_top->h/2;
        SDL_BlitSurface(img_arrow_top, NULL, draw_screen, &pos_arrow_top);

        /// Bottom arrow
        SDL_Rect pos_arrow_bottom;
        pos_arrow_bottom.x = (draw_screen->w - img_arrow_bottom->w)/2;
        pos_arrow_bottom.y = draw_screen->h -
            (draw_screen->h - MENU_BG_SQUARE_HEIGHT)/4 - img_arrow_bottom->h/2;
        SDL_BlitSurface(img_arrow_bottom, NULL, draw_screen, &pos_arrow_bottom);
    }

    /// --------- Screen Rotate --------
    /*SDL_Copy_Rotate_270((uint16_t *)draw_screen->pixels, (uint16_t *)hw_screen->pixels,
                                RES_HW_SCREEN_HORIZONTAL, RES_HW_SCREEN_VERTICAL,
                                RES_HW_SCREEN_HORIZONTAL, RES_HW_SCREEN_VERTICAL);*/
    SDL_BlitSurface(draw_screen, NULL, hw_screen, NULL);

    /// --------- Render Screen ----------
    SDL_Flip(hw_screen);
}


void run_menu_loop()
{
    MENU_DEBUG_PRINTF("Launch Menu\n");

    SDL_Event event;
    SDL_Event *events;
    uint32_t prev_ms = SDL_GetTicks();
    uint32_t cur_ms = SDL_GetTicks();
    int scroll=0;
    int start_scroll=0;
    uint8_t screen_refresh = 1;
    char shell_cmd[100];
    FILE *fp;
    uint8_t menu_confirmation = 0;
    stop_menu_loop = 0;
    char fname[MAXPATHLEN];
    int reset_last_scren_on_exit = 1;

    /// ------ Load default keymap ------
    system(SHELL_CMD_KEYMAP_DEFAULT);

    /// ------ Get init values -------
    init_menu_system_values();
    int prevItem=menuItem;

    /// ------ Copy currently displayed screen -------
    /*if(SDL_BlitSurface(hw_screen, NULL, backup_hw_screen, NULL)){
        MENU_ERROR_PRINTF("ERROR Could not copy hw_screen: %s\n", SDL_GetError());
    }*/
    //uint16_t *dst_virtual = virtual_hw_screen();
    memcpy(backup_hw_screen->pixels, (uint16_t *)hw_screen->pixels,
            RES_HW_SCREEN_HORIZONTAL * RES_HW_SCREEN_VERTICAL * sizeof(u16));

    /* Stop Ampli */
    system(SHELL_CMD_AUDIO_AMP_OFF);

    /// ------ Wait for menu UP key event ------
    while(event.type != SDL_KEYUP || event.key.keysym.sym != SDLK_q){
        while (SDL_PollEvent(&event)){
            SDL_PushEvent(&event);
            update_input();
        }

        /* 500ms timeout */
        if(SDL_GetTicks() - cur_ms > 500){
            MENU_ERROR_PRINTF("Timeout waiting for SDLK_q UP\n");
            break;
        }
    }


    /// -------- Main loop ---------
    while (!stop_menu_loop)
    {
        /// -------- Handle Keyboard Events ---------
        if(!scroll){

            while (SDL_PollEvent(&event))
            switch(event.type)
            {
	    case SDL_QUIT:
                    stop_menu_loop = 1;
		    break;
	    case SDL_KEYDOWN:
                switch (event.key.keysym.sym)
                {
                    case SDLK_b:
                        if(menu_confirmation){
                            /// ------ Reset menu confirmation ------
                            menu_confirmation = 0;
                            /// ------ Refresh screen ------
                            screen_refresh = 1;
                        }
                        /*else{
                            stop_menu_loop = 1;
                        }*/
                        break;

                    case SDLK_q:
                    case SDLK_ESCAPE:
                        stop_menu_loop = 1;
                        break;

                    case SDLK_d:
                    case SDLK_DOWN:
                        MENU_DEBUG_PRINTF("DOWN\n");
                        /// ------ Start scrolling to new menu -------
                        menuItem++;
                        if (menuItem>=nb_menu_zones) menuItem=0;
                        if (cdrIsoMultidiskCount<=1 && menuItem>=nb_menu_zones-1) menuItem=0;
                        start_scroll=1;

                        /// ------ Reset menu confirmation ------
                        menu_confirmation = 0;

                        /// ------ Refresh screen ------
                        screen_refresh = 1;
                        break;

                    case SDLK_u:
                    case SDLK_UP:
                        MENU_DEBUG_PRINTF("UP\n");
                        /// ------ Start scrolling to new menu -------
                        menuItem--;
                        if (menuItem<0){
                            menuItem=nb_menu_zones-1;
                            if (cdrIsoMultidiskCount<=1) menuItem=nb_menu_zones-2;
                        }
                        start_scroll=-1;

                        /// ------ Reset menu confirmation ------
                        menu_confirmation = 0;

                        /// ------ Refresh screen ------
                        screen_refresh = 1;
                        break;

                    case SDLK_l:
                    case SDLK_LEFT:
                        //MENU_DEBUG_PRINTF("LEFT\n");
                        if(idx_menus[menuItem] == MENU_TYPE_VOLUME){
                            MENU_DEBUG_PRINTF("Volume DOWN\n");
                            /// ----- Compute new value -----
                            volume_percentage = (volume_percentage < STEP_CHANGE_VOLUME)?
                                                    0:(volume_percentage-STEP_CHANGE_VOLUME);

                            /// ----- Shell cmd ----
                            sprintf(shell_cmd, "%s %d", SHELL_CMD_VOLUME_SET, volume_percentage);
                            system(shell_cmd);

                            /// ------ Refresh screen ------
                            screen_refresh = 1;
                        }
                        else if(idx_menus[menuItem] == MENU_TYPE_BRIGHTNESS){
                            MENU_DEBUG_PRINTF("Brightness DOWN\n");
                            /// ----- Compute new value -----
                            brightness_percentage = (brightness_percentage < STEP_CHANGE_BRIGHTNESS)?
                                                    0:(brightness_percentage-STEP_CHANGE_BRIGHTNESS);

                            /// ----- Shell cmd ----
                            sprintf(shell_cmd, "%s %d", SHELL_CMD_BRIGHTNESS_SET, brightness_percentage);
                            system(shell_cmd);

			    /// ------ Refresh screen ------
                            screen_refresh = 1;
                        }
                        else if(idx_menus[menuItem] == MENU_TYPE_SAVE){
                            MENU_DEBUG_PRINTF("Save Slot DOWN\n");
                            state_slot = (!state_slot)?(MAX_SAVE_SLOTS-1):(state_slot-1);
                            /// ------ Refresh screen ------
                            screen_refresh = 1;
                        }
                        else if(idx_menus[menuItem] == MENU_TYPE_LOAD){
                            MENU_DEBUG_PRINTF("Load Slot DOWN\n");

                            /** Choose quick save file or standard saveslot for loading */
                            if(!quick_load_slot_chosen &&
                                state_slot == 0 &&
                                access(quick_save_file, F_OK ) != -1){
                                quick_load_slot_chosen = 1;
                            }
                            else if(quick_load_slot_chosen){
                                quick_load_slot_chosen = 0;
                                state_slot = MAX_SAVE_SLOTS-1;
                            }
                            else{
                                state_slot = (!state_slot)?(MAX_SAVE_SLOTS-1):(state_slot-1);
                            }

                            /// ------ Refresh screen ------
                            screen_refresh = 1;
                        }
                        else if(idx_menus[menuItem] == MENU_TYPE_ASPECT_RATIO){
                            MENU_DEBUG_PRINTF("Aspect Ratio DOWN\n");
                            aspect_ratio = (!aspect_ratio)?(NB_ASPECT_RATIOS_TYPES-1):(aspect_ratio-1);
                            
                            /// ------ Refresh screen ------
                            screen_refresh = 1;

                            // Save config file
                            configfile_save(cfg_file_rom);
                        }
                        break;

                    case SDLK_r:
                    case SDLK_RIGHT:
                        //MENU_DEBUG_PRINTF("RIGHT\n");
                        if(idx_menus[menuItem] == MENU_TYPE_VOLUME){
                            MENU_DEBUG_PRINTF("Volume UP\n");
                            /// ----- Compute new value -----
                            volume_percentage = (volume_percentage > 100 - STEP_CHANGE_VOLUME)?
                                                    100:(volume_percentage+STEP_CHANGE_VOLUME);

                            /// ----- Shell cmd ----
                            sprintf(shell_cmd, "%s %d", SHELL_CMD_VOLUME_SET, volume_percentage);
                            system(shell_cmd);

                            /// ------ Refresh screen ------
                            screen_refresh = 1;
                        }
                        else if(idx_menus[menuItem] == MENU_TYPE_BRIGHTNESS){
                            MENU_DEBUG_PRINTF("Brightness UP\n");
                            /// ----- Compute new value -----
                            brightness_percentage = (brightness_percentage > 100 - STEP_CHANGE_BRIGHTNESS)?
                                                    100:(brightness_percentage+STEP_CHANGE_BRIGHTNESS);

                            /// ----- Shell cmd ----
                            sprintf(shell_cmd, "%s %d", SHELL_CMD_BRIGHTNESS_SET, brightness_percentage);
                            system(shell_cmd);

                            /// ------ Refresh screen ------
                            screen_refresh = 1;
                        }
                        else if(idx_menus[menuItem] == MENU_TYPE_SAVE){
                            MENU_DEBUG_PRINTF("Save Slot UP\n");
                            state_slot = (state_slot+1)%MAX_SAVE_SLOTS;
                            /// ------ Refresh screen ------
                            screen_refresh = 1;
                        }
                        else if(idx_menus[menuItem] == MENU_TYPE_LOAD){
                            MENU_DEBUG_PRINTF("Load Slot UP\n");

                            /** Choose quick save file or standard saveslot for loading */
                            if(!quick_load_slot_chosen &&
                                state_slot == MAX_SAVE_SLOTS-1 &&
                                access(quick_save_file, F_OK ) != -1){
                                quick_load_slot_chosen = 1;
                            }
                            else if(quick_load_slot_chosen){
                                quick_load_slot_chosen = 0;
                                state_slot = 0;
                            }
                            else{
                                state_slot = (state_slot+1)%MAX_SAVE_SLOTS;
                            }

                            /// ------ Refresh screen ------
                            screen_refresh = 1;
                        }
                        else if(idx_menus[menuItem] == MENU_TYPE_ASPECT_RATIO){
                            MENU_DEBUG_PRINTF("Aspect Ratio UP\n");
                            aspect_ratio = (aspect_ratio+1)%NB_ASPECT_RATIOS_TYPES;
                            
                            /// ------ Refresh screen ------
                            screen_refresh = 1;

                            // Save config file
                            configfile_save(cfg_file_rom);
                        }
                        break;

                    case SDLK_a:
                    case SDLK_RETURN:
                        if(idx_menus[menuItem] == MENU_TYPE_SAVE){
                            if(menu_confirmation){
                                MENU_DEBUG_PRINTF("Saving in slot %d\n", state_slot);
                                /// ------ Refresh Screen -------
                                menu_screen_refresh(menuItem, prevItem, scroll, menu_confirmation, 1);

                                /// ------ Save game ------
                                int ret = emu_save_load_game(0, 0);

                                /// ----- Hud Msg -----
                                if(ret){
                                    MENU_ERROR_PRINTF("Save Failed\n");
                                    sprintf(shell_cmd, "%s %d \"          SAVE FAILED\"",
                                        SHELL_CMD_NOTIF_SET, NOTIF_SECONDS_DISP);
                                }
                                else{
                                    sprintf(shell_cmd, "%s %d \"        SAVED IN SLOT %d\"",
                                        SHELL_CMD_NOTIF_SET, NOTIF_SECONDS_DISP, state_slot+1);
                                }
                                system(shell_cmd);
                                stop_menu_loop = 1;
                            }
                            else{
                                MENU_DEBUG_PRINTF("Save game - asking confirmation\n");
                                menu_confirmation = 1;
                                /// ------ Refresh screen ------
                                screen_refresh = 1;
                            }
                        }
                        else if(idx_menus[menuItem] == MENU_TYPE_LOAD){
                            if(menu_confirmation){
                                MENU_DEBUG_PRINTF("Loading in slot %d\n", state_slot);
                                /// ------ Refresh Screen -------
                                menu_screen_refresh(menuItem, prevItem, scroll, menu_confirmation, 1);

                                /// ------ Load game ------
                                int ret;
                                if(quick_load_slot_chosen){
                                    ret = LoadState(quick_save_file);
                                }
                                else{
                                    ret = emu_save_load_game(1, 0);
                                }

                                /// ----- Hud Msg -----
                                if(ret){
                                    MENU_ERROR_PRINTF("Load Failed\n");
                                    sprintf(shell_cmd, "%s %d \"          LOAD FAILED\"",
                                        SHELL_CMD_NOTIF_SET, NOTIF_SECONDS_DISP);
                                }
                                else{
                                    if(quick_load_slot_chosen){
                                        sprintf(shell_cmd, "%s %d \"     LOADED FROM AUTO SAVE\"",
                                            SHELL_CMD_NOTIF_SET, NOTIF_SECONDS_DISP);
                                    }
                                    else{
                                        sprintf(shell_cmd, "%s %d \"      LOADED FROM SLOT %d\"",
                                            SHELL_CMD_NOTIF_SET, NOTIF_SECONDS_DISP, state_slot+1);
                                    }
                                }
                                system(shell_cmd);
                                stop_menu_loop = 1;
                            }
                            else{
                                MENU_DEBUG_PRINTF("Save game - asking confirmation\n");
                                menu_confirmation = 1;
                                /// ------ Refresh screen ------
                                screen_refresh = 1;
                            }
                        }
                        else if(idx_menus[menuItem] == MENU_TYPE_EXIT){
                            MENU_DEBUG_PRINTF("Exit game\n");
                            if(menu_confirmation){
                                MENU_DEBUG_PRINTF("Exit game - confirmed\n");

                                /// ----- Long save ahead, let' clear the screen for a better effect -----
                                memset(hw_screen->pixels, 0, hw_screen->w*hw_screen->w*hw_screen->format->BytesPerPixel);
                                reset_last_scren_on_exit = 0;
                                SDL_Flip(hw_screen);

                                /// ----- The game should be saved here ----
                                if(SaveState(quick_save_file)){
                                    MENU_ERROR_PRINTF("Quick save failed");
                                    return;
                                }

                                /// ----- Exit game and back to launcher ----
                                emu_core_ask_exit();
                                stop_menu_loop = 1;
                            }
                            else{
                                MENU_DEBUG_PRINTF("Exit game - asking confirmation\n");
                                menu_confirmation = 1;
                                /// ------ Refresh screen ------
                                screen_refresh = 1;
                            }
                        }
                        else if(idx_menus[menuItem] == MENU_TYPE_POWERDOWN){
                            if(menu_confirmation){
								// Nick
								if (cdrIsoMultidiskCount > 1)
									swap_cd_multidisk();
								stop_menu_loop = 1;
                                /*MENU_DEBUG_PRINTF("Powerdown - confirmed\n");
                                /// ----- Shell cmd ----
                                execlp(SHELL_CMD_POWERDOWN, SHELL_CMD_POWERDOWN, NULL);
                                MENU_ERROR_PRINTF("Failed to run command %s\n", SHELL_CMD_POWERDOWN);
                                exit(0);*/
                            }
                            else{
                                MENU_DEBUG_PRINTF("Powerdown - asking confirmation\n");
                                menu_confirmation = 1;
                                /// ------ Refresh screen ------
                                screen_refresh = 1;
                            }
                        }
                        break;

                    default:
                        //MENU_DEBUG_PRINTF("Keydown: %d\n", event.key.keysym.sym);
                        break;
                }
                break;
            }
        }

        /// --------- Handle Scroll effect ---------
        if ((scroll>0) || (start_scroll>0)){
            scroll+=MIN(SCROLL_SPEED_PX, MENU_ZONE_HEIGHT-scroll);
            start_scroll = 0;
            screen_refresh = 1;
        }
        else if ((scroll<0) || (start_scroll<0)){
            scroll-=MIN(SCROLL_SPEED_PX, MENU_ZONE_HEIGHT+scroll);
            start_scroll = 0;
            screen_refresh = 1;
        }
        if (scroll>=MENU_ZONE_HEIGHT || scroll<=-MENU_ZONE_HEIGHT) {
            prevItem=menuItem;
            scroll=0;
            screen_refresh = 1;
        }

        /// --------- Handle FPS ---------
        cur_ms = SDL_GetTicks();
        if(cur_ms-prev_ms < 1000/FPS_MENU){
            SDL_Delay(1000/FPS_MENU - (cur_ms-prev_ms));
        }
        prev_ms = SDL_GetTicks();


        /// --------- Refresh screen
        if(screen_refresh){
            menu_screen_refresh(menuItem, prevItem, scroll, menu_confirmation, 0);
        }

        /// --------- reset screen refresh ---------
        screen_refresh = 0;
    }

    /// ------ Restore last keymap ------
    system(SHELL_CMD_KEYMAP_RESUME);

    /// ------ Reset prev key repeat params -------
    if(SDL_EnableKeyRepeat(backup_key_repeat_delay, backup_key_repeat_interval)){
        MENU_ERROR_PRINTF("ERROR with SDL_EnableKeyRepeat: %s\n", SDL_GetError());
    }

    /* Start Ampli */
    system(SHELL_CMD_AUDIO_AMP_ON);

    /// ------ Reset last screen ------
    if(reset_last_scren_on_exit){
        SDL_BlitSurface(backup_hw_screen, NULL, hw_screen, NULL);
        SDL_Flip(hw_screen);
    }
}




/****************************/
/*    Quick Resume Menu     */
/****************************/
int launch_resume_menu_loop()
{
    MENU_DEBUG_PRINTF("Init resume menu\n");

    /* Decare vars */
    SDL_Surface *text_surface = NULL;
    char text_tmp[40];
    SDL_Rect text_pos;
    SDL_Event event;
    uint32_t prev_ms = SDL_GetTicks();
    uint32_t cur_ms = SDL_GetTicks();
    stop_menu_loop = 0;
    uint8_t screen_refresh = 1;
    uint8_t menu_confirmation = 0;
    int option_idx=RESUME_YES;
    FILE *fp;

    /* Stop Ampli */
    system(SHELL_CMD_AUDIO_AMP_OFF);

    /* Save prev key repeat params and set new Key repeat */
    SDL_GetKeyRepeat(&backup_key_repeat_delay, &backup_key_repeat_interval);
    if(SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL)){
        MENU_ERROR_PRINTF("ERROR with SDL_EnableKeyRepeat: %s\n", SDL_GetError());
    }

    /* Load BG */
    SDL_Surface *img_square_bg = IMG_Load(MENU_PNG_BG_PATH);
    if(!img_square_bg) {
        MENU_ERROR_PRINTF("ERROR IMG_Load: %s\n", IMG_GetError());
    }
    SDL_Surface *bg_surface = SDL_CreateRGBSurface(SDL_SWSURFACE, hw_screen->w, hw_screen->h, 16, 0, 0, 0, 0);
    SDL_BlitSurface(img_square_bg, NULL, bg_surface, NULL);
    SDL_FreeSurface(img_square_bg);


    /*  Print top arrow */
    SDL_Rect pos_arrow_top;
    pos_arrow_top.x = (bg_surface->w - img_arrow_top->w)/2;
    pos_arrow_top.y = (bg_surface->h - MENU_BG_SQUARE_HEIGHT)/4 - img_arrow_top->h/2;
    SDL_BlitSurface(img_arrow_top, NULL, bg_surface, &pos_arrow_top);

    /*  Print bottom arrow */
    SDL_Rect pos_arrow_bottom;
    pos_arrow_bottom.x = (bg_surface->w - img_arrow_bottom->w)/2;
    pos_arrow_bottom.y = bg_surface->h -
            (bg_surface->h - MENU_BG_SQUARE_HEIGHT)/4 - img_arrow_bottom->h/2;
    SDL_BlitSurface(img_arrow_bottom, NULL, bg_surface, &pos_arrow_bottom);

    if (text_surface)
        SDL_FreeSurface(text_surface);

    /* Main loop */
    while (!stop_menu_loop)
    {
        /* Handle keyboard events */
        while (SDL_PollEvent(&event))
        switch(event.type)
        {
            case SDL_QUIT:
                stop_menu_loop = 1;
                break;
            case SDL_KEYDOWN:
                switch (event.key.keysym.sym)
                {
                    case SDLK_b:
                        if(menu_confirmation){
                            /// ------ Reset menu confirmation ------
                            menu_confirmation = 0;

                            /// ------ Refresh screen ------
                            screen_refresh = 1;
                        }
                        /*else{
                            stop_menu_loop = 1;
                        }*/
                        break;

                    case SDLK_q:
                    case SDLK_ESCAPE:
                        /*stop_menu_loop = 1;*/
                        break;

                    case SDLK_u:
                    case SDLK_UP:
                        MENU_DEBUG_PRINTF("Option UP\n");
                        option_idx = (!option_idx)?(NB_RESUME_OPTIONS-1):(option_idx-1);

                        /// ------ Reset menu confirmation ------
                        menu_confirmation = 0;

                        /// ------ Refresh screen ------
                        screen_refresh = 1;
                        break;

                    case SDLK_d:
                    case SDLK_DOWN:
                        MENU_DEBUG_PRINTF("Option DWON\n");
                        option_idx = (option_idx+1)%NB_RESUME_OPTIONS;

                        /// ------ Reset menu confirmation ------
                        menu_confirmation = 0;

                        /// ------ Refresh screen ------
                        screen_refresh = 1;
                        break;

                    case SDLK_a:
                    case SDLK_RETURN:
                        MENU_DEBUG_PRINTF("Pressed A\n");
                        if(menu_confirmation){
                            MENU_DEBUG_PRINTF("Confirmed\n");

                            /// ----- exit menu  ----
                            stop_menu_loop = 1;
                        }
                        else{
                            MENU_DEBUG_PRINTF("Asking confirmation\n");
                            menu_confirmation = 1;

                            /// ------ Refresh screen ------
                            screen_refresh = 1;
                        }
                        break;

                    default:
                        //MENU_DEBUG_PRINTF("Keydown: %d\n", event.key.keysym.sym);
                        break;
            }
            break;
        }

        /* Handle FPS */
        cur_ms = SDL_GetTicks();
        if(cur_ms-prev_ms < 1000/FPS_MENU){
            SDL_Delay(1000/FPS_MENU - (cur_ms-prev_ms));
        }
        prev_ms = SDL_GetTicks();

        /* Refresh screen */
        if(screen_refresh){
            /* Clear and draw BG */
            SDL_FillRect(hw_screen, NULL, 0);
            if(SDL_BlitSurface(bg_surface, NULL, hw_screen, NULL)){
                MENU_ERROR_PRINTF("ERROR Could not draw background: %s\n", SDL_GetError());
            }

            /* Draw resume or reset option */
            text_surface = TTF_RenderText_Blended(menu_title_font, resume_options_str[option_idx], text_color);
            text_pos.x = (hw_screen->w - MENU_ZONE_WIDTH)/2 + (MENU_ZONE_WIDTH - text_surface->w)/2;
            text_pos.y = hw_screen->h - MENU_ZONE_HEIGHT/2 - text_surface->h/2;
            SDL_BlitSurface(text_surface, NULL, hw_screen, &text_pos);

            /* Draw confirmation */
            if(menu_confirmation){
                sprintf(text_tmp, "Are you sure ?");
                text_surface = TTF_RenderText_Blended(menu_info_font, text_tmp, text_color);
                text_pos.x = (hw_screen->w - MENU_ZONE_WIDTH)/2 + (MENU_ZONE_WIDTH - text_surface->w)/2;
                text_pos.y = hw_screen->h - MENU_ZONE_HEIGHT/2 - text_surface->h/2 + 2*padding_y_from_center_menu_zone;
                SDL_BlitSurface(text_surface, NULL, hw_screen, &text_pos);
            }

            /* Flip Screen */
            SDL_Flip(hw_screen);
        }

        /* reset screen refresh */
        screen_refresh = 0;
    }

    /// ----- Clear screen -----
    SDL_FillRect(hw_screen, NULL, 0);
    SDL_Flip(hw_screen);

    /* Free SDL Surfaces */
    if(bg_surface)
        SDL_FreeSurface(bg_surface);
    if(text_surface)
        SDL_FreeSurface(text_surface);

    /* Reset prev key repeat params */
    if(SDL_EnableKeyRepeat(backup_key_repeat_delay, backup_key_repeat_interval)){
        MENU_ERROR_PRINTF("ERROR with SDL_EnableKeyRepeat: %s\n", SDL_GetError());
    }

    /* Start Ampli */
    system(SHELL_CMD_AUDIO_AMP_ON);

    return option_idx;
}


void emu_make_path(char *buff, const char *end, int size)
{
	int pos, end_len;

	end_len = strlen(end);
	pos = plat_get_root_dir(buff, size);
	strncpy(buff + pos, end, size - pos);
	buff[size - 1] = 0;
	if (pos + end_len > size - 1)
		printf("Warning: path truncated: %s\n", buff);
}

static int emu_check_save_file(int slot, int *time)
{
	char fname[MAXPATHLEN];
	struct stat status;
	int ret;

	ret = emu_check_state(slot);
	if (ret != 0 || time == NULL)
		return ret == 0 ? 1 : 0;

	ret = get_state_filename(fname, sizeof(fname), slot);
	if (ret != 0)
		return 1;

	ret = stat(fname, &status);
	if (ret != 0)
		return 1;

	if (status.st_mtime < REARMED_BIRTHDAY_TIME)
		return 1; // probably bad rtc like on some Caanoos

	*time = status.st_mtime;

	return 1;
}

static int emu_save_load_game(int load, int unused)
{
	int ret;

	if (load) {
		ret = emu_load_state(state_slot);

		// reflect hle/bios mode from savestate
		if (Config.HLE)
			bios_sel = 0;
		else if (bios_sel == 0 && bioses[1] != NULL)
			// XXX: maybe find the right bios instead
			bios_sel = 1;
	}
	else
		ret = emu_save_state(state_slot);

	return ret;
}

static void rm_namelist_entry(struct dirent **namelist,
	int count, const char *name)
{
	int i;

	for (i = 1; i < count; i++) {
		if (namelist[i] == NULL || namelist[i]->d_type == DT_DIR)
			continue;

		if (strcmp(name, namelist[i]->d_name) == 0) {
			free(namelist[i]);
			namelist[i] = NULL;
			break;
		}
	}
}

static int optional_cdimg_filter(struct dirent **namelist, int count,
	const char *basedir)
{
	const char *ext, *p;
	char buf[256], buf2[256];
	int i, d, ret, good_cue;
	struct stat64 statf;
	FILE *f;

	if (count <= 1)
		return count;

	for (i = 1; i < count; i++) {
		if (namelist[i] == NULL || namelist[i]->d_type == DT_DIR)
			continue;

		ext = strrchr(namelist[i]->d_name, '.');
		if (ext == NULL) {
			// should not happen but whatever
			free(namelist[i]);
			namelist[i] = NULL;
			continue;
		}
		ext++;

		// first find .cue files and remove files they reference
		if (strcasecmp(ext, "cue") == 0)
		{
			snprintf(buf, sizeof(buf), "%s/%s", basedir,
				namelist[i]->d_name);

			f = fopen(buf, "r");
			if (f == NULL) {
				free(namelist[i]);
				namelist[i] = NULL;
				continue;
			}

			good_cue = 0;
			while (fgets(buf, sizeof(buf), f)) {
				ret = sscanf(buf, " FILE \"%256[^\"]\"", buf2);
				if (ret != 1)
					ret = sscanf(buf, " FILE %256s", buf2);
				if (ret != 1)
					continue;

				p = strrchr(buf2, '/');
				if (p == NULL)
					p = strrchr(buf2, '\\');
				if (p != NULL)
					p++;
				else
					p = buf2;

				snprintf(buf, sizeof(buf), "%s/%s", basedir, p);
				ret = stat64(buf, &statf);
				if (ret == 0) {
					rm_namelist_entry(namelist, count, p);
					good_cue = 1;
				}
			}
			fclose(f);

			if (!good_cue) {
				free(namelist[i]);
				namelist[i] = NULL;
			}
			continue;
		}

		p = strcasestr(namelist[i]->d_name, "track");
		if (p != NULL) {
			ret = strtoul(p + 5, NULL, 10);
			if (ret > 1) {
				free(namelist[i]);
				namelist[i] = NULL;
				continue;
			}
		}
	}

	// compact namelist
	for (i = d = 1; i < count; i++)
		if (namelist[i] != NULL)
			namelist[d++] = namelist[i];

	return d;
}

// propagate menu settings to the emu vars
static void menu_sync_config(void)
{
	static int allow_abs_only_old;

	Config.PsxAuto = 1;
	if (region > 0) {
		Config.PsxAuto = 0;
		Config.PsxType = region - 1;
	}
	Config.cycle_multiplier = 10000 / psx_clock;

	switch (in_type_sel1) {
	case 1:  in_type[0] = PSE_PAD_TYPE_ANALOGPAD; break;
	case 2:  in_type[0] = PSE_PAD_TYPE_NEGCON;    break;
	default: in_type[0] = PSE_PAD_TYPE_STANDARD;
	}
	switch (in_type_sel2) {
	case 1:  in_type[1] = PSE_PAD_TYPE_ANALOGPAD; break;
	case 2:  in_type[1] = PSE_PAD_TYPE_NEGCON;    break;
	default: in_type[1] = PSE_PAD_TYPE_STANDARD;
	}
	if (in_evdev_allow_abs_only != allow_abs_only_old) {
		in_probe();
		allow_abs_only_old = in_evdev_allow_abs_only;
	}

	spu_config.iVolume = 768 + 128 * volume_boost;
	pl_rearmed_cbs.frameskip = frameskip - 1;
	pl_timing_prepare(Config.PsxType);
}

static void menu_set_defconfig(void)
{
	emu_set_default_config();

	//g_opts = 0;
	g_scaler = SCALE_4_3;
	g_gamma = 100;
	volume_boost = 0;
	frameskip = 0;
	//frameskip = 1;
	analog_deadzone = 50;
	soft_scaling = 1;
	soft_filter = 0;
	scanlines = 0;
	scanline_level = 20;
	plat_target.vout_fullscreen = 0;
	psx_clock = DEFAULT_PSX_CLOCK;

	region = 0;
	in_type_sel1 = in_type_sel2 = 0;
	in_evdev_allow_abs_only = 0;

	menu_sync_config();
}

#define CE_CONFIG_STR(val) \
	{ #val, 0, Config.val }

#define CE_CONFIG_VAL(val) \
	{ #val, sizeof(Config.val), &Config.val }

#define CE_STR(val) \
	{ #val, 0, val }

#define CE_INTVAL(val) \
	{ #val, sizeof(val), &val }

#define CE_INTVAL_N(name, val) \
	{ name, sizeof(val), &val }

#define CE_INTVAL_P(val) \
	{ #val, sizeof(pl_rearmed_cbs.val), &pl_rearmed_cbs.val }

// 'versioned' var, used when defaults change
#define CE_CONFIG_STR_V(val, ver) \
	{ #val #ver, 0, Config.val }

#define CE_INTVAL_V(val, ver) \
	{ #val #ver, sizeof(val), &val }

#define CE_INTVAL_PV(val, ver) \
	{ #val #ver, sizeof(pl_rearmed_cbs.val), &pl_rearmed_cbs.val }

static const struct {
	const char *name;
	size_t len;
	void *val;
} config_data[] = {
	CE_CONFIG_STR(Bios),
	CE_CONFIG_STR_V(Gpu, 3),
	CE_CONFIG_STR(Spu),
//	CE_CONFIG_STR(Cdr),
	CE_CONFIG_VAL(Xa),
	CE_CONFIG_VAL(Mdec),
	CE_CONFIG_VAL(Cdda),
	CE_CONFIG_VAL(Debug),
	CE_CONFIG_VAL(PsxOut),
	CE_CONFIG_VAL(icache_emulation),
	CE_CONFIG_VAL(DisableStalls),
	CE_CONFIG_VAL(Cpu),
	CE_CONFIG_VAL(GpuListWalking),
	CE_INTVAL(region),
	CE_INTVAL_V(g_scaler, 3),
	CE_INTVAL(g_gamma),
	CE_INTVAL(g_layer_x),
	CE_INTVAL(g_layer_y),
	CE_INTVAL(g_layer_w),
	CE_INTVAL(g_layer_h),
	CE_INTVAL(soft_filter),
	CE_INTVAL(scanlines),
	CE_INTVAL(scanline_level),
	CE_INTVAL(plat_target.vout_method),
	CE_INTVAL(plat_target.hwfilter),
	CE_INTVAL(plat_target.vout_fullscreen),
	CE_INTVAL(state_slot),
	CE_INTVAL(cpu_clock),
	CE_INTVAL(g_opts),
	CE_INTVAL(in_type_sel1),
	CE_INTVAL(in_type_sel2),
	CE_INTVAL(analog_deadzone),
	CE_INTVAL(memcard1_sel),
	CE_INTVAL(memcard2_sel),
	CE_INTVAL(g_autostateld_opt),
	CE_INTVAL_N("adev0_is_nublike", in_adev_is_nublike[0]),
	CE_INTVAL_N("adev1_is_nublike", in_adev_is_nublike[1]),
	CE_INTVAL_V(frameskip, 3),
	CE_INTVAL_P(gpu_peops.iUseDither),
	CE_INTVAL_P(gpu_peops.dwActFixes),
	CE_INTVAL_P(gpu_unai.lineskip),
	CE_INTVAL_P(gpu_unai.abe_hack),
	CE_INTVAL_P(gpu_unai.no_light),
	CE_INTVAL_P(gpu_unai.no_blend),
	CE_INTVAL_P(gpu_senquack.ilace_force),
	CE_INTVAL_P(gpu_senquack.pixel_skip),
	CE_INTVAL_P(gpu_senquack.lighting),
	CE_INTVAL_P(gpu_senquack.fast_lighting),
	CE_INTVAL_P(gpu_senquack.blending),
	CE_INTVAL_P(gpu_senquack.dithering),
	CE_INTVAL_P(gpu_senquack.scale_hires),
	CE_INTVAL_P(gpu_neon.allow_interlace),
	CE_INTVAL_P(gpu_neon.enhancement_enable),
	CE_INTVAL_P(gpu_neon.enhancement_no_main),
	CE_INTVAL_P(gpu_peopsgl.bDrawDither),
	CE_INTVAL_P(gpu_peopsgl.iFilterType),
	CE_INTVAL_P(gpu_peopsgl.iFrameTexType),
	CE_INTVAL_P(gpu_peopsgl.iUseMask),
	CE_INTVAL_P(gpu_peopsgl.bOpaquePass),
	CE_INTVAL_P(gpu_peopsgl.bAdvancedBlend),
	CE_INTVAL_P(gpu_peopsgl.bUseFastMdec),
	CE_INTVAL_P(gpu_peopsgl.iVRamSize),
	CE_INTVAL_P(gpu_peopsgl.iTexGarbageCollection),
	CE_INTVAL_P(gpu_peopsgl.dwActFixes),
	CE_INTVAL(spu_config.iUseReverb),
	CE_INTVAL(spu_config.iXAPitch),
	CE_INTVAL(spu_config.iUseInterpolation),
	CE_INTVAL(spu_config.iTempo),
	CE_INTVAL(spu_config.iUseThread),
	CE_INTVAL(config_save_counter),
	CE_INTVAL(in_evdev_allow_abs_only),
	CE_INTVAL(volume_boost),
	CE_INTVAL(psx_clock),
	CE_INTVAL(new_dynarec_hacks),
	CE_INTVAL(in_enable_vibration),
};

static char *get_cd_label(void)
{
	static char trimlabel[33];
	int j;

	strncpy(trimlabel, CdromLabel, 32);
	trimlabel[32] = 0;
	for (j = 31; j >= 0; j--)
		if (trimlabel[j] == ' ')
			trimlabel[j] = 0;

	return trimlabel;
}

static void make_relative_cfg_fname(char *buf, size_t size, int is_game)
{
	if (is_game)
		snprintf(buf, size, "." PCSX_DOT_DIR "cfg/%.32s-%.9s.cfg", get_cd_label(), CdromId);
	else
		snprintf(buf, size, "." PCSX_DOT_DIR "%s", cfgfile_basename);
}

static void make_absolute_cfg_fname(char *buf, size_t size, int is_game)
{
    if (is_game)
        snprintf(buf, size, PCSX_DOT_DIR "cfg/%.32s-%.9s.cfg", get_cd_label(), CdromId);
    else
        snprintf(buf, size, PCSX_DOT_DIR "%s", cfgfile_basename);
}

static void keys_write_all(FILE *f);
static char *mystrip(char *str);

static int menu_write_config(int is_game)
{
	char cfgfile[MAXPATHLEN];
	FILE *f;
	int i;

	config_save_counter++;

	make_absolute_cfg_fname(cfgfile, sizeof(cfgfile), is_game);
	f = fopen(cfgfile, "w");
	if (f == NULL) {
		printf("menu_write_config: failed to open: %s\n", cfgfile);
		return -1;
	}

	for (i = 0; i < ARRAY_SIZE(config_data); i++) {
		fprintf(f, "%s = ", config_data[i].name);
		switch (config_data[i].len) {
		case 0:
			fprintf(f, "%s\n", (char *)config_data[i].val);
			break;
		case 1:
			fprintf(f, "%x\n", *(u8 *)config_data[i].val);
			break;
		case 2:
			fprintf(f, "%x\n", *(u16 *)config_data[i].val);
			break;
		case 4:
			fprintf(f, "%x\n", *(u32 *)config_data[i].val);
			break;
		default:
			printf("menu_write_config: unhandled len %d for %s\n",
				 (int)config_data[i].len, config_data[i].name);
			break;
		}
	}

	keys_write_all(f);
	fclose(f);

	return 0;
}

static int menu_do_last_cd_img(int is_get)
{
	static const char *defaults[] = { "/media", "/mnt/sd", "/mnt" };
	char path[256];
	struct stat64 st;
	FILE *f;
	int i, ret = -1;

	snprintf(path, sizeof(path), "." PCSX_DOT_DIR "lastcdimg.txt");
	f = fopen(path, is_get ? "r" : "w");
	if (f == NULL) {
		ret = -1;
		goto out;
	}

	if (is_get) {
		ret = fread(last_selected_fname, 1, sizeof(last_selected_fname) - 1, f);
		last_selected_fname[ret] = 0;
		mystrip(last_selected_fname);
	}
	else
		fprintf(f, "%s\n", last_selected_fname);
	fclose(f);

out:
	if (is_get) {
		for (i = 0; last_selected_fname[0] == 0
		       || stat64(last_selected_fname, &st) != 0; i++)
		{
			if (i >= ARRAY_SIZE(defaults))
				break;
			strcpy(last_selected_fname, defaults[i]);
		}
	}

	return 0;
}

static void parse_str_val(char *cval, const char *src)
{
	char *tmp;
	strncpy(cval, src, MAXPATHLEN);
	cval[MAXPATHLEN - 1] = 0;
	tmp = strchr(cval, '\n');
	if (tmp == NULL)
		tmp = strchr(cval, '\r');
	if (tmp != NULL)
		*tmp = 0;
}

static void keys_load_all(const char *cfg);

static int menu_load_config(int is_game)
{
	char cfgfile[MAXPATHLEN];
	int i, ret = -1;
	long size;
	char *cfg;
	FILE *f;

	make_absolute_cfg_fname(cfgfile, sizeof(cfgfile), is_game);
	f = fopen(cfgfile, "r");
	if (f == NULL) {
		printf("menu_load_config: failed to open: %s\n", cfgfile);
		goto fail;
	}

	fseek(f, 0, SEEK_END);
	size = ftell(f);
	if (size <= 0) {
		printf("bad size %ld: %s\n", size, cfgfile);
		goto fail;
	}

	cfg = malloc(size + 1);
	if (cfg == NULL)
		goto fail;

	fseek(f, 0, SEEK_SET);
	if (fread(cfg, 1, size, f) != size) {
		printf("failed to read: %s\n", cfgfile);
		goto fail_read;
	}
	cfg[size] = 0;

	for (i = 0; i < ARRAY_SIZE(config_data); i++) {
		char *tmp, *tmp2;
		u32 val;

		tmp = strstr(cfg, config_data[i].name);
		if (tmp == NULL)
			continue;
		tmp += strlen(config_data[i].name);
		if (strncmp(tmp, " = ", 3) != 0)
			continue;
		tmp += 3;

		if (config_data[i].len == 0) {
			parse_str_val(config_data[i].val, tmp);
			continue;
		}

		tmp2 = NULL;
		val = strtoul(tmp, &tmp2, 16);
		if (tmp2 == NULL || tmp == tmp2)
			continue; // parse failed

		switch (config_data[i].len) {
		case 1:
			*(u8 *)config_data[i].val = val;
			break;
		case 2:
			*(u16 *)config_data[i].val = val;
			break;
		case 4:
			*(u32 *)config_data[i].val = val;
			break;
		default:
			printf("menu_load_config: unhandled len %d for %s\n",
				 (int)config_data[i].len, config_data[i].name);
			break;
		}
	}

	if (!is_game) {
		char *tmp = strstr(cfg, "lastcdimg = ");
		if (tmp != NULL) {
			tmp += 12;
			parse_str_val(last_selected_fname, tmp);
		}
	}

	keys_load_all(cfg);
	ret = 0;
fail_read:
	free(cfg);
fail:
	if (f != NULL)
		fclose(f);

	menu_sync_config();

	// sync plugins
	for (i = bios_sel = 0; bioses[i] != NULL; i++)
		if (strcmp(Config.Bios, bioses[i]) == 0)
			{ bios_sel = i; break; }

	for (i = gpu_plugsel = 0; gpu_plugins[i] != NULL; i++)
		if (strcmp(Config.Gpu, gpu_plugins[i]) == 0)
			{ gpu_plugsel = i; break; }

	for (i = spu_plugsel = 0; spu_plugins[i] != NULL; i++)
		if (strcmp(Config.Spu, spu_plugins[i]) == 0)
			{ spu_plugsel = i; break; }

	// memcard selections
	char mcd1_old[sizeof(Config.Mcd1)];
	char mcd2_old[sizeof(Config.Mcd2)];
	strcpy(mcd1_old, Config.Mcd1);
	strcpy(mcd2_old, Config.Mcd2);

	if ((unsigned int)memcard1_sel < ARRAY_SIZE(memcards)) {
		if (memcard1_sel == 0)
			strcpy(Config.Mcd1, "none");
		else if (memcards[memcard1_sel] != NULL)
			snprintf(Config.Mcd1, sizeof(Config.Mcd1), ".%s%s",
				MEMCARD_DIR, memcards[memcard1_sel]);
	}
	if ((unsigned int)memcard2_sel < ARRAY_SIZE(memcards)) {
		if (memcard2_sel == 0)
			strcpy(Config.Mcd2, "none");
		else if (memcards[memcard2_sel] != NULL)
			snprintf(Config.Mcd2, sizeof(Config.Mcd2), ".%s%s",
				MEMCARD_DIR, memcards[memcard2_sel]);
	}
	if (strcmp(mcd1_old, Config.Mcd1) || strcmp(mcd2_old, Config.Mcd2))
		LoadMcds(Config.Mcd1, Config.Mcd2);

	return ret;
}

static const char *filter_exts[] = {
	"bin", "img", "mdf", "iso", "cue", "z",
	#ifdef HAVE_CHD
	"chd",
	#endif
	"bz",  "znx", "pbp", "cbn", NULL
};

// rrrr rggg gggb bbbb
static unsigned short fname2color(const char *fname)
{
	static const char *other_exts[] = {
		"ccd", "toc", "mds", "sub", "table", "index", "sbi"
	};
	const char *ext = strrchr(fname, '.');
	int i;

	if (ext == NULL)
		return 0xffff;
	ext++;
	for (i = 0; filter_exts[i] != NULL; i++)
		if (strcasecmp(ext, filter_exts[i]) == 0)
			return 0x7bff;
	for (i = 0; i < array_size(other_exts); i++)
		if (strcasecmp(ext, other_exts[i]) == 0)
			return 0xa514;
	return 0xffff;
}

static void draw_savestate_bg(int slot);

#define MENU_ALIGN_LEFT
#ifndef HAVE_PRE_ARMV7 // assume hires device
#define MENU_X2 1
#else
#define MENU_X2 0
#endif

#include "libpicofe/menu.c"

// a bit of black magic here
static void draw_savestate_bg(int slot)
{
	static const int psx_widths[8]  = { 256, 368, 320, 384, 512, 512, 640, 640 };
	int x, y, w, h;
	char fname[MAXPATHLEN];
	GPUFreeze_t *gpu;
	u16 *s, *d;
	gzFile f;
	int ret;
	u32 tmp;

	ret = get_state_filename(fname, sizeof(fname), slot);
	if (ret != 0)
		return;

	f = gzopen(fname, "rb");
	if (f == NULL)
		return;

	if ((ret = (int)gzseek(f, 0x29933d, SEEK_SET)) != 0x29933d) {
		fprintf(stderr, "gzseek failed: %d\n", ret);
		gzclose(f);
		return;
	}

	gpu = malloc(sizeof(*gpu));
	if (gpu == NULL) {
		gzclose(f);
		return;
	}

	ret = gzread(f, gpu, sizeof(*gpu));
	gzclose(f);
	if (ret != sizeof(*gpu)) {
		fprintf(stderr, "gzread failed\n");
		goto out;
	}

	memcpy(g_menubg_ptr, g_menubg_src_ptr, g_menuscreen_w * g_menuscreen_h * 2);

	if (gpu->ulStatus & 0x800000)
		goto out; // disabled

	x = gpu->ulControl[5] & 0x3ff;
	y = (gpu->ulControl[5] >> 10) & 0x1ff;
	w = psx_widths[(gpu->ulStatus >> 16) & 7];
	tmp = gpu->ulControl[7];
	h = ((tmp >> 10) & 0x3ff) - (tmp & 0x3ff);
	if (gpu->ulStatus & 0x80000) // doubleheight
		h *= 2;
	if (h <= 0 || h > 512)
		goto out;
	if (y > 512 - 64)
		y = 0;
	if (y + h > 512)
		h = 512 - y;
	s = (u16 *)gpu->psxVRam + y * 1024 + x;

	x = max(0, g_menuscreen_w - w) & ~3;
	y = max(0, g_menuscreen_h / 2 - h / 2);
	w = min(g_menuscreen_w, w);
	h = min(g_menuscreen_h, h);
	d = (u16 *)g_menubg_ptr + g_menuscreen_w * y + x;

	for (; h > 0; h--, d += g_menuscreen_w, s += 1024) {
		if (gpu->ulStatus & 0x200000)
			bgr888_to_rgb565(d, s, w * 3);
		else
			bgr555_to_rgb565(d, s, w * 2);

		// darken this so that menu text is visible
		if (g_menuscreen_w - w < 320)
			menu_darken_bg(d, d, w, 0);
	}

out:
	free(gpu);
}

// -------------- key config --------------

me_bind_action me_ctrl_actions[] =
{
	{ "UP      ", 1 << DKEY_UP},
	{ "DOWN    ", 1 << DKEY_DOWN },
	{ "LEFT    ", 1 << DKEY_LEFT },
	{ "RIGHT   ", 1 << DKEY_RIGHT },
	{ "TRIANGLE", 1 << DKEY_TRIANGLE },
	{ "CIRCLE  ", 1 << DKEY_CIRCLE },
	{ "CROSS   ", 1 << DKEY_CROSS },
	{ "SQUARE  ", 1 << DKEY_SQUARE },
	{ "L1      ", 1 << DKEY_L1 },
	{ "R1      ", 1 << DKEY_R1 },
	{ "L2      ", 1 << DKEY_L2 },
	{ "R2      ", 1 << DKEY_R2 },
	{ "L3      ", 1 << DKEY_L3 },
	{ "R3      ", 1 << DKEY_R3 },
	{ "START   ", 1 << DKEY_START },
	{ "SELECT  ", 1 << DKEY_SELECT },
	{ NULL,       0 }
};

me_bind_action emuctrl_actions[] =
{
	{ "Save State       ", 1 << SACTION_SAVE_STATE },
	{ "Load State       ", 1 << SACTION_LOAD_STATE },
	{ "Prev Save Slot   ", 1 << SACTION_PREV_SSLOT },
	{ "Next Save Slot   ", 1 << SACTION_NEXT_SSLOT },
	{ "Toggle Frameskip ", 1 << SACTION_TOGGLE_FSKIP },
	{ "Take Screenshot  ", 1 << SACTION_SCREENSHOT },
	{ "Show/Hide FPS    ", 1 << SACTION_TOGGLE_FPS },
#ifndef HAVE_PRE_ARMV7
	{ "Switch Renderer  ", 1 << SACTION_SWITCH_DISPMODE },
#endif
	{ "Fast Forward     ", 1 << SACTION_FAST_FORWARD },
#if MENU_SHOW_MINIMIZE
	{ "Minimize         ", 1 << SACTION_MINIMIZE },
#endif
#if MENU_SHOW_FULLSCREEN
	{ "Toggle fullscreen", 1 << SACTION_TOGGLE_FULLSCREEN },
#endif
	{ "Enter Menu       ", 1 << SACTION_ENTER_MENU },
	{ "Gun Trigger      ", 1 << SACTION_GUN_TRIGGER },
	{ "Gun A button     ", 1 << SACTION_GUN_A },
	{ "Gun B button     ", 1 << SACTION_GUN_B },
	{ "Gun Offscreen Trigger", 1 << SACTION_GUN_TRIGGER2 },
#if MENU_SHOW_VOLUME
	{ "Volume Up        ", 1 << SACTION_VOLUME_UP },
	{ "Volume Down      ", 1 << SACTION_VOLUME_DOWN },
#endif
	{ NULL,                0 }
};

static char *mystrip(char *str)
{
	int i, len;

	len = strlen(str);
	for (i = 0; i < len; i++)
		if (str[i] != ' ') break;
	if (i > 0) memmove(str, str + i, len - i + 1);

	len = strlen(str);
	for (i = len - 1; i >= 0; i--)
		if (str[i] != ' ' && str[i] != '\r' && str[i] != '\n') break;
	str[i+1] = 0;

	return str;
}

static void get_line(char *d, size_t size, const char *s)
{
	const char *pe;
	size_t len;

	for (pe = s; *pe != '\r' && *pe != '\n' && *pe != 0; pe++)
		;
	len = pe - s;
	if (len > size - 1)
		len = size - 1;
	strncpy(d, s, len);
	d[len] = 0;
}

static void keys_write_all(FILE *f)
{
	int d;

	for (d = 0; d < IN_MAX_DEVS; d++)
	{
		const int *binds = in_get_dev_binds(d);
		const char *name = in_get_dev_name(d, 0, 0);
		int k, count = 0;

		if (binds == NULL || name == NULL)
			continue;

		fprintf(f, "binddev = %s\n", name);
		in_get_config(d, IN_CFG_BIND_COUNT, &count);

		for (k = 0; k < count; k++)
		{
			int i, kbinds, mask;
			char act[32];

			act[0] = act[31] = 0;
			name = in_get_key_name(d, k);

			kbinds = binds[IN_BIND_OFFS(k, IN_BINDTYPE_PLAYER12)];
			for (i = 0; kbinds && i < ARRAY_SIZE(me_ctrl_actions) - 1; i++) {
				mask = me_ctrl_actions[i].mask;
				if (mask & kbinds) {
					strncpy(act, me_ctrl_actions[i].name, 31);
					fprintf(f, "bind %s = player1 %s\n", name, mystrip(act));
					kbinds &= ~mask;
				}
				mask = me_ctrl_actions[i].mask << 16;
				if (mask & kbinds) {
					strncpy(act, me_ctrl_actions[i].name, 31);
					fprintf(f, "bind %s = player2 %s\n", name, mystrip(act));
					kbinds &= ~mask;
				}
			}

			kbinds = binds[IN_BIND_OFFS(k, IN_BINDTYPE_EMU)];
			for (i = 0; kbinds && emuctrl_actions[i].name != NULL; i++) {
				mask = emuctrl_actions[i].mask;
				if (mask & kbinds) {
					strncpy(act, emuctrl_actions[i].name, 31);
					fprintf(f, "bind %s = %s\n", name, mystrip(act));
					kbinds &= ~mask;
				}
			}
		}

		for (k = 0; k < array_size(in_adev); k++)
		{
			if (in_adev[k] == d)
				fprintf(f, "bind_analog = %d\n", k);
		}
	}
}

static int parse_bind_val(const char *val, int *type)
{
	int i;

	*type = IN_BINDTYPE_NONE;
	if (val[0] == 0)
		return 0;

	if (strncasecmp(val, "player", 6) == 0)
	{
		int player, shift = 0;
		player = atoi(val + 6) - 1;

		if ((unsigned int)player > 1)
			return -1;
		if (player == 1)
			shift = 16;

		*type = IN_BINDTYPE_PLAYER12;
		for (i = 0; me_ctrl_actions[i].name != NULL; i++) {
			if (strncasecmp(me_ctrl_actions[i].name, val + 8, strlen(val + 8)) == 0)
				return me_ctrl_actions[i].mask << shift;
		}
	}
	for (i = 0; emuctrl_actions[i].name != NULL; i++) {
		if (strncasecmp(emuctrl_actions[i].name, val, strlen(val)) == 0) {
			*type = IN_BINDTYPE_EMU;
			return emuctrl_actions[i].mask;
		}
	}

	return -1;
}

static void keys_load_all(const char *cfg)
{
	char dev[256], key[128], *act;
	const char *p;
	int bind, bindtype;
	int ret, dev_id;

	p = cfg;
	while (p != NULL && (p = strstr(p, "binddev = ")) != NULL) {
		p += 10;

		// don't strip 'dev' because there are weird devices
		// with names with space at the end
		get_line(dev, sizeof(dev), p);

		dev_id = in_config_parse_dev(dev);
		if (dev_id < 0) {
			printf("input: can't handle dev: %s\n", dev);
			continue;
		}

		in_unbind_all(dev_id, -1, -1);
		while ((p = strstr(p, "bind"))) {
			if (strncmp(p, "binddev = ", 10) == 0)
				break;

			if (strncmp(p, "bind_analog", 11) == 0) {
				ret = sscanf(p, "bind_analog = %d", &bind);
				p += 11;
				if (ret != 1) {
					printf("input: parse error: %16s..\n", p);
					continue;
				}
				if ((unsigned int)bind >= array_size(in_adev)) {
					printf("input: analog id %d out of range\n", bind);
					continue;
				}
				in_adev[bind] = dev_id;
				continue;
			}

			p += 4;
			if (*p != ' ') {
				printf("input: parse error: %16s..\n", p);
				continue;
			}

			get_line(key, sizeof(key), p);
			act = strchr(key, '=');
			if (act == NULL) {
				printf("parse failed: %16s..\n", p);
				continue;
			}
			*act = 0;
			act++;
			mystrip(key);
			mystrip(act);

			bind = parse_bind_val(act, &bindtype);
			if (bind != -1 && bind != 0) {
				//printf("bind #%d '%s' %08x (%s)\n", dev_id, key, bind, act);
				in_config_bind_key(dev_id, key, bind, bindtype);
			}
			else
				lprintf("config: unhandled action \"%s\"\n", act);
		}
	}
	in_clean_binds();
}

static int key_config_loop_wrap(int id, int keys)
{
	switch (id) {
		case MA_CTRL_PLAYER1:
			key_config_loop(me_ctrl_actions, array_size(me_ctrl_actions) - 1, 0);
			break;
		case MA_CTRL_PLAYER2:
			key_config_loop(me_ctrl_actions, array_size(me_ctrl_actions) - 1, 1);
			break;
		case MA_CTRL_EMU:
			key_config_loop(emuctrl_actions, array_size(emuctrl_actions) - 1, -1);
			break;
		default:
			break;
	}
	return 0;
}

static const char h_nubmode[] = "Maps nub-like analog controls to PSX ones better\n"
				"Might cause problems with real analog sticks";
static const char *adevnames[IN_MAX_DEVS + 2];
static int stick_sel[2];

static menu_entry e_menu_keyconfig_analog[] =
{
	mee_enum   ("Left stick (L3)",  0, stick_sel[0], adevnames),
	mee_range  ("  X axis",    0, in_adev_axis[0][0], 0, 7),
	mee_range  ("  Y axis",    0, in_adev_axis[0][1], 0, 7),
	mee_onoff_h("  nub mode",  0, in_adev_is_nublike[0], 1, h_nubmode),
	mee_enum   ("Right stick (R3)", 0, stick_sel[1], adevnames),
	mee_range  ("  X axis",    0, in_adev_axis[1][0], 0, 7),
	mee_range  ("  Y axis",    0, in_adev_axis[1][1], 0, 7),
	mee_onoff_h("  nub mode",  0, in_adev_is_nublike[1], 1, h_nubmode),
	mee_end,
};

static int key_config_analog(int id, int keys)
{
	int i, d, count, sel = 0;
	int sel2dev_map[IN_MAX_DEVS];

	memset(adevnames, 0, sizeof(adevnames));
	memset(sel2dev_map, 0xff, sizeof(sel2dev_map));
	memset(stick_sel, 0, sizeof(stick_sel));

	adevnames[0] = "None";
	i = 1;
	for (d = 0; d < IN_MAX_DEVS; d++)
	{
		const char *name = in_get_dev_name(d, 0, 1);
		if (name == NULL)
			continue;

		count = 0;
		in_get_config(d, IN_CFG_ABS_AXIS_COUNT, &count);
		if (count == 0)
			continue;

		if (in_adev[0] == d) stick_sel[0] = i;
		if (in_adev[1] == d) stick_sel[1] = i;
		sel2dev_map[i] = d;
		adevnames[i++] = name;
	}
	adevnames[i] = NULL;

	me_loop(e_menu_keyconfig_analog, &sel);

	in_adev[0] = sel2dev_map[stick_sel[0]];
	in_adev[1] = sel2dev_map[stick_sel[1]];

	return 0;
}

static const char *mgn_dev_name(int id, int *offs)
{
	const char *name = NULL;
	static int it = 0;

	if (id == MA_CTRL_DEV_FIRST)
		it = 0;

	for (; it < IN_MAX_DEVS; it++) {
		name = in_get_dev_name(it, 1, 1);
		if (name != NULL)
			break;
	}

	it++;
	return name;
}

static const char *mgn_saveloadcfg(int id, int *offs)
{
	return "";
}

static int mh_savecfg(int id, int keys)
{
	if (menu_write_config(id == MA_OPT_SAVECFG_GAME ? 1 : 0) == 0)
		menu_update_msg("config saved");
	else
		menu_update_msg("failed to write config");

	return 1;
}

static int mh_input_rescan(int id, int keys)
{
	//menu_sync_config();
	in_probe();
	menu_update_msg("rescan complete.");

	return 0;
}

static const char *men_in_type_sel[] = {
	"Standard (SCPH-1080)",
	"Analog (SCPH-1150)",
	"GunCon",
	NULL
};
static const char h_nub_btns[] = "Experimental, keep this OFF if unsure. Select rescan after change.";
static const char h_notsgun[]  = "Don't trigger (shoot) when touching screen in gun games.";
static const char h_vibration[]= "Must select analog above and enable this ingame too.";

static menu_entry e_menu_keyconfig[] =
{
	mee_handler_id("Player 1",              MA_CTRL_PLAYER1,    key_config_loop_wrap),
	mee_handler_id("Player 2",              MA_CTRL_PLAYER2,    key_config_loop_wrap),
	mee_handler_id("Analog controls",       MA_CTRL_ANALOG,     key_config_analog),
	mee_handler_id("Emulator/Gun controls", MA_CTRL_EMU,        key_config_loop_wrap),
	mee_label     (""),
	mee_enum      ("Port 1 device",     0, in_type_sel1,    men_in_type_sel),
	mee_enum      ("Port 2 device",     0, in_type_sel2,    men_in_type_sel),
	mee_onoff_h   ("Nubs as buttons",   MA_CTRL_NUBS_BTNS,  in_evdev_allow_abs_only, 1, h_nub_btns),
	mee_onoff_h   ("Vibration",         MA_CTRL_VIBRATION,  in_enable_vibration, 1, h_vibration),
	mee_range     ("Analog deadzone",   MA_CTRL_DEADZONE,   analog_deadzone, 1, 99),
	mee_onoff_h   ("No TS Gun trigger", 0, g_opts, OPT_TSGUN_NOTRIGGER, h_notsgun),
	mee_cust_nosave("Save global config",       MA_OPT_SAVECFG,      mh_savecfg, mgn_saveloadcfg),
	mee_cust_nosave("Save cfg for loaded game", MA_OPT_SAVECFG_GAME, mh_savecfg, mgn_saveloadcfg),
	mee_handler   ("Rescan devices:",  mh_input_rescan),
	mee_label     (""),
	mee_label_mk  (MA_CTRL_DEV_FIRST, mgn_dev_name),
	mee_label_mk  (MA_CTRL_DEV_NEXT,  mgn_dev_name),
	mee_label_mk  (MA_CTRL_DEV_NEXT,  mgn_dev_name),
	mee_label_mk  (MA_CTRL_DEV_NEXT,  mgn_dev_name),
	mee_label_mk  (MA_CTRL_DEV_NEXT,  mgn_dev_name),
	mee_label_mk  (MA_CTRL_DEV_NEXT,  mgn_dev_name),
	mee_label_mk  (MA_CTRL_DEV_NEXT,  mgn_dev_name),
	mee_end,
};

static int menu_loop_keyconfig(int id, int keys)
{
	static int sel = 0;

//	me_enable(e_menu_keyconfig, MA_OPT_SAVECFG_GAME, ready_to_go && CdromId[0]);
	me_loop(e_menu_keyconfig, &sel);
	return 0;
}

// ------------ gfx options menu ------------

static const char *men_scaler[] = {
	"1x1", "integer scaled 2x", "scaled 4:3", "integer scaled 4:3", "fullscreen", "custom", NULL
};
static const char *men_soft_filter[] = { "None",
#ifdef __ARM_NEON__
	"scale2x", "eagle2x",
#endif
	NULL };
static const char *men_dummy[] = { NULL };
static const char h_scaler[]    = "int. 2x  - scales w. or h. 2x if it fits on screen\n"
				  "int. 4:3 - uses integer if possible, else fractional";
static const char h_cscaler[]   = "Displays the scaler layer, you can resize it\n"
				  "using d-pad or move it using R+d-pad";
static const char h_soft_filter[] = "Works only if game uses low resolution modes";
static const char h_gamma[]     = "Gamma/brightness adjustment (default 100)";
#ifdef __ARM_NEON__
static const char h_scanline_l[]  = "Scanline brightness, 0-100%";
#endif

static int menu_loop_cscaler(int id, int keys)
{
	unsigned int inp;

	g_scaler = SCALE_CUSTOM;

	plat_gvideo_open(Config.PsxType);

	for (;;)
	{
		menu_draw_begin(0, 1);
		memset(g_menuscreen_ptr, 4, g_menuscreen_w * g_menuscreen_h * 2);
		text_out16(2, 2, "%d,%d", g_layer_x, g_layer_y);
		text_out16(2, 480 - 18, "%dx%d | d-pad: resize, R+d-pad: move",	g_layer_w, g_layer_h);
		menu_draw_end();

		inp = in_menu_wait(PBTN_UP|PBTN_DOWN|PBTN_LEFT|PBTN_RIGHT
				|PBTN_R|PBTN_MOK|PBTN_MBACK, NULL, 40);
		if (inp & PBTN_UP)    g_layer_y--;
		if (inp & PBTN_DOWN)  g_layer_y++;
		if (inp & PBTN_LEFT)  g_layer_x--;
		if (inp & PBTN_RIGHT) g_layer_x++;
		if (!(inp & PBTN_R)) {
			if (inp & PBTN_UP)    g_layer_h += 2;
			if (inp & PBTN_DOWN)  g_layer_h -= 2;
			if (inp & PBTN_LEFT)  g_layer_w += 2;
			if (inp & PBTN_RIGHT) g_layer_w -= 2;
		}
		if (inp & (PBTN_MOK|PBTN_MBACK))
			break;

		if (inp & (PBTN_UP|PBTN_DOWN|PBTN_LEFT|PBTN_RIGHT)) {
			if (g_layer_x < 0)   g_layer_x = 0;
			if (g_layer_x > 640) g_layer_x = 640;
			if (g_layer_y < 0)   g_layer_y = 0;
			if (g_layer_y > 420) g_layer_y = 420;
			if (g_layer_w < 160) g_layer_w = 160;
			if (g_layer_h < 60)  g_layer_h = 60;
			if (g_layer_x + g_layer_w > 800)
				g_layer_w = 800 - g_layer_x;
			if (g_layer_y + g_layer_h > 480)
				g_layer_h = 480 - g_layer_y;
			// resize the layer
			plat_gvideo_open(Config.PsxType);
		}
	}

	plat_gvideo_close();

	return 0;
}

static menu_entry e_menu_gfx_options[] =
{
	mee_enum_h    ("Scaler",                   MA_OPT_VARSCALER, g_scaler, men_scaler, h_scaler),
	mee_enum      ("Video output mode",        MA_OPT_VOUT_MODE, plat_target.vout_method, men_dummy),
	mee_onoff     ("Software Scaling",         MA_OPT_SCALER2, soft_scaling, 1),
	mee_enum      ("Hardware Filter",          MA_OPT_HWFILTER, plat_target.hwfilter, men_dummy),
	mee_enum_h    ("Software Filter",          MA_OPT_SWFILTER, soft_filter, men_soft_filter, h_soft_filter),
#ifdef __ARM_NEON__
	mee_onoff     ("Scanlines",                MA_OPT_SCANLINES, scanlines, 1),
	mee_range_h   ("Scanline brightness",      MA_OPT_SCANLINE_LEVEL, scanline_level, 0, 100, h_scanline_l),
#endif
	mee_range_h   ("Gamma adjustment",         MA_OPT_GAMMA, g_gamma, 1, 200, h_gamma),
//	mee_onoff     ("Vsync",                    0, vsync, 1),
	mee_cust_h    ("Setup custom scaler",      MA_OPT_VARSCALER_C, menu_loop_cscaler, NULL, h_cscaler),
	mee_end,
};

static int menu_loop_gfx_options(int id, int keys)
{
	static int sel = 0;

	me_loop(e_menu_gfx_options, &sel);

	return 0;
}

// ------------ bios/plugins ------------

#ifdef BUILTIN_GPU_NEON

static const char h_gpu_neon[] =
	"Configure built-in NEON GPU plugin";
static const char h_gpu_neon_enhanced[] =
	"Renders in double resolution at the cost of lower performance\n"
	"(not available for high resolution games)";
static const char h_gpu_neon_enhanced_hack[] =
	"Speed hack for above option (glitches some games)";
static const char *men_gpu_interlace[] = { "Off", "On", "Auto", NULL };

static menu_entry e_menu_plugin_gpu_neon[] =
{
	mee_enum      ("Enable interlace mode",      0, pl_rearmed_cbs.gpu_neon.allow_interlace, men_gpu_interlace),
	mee_onoff_h   ("Enhanced resolution (slow)", 0, pl_rearmed_cbs.gpu_neon.enhancement_enable, 1, h_gpu_neon_enhanced),
	mee_onoff_h   ("Enhanced res. speed hack",   0, pl_rearmed_cbs.gpu_neon.enhancement_no_main, 1, h_gpu_neon_enhanced_hack),
	mee_end,
};

static int menu_loop_plugin_gpu_neon(int id, int keys)
{
	static int sel = 0;
	me_loop(e_menu_plugin_gpu_neon, &sel);
	return 0;
}

#endif

static menu_entry e_menu_plugin_gpu_unai[] =
{
	mee_onoff     ("Skip every 2nd line",        0, pl_rearmed_cbs.gpu_unai.lineskip, 1),
	mee_onoff     ("Abe's Odyssey hack",         0, pl_rearmed_cbs.gpu_unai.abe_hack, 1),
	mee_onoff     ("Disable lighting",           0, pl_rearmed_cbs.gpu_unai.no_light, 1),
	mee_onoff     ("Disable blending",           0, pl_rearmed_cbs.gpu_unai.no_blend, 1),
	mee_end,
};

static int menu_loop_plugin_gpu_unai(int id, int keys)
{
	int sel = 0;
	me_loop(e_menu_plugin_gpu_unai, &sel);
	return 0;
}

static menu_entry e_menu_plugin_gpu_senquack[] =
{
	mee_onoff     ("Interlace",                  0, pl_rearmed_cbs.gpu_senquack.ilace_force, 1),
	mee_onoff     ("Dithering",                  0, pl_rearmed_cbs.gpu_senquack.dithering, 1),
	mee_onoff     ("Lighting",                   0, pl_rearmed_cbs.gpu_senquack.lighting, 1),
	mee_onoff     ("Fast lighting",              0, pl_rearmed_cbs.gpu_senquack.fast_lighting, 1),
	mee_onoff     ("Blending",                   0, pl_rearmed_cbs.gpu_senquack.blending, 1),
	mee_onoff     ("Pixel skip",                 0, pl_rearmed_cbs.gpu_senquack.pixel_skip, 1),
	mee_end,
};

static int menu_loop_plugin_gpu_senquack(int id, int keys)
{
	int sel = 0;
	me_loop(e_menu_plugin_gpu_senquack, &sel);
	return 0;
}


static const char *men_gpu_dithering[] = { "None", "Game dependant", "Always", NULL };
//static const char h_gpu_0[]            = "Needed for Chrono Cross";
static const char h_gpu_1[]            = "Capcom fighting games";
static const char h_gpu_2[]            = "Black screens in Lunar";
static const char h_gpu_3[]            = "Compatibility mode";
static const char h_gpu_6[]            = "Pandemonium 2";
//static const char h_gpu_7[]            = "Skip every second frame";
static const char h_gpu_8[]            = "Needed by Dark Forces";
static const char h_gpu_9[]            = "better g-colors, worse textures";
static const char h_gpu_10[]           = "Toggle busy flags after drawing";

static menu_entry e_menu_plugin_gpu_peops[] =
{
	mee_enum      ("Dithering",                  0, pl_rearmed_cbs.gpu_peops.iUseDither, men_gpu_dithering),
//	mee_onoff_h   ("Odd/even bit hack",          0, pl_rearmed_cbs.gpu_peops.dwActFixes, 1<<0, h_gpu_0),
	mee_onoff_h   ("Expand screen width",        0, pl_rearmed_cbs.gpu_peops.dwActFixes, 1<<1, h_gpu_1),
	mee_onoff_h   ("Ignore brightness color",    0, pl_rearmed_cbs.gpu_peops.dwActFixes, 1<<2, h_gpu_2),
	mee_onoff_h   ("Disable coordinate check",   0, pl_rearmed_cbs.gpu_peops.dwActFixes, 1<<3, h_gpu_3),
	mee_onoff_h   ("Lazy screen update",         0, pl_rearmed_cbs.gpu_peops.dwActFixes, 1<<6, h_gpu_6),
//	mee_onoff_h   ("Old frame skipping",         0, pl_rearmed_cbs.gpu_peops.dwActFixes, 1<<7, h_gpu_7),
	mee_onoff_h   ("Repeated flat tex triangles ",0,pl_rearmed_cbs.gpu_peops.dwActFixes, 1<<8, h_gpu_8),
	mee_onoff_h   ("Draw quads with triangles",  0, pl_rearmed_cbs.gpu_peops.dwActFixes, 1<<9, h_gpu_9),
	mee_onoff_h   ("Fake 'gpu busy' states",     0, pl_rearmed_cbs.gpu_peops.dwActFixes, 1<<10, h_gpu_10),
	mee_end,
};

static int menu_loop_plugin_gpu_peops(int id, int keys)
{
	static int sel = 0;
	me_loop(e_menu_plugin_gpu_peops, &sel);
	return 0;
}

static const char *men_peopsgl_texfilter[] = { "None", "Standard", "Extended",
	"Standard-sprites", "Extended-sprites", "Standard+sprites", "Extended+sprites", NULL };
static const char *men_peopsgl_fbtex[] = { "Emulated VRam", "Black", "Card", "Card+soft" };

static menu_entry e_menu_plugin_gpu_peopsgl[] =
{
	mee_onoff     ("Dithering",                  0, pl_rearmed_cbs.gpu_peopsgl.bDrawDither, 1),
	mee_enum      ("Texture Filtering",          0, pl_rearmed_cbs.gpu_peopsgl.iFilterType, men_peopsgl_texfilter),
	mee_enum      ("Framebuffer Textures",       0, pl_rearmed_cbs.gpu_peopsgl.iFrameTexType, men_peopsgl_fbtex),
	mee_onoff     ("Mask Detect",                0, pl_rearmed_cbs.gpu_peopsgl.iUseMask, 1),
	mee_onoff     ("Opaque Pass",                0, pl_rearmed_cbs.gpu_peopsgl.bOpaquePass, 1),
	mee_onoff     ("Advanced Blend",             0, pl_rearmed_cbs.gpu_peopsgl.bAdvancedBlend, 1),
	mee_onoff     ("Use Fast Mdec",              0, pl_rearmed_cbs.gpu_peopsgl.bUseFastMdec, 1),
	mee_range     ("Texture RAM size (MB)",      0, pl_rearmed_cbs.gpu_peopsgl.iVRamSize, 4, 128),
	mee_onoff     ("Texture garbage collection", 0, pl_rearmed_cbs.gpu_peopsgl.iTexGarbageCollection, 1),
	mee_label     ("Fixes/hacks:"),
	mee_onoff     ("FF7 cursor",                 0, pl_rearmed_cbs.gpu_peopsgl.dwActFixes, 1<<0),
	mee_onoff     ("Direct FB updates",          0, pl_rearmed_cbs.gpu_peopsgl.dwActFixes, 1<<1),
	mee_onoff     ("Black brightness",           0, pl_rearmed_cbs.gpu_peopsgl.dwActFixes, 1<<2),
	mee_onoff     ("Swap front detection",       0, pl_rearmed_cbs.gpu_peopsgl.dwActFixes, 1<<3),
	mee_onoff     ("Disable coord check",        0, pl_rearmed_cbs.gpu_peopsgl.dwActFixes, 1<<4),
	mee_onoff     ("No blue glitches (LoD)",     0, pl_rearmed_cbs.gpu_peopsgl.dwActFixes, 1<<5),
	mee_onoff     ("Soft FB access",             0, pl_rearmed_cbs.gpu_peopsgl.dwActFixes, 1<<6),
	mee_onoff     ("FF9 rect",                   0, pl_rearmed_cbs.gpu_peopsgl.dwActFixes, 1<<9),
	mee_onoff     ("No subtr. blending",         0, pl_rearmed_cbs.gpu_peopsgl.dwActFixes, 1<<10),
	mee_onoff     ("Lazy upload (DW7)",          0, pl_rearmed_cbs.gpu_peopsgl.dwActFixes, 1<<11),
	mee_onoff     ("Additional uploads",         0, pl_rearmed_cbs.gpu_peopsgl.dwActFixes, 1<<15),
	mee_end,
};

static int menu_loop_plugin_gpu_peopsgl(int id, int keys)
{
	static int sel = 0;
	me_loop(e_menu_plugin_gpu_peopsgl, &sel);
	return 0;
}

static const char *men_spu_interp[] = { "None", "Simple", "Gaussian", "Cubic", NULL };
static const char h_spu_volboost[]  = "Large values cause distortion";
static const char h_spu_tempo[]     = "Slows down audio if emu is too slow\n"
				      "This is inaccurate and breaks games";

static menu_entry e_menu_plugin_spu[] =
{
	mee_range_h   ("Volume boost",              0, volume_boost, -5, 30, h_spu_volboost),
	mee_onoff     ("Reverb",                    0, spu_config.iUseReverb, 1),
	mee_enum      ("Interpolation",             0, spu_config.iUseInterpolation, men_spu_interp),
	//mee_onoff     ("Adjust XA pitch",           0, spu_config.iXAPitch, 1),
	mee_onoff_h   ("Adjust tempo",              0, spu_config.iTempo, 1, h_spu_tempo),
	mee_end,
};

static int menu_loop_plugin_spu(int id, int keys)
{
	static int sel = 0;
	me_loop(e_menu_plugin_spu, &sel);
	return 0;
}

static const char h_bios[]       = "HLE is simulated BIOS. BIOS selection is saved in\n"
				   "savestates and can't be changed there. Must save\n"
				   "config and reload the game for change to take effect";
static const char h_plugin_gpu[] = 
#ifdef BUILTIN_GPU_NEON
				   "builtin_gpu is the NEON GPU, very fast and accurate\n"
#endif
				   "gpu_peops is Pete's soft GPU, slow but accurate\n"
				   "gpu_unai is GPU from PCSX4ALL, fast but glitchy\n"
				   "gpu_senquack is more accurate but slower\n"
				   "gpu_gles Pete's hw GPU, uses 3D chip but is glitchy\n"
				   "must save config and reload the game if changed";
static const char h_plugin_spu[] = "spunull effectively disables sound\n"
				   "must save config and reload the game if changed";
static const char h_gpu_peops[]  = "Configure P.E.Op.S. SoftGL Driver V1.17";
static const char h_gpu_peopsgl[]= "Configure P.E.Op.S. MesaGL Driver V1.78";
static const char h_gpu_unai[]   = "Configure Unai/PCSX4ALL Team GPU plugin";
static const char h_gpu_senquack[]   = "Configure Unai/PCSX4ALL Senquack plugin";
static const char h_spu[]        = "Configure built-in P.E.Op.S. Sound Driver V1.7";

static menu_entry e_menu_plugin_options[] =
{
	mee_enum_h    ("BIOS",                          0, bios_sel, bioses, h_bios),
	mee_enum_h    ("GPU plugin",                    0, gpu_plugsel, gpu_plugins, h_plugin_gpu),
	mee_enum_h    ("SPU plugin",                    0, spu_plugsel, spu_plugins, h_plugin_spu),
#ifdef BUILTIN_GPU_NEON
	mee_handler_h ("Configure built-in GPU plugin", menu_loop_plugin_gpu_neon, h_gpu_neon),
#endif
	mee_handler_h ("Configure gpu_peops plugin",    menu_loop_plugin_gpu_peops, h_gpu_peops),
	mee_handler_h ("Configure gpu_unai GPU plugin", menu_loop_plugin_gpu_unai, h_gpu_unai),
	mee_handler_h ("Configure gpu_senquack GPU plugin", menu_loop_plugin_gpu_senquack, h_gpu_senquack),
	mee_handler_h ("Configure gpu_gles GPU plugin", menu_loop_plugin_gpu_peopsgl, h_gpu_peopsgl),
	mee_handler_h ("Configure built-in SPU plugin", menu_loop_plugin_spu, h_spu),
	mee_end,
};

static menu_entry e_menu_main2[];

static int menu_loop_plugin_options(int id, int keys)
{
	static int sel = 0;
	me_loop(e_menu_plugin_options, &sel);

	// sync BIOS/plugins
	snprintf(Config.Bios, sizeof(Config.Bios), "%s", bioses[bios_sel]);
	snprintf(Config.Gpu, sizeof(Config.Gpu), "%s", gpu_plugins[gpu_plugsel]);
	snprintf(Config.Spu, sizeof(Config.Spu), "%s", spu_plugins[spu_plugsel]);
	me_enable(e_menu_main2, MA_MAIN_RUN_BIOS, bios_sel != 0);

	return 0;
}

// ------------ adv options menu ------------

#ifndef DRC_DISABLE
static const char h_cfg_noch[]    = "Disables game-specific compatibility hacks";
static const char h_cfg_nosmc[]   = "Will cause crashes when loading, break memcards";
static const char h_cfg_gteunn[]  = "May cause graphical glitches";
static const char h_cfg_gteflgs[] = "Will cause graphical glitches";
#endif
static const char h_cfg_stalls[]  = "Will cause some games to run too fast";

static menu_entry e_menu_speed_hacks[] =
{
#ifndef DRC_DISABLE
	mee_onoff_h   ("Disable compat hacks",     0, new_dynarec_hacks, NDHACK_NO_COMPAT_HACKS, h_cfg_noch),
	mee_onoff_h   ("Disable SMC checks",       0, new_dynarec_hacks, NDHACK_NO_SMC_CHECK, h_cfg_nosmc),
	mee_onoff_h   ("Assume GTE regs unneeded", 0, new_dynarec_hacks, NDHACK_GTE_UNNEEDED, h_cfg_gteunn),
	mee_onoff_h   ("Disable GTE flags",        0, new_dynarec_hacks, NDHACK_GTE_NO_FLAGS, h_cfg_gteflgs),
#endif
	mee_onoff_h   ("Disable CPU/GTE stalls",   0, menu_iopts[0], 1, h_cfg_stalls),
	mee_end,
};

static int menu_loop_speed_hacks(int id, int keys)
{
	static int sel = 0;
	menu_iopts[0] = Config.DisableStalls;
	me_loop(e_menu_speed_hacks, &sel);
	Config.DisableStalls = menu_iopts[0];
	return 0;
}

static const char *men_gpul[]    = { "Auto", "Off", "On", NULL };

static const char h_cfg_cpul[]   = "Shows CPU usage in %";
static const char h_cfg_spu[]    = "Shows active SPU channels\n"
				   "(green: normal, red: fmod, blue: noise)";
static const char h_cfg_fl[]     = "Frame Limiter keeps the game from running too fast";
static const char h_cfg_xa[]     = "Disables XA sound, which can sometimes improve performance";
static const char h_cfg_cdda[]   = "Disable CD Audio for a performance boost\n"
				   "(proper .cue/.bin dump is needed otherwise)";
#ifndef DRC_DISABLE
static const char h_cfg_nodrc[]  = "Disable dynamic recompiler and use interpreter\n"
				   "Might be useful to overcome some dynarec bugs";
#endif
static const char h_cfg_shacks[] = "Breaks games but may give better performance";
static const char h_cfg_icache[] = "Support F1 games (only when dynarec is off)";
static const char h_cfg_gpul[]   = "Try enabling this if the game is missing some graphics\n"
				   "causes a performance hit";
static const char h_cfg_psxclk[]  = "Over/under-clock the PSX, default is " DEFAULT_PSX_CLOCK_S "\n"
				    "(adjust this if the game is too slow/too fast/hangs)";

enum { AMO_XA, AMO_CDDA, AMO_IC, AMO_CPU, AMO_GPUL };

static menu_entry e_menu_adv_options[] =
{
	mee_onoff_h   ("Show CPU load",          0, g_opts, OPT_SHOWCPU, h_cfg_cpul),
	mee_onoff_h   ("Show SPU channels",      0, g_opts, OPT_SHOWSPU, h_cfg_spu),
	mee_onoff_h   ("Disable Frame Limiter",  0, g_opts, OPT_NO_FRAMELIM, h_cfg_fl),
	mee_onoff_h   ("Disable XA Decoding",    0, menu_iopts[AMO_XA],   1, h_cfg_xa),
	mee_onoff_h   ("Disable CD Audio",       0, menu_iopts[AMO_CDDA], 1, h_cfg_cdda),
	mee_onoff_h   ("ICache emulation",       0, menu_iopts[AMO_IC],   1, h_cfg_icache),
	mee_enum_h    ("GPU l-list slow walking",0, menu_iopts[AMO_GPUL], men_gpul, h_cfg_gpul),
#if !defined(DRC_DISABLE) || defined(LIGHTREC)
	mee_onoff_h   ("Disable dynarec (slow!)",0, menu_iopts[AMO_CPU],  1, h_cfg_nodrc),
#endif
	mee_range_h   ("PSX CPU clock, %",       0, psx_clock, 1, 500, h_cfg_psxclk),
	mee_handler_h ("[Speed hacks]",             menu_loop_speed_hacks, h_cfg_shacks),
	mee_end,
};

static int menu_loop_adv_options(int id, int keys)
{
	static int sel = 0;
	static struct {
		boolean *opt;
		int *mopt;
	} opts[] = {
		{ &Config.Xa,      &menu_iopts[AMO_XA] },
		{ &Config.Cdda,    &menu_iopts[AMO_CDDA] },
		{ &Config.icache_emulation, &menu_iopts[AMO_IC] },
		{ &Config.Cpu,     &menu_iopts[AMO_CPU] },
	};
	int i;
	for (i = 0; i < ARRAY_SIZE(opts); i++)
		*opts[i].mopt = *opts[i].opt;
	menu_iopts[AMO_GPUL] = Config.GpuListWalking + 1;

	me_loop(e_menu_adv_options, &sel);

	for (i = 0; i < ARRAY_SIZE(opts); i++)
		*opts[i].opt = *opts[i].mopt;
	Config.GpuListWalking = menu_iopts[AMO_GPUL] - 1;

	return 0;
}

// ------------ options menu ------------

static int mh_restore_defaults(int id, int keys)
{
	menu_set_defconfig();
	menu_update_msg("defaults restored");
	return 1;
}

static const char *men_region[]       = { "Auto", "NTSC", "PAL", NULL };
static const char *men_frameskip[]    = { "Auto", "Off", "1", "2", "3", NULL };
/*
static const char *men_confirm_save[] = { "OFF", "writes", "loads", "both", NULL };
static const char h_confirm_save[]    = "Ask for confirmation when overwriting save,\n"
					"loading state or both";
*/
static const char h_restore_def[]     = "Switches back to default / recommended\n"
					"configuration";
static const char h_frameskip[]       = "Warning: frameskip sometimes causes glitches\n";

static menu_entry e_menu_options[] =
{
//	mee_range     ("Save slot",                0, state_slot, 0, 9),
//	mee_enum_h    ("Confirm savestate",        0, dummy, men_confirm_save, h_confirm_save),
	mee_enum_h    ("Frameskip",                0, frameskip, men_frameskip, h_frameskip),
	mee_onoff     ("Show FPS",                 0, g_opts, OPT_SHOWFPS),
	mee_enum      ("Region",                   0, region, men_region),
	mee_range     ("CPU clock",                MA_OPT_CPU_CLOCKS, cpu_clock, 20, 5000),
#ifdef C64X_DSP
	mee_onoff     ("Use C64x DSP for sound",   MA_OPT_SPU_THREAD, spu_config.iUseThread, 1),
#else
	mee_onoff     ("Threaded SPU",             MA_OPT_SPU_THREAD, spu_config.iUseThread, 1),
#endif
	mee_handler_id("[Display]",                MA_OPT_DISP_OPTS, menu_loop_gfx_options),
	mee_handler   ("[BIOS/Plugins]",           menu_loop_plugin_options),
	mee_handler   ("[Advanced]",               menu_loop_adv_options),
	mee_cust_nosave("Save global config",      MA_OPT_SAVECFG,      mh_savecfg, mgn_saveloadcfg),
	mee_cust_nosave("Save cfg for loaded game",MA_OPT_SAVECFG_GAME, mh_savecfg, mgn_saveloadcfg),
	mee_handler_h ("Restore default config",   mh_restore_defaults, h_restore_def),
	mee_end,
};

static int menu_loop_options(int id, int keys)
{
	static int sel = 0;

	me_enable(e_menu_options, MA_OPT_CPU_CLOCKS, cpu_clock_st > 0);
	me_enable(e_menu_options, MA_OPT_SPU_THREAD, spu_config.iThreadAvail);
	me_enable(e_menu_options, MA_OPT_SAVECFG_GAME, ready_to_go && CdromId[0]);

	me_loop(e_menu_options, &sel);

	return 0;
}

// ------------ debug menu ------------

static void draw_frame_debug(GPUFreeze_t *gpuf, int x, int y)
{
	int w = min(g_menuscreen_w, 1024);
	int h = min(g_menuscreen_h, 512);
	u16 *d = g_menuscreen_ptr;
	u16 *s = (u16 *)gpuf->psxVRam + y * 1024 + x;
	char buff[64];
	int ty = 1;

	gpuf->ulFreezeVersion = 1;
	if (GPU_freeze != NULL)
		GPU_freeze(1, gpuf);

	for (; h > 0; h--, d += g_menuscreen_w, s += 1024)
		bgr555_to_rgb565(d, s, w * 2);

	//smalltext_out16(4, 1, "build: "__DATE__ " " __TIME__ " " REV, 0xe7fc);
	snprintf(buff, sizeof(buff), "GPU sr: %08x", gpuf->ulStatus);
	smalltext_out16(4, (ty += me_sfont_h), buff, 0xe7fc);
	snprintf(buff, sizeof(buff), "PC/SP: %08x %08x", psxRegs.pc, psxRegs.GPR.n.sp);
	smalltext_out16(4, (ty += me_sfont_h), buff, 0xe7fc);
}

static void debug_menu_loop(void)
{
	int inp, df_x = 0, df_y = 0;
	GPUFreeze_t *gpuf;

	gpuf = malloc(sizeof(*gpuf));
	if (gpuf == NULL)
		return;

	while (1)
	{
		menu_draw_begin(0, 1);
		draw_frame_debug(gpuf, df_x, df_y);
		menu_draw_end();

		inp = in_menu_wait(PBTN_MOK|PBTN_MBACK|PBTN_MA2|PBTN_MA3|PBTN_L|PBTN_R |
					PBTN_UP|PBTN_DOWN|PBTN_LEFT|PBTN_RIGHT, NULL, 10);
		if      (inp & PBTN_MBACK) break;
		else if (inp & PBTN_UP)    { if (df_y > 0) df_y--; }
		else if (inp & PBTN_DOWN)  { if (df_y < 512 - g_menuscreen_h) df_y++; }
		else if (inp & PBTN_LEFT)  { if (df_x > 0) df_x -= 2; }
		else if (inp & PBTN_RIGHT) { if (df_x < 1024 - g_menuscreen_w) df_x += 2; }
	}

	free(gpuf);
}

// --------- memcard manager ---------

static void draw_mc_icon(int dx, int dy, const u16 *s)
{
	u16 *d;
	int x, y, l, p;

	d = (u16 *)g_menuscreen_ptr + g_menuscreen_w * dy + dx;

	for (y = 0; y < 16; y++, s += 16) {
		for (l = 0; l < 2; l++, d += g_menuscreen_w) {
			for (x = 0; x < 16; x++) {
				p = s[x];
				d[x*2] = d[x*2 + 1] = ((p & 0x7c00) >> 10)
					| ((p & 0x03e0) << 1) | ((p & 0x1f) << 11);
			}
		}
	}
}

static void draw_mc_bg(void)
{
	McdBlock *blocks1, *blocks2;
	int maxicons = 15;
	int i, y, row2;

	blocks1 = malloc(15 * sizeof(blocks1[0]));
	blocks2 = malloc(15 * sizeof(blocks1[0]));
	if (blocks1 == NULL || blocks2 == NULL)
		goto out;

	for (i = 0; i < 15; i++) {
		GetMcdBlockInfo(1, i + 1, &blocks1[i]);
		GetMcdBlockInfo(2, i + 1, &blocks2[i]);
	}

	menu_draw_begin(1, 1);

	memcpy(g_menuscreen_ptr, g_menubg_src_ptr, g_menuscreen_w * g_menuscreen_h * 2);

	y = g_menuscreen_h / 2 - 15 * 32 / 2;
	if (y < 0) {
		// doesn't fit..
		y = 0;
		maxicons = g_menuscreen_h / 32;
	}

	row2 = g_menuscreen_w / 2;
	for (i = 0; i < maxicons; i++) {
		draw_mc_icon(8, y + i * 32, (u16 *)blocks1[i].Icon);
		smalltext_out16(10+32, y + i * 32 + 8, blocks1[i].sTitle, 0xf71e);

		draw_mc_icon(row2 + 8, y + i * 32, (u16 *)blocks2[i].Icon);
		smalltext_out16(row2 + 10+32, y + i * 32 + 8, blocks2[i].sTitle, 0xf71e);
	}

	menu_darken_bg(g_menubg_ptr, g_menuscreen_ptr, g_menuscreen_w * g_menuscreen_h, 0);

	menu_draw_end();
out:
	free(blocks1);
	free(blocks2);
}

static void handle_memcard_sel(void)
{
	strcpy(Config.Mcd1, "none");
	if (memcard1_sel != 0)
		snprintf(Config.Mcd1, sizeof(Config.Mcd1), ".%s%s", MEMCARD_DIR, memcards[memcard1_sel]);
	strcpy(Config.Mcd2, "none");
	if (memcard2_sel != 0)
		snprintf(Config.Mcd2, sizeof(Config.Mcd2), ".%s%s", MEMCARD_DIR, memcards[memcard2_sel]);
	LoadMcds(Config.Mcd1, Config.Mcd2);
	draw_mc_bg();
}

static menu_entry e_memcard_options[] =
{
	mee_enum("Memory card 1", 0, memcard1_sel, memcards),
	mee_enum("Memory card 2", 0, memcard2_sel, memcards),
	mee_end,
};

static int menu_loop_memcards(int id, int keys)
{
	static int sel = 0;
	char *p;
	int i;

	memcard1_sel = memcard2_sel = 0;
	p = strrchr(Config.Mcd1, '/');
	if (p != NULL)
		for (i = 0; memcards[i] != NULL; i++)
			if (strcmp(p + 1, memcards[i]) == 0)
				{ memcard1_sel = i; break; }
	p = strrchr(Config.Mcd2, '/');
	if (p != NULL)
		for (i = 0; memcards[i] != NULL; i++)
			if (strcmp(p + 1, memcards[i]) == 0)
				{ memcard2_sel = i; break; }

	me_loop_d(e_memcard_options, &sel, handle_memcard_sel, NULL);

	memcpy(g_menubg_ptr, g_menubg_src_ptr, g_menuscreen_w * g_menuscreen_h * 2);

	return 0;
}

// ------------ cheats menu ------------

static void draw_cheatlist(int sel)
{
	int max_cnt, start, i, pos, active;

	max_cnt = g_menuscreen_h / me_sfont_h;
	start = max_cnt / 2 - sel;

	menu_draw_begin(1, 1);

	for (i = 0; i < NumCheats; i++) {
		pos = start + i;
		if (pos < 0) continue;
		if (pos >= max_cnt) break;
		active = Cheats[i].Enabled;
		smalltext_out16(14,                pos * me_sfont_h,
			active ? "ON " : "OFF", active ? 0xfff6 : 0xffff);
		smalltext_out16(14 + me_sfont_w*4, pos * me_sfont_h,
			Cheats[i].Descr, active ? 0xfff6 : 0xffff);
	}
	pos = start + i;
	if (pos < max_cnt)
		smalltext_out16(14, pos * me_sfont_h, "done", 0xffff);

	text_out16(5, max_cnt / 2 * me_sfont_h, ">");
	menu_draw_end();
}

static void menu_loop_cheats(void)
{
	static int menu_sel = 0;
	int inp;

	for (;;)
	{
		draw_cheatlist(menu_sel);
		inp = in_menu_wait(PBTN_UP|PBTN_DOWN|PBTN_LEFT|PBTN_RIGHT|PBTN_L|PBTN_R
				|PBTN_MOK|PBTN_MBACK, NULL, 33);
		if (inp & PBTN_UP  ) { menu_sel--; if (menu_sel < 0) menu_sel = NumCheats; }
		if (inp & PBTN_DOWN) { menu_sel++; if (menu_sel > NumCheats) menu_sel = 0; }
		if (inp &(PBTN_LEFT|PBTN_L))  { menu_sel-=10; if (menu_sel < 0) menu_sel = 0; }
		if (inp &(PBTN_RIGHT|PBTN_R)) { menu_sel+=10; if (menu_sel > NumCheats) menu_sel = NumCheats; }
		if (inp & PBTN_MOK) { // action
			if (menu_sel < NumCheats)
				Cheats[menu_sel].Enabled = !Cheats[menu_sel].Enabled;
			else 	break;
		}
		if (inp & PBTN_MBACK)
			break;
	}
}

// --------- main menu help ----------

static void menu_bios_warn(void)
{
	int inp;
	static const char msg[] =
		"You don't seem to have copied any BIOS\n"
		"files to\n"
		MENU_BIOS_PATH "\n\n"

		"While many games work fine with fake\n"
		"(HLE) BIOS, others (like MGS and FF8)\n"
		"require BIOS to work.\n"
		"After copying the file, you'll also need\n"
		"to select it in the emu's menu:\n"
		"options->[BIOS/Plugins]\n\n"
		"The file is usually named SCPH1001.BIN,\n"
		"but other not compressed files can be\n"
		"used too.\n\n"
		"Press %s or %s to continue";
	char tmp_msg[sizeof(msg) + 64];

	snprintf(tmp_msg, sizeof(tmp_msg), msg,
		in_get_key_name(-1, -PBTN_MOK), in_get_key_name(-1, -PBTN_MBACK));
	/*while (1)
	{
		draw_menu_message(tmp_msg, NULL);

		inp = in_menu_wait(PBTN_MOK|PBTN_MBACK, NULL, 70);
		if (inp & (PBTN_MBACK|PBTN_MOK))
			return;
	}*/
    printf("%s\n", tmp_msg);


    /** Set notif for BIOS */
    char shell_cmd[400];
    sprintf(shell_cmd, "%s 0 \"     BIOS FILE MISSING^^While many games work fine ^with fake BIOS, others (like ^MGS and FF8) require BIOS to^work. Copy the BIOS^file in FunKey/.pcsx/bios/^^BIOS file is called:^ SCPH1001.BIN^File size is always 512KB^^For more instructions:^www.funkey-project.com^^Press any button to continue^^\"",
            SHELL_CMD_NOTIF_SET);
    system(shell_cmd);

    /// ------ Wait for key press ------
    SDL_Event event;
    while(event.type != SDL_KEYUP && event.type != SDL_QUIT){
        while (SDL_PollEvent(&event))
		SDL_Delay(60);
    }

    /** Clear notif for BIOS */
    system(SHELL_CMD_NOTIF_CLEAR);
}

// ------------ main menu ------------

static menu_entry e_menu_main[];

static void draw_frame_main(void)
{
	struct tm *tmp;
	time_t ltime;
	int capacity;
	char ltime_s[16];
	char buff[64];
	char *out;

	if (CdromId[0] != 0) {
		snprintf(buff, sizeof(buff), "%.32s/%.9s (running as %s, with %s)",
			 get_cd_label(), CdromId, Config.PsxType ? "PAL" : "NTSC",
			 Config.HLE ? "HLE" : "BIOS");
		smalltext_out16(4, 1, buff, 0x105f);
	}

	if (ready_to_go) {
		capacity = plat_target_bat_capacity_get();
		ltime = time(NULL);
		tmp = localtime(&ltime);
		strftime(ltime_s, sizeof(ltime_s), "%H:%M", tmp);
		if (capacity >= 0) {
			snprintf(buff, sizeof(buff), "%s %3d%%", ltime_s, capacity);
			out = buff;
		}
		else
			out = ltime_s;
		smalltext_out16(4, 1 + me_sfont_h, out, 0x105f);
	}
}

static void draw_frame_credits(void)
{
	//smalltext_out16(4, 1, "build: " __DATE__ " " __TIME__ " " REV, 0xe7fc);
}

static const char credits_text[] =
	"PCSX-ReARMed\n\n"
	"(C) 1999-2003 PCSX Team\n"
	"(C) 2005-2009 PCSX-df Team\n"
	"(C) 2009-2011 PCSX-Reloaded Team\n\n"
	"ARM recompiler (C) 2009-2011 Ari64\n"
#ifdef BUILTIN_GPU_NEON
	"ARM NEON GPU (c) 2011-2012 Exophase\n"
#endif
	"PEOpS GPU and SPU by Pete Bernert\n"
	"  and the P.E.Op.S. team\n"
	"PCSX4ALL plugin by PCSX4ALL team\n"
	"  Chui, Franxis, Unai\n\n"
	"integration, optimization and\n"
	"  frontend (C) 2010-2015 notaz\n";

static int reset_game(void)
{
	// sanity check
	if (bios_sel == 0 && !Config.HLE)
		return -1;

	ClosePlugins();
	OpenPlugins();
	SysReset();
	if (CheckCdrom() != -1) {
		LoadCdrom();
	}
	return 0;
}

static int reload_plugins(const char *cdimg)
{
	pl_vout_buf = NULL;

	ClosePlugins();

	set_cd_image(cdimg);
	LoadPlugins();
	pcnt_hook_plugins();
	NetOpened = 0;
	if (OpenPlugins() == -1) {
		menu_update_msg("failed to open plugins");
		return -1;
	}
	plugin_call_rearmed_cbs();

	cdrIsoMultidiskCount = 1;
	CdromId[0] = '\0';
	CdromLabel[0] = '\0';

	return 0;
}

static int run_bios(void)
{
	if (bios_sel == 0)
		return -1;

	ready_to_go = 0;
	if (reload_plugins(NULL) != 0)
		return -1;
	SysReset();

	ready_to_go = 1;
	return 0;
}

static int run_exe(void)
{
	const char *exts[] = { "exe", NULL };
	const char *fname;

	fname = menu_loop_romsel(last_selected_fname,
		sizeof(last_selected_fname), exts, NULL);
	if (fname == NULL)
		return -1;

	ready_to_go = 0;
	if (reload_plugins(NULL) != 0)
		return -1;

	SysReset();
	if (Load(fname) != 0) {
		menu_update_msg("exe load failed, bad file?");
		printf("meh\n");
		return -1;
	}

	ready_to_go = 1;
	return 0;
}

static int run_cd_image(const char *fname)
{
	int autoload_state = g_autostateld_opt;

	ready_to_go = 0;
	reload_plugins(fname);

	// always autodetect, menu_sync_config will override as needed
	Config.PsxAuto = 1;

	if (CheckCdrom() == -1) {
		// Only check the CD if we are starting the console with a CD
		ClosePlugins();
		menu_update_msg("unsupported/invalid CD image");
		return -1;
	}

	SysReset();

	// Read main executable directly from CDRom and start it
	if (LoadCdrom() == -1) {
		ClosePlugins();
		menu_update_msg("failed to load CD image");
		return -1;
	}

	emu_on_new_cd(1);
	ready_to_go = 1;

	if (autoload_state) {
		unsigned int newest = 0;
		int time, slot, newest_slot = -1;

		for (slot = 0; slot < 10; slot++) {
			if (emu_check_save_file(slot, &time)) {
				if ((unsigned int)time > newest) {
					newest = time;
					newest_slot = slot;
				}
			}
		}

		if (newest_slot >= 0) {
			lprintf("autoload slot %d\n", newest_slot);
			emu_load_state(newest_slot);
		}
		else {
			lprintf("no save to autoload.\n");
		}
	}

	return 0;
}

static int romsel_run(void)
{
	int prev_gpu, prev_spu;
	const char *fname;

	fname = menu_loop_romsel(last_selected_fname,
			sizeof(last_selected_fname), filter_exts,
			optional_cdimg_filter);
	if (fname == NULL)
		return -1;

	printf("selected file: %s\n", fname);

	new_dynarec_clear_full();

	if (run_cd_image(fname) != 0)
		return -1;

	prev_gpu = gpu_plugsel;
	prev_spu = spu_plugsel;
	if (menu_load_config(1) != 0)
		menu_load_config(0);

	// check for plugin changes, have to repeat
	// loading if game config changed plugins to reload them
	if (prev_gpu != gpu_plugsel || prev_spu != spu_plugsel) {
		printf("plugin change detected, reloading plugins..\n");
		if (run_cd_image(fname) != 0)
			return -1;
	}

	strcpy(last_selected_fname, fname);
	menu_do_last_cd_img(0);
	return 0;
}

static int swap_cd_image(void)
{
	const char *fname;

	fname = menu_loop_romsel(last_selected_fname,
			sizeof(last_selected_fname), filter_exts,
			optional_cdimg_filter);
	if (fname == NULL)
		return -1;

	printf("selected file: %s\n", fname);

	CdromId[0] = '\0';
	CdromLabel[0] = '\0';

	set_cd_image(fname);
	if (ReloadCdromPlugin() < 0) {
		menu_update_msg("failed to load cdr plugin");
		return -1;
	}
	if (CDR_open() < 0) {
		menu_update_msg("failed to open cdr plugin");
		return -1;
	}

	SetCdOpenCaseTime(time(NULL) + 2);
	LidInterrupt();

	strcpy(last_selected_fname, fname);
	return 0;
}

static int swap_cd_multidisk(void)
{
	// Nick
	cdrIsoMultidiskSelect++;
	CdromId[0] = '\0';
	CdromLabel[0] = '\0';

	CDR_close();
	if (CDR_open() < 0) {
		menu_update_msg("failed to open cdr plugin");
		return -1;
	}

	SetCdOpenCaseTime(time(NULL) + 2);
	LidInterrupt();

	return 0;
}

static void load_pcsx_cht(void)
{
	static const char *exts[] = { "cht", NULL };
	const char *fname;
	char msg[64];

	fname = menu_loop_romsel(last_selected_fname,
			sizeof(last_selected_fname), exts, NULL);
	if (fname == NULL)
		return;

	printf("selected cheat file: %s\n", fname);
	LoadCheats(fname);

	if (NumCheats == 0 && NumCodes == 0)
		menu_update_msg("failed to load cheats");
	else {
		snprintf(msg, sizeof(msg), "%d cheat(s) loaded", NumCheats + NumCodes);
		menu_update_msg(msg);
	}
	me_enable(e_menu_main, MA_MAIN_CHEATS, ready_to_go && NumCheats);
}

static int main_menu_handler(int id, int keys)
{
	switch (id)
	{
	case MA_MAIN_RESUME_GAME:
		if (ready_to_go)
			return 1;
		break;
	case MA_MAIN_SAVE_STATE:
		if (ready_to_go)
			return menu_loop_savestate(0);
		break;
	case MA_MAIN_LOAD_STATE:
		if (ready_to_go)
			return menu_loop_savestate(1);
		break;
	case MA_MAIN_RESET_GAME:
		if (ready_to_go && reset_game() == 0)
			return 1;
		break;
	case MA_MAIN_LOAD_ROM:
		if (romsel_run() == 0)
			return 1;
		break;
	case MA_MAIN_SWAP_CD:
		if (swap_cd_image() == 0)
			return 1;
		break;
	case MA_MAIN_SWAP_CD_MULTI:
		if (swap_cd_multidisk() == 0)
			return 1;
		break;
	case MA_MAIN_RUN_BIOS:
		if (run_bios() == 0)
			return 1;
		break;
	case MA_MAIN_RUN_EXE:
		if (run_exe() == 0)
			return 1;
		break;
	case MA_MAIN_CHEATS:
		menu_loop_cheats();
		break;
	case MA_MAIN_LOAD_CHEATS:
		load_pcsx_cht();
		break;
	case MA_MAIN_CREDITS:
		draw_menu_message(credits_text, draw_frame_credits);
		in_menu_wait(PBTN_MOK|PBTN_MBACK, NULL, 70);
		break;
	case MA_MAIN_EXIT:
		emu_core_ask_exit();
		return 1;
	default:
		lprintf("%s: something unknown selected\n", __FUNCTION__);
		break;
	}

	return 0;
}

static menu_entry e_menu_main2[] =
{
	mee_handler_id("Change CD image",    MA_MAIN_SWAP_CD,       main_menu_handler),
	mee_handler_id("Next multidisk CD",  MA_MAIN_SWAP_CD_MULTI, main_menu_handler),
	mee_handler_id("Run BIOS",           MA_MAIN_RUN_BIOS,      main_menu_handler),
	mee_handler_id("Run EXE",            MA_MAIN_RUN_EXE,       main_menu_handler),
	mee_handler   ("Memcard manager",    menu_loop_memcards),
	mee_handler_id("Load PCSX cheats..", MA_MAIN_LOAD_CHEATS,   main_menu_handler),
	mee_end,
};

static int main_menu2_handler(int id, int keys)
{
	static int sel = 0;

	me_enable(e_menu_main2, MA_MAIN_SWAP_CD,  ready_to_go);
	me_enable(e_menu_main2, MA_MAIN_SWAP_CD_MULTI, ready_to_go && cdrIsoMultidiskCount > 1);
	me_enable(e_menu_main2, MA_MAIN_RUN_BIOS, bios_sel != 0);
	me_enable(e_menu_main2, MA_MAIN_LOAD_CHEATS, ready_to_go);

	return me_loop_d(e_menu_main2, &sel, NULL, draw_frame_main);
}

static const char h_extra[] = "Change CD, manage memcards..\n";

static menu_entry e_menu_main[] =
{
	mee_label     (""),
	mee_label     (""),
	mee_handler_id("Resume game",        MA_MAIN_RESUME_GAME, main_menu_handler),
	mee_handler_id("Save State",         MA_MAIN_SAVE_STATE,  main_menu_handler),
	mee_handler_id("Load State",         MA_MAIN_LOAD_STATE,  main_menu_handler),
	mee_handler_id("Reset game",         MA_MAIN_RESET_GAME,  main_menu_handler),
	mee_handler_id("Load CD image",      MA_MAIN_LOAD_ROM,    main_menu_handler),
	mee_handler   ("Options",            menu_loop_options),
	mee_handler   ("Controls",           menu_loop_keyconfig),
	mee_handler_id("Cheats",             MA_MAIN_CHEATS,      main_menu_handler),
	mee_handler_h ("Extra stuff",        main_menu2_handler,  h_extra),
	mee_handler_id("Credits",            MA_MAIN_CREDITS,     main_menu_handler),
	mee_handler_id("Exit",               MA_MAIN_EXIT,        main_menu_handler),
	mee_end,
};

// ----------------------------

static void menu_leave_emu(void);

void check_bioses(void){
    // assume first run
    if (bioses[1] != NULL) {
        // autoselect BIOS to make user's life easier
        printf("bioses\n");
        snprintf(Config.Bios, sizeof(Config.Bios), "%s", bioses[1]);
        bios_sel = 1;
    }
    else {
        printf("menu_bios_warn\n");
        menu_bios_warn();
    }
}

void menu_loop(void)
{
	static int warned_about_bios = 0;
	static int sel = 0;

	menu_leave_emu();

	if (config_save_counter == 0) {
		if (bioses[1] != NULL) {
			// autoselect BIOS to make user's life easier
			printf("bioses\n");
			snprintf(Config.Bios, sizeof(Config.Bios), "%s", bioses[1]);
			bios_sel = 1;
		}
		else if (!warned_about_bios) {
			printf("menu_bios_warn\n");
			menu_bios_warn();
			warned_about_bios = 1;
		}
	}

	me_enable(e_menu_main, MA_MAIN_RESUME_GAME, ready_to_go);
	me_enable(e_menu_main, MA_MAIN_SAVE_STATE,  ready_to_go && CdromId[0]);
	me_enable(e_menu_main, MA_MAIN_LOAD_STATE,  ready_to_go && CdromId[0]);
	me_enable(e_menu_main, MA_MAIN_RESET_GAME,  ready_to_go);
	me_enable(e_menu_main, MA_MAIN_CHEATS,      ready_to_go && NumCheats);

	in_set_config_int(0, IN_CFG_BLOCKING, 1);

	do {
		me_loop_d(e_menu_main, &sel, NULL, draw_frame_main);
	} while (!ready_to_go && !g_emu_want_quit);

	/* wait until menu, ok, back is released */
	while (in_menu_wait_any(NULL, 50) & (PBTN_MENU|PBTN_MOK|PBTN_MBACK))
		;

	in_set_config_int(0, IN_CFG_BLOCKING, 0);

	menu_prepare_emu();
}

static int qsort_strcmp(const void *p1, const void *p2)
{
	char * const *s1 = (char * const *)p1;
	char * const *s2 = (char * const *)p2;
	return strcasecmp(*s1, *s2);
}

static void scan_bios_plugins(void)
{
	char fname[MAXPATHLEN];
	struct dirent *ent;
	int bios_i, gpu_i, spu_i, mc_i;
	char *p;
	DIR *dir;

	bioses[0] = "HLE";
	gpu_plugins[0] = "builtin_gpu";
	spu_plugins[0] = "builtin_spu";
	memcards[0] = "(none)";
	bios_i = gpu_i = spu_i = mc_i = 1;

	snprintf(fname, sizeof(fname), "%s/", Config.BiosDir);
	dir = opendir(fname);
	if (dir == NULL) {
		perror("scan_bios_plugins bios opendir");
		goto do_plugins;
	}

	while (1) {
		struct stat st;

		errno = 0;
		ent = readdir(dir);
		if (ent == NULL) {
			if (errno != 0)
				perror("readdir");
			break;
		}

		if (ent->d_type != DT_REG && ent->d_type != DT_LNK)
			continue;

		snprintf(fname, sizeof(fname), "%s/%s", Config.BiosDir, ent->d_name);
		if (stat(fname, &st) != 0 || st.st_size != 512*1024) {
			printf("bad BIOS file: %s\n", ent->d_name);
			continue;
		}

		if (bios_i < ARRAY_SIZE(bioses) - 1) {
			bioses[bios_i++] = strdup(ent->d_name);
			continue;
		}

		printf("too many BIOSes, dropping \"%s\"\n", ent->d_name);
	}

	closedir(dir);

do_plugins:
	snprintf(fname, sizeof(fname), "%s/", Config.PluginsDir);
	dir = opendir(fname);
	if (dir == NULL) {
		perror("scan_bios_plugins plugins opendir");
		goto do_memcards;
	}

	while (1) {
		void *h, *tmp;

		errno = 0;
		ent = readdir(dir);
		if (ent == NULL) {
			if (errno != 0)
				perror("readdir");
			break;
		}
		p = strstr(ent->d_name, ".so");
		if (p == NULL)
			continue;

		snprintf(fname, sizeof(fname), "%s/%s", Config.PluginsDir, ent->d_name);
		h = dlopen(fname, RTLD_LAZY | RTLD_LOCAL);
		if (h == NULL) {
			fprintf(stderr, "%s\n", dlerror());
			continue;
		}

		// now what do we have here?
		tmp = dlsym(h, "GPUinit");
		if (tmp) {
			dlclose(h);
			if (gpu_i < ARRAY_SIZE(gpu_plugins) - 1)
				gpu_plugins[gpu_i++] = strdup(ent->d_name);
			continue;
		}

		tmp = dlsym(h, "SPUinit");
		if (tmp) {
			dlclose(h);
			if (spu_i < ARRAY_SIZE(spu_plugins) - 1)
				spu_plugins[spu_i++] = strdup(ent->d_name);
			continue;
		}

		fprintf(stderr, "ignoring unidentified plugin: %s\n", fname);
		dlclose(h);
	}

	closedir(dir);

do_memcards:
	dir = opendir("." MEMCARD_DIR);
	if (dir == NULL) {
		perror("scan_bios_plugins memcards opendir");
		return;
	}

	while (1) {
		struct stat st;

		errno = 0;
		ent = readdir(dir);
		if (ent == NULL) {
			if (errno != 0)
				perror("readdir");
			break;
		}

		if (ent->d_type != DT_REG && ent->d_type != DT_LNK)
			continue;

		snprintf(fname, sizeof(fname), "." MEMCARD_DIR "%s", ent->d_name);
		if (stat(fname, &st) != 0) {
			printf("bad memcard file: %s\n", ent->d_name);
			continue;
		}

		if (mc_i < ARRAY_SIZE(memcards) - 1) {
			memcards[mc_i++] = strdup(ent->d_name);
			continue;
		}

		printf("too many memcards, dropping \"%s\"\n", ent->d_name);
	}

	if (mc_i > 2)
		qsort(memcards + 1, mc_i - 1, sizeof(memcards[0]), qsort_strcmp);

	closedir(dir);
}

void menu_init(void)
{
	char buff[MAXPATHLEN];
	int i;

	cpu_clock_st = cpu_clock = plat_target_cpu_clock_get();

	scan_bios_plugins();
	menu_init_base();

	menu_set_defconfig();
	menu_load_config(0);
	menu_do_last_cd_img(1);
	last_vout_w = 320;
	last_vout_h = 240;
	last_vout_bpp = 16;

	g_menubg_src_ptr = calloc(g_menuscreen_w * g_menuscreen_h * 2, 1);
	g_menubg_ptr = calloc(g_menuscreen_w * g_menuscreen_h * 2, 1);
	if (g_menubg_src_ptr == NULL || g_menubg_ptr == NULL) {
		fprintf(stderr, "OOM\n");
		exit(1);
	}

	emu_make_path(buff, "skin/background.png", sizeof(buff));
	readpng(g_menubg_src_ptr, buff, READPNG_BG, g_menuscreen_w, g_menuscreen_h);

	i = plat_target.cpu_clock_set != NULL
		&& plat_target.cpu_clock_get != NULL && cpu_clock_st > 0;
	me_enable(e_menu_gfx_options, MA_OPT_CPU_CLOCKS, i);

	i = me_id2offset(e_menu_gfx_options, MA_OPT_VOUT_MODE);
	e_menu_gfx_options[i].data = plat_target.vout_methods;
	me_enable(e_menu_gfx_options, MA_OPT_VOUT_MODE,
		plat_target.vout_methods != NULL);

	i = me_id2offset(e_menu_gfx_options, MA_OPT_HWFILTER);
	e_menu_gfx_options[i].data = plat_target.hwfilters;
	me_enable(e_menu_gfx_options, MA_OPT_HWFILTER,
		plat_target.hwfilters != NULL);

	me_enable(e_menu_gfx_options, MA_OPT_GAMMA,
		plat_target.gamma_set != NULL);

#ifdef HAVE_PRE_ARMV7
	me_enable(e_menu_gfx_options, MA_OPT_SWFILTER, 0);
#endif
	me_enable(e_menu_gfx_options, MA_OPT_VARSCALER, MENU_SHOW_VARSCALER);
	me_enable(e_menu_gfx_options, MA_OPT_VOUT_MODE, MENU_SHOW_VOUTMODE);
	me_enable(e_menu_gfx_options, MA_OPT_VARSCALER_C, MENU_SHOW_VARSCALER);
	me_enable(e_menu_gfx_options, MA_OPT_SCALER2, MENU_SHOW_SCALER2);
	me_enable(e_menu_keyconfig, MA_CTRL_NUBS_BTNS, MENU_SHOW_NUBS_BTNS);
	me_enable(e_menu_keyconfig, MA_CTRL_VIBRATION, MENU_SHOW_VIBRATION);
	me_enable(e_menu_keyconfig, MA_CTRL_DEADZONE, MENU_SHOW_DEADZONE);
}

void menu_notify_mode_change(int w, int h, int bpp)
{
	last_vout_w = w;
	last_vout_h = h;
	last_vout_bpp = bpp;
}

static void menu_leave_emu(void)
{
	if (GPU_close != NULL) {
		int ret = GPU_close();
		if (ret)
			fprintf(stderr, "Warning: GPU_close returned %d\n", ret);
	}

	plat_video_menu_enter(ready_to_go);

	memcpy(g_menubg_ptr, g_menubg_src_ptr, g_menuscreen_w * g_menuscreen_h * 2);
	if (pl_vout_buf != NULL && ready_to_go) {
		int x = max(0, g_menuscreen_w - last_vout_w);
		int y = max(0, g_menuscreen_h / 2 - last_vout_h / 2);
		int w = min(g_menuscreen_w, last_vout_w);
		int h = min(g_menuscreen_h, last_vout_h);
		u16 *d = (u16 *)g_menubg_ptr + g_menuscreen_w * y + x;
		char *s = pl_vout_buf;

		if (last_vout_bpp == 16) {
			for (; h > 0; h--, d += g_menuscreen_w, s += last_vout_w * 2)
				menu_darken_bg(d, s, w, 0);
		}
		else {
			for (; h > 0; h--, d += g_menuscreen_w, s += last_vout_w * 3) {
				rgb888_to_rgb565(d, s, w * 3);
				menu_darken_bg(d, d, w, 0);
			}
		}
	}

	if (ready_to_go)
		cpu_clock = plat_target_cpu_clock_get();
}

void menu_prepare_emu(void)
{
	R3000Acpu *prev_cpu = psxCpu;

	plat_video_menu_leave();

	#if !defined(DRC_DISABLE) || defined(LIGHTREC)
	psxCpu = (Config.Cpu == CPU_INTERPRETER) ? &psxInt : &psxRec;
	#else
	psxCpu = &psxInt;
	#endif
	if (psxCpu != prev_cpu) {
		prev_cpu->Shutdown();
		psxCpu->Init();
		// note that this does not really reset, just clears drc caches
		psxCpu->Reset();
	}

	menu_sync_config();
	psxCpu->ApplyConfig();

	// core doesn't care about Config.Cdda changes,
	// so handle them manually here
	if (Config.Cdda)
		CDR_stop();

	if (cpu_clock > 0)
		plat_target_cpu_clock_set(cpu_clock);

	// push config to GPU plugin
	plugin_call_rearmed_cbs();

	if (GPU_open != NULL) {
		int ret = GPU_open(&gpuDisp, "PCSX", NULL);
		if (ret)
			fprintf(stderr, "Warning: GPU_open returned %d\n", ret);
	}

	dfinput_activate();
}

void menu_update_msg(const char *msg)
{
	strncpy(menu_error_msg, msg, sizeof(menu_error_msg));
	menu_error_msg[sizeof(menu_error_msg) - 1] = 0;

	menu_error_time = plat_get_ticks_ms();
	lprintf("msg: %s\n", menu_error_msg);
}

void menu_finish(void)
{
	if (cpu_clock_st > 0)
		plat_target_cpu_clock_set(cpu_clock_st);
}
