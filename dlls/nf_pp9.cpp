/*
nf_pp9.cpp - James Bond 007: Nightfire (PC) PP9 pistol (weapon_pp9, ammo_pp9)

Bond's Walther P99 is a reworked Half-Life Glock in the retail game.dll:
the view model v_p99.mdl has the Glock's sequence order (idle1-3, shoot,
shoot_empty, reload, reload_noshot, draw, holster, then the silencer
sequences) and plays its own fire/reload/draw sounds and muzzle flash as
model events (5004, 5001). Values from the retail code (CPP9): clip 16,
16 rounds with the weapon, ammo "9mm" up to 200, ammo_p99 gives 32, fire
cycle 0.35 s, reload 1.8 s (empty) / 1.5 s. The secondary attack screws the
silencer on or off (body 1, own shoot sequences, quiet).

Built into the server and the client (weapon prediction).
*/

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "nodes.h"
#include "player.h"
#include "nf_weapons.h"

enum nf_pp9_e
{
	PP9_IDLE1 = 0,
	PP9_IDLE2,
	PP9_IDLE3,
	PP9_SHOOT,
	PP9_SHOOT_EMPTY,
	PP9_RELOAD,		// empty clip
	PP9_RELOAD_NOSHOT,
	PP9_DRAW,
	PP9_HOLSTER,
	PP9_SILENCER_ON,	// silencer_draw
	PP9_SILENCER_OFF,	// silencer_holster
	PP9_SILENCER_SHOOT,
	PP9_SILENCER_SHOOT_EMPTY,
};

// m_fireState (synced to the client as iuser3) holds the silencer state;
// the view model shows it as body 1 (bodygroup "silencer")
#define PP9_SILENCED	( m_fireState != 0 )

LINK_ENTITY_TO_CLASS( weapon_pp9, CNightfirePP9 )

void CNightfirePP9::Spawn( void )
{
	Precache();
	m_iId = WEAPON_NF_PP9;
	SET_MODEL( ENT( pev ), "models/w_p99.mdl" );

	m_iDefaultAmmo = NF_PP9_DEFAULT_GIVE;

	FallInit();
}

void CNightfirePP9::Precache( void )
{
	PRECACHE_MODEL( "models/v_p99.mdl" );
	PRECACHE_MODEL( "models/w_p99.mdl" );
	PRECACHE_MODEL( "models/p_p99.mdl" );

	m_iShell = PRECACHE_MODEL( "models/shell.mdl" );

	// played by the view model's events, and by the event for other players
	PRECACHE_SOUND( "weapons/p99_fire1.wav" );
	PRECACHE_SOUND( "weapons/p99_fire2.wav" );
	PRECACHE_SOUND( "weapons/p99_fire_sil1.wav" );
	PRECACHE_SOUND( "weapons/p99_fire_sil2.wav" );
	PRECACHE_SOUND( "weapons/p99_reload.wav" );
	PRECACHE_SOUND( "weapons/p99_reload_empty.wav" );
	PRECACHE_SOUND( "weapons/p99_draw.wav" );
	PRECACHE_SOUND( "weapons/p99_draw_sil.wav" );
	PRECACHE_SOUND( "weapons/p99_holster_sil.wav" );
	PRECACHE_SOUND( "items/9mmclip1.wav" );

	m_usFirePP9 = PRECACHE_EVENT( 1, "events/pp9.sc" );
}

int CNightfirePP9::GetItemInfo( ItemInfo *p )
{
	p->pszName = STRING( pev->classname );
	p->pszAmmo1 = "9mm";
	p->iMaxAmmo1 = NF_9MM_MAX_CARRY;
	p->pszAmmo2 = NULL;
	p->iMaxAmmo2 = -1;
	p->iMaxClip = NF_PP9_MAX_CLIP;
	p->iSlot = 1;
	p->iPosition = 1;	// after the HL glock in the pistol slot
	p->iFlags = 0;
	p->iId = m_iId = WEAPON_NF_PP9;
	p->iWeight = 10;	// [assumed] HL glock weight

	return 1;
}

int CNightfirePP9::AddToPlayer( CBasePlayer *pPlayer )
{
	if( CBasePlayerWeapon::AddToPlayer( pPlayer ))
	{
		MESSAGE_BEGIN( MSG_ONE, gmsgWeapPickup, NULL, pPlayer->pev );
			WRITE_BYTE( m_iId );
		MESSAGE_END();
		return TRUE;
	}
	return FALSE;
}

BOOL CNightfirePP9::Deploy( void )
{
	return DefaultDeploy( "models/v_p99.mdl", "models/p_p99.mdl", PP9_DRAW, "onehanded", UseDecrement() ? 1 : 0, PP9_SILENCED ? 1 : 0 );
}

void CNightfirePP9::Holster( int skiplocal )
{
	m_fInReload = FALSE;
	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.5f;
	SendWeaponAnim( PP9_HOLSTER, UseDecrement() ? 1 : 0, PP9_SILENCED ? 1 : 0 );
}

