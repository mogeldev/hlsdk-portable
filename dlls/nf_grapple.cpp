/*
nf_grapple.cpp - James Bond 007: Nightfire (PC) cellphone grapple (weapon_grapple)

Retail CGrapple (game.dll vtable 0x4212FDB4) and CGrappleTip ("grapple_tip",
vtable 0x4212FC8C), docs/retail/grapple.md. Weapon id 26, no ammo.
Holding the primary attack fires a tip (2000 units/s along the view) with a
wire beam back to the gun. The tip stops at whatever it touches: an
item_grappletarget (IsGrappleTarget) holds it, then every frame the player
is pulled towards the tip at 600 units/s; anything else is a miss (1 damage
to monsters, 5 to other damageable entities) and the tip is reeled in.
Letting go of the trigger, the secondary attack or 5 s on the wire (with
the trigger still held) reel it in. The fire / draw / holster sounds are
events of v_grapple.mdl.

Server side only (no client prediction, like the watch). HUD bucket 1
[assumed: retail lists it in the gadget wheel].
*/

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "weapons.h"
#include "player.h"
#include "effects.h"
#include "customentity.h"
#include "nf_weapons.h"
#include "nf_debug.h"

#define GRAPPLE_TIP_MODEL	"models/grapple_tip.mdl"
#define GRAPPLE_WIRE_SPRITE	"sprites/grapplewire.spz"
#define GRAPPLE_HIT_SOUND	"gadgets/grapple_hit.wav"
#define GRAPPLE_TIP_SPEED	2000.0f	// retail CreateTip 0x420DA190
#define GRAPPLE_PULL_SPEED	600.0f	// retail Maintenance 0x420D9C30
#define GRAPPLE_HANG_TIME	5.0f	// retail PrimaryAttack 0x420DA410

enum nf_grapple_e
{
	GRAPPLE_IDLE1 = 0,
	GRAPPLE_FIRE_HOLD,
	GRAPPLE_FIRE_RETURN,
	GRAPPLE_PULL,
	GRAPPLE_DRAW,
	GRAPPLE_HOLSTER,
	GRAPPLE_FIRE_START,
};

enum
{
	TIP_FLYING = 0,
	TIP_ATTACHED,
	TIP_MISSED,
};

enum
{
	GRAPPLE_READY = 0,
	GRAPPLE_FIRED = 2,	// retail +0xD4
};

//=========================================================
// grapple_tip
//=========================================================
class CNightfireGrappleTip : public CBaseEntity
{
public:
	void Spawn( void );
	void Precache( void );
	void EXPORT AttachTouch( CBaseEntity *pOther );

	static CNightfireGrappleTip *Create( CBasePlayer *pPlayer );

	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];

	int m_iState;		// TIP_*
	float m_flAttachTime;
};

TYPEDESCRIPTION CNightfireGrappleTip::m_SaveData[] =
{
	DEFINE_FIELD( CNightfireGrappleTip, m_iState, FIELD_INTEGER ),
	DEFINE_FIELD( CNightfireGrappleTip, m_flAttachTime, FIELD_TIME ),
};

IMPLEMENT_SAVERESTORE( CNightfireGrappleTip, CBaseEntity )

LINK_ENTITY_TO_CLASS( grapple_tip, CNightfireGrappleTip )

void CNightfireGrappleTip::Precache( void )
{
	PRECACHE_MODEL( GRAPPLE_TIP_MODEL );
}

// retail 0x420DA0F0
void CNightfireGrappleTip::Spawn( void )
{
	Precache();
	pev->movetype = MOVETYPE_FLY;
	pev->solid = SOLID_BBOX;
	SET_MODEL( ENT( pev ), GRAPPLE_TIP_MODEL );
	UTIL_SetSize( pev, g_vecZero, g_vecZero );
	UTIL_SetOrigin( pev, pev->origin );
	pev->classname = MAKE_STRING( "grapple_tip" );
	m_iState = TIP_FLYING;
	SetTouch( &CNightfireGrappleTip::AttachTouch );
}

