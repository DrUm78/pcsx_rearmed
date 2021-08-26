/*
 * (C) notaz, 2010-2012
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#ifndef _WIN32
#include <dlfcn.h>
#endif

#include "main.h"
#include "plugin.h"
#include "plugin_lib.h"
#include "pcnt.h"
#include "menu.h"
#include "configfile_fk.h"
#include "plat.h"
#include "../libpcsxcore/misc.h"
#include "../libpcsxcore/cheat.h"
#include "../libpcsxcore/new_dynarec/new_dynarec.h"
#include "../plugins/cdrcimg/cdrcimg.h"
#include "../plugins/dfsound/spu_config.h"
#include "arm_features.h"
#include "revision.h"

#ifndef NO_FRONTEND
#include "libpicofe/input.h"
#include "libpicofe/plat.h"
#include "libpicofe/readpng.h"

static void toggle_fast_forward(int force_off);
static void check_profile(void);
static void check_memcards(void);
#endif

#define BOOT_MSG " "
#ifndef BOOT_MSG
#define BOOT_MSG "Booting up..."
#endif

// don't include debug.h - it breaks ARM build (R1 redefined)
void StartDebugger();
void StopDebugger();

int ready_to_go, g_emu_want_quit, g_emu_resetting;
unsigned long gpuDisp;
int need_screen_cleared;
char cfgfile_basename[MAXPATHLEN];
int state_slot;
enum sched_action emu_action, emu_action_old;
enum sched_action emu_action_future = SACTION_NONE;
int mQuickSaveAndPoweroff=0;
char hud_msg[64];
int hud_new_msg;
char *prog_name = NULL;
char *cdfile = NULL;
char *cdPath = NULL;
static char *quick_save_file_extension = "quicksave";
char *quick_save_file = NULL;
char *cfg_file_default = NULL;
char *cfg_file_rom = NULL;
static char *cfg_file_default_name = "default_config";
static char *cfg_file_extension = "fkcfg";



/* Quick save and turn off the console */
void quick_save_and_poweroff()
{
    FILE *fp;
    printf("Save Instant Play file\n");

    /* Send command to cancel any previously scheduled powerdown */
    fp = popen(SHELL_CMD_POWERDOWN_HANDLE, "r");
    if (fp == NULL)
    {
        /* Countdown is still ticking, so better do nothing
	   than start writing and get interrupted!
	*/
        printf("Failed to cancel scheduled shutdown\n");
	exit(0);
    }
    pclose(fp);

    /* Save  */
    if(SaveState(quick_save_file)){
	printf("Save failed");
	return;
    }

    /* Perform Instant Play save and shutdown */
    execlp(SHELL_CMD_INSTANT_PLAY, SHELL_CMD_INSTANT_PLAY,
	   "save", prog_name, "-cdfile", cdfile, "-loadf", quick_save_file, NULL);

    /* Should not be reached */
    printf("Failed to perform Instant Play save and shutdown\n");

    /* Exit Emulator */
    exit(0);
}

/* Handler for SIGUSR1, caused by closing the console */
void handle_sigusr1(int sig)
{
    //printf("Caught signal USR1 %d\n", sig);

    /* Exit menu if it was launched */
    stop_menu_loop = 1;

    /* Signal to quick save and poweoff after next loop */
    emu_set_action(SACTION_QUICK_SAVE_AND_POWEROFF);
    mQuickSaveAndPoweroff = 1;
}

static void make_relative_path(char *buf, size_t size, const char *dir, const char *fname)
{
	if (fname)
		snprintf(buf, size, ".%s%s", dir, fname);
	else
		snprintf(buf, size, ".%s", dir);
}
#define MAKE_RELATIVE_PATH(buf, dir, fname) \
	make_relative_path(buf, sizeof(buf), dir, fname)

static void make_absolute_path(char *buf, size_t size, const char *dir, const char *fname)
{
	if (fname)
		snprintf(buf, size, "%s%s", dir, fname);
	else
		snprintf(buf, size, "%s", dir);
}
#define MAKE_ABSOLUTE_PATH(buf, dir, fname) \
	make_absolute_path(buf, sizeof(buf), dir, fname)

static int get_gameid_filename(char *buf, int size, const char *fmt, int i) {
	char trimlabel[33];
	int j;

	strncpy(trimlabel, CdromLabel, 32);
	trimlabel[32] = 0;
	for (j = 31; j >= 0; j--)
		if (trimlabel[j] == ' ')
			trimlabel[j] = 0;
		else
			continue;

	snprintf(buf, size, fmt, trimlabel, CdromId, i);

	return 0;
}

void set_cd_image(const char *fname)
{
	const char *ext = NULL;

	if (fname != NULL)
		ext = strrchr(fname, '.');

	if (ext && (
	    strcasecmp(ext, ".z") == 0 || strcasecmp(ext, ".bz") == 0 ||
	    strcasecmp(ext, ".znx") == 0 /*|| strcasecmp(ext, ".pbp") == 0*/)) {
		SetIsoFile(NULL);
		cdrcimg_set_fname(fname);
		strcpy(Config.Cdr, "builtin_cdrcimg");
	} else {
		SetIsoFile(fname);
		strcpy(Config.Cdr, "builtin_cdr");
	}
}

static void set_default_paths(void)
{
#ifndef NO_FRONTEND
	snprintf(Config.PatchesDir, sizeof(Config.PatchesDir), "." PATCHES_DIR);
	MAKE_ABSOLUTE_PATH(Config.Mcd1, MEMCARD_DIR, "card1.mcd");
	MAKE_ABSOLUTE_PATH(Config.Mcd2, MEMCARD_DIR, "card2.mcd");
	strcpy(Config.BiosDir, "/mnt/PS1/bios");
#endif

	strcpy(Config.PluginsDir, "plugins");
	strcpy(Config.Gpu, "builtin_gpu");
	strcpy(Config.Spu, "builtin_spu");
	strcpy(Config.Cdr, "builtin_cdr");
	strcpy(Config.Pad1, "builtin_pad");
	strcpy(Config.Pad2, "builtin_pad");
	strcpy(Config.Net, "Disabled");
}

