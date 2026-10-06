/*
nf_gun_info.h - James Bond 007: Nightfire (PC) bullet weapon descriptions

Plain data, shared by the weapon code (dlls/nf_guns.cpp, server and client)
and the client fire event (cl_dll/ev_hldm.cpp, EV_FireNFGun).
*/
#pragma once
#ifndef NF_GUN_INFO_H
#define NF_GUN_INFO_H

typedef struct nf_gun_info_s
{
	int id;			// retail weapon id
	const char *classname;
	const char *vmodel, *pmodel, *wmodel;
	const char *animext;	// player model animation set (retail Deploy)
	const char *ammo;	// ammo type
	int max_ammo;		// retail ammo table
	int max_clip;		// retail GetItemInfo
	int default_give;	// retail Spawn (m_iDefaultAmmo)
	int slot, position;	// HUD bucket (Half-Life layout) [assumed]
	float cycle;		// seconds between rounds (retail fire helper)
	float spread;		// cone (retail FireBulletsPlayer argument)
	float spread_mode1;	// cone in fire mode 1, 0 = same
	int burst;		// rounds per trigger pull in fire mode 0 (0 = automatic)
	const char *damage;	// skill.cfg cvar base (sk_plr_*_bullet)
	int fallback_damage;	// when skill.cfg is missing
	int quiet;		// silenced: quiet volume, dim flash
	const char *fire_sound;	// heard by other players (the view model plays its own)
	float punch;		// view punch (pitch, degrees)

	// view model sequences; -1 = none
	int seq_idle[3];
	float idle_time[3];	// sequence length (frames / fps)
	int seq_fire, seq_draw, seq_holster, seq_reload, seq_reload_empty;
	float reload_time, reload_empty_time;	// retail DefaultReload delays
	int seq_mode0, seq_mode1;	// fire mode switch sequences (to mode 0 / 1)
	float mode_time;
	int mode_body;		// the fire mode is shown as the view model body
} nf_gun_info_t;

extern const nf_gun_info_t g_nfGunMP9;
extern const nf_gun_info_t g_nfGunMP9Silenced;
extern const nf_gun_info_t g_nfGunCommando;
extern const nf_gun_info_t g_nfGunPDW90;

const nf_gun_info_t *NF_GunInfo( int id );

#endif // NF_GUN_INFO_H
