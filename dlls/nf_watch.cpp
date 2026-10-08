/*
nf_watch.cpp - James Bond 007: Nightfire (PC) laser watch (weapon_watch)

Retail CWatch (game.dll vtable 0x4213544c, docs/retail/lasertarget.md):
weapon id 21, view model v_watch.mdl (no p_ model), ammo "battery" 0..100.
Holding the primary attack: FIRE_START, 0.05 s later the laser is on
(watch_fire.wav, FIRE_HOLD): every frame a trace along the view, a beam
from the hit point to the view model's attachment, and the hit laser
target loses health (dlls/nf_lasertarget.cpp). The battery drains 33/s;
empty or released: FIRE_RETURN, recharge 33/s from 1 s later. The
secondary attack only stops the laser.

Server side only (no client prediction; the view model animations come
from the server). HUD bucket 1 [assumed: retail lists it in the gadget
wheel].
*/

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "weapons.h"
#include "player.h"
#include "effects.h"
#include "nf_weapons.h"
#include "nf_lasertarget.h"
#include "nf_debug.h"

#define WATCH_BEAM_SPRITE	"sprites/laser_beam.spz"
#define WATCH_FIRE_SOUND	"weapons/watch_fire.wav"
#define WATCH_RANGE		256.0f	// [assumed] retail trace length
#define WATCH_CHARGE_MAX	100.0f
#define WATCH_CHARGE_RATE	33.0f	// battery per second, drain and recharge
#define WATCH_LASER_RATE	60.0f	// target health per second ([assumed]: retail 1 per frame)

enum nf_watch_e
{
	WATCH_IDLE1 = 0,
	WATCH_IDLE2,
	WATCH_FIRE_START,
	WATCH_FIRE_HOLD,
	WATCH_FIRE_RETURN,
	WATCH_DRAW,
	WATCH_HOLSTER,
};

enum
{
	WATCH_OFF = 0,
	WATCH_STARTING,
	WATCH_FIRING,
};

class CNightfireWatch : public CBasePlayerWeapon
{
public:
	void Spawn( void );
	void Precache( void );
	int iItemSlot( void ) { return 1; }
	int GetItemInfo( ItemInfo *p );
	int AddToPlayer( CBasePlayer *pPlayer );
	BOOL Deploy( void );
	void Holster( int skiplocal = 0 );
	void PrimaryAttack( void );
	void SecondaryAttack( void ) { WeaponIdle(); }
	void WeaponIdle( void );
	void UpdateOnRemove( void );
	BOOL IsUseable( void ) { return TRUE; }	// selectable with an empty battery
	BOOL CanDeploy( void ) { return TRUE; }	// retail 0x420ed180

	virtual BOOL UseDecrement( void ) { return FALSE; }

	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];

private:
	void Stop( void );
	void UpdateCharge( void );
	void SetBattery( void );

	int m_iFireMode;		// WATCH_*
	float m_flStartTime;		// laser on at
	float m_flCharge;		// battery 0..100
	float m_flRechargeTime;		// recharge starts at
	float m_flLastUpdate;		// last charge / laser update
	CBeam *m_pBeam;
};

TYPEDESCRIPTION CNightfireWatch::m_SaveData[] =
{
	DEFINE_FIELD( CNightfireWatch, m_iFireMode, FIELD_INTEGER ),
	DEFINE_FIELD( CNightfireWatch, m_flCharge, FIELD_FLOAT ),
	DEFINE_FIELD( CNightfireWatch, m_flRechargeTime, FIELD_TIME ),
};

IMPLEMENT_SAVERESTORE( CNightfireWatch, CBasePlayerWeapon )

LINK_ENTITY_TO_CLASS( weapon_watch, CNightfireWatch )

void CNightfireWatch::Spawn( void )
{
	Precache();
	m_iId = NF_WEAPON_WATCH;
	SET_MODEL( ENT( pev ), "models/w_kowloon.mdl" );	// retail: the watch has no world model
	m_iDefaultAmmo = (int)WATCH_CHARGE_MAX;
	m_flCharge = WATCH_CHARGE_MAX;
	FallInit();
}

