void menu_init(void);
void menu_prepare_emu(void);
void menu_loop(void);
void check_bioses(void);
void menu_finish(void);

void init_menu_SDL();
void deinit_menu_SDL();
void init_menu_zones();
void run_menu_loop();
void init_menu_system_values();
int launch_resume_menu_loop();

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

///------ Definition of the different resume options
#define RESUME_OPTIONS \
    X(RESUME_YES, "RESUME GAME") \
    X(RESUME_NO, "NEW GAME") \
    X(NB_RESUME_OPTIONS, "")

////------ Enumeration of the different resume options ------
#undef X
#define X(a, b) a,
typedef enum {RESUME_OPTIONS} ENUM_RESUME_OPTIONS;

////------ Defines to be shared -------
#define STEP_CHANGE_VOLUME          10
#define STEP_CHANGE_BRIGHTNESS      10
#define NOTIF_SECONDS_DISP          2

////------ Menu commands -------
#define SHELL_CMD_VOLUME_GET                "volume get"
#define SHELL_CMD_VOLUME_SET                "volume set"
#define SHELL_CMD_BRIGHTNESS_GET            "brightness get"
#define SHELL_CMD_BRIGHTNESS_SET            "brightness set"
#define SHELL_CMD_NOTIF_SET                 "notif set"
#define SHELL_CMD_NOTIF_CLEAR               "notif clear"
#define SHELL_CMD_AUDIO_AMP_ON              "audio_amp on"
#define SHELL_CMD_AUDIO_AMP_OFF             "audio_amp off"
#define SHELL_CMD_POWERDOWN                 "powerdown"
#define SHELL_CMD_POWERDOWN_HANDLE          "powerdown handle"
#define SHELL_CMD_INSTANT_PLAY              "instant_play"
#define SHELL_CMD_KEYMAP_DEFAULT            "keymap default"
#define SHELL_CMD_KEYMAP_RESUME             "keymap resume"

////------ Global variables -------
extern int g_opts, g_scaler, g_gamma;
extern int scanlines, scanline_level;
extern int soft_scaling, analog_deadzone;
extern int soft_filter;

extern int g_menuscreen_w;
extern int g_menuscreen_h;

extern int volume_percentage;
extern int brightness_percentage;

extern int stop_menu_loop;
extern char *quick_save_file;
extern char *cfg_file_rom;
