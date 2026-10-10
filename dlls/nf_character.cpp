/*
Nightfire trigger_changecharacter. Retail applies to the first named character,
keeps non-positive patrol/sight settings, and replaces nonzero spawnflags.
Original-code details and unsupported NPC metadata: docs/retail/characters.md.
*/
#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "nf_character.h"
#include "nf_debug.h"

class CNFChangeCharacter : public CPointEntity
{
public:
	void KeyValue( KeyValueData *pkvd );
	void Use( CBaseEntity *activator, CBaseEntity *caller, USE_TYPE type, float value );
	int Save( CSave &save );
	int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];
	nf_character_change_t m_change;
};

LINK_ENTITY_TO_CLASS( trigger_changecharacter, CNFChangeCharacter )

TYPEDESCRIPTION CNFChangeCharacter::m_SaveData[] =
{
	DEFINE_FIELD( CNFChangeCharacter, m_change.triggerTarget, FIELD_STRING ),
	DEFINE_FIELD( CNFChangeCharacter, m_change.triggerCondition, FIELD_INTEGER ),
	DEFINE_FIELD( CNFChangeCharacter, m_change.minPatrol, FIELD_FLOAT ),
	DEFINE_FIELD( CNFChangeCharacter, m_change.maxPatrol, FIELD_FLOAT ),
	DEFINE_FIELD( CNFChangeCharacter, m_change.maxPath, FIELD_FLOAT ),
	DEFINE_FIELD( CNFChangeCharacter, m_change.waitPatrol, FIELD_FLOAT ),
	DEFINE_FIELD( CNFChangeCharacter, m_change.deathCamera, FIELD_STRING ),
	DEFINE_FIELD( CNFChangeCharacter, m_change.rescueTarget, FIELD_STRING ),
	DEFINE_FIELD( CNFChangeCharacter, m_change.excludeEvents, FIELD_INTEGER ),
	DEFINE_FIELD( CNFChangeCharacter, m_change.cameraTarget, FIELD_STRING ),
	DEFINE_FIELD( CNFChangeCharacter, m_change.deathTarget, FIELD_STRING ),
	DEFINE_FIELD( CNFChangeCharacter, m_change.sight, FIELD_FLOAT ),
	DEFINE_FIELD( CNFChangeCharacter, m_change.initEvent, FIELD_INTEGER ),
	DEFINE_FIELD( CNFChangeCharacter, m_change.primaryWeapon, FIELD_INTEGER ),
	DEFINE_FIELD( CNFChangeCharacter, m_change.secondaryWeapon, FIELD_INTEGER ),
	DEFINE_FIELD( CNFChangeCharacter, m_change.gunIndex, FIELD_INTEGER ),
};

IMPLEMENT_SAVERESTORE( CNFChangeCharacter, CPointEntity )

void CNFChangeCharacter::KeyValue( KeyValueData *pkvd )
{
	const char *key = pkvd->szKeyName, *value = pkvd->szValue;
	if( FStrEq( key, "TriggerTarget" )) m_change.triggerTarget = ALLOC_STRING( value );
	else if( FStrEq( key, "TriggerCondition" )) m_change.triggerCondition = atoi( value );
	else if( FStrEq( key, "minpatroldist" )) m_change.minPatrol = atof( value );
	else if( FStrEq( key, "maxpatroldist" )) m_change.maxPatrol = atof( value );
	else if( FStrEq( key, "maxpatrolpath" )) m_change.maxPath = atof( value );
	else if( FStrEq( key, "waitpatroltime" )) m_change.waitPatrol = atof( value );
	else if( FStrEq( key, "deathcam" )) m_change.deathCamera = ALLOC_STRING( value );
	else if( FStrEq( key, "rescuetarget" )) m_change.rescueTarget = ALLOC_STRING( value );
	else if( FStrEq( key, "excludeaievents" )) m_change.excludeEvents = atoi( value );
	else if( FStrEq( key, "cameratarget" )) m_change.cameraTarget = ALLOC_STRING( value );
	else if( FStrEq( key, "deathtarget" )) m_change.deathTarget = ALLOC_STRING( value );
	else if( FStrEq( key, "sight_dist" )) m_change.sight = atof( value );
	else if( FStrEq( key, "initeventid" )) m_change.initEvent = atoi( value );
	else if( FStrEq( key, "primary_weapon" )) m_change.primaryWeapon = atoi( value );
	else if( FStrEq( key, "secondary_weapon" )) m_change.secondaryWeapon = atoi( value );
	else if( FStrEq( key, "gun_index" )) m_change.gunIndex = atoi( value );
	else if( FStrEq( key, "hostagegroup" )) pev->netname = ALLOC_STRING( value );
	else
	{
		CPointEntity::KeyValue( pkvd );
		return;
	}
	pkvd->fHandled = TRUE;
}

void CNFChangeCharacter::Use( CBaseEntity *activator, CBaseEntity *caller, USE_TYPE type, float value )
{
	if( FStringNull( pev->target )) return;
	CBaseEntity *entity = UTIL_FindEntityByTargetname( NULL, STRING( pev->target ));
	m_change.spawnflags = pev->spawnflags;
	m_change.hostageGroup = pev->netname;
	BOOL applied = entity && NF_EnemyChangeCharacter( entity, m_change );
	if( NF_DEBUG( NF_DBG_MONSTERS ))
		ALERT( at_console, "nf_debug: changecharacter %s target %s applied %d\n",
			STRING( pev->targetname ), STRING( pev->target ), applied );
}
