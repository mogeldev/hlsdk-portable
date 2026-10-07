/*
nf_items.cpp - James Bond 007: Nightfire (PC) map props for the Xash3D port

There is no Nightfire FGD; the key meanings below are inferred from the key
values in the retail maps (m5_power01, m7_island03) and are assumptions.

item_generic   - a studio-model prop (pipes, spotlights, servers, monitors):
                 "model", "body", "skin", "sequencename" (always "idle1" on
                 m5/m7), render keys; "usebody" = body to switch to when
                 triggered (otherwise triggering toggles visibility);
                 "minbbox"/"maxbbox" = collision box ("0 0 0" = not solid).
                 Not handled: "fixedlight", "Effects", "gibmodel".
item_breakable - a prop that breaks when damaged (monitors, PCs, phones):
                 "max_health", "gibmodel" (debris, "none" = no debris),
                 "damagedbody" (body after breaking; the prop stays),
                 "hitanim" / "posthitanim" (sequence on hit / after it),
                 "explosionscale", "material" (4 on all electronics: sparks).
item_armor_plate / item_armor_vest - health pickups (retail CArmorPlate /
                 CArmorVest, docs/retail/player.md *Armour pickups*).
*/

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "animation.h"
#include "effects.h"
#include "weapons.h"
#include "player.h"
#include "skill.h"
#include "items.h"
#include "gamerules.h"
#include "nf_debug.h"

static Vector NF_ParseVector( const char *s )
{
	Vector v;
	UTIL_StringToVector( v, s );
	return v;
}

static BOOL NF_IsNone( string_t s )
{
	return FStringNull( s ) || !STRING( s )[0] || FStrEq( STRING( s ), "none" );
}

class CNightfireItem : public CBaseAnimating
{
public:
	void Spawn( void );
	void Precache( void );
	void KeyValue( KeyValueData *pkvd );
	void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );
	int ObjectCaps( void ) { return CBaseAnimating::ObjectCaps() & ~FCAP_ACROSS_TRANSITION; }

	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];

protected:
	void PlaySequence( string_t name );

	string_t m_iszSequence;
	int m_iUseBody;
	Vector m_vecBBoxMin;
	Vector m_vecBBoxMax;
};

TYPEDESCRIPTION CNightfireItem::m_SaveData[] =
{
	DEFINE_FIELD( CNightfireItem, m_iszSequence, FIELD_STRING ),
	DEFINE_FIELD( CNightfireItem, m_iUseBody, FIELD_INTEGER ),
	DEFINE_FIELD( CNightfireItem, m_vecBBoxMin, FIELD_VECTOR ),
	DEFINE_FIELD( CNightfireItem, m_vecBBoxMax, FIELD_VECTOR ),
};

IMPLEMENT_SAVERESTORE( CNightfireItem, CBaseAnimating )

LINK_ENTITY_TO_CLASS( item_generic, CNightfireItem )

void CNightfireItem::KeyValue( KeyValueData *pkvd )
{
	if( FStrEq( pkvd->szKeyName, "sequencename" ))
	{
		m_iszSequence = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "usebody" ))
	{
		m_iUseBody = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "minbbox" ))
	{
		m_vecBBoxMin = NF_ParseVector( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "maxbbox" ))
	{
		m_vecBBoxMax = NF_ParseVector( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else
		CBaseAnimating::KeyValue( pkvd );
}

void CNightfireItem::Precache( void )
{
	PRECACHE_MODEL( STRING( pev->model ));
}

void CNightfireItem::PlaySequence( string_t name )
{
	int seq = NF_IsNone( name ) ? 0 : LookupSequence( STRING( name ));

	pev->sequence = seq >= 0 ? seq : 0;
	pev->frame = 0;
	ResetSequenceInfo();
	pev->animtime = gpGlobals->time;
	pev->framerate = 1.0f;
}

void CNightfireItem::Spawn( void )
{
	if( FStringNull( pev->model ))
	{
		ALERT( at_error, "%s at %.0f %.0f %.0f has no model\n", STRING( pev->classname ),
			(double)pev->origin.x, (double)pev->origin.y, (double)pev->origin.z );
		REMOVE_ENTITY( ENT( pev ));
		return;
	}

	Precache();
	SET_MODEL( ENT( pev ), STRING( pev->model ));

	pev->movetype = MOVETYPE_NONE;
	pev->takedamage = DAMAGE_NO;

	// the client animates the sequence from animtime/framerate, no think
	PlaySequence( m_iszSequence );

	if( m_vecBBoxMin != g_vecZero || m_vecBBoxMax != g_vecZero )
	{
		pev->solid = SOLID_BBOX;
		UTIL_SetSize( pev, m_vecBBoxMin, m_vecBBoxMax );
	}
	else
	{
		pev->solid = SOLID_NOT;
		UTIL_SetOrigin( pev, pev->origin );
	}
}

void CNightfireItem::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	if( m_iUseBody > 0 )
		pev->body = m_iUseBody;
	else if( FBitSet( pev->effects, EF_NODRAW ))
		ClearBits( pev->effects, EF_NODRAW );
	else
		SetBits( pev->effects, EF_NODRAW );
}