void emu_set_default_config(void)
{
	// try to set sane config on which most games work
	Config.Xa = Config.Cdda = Config.Sio =
	Config.SpuIrq = Config.RCntFix = Config.VSyncWA = 0;
	Config.PsxAuto = 1;
	Config.AsyncCD = 1;

	pl_rearmed_cbs.gpu_neon.allow_interlace = 2; // auto
	pl_rearmed_cbs.gpu_neon.enhancement_enable =
	pl_rearmed_cbs.gpu_neon.enhancement_no_main = 0;
	pl_rearmed_cbs.gpu_peops.iUseDither = 0;
	pl_rearmed_cbs.gpu_peops.dwActFixes = 1<<7;
	pl_rearmed_cbs.gpu_unai.abe_hack =
	pl_rearmed_cbs.gpu_unai.no_light =
	pl_rearmed_cbs.gpu_unai.no_blend = 0;
	memset(&pl_rearmed_cbs.gpu_peopsgl, 0, sizeof(pl_rearmed_cbs.gpu_peopsgl));
	pl_rearmed_cbs.gpu_peopsgl.iVRamSize = 64;
	pl_rearmed_cbs.gpu_peopsgl.iTexGarbageCollection = 1;

	spu_config.iUseReverb = 1;
	spu_config.iUseInterpolation = 1;
	spu_config.iXAPitch = 0;
	spu_config.iVolume = 768;
	spu_config.iTempo = 0;
	spu_config.iUseThread = 1; // no effect if only 1 core is detected
#ifdef HAVE_PRE_ARMV7 /* XXX GPH hack */
	spu_config.iUseReverb = 0;
	spu_config.iUseInterpolation = 0;
	spu_config.iTempo = 1;
#endif
	new_dynarec_hacks = 0;
	cycle_multiplier = 200;

	in_type1 = PSE_PAD_TYPE_STANDARD;
	in_type2 = PSE_PAD_TYPE_STANDARD;
}

