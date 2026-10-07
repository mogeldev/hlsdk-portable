/*
nf_grenades.cpp - James Bond 007: Nightfire (PC) hand grenades

Frag grenade (weapon_fraggrenade / ammo_fraggrenade) and flash grenade
(weapon_flashgrenade / ammo_flashgrenade) with their projectiles
(frag_grenade, flash_grenade). Retail game.dll classes CFragHandGrenade /
CFlashHandGrenade (factories 0x420dcd70 / 0x420dcd10) and the projectiles
(0x4206f660 / 0x4206f600); addresses and values in docs/retail/weapons.md
of the project repo. Like the Half-Life hand grenade: the primary attack
pulls the pin, the release throws; the secondary attack rolls the grenade,
farther the longer it is held.

The weapon is built into the server and the client (weapon prediction),
the projectiles only into the server.
*/

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "nodes.h"
#include "player.h"
#include "soundent.h"
#include "shake.h"
#include "nf_weapons.h"
#include "nf_debug.h"

enum nf_grenade_e
{
	NF_GRENADE_IDLE1 = 0,
	NF_GRENADE_IDLE2,
	NF_GRENADE_IDLE3,
	NF_GRENADE_PULL,
	NF_GRENADE_THROW,
	NF_GRENADE_THROW_LAST,	// not used by retail
	NF_GRENADE_DRAW,
	NF_GRENADE_HOLSTER	// not used by retail
};

#define NF_GRENADE_MAX_CARRY	10	// retail ammo table: fraggren / flashgren 10
#define NF_GRENADE_ROLL_FUSE	3.75f	// rolled: seconds from the release

// frag: retail id 16, fuse 3.25 s; rolled 350 u/s x 0.9 .. 2 (this branch
// sits in the extra code section "new" of the retail game.dll, 0x422d2e0b)
const nf_grenade_info_t g_nfFragGrenade =
{
	NF_WEAPON_FRAGGRENADE, "weapon_fraggrenade", "ammo_fraggrenade",
	"models/v_frag_grenade.mdl", "models/p_frag_grenade.mdl",
	"models/w_frag_grenade.mdl", "models/w_ammo_frag_grenade.mdl",
	"fraggren", 0,
	3.25f,
	350.0f, 1.0f, 0.9f, 2.0f,
	FALSE
};

// flash: retail id 15, fuse 3.75 s; rolled 100 u/s x 0 .. 10
const nf_grenade_info_t g_nfFlashGrenade =
{
	NF_WEAPON_FLASHGRENADE, "weapon_flashgrenade", "ammo_flashgrenade",
	"models/v_flash_grenade.mdl", "models/p_flash_grenade.mdl",
	"models/w_flash_grenade.mdl", "models/w_ammo_flash_grenade.mdl",
	"flashgren", 1,
	3.75f,
	100.0f, 2.0f, 0.0f, 10.0f,
	TRUE
};

LINK_ENTITY_TO_CLASS( weapon_fraggrenade, CNightfireFragGrenade )
LINK_ENTITY_TO_CLASS( ammo_fraggrenade, CNightfireFragGrenade )
LINK_ENTITY_TO_CLASS( weapon_flashgrenade, CNightfireFlashGrenade )
LINK_ENTITY_TO_CLASS( ammo_flashgrenade, CNightfireFlashGrenade )

#if !CLIENT_DLL
//=========================================================
// projectile
//=========================================================
class CNightfireGrenade : public CGrenade
{
public:
	void Spawn( void );
	void Precache( void );
	void BounceSound( void );
	void Explode( TraceResult *pTrace, int bitsDamageType );

	static CNightfireGrenade *Shoot( const nf_grenade_info_t *info, entvars_t *pevOwner, Vector vecStart, Vector vecVelocity, float time );

private:
	BOOL IsFlash( void ) { return FClassnameIs( pev, "flash_grenade" ); }
};

LINK_ENTITY_TO_CLASS( frag_grenade, CNightfireGrenade )
LINK_ENTITY_TO_CLASS( flash_grenade, CNightfireGrenade )

