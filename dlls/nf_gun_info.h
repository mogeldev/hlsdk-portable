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
	const char *damage;	// skill.cfg cvar base (sk_plr_*)
	int fallback_damage;	// when skill.cfg is missing
	int quiet;		// silenced: quiet volume, dim flash
	const char *fire_sound;	// heard by other players (the view model plays its own)
	float punch;		// view punch (pitch, degrees)

	// view model sequences; -1 = none
	int seq_idle[4];
	float idle_time[4];	// sequence length (frames / fps), or the retail idle delay
	int seq_fire, seq_draw, seq_holster, seq_reload, seq_reload_empty;
	float reload_time, reload_empty_time;	// retail DefaultReload delays
	int seq_mode0, seq_mode1;	// fire mode switch sequences (to mode 0 / 1)
	float mode_time;
	int mode_body;		// the fire mode is shown as the view model body

	int seq_fire_last;	// shot that empties the clip (-1 = seq_fire)
	float idle_rand;	// random extra idle delay, 0 .. idle_rand seconds
	int idle_needs_clip;	// no idle animation with an empty clip (retail pistols)
	int volume;		// player weapon volume (AI hearing), 0 = normal / quiet
	int local_sound;	// the view model has no fire sound event: the event plays it
	int pellets;		// rounds per shot, 0 = 1
	float range;		// 0 = 8192
	int bullet;		// Half-Life bullet type for decals and impact sounds
	int seq_fire2;		// secondary fire (Frinesi shoot_big), -1 = none
	int pellets2;
	float spread2;
} nf_gun_info_t;

extern const nf_gun_info_t g_nfGunMP9;
extern const nf_gun_info_t g_nfGunMP9Silenced;
extern const nf_gun_info_t g_nfGunCommando;
extern const nf_gun_info_t g_nfGunPDW90;
extern const nf_gun_info_t g_nfGunKowloon;
extern const nf_gun_info_t g_nfGunRaptor;
extern const nf_gun_info_t g_nfGunFrinesi;
extern const nf_gun_info_t g_nfGunL96;
extern const nf_gun_info_t g_nfGunL96Winter;
extern const nf_gun_info_t g_nfGunMinigun;

const nf_gun_info_t *NF_GunInfo( int id );

#endif // NF_GUN_INFO_H