void do_emu_action(void)
{
	int ret;
	char shell_cmd[100];
	FILE *fp;
	emu_action_old = emu_action;

	switch (emu_action) {
	case SACTION_QUICK_SAVE_AND_POWEROFF:
		quick_save_and_poweroff();
		mQuickSaveAndPoweroff = 0;
		break;
	case SACTION_LOAD_STATE:
		//snprintf(hud_msg, sizeof(hud_msg), "LOADING FROM SLOT %d...", state_slot+1);
		sprintf(shell_cmd, "%s %d \"    LOADING FROM SLOT %d...\"",
			SHELL_CMD_NOTIF_SET, NOTIF_SECONDS_DISP, state_slot+1);
		ret = emu_load_state(state_slot);
		//snprintf(hud_msg, sizeof(hud_msg), "%s FROM SLOT %d", ret == 0 ? "LOADED" : "FAILED TO LOAD", state_slot+1);
		//hud_new_msg = 4;
		sprintf(shell_cmd, "%s %d \"%s FROM SLOT %d\"",
			SHELL_CMD_NOTIF_SET, NOTIF_SECONDS_DISP, ret == 0 ? "      LOADED" : "  FAILED TO LOAD", state_slot+1);
		fp = popen(shell_cmd, "r");
		if (fp == NULL) {
			printf("Failed to run command %s\n", shell_cmd);
		} else {
			pclose(fp);
		}
		break;
	case SACTION_PRE_SAVE_STATE:
		/*snprintf(hud_msg, sizeof(hud_msg), "SAVING IN SLOT %d...", state_slot+1);
		  hud_new_msg = 4;*/
		sprintf(shell_cmd, "%s %d \"      SAVING IN SLOT %d...\"",
			SHELL_CMD_NOTIF_SET, NOTIF_SECONDS_DISP, state_slot+1);
		fp = popen(shell_cmd, "r");
		if (fp == NULL) {
			printf("Failed to run command %s\n", shell_cmd);
		} else {
			pclose(fp);
		}

		emu_action_future = SACTION_SAVE_STATE;
		break;
	case SACTION_SAVE_STATE:
		ret = emu_save_state(state_slot);
		/*snprintf(hud_msg, sizeof(hud_msg), "%s IN SLOT %d", ret == 0 ? "SAVED" : "FAILED TO LOAD", state_slot+1);
		  hud_new_msg = 4;*/
		sprintf(shell_cmd, "%s %d \"%s IN SLOT %d\"",
			SHELL_CMD_NOTIF_SET, NOTIF_SECONDS_DISP, ret == 0 ? "        SAVED" : "  FAILED TO SAVE", state_slot+1);
		fp = popen(shell_cmd, "r");
		if (fp == NULL) {
			printf("Failed to run command %s\n", shell_cmd);
		} else {
			pclose(fp);
		}
		break;
#ifndef NO_FRONTEND
	case SACTION_ENTER_MENU:
		toggle_fast_forward(1);
		//menu_loop();

		in_set_config_int(0, IN_CFG_BLOCKING, 1);
		run_menu_loop();
		in_set_config_int(0, IN_CFG_BLOCKING, 0);
		//printf("exiting menu\n");
		return;
	case SACTION_NEXT_SSLOT:
		state_slot++;
		if (state_slot > 9)
			state_slot = 0;
		goto do_state_slot;
	case SACTION_PREV_SSLOT:
		state_slot--;
		if (state_slot < 0)
			state_slot = 9;
do_state_slot:
		snprintf(hud_msg, sizeof(hud_msg), "STATE SLOT %d [%s]", state_slot,
			emu_check_state(state_slot) == 0 ? "USED" : "FREE");
		hud_new_msg = 3;
		SysPrintf("* %s\n", hud_msg);
		break;
	case SACTION_TOGGLE_FSKIP:
		pl_rearmed_cbs.fskip_advice = 0;
		pl_rearmed_cbs.frameskip++;
		if (pl_rearmed_cbs.frameskip > 1)
			pl_rearmed_cbs.frameskip = -1;
		snprintf(hud_msg, sizeof(hud_msg), "FRAMESKIP: %s",
			pl_rearmed_cbs.frameskip == -1 ? "AUTO" :
			pl_rearmed_cbs.frameskip == 0 ? "OFF" : "1" );
		plugin_call_rearmed_cbs();
		break;
	case SACTION_SWITCH_DISPMODE:
		aspect_ratio = (aspect_ratio+1)%NB_ASPECT_RATIOS_TYPES;
		if(aspect_ratio == ASPECT_RATIOS_TYPE_MANUAL){
			//snprintf(hud_msg, sizeof(hud_msg), "    DISPLAY MODE: ZOOMED %d%%", aspect_ratio_factor_percent);
			sprintf(shell_cmd, "%s %d \"    DISPLAY MODE: ZOOMED %d%%%%\"",
				SHELL_CMD_NOTIF_SET, NOTIF_SECONDS_DISP, aspect_ratio_factor_percent);
		}
		else{
			//snprintf(hud_msg, sizeof(hud_msg), "DISPLAY MODE: %s", aspect_ratio_name[aspect_ratio]);
			sprintf(shell_cmd, "%s %d \"    DISPLAY MODE: %s\"",
				SHELL_CMD_NOTIF_SET, NOTIF_SECONDS_DISP, aspect_ratio_name[aspect_ratio]);
		}
		//hud_new_msg = 4;
		fp = popen(shell_cmd, "r");
		if (fp == NULL) {
			printf("Failed to run command %s\n", shell_cmd);
		} else {
			pclose(fp);
		}

        // Save config file
        configfile_save(cfg_file_rom);
		break;
	case SACTION_ASPECT_RATIO_FACTOR_DECREASE:
		if(aspect_ratio == ASPECT_RATIOS_TYPE_MANUAL){
			aspect_ratio_factor_percent = (aspect_ratio_factor_percent>aspect_ratio_factor_step)?
				aspect_ratio_factor_percent-aspect_ratio_factor_step:0;
			need_screen_cleared = 1;
		}
		else{
			aspect_ratio = ASPECT_RATIOS_TYPE_MANUAL;
		}
		/*snprintf(hud_msg, sizeof(hud_msg), "    DISPLAY MODE: ZOOMED %d%%", aspect_ratio_factor_percent);
		  hud_new_msg = 4;*/
		sprintf(shell_cmd, "%s %d \"    DISPLAY MODE: ZOOMED %d%%%%\"",
			SHELL_CMD_NOTIF_SET, NOTIF_SECONDS_DISP, aspect_ratio_factor_percent);
		fp = popen(shell_cmd, "r");
		if (fp == NULL) {
			printf("Failed to run command %s\n", shell_cmd);
		} else {
			pclose(fp);
		}

        // Save config file
        configfile_save(cfg_file_rom);
		break;
	case SACTION_ASPECT_RATIO_FACTOR_INCREASE:
		if(aspect_ratio == ASPECT_RATIOS_TYPE_MANUAL){
			aspect_ratio_factor_percent = (aspect_ratio_factor_percent+aspect_ratio_factor_step<100)?
				aspect_ratio_factor_percent+aspect_ratio_factor_step:100;
			need_screen_cleared = 1;
		}
		else{
			aspect_ratio = ASPECT_RATIOS_TYPE_MANUAL;
		}
		aspect_ratio = ASPECT_RATIOS_TYPE_MANUAL;
		/*snprintf(hud_msg, sizeof(hud_msg), "    DISPLAY MODE: ZOOMED %d%%", aspect_ratio_factor_percent);
		  hud_new_msg = 4;*/
		sprintf(shell_cmd, "%s %d \"    DISPLAY MODE: ZOOMED %d%%%%\"",
			SHELL_CMD_NOTIF_SET, NOTIF_SECONDS_DISP, aspect_ratio_factor_percent);
		fp = popen(shell_cmd, "r");
		if (fp == NULL) {
			printf("Failed to run command %s\n", shell_cmd);
		} else {
			pclose(fp);
		}

        // Save config file
        configfile_save(cfg_file_rom);
		break;
	case SACTION_FAST_FORWARD:
		toggle_fast_forward(0);
		plugin_call_rearmed_cbs();
		break;
	case SACTION_TOGGLE_FPS:
		if ((g_opts & (OPT_SHOWFPS|OPT_SHOWCPU))
		    == (OPT_SHOWFPS|OPT_SHOWCPU))
			g_opts &= ~(OPT_SHOWFPS|OPT_SHOWCPU);
		else if (g_opts & OPT_SHOWFPS)
			g_opts |= OPT_SHOWCPU;
		else
			g_opts |= OPT_SHOWFPS;
		break;
	case SACTION_TOGGLE_FULLSCREEN:
		plat_target.vout_fullscreen = !plat_target.vout_fullscreen;
		if (GPU_open != NULL && GPU_close != NULL) {
			GPU_close();
			GPU_open(&gpuDisp, "PCSX", NULL);
		}
		break;
	case SACTION_SCREENSHOT:
		{
#ifdef SCREENSHOTS_ALLOWED
			char buf[MAXPATHLEN];
			void *scrbuf;
			int w, h, bpp;
			time_t t = time(NULL);
			struct tm *tb = localtime(&t);
			int ti = tb->tm_yday * 1000000 + tb->tm_hour * 10000 +
				tb->tm_min * 100 + tb->tm_sec;

			scrbuf = pl_prepare_screenshot(&w, &h, &bpp);
			get_gameid_filename(buf, sizeof(buf),
				"screenshots/%.32s-%.9s.%d.png", ti);
			ret = -1;
			if (scrbuf != 0 && bpp == 16)
				ret = writepng(buf, scrbuf, w, h);
			if (ret == 0)
				snprintf(hud_msg, sizeof(hud_msg), "SCREENSHOT TAKEN");
#endif //SCREENSHOTS_ALLOWED
			break;
		}
	case SACTION_VOLUME_DOWN:
		snprintf(hud_msg, sizeof(hud_msg), "VOLUME %d%%", volume_percentage);
		hud_new_msg = 4;
		/// ----- Compute new value -----
		volume_percentage = (volume_percentage < STEP_CHANGE_VOLUME)?
			0:(volume_percentage-STEP_CHANGE_VOLUME);
		/// ----- Shell cmd ----
		sprintf(shell_cmd, "%s %d", SHELL_CMD_VOLUME_SET, volume_percentage);
		fp = popen(shell_cmd, "r");
		if (fp == NULL) {
			printf("Failed to run command %s\n", shell_cmd);
		} else {
			pclose(fp);
		}
		break;
	case SACTION_VOLUME_UP:
		snprintf(hud_msg, sizeof(hud_msg), "VOLUME %d%%", volume_percentage);
		hud_new_msg = 4;
		/// ----- Compute new value -----
		volume_percentage = (volume_percentage > 100 - STEP_CHANGE_VOLUME)?
			100:(volume_percentage+STEP_CHANGE_VOLUME);
		/// ----- Shell cmd ----
		sprintf(shell_cmd, "%s %d", SHELL_CMD_VOLUME_SET, volume_percentage);
		fp = popen(shell_cmd, "r");
		if (fp == NULL) {
			printf("Failed to run command %s\n", shell_cmd);
		} else {
			pclose(fp);
		}
		break;
	case SACTION_BRIGHTNESS_DOWN:
		snprintf(hud_msg, sizeof(hud_msg), "BRIGHTNESS %d%%", brightness_percentage);
		hud_new_msg = 4;
		/// ----- System brightness change -----
		brightness_percentage = (brightness_percentage < STEP_CHANGE_BRIGHTNESS)?
			0:(brightness_percentage-STEP_CHANGE_BRIGHTNESS);
		/// ----- Shell cmd ----
		sprintf(shell_cmd, "%s %d", SHELL_CMD_BRIGHTNESS_SET, brightness_percentage);
		fp = popen(shell_cmd, "r");
		if (fp == NULL) {
			printf("Failed to run command %s\n", shell_cmd);
		} else {
			pclose(fp);
		}
		break;
	case SACTION_BRIGHTNESS_UP:
		snprintf(hud_msg, sizeof(hud_msg), "BRIGHTNESS %d%%", brightness_percentage);
		hud_new_msg = 4;
		/// ----- System brightness change -----
		brightness_percentage = (brightness_percentage > 100 - STEP_CHANGE_BRIGHTNESS)?
			100:(brightness_percentage+STEP_CHANGE_BRIGHTNESS);
		/// ----- Shell cmd ----
		sprintf(shell_cmd, "%s %d", SHELL_CMD_BRIGHTNESS_SET, brightness_percentage);
		fp = popen(shell_cmd, "r");
		if (fp == NULL) {
			printf("Failed to run command %s\n", shell_cmd);
		} else {
			pclose(fp);
		}
		break;
	case SACTION_MINIMIZE:
		if (GPU_close != NULL)
			GPU_close();

		plat_minimize();

		if (GPU_open != NULL) {
			ret = GPU_open(&gpuDisp, "PCSX", NULL);
			if (ret)
				SysMessage("GPU_open returned %d", ret);
		}
		return;
#endif
	default:
		return;
	}

	hud_new_msg = 3;
}