void CNightfireWatch::Precache( void )
{
	PRECACHE_MODEL( "models/v_watch.mdl" );
	PRECACHE_MODEL( "models/w_kowloon.mdl" );
	PRECACHE_MODEL( WATCH_BEAM_SPRITE );
	PRECACHE_SOUND( WATCH_FIRE_SOUND );
}

int CNightfireWatch::GetItemInfo( ItemInfo *p )
{
	p->pszName = STRING( pev->classname );
	p->pszAmmo1 = "battery";
	p->iMaxAmmo1 = (int)WATCH_CHARGE_MAX;
	p->pszAmmo2 = NULL;
	p->iMaxAmmo2 = -1;
	p->iMaxClip = WEAPON_NOCLIP;
	p->iSlot = 0;		// [assumed] bucket 1 (retail: gadget wheel)
	p->iPosition = 0;
	p->iFlags = 0;
	p->iId = m_iId = NF_WEAPON_WATCH;
	p->iWeight = -1;	// retail: never auto-selected
	return 1;
}

int CNightfireWatch::AddToPlayer( CBasePlayer *pPlayer )
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

BOOL CNightfireWatch::Deploy( void )
{
	m_iFireMode = WATCH_OFF;
	m_flLastUpdate = gpGlobals->time;
	m_flTimeWeaponIdle = gpGlobals->time + 2.0f;
	return DefaultDeploy( "models/v_watch.mdl", "", WATCH_DRAW, "onehanded" );
}

void CNightfireWatch::Holster( int skiplocal )
{
	if( m_iFireMode != WATCH_OFF )
		Stop();
	m_pPlayer->m_flNextAttack = gpGlobals->time + 0.5f;
	SendWeaponAnim( WATCH_HOLSTER );
}

void CNightfireWatch::UpdateOnRemove( void )
{
	if( m_pBeam )
	{
		UTIL_Remove( m_pBeam );
		m_pBeam = NULL;
	}
	CBasePlayerWeapon::UpdateOnRemove();
}

// the battery is the player's "battery" ammo (HUD); m_flCharge keeps the fraction
void CNightfireWatch::SetBattery( void )
{
	if( m_iPrimaryAmmoType >= 0 )
		m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] = (int)( m_flCharge + 0.5f );
}

void CNightfireWatch::Stop( void )
{
	STOP_SOUND( ENT( m_pPlayer->pev ), CHAN_STATIC, WATCH_FIRE_SOUND );
	if( m_pBeam )
	{
		UTIL_Remove( m_pBeam );
		m_pBeam = NULL;
	}
	if( m_iFireMode != WATCH_OFF )
		SendWeaponAnim( WATCH_FIRE_RETURN );
	m_iFireMode = WATCH_OFF;
	m_flRechargeTime = gpGlobals->time + 1.0f;
	m_flTimeWeaponIdle = gpGlobals->time + RANDOM_FLOAT( 12, 20 );
}

