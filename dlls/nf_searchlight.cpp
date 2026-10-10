/*
Nightfire searchlight patrol, alarm, investigation and destruction.
Retail addresses, formulas and transport differences: docs/retail/searchlight.md.
*/
#include <math.h>
#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "soundent.h"
#include "nf_debug.h"
#include "nf_searchlight.h"

class CNFSearchlightTarget : public CPointEntity
{
public:
	void Spawn( void ) { pev->solid = SOLID_NOT; pev->movetype = MOVETYPE_NONE; }
};

class CNFSearchlight : public CBaseAnimating
{
public:
	void Spawn( void );
	void Precache( void );
	void KeyValue( KeyValueData *pkvd );
	void Use( CBaseEntity *activator, CBaseEntity *caller, USE_TYPE type, float value );
	int TakeDamage( entvars_t *inflictor, entvars_t *attacker, float damage, int type );
	int BloodColor( void ) { return DONT_BLEED; }
	void EXPORT StartThink( void );
	void EXPORT SearchThink( void );
	void EXPORT DeathThink( void );
	int Save( CSave &save );
	int Restore( CRestore &restore );
	int ObjectCaps( void ) { return CBaseAnimating::ObjectCaps() & ~FCAP_ACROSS_TRANSITION; }
	static TYPEDESCRIPTION m_SaveData[];
	void Report( void );

private:
	void Aim( const Vector &position );
	void Move( float rate, BOOL patrol );
	BOOL Arrived( void ) { return m_pitch == m_idealPitch && m_yaw == m_idealYaw; }
	void NextPoint( BOOL first );
	void UpdateBeam( void );
	BOOL Sees( CBaseEntity *entity );
	void Alarm( CBaseEntity *target );
	void Investigate( const Vector &position, float rate );
	void Search( void );
	void Disable( void );
	void Sequence( const char *name );

	string_t m_sequence, m_alarmTarget, m_alarmSquad, m_brokenTarget;
	float m_alarmDistance, m_alarmAccuracy, m_moveRate;
	BOOL m_on, m_alarm, m_tracking, m_heard;
	EHANDLE m_goal;
	Vector m_area, m_lastPosition;
	float m_pitch, m_yaw, m_idealPitch, m_idealYaw, m_rateAdjust, m_lastSeen;
	int m_phase;
};

LINK_ENTITY_TO_CLASS( enemy_searchlight, CNFSearchlight )
LINK_ENTITY_TO_CLASS( enemy_searchlight_target, CNFSearchlightTarget )

