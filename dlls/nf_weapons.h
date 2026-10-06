/*
nf_weapons.h - James Bond 007: Nightfire (PC) player weapons for the Xash3D port

Shared by the server and the client (weapon prediction) like weapons.h.
Values from the retail game.dll: docs/retail-game-logic.md in the project
repo (mogeldev/nightfire-xash3d).
*/
#pragma once
#ifndef NF_WEAPONS_H
#define NF_WEAPONS_H

#include "nf_gun_info.h"

// The retail weapon ids (m_iId in each Spawn / GetItemInfo). They overlap the
// Half-Life ids, so W_Precache no longer registers the Half-Life weapons.
#define NF_WEAPON_PP9		2
#define NF_WEAPON_KOWLOON	3
#define NF_WEAPON_RAPTOR	4
#define NF_WEAPON_MP9		5
#define NF_WEAPON_MP9_SILENCED	6
#define NF_WEAPON_COMMANDO	7	// SIG552
#define NF_WEAPON_PDW90		8	// P90
#define NF_WEAPON_MINIGUN	9
#define NF_WEAPON_FRINESI	10
#define NF_WEAPON_L96A1		12
#define NF_WEAPON_L96A1_WINTER	13
#define WEAPON_NF_PP9		NF_WEAPON_PP9

#define NF_PP9_MAX_CLIP		16	// retail GetItemInfo
#define NF_PP9_DEFAULT_GIVE	16	// retail Spawn (m_iDefaultAmmo)
#define NF_9MM_MAX_CARRY	200	// retail ammo table: ammo_p99/mp9/kowloon -> "9mm", 200
#define NF_AMMO_PP9_GIVE	32	// retail ammo_p99 AddAmmo

#ifndef CLIENT_DLL
float NF_SkillValue( const char *base );	// dlls/nf_enemy.cpp
#endif

class CNightfirePP9 : public CBasePlayerWeapon
{
public:
	void Spawn( void );
	void Precache( void );
	int iItemSlot( void ) { return 2; }
	int GetItemInfo( ItemInfo *p );
	int AddToPlayer( CBasePlayer *pPlayer );

	void PrimaryAttack( void );
	void SecondaryAttack( void );
	BOOL Deploy( void );
	void Holster( int skiplocal = 0 );
	void Reload( void );
	void WeaponIdle( void );

	virtual BOOL UseDecrement( void )
	{
#if CLIENT_WEAPONS
		return TRUE;
#else
		return FALSE;
#endif
	}

private:
	int m_iShell;
	unsigned short m_usFirePP9;
};

// Table-driven bullet weapon (dlls/nf_guns.cpp): MP9, SIG552, P90, ...
class CNightfireGun : public CBasePlayerWeapon
{
public:
	virtual const nf_gun_info_t *Info( void ) = 0;

	void Spawn( void );
	void Precache( void );
	int iItemSlot( void ) { return Info()->slot + 1; }
	int GetItemInfo( ItemInfo *p );
	int AddToPlayer( CBasePlayer *pPlayer );

	void PrimaryAttack( void );
	void SecondaryAttack( void );
	BOOL Deploy( void );
	void Holster( int skiplocal = 0 );
	void Reload( void );
	void WeaponIdle( void );

	virtual BOOL UseDecrement( void )
	{
#if CLIENT_WEAPONS
		return TRUE;
#else
		return FALSE;
#endif
	}

protected:
	// one shot: clip, volume, bullets (pellets), fire event with the sequence
	void FireRound( int pellets, float spread, int seq );

	// m_fireState (synced as iuser3): fire mode of weapons that have one
	// (SIG552: 0 = 3-round burst, 1 = automatic); m_fInAttack (iuser2):
	// rounds fired since the trigger was pressed (burst / semi-automatic)
	int Body( void ) { return Info()->mode_body ? m_fireState : 0; }

	unsigned short m_usFire;
};

#define NF_DECLARE_GUN( cls, info ) \
class cls : public CNightfireGun \
{ \
public: \
	const nf_gun_info_t *Info( void ) { return &info; } \
};

NF_DECLARE_GUN( CNightfireMP9, g_nfGunMP9 )
NF_DECLARE_GUN( CNightfireMP9Silenced, g_nfGunMP9Silenced )
NF_DECLARE_GUN( CNightfireCommando, g_nfGunCommando )
NF_DECLARE_GUN( CNightfirePDW90, g_nfGunPDW90 )
NF_DECLARE_GUN( CNightfireKowloon, g_nfGunKowloon )
NF_DECLARE_GUN( CNightfireRaptor, g_nfGunRaptor )

// Frinesi: Half-Life shotgun style shell-by-shell reload; secondary = big shot
class CNightfireFrinesi : public CNightfireGun
{
public:
	const nf_gun_info_t *Info( void ) { return &g_nfGunFrinesi; }
	void PrimaryAttack( void ) { Fire( FALSE ); }
	void SecondaryAttack( void ) { Fire( TRUE ); }
	void Reload( void );
	void WeaponIdle( void );

private:
	void Fire( BOOL big );
};

// L96A1 sniper rifle: the secondary attack steps the zoom (m_fireState:
// 0 off, 1 = FOV 40, 2 = FOV 10); a reload drops the zoom until it is done
class CNightfireSniper : public CNightfireGun
{
public:
	void SecondaryAttack( void );
	void Holster( int skiplocal = 0 );
	void Reload( void );
	void ItemPostFrame( void );

private:
	int ZoomFOV( void ) { return m_fireState == 1 ? 40 : m_fireState == 2 ? 10 : 0; }
	void SetFOV( int fov );
};

class CNightfireL96 : public CNightfireSniper
{
public:
	const nf_gun_info_t *Info( void ) { return &g_nfGunL96; }
};

class CNightfireL96Winter : public CNightfireSniper
{
public:
	const nf_gun_info_t *Info( void ) { return &g_nfGunL96Winter; }
};

// minigun: spins up before it fires and down after the trigger is released;
// m_fInAttack (iuser2) holds the spin state, pev->fuser1 (counted down like
// the attack times) the time left in spin-up / spin-down
class CNightfireMinigun : public CNightfireGun
{
public:
	const nf_gun_info_t *Info( void ) { return &g_nfGunMinigun; }
	BOOL Deploy( void );
	void Holster( int skiplocal = 0 );
	void PrimaryAttack( void );
	void SecondaryAttack( void ) { m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + 0.5f; }
	void Reload( void );
	void WeaponIdle( void );
};

#endif // NF_WEAPONS_H