static char basic_lcase(char c)
{
	if ('A' <= c && c <= 'Z')
		return c - 'A' + 'a';
	return c;
}

static int cdidcmp(const char *id1, const char *id2)
{
	while (*id1 != 0 && *id2 != 0) {
		if (*id1 == '_') { id1++; continue; }
		if (*id2 == '_') { id2++; continue; }
		if (basic_lcase(*id1) != basic_lcase(*id2))
			break;
		id1++;
		id2++;
	}

	return *id1 - *id2;
}

static void parse_cwcheat(void)
{
	char line[256], buf[64], name[64], *p;
	int newcheat = 1;
	u32 a, v;
	FILE *f;

	f = fopen("cheatpops.db", "r");
	if (f == NULL)
		return;

	/* find the game */
	while (fgets(line, sizeof(line), f)) {
		if (sscanf(line, "_S %63s", buf) != 1)
			continue;
		if (cdidcmp(buf, CdromId) == 0)
			break;
	}

	if (feof(f))
		goto out;

	SysPrintf("cwcheat section found for %s\n", CdromId);
	while (fgets(line, sizeof(line), f))
	{
		p = line + strlen(line);
		for (p--; p >= line && (*p == '\r' || *p == '\n' || *p == ' '); p--)
			*p = 0;
		if (*p == 0 || *p == '#' || *p == ';')
			continue;

		if (strncmp(line, "_S", 2) == 0)
			break;
		if (strncmp(line, "_G", 2) == 0) {
			SysPrintf("  cwcheat game name: '%s'\n", line + 3);
			continue;
		}
		if (strncmp(line, "_C0", 3) == 0) {
			if (!newcheat && Cheats[NumCheats - 1].n == 0) {
				SysPrintf("cheat '%s' failed to parse\n", name);
				free(Cheats[NumCheats - 1].Descr);
				NumCheats--;
			}
			snprintf(name, sizeof(name), "%s", line + 4);
			newcheat = 1;
			continue;
		}
		if (sscanf(line, "_L %x %x", &a, &v) != 2) {
			SysPrintf("line failed to parse: '%s'\n", line);
			continue;
		}

		if (newcheat) {
			if (NumCheats >= NumCheatsAllocated) {
				NumCheatsAllocated += 16;
				Cheats = realloc(Cheats, sizeof(Cheat) *
						NumCheatsAllocated);
				if (Cheats == NULL)
					break;
			}
			Cheats[NumCheats].Descr = strdup(name);
			Cheats[NumCheats].Enabled = 0;
			Cheats[NumCheats].WasEnabled = 0;
			Cheats[NumCheats].First = NumCodes;
			Cheats[NumCheats].n = 0;
			NumCheats++;
			newcheat = 0;
		}

		if (NumCodes >= NumCodesAllocated) {
			NumCodesAllocated += 16;
			CheatCodes = realloc(CheatCodes, sizeof(CheatCode) *
				NumCodesAllocated);
			if (CheatCodes == NULL)
				break;
		}
		CheatCodes[NumCodes].Addr = a;
		CheatCodes[NumCodes].Val = v;
		NumCodes++;
		Cheats[NumCheats - 1].n++;
	}

out:
	fclose(f);
}

