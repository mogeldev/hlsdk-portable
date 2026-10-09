/*
nf_pathtruck.cpp - Nightfire's target-linked truck and passenger collision.
Retail behavior/addresses: project docs/retail/pathtruck.md.
The BSP pusher is authoritative in the port: a blocked passenger must not
leave the visible truck driving away from its collision model.
*/
#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "nf_debug.h"

class CNFPathTruck : public CBaseAnimating
{
public:
	void Spawn( void );
	void Precache( void );
	void KeyValue( KeyValueData *pkvd );
	void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );
	void UpdateOnRemove( void );
	void EXPORT TruckThink( void );
	void ClipBlocked( CBaseEntity *pOther );
	int Save( CSave &save );
	int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];

private:
	BOOL LinkClip( void );
	BOOL FindGoal( void );
	void SetGoal( CBaseEntity *pGoal );
	void Stop( void );
	void Animate( const char *name );
	void UpdateSound( void );
	void Move( void );
	void TerrainPitch( void );

	string_t m_iszClipModel;
	EHANDLE m_hClip;
	EHANDLE m_hGoal;
	EHANDLE m_hPrevious;
	BOOL m_bActive;
	BOOL m_bStopped;
	BOOL m_bFinished;
	BOOL m_bReverse;
	Vector m_vecVelocity;
	float m_flVehicleSpeed;
	float m_flBlockedUntil;
	float m_flThinkTime;
	int m_iPitch;
	BOOL m_bSound;
};

class CNFPathTruckClip : public CBaseEntity
{
public:
	void Spawn( void );
	void Blocked( CBaseEntity *pOther );
	void EXPORT WaitThink( void );
};

LINK_ENTITY_TO_CLASS( npc_pathtruck, CNFPathTruck )
LINK_ENTITY_TO_CLASS( func_pathtruck_clip, CNFPathTruckClip )

TYPEDESCRIPTION CNFPathTruck::m_SaveData[] =
{
	DEFINE_FIELD( CNFPathTruck, m_iszClipModel, FIELD_STRING ),
	DEFINE_FIELD( CNFPathTruck, m_hClip, FIELD_EHANDLE ),
	DEFINE_FIELD( CNFPathTruck, m_hGoal, FIELD_EHANDLE ),
	DEFINE_FIELD( CNFPathTruck, m_hPrevious, FIELD_EHANDLE ),
	DEFINE_FIELD( CNFPathTruck, m_bActive, FIELD_BOOLEAN ),
	DEFINE_FIELD( CNFPathTruck, m_bStopped, FIELD_BOOLEAN ),
	DEFINE_FIELD( CNFPathTruck, m_bFinished, FIELD_BOOLEAN ),
	DEFINE_FIELD( CNFPathTruck, m_bReverse, FIELD_BOOLEAN ),
	DEFINE_FIELD( CNFPathTruck, m_vecVelocity, FIELD_VECTOR ),
	DEFINE_FIELD( CNFPathTruck, m_flVehicleSpeed, FIELD_FLOAT ),
	DEFINE_FIELD( CNFPathTruck, m_flBlockedUntil, FIELD_TIME ),
	DEFINE_FIELD( CNFPathTruck, m_flThinkTime, FIELD_TIME ),
	DEFINE_FIELD( CNFPathTruck, m_iPitch, FIELD_INTEGER ),
};

int CNFPathTruck::Save( CSave &save )
{
	if( !CBaseAnimating::Save( save )) return 0;
	return save.WriteFields( "CNFPathTruck", this, m_SaveData, ARRAYSIZE( m_SaveData ));
}

int CNFPathTruck::Restore( CRestore &restore )
{
	if( !CBaseAnimating::Restore( restore )) return 0;
	int result = restore.ReadFields( "CNFPathTruck", this, m_SaveData, ARRAYSIZE( m_SaveData ));
	m_bSound = FALSE;
	if( NF_DEBUG( NF_DBG_VEHICLES ))
		ALERT( at_console, "nf_debug: truck %s restored active %d stopped %d finished %d goal %s\n",
			STRING( pev->targetname ), m_bActive, m_bStopped, m_bFinished,
			m_hGoal ? STRING( ((CBaseEntity *)m_hGoal)->pev->targetname ) : "-" );
	return result;
}