// retail 0x420DA190: from the gun position along the view
CNightfireGrappleTip *CNightfireGrappleTip::Create( CBasePlayer *pPlayer )
{
	CNightfireGrappleTip *pTip = GetClassPtr( (CNightfireGrappleTip *)NULL );
	pTip->Spawn();

	UTIL_MakeVectors( pPlayer->pev->v_angle + pPlayer->pev->punchangle );
	pTip->pev->origin = pPlayer->GetGunPosition();
	pTip->pev->velocity = gpGlobals->v_forward * GRAPPLE_TIP_SPEED;
	pTip->pev->owner = pPlayer->edict();
	pTip->pev->angles = UTIL_VecToAngles( pTip->pev->velocity );
	UTIL_SetOrigin( pTip->pev, pTip->pev->origin );
	return pTip;
}

// retail 0x420D9DA0 (+ the patch at 0x42101E97)
void CNightfireGrappleTip::AttachTouch( CBaseEntity *pOther )
{
	m_iState = TIP_MISSED;

	if( pOther->IsGrappleTarget())
	{
		pev->velocity = g_vecZero;
		m_iState = TIP_ATTACHED;
		m_flAttachTime = gpGlobals->time;
		SetTouch( NULL );	// the tip rests inside the target's box: one attach, not one per frame
		// [assumed] the retail server plays no sound here (only on a miss)
		EMIT_SOUND_DYN( ENT( pev ), CHAN_STATIC, GRAPPLE_HIT_SOUND, 1.0f, 0.8f, 0, 125 );
		if( NF_DEBUG( NF_DBG_ITEMS ))
			ALERT( at_console, "nf_debug: grapple tip attached to %s at %.0f %.0f %.0f\n", STRING( pOther->pev->classname ),
				(double)pev->origin.x, (double)pev->origin.y, (double)pev->origin.z );
		return;
	}

	Vector vecDir = pev->velocity.Normalize();
	pev->velocity = g_vecZero;

	edict_t *pOwner = pev->owner;
	EMIT_SOUND_DYN( pOwner ? pOwner : ENT( pev ), CHAN_STATIC, GRAPPLE_HIT_SOUND, 1.0f, 0.8f, 0, 125 );

	if( NF_DEBUG( NF_DBG_ITEMS ))
		ALERT( at_console, "nf_debug: grapple tip missed (%s) at %.0f %.0f %.0f\n", STRING( pOther->pev->classname ),
			(double)pev->origin.x, (double)pev->origin.y, (double)pev->origin.z );

	if( pOther->pev->takedamage == DAMAGE_NO )
		return;

	TraceResult tr;
	UTIL_TraceLine( pev->origin, pev->origin + vecDir * 1024.0f, dont_ignore_monsters, ENT( pev ), &tr );

	entvars_t *pevOwner = pOwner ? VARS( pOwner ) : pev;
	float flDamage = pOther->MyMonsterPointer() ? 1.0f : 5.0f;
	ClearMultiDamage();
	pOther->TraceAttack( pevOwner, flDamage, vecDir, &tr, DMG_BULLET );
	ApplyMultiDamage( pev, pevOwner );
}

//=========================================================
// weapon_grapple
//=========================================================
class CNightfireGrapple : public CBasePlayerWeapon
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
	void ItemPostFrame( void );
	void UpdateOnRemove( void );
	BOOL IsUseable( void ) { return TRUE; }
	BOOL CanDeploy( void ) { return TRUE; }

	virtual BOOL UseDecrement( void ) { return FALSE; }

	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];

private:
	CNightfireGrappleTip *Tip( void ) { return (CNightfireGrappleTip *)(CBaseEntity *)m_hTip; }
	void Fire( void );
	void CreateWire( CNightfireGrappleTip *pTip );
	void Reel( void );
	void DestroyTip( void );
	void Maintenance( void );

	int m_iGrappleState;	// GRAPPLE_READY / GRAPPLE_FIRED
	EHANDLE m_hTip;
	CBeam *m_pWire;
};

TYPEDESCRIPTION CNightfireGrapple::m_SaveData[] =
{
	DEFINE_FIELD( CNightfireGrapple, m_iGrappleState, FIELD_INTEGER ),
	DEFINE_FIELD( CNightfireGrapple, m_hTip, FIELD_EHANDLE ),
};

IMPLEMENT_SAVERESTORE( CNightfireGrapple, CBasePlayerWeapon )

LINK_ENTITY_TO_CLASS( weapon_grapple, CNightfireGrapple )

// retail 0x420D9B70
void CNightfireGrapple::Spawn( void )
{
	Precache();
	m_iId = NF_WEAPON_GRAPPLE;
	m_iClip = WEAPON_NOCLIP;	// no magazine: an empty clip would "reload" instead of WeaponIdle
	SET_MODEL( ENT( pev ), "models/w_grapple.mdl" );
	FallInit();
}