void CNightfireGrenade::Precache( void )
{
	PRECACHE_MODEL( "models/w_frag_grenade.mdl" );
	PRECACHE_MODEL( "models/w_flash_grenade.mdl" );
	PRECACHE_SOUND( "common/grenade_bounce_1.wav" );
	PRECACHE_SOUND( "common/grenade_bounce_2.wav" );
	PRECACHE_SOUND( "common/grenade_bounce_3.wav" );
	PRECACHE_SOUND( "weapons/flashgr.wav" );
}

// retail 0x4206ef90 / 0x4206ec40; the classname is set before
void CNightfireGrenade::Spawn( void )
{
	Precache();
	pev->movetype = MOVETYPE_BOUNCE;
	pev->solid = SOLID_BBOX;
	SET_MODEL( ENT( pev ), IsFlash() ? "models/w_flash_grenade.mdl" : "models/w_frag_grenade.mdl" );
	UTIL_SetSize( pev, g_vecZero, g_vecZero );
	m_fRegisteredSound = FALSE;
}

// retail ShootTimed variants 0x4206fdf0 (frag) / 0x4206fbb0 (flash): the
// Half-Life one with the Nightfire model and damage
CNightfireGrenade *CNightfireGrenade::Shoot( const nf_grenade_info_t *info, entvars_t *pevOwner, Vector vecStart, Vector vecVelocity, float time )
{
	CNightfireGrenade *pGrenade = GetClassPtr( (CNightfireGrenade *)NULL );
	pGrenade->pev->classname = MAKE_STRING( info->flash ? "flash_grenade" : "frag_grenade" );
	pGrenade->Spawn();
	UTIL_SetOrigin( pGrenade->pev, vecStart );
	pGrenade->pev->velocity = vecVelocity;
	pGrenade->pev->angles = UTIL_VecToAngles( pGrenade->pev->velocity );
	pGrenade->pev->owner = ENT( pevOwner );

	pGrenade->SetTouch( &CGrenade::BounceTouch );
	pGrenade->pev->dmgtime = gpGlobals->time + time;
	pGrenade->SetThink( &CGrenade::TumbleThink );
	pGrenade->pev->nextthink = gpGlobals->time + 0.1f;
	if( time < 0.1f )
	{
		pGrenade->pev->nextthink = gpGlobals->time;
		pGrenade->pev->velocity = g_vecZero;
	}

	pGrenade->pev->sequence = RANDOM_LONG( 3, 6 );
	pGrenade->pev->framerate = 1.0f;
	pGrenade->ResetSequenceInfo();
	pGrenade->pev->gravity = 0.5f;
	pGrenade->pev->friction = 0.8f;
	pGrenade->pev->dmg = info->flash ? 100.0f : 182.0f;
	return pGrenade;
}

// retail 0x4206e480
void CNightfireGrenade::BounceSound( void )
{
	switch( RANDOM_LONG( 0, 2 ))
	{
	case 0: EMIT_SOUND( ENT( pev ), CHAN_BODY, "common/grenade_bounce_1.wav", 0.25f, 0.8f ); break;
	case 1: EMIT_SOUND( ENT( pev ), CHAN_BODY, "common/grenade_bounce_2.wav", 0.25f, 0.8f ); break;
	case 2: EMIT_SOUND( ENT( pev ), CHAN_BODY, "common/grenade_bounce_3.wav", 0.25f, 0.8f ); break;
	}
}

