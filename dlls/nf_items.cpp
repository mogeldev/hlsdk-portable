/*
nf_items.cpp - James Bond 007: Nightfire (PC) map props for the Xash3D port

There is no Nightfire FGD; prop keys initially came from retail maps.
Player-use behavior below is based on CGenericItem::Use (0x420699D0);
origin-based visibility, conditional caps and direct-use once are port decisions.

item_generic   - a studio-model prop (pipes, spotlights, servers, monitors):
                 "model", "body", "skin", "sequencename" (always "idle1" on
                 m5/m7), render keys; "usebody" = body to switch to when
                 triggered; player use (spawnflag 0x80) also supports
                 "usesequencename", "usesound", "useskin", "master" and
                 targets. Spawnflag 0x20 makes player use once-only. Scripted
                 use retains the existing body/visibility behavior;
                 "minbbox"/"maxbbox" = collision box ("0 0 0" = not solid).
                 "fixedlight" (RGB) and "Effects" 256 (EF_FIXEDLIGHT) are plain
                 entvars: the engine uses them as the model light floor.
                 Not handled: "gibmodel".
item_breakable - a prop that breaks when damaged (monitors, PCs, phones):
                 "max_health", "gibmodel" (debris, "none" = no debris),
                 "damagedbody" (body after breaking; the prop stays),
                 "hitanim" / "posthitanim" (sequence on hit / after it),
                 "explosionscale", "material" (4 on all electronics: sparks).
item_grappletarget - an item_generic the grapple hooks into (retail
                 CGrappleTarget, docs/retail/grapple.md).
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
#include "nf_items.h"

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

#define SF_NFITEM_USE_ONCE 0x20
#define SF_NFITEM_PLAYER_USE 0x80

class CNightfireItem : public CBaseAnimating
{
public:
	void Spawn( void );
	void Precache( void );
	void KeyValue( KeyValueData *pkvd );
	void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );
	int ObjectCaps( void )
	{
		int caps = CBaseAnimating::ObjectCaps() & ~FCAP_ACROSS_TRANSITION;
		return FBitSet( pev->spawnflags, SF_NFITEM_PLAYER_USE ) ? caps | FCAP_IMPULSE_USE : caps;
	}
	void EXPORT UseSequenceThink( void );
	void EXPORT UseAnimThink( void );
	void ReportUse( void );

	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];

protected:
	void PlaySequence( string_t name );
	void ApplyPlayerUse( void );

	string_t m_iszSequence;
	int m_iUseBody;
	string_t m_iszUseSequence;
	string_t m_iszUseSound;
	string_t m_iszUseMaster;
	int m_iUseSkin;
	BOOL m_fPlayerUsed;
	BOOL m_fPlayerTargetsFired;
	BOOL m_fUsePending;
	BOOL m_fBroken;
	Vector m_vecBBoxMin;
	Vector m_vecBBoxMax;
};

TYPEDESCRIPTION CNightfireItem::m_SaveData[] =
{
	DEFINE_FIELD( CNightfireItem, m_iszSequence, FIELD_STRING ),
	DEFINE_FIELD( CNightfireItem, m_iUseBody, FIELD_INTEGER ),
	DEFINE_FIELD( CNightfireItem, m_iszUseSequence, FIELD_STRING ),
	DEFINE_FIELD( CNightfireItem, m_iszUseSound, FIELD_STRING ),
	DEFINE_FIELD( CNightfireItem, m_iszUseMaster, FIELD_STRING ),
	DEFINE_FIELD( CNightfireItem, m_iUseSkin, FIELD_INTEGER ),
	DEFINE_FIELD( CNightfireItem, m_fPlayerUsed, FIELD_BOOLEAN ),
	DEFINE_FIELD( CNightfireItem, m_fPlayerTargetsFired, FIELD_BOOLEAN ),
	DEFINE_FIELD( CNightfireItem, m_fUsePending, FIELD_BOOLEAN ),
	DEFINE_FIELD( CNightfireItem, m_fBroken, FIELD_BOOLEAN ),
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
	else if( FStrEq( pkvd->szKeyName, "usesequencename" ))
	{
		m_iszUseSequence = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "usesound" ))
	{
		m_iszUseSound = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "useskin" ))
	{
		m_iUseSkin = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "master" ))
	{
		m_iszUseMaster = ALLOC_STRING( pkvd->szValue );
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
	if( !NF_IsNone( m_iszUseSound ))
		PRECACHE_SOUND( STRING( m_iszUseSound ));
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
	if( m_fBroken || m_fUsePending )
	{
		if( NF_DEBUG( NF_DBG_ITEMS ))
			ALERT( at_console, "nf_debug: item %s reject %s\n", STRING( pev->model ), m_fBroken ? "broken" : "pending" );
		return;
	}

	// Retail tests the caller; normal PlayerUse passes the player in both slots.
	CBaseEntity *pUser = pCaller ? pCaller : pActivator;
	if( !pUser || !pUser->IsPlayer() )
	{
		if( m_iUseBody > 0 )
			pev->body = m_iUseBody;
		else if( FBitSet( pev->effects, EF_NODRAW ))
			ClearBits( pev->effects, EF_NODRAW );
		else
			SetBits( pev->effects, EF_NODRAW );
		return;
	}

	if( !FBitSet( pev->spawnflags, SF_NFITEM_PLAYER_USE ) ||
		( FBitSet( pev->spawnflags, SF_NFITEM_USE_ONCE ) && m_fPlayerUsed ) ||
		!UTIL_IsMasterTriggered( m_iszUseMaster, pActivator ))
	{
		if( NF_DEBUG( NF_DBG_ITEMS ))
			ALERT( at_console, "nf_debug: item %s reject gate flags %d used %d master %s\n",
				STRING( pev->model ), pev->spawnflags, m_fPlayerUsed, STRING( m_iszUseMaster ));
		return;
	}

	Vector usePoint;
	NF_ItemUsePoint( this, pUser->pev->origin, usePoint );
	if(( usePoint - pUser->pev->origin ).Length() > 64.0f )
	{
		if( NF_DEBUG( NF_DBG_ITEMS ))
			ALERT( at_console, "nf_debug: item %s reject range\n", STRING( pev->model ));
		return;
	}
	NF_ItemUsePoint( this, pUser->EyePosition(), usePoint );
	TraceResult tr;
	UTIL_TraceLine( pUser->EyePosition(), usePoint, dont_ignore_monsters,
		dont_ignore_glass, pUser->edict(), &tr );
	if( tr.fStartSolid || tr.fAllSolid || ( tr.flFraction < 1.0f && tr.pHit != edict() ))
	{
		if( NF_DEBUG( NF_DBG_ITEMS ))
			ALERT( at_console, "nf_debug: item %s reject occluded fraction %.3f hit %s eye %.1f %.1f %.1f point %.1f %.1f %.1f\n",
				STRING( pev->model ), tr.flFraction, tr.pHit ? STRING( tr.pHit->v.classname ) : "none",
				pUser->EyePosition().x, pUser->EyePosition().y, pUser->EyePosition().z,
				usePoint.x, usePoint.y, usePoint.z );
		return;
	}

	// [assumed] Also latch direct use; retail only latches the sequence branch.
	m_fPlayerUsed = TRUE;
	if( !NF_IsNone( m_iszUseSequence ))
	{
		if( !NF_IsNone( m_iszUseSound ))
			EMIT_SOUND( edict(), CHAN_BODY, STRING( m_iszUseSound ), 1.0f, ATTN_NONE );
		m_fUsePending = TRUE;
		SetThink( &CNightfireItem::UseSequenceThink );
		pev->nextthink = gpGlobals->time + 0.1f;
	}
	else
		ApplyPlayerUse();
}

void CNightfireItem::ApplyPlayerUse( void )
{
	if( m_iUseBody > 0 )
		SetBodygroup( 0, m_iUseBody );
	if( m_iUseSkin > 0 )
		pev->skin = m_iUseSkin;

	CBaseEntity *pPlayer = UTIL_PlayerByIndex( 1 );
	m_fPlayerTargetsFired = TRUE;
	if( NF_DEBUG( NF_DBG_ITEMS ))
		ALERT( at_console, "nf_debug: item %s player targets %s body %d skin %d sequence %d\n",
			STRING( pev->model ), STRING( pev->target ), pev->body, pev->skin, pev->sequence );
	SUB_UseTargets( pPlayer ? pPlayer : this, USE_TOGGLE, 0 );
}

void CNightfireItem::UseSequenceThink( void )
{
	m_fUsePending = FALSE;
	if( m_fBroken )
	{
		SetThink( NULL );
		pev->nextthink = 0;
		return;
	}

	PlaySequence( m_iszUseSequence );
	SetThink( &CNightfireItem::UseAnimThink );
	pev->nextthink = gpGlobals->time + 0.1f;
	// Retail 0x42069110 fires targets at sequence start, not at completion.
	ApplyPlayerUse();
}

void CNightfireItem::UseAnimThink( void )
{
	StudioFrameAdvance();
	BOOL finished = m_fSequenceFinished;
	DispatchAnimEvents();
	// Event dispatch predicts one tick ahead; finish on the actual last frame.
	if( finished && !m_fSequenceLoops )
	{
		pev->frame = 255.0f;
		pev->framerate = 0;
		SetThink( NULL );
		pev->nextthink = 0;
	}
	else
		pev->nextthink = gpGlobals->time + 0.1f;
}

void CNightfireItem::ReportUse( void )
{
	ALERT( at_console, "nf_debug: item %s model %s flags %d caps %d body %d skin %d used %d pending %d fired %d broken %d sequence %d frame %.1f master %s/%d target %s origin %.1f %.1f %.1f effects %d finished %d loops %d next %.2f\n",
		STRING( pev->targetname ), STRING( pev->model ), pev->spawnflags, ObjectCaps(), pev->body, pev->skin,
		m_fPlayerUsed, m_fUsePending, m_fPlayerTargetsFired, m_fBroken, pev->sequence, pev->frame,
		STRING( m_iszUseMaster ), UTIL_IsMasterTriggered( m_iszUseMaster, UTIL_PlayerByIndex( 1 )),
		STRING( pev->target ), pev->origin.x, pev->origin.y, pev->origin.z, pev->effects,
		m_fSequenceFinished, m_fSequenceLoops, pev->nextthink > 0 ? pev->nextthink - gpGlobals->time : 0 );
	CBaseEntity *player = UTIL_PlayerByIndex( 1 );
	if( player )
	{
		TraceResult tr;
		Vector usePoint;
		if( !NF_ItemUsePoint( this, player->EyePosition(), usePoint )) usePoint = pev->origin;
		UTIL_TraceLine( player->EyePosition(), usePoint, dont_ignore_monsters,
			dont_ignore_glass, player->edict(), &tr );
		ALERT( at_console, "nf_debug: item %s use trace %.3f start %d hit %s center %.1f %.1f %.1f eye %.1f %.1f %.1f\n",
			STRING( pev->targetname ), tr.flFraction, tr.fStartSolid, tr.pHit ? STRING( tr.pHit->v.classname ) : "none",
			Center().x, Center().y, Center().z, player->EyePosition().x, player->EyePosition().y, player->EyePosition().z );
	}
}

BOOL NF_ItemUsePoint( CBaseEntity *entity, const Vector &source, Vector &point )
{
	if( !FBitSet( entity->pev->spawnflags, SF_NFITEM_PLAYER_USE ) ||
		!( FClassnameIs( entity->pev, "item_generic" ) || FClassnameIs( entity->pev, "item_breakable" ) ||
			FClassnameIs( entity->pev, "item_grappletarget" )))
		return FALSE;
	Vector mins = entity->pev->origin + entity->pev->mins;
	Vector maxs = entity->pev->origin + entity->pev->maxs;
	// Retain the authored interaction height when sequence bounds lie below it.
	mins.z = min( mins.z, entity->pev->origin.z );
	maxs.z = max( maxs.z, entity->pev->origin.z );
	for( int i = 0; i < 3; i++ )
		point[i] = max( mins[i], min( maxs[i], source[i] ));
	return TRUE;
}

BOOL NF_ItemCommand( CBaseEntity *player, const char *command )
{
	if( !FStrEq( command, "nf_iteminfo" )) return FALSE;
	if( NF_DEBUG( NF_DBG_ITEMS ))
	{
		const char *classes[] = { "item_generic", "item_breakable", "item_grappletarget" };
		for( int i = 0; i < 3; i++ )
		{
			CBaseEntity *entity = NULL;
			while(( entity = UTIL_FindEntityByClassname( entity, classes[i] )) != NULL )
				if( CMD_ARGC() < 2 || FStrEq( STRING( entity->pev->targetname ), CMD_ARGV( 1 )) ||
					FStrEq( STRING( entity->pev->model ), CMD_ARGV( 1 )))
					((CNightfireItem *)entity)->ReportUse();
		}
	}
	return TRUE;
}

// item_grappletarget: what the grapple (dlls/nf_grapple.cpp) can hook into.
// Retail CGrappleTarget (factory 0x42070290, docs/retail/grapple.md) is an
// item_generic: models/grapple_point.mdl unless "model" is set, solid box
// (-16 -16 0) - (16 16 16), animated in single player. Not ported: its
// "fixedlight" pulsing between 120 and 255 (AnimateThink 0x4206ff60, single
// player only; Spawn 0x42070140 sets effect 0x100 = EF_FIXEDLIGHT).
class CNightfireGrappleTarget : public CNightfireItem
{
public:
	void Spawn( void );
	BOOL IsGrappleTarget( void ) { return TRUE; }
	void EXPORT PulseThink( void );

	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];

private:
	float m_flLevel;	// retail +0xF8: the grey level of fixedlight
	int m_iStep;		// retail +0x104: +1 / -1
};

TYPEDESCRIPTION CNightfireGrappleTarget::m_SaveData[] =
{
	DEFINE_FIELD( CNightfireGrappleTarget, m_flLevel, FIELD_FLOAT ),
	DEFINE_FIELD( CNightfireGrappleTarget, m_iStep, FIELD_INTEGER ),
};

IMPLEMENT_SAVERESTORE( CNightfireGrappleTarget, CNightfireItem )

LINK_ENTITY_TO_CLASS( item_grappletarget, CNightfireGrappleTarget )

void CNightfireGrappleTarget::Spawn( void )
{
	if( FStringNull( pev->model ) || !STRING( pev->model )[0] )
		pev->model = MAKE_STRING( "models/grapple_point.mdl" );

	CNightfireItem::Spawn();
	pev->solid = SOLID_BBOX;
	UTIL_SetSize( pev, Vector( -16, -16, 0 ), Vector( 16, 16, 16 ));

	pev->effects |= EF_FIXEDLIGHT;
	m_flLevel = pev->fixedlight.x;
	m_iStep = 1;
	if( !g_pGameRules->IsMultiplayer())
	{
		SetThink( &CNightfireGrappleTarget::PulseThink );
		pev->nextthink = gpGlobals->time + 0.1f;
	}
}

// retail AnimateThink 0x4206ff60: 120 <-> 255 in steps of 5, all channels equal
void CNightfireGrappleTarget::PulseThink( void )
{
	pev->nextthink = gpGlobals->time + 0.1f;

	if( m_flLevel >= 255.0f )
		m_iStep = -1;
	else if( m_flLevel <= 120.0f )
		m_iStep = 1;

	m_flLevel += m_iStep * 5;
	pev->fixedlight = Vector( m_flLevel, m_flLevel, m_flLevel );
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

	if( !m_fUsePending && !NF_IsNone( m_iszHitAnim ) && pev->health > flDamage )
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
	if( m_fBroken )
		return;
	m_fBroken = TRUE;
	m_fUsePending = FALSE;

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

	// [assumed] Deduplicate completed use, but let destruction fire a cancelled start.
	if( !FBitSet( pev->spawnflags, SF_NFITEM_USE_ONCE ) || !m_fPlayerTargetsFired )
	{
		if( NF_DEBUG( NF_DBG_ITEMS ))
			ALERT( at_console, "nf_debug: item %s destroyed targets %s\n", STRING( pev->model ), STRING( pev->target ));
		SUB_UseTargets( CBaseEntity::Instance( pevAttacker ), USE_TOGGLE, 0 );
	}

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