void CNightfireWatch::PrimaryAttack( void )
{
	float dt = Q_min( gpGlobals->time - m_flLastUpdate, 0.1f );
	m_flLastUpdate = gpGlobals->time;
	if( dt < 0 )
		dt = 0;

	if( m_iPrimaryAmmoType >= 0 && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] < (int)m_flCharge )
		m_flCharge = m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType];	// e.g. after a save / give
	if( m_flCharge <= 0 )
	{
		if( m_iFireMode != WATCH_OFF )
			Stop();
		return;
	}

	switch( m_iFireMode )
	{
	case WATCH_OFF:
		m_flStartTime = gpGlobals->time + 0.05f;
		m_iFireMode = WATCH_STARTING;
		SendWeaponAnim( WATCH_FIRE_START );
		m_pPlayer->SetAnimation( PLAYER_ATTACK1 );
		return;
	case WATCH_STARTING:
		if( gpGlobals->time < m_flStartTime )
			return;
		m_iFireMode = WATCH_FIRING;
		if( NF_DEBUG( NF_DBG_ITEMS ))
		{
			UTIL_MakeVectors( m_pPlayer->pev->v_angle + m_pPlayer->pev->punchangle );
			Vector vecStart = m_pPlayer->GetGunPosition();
			TraceResult trDbg;
			UTIL_TraceLine( vecStart, vecStart + gpGlobals->v_forward * WATCH_RANGE, dont_ignore_monsters, dont_ignore_glass, m_pPlayer->edict(), &trDbg );
			ALERT( at_console, "nf_debug: watch laser on, battery %.0f, hits %s at %.0f %.0f %.0f\n", m_flCharge,
				trDbg.flFraction < 1.0f && !FNullEnt( trDbg.pHit ) ? STRING( VARS( trDbg.pHit )->classname ) : trDbg.flFraction < 1.0f ? "world" : "nothing",
				trDbg.vecEndPos.x, trDbg.vecEndPos.y, trDbg.vecEndPos.z );
		}
		EMIT_SOUND_DYN( ENT( m_pPlayer->pev ), CHAN_STATIC, WATCH_FIRE_SOUND, 0.98f, 0.8f, 0, 125 );
		SendWeaponAnim( WATCH_FIRE_HOLD );
		break;
	}

	// the laser: trace along the view
	UTIL_MakeVectors( m_pPlayer->pev->v_angle + m_pPlayer->pev->punchangle );
	Vector vecSrc = m_pPlayer->GetGunPosition();
	TraceResult tr;
	UTIL_TraceLine( vecSrc, vecSrc + gpGlobals->v_forward * WATCH_RANGE, dont_ignore_monsters, dont_ignore_glass, m_pPlayer->edict(), &tr );

	if( !m_pBeam )
	{
		m_pBeam = CBeam::BeamCreate( WATCH_BEAM_SPRITE, 7 );
		m_pBeam->PointEntInit( tr.vecEndPos, m_pPlayer->entindex());
		m_pBeam->SetEndAttachment( 1 );
		m_pBeam->pev->spawnflags |= SF_BEAM_TEMPORARY;
		m_pBeam->SetColor( 255, 255, 255 );
		m_pBeam->SetScrollRate( 0 );
		m_pBeam->SetNoise( 0 );
	}
	m_pBeam->SetStartPos( tr.vecEndPos );
	m_pBeam->SetBrightness( RANDOM_LONG( 0, 255 ));
	m_pBeam->SetWidth( (int)( 2.0f * sin( gpGlobals->time ) + 4.0f ));
	m_pBeam->RelinkBeam();

	if( tr.flFraction < 1.0f )
		NF_LaserHit( CBaseEntity::Instance( tr.pHit ), m_pPlayer, WATCH_LASER_RATE * dt );

	m_flCharge = Q_max( m_flCharge - WATCH_CHARGE_RATE * dt, 0.0f );
	SetBattery();
	if( m_flCharge <= 0 )
		Stop();
}

void CNightfireWatch::UpdateCharge( void )
{
	float dt = Q_min( gpGlobals->time - m_flLastUpdate, 0.1f );
	m_flLastUpdate = gpGlobals->time;
	if( dt > 0 && gpGlobals->time >= m_flRechargeTime && m_flCharge < WATCH_CHARGE_MAX )
	{
		m_flCharge = Q_min( m_flCharge + WATCH_CHARGE_RATE * dt, WATCH_CHARGE_MAX );
		SetBattery();
	}
}

void CNightfireWatch::WeaponIdle( void )
{
	if( m_iFireMode != WATCH_OFF )
		Stop();

	UpdateCharge();

	if( m_flTimeWeaponIdle > gpGlobals->time )
		return;
	SendWeaponAnim( RANDOM_LONG( 0, 1 ) ? WATCH_IDLE2 : WATCH_IDLE1 );
	m_flTimeWeaponIdle = gpGlobals->time + RANDOM_FLOAT( 12, 20 );
}