// retail 0x420412e0: players and monsters within 1500 units that see the
// flash; amount = flAmount at the grenade, 0 at 1500 units
static void NF_RadiusFlash( Vector vecSrc, entvars_t *pevInflictor, float flAmount )
{
	const float flRadius = 1500.0f;
	const BOOL bInWater = UTIL_PointContents( vecSrc ) == CONTENTS_WATER;
	CBaseEntity *pEntity = NULL;

	vecSrc.z += 1.0f;	// in case the grenade lies on the ground

	while(( pEntity = UTIL_FindEntityInSphere( pEntity, vecSrc, flRadius )) != NULL )
	{
		if( !pEntity->IsPlayer() && !pEntity->MyMonsterPointer())
			continue;
		if( pEntity->pev->takedamage == DAMAGE_NO || pEntity->pev->deadflag != DEAD_NO )
			continue;
		if( bInWater ? pEntity->pev->waterlevel == 0 : pEntity->pev->waterlevel == 3 )
			continue;

		TraceResult tr;
		UTIL_TraceLine( vecSrc, pEntity->BodyTarget( vecSrc ), dont_ignore_monsters, ENT( pevInflictor ), &tr );
		if( tr.flFraction != 1.0f && tr.pHit != pEntity->edict())
			continue;
		if( tr.fStartSolid )
			tr.vecEndPos = vecSrc;

		float flAdj = flAmount - ( vecSrc - tr.vecEndPos ).Length() * ( flAmount / flRadius );
		if( flAdj < 0.0f )
			flAdj = 0.0f;

		// facing the flash: it lies in front of the eyes
		UTIL_MakeVectors( pEntity->IsPlayer() ? pEntity->pev->v_angle : pEntity->pev->angles );
		const Vector vecEye = pEntity->pev->origin + pEntity->pev->view_ofs;
		const BOOL bFacing = DotProduct( vecSrc - vecEye, gpGlobals->v_forward ) >= 0.0f;

		if( pEntity->IsPlayer())
		{
			if( bFacing )
				UTIL_ScreenFade( pEntity, Vector( 255, 255, 255 ), flAdj * 3.0f, flAdj / 1.5f, 255, FFADE_IN );
		}
		// retail monsters: blinded until now + amount * 3 (facing) or * 1.75
		// (+0x384) [not yet: no blinded state in the enemy AI]

		if( NF_DEBUG( NF_DBG_WEAPONS ))
			ALERT( at_console, "nf_debug: flash hits %s amount %.1f facing %d%s\n", STRING( pEntity->pev->classname ),
				flAdj, bFacing, pEntity->IsPlayer() ? ( bFacing ? " -> screen fade" : "" ) : " (monster: not blinded yet)" );
	}
}

// frag: the Half-Life explosion (retail plays events/explosion.sc and
// weapons/grenade_explode.wav instead [not yet]); flash: retail 0x4206ed50
void CNightfireGrenade::Explode( TraceResult *pTrace, int bitsDamageType )
{
	if( NF_DEBUG( NF_DBG_WEAPONS ))
		ALERT( at_console, "nf_debug: %s explodes at %.0f %.0f %.0f damage %.0f\n", STRING( pev->classname ),
			pev->origin.x, pev->origin.y, pev->origin.z, pev->dmg );

	if( !IsFlash())
	{
		CGrenade::Explode( pTrace, bitsDamageType );
		return;
	}

	pev->model = iStringNull;
	pev->solid = SOLID_NOT;
	pev->takedamage = DAMAGE_NO;

	if( pTrace->flFraction != 1.0f )
		pev->origin = pTrace->vecEndPos + pTrace->vecPlaneNormal * ( pev->dmg - 24.0f ) * 0.6f;

	const BOOL bInWater = UTIL_PointContents( pev->origin ) == CONTENTS_WATER;

	CSoundEnt::InsertSound( bits_SOUND_COMBAT, pev->origin, 256, 3.0f );
	pev->owner = NULL;
	NF_RadiusFlash( pev->origin, pev, 10.0f );
	EMIT_SOUND( ENT( pev ), CHAN_BODY, "weapons/flashgr.wav", 1.0f, 0.8f );

	pev->effects |= EF_NODRAW;
	pev->velocity = g_vecZero;
	SetThink( &CGrenade::Smoke );
	pev->nextthink = gpGlobals->time + 0.3f;

	if( !bInWater )
	{
		int sparkCount = RANDOM_LONG( 0, 3 );
		for( int i = 0; i < sparkCount; i++ )
			Create( "spark_shower", pev->origin, pTrace->vecPlaneNormal, NULL );
	}
}
#endif // !CLIENT_DLL