void CNFPathTruckClip::Spawn( void )
{
	pev->solid = SOLID_BSP;
	pev->movetype = MOVETYPE_PUSH;
	SET_MODEL( edict(), STRING( pev->model ));
	UTIL_SetSize( pev, pev->mins, pev->maxs );
	UTIL_SetOrigin( pev, pev->origin );
	SetThink( &CNFPathTruckClip::WaitThink );
	pev->nextthink = pev->ltime + 1;
}

void CNFPathTruckClip::WaitThink( void )
{
	pev->nextthink = pev->ltime + 10;
}

void CNFPathTruckClip::Blocked( CBaseEntity *pOther )
{
	CBaseEntity *pOwner = CBaseEntity::Instance( pev->owner );
	if( pOwner && FClassnameIs( pOwner->pev, "npc_pathtruck" ))
		((CNFPathTruck *)pOwner)->ClipBlocked( pOther );
}

void CNFPathTruck::KeyValue( KeyValueData *pkvd )
{
	if( FStrEq( pkvd->szKeyName, "clip_model" ))
	{
		m_iszClipModel = pkvd->szValue[0] ? ALLOC_STRING( pkvd->szValue ) : 0;
		pkvd->fHandled = TRUE;
	}
	else CBaseAnimating::KeyValue( pkvd );
}

void CNFPathTruck::Precache( void )
{
	if( !FStringNull( pev->model )) PRECACHE_MODEL( STRING( pev->model ));
	PRECACHE_SOUND( "common/truck1.wav" );
}

void CNFPathTruck::Spawn( void )
{
	Precache();
	if( FStringNull( pev->model ))
	{
		ALERT( at_error, "npc_pathtruck without model\n" );
		UTIL_Remove( this );
		return;
	}
	SET_MODEL( edict(), STRING( pev->model ));
	pev->solid = SOLID_NOT;
	pev->movetype = MOVETYPE_NOCLIP;
	pev->health = 1000;
	UTIL_SetSize( pev, Vector( -1, -1, 0 ), Vector( 1, 1, 1 ));
	pev->origin.z += 20;
	UTIL_SetOrigin( pev, pev->origin );
	m_bActive = FStringNull( pev->targetname );
	m_bStopped = !m_bActive;
	m_flVehicleSpeed = 100;
	m_iPitch = 50;
	Animate( "idle" );
	SetThink( &CNFPathTruck::TruckThink );
	pev->nextthink = gpGlobals->time + 0.1f;
	m_flThinkTime = gpGlobals->time;
}

BOOL CNFPathTruck::LinkClip( void )
{
	if( m_hClip ) return TRUE;
	CBaseEntity *pClip = FStringNull( m_iszClipModel ) ? NULL :
		UTIL_FindEntityByTargetname( NULL, STRING( m_iszClipModel ));
	if( !pClip || !FClassnameIs( pClip->pev, "func_pathtruck_clip" )) return FALSE;
	if( pClip->pev->owner && pClip->pev->owner != edict() ) return FALSE;
	m_hClip = pClip;
	pClip->pev->owner = edict();
	pClip->pev->angles = pev->angles;
	UTIL_SetOrigin( pClip->pev, pev->origin );
	if( NF_DEBUG( NF_DBG_VEHICLES ))
		ALERT( at_console, "nf_debug: truck %s linked clip %s at %.1f %.1f %.1f\n",
			STRING( pev->targetname ), STRING( pClip->pev->targetname ), pev->origin.x, pev->origin.y, pev->origin.z );
	return TRUE;
}

BOOL CNFPathTruck::FindGoal( void )
{
	if( m_hGoal ) return TRUE;
	if( m_bFinished || FStringNull( pev->target )) return FALSE;
	CBaseEntity *pGoal = UTIL_FindEntityByTargetname( NULL, STRING( pev->target ));
	if( !pGoal || !FClassnameIs( pGoal->pev, "path_corner" )) return FALSE;
	SetGoal( pGoal );
	return TRUE;
}

