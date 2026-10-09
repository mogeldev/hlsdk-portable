/* Nightfire CTaser, server-side like the watch/PDA. Retail: docs/retail/taser.md. */
#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "weapons.h"
#include "player.h"
#include "effects.h"
#include "nf_weapons.h"
#include "nf_taser.h"
#include "nf_debug.h"

#define TASER_SHOCK_SOUND "gadgets/taser_shocking.wav"
#define TASER_BEAM_SPRITE "sprites/lgtning.spz"

extern void RadiusDamage( Vector src, entvars_t *inflictor, entvars_t *attacker,
	float damage, float radius, int ignoreClass, int damageType );

enum
{
	TASER_IDLE1 = 0, TASER_IDLE2, TASER_FIRE_START, TASER_FIRE_HOLD,
	TASER_FIRE_RETURN, TASER_DRAW, TASER_HOLSTER,
};
enum { TASER_OFF = 0, TASER_STARTING, TASER_FIRING };

class CNightfireTaser : public CBasePlayerWeapon
{
public:
	void Spawn( void );
	void Precache( void );
	int iItemSlot( void ) { return 1; }
	int GetItemInfo( ItemInfo *p );
	int AddToPlayer( CBasePlayer *player );
	BOOL Deploy( void );
	void Holster( int skiplocal = 0 );
	void PrimaryAttack( void );
	void SecondaryAttack( void ) { WeaponIdle(); }
	void ItemPostFrame( void );
	void WeaponIdle( void );
	void UpdateOnRemove( void );
	BOOL IsUseable( void ) { return TRUE; }
	BOOL CanDeploy( void ) { return TRUE; }
	BOOL UseDecrement( void ) { return FALSE; }
	int Save( CSave &save );
	int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];

private:
	BOOL Acquire( void );
	void Stop( BOOL animate );
	void SetBattery( void );
	void UpdateFire( void );
	void CreateBeams( void );

	int m_iFireMode;
	EHANDLE m_hTarget;
	EHANDLE m_hBeams[2];
	float m_flCharge;
	float m_flChargeTime;
	float m_flStartFireTime;
	float m_flNextDamageTime;
	float m_flNextLightTime;
	unsigned short m_usLight;
};

TYPEDESCRIPTION CNightfireTaser::m_SaveData[] =
{
	DEFINE_FIELD( CNightfireTaser, m_iFireMode, FIELD_INTEGER ),
	DEFINE_FIELD( CNightfireTaser, m_hTarget, FIELD_EHANDLE ),
	DEFINE_ARRAY( CNightfireTaser, m_hBeams, FIELD_EHANDLE, 2 ),
	DEFINE_FIELD( CNightfireTaser, m_flCharge, FIELD_FLOAT ),
	DEFINE_FIELD( CNightfireTaser, m_flChargeTime, FIELD_TIME ),
	DEFINE_FIELD( CNightfireTaser, m_flStartFireTime, FIELD_TIME ),
	DEFINE_FIELD( CNightfireTaser, m_flNextDamageTime, FIELD_TIME ),
	DEFINE_FIELD( CNightfireTaser, m_flNextLightTime, FIELD_TIME ),
};

IMPLEMENT_SAVERESTORE( CNightfireTaser, CBasePlayerWeapon )
LINK_ENTITY_TO_CLASS( weapon_taser, CNightfireTaser )

void CNightfireTaser::Spawn( void )
{
	Precache();
	m_iId = NF_WEAPON_TASER;
	SET_MODEL( ENT( pev ), "models/w_kowloon.mdl" );
	m_iDefaultAmmo = (int)NF_TASER_CHARGE_MAX;
	m_iClip = WEAPON_NOCLIP;
	m_flCharge = NF_TASER_CHARGE_MAX;
	m_flChargeTime = gpGlobals->time;
	FallInit();
}

void CNightfireTaser::Precache( void )
{
	PRECACHE_MODEL( "models/v_taser.mdl" );
	PRECACHE_MODEL( "models/w_kowloon.mdl" );
	PRECACHE_MODEL( TASER_BEAM_SPRITE );
	PRECACHE_SOUND( "gadgets/taser_fire.wav" );
	PRECACHE_SOUND( TASER_SHOCK_SOUND );
	PRECACHE_SOUND( "misc/electrocution.wav" );
	m_usLight = PRECACHE_EVENT( 1, "events/taser.sc" );
}

int CNightfireTaser::GetItemInfo( ItemInfo *p )
{
	p->pszName = STRING( pev->classname );
	p->pszAmmo1 = "battery";
	p->iMaxAmmo1 = (int)NF_TASER_CHARGE_MAX;
	p->pszAmmo2 = NULL;
	p->iMaxAmmo2 = -1;
	p->iMaxClip = WEAPON_NOCLIP;
	p->iSlot = 0;
	p->iPosition = 3;
	p->iFlags = 0;
	p->iId = m_iId = NF_WEAPON_TASER;
	p->iWeight = -1;
	return 1;
}