void emu_on_new_cd(int show_hud_msg)
{
	ClearAllCheats();
	parse_cwcheat();

	if (Config.HLE) {
		SysPrintf("note: running with HLE BIOS, expect compatibility problems\n");
		SysPrintf("----------------------------------------------------------\n");
	}

	if (show_hud_msg) {
		snprintf(hud_msg, sizeof(hud_msg), BOOT_MSG);
		hud_new_msg = 3;
	}
}

int emu_core_preinit(void)
{
	// what is the name of the config file?
	// it may be redefined by -cfg on the command line
	strcpy(cfgfile_basename, "pcsx.cfg");

#ifdef IOS
	emuLog = fopen("/User/Documents/pcsxr.log", "w");
	if (emuLog == NULL)
		emuLog = fopen("pcsxr.log", "w");
	if (emuLog == NULL)
#endif
	emuLog = stdout;

	SetIsoFile(NULL);

	memset(&Config, 0, sizeof(Config));

	set_default_paths();
	emu_set_default_config();
	strcpy(Config.Bios, "HLE");

	return 0;
}

int emu_core_init(void)
{
	SysPrintf("Starting PCSX-ReARMed " REV "\n");

#ifndef NO_FRONTEND
	check_profile();
	check_memcards();
#endif

	if (EmuInit() == -1) {
		SysPrintf("PSX emulator couldn't be initialized.\n");
		return -1;
	}

	LoadMcds(Config.Mcd1, Config.Mcd2);

	if (Config.Debug) {
		StartDebugger();
	}

	return 0;
}

void emu_core_ask_exit(void)
{
	stop = 1;
	g_emu_want_quit = 1;
}

#ifndef NO_FRONTEND

#include <sys/stat.h>
#include <sys/types.h>

static void create_profile_dir(const char *directory) {
	char path[MAXPATHLEN];

	MAKE_ABSOLUTE_PATH(path, directory, NULL);
	mkdir(path, S_IRWXU | S_IRWXG);
}

static void check_profile(void) {
	// make sure that ~/.pcsx exists
	create_profile_dir(PCSX_DOT_DIR);

	create_profile_dir(BIOS_DIR);
	create_profile_dir(MEMCARD_DIR);
	create_profile_dir(STATES_DIR);
	create_profile_dir(PLUGINS_DIR);
	create_profile_dir(PLUGINS_CFG_DIR);
	create_profile_dir(CHEATS_DIR);
	create_profile_dir(PATCHES_DIR);
	create_profile_dir(PCSX_DOT_DIR "cfg");
#ifdef SCREENSHOTS_ALLOWED
	create_profile_dir("/screenshots/");
#endif //SCREENSHOTS_ALLOWED
}

static void check_memcards(void)
{
	char buf[MAXPATHLEN];
	FILE *f;
	int i;

	for (i = 1; i <= 9; i++) {
		snprintf(buf, sizeof(buf), "%scard%d.mcd", MEMCARD_DIR, i);

		f = fopen(buf, "rb");
		if (f == NULL) {
			SysPrintf("Creating memcard: %s\n", buf);
			CreateMcd(buf);
		}
		else
			fclose(f);
	}
}