void CNFPathTruck::SetGoal( CBaseEntity *pGoal )
{
	m_hGoal = pGoal;
	m_bReverse = (pGoal->pev->spawnflags & 16) != 0;
	m_flVehicleSpeed = pGoal->pev->speed > 0 ? pGoal->pev->speed : 100;
	Vector start = m_hPrevious ? ((CBaseEntity *)m_hPrevious)->pev->origin + Vector( 0, 0, 20 ) : pev->origin;
	Vector direction = pGoal->pev->origin + Vector( 0, 0, 20 ) - start;
	if( direction.Length2D() > 0.01f )
		pev->ideal_yaw = UTIL_VecToYaw( direction ) + (m_bReverse ? 180 : 0);
	Animate( m_bReverse ? "reverse" : "driving" );
	if( NF_DEBUG( NF_DBG_VEHICLES ))
		ALERT( at_console, "nf_debug: truck %s goal %s speed %.1f reverse %d\n",
			STRING( pev->targetname ), STRING( pGoal->pev->targetname ), m_flVehicleSpeed, m_bReverse );
}

void CNFPathTruck::Animate( const char *name )
{
	int sequence = LookupSequence( name );
	if( sequence < 0 ) sequence = 0;
	if( pev->sequence == sequence && m_flFrameRate != 0 ) return;
	pev->sequence = sequence;
	pev->frame = 0;
	ResetSequenceInfo();
}

void CNFPathTruck::Stop( void )
{
	m_vecVelocity = g_vecZero;
	pev->velocity = pev->avelocity = g_vecZero;
	if( m_hClip )
	{
		CBaseEntity *pClip = m_hClip;
		pClip->pev->velocity = pClip->pev->avelocity = g_vecZero;
	}
	Animate( "idle" );
}

void CNFPathTruck::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	if( m_bFinished ) return;
	if( !m_bActive )
	{
		if( useType == USE_OFF ) return;
		m_bActive = TRUE;
		m_bStopped = FALSE;
		FindGoal();
	}
	else if( m_bStopped )
	{
		if( useType != USE_ON && useType != USE_TOGGLE ) return;
		m_bStopped = FALSE;
		if( m_hGoal ) SetGoal( m_hGoal );
	}
	else
	{
		if( useType != USE_TOGGLE ) return;
		m_bStopped = TRUE;
		Stop();
	}
	if( NF_DEBUG( NF_DBG_VEHICLES ))
		ALERT( at_console, "nf_debug: truck %s use %d active %d stopped %d\n",
			STRING( pev->targetname ), useType, m_bActive, m_bStopped );
}

void CNFPathTruck::ClipBlocked( CBaseEntity *pOther )
{
	// GoldSrc has no retail BlockedTest callback. Keep collision enabled and
	// pause the mover instead of separating its model and passenger hull.
	if( gpGlobals->time >= m_flBlockedUntil && NF_DEBUG( NF_DBG_VEHICLES ))
		ALERT( at_console, "nf_debug: truck %s blocked by %s; retry in 5s\n",
			STRING( pev->targetname ), STRING( pOther->pev->classname ));
	m_flBlockedUntil = gpGlobals->time + 5;
	Stop();
}

void CNFPathTruck::UpdateSound( void )
{
	if( !m_bActive ) return;
	int desired = !m_bStopped && !m_bFinished && gpGlobals->time >= m_flBlockedUntil ? 150 : 50;
	int oldPitch = m_iPitch;
	if( m_iPitch < desired ) m_iPitch = min( m_iPitch + 15, desired );
	else if( m_iPitch > desired ) m_iPitch = max( m_iPitch - 15, desired );
	if( m_bSound && m_iPitch == oldPitch ) return;
	EMIT_SOUND_DYN( edict(), CHAN_STATIC, "common/truck1.wav", desired == 150 ? 1.0f : 0.4f,
		0.3f, m_bSound ? SND_CHANGE_PITCH | SND_CHANGE_VOL : 0, m_iPitch );
	m_bSound = TRUE;
}