int CNightfireTaser::AddToPlayer( CBasePlayer *player )
{
	if( !CBasePlayerWeapon::AddToPlayer( player ))
		return FALSE;
	MESSAGE_BEGIN( MSG_ONE, gmsgWeapPickup, NULL, player->pev );
		WRITE_BYTE( m_iId );
	MESSAGE_END();
	return TRUE;
}

BOOL CNightfireTaser::Deploy( void )
{
	Stop( FALSE );
	m_flChargeTime = gpGlobals->time;
	m_flTimeWeaponIdle = gpGlobals->time + 2.0f;
	SetBattery();
	return DefaultDeploy( "models/v_taser.mdl", "", TASER_DRAW, "onehanded" );
}

void CNightfireTaser::Holster( int skiplocal )
{
	Stop( FALSE );
	CBasePlayerWeapon::Holster( skiplocal );
}

void CNightfireTaser::UpdateOnRemove( void )
{
	Stop( FALSE );
	CBasePlayerWeapon::UpdateOnRemove();
}

void CNightfireTaser::SetBattery( void )
{
	if( m_pPlayer && m_iPrimaryAmmoType > 0 )
		m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] = (int)m_flCharge;
}

void CNightfireTaser::Stop( BOOL animate )
{
	CBaseEntity *target = m_hTarget;
	if( target )
		NF_TaserRelease( target, this );
	m_hTarget = NULL;
	for( int i = 0; i < 2; i++ )
	{
		CBaseEntity *beam = m_hBeams[i];
		if( beam )
			UTIL_Remove( beam );
		m_hBeams[i] = NULL;
	}
	if( m_pPlayer && m_iFireMode != TASER_OFF )
	{
		STOP_SOUND( m_pPlayer->edict(), CHAN_STATIC, TASER_SHOCK_SOUND );
		if( animate )
			SendWeaponAnim( TASER_FIRE_RETURN );
	}
	if( m_iFireMode != TASER_OFF )
	{
		m_flNextPrimaryAttack = gpGlobals->time + 1.0f;
		m_flTimeWeaponIdle = gpGlobals->time + 0.5f;
		if( NF_DEBUG( NF_DBG_WEAPONS ))
			ALERT( at_console, "nf_debug: taser stopped, charge %.1f\n", m_flCharge );
	}
	m_iFireMode = TASER_OFF;
	m_flChargeTime = gpGlobals->time;
}

BOOL CNightfireTaser::Acquire( void )
{
	UTIL_MakeVectors( m_pPlayer->pev->v_angle + m_pPlayer->pev->punchangle );
	Vector src = m_pPlayer->GetGunPosition();
	TraceResult tr;
	UTIL_TraceLine( src, src + gpGlobals->v_forward * NF_TASER_RANGE,
		dont_ignore_monsters, dont_ignore_glass, m_pPlayer->edict(), &tr );
	CBaseEntity *target = tr.flFraction < 1 ? CBaseEntity::Instance( tr.pHit ) : NULL;
	if( !target || !NF_TaserAcquire( target, this ))
		return FALSE;
	m_hTarget = target;
	return TRUE;
}

void CNightfireTaser::PrimaryAttack( void )
{
	if( m_iFireMode != TASER_OFF )
		return;
	if( m_pPlayer->pev->waterlevel == 3 )
	{
		float charge = (float)(int)m_flCharge;
		m_flCharge = 0;
		SetBattery();
		m_flChargeTime = gpGlobals->time;
		m_flNextPrimaryAttack = gpGlobals->time + 0.15f;
		if( charge > 0 )
			::RadiusDamage( m_pPlayer->pev->origin, m_pPlayer->pev, m_pPlayer->pev,
				charge * 2, charge * 3, CLASS_NONE, DMG_SHOCK | DMG_ALWAYSGIB );
		return;
	}
	if( m_flCharge <= 0 )
	{
		m_flNextPrimaryAttack = gpGlobals->time + 0.15f;
		return;
	}
	m_iFireMode = TASER_STARTING;
	m_flStartFireTime = gpGlobals->time + 0.05f;
	m_flNextDamageTime = gpGlobals->time;
	m_flChargeTime = gpGlobals->time;
	BOOL hit = Acquire();
	SendWeaponAnim( TASER_FIRE_START );
	m_pPlayer->SetAnimation( PLAYER_ATTACK1 );
	EMIT_SOUND( m_pPlayer->edict(), CHAN_WEAPON, "gadgets/taser_fire.wav", 1, ATTN_NORM );
	if( NF_DEBUG( NF_DBG_WEAPONS ))
		ALERT( at_console, "nf_debug: taser started, target %s, charge %.1f\n",
			hit ? STRING( ((CBaseEntity *)m_hTarget)->pev->targetname ) : "none", m_flCharge );
}

