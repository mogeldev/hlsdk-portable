/*
nf_lasertarget.h - James Bond 007: Nightfire (PC) laser targets and the use icon (dlls/nf_lasertarget.cpp)
*/
#pragma once
#ifndef NF_LASERTARGET_H
#define NF_LASERTARGET_H

class CBaseEntity;
class CBasePlayer;

// "SetHudIcon" values (client gui/hud/640_use*.png, retail 0x41040bf0)
#define NF_HUDICON_NONE		0
#define NF_HUDICON_USE		1	// 640_use.png
#define NF_HUDICON_LEVELTRANS	2	// 640_use_level_trans.png
#define NF_HUDICON_PDA		3
#define NF_HUDICON_QWORM	4
#define NF_HUDICON_WATCH	5	// 640_use_watch.png

void NF_LaserHit( CBaseEntity *pEntity, CBasePlayer *pPlayer, float flAmount );	// watch laser on an entity
void NF_SetHudIcon( CBasePlayer *pPlayer, int icon );	// send on change
void NF_UseIconThink( CBasePlayer *pPlayer );		// CBasePlayer::PostThink

#endif // NF_LASERTARGET_H