TYPEDESCRIPTION CNFSearchlight::m_SaveData[] =
{
	DEFINE_FIELD( CNFSearchlight, m_sequence, FIELD_STRING ),
	DEFINE_FIELD( CNFSearchlight, m_alarmTarget, FIELD_STRING ),
	DEFINE_FIELD( CNFSearchlight, m_alarmSquad, FIELD_STRING ),
	DEFINE_FIELD( CNFSearchlight, m_brokenTarget, FIELD_STRING ),
	DEFINE_FIELD( CNFSearchlight, m_alarmDistance, FIELD_FLOAT ),
	DEFINE_FIELD( CNFSearchlight, m_alarmAccuracy, FIELD_FLOAT ),
	DEFINE_FIELD( CNFSearchlight, m_moveRate, FIELD_FLOAT ),
	DEFINE_FIELD( CNFSearchlight, m_on, FIELD_BOOLEAN ),
	DEFINE_FIELD( CNFSearchlight, m_alarm, FIELD_BOOLEAN ),
	DEFINE_FIELD( CNFSearchlight, m_tracking, FIELD_BOOLEAN ),
	DEFINE_FIELD( CNFSearchlight, m_heard, FIELD_BOOLEAN ),
	DEFINE_FIELD( CNFSearchlight, m_goal, FIELD_EHANDLE ),
	DEFINE_FIELD( CNFSearchlight, m_area, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( CNFSearchlight, m_lastPosition, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( CNFSearchlight, m_pitch, FIELD_FLOAT ),
	DEFINE_FIELD( CNFSearchlight, m_yaw, FIELD_FLOAT ),
	DEFINE_FIELD( CNFSearchlight, m_idealPitch, FIELD_FLOAT ),
	DEFINE_FIELD( CNFSearchlight, m_idealYaw, FIELD_FLOAT ),
	DEFINE_FIELD( CNFSearchlight, m_rateAdjust, FIELD_FLOAT ),
	DEFINE_FIELD( CNFSearchlight, m_lastSeen, FIELD_TIME ),
	DEFINE_FIELD( CNFSearchlight, m_phase, FIELD_INTEGER ),
};

int CNFSearchlight::Save( CSave &save )
{
	return CBaseAnimating::Save( save ) && save.WriteFields( "CNFSearchlight", this, m_SaveData, ARRAYSIZE( m_SaveData ));
}

int CNFSearchlight::Restore( CRestore &restore )
{
	if( !CBaseAnimating::Restore( restore )) return 0;
	int result = restore.ReadFields( "CNFSearchlight", this, m_SaveData, ARRAYSIZE( m_SaveData ));
	UpdateBeam();
	if( NF_DEBUG( NF_DBG_MONSTERS )) { ALERT( at_console, "nf_debug: searchlight restored " ); Report(); }
	return result;
}

void CNFSearchlight::KeyValue( KeyValueData *pkvd )
{
	const char *key = pkvd->szKeyName, *value = pkvd->szValue;
	if( FStrEq( key, "sequencename" )) m_sequence = ALLOC_STRING( value );
	else if( FStrEq( key, "alarmtarget" )) m_alarmTarget = ALLOC_STRING( value );
	else if( FStrEq( key, "alarmsquad" )) m_alarmSquad = ALLOC_STRING( value );
	else if( FStrEq( key, "brokentarget" )) m_brokenTarget = ALLOC_STRING( value );
	else if( FStrEq( key, "alarmdist" )) m_alarmDistance = atof( value );
	else if( FStrEq( key, "alarmaccuracy" )) m_alarmAccuracy = atof( value );
	else if( FStrEq( key, "moverate" )) m_moveRate = atof( value );
	else { CBaseAnimating::KeyValue( pkvd ); return; }
	pkvd->fHandled = TRUE;
}

void CNFSearchlight::Precache( void )
{
	PRECACHE_MODEL( "models/spotlight.mdl" );
	PRECACHE_MODEL( "sprites/spotlight_beam.spz" );
	PRECACHE_MODEL( "sprites/corona_spotlight.spz" );
	PRECACHE_SOUND( "explosions/spotlight_explode.wav" );
}

void CNFSearchlight::Sequence( const char *name )
{
	int sequence = LookupSequence( name );
	pev->sequence = sequence >= 0 ? sequence : 0;
	pev->frame = 0;
	ResetSequenceInfo();
}

void CNFSearchlight::Spawn( void )
{
	Precache();
	SET_MODEL( edict(), "models/spotlight.mdl" );
	pev->solid = SOLID_BBOX;
	pev->movetype = MOVETYPE_NONE;
	pev->flags |= FL_MONSTER;
	pev->health = pev->max_health = 1;
	pev->takedamage = DAMAGE_AIM;
	pev->deadflag = DEAD_NO;
	UTIL_SetSize( pev, Vector( -8, -8, 0 ), Vector( 8, 8, 16 ));
	UTIL_SetOrigin( pev, pev->origin );
	if( pev->spawnflags & 1 )
	{
		if( DROP_TO_FLOOR( edict()) == 0 ) { UTIL_Remove( this ); return; }
	}
	if( !( m_moveRate > 0 && m_moveRate <= 360 )) m_moveRate = 0.5f;
	m_pitch = m_idealPitch = 180;
	SetBoneController( 0, 0 );
	SetBoneController( 1, m_pitch );
	pev->iuser1 = NF_SEARCHLIGHT_MARKER;
	m_on = ( m_sequence || pev->target ) && !( pev->spawnflags & 2 );
	pev->skin = m_on ? 0 : 1;
	if( m_on ) { SetThink( &CNFSearchlight::StartThink ); pev->nextthink = gpGlobals->time + 1.1f; }
	if( NF_DEBUG( NF_DBG_MONSTERS ))
		ALERT( at_console, "nf_debug: searchlight spawn %s on %d flags %d path %s distance %.0f\n",
			STRING( pev->targetname ), m_on, pev->spawnflags, STRING( pev->target ), m_alarmDistance );
}

static float NF_SearchAngle( float angle )
{
	angle = fmodf( angle, 360.0f );
	if( angle > 180 ) angle -= 360;
	if( angle < -180 ) angle += 360;
	return angle;
}

void CNFSearchlight::Aim( const Vector &position )
{
	Vector origin, angles;
	GetAttachment( 0, origin, angles );
	Vector aim = UTIL_VecToAngles( position - origin );
	m_idealPitch = -NF_SearchAngle( aim.x );
	m_idealYaw = NF_SearchAngle( aim.y - pev->angles.y );
}

void CNFSearchlight::NextPoint( BOOL first )
{
	string_t next = !first && m_goal ? ((CBaseEntity *)m_goal)->pev->target : pev->target;
	CBaseEntity *point = next ? UTIL_FindEntityByTargetname( NULL, STRING( next )) : NULL;
	m_goal = point;
	if( !point ) return;
	Aim( point->pev->origin );
	if( first ) { m_pitch = m_idealPitch; m_yaw = m_idealYaw; }
	if( NF_DEBUG( NF_DBG_MONSTERS ))
		ALERT( at_console, "nf_debug: searchlight %s point %s pitch %.2f yaw %.2f\n",
			STRING( pev->targetname ), STRING( point->pev->targetname ), m_idealPitch, m_idealYaw );
}

void CNFSearchlight::Move( float rate, BOOL patrol )
{
	float pitch = fabsf( m_idealPitch - m_pitch ), yaw = fabsf( m_idealYaw - m_yaw );
	float major = Q_max( pitch, yaw );
	if( major <= 0 ) return;
	float pitchRate = pitch >= yaw ? rate : ( pitch / major ) * ( patrol ? 1.0f : rate );
	float yawRate = yaw >= pitch ? rate : ( yaw / major ) * ( patrol ? 1.0f : rate );
	m_pitch += ( m_idealPitch > m_pitch ? 1 : -1 ) * Q_min( pitch, pitchRate );
	m_yaw += ( m_idealYaw > m_yaw ? 1 : -1 ) * Q_min( yaw, yawRate );
	SetBoneController( 1, m_pitch );
	SetBoneController( 0, m_yaw );
}

void CNFSearchlight::UpdateBeam( void )
{
	pev->iuser1 = NF_SEARCHLIGHT_MARKER;
	pev->iuser2 = m_on && pev->deadflag == DEAD_NO && pev->skin == 0;
	Vector angles;
	GetAttachment( 0, pev->vuser1, angles );
	GetAttachment( 1, pev->vuser2, angles );
}

void CNFSearchlight::StartThink( void )
{
	SetBoneController( 0, 0 ); SetBoneController( 1, 0 );
	Sequence( m_sequence ? STRING( m_sequence ) : "idle" );
	if( pev->target )
	{
		NextPoint( TRUE );
		SetBoneController( 0, m_yaw ); SetBoneController( 1, m_pitch );
	}
	UpdateBeam();
	SetThink( &CNFSearchlight::SearchThink );
	pev->nextthink = gpGlobals->time + 0.1f;
	if( NF_DEBUG( NF_DBG_MONSTERS )) ALERT( at_console, "nf_debug: searchlight %s started\n", STRING( pev->targetname ));
}

BOOL CNFSearchlight::Sees( CBaseEntity *entity )
{
	if( !entity || ( entity->pev->flags & FL_NOTARGET )) return FALSE;
	Vector delta = entity->pev->origin - pev->origin;
	if( fabsf( delta.x ) > 4098 || fabsf( delta.y ) > 4098 || fabsf( delta.z ) > 4098 ) return FALSE;
	Vector direction = ( pev->vuser2 - pev->vuser1 ).Normalize();
	if( DotProduct( direction, delta.Normalize()) <= 0.98f ) return FALSE;
	return FVisible( entity );
}

void CNFSearchlight::Alarm( CBaseEntity *target )
{
	m_alarm = m_tracking = TRUE;
	m_heard = FALSE;
	m_goal = target;
	m_lastSeen = gpGlobals->time;
	m_lastPosition = target->pev->origin;
	m_phase = 0;
	if( m_alarmTarget ) FireTargets( STRING( m_alarmTarget ), this, this, USE_TOGGLE, 0 );
	if( m_alarmSquad )
	{
		CBaseEntity *guard = NULL;
		while(( guard = UTIL_FindEntityByString( guard, "netname", STRING( m_alarmSquad ))) != NULL )
			NF_EnemySearchlightAlarm( guard, target, m_alarmDistance );
	}
	CSoundEnt::InsertSound( bits_SOUND_COMBAT, target->pev->origin, 3072, 3 );
	if( NF_DEBUG( NF_DBG_MONSTERS ))
		ALERT( at_console, "nf_debug: searchlight %s alarm target %d output %s squad %s\n",
			STRING( pev->targetname ), target->entindex(), STRING( m_alarmTarget ), STRING( m_alarmSquad ));
}

void CNFSearchlight::Investigate( const Vector &position, float rate )
{
	m_heard = TRUE;
	m_area = m_lastPosition = position;
	m_lastSeen = gpGlobals->time;
	m_rateAdjust = rate;
	m_phase = 0;
	if( pev->netname )
	{
		CBaseEntity *entity = NULL;
		while(( entity = UTIL_FindEntityByString( entity, "netname", STRING( pev->netname ))) != NULL )
			if( entity != this && FClassnameIs( entity->pev, "enemy_searchlight" ))
			{
				CNFSearchlight *peer = (CNFSearchlight *)entity;
				if( !peer->m_on || peer->pev->deadflag != DEAD_NO ) continue;
				peer->m_heard = TRUE;
				peer->m_area = peer->m_lastPosition = position;
				peer->m_lastSeen = gpGlobals->time;
				peer->m_rateAdjust = rate; peer->m_phase = 0;
			}
	}
	if( NF_DEBUG( NF_DBG_MONSTERS ))
		ALERT( at_console, "nf_debug: searchlight %s investigate %.0f %.0f %.0f\n",
			STRING( pev->targetname ), position.x, position.y, position.z );
}

void CNFSearchlight::Search( void )
{
	CBaseEntity *player = UTIL_PlayerByIndex( 1 );
	if( !m_alarm && Sees( player ))
	{
		Alarm( player );
		if( pev->netname )
		{
			CBaseEntity *entity = NULL;
			while(( entity = UTIL_FindEntityByString( entity, "netname", STRING( pev->netname ))) != NULL )
				if( entity != this && FClassnameIs( entity->pev, "enemy_searchlight" ))
				{
					CNFSearchlight *peer = (CNFSearchlight *)entity;
					if( peer->m_on && !peer->m_alarm && peer->pev->deadflag == DEAD_NO ) peer->Alarm( player );
				}
		}
	}
	if( m_alarm && !m_heard && Sees( player ))
	{
		m_lastPosition = player->pev->origin;
		m_lastSeen = gpGlobals->time;
		m_phase = 0;
	}
	if( !m_alarm && !m_heard )
	{
		CBaseEntity *corpse = NULL;
		while(( corpse = UTIL_FindEntityByClassname( corpse, "enemy_generic" )) != NULL )
			if( !corpse->IsAlive() && !NF_EnemyCorpseSpotted( corpse, FALSE ) && Sees( corpse ))
			{
				NF_EnemyCorpseSpotted( corpse, TRUE );
				Investigate( corpse->pev->origin, 3 );
				break;
			}
	}
	if( !m_alarm && !m_heard )
	{
		for( int index = CSoundEnt::ActiveList(), count = 0; index != SOUNDLIST_EMPTY && count < MAX_WORLD_SOUNDS; count++ )
		{
			CSound *sound = CSoundEnt::SoundPointerForIndex( index );
			if( !sound ) break;
			if(( sound->m_iType & bits_SOUND_COMBAT ) && ( sound->m_vecOrigin - pev->origin ).Length() <= sound->m_iVolume * 1.5f )
			{
				Investigate( sound->m_vecOrigin, 3 );
				break;
			}
			index = sound->m_iNext;
		}
	}
	if(( m_alarm || m_heard ) && gpGlobals->time > m_lastSeen + 5 && ( Arrived() || ( pev->spawnflags & 4 )))
	{
		if( m_phase == 4 )
		{
			m_alarm = m_tracking = m_heard = FALSE;
			m_phase = 0; m_goal = NULL;
			NextPoint( TRUE );
			if( NF_DEBUG( NF_DBG_MONSTERS )) ALERT( at_console, "nf_debug: searchlight %s rearmed\n", STRING( pev->targetname ));
		}
		else
		{
			// Use a stable local basis rather than the original mutable global vectors.
			UTIL_MakeVectors( pev->angles );
			Vector offset = m_phase < 2 ? gpGlobals->v_right : gpGlobals->v_forward;
			m_area = m_lastPosition + offset * ( m_phase % 2 ? -256 : 256 );
			m_phase++; m_rateAdjust = 1; m_tracking = FALSE;
			Aim( m_area );
		}
	}
}

void CNFSearchlight::SearchThink( void )
{
	StudioFrameAdvance();
	if( m_fSequenceFinished && !m_fSequenceLoops ) ResetSequenceInfo();
	if(( m_heard || m_phase > 0 ) && !( pev->spawnflags & 4 )) { Aim( m_area ); Move( m_moveRate * m_rateAdjust, FALSE ); }
	else if( m_tracking && m_goal && !( pev->spawnflags & 4 )) { Aim( ((CBaseEntity *)m_goal)->pev->origin ); Move( 2, FALSE ); }
	else if( pev->target ) { if( Arrived()) NextPoint( FALSE ); Move( m_moveRate, TRUE ); }
	UpdateBeam();
	Search();
	pev->nextthink = gpGlobals->time + 0.05f;
}

void CNFSearchlight::Disable( void )
{
	m_on = FALSE; m_goal = NULL;
	pev->iuser2 = 0;
	pev->skin = 1;
	Sequence( "death" );
	SetThink( NULL ); pev->nextthink = 0;
}

void CNFSearchlight::Use( CBaseEntity *activator, CBaseEntity *caller, USE_TYPE type, float value )
{
	if( pev->deadflag == DEAD_DEAD || type == USE_SET ) return;
	if( type == USE_ON || ( type == USE_TOGGLE && !m_on ))
	{
		if( !m_on )
		{
			m_on = TRUE; pev->skin = 0;
			SetThink( &CNFSearchlight::StartThink ); pev->nextthink = gpGlobals->time + 1.1f;
		}
	}
	else Disable();
	if( NF_DEBUG( NF_DBG_MONSTERS )) ALERT( at_console, "nf_debug: searchlight %s use %d on %d\n", STRING( pev->targetname ), type, m_on );
}

int CNFSearchlight::TakeDamage( entvars_t *inflictor, entvars_t *attacker, float damage, int type )
{
	if( !m_on || pev->deadflag == DEAD_DEAD ) return 0;
	CBaseEntity *entity = CBaseEntity::Instance( attacker );
	if( entity && entity->IsPlayer())
	{
		if( m_alarmTarget ) FireTargets( STRING( m_alarmTarget ), this, this, USE_TOGGLE, 0 );
		if( m_brokenTarget ) FireTargets( STRING( m_brokenTarget ), this, this, USE_TOGGLE, 0 );
	}
	pev->deadflag = DEAD_DEAD;
	pev->takedamage = DAMAGE_NO;
	pev->solid = SOLID_NOT;
	pev->iuser2 = 0; pev->skin = 1;
	m_pitch = m_yaw = m_idealPitch = m_idealYaw = 0;
	SetBoneController( 0, 0 ); SetBoneController( 1, 0 );
	Sequence( "death" );
	SetThink( &CNFSearchlight::DeathThink ); pev->nextthink = gpGlobals->time + 0.05f;
	EMIT_SOUND( edict(), CHAN_BODY, "explosions/spotlight_explode.wav", 1, ATTN_NORM );
	UTIL_Sparks( pev->vuser1 );
	CSoundEnt::InsertSound( bits_SOUND_COMBAT, pev->origin, 4096, 5 );
	if( NF_DEBUG( NF_DBG_MONSTERS )) ALERT( at_console, "nf_debug: searchlight %s destroyed player %d broken %s\n",
		STRING( pev->targetname ), entity && entity->IsPlayer(), STRING( m_brokenTarget ));
	return 1;
}

void CNFSearchlight::DeathThink( void )
{
	StudioFrameAdvance();
	if( m_fSequenceFinished ) { SetThink( NULL ); pev->nextthink = 0; }
	else pev->nextthink = gpGlobals->time + 0.05f;
}

void CNFSearchlight::Report( void )
{
	ALERT( at_console, "nf_debug: searchlight %s on %d dead %d alarm %d track %d heard %d phase %d goal %s pitch %.2f yaw %.2f ideal %.2f %.2f beam %d start %.2f %.2f %.2f end %.2f %.2f %.2f\n",
		STRING( pev->targetname ), m_on, pev->deadflag, m_alarm, m_tracking, m_heard, m_phase,
		m_goal ? STRING( ((CBaseEntity *)m_goal)->pev->targetname ) : "-", m_pitch, m_yaw, m_idealPitch, m_idealYaw,
		pev->iuser2, pev->vuser1.x, pev->vuser1.y, pev->vuser1.z, pev->vuser2.x, pev->vuser2.y, pev->vuser2.z );
	ALERT( at_console, "nf_debug: searchlight %s clock %.3f next %.3f\n", STRING( pev->targetname ), gpGlobals->time, pev->nextthink );
	CBaseEntity *player = UTIL_PlayerByIndex( 1 );
	if( player )
		ALERT( at_console, "nf_debug: searchlight %s probe dot %.4f visible %d sees %d\n", STRING( pev->targetname ),
			DotProduct(( pev->vuser2 - pev->vuser1 ).Normalize(), ( player->pev->origin - pev->origin ).Normalize()),
			FVisible( player ), Sees( player ));
}

BOOL NF_SearchlightCommand( CBaseEntity *player, const char *command )
{
	if( !FStrEq( command, "nf_searchlightinfo" )) return FALSE;
	if( NF_DEBUG( NF_DBG_MONSTERS ))
	{
		CBaseEntity *entity = NULL;
		while(( entity = UTIL_FindEntityByClassname( entity, "enemy_searchlight" )) != NULL )
			if( CMD_ARGC() < 2 || FStrEq( STRING( entity->pev->targetname ), CMD_ARGV( 1 ))) ((CNFSearchlight *)entity)->Report();
	}
	return TRUE;
}
