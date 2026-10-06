/*
nf_weapons.h - James Bond 007: Nightfire (PC) player weapons for the Xash3D port

Shared by the server and the client (weapon prediction) like weapons.h.
*/
#pragma once
#ifndef NF_WEAPONS_H
#define NF_WEAPONS_H

// Weapon ids after the Half-Life ones (1-15). The retail game uses its own
// numbering (PP9 = 2); the HL weapons still occupy those ids here.
#define WEAPON_NF_PP9		16

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

#endif // NF_WEAPONS_H