void CNFPathTruck::Move( void )
{
	CBaseEntity *pClip = m_hClip;
	// Limit coincident/cyclic routes so malformed content cannot hang a frame.
	for( int advances = 0; advances < 16 && m_hGoal; ++advances )
	{
		CBaseEntity *pGoal = m_hGoal;
		Vector delta = pGoal->pev->origin + Vector( 0, 0, 20 ) - pev->origin;
		CBaseEntity *pNext = pGoal->GetNextTarget();
		float threshold = pNext ? 32.0f : m_flVehicleSpeed * 0.2f;
		if( delta.Length() > threshold )
		{
			float yawSpeed = pGoal->pev->yaw_speed > 0 ? pGoal->pev->yaw_speed : 5;
			float yawDelta = UTIL_AngleDistance( pev->ideal_yaw, pev->angles.y );
			pev->angles.y += max( -yawSpeed * 0.5f, min( yawDelta, yawSpeed * 0.5f ));
			m_vecVelocity = m_vecVelocity * 0.8f + delta.Normalize() * (m_flVehicleSpeed * 0.2f);
			pClip->pev->velocity = m_vecVelocity;
			pClip->pev->avelocity.y = 10 * UTIL_AngleDistance( pev->angles.y, pClip->pev->angles.y );
			TerrainPitch();
			Animate( m_bReverse ? "reverse" : "driving" );
			return;
		}
		pev->ideal_yaw = pGoal->pev->angles.y;
		EHANDLE hGoal, hNext;
		hGoal = pGoal;
		hNext = pNext;
		if( NF_DEBUG( NF_DBG_VEHICLES ))
			ALERT( at_console, "nf_debug: truck %s reached %s output %s at %.1f %.1f %.1f\n",
				STRING( pev->targetname ), STRING( pGoal->pev->targetname ),
				FStringNull( pGoal->pev->message ) ? "-" : STRING( pGoal->pev->message ),
				pev->origin.x, pev->origin.y, pev->origin.z );
		if( !FStringNull( pGoal->pev->message ))
			FireTargets( STRING( pGoal->pev->message ), this, pGoal, USE_TOGGLE, 0 );
		if( pev->flags & FL_KILLME ) return;
		m_hPrevious = hGoal;
		m_hGoal = hNext;
		if( !m_hGoal )
		{
			m_bFinished = TRUE;
			Stop();
			return;
		}
		if( !FClassnameIs( ((CBaseEntity *)m_hGoal)->pev, "path_corner" ))
		{
			m_hGoal = NULL;
			m_bFinished = TRUE;
			Stop();
			return;
		}
		SetGoal( m_hGoal );
		if( m_bStopped ) { Stop(); return; }
	}
	Stop();
}

void CNFPathTruck::TerrainPitch( void )
{
	Vector points[4], angles;
	for( int i = 0; i < 4; ++i )
	{
		GetAttachment( i, points[i], angles );
		TraceResult tr;
		UTIL_TraceLine( points[i] + Vector( 0, 0, 32 ), points[i] - Vector( 0, 0, 64 ),
			ignore_monsters, edict(), &tr );
		if( tr.flFraction < 1 ) points[i] = tr.vecEndPos;
	}
	Vector direction = points[0] - points[2];
	if( direction.Length2D() < 0.01f ) return;
	angles = UTIL_VecToAngles( direction );
	pev->angles.x = UTIL_AngleDistance( -angles.x, 0 );
	// Retail uses the unnegated probe pitch for the collision model's servo.
	((CBaseEntity *)m_hClip)->pev->avelocity.x =
		UTIL_AngleDistance( angles.x, ((CBaseEntity *)m_hClip)->pev->angles.x );
}

void CNFPathTruck::TruckThink( void )
{
	pev->nextthink = gpGlobals->time + 0.1f;
	float interval = max( 0.0f, min( gpGlobals->time - m_flThinkTime, 0.2f ));
	m_flThinkTime = gpGlobals->time;
	StudioFrameAdvance( interval );
	if( !LinkClip() )
	{
		pev->velocity = pev->avelocity = g_vecZero;
		return;
	}
	CBaseEntity *pClip = m_hClip;
	UTIL_SetOrigin( pev, pClip->pev->origin );
	pev->angles.y = pClip->pev->angles.y;
	if( m_bActive && !m_bStopped && !m_bFinished && gpGlobals->time >= m_flBlockedUntil && FindGoal() ) Move();
	else Stop();
	// The non-solid studio entity also integrates between 10-Hz decisions;
	// the pusher remains authoritative when movement is rolled back.
	pev->velocity = pClip->pev->velocity;
	pev->avelocity.y = pClip->pev->avelocity.y;
	UpdateSound();
}

void CNFPathTruck::UpdateOnRemove( void )
{
	Stop();
	if( m_hClip ) UTIL_Remove( m_hClip );
	if( m_bSound ) STOP_SOUND( edict(), CHAN_STATIC, "common/truck1.wav" );
	CBaseAnimating::UpdateOnRemove();
}
