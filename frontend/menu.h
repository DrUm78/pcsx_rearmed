void menu_init(void);
void menu_prepare_emu(void);
void menu_loop(void);
void menu_finish(void);

void init_menu_SDL();
void deinit_menu_SDL();
void init_menu_zones();
void run_menu_loop();
void init_menu_system_values();

void menu_notify_mode_change(int w, int h, int bpp);

enum g_opts_opts {
	OPT_SHOWFPS = 1 << 0,
	OPT_SHOWCPU = 1 << 1,
	OPT_NO_FRAMELIM = 1 << 2,
	OPT_SHOWSPU = 1 << 3,
	OPT_TSGUN_NOTRIGGER = 1 << 4,
};

enum g_scaler_opts {
	SCALE_1_1,
	SCALE_2_2,
	SCALE_4_3,
	SCALE_4_3v2,
	SCALE_FULLSCREEN,
	SCALE_CUSTOM,
};

enum g_soft_filter_opts {
	SOFT_FILTER_NONE,
	SOFT_FILTER_SCALE2X,
	SOFT_FILTER_EAGLE2X,
};

typedef enum{
    MENU_TYPE_VOLUME,
    MENU_TYPE_BRIGHTNESS,
    MENU_TYPE_SAVE,
    MENU_TYPE_LOAD,
    MENU_TYPE_ASPECT_RATIO,
    MENU_TYPE_EXIT,
    MENU_TYPE_POWERDOWN,
    NB_MENU_TYPES,
} ENUM_MENU_TYPE;


///------ Definition of the different aspect ratios
#define ASPECT_RATIOS \
    X(ASPECT_RATIOS_TYPE_MANUAL, "MANUAL ZOOM") \
    X(ASPECT_RATIOS_TYPE_STRECHED, "STRECHED") \
    X(ASPECT_RATIOS_TYPE_CROPPED, "CROPPED") \
    X(ASPECT_RATIOS_TYPE_SCALED, "SCALED") \
    X(NB_ASPECT_RATIOS_TYPES, "")

////------ Enumeration of the different aspect ratios ------
#undef X
#define X(a, b) a,
typedef enum {ASPECT_RATIOS} ENUM_ASPECT_RATIOS_TYPES;

////------ Defines to be shared -------
#define STEP_CHANGE_VOLUME          10
#define STEP_CHANGE_BRIGHTNESS      10
#define NOTIF_SECONDS_DISP			2

////------ Menu commands -------
#define SHELL_CMD_VOLUME_GET        "volume_get"
#define SHELL_CMD_VOLUME_SET        "volume_set"
#define SHELL_CMD_BRIGHTNESS_GET    "brightness_get"
#define SHELL_CMD_BRIGHTNESS_SET    "brightness_set"
#define SHELL_CMD_POWERDOWN         "shutdown_funkey"
#define SHELL_CMD_NOTIF				"notif_set"

////------ Global variables -------
extern int g_opts, g_scaler, g_gamma;
extern int scanlines, scanline_level;
extern int soft_scaling, analog_deadzone;
extern int soft_filter;

extern int g_menuscreen_w;
extern int g_menuscreen_h;

extern int volume_percentage;
extern int brightness_percentage;

extern const char *aspect_ratio_name[];
extern int aspect_ratio;
extern int aspect_ratio_factor_percent;
extern int aspect_ratio_factor_step;