int main(int argc, char *argv[])
{
	char file[MAXPATHLEN] = "";
	char path[MAXPATHLEN];
	const char *loadst_f = NULL;
	int psxout = 0;
	int loadst = 0;
	int i;

	/* Save program name */
	prog_name = argv[0];

	emu_core_preinit();

	if (argc <= 1){
		printf("Not enough args specified, exit\n");
		exit(EXIT_FAILURE);
	};

	// read command line options
	for (i = 1; i < argc; i++) {
		     if (!strcmp(argv[i], "-psxout")) psxout = 1;
		else if (!strcmp(argv[i], "-load")) loadst = atol(argv[++i]);
		else if (!strcmp(argv[i], "-cfg")) {
			if (i+1 >= argc){
				printf("Config file not specified, exit\n");
				exit(EXIT_FAILURE);
			};
			strncpy(cfgfile_basename, argv[++i], MAXPATHLEN-100);	/* TODO buffer overruns */
			SysPrintf("Using config file %s.\n", cfgfile_basename);
		}
		else if (!strcmp(argv[i], "-cdfile")) {
			char isofilename[MAXPATHLEN];

			if (i+1 >= argc){
				printf("cdfile arg present but cd file not specified, exit\n");
				exit(EXIT_FAILURE);
			};
			strncpy(isofilename, argv[++i], MAXPATHLEN);
			if (isofilename[0] != '/') {
				getcwd(path, MAXPATHLEN);
				if (strlen(path) + strlen(isofilename) + 1 < MAXPATHLEN) {
					strcat(path, "/");
					strcat(path, isofilename);
					strcpy(isofilename, path);
				} else
					isofilename[0] = 0;
			}

			cdfile = (char*) malloc(strlen(isofilename)*sizeof(char));
			strcpy(cdfile, isofilename);
			printf("cdfile = %s\n", cdfile);

			/* Check if rom file exists, Save ROM name, and ROM path */
			FILE *f = fopen(cdfile, "rb");
			if (f) {
				/* Save Rom path */
				cdPath = (char *) malloc(strlen(cdfile)*sizeof(char));
				strcpy(cdPath, cdfile);
				char *slash = strrchr ((char*)cdPath, '/');
				*slash = 0;
				printf("cdPath: %s\n", cdPath);

				/* Rom name without extension */
				char *point = strrchr ((char*)slash+1, '.');
				*point = 0;
				char *cdfile_no_ext = slash+1;

				/* Set quicksave filename */
				quick_save_file = (char *) malloc( (strlen(cdfile) +
					strlen(cdfile_no_ext) +
					strlen(quick_save_file_extension) + 2) * sizeof(char) );
				sprintf(quick_save_file, "%s/%s.%s",
					cdPath, cdfile_no_ext, quick_save_file_extension);
				printf("quick_save_file: %s\n", quick_save_file);
				
		        /* Set rom cfg filepath */
		        cfg_file_rom = (char *)malloc(strlen(cdPath) + strlen(slash+1) +
		          strlen(cfg_file_extension) + 2 + 1);
		        sprintf(cfg_file_rom, "%s/%s.%s",
		          cdPath, slash+1, cfg_file_extension);
		        printf("cfg_file_rom: %s\n", cfg_file_rom);

		        /* Set console cfg filepath */
		        cfg_file_default = (char *)malloc(strlen(cdPath) + strlen(cfg_file_default_name) +
		          strlen(cfg_file_extension) + 2 + 1);
		        sprintf(cfg_file_default, "%s/%s.%s",
		          cdPath, cfg_file_default_name, cfg_file_extension);
		        printf("cfg_file_default: %s\n", cfg_file_default);

		        /** Load config files */
		        configfile_load(cfg_file_default);
		        configfile_load(cfg_file_rom);

				fclose(f);
			}
			else{
				SysMessage("cdfile not found!");
				return 1;
			}
		}
		else if (!strcmp(argv[i], "-loadf")) {
			if (i+1 >= argc) break;
			loadst_f = argv[++i];
		}
		else if (!strcmp(argv[i], "-fps")) {
			printf("Show FPS\n");
			g_opts |= OPT_SHOWFPS;
		}
		else if (!strcmp(argv[i], "-frameskip")) {
			printf("Frameskip ON\n");
			//pl_rearmed_cbs.frameskip = -1;
			pl_rearmed_cbs.frameskip = 1;
			plugin_call_rearmed_cbs();
		}
		else if (!strcmp(argv[i], "-h") ||
			 !strcmp(argv[i], "-help") ||
			 !strcmp(argv[i], "--help")) {
			 printf("PCSX-ReARMed " REV "\n");
			 printf("%s\n", _(
							" pcsx [options] [file]\n"
							"\toptions:\n"
							"\t-cdfile FILE\tRuns a CD image file\n"
							"\t-cfg FILE\tLoads desired configuration file (default: ~/.pcsx/pcsx.cfg)\n"
							"\t-psxout\t\tEnable PSX output\n"
							"\t-load STATENUM\tLoads savestate STATENUM (1-5)\n"
							"\t-loadf save_file\tLoads from save file\n"
							"\t-fps\t\tDisplays FPS\n"
							"\t-frameskip\tset frameskip on\n"
							"\t-h -help\tDisplay this message\n"
							"\tfile\t\tLoads a PSX EXE file\n"));
			 return 0;
		} else {
			strncpy(file, argv[i], MAXPATHLEN);
			if (file[0] != '/') {
				getcwd(path, MAXPATHLEN);
				if (strlen(path) + strlen(file) + 1 < MAXPATHLEN) {
					strcat(path, "/");
					strcat(path, file);
					strcpy(file, path);
				} else
					file[0] = 0;
			}
		}
	}

	if (cdfile)
		set_cd_image(cdfile);

    /* Set env var for no mouse */
    putenv(strdup("SDL_NOMOUSE=1"));

	// frontend stuff
	// init input but leave probing to platform code,
	// they add input drivers and may need to modify them after probe
	in_init();
	pl_init();
	plat_init();
	menu_init(); // loads config

	check_bioses();

	if (emu_core_init() != 0)
		return 1;

	if (psxout)
		Config.PsxOut = 1;

	if (LoadPlugins() == -1) {
		// FIXME: this recovery doesn't work, just delete bad config and bail out
		// SysMessage("could not load plugins, retrying with defaults\n");
		set_default_paths();
		snprintf(path, sizeof(path), "." PCSX_DOT_DIR "%s", cfgfile_basename);
		remove(path);
		SysMessage("Failed loading plugins!");
		return 1;
	}
	pcnt_hook_plugins();

	if (OpenPlugins() == -1) {
		return 1;
	}
	plugin_call_rearmed_cbs();

	CheckCdrom();
	SysReset();

	/* Init Signals */
	signal(SIGUSR1, handle_sigusr1);


	if (file[0] != '\0') {
		if (Load(file) != -1)
			ready_to_go = 1;
	} else {
		if (cdfile) {
			if (LoadCdrom() == -1) {
				ClosePlugins();
				SysPrintf(_("Could not load CD-ROM!\n"));
				return -1;
			}
			emu_on_new_cd(!loadst);
			ready_to_go = 1;
		}
	}

	if (loadst_f) {
		int ret = LoadState(loadst_f);
		SysPrintf("%s state file: %s\n",
			ret ? "failed to load" : "loaded", loadst_f);
		ready_to_go |= (ret == 0);
	}

	if (ready_to_go) {
		menu_prepare_emu();

		// If a state has been specified, then load that
		if (!loadst_f && loadst) {
			int ret = emu_load_state(loadst - 1);
			SysPrintf("%s state %d\n",
				ret ? "failed to load" : "loaded", loadst);
		}
		/* Load quick save file */
		else if(!loadst_f && access( quick_save_file, F_OK ) != -1){
			printf("Found quick save file: %s\n", quick_save_file);

			int resume = launch_resume_menu_loop();
			if(resume == RESUME_YES){
				printf("Resume game from quick save file: %s\n", quick_save_file);
				int ret = LoadState(quick_save_file);
				ready_to_go |= ret == 0;
				SysPrintf("%s state file: %s\n",
					ret ? "failed to load" : "loaded", quick_save_file);
			}
			else{
				printf("Reset game\n");

				/* Remove quicksave file if present */
				if (remove(quick_save_file) == 0){
					printf("Deleted successfully: %s\n", quick_save_file);
				}
				else{
					printf("Unable to delete the file: %s\n", quick_save_file);
				}
			}
		}
	}
	/*else{
		/// We deliberately do not show the menu but exit the game if load failed
		menu_loop();
	}*/


	pl_start_watchdog();

	while (!g_emu_want_quit)
	{
		stop = 0;
		emu_action = SACTION_NONE;

		psxCpu->Execute();

		if (emu_action == SACTION_NONE && emu_action_future != SACTION_NONE){
			emu_set_action(emu_action_future);
			emu_action_future = SACTION_NONE;
		}

		if (emu_action != SACTION_NONE){
			do_emu_action();
		}

		// Force Quick save and poweroff
		if(mQuickSaveAndPoweroff){
			quick_save_and_poweroff();

			/*kill_scheduled_shutdown();
			emu_action_future = SACTION_QUICK_SAVE_AND_POWEROFF;*/
			//mQuickSaveAndPoweroff = 0;
		}
	}

	printf("Exit..\n");
	ClosePlugins();
	SysClose();
	menu_finish();
	plat_finish();

	return 0;
}