//=========================================================
// weapon (retail vtable slots: Spawn 0, GetItemInfo 74, Deploy 76,
// CanHolster 77, Holster 78, PrimaryAttack 103, SecondaryAttack 104,
// WeaponIdle 106; frag 0x420dc570 ...)
//=========================================================
void CNightfireHandGrenade::Spawn( void )
{
	Precache();
	m_iId = Info()->id;
#if !CLIENT_DLL
	// ammo_* gives two grenades and keeps its own model; both become the weapon
	if( FClassnameIs( pev, Info()->ammoclass ))
	{
		m_iDefaultAmmo = 2;
		SET_MODEL( ENT( pev ), Info()->ammomodel );
	}
	else
	{
		m_iDefaultAmmo = 1;
		SET_MODEL( ENT( pev ), Info()->wmodel );
	}
	pev->classname = MAKE_STRING( Info()->classname );
#endif
	FallInit();
}

void CNightfireHandGrenade::Precache( void )
{
	PRECACHE_MODEL( Info()->vmodel );
	PRECACHE_MODEL( Info()->pmodel );
	PRECACHE_MODEL( Info()->wmodel );
	PRECACHE_MODEL( Info()->ammomodel );
#if !CLIENT_DLL
	UTIL_PrecacheOther( Info()->flash ? "flash_grenade" : "frag_grenade" );
#endif
}

int CNightfireHandGrenade::GetItemInfo( ItemInfo *p )
{
	p->pszName = Info()->classname;
	p->pszAmmo1 = Info()->ammo;
	p->iMaxAmmo1 = NF_GRENADE_MAX_CARRY;
	p->pszAmmo2 = NULL;
	p->iMaxAmmo2 = -1;
	p->iMaxClip = WEAPON_NOCLIP;	// retail 1 (its m_iClip only flags "has a grenade")
	p->iSlot = 4;			// [assumed] Half-Life grenade bucket
	p->iPosition = Info()->position;
	p->iId = m_iId = Info()->id;
	p->iWeight = 5;
	p->iFlags = ITEM_FLAG_LIMITINWORLD | ITEM_FLAG_EXHAUSTIBLE;
	return 1;
}

int CNightfireHandGrenade::AddToPlayer( CBasePlayer *pPlayer )
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

BOOL CNightfireHandGrenade::Deploy( void )
{
	m_flReleaseThrow = -1.0f;
	m_fInAttack = 0;
	return DefaultDeploy( Info()->vmodel, Info()->pmodel, NF_GRENADE_DRAW, "grenade", UseDecrement() ? 1 : 0 );
}

// retail 0x420dc640: no holster animation; an empty weapon is removed
void CNightfireHandGrenade::Holster( int skiplocal )
{
	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.5f;

	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0 )
	{
		m_pPlayer->pev->weapons &= ~( 1 << m_iId );
		DestroyItem();
	}
	else
	{
		m_flStartThrow = 0.0f;
		m_flReleaseThrow = 0.0f;
		m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 1.0f;
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 2.5f;
	}

	EMIT_SOUND( ENT( m_pPlayer->pev ), CHAN_WEAPON, "common/null.wav", 1.0f, ATTN_NORM );
}

// retail 0x420db730 / 0x420db7a0
void CNightfireHandGrenade::PullPin( int mode )
{
	if( m_flStartThrow == 0.0f && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] > 0 )
	{
		m_flStartThrow = gpGlobals->time;
		m_flReleaseThrow = 0.0f;
		m_fireState = mode;
		SendWeaponAnim( NF_GRENADE_PULL, UseDecrement() ? 1 : 0 );
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 1.0f;
	}
}

