#include "misc.h"
#include "../plugins/dfsound/spu_config.h"
#include "sio.h"

/* It's duplicated from emu_if.c */
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

static const char MemorycardHack_db[8][10] =
{
	/* Lifeforce Tenka, also known as Codename Tenka */
	{"SLES00613"},
	{"SLED00690"},
	{"SLES00614"},
	{"SLES00615"},
	{"SLES00616"},
	{"SLES00617"},
	{"SCUS94409"}
};

static const char CNTfix_table[25][10] =
{
	/* Vandal Hearts */
	{"SCPS45183"},
	{"SCPS45183"},
	{"SLES00204"},
	{"SLUS00447"},
	/* Vandal Hearts II */
	{"SLES02469"},
	{"SLES02497"},
	{"SLES02496"},
	{"SLUS00940"},
	{"SLPM86251"},
	{"SLPM86007"},
	/* Parasite Eve II */
	{"SLES02561"},
	{"SLES12562"},
	{"SLES02562"},
	{"SLES12560"},
	{"SLES02560"},
	{"SLES12559"},
	{"SLES02559"},
	{"SLES12558"},
	{"SLES02558"},
	{"SLUS01042"},
	{"SLUS01055"},
	{"SCPS45467"},
	{"SLPS02480"},
	{"SLPS91479"},
	{"SLPS02779"},
};

/* Function for automatic patching according to GameID. */
void Apply_Hacks_Cdrom()
{
	uint32_t i;
	
	/* Apply Memory card hack for Codename Tenka. (The game needs one of the memory card slots to be empty) */
	for(i=0;i<ARRAY_SIZE(MemorycardHack_db);i++)
	{
		if (strncmp(CdromId, MemorycardHack_db[i], 9) == 0)
		{
			/* Disable the second memory card slot for the game */
			Config.Mcd2[0] = 0;
			/* This also needs to be done because in sio.c, they don't use Config.Mcd2 for that purpose */
			McdDisable[1] = 1;
		}
	}
	
	/* Apply hackfix for Parasite Eve 2, Vandal Hearts I/II */
	for(i=0;i<ARRAY_SIZE(CNTfix_table);i++)
	{
		if (strncmp(CdromId, CNTfix_table[i], 9) == 0)
		{
			Config.RCntFix = 1;
		}
	}
}
