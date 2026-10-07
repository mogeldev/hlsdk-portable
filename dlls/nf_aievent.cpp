/*
nf_aievent.cpp - James Bond 007: Nightfire (PC) AI events

info_aievent marks a spot where characters do something: run to an alarm
and press it, take cover, die in a special way. Retail CAIEvent (game.dll,
vtable 0x421097CC) derives from the AI scripted sequence; here it is a
plain point entity and the character side lives in dlls/nf_enemy.cpp.
Retail behaviour, offsets and open questions: docs/retail/aievent.md.

Keys: eventtype (see nf_aievent.h), m_iszPlay (sequence), m_flRadius,
anglerange (type 1), animcount (type 2), usetarget (fired with USE_ON when a
character arrives at a type 3/4 event), target / delay (fired after the
animation), probability (parsed, never read in retail), m_iszEntity /
m_fMoveTo (scripted-sequence keys, not used by AI events).
*/

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "nf_aievent.h"
#include "nf_debug.h"

TYPEDESCRIPTION CAIEvent::m_SaveData[] =
{
	DEFINE_FIELD( CAIEvent, m_iEventType, FIELD_INTEGER ),
	DEFINE_FIELD( CAIEvent, m_flAngleRange, FIELD_FLOAT ),
	DEFINE_FIELD( CAIEvent, m_iAnimCount, FIELD_INTEGER ),
	DEFINE_FIELD( CAIEvent, m_iCounter, FIELD_INTEGER ),
	DEFINE_FIELD( CAIEvent, m_iszUseTarget, FIELD_STRING ),
	DEFINE_FIELD( CAIEvent, m_iszPlay, FIELD_STRING ),
	DEFINE_FIELD( CAIEvent, m_iszEntity, FIELD_STRING ),
	DEFINE_FIELD( CAIEvent, m_flRadius, FIELD_FLOAT ),
	DEFINE_FIELD( CAIEvent, m_fLocked, FIELD_BOOLEAN ),
	DEFINE_FIELD( CAIEvent, m_flNextValidTime, FIELD_TIME ),
};

IMPLEMENT_SAVERESTORE( CAIEvent, CBaseDelay )

LINK_ENTITY_TO_CLASS( info_aievent, CAIEvent )

void CAIEvent::KeyValue( KeyValueData *pkvd )
{
	if( FStrEq( pkvd->szKeyName, "eventtype" ))
	{
		m_iEventType = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "anglerange" ))
	{
		m_flAngleRange = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "animcount" ))
	{
		m_iAnimCount = (int)atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "probability" ))
	{
		pkvd->fHandled = TRUE;	// retail parses it into +0x88C and never reads it
	}
	else if( FStrEq( pkvd->szKeyName, "usetarget" ))
	{
		m_iszUseTarget = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "m_iszPlay" ))
	{
		m_iszPlay = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "m_iszEntity" ))
	{
		m_iszEntity = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "m_flRadius" ))
	{
		m_flRadius = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "m_fMoveTo" ) || FStrEq( pkvd->szKeyName, "nodetype" ))
	{
		pkvd->fHandled = TRUE;	// not used by AI events
	}
	else
		CBaseDelay::KeyValue( pkvd );
}

void CAIEvent::Spawn( void )
{
	pev->solid = SOLID_NOT;
	pev->movetype = MOVETYPE_NONE;
	pev->effects |= EF_NODRAW;

	// retail: trace 32 units up and down, a start in solid lifts the event by one unit
	if( !FBitSet( pev->spawnflags, SF_NF_AIEVENT_NO_DROP ))
	{
		TraceResult tr;
		UTIL_TraceLine( pev->origin + Vector( 0, 0, 32 ), pev->origin - Vector( 0, 0, 32 ), ignore_monsters, ENT( pev ), &tr );
		if( tr.fStartSolid )
			pev->origin.z += 1;
		else if( tr.flFraction < 1.0f )
			pev->origin = tr.vecEndPos;
	}
	UTIL_SetOrigin( pev, pev->origin );

	if( m_iEventType == NF_AIEVENT_VENT )
	{
		SetThink( &CAIEvent::DetectEnemyThink );
		pev->nextthink = gpGlobals->time + 1.0f;
	}

	if( NF_DEBUG( NF_DBG_MONSTERS ))
		ALERT( at_console, "nf_debug: aievent type %d '%s' at %.0f %.0f %.0f radius %.0f\n", m_iEventType, STRING( m_iszPlay ),
			pev->origin.x, pev->origin.y, pev->origin.z, m_flRadius );
}

void CAIEvent::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	// retail: the activator is always player 1
	Trigger( UTIL_PlayerByIndex( 1 ));
}

void CAIEvent::Trigger( CBaseEntity *pActivator )
{
	if( m_fLocked )
		return;

	CBaseEntity *pEntity = NULL;
	while(( pEntity = UTIL_FindEntityInSphere( pEntity, pev->origin, m_flRadius )) != NULL )
	{
		if( pEntity->MyMonsterPointer() && NF_EnemyActivateAIEvent( pEntity, this, pActivator ))
			break;
	}
}

// retail 0x42014AD0 (type 6): when the player is visible from the vent, send
// a character within the radius there
void CAIEvent::DetectEnemyThink( void )
{
	CBaseEntity *pPlayer = UTIL_PlayerByIndex( 1 );
	if( m_flNextValidTime <= gpGlobals->time && pPlayer && pPlayer->IsAlive())
	{
		TraceResult tr;
		UTIL_TraceLine( pev->origin + Vector( 0, 0, 8 ), pPlayer->pev->origin, dont_ignore_monsters, ENT( pev ), &tr );
		if( tr.flFraction == 1.0f || tr.pHit == pPlayer->edict())
			Trigger( pPlayer );
	}
	pev->nextthink = gpGlobals->time + 0.1f;
}

BOOL CAIEvent::InViewCone( const Vector &vecSpot )
{
	UTIL_MakeVectors( pev->angles );
	Vector2D vec2LOS = ( vecSpot - pev->origin ).Make2D().Normalize();
	return DotProduct( vec2LOS, gpGlobals->v_forward.Make2D()) > 0.7f;
}

void CAIEvent::SequenceDone( CBaseEntity *pCharacter )
{
	if( NF_DEBUG( NF_DBG_MONSTERS ))
		ALERT( at_console, "nf_debug: aievent type %d '%s' done by '%s', fires '%s'\n", m_iEventType, STRING( m_iszPlay ),
			STRING( pCharacter->pev->targetname ), STRING( pev->target ));
	SUB_UseTargets( NULL, USE_TOGGLE, 0 );
}