void CNightfireHandGrenade::Throw( void )
{
	const nf_grenade_info_t *g = Info();
	Vector angThrow = m_pPlayer->pev->v_angle + m_pPlayer->pev->punchangle;

	if( angThrow.x < 0.0f )
		angThrow.x = -10.0f + angThrow.x * ( ( 90.0f - 10.0f ) / 90.0f );
	else
		angThrow.x = -10.0f + angThrow.x * ( ( 90.0f + 10.0f ) / 90.0f );

	float flVel = ( 90.0f - angThrow.x ) * 4.0f;
	if( flVel > 500.0f )
		flVel = 500.0f;

	UTIL_MakeVectors( angThrow );
	Vector vecSrc = m_pPlayer->pev->origin + m_pPlayer->pev->view_ofs + gpGlobals->v_forward * 16.0f;
	Vector vecThrow;
	float flFuse;

	if( m_fireState == 1 )
	{
		float flScale = g->roll_scale * ( m_flReleaseThrow - m_flStartThrow - 0.6f );
		if( flScale < g->roll_min )
			flScale = g->roll_min;
		else if( flScale > g->roll_max )
			flScale = g->roll_max;
		vecThrow = ( gpGlobals->v_forward * g->roll_speed + m_pPlayer->pev->velocity ) * flScale;
		flFuse = m_flReleaseThrow - gpGlobals->time + NF_GRENADE_ROLL_FUSE;
	}
	else
	{
		vecThrow = gpGlobals->v_forward * flVel + m_pPlayer->pev->velocity;
		flFuse = m_flStartThrow - gpGlobals->time + g->fuse;
	}
	if( flFuse < 0.0f )
		flFuse = 0.0f;

#if !CLIENT_DLL
	CNightfireGrenade::Shoot( g, m_pPlayer->pev, vecSrc, vecThrow, flFuse );
	if( NF_DEBUG( NF_DBG_WEAPONS ))
		ALERT( at_console, "nf_debug: %s %s speed %.0f fuse %.2f, %d left\n", g->classname, m_fireState == 1 ? "rolled" : "thrown",
			vecThrow.Length(), flFuse, m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] - 1 );
#endif

	m_pPlayer->SetAnimation( PLAYER_ATTACK1 );

	m_flStartThrow = 0.0f;
	m_flReleaseThrow = 0.0f;
	m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 1.0f;
	m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + 1.0f;
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 2.5f;

	if( --m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0 )
	{
		// let the throw animation finish, then switch away
		m_flNextPrimaryAttack = m_flNextSecondaryAttack = m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 2.0f;
		RetireWeapon();
	}
}

// retail 0x420dc720
void CNightfireHandGrenade::WeaponIdle( void )
{
	if( m_flTimeWeaponIdle > UTIL_WeaponTimeBase())
		return;

	// trigger let go (WeaponIdle runs only without attack buttons)
	if( m_flReleaseThrow == 0.0f && m_flStartThrow != 0.0f && gpGlobals->time - m_flStartThrow >= 1.0f )
	{
		m_flReleaseThrow = gpGlobals->time;
		m_fInAttack = 0;
	}

	if( m_flStartThrow != 0.0f )
	{
		// the throw animation first; the grenade leaves the hand 0.6 s later
		if( !m_fInAttack )
		{
			SendWeaponAnim( NF_GRENADE_THROW, UseDecrement() ? 1 : 0 );
			m_fInAttack = 1;
			return;
		}
		if( gpGlobals->time - m_flReleaseThrow >= 0.6f )
			Throw();
		return;
	}

	if( m_flReleaseThrow > 0.0f )
	{
		// thrown: take the next grenade
		m_flStartThrow = 0.0f;
		if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0 )
		{
			RetireWeapon();
			return;
		}
		SendWeaponAnim( NF_GRENADE_DRAW, UseDecrement() ? 1 : 0 );
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10.0f, 15.0f );
		m_flReleaseThrow = -1.0f;
		return;
	}

	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] > 0 )
	{
		float flRand = UTIL_SharedRandomFloat( m_pPlayer->random_seed, 0.0f, 1.0f );
		int iAnim = flRand <= 0.33f ? NF_GRENADE_IDLE1 : flRand < 0.66f ? NF_GRENADE_IDLE2 : NF_GRENADE_IDLE3;
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10.0f, 15.0f );
		SendWeaponAnim( iAnim, UseDecrement() ? 1 : 0 );
	}
}
