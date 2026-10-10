/*
nf_itemmeta.h - James Bond 007: Nightfire (PC) selection metadata

Shared server/client table; retail findings in the project documentation
at docs/retail/weapon-selection.md. No entity registration here.
*/
#pragma once
#ifndef NF_ITEMMETA_H
#define NF_ITEMMETA_H

#include <string.h>

#define NF_WHEEL_NONE 0
#define NF_WHEEL_WEAPONS 1
#define NF_WHEEL_GADGETS 3
#define NF_FIREARM_LIMIT 4

struct nf_itemmeta_t
{
	int id;
	const char *classname;
	const char *icon;
	int wheel;
	int selectable;
	int firearm;
};

inline const nf_itemmeta_t *NF_ItemMeta( int id, const char *classname )
{
	static const nf_itemmeta_t items[] =
	{
		{ 1, "weapon_dukes", "dukes", 1, 1, 0 },
		{ 2, "weapon_pp9", "pp9", 1, 1, 1 },
		{ 3, "weapon_kowloon", "kowloon", 1, 1, 1 },
		{ 4, "weapon_raptor", "raptor", 1, 1, 1 },
		{ 5, "weapon_mp9", "mp9", 1, 1, 1 },
		{ 6, "weapon_mp9_silenced", "mp9_silenced", 1, 1, 1 },
		{ 7, "weapon_commando", "commando", 1, 1, 1 },
		{ 8, "weapon_pdw90", "pdw90", 1, 1, 1 },
		{ 9, "weapon_minigun", "mini", 1, 1, 1 },
		{ 10, "weapon_frinesi", "frinesi", 1, 1, 1 },
		{ 11, "weapon_up11", "up11", 1, 1, 1 },
		{ 12, "weapon_l96a1", "l96a1", 1, 1, 1 },
		{ 13, "weapon_l96a1_winter", "l96a1_winter", 1, 1, 1 },
		{ 14, "weapon_smokegrenade", "smokegrenade", 1, 1, 0 },
		{ 15, "weapon_flashgrenade", "flashgrenade", 1, 1, 0 },
		{ 16, "weapon_fraggrenade", "fraggrenade", 1, 1, 0 },
		{ 17, "weapon_bondmine", "bondmine", 1, 1, 0 },
		{ 18, "weapon_ronin", "ronin", 1, 1, 0 },
		{ 19, "weapon_grenadelauncher", "grenadelauncher", 1, 1, 1 },
		{ 20, "weapon_rocketlauncher", "rocketlauncher", 1, 1, 1 },
		{ 21, "weapon_watch", "watch", 3, 1, 0 },
		{ 22, "weapon_taser", "taser", 3, 1, 0 },
		{ 23, "weapon_pen", "pen", 3, 1, 0 },
		{ 24, "weapon_pda", "pda", 3, 1, 0 },
		{ 25, "weapon_lighter", "lighter", 3, 1, 0 },
		{ 26, "weapon_grapple", "grapple", 3, 1, 0 },
		{ 27, "weapon_qworm", "qworm", 3, 1, 0 },
		{ 28, "gadget_nightvision", NULL, 0, 0, 0 },
		{ 29, "weapon_laserrifle", "laser", 1, 1, 0 }
	};
	for( unsigned int i = 0; i < sizeof( items ) / sizeof( items[0] ); i++ )
	{
		if( items[i].id == id && classname && !strcmp( items[i].classname, classname ) )
			return &items[i];
	}
	return NULL;
}

#endif // NF_ITEMMETA_H