// retail 0x420D9910
void CNightfireGrapple::Precache( void )
{
	PRECACHE_MODEL( "models/v_grapple.mdl" );
	PRECACHE_MODEL( "models/w_grapple.mdl" );
	PRECACHE_MODEL( "models/p_grapple.mdl" );
	PRECACHE_MODEL( GRAPPLE_TIP_MODEL );
	PRECACHE_MODEL( GRAPPLE_WIRE_SPRITE );
	PRECACHE_SOUND( "gadgets/grapple_fire.wav" );
	PRECACHE_SOUND( GRAPPLE_HIT_SOUND );
	PRECACHE_SOUND( "gadgets/grapple_flying.wav" );
	PRECACHE_SOUND( "gadgets/grapple_draw.wav" );		// v_grapple.mdl events
	PRECACHE_SOUND( "gadgets/grapple_holster.wav" );
}

// retail 0x420D9970: id 26, no ammo, no clip
int CNightfireGrapple::GetItemInfo( ItemInfo *p )
{
	p->pszName = STRING( pev->classname );
	p->pszAmmo1 = NULL;
	p->iMaxAmmo1 = -1;
	p->pszAmmo2 = NULL;
	p->iMaxAmmo2 = -1;
	p->iMaxClip = WEAPON_NOCLIP;
	p->iSlot = 0;		// [assumed] bucket 1 next to the watch (retail: gadget wheel)
	p->iPosition = 1;
	p->iFlags = 0;
	p->iId = m_iId = NF_WEAPON_GRAPPLE;
	p->iWeight = -1;	// never auto-selected
	return 1;
}

int CNightfireGrapple::AddToPlayer( CBasePlayer *pPlayer )
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

// retail 0x420D99E0
BOOL CNightfireGrapple::Deploy( void )
{
	m_iGrappleState = GRAPPLE_READY;
	m_iClip = WEAPON_NOCLIP;
	m_flNextPrimaryAttack = m_flNextSecondaryAttack = m_flTimeWeaponIdle = gpGlobals->time + 0.5f;
	return DefaultDeploy( "models/v_grapple.mdl", "models/p_grapple.mdl", GRAPPLE_DRAW, "grapple" );
}

// retail 0x420D9BF0
void CNightfireGrapple::Holster( int skiplocal )
{
	m_flTimeWeaponIdle = gpGlobals->time + 10.0f;
	DestroyTip();
	m_iGrappleState = GRAPPLE_READY;
	CBasePlayerWeapon::Holster( skiplocal );
}

void CNightfireGrapple::UpdateOnRemove( void )
{
	DestroyTip();
	CBasePlayerWeapon::UpdateOnRemove();
}

// retail 0x420D9A40: the next shot 0.1 s after an attached tip, else 0.5 s
void CNightfireGrapple::DestroyTip( void )
{
	CNightfireGrappleTip *pTip = Tip();
	if( pTip )
	{
		m_flNextPrimaryAttack = gpGlobals->time + ( pTip->m_iState == TIP_ATTACHED ? 0.1f : 0.5f );
		UTIL_Remove( pTip );
		m_hTip = NULL;
	}
	if( m_pWire )
	{
		UTIL_Remove( m_pWire );
		m_pWire = NULL;
	}
}

// reel in: FIRE_RETURN, the tip and the wire go
void CNightfireGrapple::Reel( void )
{
	SendWeaponAnim( GRAPPLE_FIRE_RETURN );
	m_flTimeWeaponIdle = gpGlobals->time + 0.26f;
	DestroyTip();
	m_iGrappleState = GRAPPLE_READY;
}

// retail 0x420DA2E0: the tip and the wire from the tip to the gun (width 10,
// sine noise 10 while flying, scroll 10)
void CNightfireGrapple::Fire( void )
{
	DestroyTip();

	CNightfireGrappleTip *pTip = CNightfireGrappleTip::Create( m_pPlayer );
	m_hTip = pTip;
	CreateWire( pTip );
}