class CNightfireBreakable : public CNightfireItem
{
public:
	void Spawn( void );
	void Precache( void );
	void KeyValue( KeyValueData *pkvd );
	int TakeDamage( entvars_t *pevInflictor, entvars_t *pevAttacker, float flDamage, int bitsDamageType );
	void Killed( entvars_t *pevAttacker, int iGib );
	void EXPORT HitAnimThink( void );

	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];

private:
	string_t m_iszGibModel;
	string_t m_iszHitAnim;
	string_t m_iszPostHitAnim;
	int m_iDamagedBody;
	int m_iMaterial;
	int m_idGib;
};

TYPEDESCRIPTION CNightfireBreakable::m_SaveData[] =
{
	DEFINE_FIELD( CNightfireBreakable, m_iszGibModel, FIELD_STRING ),
	DEFINE_FIELD( CNightfireBreakable, m_iszHitAnim, FIELD_STRING ),
	DEFINE_FIELD( CNightfireBreakable, m_iszPostHitAnim, FIELD_STRING ),
	DEFINE_FIELD( CNightfireBreakable, m_iDamagedBody, FIELD_INTEGER ),
	DEFINE_FIELD( CNightfireBreakable, m_iMaterial, FIELD_INTEGER ),
};

IMPLEMENT_SAVERESTORE( CNightfireBreakable, CNightfireItem )

LINK_ENTITY_TO_CLASS( item_breakable, CNightfireBreakable )

void CNightfireBreakable::KeyValue( KeyValueData *pkvd )
{
	if( FStrEq( pkvd->szKeyName, "gibmodel" ))
	{
		m_iszGibModel = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "hitanim" ))
	{
		m_iszHitAnim = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "posthitanim" ))
	{
		m_iszPostHitAnim = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "damagedbody" ))
	{
		m_iDamagedBody = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "material" ))
	{
		m_iMaterial = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else
		CNightfireItem::KeyValue( pkvd );
}

void CNightfireBreakable::Precache( void )
{
	CNightfireItem::Precache();

	m_idGib = NF_IsNone( m_iszGibModel ) ? 0 : PRECACHE_MODEL( STRING( m_iszGibModel ));
}

void CNightfireBreakable::Spawn( void )
{
	CNightfireItem::Spawn();

	if( FStringNull( pev->model ))
		return; // removed

	// shootable: a box from the sequence bounds unless the map gives one
	if( pev->solid == SOLID_NOT )
	{
		Vector mins, maxs;

		ExtractBbox( pev->sequence, mins, maxs );
		pev->solid = SOLID_BBOX;
		UTIL_SetSize( pev, mins, maxs );
	}

	pev->takedamage = DAMAGE_YES;
	if( pev->max_health <= 0 )
		pev->max_health = 1;
	pev->health = pev->max_health;
}

int CNightfireBreakable::TakeDamage( entvars_t *pevInflictor, entvars_t *pevAttacker, float flDamage, int bitsDamageType )
{
	if( pev->takedamage == DAMAGE_NO )
		return 0;

	if( !NF_IsNone( m_iszHitAnim ) && pev->health > flDamage )
	{
		PlaySequence( m_iszHitAnim );
		SetThink( &CNightfireBreakable::HitAnimThink );
		pev->nextthink = gpGlobals->time + 0.5f;
	}

	return CBaseEntity::TakeDamage( pevInflictor, pevAttacker, flDamage, bitsDamageType );
}

void CNightfireBreakable::HitAnimThink( void )
{
	PlaySequence( NF_IsNone( m_iszPostHitAnim ) ? m_iszSequence : m_iszPostHitAnim );
	SetThink( NULL );
}