static void toggle_fast_forward(int force_off)
{
	static int fast_forward;
	static int normal_g_opts;
	static int normal_frameskip;
	static int normal_enhancement_enable;

	if (force_off && !fast_forward)
		return;

	fast_forward = !fast_forward;
	if (fast_forward) {
		normal_g_opts = g_opts;
		normal_frameskip = pl_rearmed_cbs.frameskip;
		normal_enhancement_enable =
			pl_rearmed_cbs.gpu_neon.enhancement_enable;

		g_opts |= OPT_NO_FRAMELIM;
		pl_rearmed_cbs.frameskip = 3;
		pl_rearmed_cbs.gpu_neon.enhancement_enable = 0;
	} else {
		g_opts = normal_g_opts;
		pl_rearmed_cbs.frameskip = normal_frameskip;
		pl_rearmed_cbs.gpu_neon.enhancement_enable =
			normal_enhancement_enable;

		pl_timing_prepare(Config.PsxType);
	}

	if (!force_off)
		snprintf(hud_msg, sizeof(hud_msg), "FAST FORWARD %s",
			fast_forward ? "ON" : "OFF");
}

static void SignalExit(int sig) {
	// only to restore framebuffer/resolution on some devices
	plat_finish();
	exit(1);
}
#endif

void SysRunGui() {
        printf("SysRunGui\n");
}

static void dummy_lace()
{
}

void SysReset() {
	// rearmed hack: EmuReset() runs some code when real BIOS is used,
	// but we usually do reset from menu while GPU is not open yet,
	// so we need to prevent updateLace() call..
	void *real_lace = GPU_updateLace;
	GPU_updateLace = dummy_lace;
	g_emu_resetting = 1;

	// reset can run code, timing must be set
	pl_timing_prepare(Config.PsxType);

	EmuReset();

	// hmh core forgets this
	CDR_stop();

	GPU_updateLace = real_lace;
	g_emu_resetting = 0;
}

void SysClose() {
	EmuShutdown();
	ReleasePlugins();

	StopDebugger();

	if (emuLog != NULL && emuLog != stdout && emuLog != stderr) {
		fclose(emuLog);
		emuLog = NULL;
	}
}

void SysUpdate() {
}

int get_state_filename(char *buf, int size, int i) {
	/*return get_gameid_filename(buf, size,
		"." STATES_DIR "%.32s-%.9s.%3.3d", i);*/

	char *romDataFmt = "%.32s-%.9s.%3.3d";
	char *save_path_formated = (char*)malloc(strlen(romDataFmt)+strlen(cdPath)+2);
	sprintf(save_path_formated, "%s/%s", cdPath, romDataFmt);
	return get_gameid_filename(buf, size, save_path_formated , i);
}

int emu_check_state(int slot)
{
	char fname[MAXPATHLEN];
	int ret;

	ret = get_state_filename(fname, sizeof(fname), slot);
	if (ret != 0)
		return ret;

	return CheckState(fname);
}

int emu_save_state(int slot)
{
	char fname[MAXPATHLEN];
	int ret;

	ret = get_state_filename(fname, sizeof(fname), slot);
	if (ret != 0)
		return ret;

	ret = SaveState(fname);
#ifdef HAVE_PRE_ARMV7 /* XXX GPH hack */
	sync();
#endif
	SysPrintf("* %s \"%s\" [%d]\n",
		ret == 0 ? "saved" : "failed to save", fname, slot);
	return ret;
}

int emu_load_state(int slot)
{
	char fname[MAXPATHLEN];
	int ret;

	hud_msg[0] = 0;

	ret = get_state_filename(fname, sizeof(fname), slot);
	if (ret != 0)
		return ret;

	return LoadState(fname);
}

#ifndef ANDROID

void SysPrintf(const char *fmt, ...) {
	va_list list;

	va_start(list, fmt);
	vfprintf(emuLog, fmt, list);
	va_end(list);
	fflush(emuLog);
}

#else

#include <android/log.h>

void SysPrintf(const char *fmt, ...) {
	va_list list;

	va_start(list, fmt);
	__android_log_vprint(ANDROID_LOG_INFO, "PCSX", fmt, list);
	va_end(list);
}

#endif

void SysMessage(const char *fmt, ...) {
	va_list list;
	char msg[512];
	int ret;

	va_start(list, fmt);
	ret = vsnprintf(msg, sizeof(msg), fmt, list);
	va_end(list);

	if (ret < sizeof(msg) && msg[ret - 1] == '\n')
		msg[ret - 1] = 0;

	SysPrintf("%s\n", msg);
}

#define PARSEPATH(dst, src) \
	ptr = src + strlen(src); \
	while (*ptr != '\\' && ptr != src) ptr--; \
	if (ptr != src) { \
		strcpy(dst, ptr+1); \
	}