void CNightfirePP9::PrimaryAttack( void )
{
	if( m_iClip <= 0 )
	{
		if( m_fFireOnEmpty )
		{
			PlayEmptySound();
			m_flNextPrimaryAttack = GetNextAttackDelay( 0.2f );
		}
		return;
	}

	// the retail PP9 does not fire under water
	if( m_pPlayer->pev->waterlevel == 3 )
	{
		PlayEmptySound();
		m_flNextPrimaryAttack = GetNextAttackDelay( 0.15f );
		return;
	}

	m_iClip--;

	m_pPlayer->pev->effects = (int)( m_pPlayer->pev->effects ) | EF_MUZZLEFLASH;
	m_pPlayer->SetAnimation( PLAYER_ATTACK1 );

	if( PP9_SILENCED )
	{
		m_pPlayer->m_iWeaponVolume = QUIET_GUN_VOLUME;
		m_pPlayer->m_iWeaponFlash = DIM_GUN_FLASH;
	}
	else
	{
		m_pPlayer->m_iWeaponVolume = NORMAL_GUN_VOLUME;
		m_pPlayer->m_iWeaponFlash = NORMAL_GUN_FLASH;
	}

	int flags;
#if CLIENT_WEAPONS
	flags = FEV_NOTHOST;
#else
	flags = 0;
#endif

	int damage = 0;
#ifndef CLIENT_DLL
	damage = (int)NF_SkillValue( "sk_plr_pp9_bullet" );
	if( damage < 1 )
		damage = 8;	// skill.cfg missing
#endif

	Vector vecSrc = m_pPlayer->GetGunPosition();
	Vector vecAiming = m_pPlayer->GetAutoaimVector( AUTOAIM_10DEGREES );

	// [assumed] the retail call passes spread 0; keep the HL glock's small one
	Vector vecDir = m_pPlayer->FireBulletsPlayer( 1, vecSrc, vecAiming, VECTOR_CONE_1DEGREES, 8192, BULLET_PLAYER_9MM, 0, damage, m_pPlayer->pev, m_pPlayer->random_seed );

	PLAYBACK_EVENT_FULL( flags, m_pPlayer->edict(), m_usFirePP9, 0.0, g_vecZero, g_vecZero, vecDir.x, vecDir.y,
		PP9_SILENCED ? 1 : 0, 0, ( m_iClip == 0 ) ? 1 : 0, 0 );

	m_flNextPrimaryAttack = m_flNextSecondaryAttack = GetNextAttackDelay( 0.35f );
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10, 15 );
}

void CNightfirePP9::SecondaryAttack( void )
{
	// screw the silencer on / off
	m_fireState = !m_fireState;
	SendWeaponAnim( m_fireState ? PP9_SILENCER_ON : PP9_SILENCER_OFF, UseDecrement() ? 1 : 0, 1 );

	m_flNextPrimaryAttack = m_flNextSecondaryAttack = GetNextAttackDelay( 2.0f );
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 10.0f;
}

void CNightfirePP9::Reload( void )
{
	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0 || m_iClip == NF_PP9_MAX_CLIP )
		return;

	int iResult;
	if( m_iClip == 0 )
		iResult = DefaultReload( NF_PP9_MAX_CLIP, PP9_RELOAD, 1.8f, PP9_SILENCED ? 1 : 0 );
	else
		iResult = DefaultReload( NF_PP9_MAX_CLIP, PP9_RELOAD_NOSHOT, 1.5f, PP9_SILENCED ? 1 : 0 );

	if( iResult )
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10, 15 );
}

void CNightfirePP9::WeaponIdle( void )
{
	ResetEmptySound();

	m_pPlayer->GetAutoaimVector( AUTOAIM_10DEGREES );

	if( m_flTimeWeaponIdle > UTIL_WeaponTimeBase())
		return;

	// retail: idle3 below 0.3, idle1 below 0.6, else idle2
	int iAnim;
	float flRand = UTIL_SharedRandomFloat( m_pPlayer->random_seed, 0.0f, 1.0f );
	if( flRand <= 0.3f )
	{
		iAnim = PP9_IDLE3;
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 3.0625f;
	}
	else if( flRand <= 0.6f )
	{
		iAnim = PP9_IDLE1;
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 3.75f;
	}
	else
	{
		iAnim = PP9_IDLE2;
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 2.5f;
	}
	SendWeaponAnim( iAnim, UseDecrement() ? 1 : 0, PP9_SILENCED ? 1 : 0 );
}

#ifndef CLIENT_DLL
class CNightfirePP9Ammo : public CBasePlayerAmmo
{
	void Spawn( void )
	{
		Precache();
		SET_MODEL( ENT( pev ), "models/w_ammo_p99.mdl" );
		CBasePlayerAmmo::Spawn();
	}

	void Precache( void )
	{
		PRECACHE_MODEL( "models/w_ammo_p99.mdl" );
		PRECACHE_SOUND( "items/9mmclip1.wav" );
	}

	BOOL AddAmmo( CBaseEntity *pOther )
	{
		if( pOther->GiveAmmo( NF_AMMO_PP9_GIVE, "9mm", NF_9MM_MAX_CARRY ) != -1 )
		{
			EMIT_SOUND( ENT( pev ), CHAN_ITEM, "items/9mmclip1.wav", 1, ATTN_NORM );
			return TRUE;
		}
		return FALSE;
	}
};

// Nightfire maps use both names (ammo_pp9 on m5, ammo_p99 on m7 and in
// the retail ammo table)
LINK_ENTITY_TO_CLASS( ammo_pp9, CNightfirePP9Ammo )
LINK_ENTITY_TO_CLASS( ammo_p99, CNightfirePP9Ammo )
#endif