void CNightfireGrapple::CreateWire( CNightfireGrappleTip *pTip )
{
	m_pWire = CBeam::BeamCreate( GRAPPLE_WIRE_SPRITE, 10 );
	m_pWire->EntsInit( pTip->entindex(), m_pPlayer->entindex());
	m_pWire->SetEndAttachment( 1 );
	m_pWire->SetFlags( BEAM_FSINE | BEAM_FSOLID );
	m_pWire->pev->spawnflags |= SF_BEAM_TEMPORARY;
	m_pWire->SetScrollRate( 10 );
	m_pWire->SetNoise( 10 );
	m_pWire->RelinkBeam();
}

// retail 0x420DA410
void CNightfireGrapple::PrimaryAttack( void )
{
	CNightfireGrappleTip *pTip = Tip();

	if( !pTip && m_iGrappleState == GRAPPLE_READY )
	{
		m_iGrappleState = GRAPPLE_FIRED;
		SendWeaponAnim( GRAPPLE_FIRE_START );
		Fire();
		m_pPlayer->SetAnimation( PLAYER_ATTACK1 );
		m_flNextPrimaryAttack = m_flNextSecondaryAttack = gpGlobals->time + 0.1f;
		if( NF_DEBUG( NF_DBG_ITEMS ))
			ALERT( at_console, "nf_debug: grapple fired from %.0f %.0f %.0f, view %.1f %.1f\n",
				(double)m_pPlayer->pev->origin.x, (double)m_pPlayer->pev->origin.y, (double)m_pPlayer->pev->origin.z,
				(double)m_pPlayer->pev->v_angle.x, (double)m_pPlayer->pev->v_angle.y );
		return;
	}

	// hung on the wire for 5 s with the trigger still held
	if( pTip && pTip->m_iState == TIP_ATTACHED && pTip->m_flAttachTime + GRAPPLE_HANG_TIME < gpGlobals->time )
	{
		if( NF_DEBUG( NF_DBG_ITEMS ))
			ALERT( at_console, "nf_debug: grapple released after %.0f s, player at %.0f %.0f %.0f, %.0f units from the tip\n",
				(double)GRAPPLE_HANG_TIME, (double)m_pPlayer->pev->origin.x, (double)m_pPlayer->pev->origin.y,
				(double)m_pPlayer->pev->origin.z, (double)( pTip->pev->origin - m_pPlayer->pev->origin ).Length());
		Reel();
		m_flNextPrimaryAttack = m_flNextSecondaryAttack = gpGlobals->time + 0.26f;
	}
}

// retail 0x420D9AB0: the trigger let go (or the secondary attack) reels in
void CNightfireGrapple::WeaponIdle( void )
{
	if( m_iGrappleState == GRAPPLE_FIRED )
	{
		Reel();
		if( NF_DEBUG( NF_DBG_ITEMS ))
			ALERT( at_console, "nf_debug: grapple reeled in\n" );
	}

	if( m_flTimeWeaponIdle > gpGlobals->time )
		return;
	SendWeaponAnim( GRAPPLE_IDLE1 );
	m_flTimeWeaponIdle = gpGlobals->time + RANDOM_FLOAT( 8, 15 );
}

// retail Maintenance 0x420D9C30 (vtable slot 110, every frame): an attached
// tip pulls the player, a missed one is reeled in
void CNightfireGrapple::Maintenance( void )
{
	if( m_iGrappleState != GRAPPLE_FIRED )
		return;

	CNightfireGrappleTip *pTip = Tip();
	if( !pTip )
	{
		// the tip is gone (removed by the engine, a level change)
		Reel();
		return;
	}

	if( !m_pWire )
		CreateWire( pTip );	// temporary beams are not saved

	if( pTip->m_iState == TIP_ATTACHED )
	{
		Vector vecDir = ( pTip->pev->origin - m_pPlayer->pev->origin ).Normalize();
		m_pPlayer->pev->velocity = vecDir * GRAPPLE_PULL_SPEED;

		if( m_pWire )
		{
			m_pWire->SetFlags( BEAM_FSOLID );	// taut
			m_pWire->SetNoise( 0 );
		}

		if( gpGlobals->time >= m_flTimeWeaponIdle )
		{
			SendWeaponAnim( GRAPPLE_PULL );
			m_flTimeWeaponIdle = gpGlobals->time + 0.33f;
		}
	}
	else if( pTip->m_iState == TIP_MISSED )
		Reel();
}

void CNightfireGrapple::ItemPostFrame( void )
{
	Maintenance();
	CBasePlayerWeapon::ItemPostFrame();
}