static int _OpenPlugins(void) {
	int ret;

#ifndef NO_FRONTEND
	signal(SIGINT, SignalExit);
	signal(SIGPIPE, SignalExit);
#endif

	GPU_clearDynarec(clearDynarec);

	ret = CDR_open();
	if (ret < 0) { SysMessage(_("Error opening CD-ROM plugin!")); return -1; }
	ret = SPU_open();
	if (ret < 0) { SysMessage(_("Error opening SPU plugin!")); return -1; }
	SPU_registerCallback(SPUirq);
	SPU_registerScheduleCb(SPUschedule);
	// pcsx-rearmed: we handle gpu elsewhere
	//ret = GPU_open(&gpuDisp, "PCSX", NULL);
	//if (ret < 0) { SysMessage(_("Error opening GPU plugin!")); return -1; }
	ret = PAD1_open(&gpuDisp);
	if (ret < 0) { SysMessage(_("Error opening Controller 1 plugin!")); return -1; }
	ret = PAD2_open(&gpuDisp);
	if (ret < 0) { SysMessage(_("Error opening Controller 2 plugin!")); return -1; }

	if (Config.UseNet && !NetOpened) {
		netInfo info;
		char path[MAXPATHLEN];
		char dotdir[MAXPATHLEN];

		MAKE_ABSOLUTE_PATH(dotdir, "~/.pcsx/plugins/", NULL);

		strcpy(info.EmuName, "PCSX");
		strncpy(info.CdromID, CdromId, 9);
		strncpy(info.CdromLabel, CdromLabel, 9);
		info.psxMem = psxM;
		info.GPU_showScreenPic = GPU_showScreenPic;
		info.GPU_displayText = GPU_displayText;
		info.GPU_showScreenPic = GPU_showScreenPic;
		info.PAD_setSensitive = PAD1_setSensitive;
		sprintf(path, "%s%s", Config.BiosDir, Config.Bios);
		strcpy(info.BIOSpath, path);
		strcpy(info.MCD1path, Config.Mcd1);
		strcpy(info.MCD2path, Config.Mcd2);
		sprintf(path, "%s%s", dotdir, Config.Gpu);
		strcpy(info.GPUpath, path);
		sprintf(path, "%s%s", dotdir, Config.Spu);
		strcpy(info.SPUpath, path);
		sprintf(path, "%s%s", dotdir, Config.Cdr);
		strcpy(info.CDRpath, path);
		NET_setInfo(&info);

		ret = NET_open(&gpuDisp);
		if (ret < 0) {
			if (ret == -2) {
				// -2 is returned when something in the info
				// changed and needs to be synced
				char *ptr;

				PARSEPATH(Config.Bios, info.BIOSpath);
				PARSEPATH(Config.Gpu,  info.GPUpath);
				PARSEPATH(Config.Spu,  info.SPUpath);
				PARSEPATH(Config.Cdr,  info.CDRpath);

				strcpy(Config.Mcd1, info.MCD1path);
				strcpy(Config.Mcd2, info.MCD2path);
				return -2;
			} else {
				Config.UseNet = FALSE;
			}
		} else {
			if (NET_queryPlayer() == 1) {
				if (SendPcsxInfo() == -1) Config.UseNet = FALSE;
			} else {
				if (RecvPcsxInfo() == -1) Config.UseNet = FALSE;
			}
		}
		NetOpened = TRUE;
	} else if (Config.UseNet) {
		NET_resume();
	}

	return 0;
}

int OpenPlugins() {
	int ret;

	while ((ret = _OpenPlugins()) == -2) {
		ReleasePlugins();
		LoadMcds(Config.Mcd1, Config.Mcd2);
		if (LoadPlugins() == -1) return -1;
	}
	return ret;
}

void ClosePlugins() {
	int ret;

#ifndef NO_FRONTEND
	signal(SIGINT, SIG_DFL);
	signal(SIGPIPE, SIG_DFL);
#endif

	ret = CDR_close();
	if (ret < 0) { SysMessage(_("Error closing CD-ROM plugin!")); return; }
	ret = SPU_close();
	if (ret < 0) { SysMessage(_("Error closing SPU plugin!")); return; }
	ret = PAD1_close();
	if (ret < 0) { SysMessage(_("Error closing Controller 1 Plugin!")); return; }
	ret = PAD2_close();
	if (ret < 0) { SysMessage(_("Error closing Controller 2 plugin!")); return; }
	// pcsx-rearmed: we handle gpu elsewhere
	//ret = GPU_close();
	//if (ret < 0) { SysMessage(_("Error closing GPU plugin!")); return; }

	if (Config.UseNet) {
		NET_pause();
	}
}

/* we hook statically linked plugins here */
static const char *builtin_plugins[] = {
	"builtin_gpu", "builtin_spu", "builtin_cdr", "builtin_pad",
	"builtin_cdrcimg",
};

static const int builtin_plugin_ids[] = {
	PLUGIN_GPU, PLUGIN_SPU, PLUGIN_CDR, PLUGIN_PAD,
	PLUGIN_CDRCIMG,
};

void *SysLoadLibrary(const char *lib) {
	const char *tmp = strrchr(lib, '/');
	void *ret = NULL;
	int i;

	SysPrintf("plugin: %s\n", lib);

	if (tmp != NULL) {
		tmp++;
		for (i = 0; i < ARRAY_SIZE(builtin_plugins); i++)
			if (strcmp(tmp, builtin_plugins[i]) == 0)
				return (void *)(long)(PLUGIN_DL_BASE + builtin_plugin_ids[i]);
	}

#ifndef _WIN32
	ret = dlopen(lib, RTLD_NOW);
	if (ret == NULL)
		SysMessage("dlopen: %s", dlerror());
#else
	/* no external plugin support, abi is no longer
	 * compatible with psemu/pcsx anyway */
#endif
	return ret;
}

void *SysLoadSym(void *lib, const char *sym) {
	unsigned int plugid = (unsigned int)(long)lib;

	if (PLUGIN_DL_BASE <= plugid && plugid < PLUGIN_DL_BASE + ARRAY_SIZE(builtin_plugins))
		return plugin_link(plugid - PLUGIN_DL_BASE, sym);

#ifndef _WIN32
	return dlsym(lib, sym);
#else
	return NULL;
#endif
}

const char *SysLibError() {
#ifndef _WIN32
	return dlerror();
#else
	return "not supported";
#endif
}

void SysCloseLibrary(void *lib) {
	unsigned int plugid = (unsigned int)(long)lib;

	if (PLUGIN_DL_BASE <= plugid && plugid < PLUGIN_DL_BASE + ARRAY_SIZE(builtin_plugins))
		return;

#ifndef _WIN32
	dlclose(lib);
#endif
}