void CNightfireBreakable::Killed( entvars_t *pevAttacker, int iGib )
{
	Vector vecSpot = pev->origin + ( pev->mins + pev->maxs ) * 0.5f;

	pev->takedamage = DAMAGE_NO;
	SetThink( NULL );

	if( m_idGib )
	{
		MESSAGE_BEGIN( MSG_PVS, SVC_TEMPENTITY, vecSpot );
			WRITE_BYTE( TE_BREAKMODEL );
			WRITE_COORD( vecSpot.x );
			WRITE_COORD( vecSpot.y );
			WRITE_COORD( vecSpot.z );
			WRITE_COORD( pev->size.x );
			WRITE_COORD( pev->size.y );
			WRITE_COORD( pev->size.z );
			WRITE_COORD( 0 );
			WRITE_COORD( 0 );
			WRITE_COORD( 100 );
			WRITE_BYTE( 10 );		// velocity randomisation
			WRITE_SHORT( m_idGib );
			WRITE_BYTE( 0 );		// let the client decide the count
			WRITE_BYTE( 25 );		// 2.5 seconds
			WRITE_BYTE( BREAK_METAL );
		MESSAGE_END();
	}

	// material 4 is on every electronic breakable of m5/m7
	if( m_iMaterial == 4 )
		UTIL_Sparks( vecSpot );

	SUB_UseTargets( CBaseEntity::Instance( pevAttacker ), USE_TOGGLE, 0 );

	if( m_iDamagedBody > 0 )
	{
		// the broken prop stays, e.g. a smashed monitor
		pev->body = m_iDamagedBody;
		PlaySequence( m_iszSequence );
	}
	else
	{
		pev->solid = SOLID_NOT;
		SetThink( &CBaseEntity::SUB_Remove );
		pev->nextthink = gpGlobals->time + 0.1f;
	}
}

// Armour pickups. Despite the name they heal: retail MyTouch calls the
// player's TakeHealth (the HL health kit with another model, sound and amount;
// CArmorPlate 0x42072e50, CArmorVest 0x42072f50). Spawnflag 1 (float) is
// handled in CItem::Spawn.
extern int gmsgItemPickup;

class CNightfireArmor : public CItem
{
protected:
	BOOL GiveHealth( CBasePlayer *pPlayer, float flHealth, const char *pszSound );
};

BOOL CNightfireArmor::GiveHealth( CBasePlayer *pPlayer, float flHealth, const char *pszSound )
{
	float flBefore = pPlayer->pev->health;

	if( pPlayer->pev->deadflag != DEAD_NO )
		return FALSE;

	if( !pPlayer->TakeHealth( flHealth, DMG_GENERIC ))
		return FALSE;	// full health: the item stays (touched every frame, so no print)

	if( NF_DEBUG( NF_DBG_ITEMS ))
		ALERT( at_console, "nf_debug: %s health %.0f -> %.0f\n", STRING( pev->classname ), flBefore, pPlayer->pev->health );

	MESSAGE_BEGIN( MSG_ONE, gmsgItemPickup, NULL, pPlayer->pev );
		WRITE_STRING( STRING( pev->classname ));
	MESSAGE_END();

	EMIT_SOUND( ENT( pPlayer->pev ), CHAN_STATIC, pszSound, 1, ATTN_NORM );

	if( g_pGameRules->ItemShouldRespawn( this ))
		Respawn();
	else
		UTIL_Remove( this );

	return TRUE;
}

// heals sk_healthkit (skill.cfg: 75 / 50 / 50)
class CArmorPlate : public CNightfireArmor
{
	void Spawn( void );
	void Precache( void );
	BOOL MyTouch( CBasePlayer *pPlayer ) { return GiveHealth( pPlayer, gSkillData.healthkitCapacity, "player/armor_shard.wav" ); }
};

LINK_ENTITY_TO_CLASS( item_armor_plate, CArmorPlate )

void CArmorPlate::Precache( void )
{
	PRECACHE_MODEL( "models/w_armor_plate.mdl" );
	PRECACHE_SOUND( "player/armor_shard.wav" );
}

void CArmorPlate::Spawn( void )
{
	Precache();
	SET_MODEL( ENT( pev ), "models/w_armor_plate.mdl" );
	CItem::Spawn();
	UTIL_SetSize( pev, Vector( -10, -10, 0 ), Vector( 10, 10, 1 ));	// retail 0x42072de0
}

// heals to full (retail: TakeHealth( 200 - health ))
class CArmorVest : public CNightfireArmor
{
	void Spawn( void );
	void Precache( void );
	BOOL MyTouch( CBasePlayer *pPlayer ) { return GiveHealth( pPlayer, pPlayer->pev->max_health - pPlayer->pev->health, "player/armor_vest.wav" ); }
};

LINK_ENTITY_TO_CLASS( item_armor_vest, CArmorVest )

void CArmorVest::Precache( void )
{
	PRECACHE_MODEL( "models/w_armor_vest.mdl" );
	PRECACHE_SOUND( "player/armor_vest.wav" );
}

void CArmorVest::Spawn( void )
{
	Precache();
	SET_MODEL( ENT( pev ), "models/w_armor_vest.mdl" );
	CItem::Spawn();
}