void CNightfireTaser::CreateBeams( void )
{
	for( int i = 0; i < 2; i++ )
	{
		if( m_hBeams[i] != 0 )
			continue;
		CBeam *beam = CBeam::BeamCreate( TASER_BEAM_SPRITE, 2 );
		if( !beam )
			continue;
		beam->EntsInit( m_pPlayer->entindex(), m_pPlayer->entindex() );
		beam->SetStartAttachment( 1 );
		beam->SetEndAttachment( 2 );
		// Negative entity indices select the local viewmodel's attachments.
		beam->pev->sequence = -beam->pev->sequence;
		beam->pev->skin = -beam->pev->skin;
		beam->SetColor( 150, 150, 255 );
		beam->SetBrightness( 255 );
		beam->SetNoise( 10 );
		beam->SetScrollRate( 0 );
		beam->pev->frame = 35;
		beam->pev->spawnflags |= SF_BEAM_TEMPORARY;
		UTIL_SetOrigin( beam->pev, m_pPlayer->pev->origin );
		UTIL_SetSize( beam->pev, Vector( -64, -64, -64 ), Vector( 64, 64, 64 ));
		m_hBeams[i] = beam;
	}
}

void CNightfireTaser::UpdateFire( void )
{
	if( !( m_pPlayer->pev->button & IN_ATTACK ) || ( m_pPlayer->pev->button & IN_ATTACK2 ) ||
		!m_pPlayer->IsAlive() || m_pPlayer->pev->waterlevel == 3 )
	{
		Stop( TRUE );
		return;
	}
	CBaseEntity *target = m_hTarget;
	if( !target || !NF_TaserHeld( target, this ) || !target->IsAlive() ||
		( target->pev->origin - m_pPlayer->pev->origin ).Length() > NF_TASER_HOLD_RANGE || !m_pPlayer->FVisible( target ))
	{
		Stop( TRUE );
		return;
	}
	if( gpGlobals->time < m_flStartFireTime )
		return;
	if( m_iFireMode == TASER_STARTING )
	{
		m_iFireMode = TASER_FIRING;
		m_flChargeTime = gpGlobals->time;
		SendWeaponAnim( TASER_FIRE_HOLD );
		m_flNextLightTime = gpGlobals->time;
		EMIT_SOUND_DYN( m_pPlayer->edict(), CHAN_STATIC, TASER_SHOCK_SOUND, 0.98f, 0.8f, 0, 125 );
	}
	CreateBeams();
	if( gpGlobals->time >= m_flNextLightTime )
	{
		float life = RANDOM_FLOAT( 0.01f, 0.15f );
		PLAYBACK_EVENT_FULL( 0, m_pPlayer->edict(), m_usLight, 0, g_vecZero, g_vecZero,
			life, 0, 0, 0, 0, 0 );
		m_flNextLightTime = gpGlobals->time + RANDOM_FLOAT( life, life + 0.05f );
	}
	for( int i = 0; i < 2; i++ )
	{
		CBaseEntity *beam = m_hBeams[i];
		if( beam )
			UTIL_SetOrigin( beam->pev, m_pPlayer->pev->origin );
	}
	m_flCharge = NF_TaserCharge( m_flCharge, gpGlobals->time - m_flChargeTime, true );
	m_flChargeTime = gpGlobals->time;
	SetBattery();
	if( gpGlobals->time >= m_flNextDamageTime && m_flCharge > 0 )
	{
		m_flNextDamageTime = gpGlobals->time + NF_TASER_DAMAGE_INTERVAL;
		target->TakeDamage( pev, m_pPlayer->pev, NF_SkillValue( "sk_plr_taser" ), DMG_SHOCK );
	}
	if( m_flCharge <= 0 || !target->IsAlive() || !NF_TaserHeld( target, this ))
		Stop( TRUE );
}

void CNightfireTaser::ItemPostFrame( void )
{
	if( m_iFireMode != TASER_OFF )
	{
		UpdateFire();
		return;
	}
	if( !( m_pPlayer->pev->button & IN_ATTACK ))
		m_flCharge = NF_TaserCharge( m_flCharge, gpGlobals->time - m_flChargeTime, false );
	m_flChargeTime = gpGlobals->time;
	SetBattery();
	CBasePlayerWeapon::ItemPostFrame();
}

void CNightfireTaser::WeaponIdle( void )
{
	if( m_iFireMode != TASER_OFF )
	{
		Stop( TRUE );
		return;
	}
	if( gpGlobals->time < m_flTimeWeaponIdle )
		return;
	SendWeaponAnim( RANDOM_LONG( 0, 1 ) ? TASER_IDLE2 : TASER_IDLE1 );
	m_flTimeWeaponIdle = gpGlobals->time + RANDOM_FLOAT( 12, 20 );
}
