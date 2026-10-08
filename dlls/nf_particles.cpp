/*
nf_particles.cpp - James Bond 007: Nightfire (PC) particle emitter

particle_emitter
	Retail CParticleEmitter (game.dll, KeyValue 0x4208CE80, SpewThink
	0x4208D140, UseToggle 0x4208D660, Spawn 0x4208D6A0; docs/retail/
	particles.md). A hidden point entity that, while on, sends "Particles"
	(MSG_PVS) every frequency / 10 s; the client (cl_dll/nf_particles.cpp)
	spawns and simulates the particles. All keys but particle_gravity are
	read with atoi, so ".5" is 0 (frequency 0 = one burst, then off).
	Spawnflag 32 starts it on; use toggles it. The particles start at the
	target_origin entity ("none" = the emitter) and fly towards the
	target_direction entity ("player" = the local player).
*/

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "nf_debug.h"

extern int gmsgNFParticles;

#define SF_PARTICLE_START_ON	32

class CParticleEmitter : public CPointEntity
{
public:
	void Spawn( void );
	void Precache( void );
	void KeyValue( KeyValueData *pkvd );
	void EXPORT SpewThink( void );
	void EXPORT UseToggle( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );

	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];

	string_t m_iszOrigin;		// target_origin
	string_t m_iszDirection;	// target_direction
	float m_flSpeed;		// particle_speed
	int m_iNoise;			// particle_noise
	int m_iCount;			// particle_count
	int m_iRndCount;		// rnd_count
	float m_flFrequency;		// frequency (tenths of a second)
	string_t m_iszTexture;		// particle_texture
	int m_iSprite;
	float m_flGravity;		// particle_gravity (atof)
	int m_iType;			// particle_type (only 0 exists)
	float m_flScale;		// particle_scale
	float m_flScaleSpeed;		// scale_speed
	float m_flFadeSpeed;		// fade_speed
	float m_flLife;			// particle_life (tenths of a second)
	BOOL m_bOn;
	int m_iPreciseFade;		// preciseFade
};

LINK_ENTITY_TO_CLASS( particle_emitter, CParticleEmitter )

TYPEDESCRIPTION CParticleEmitter::m_SaveData[] =
{
	DEFINE_FIELD( CParticleEmitter, m_iszOrigin, FIELD_STRING ),
	DEFINE_FIELD( CParticleEmitter, m_iszDirection, FIELD_STRING ),
	DEFINE_FIELD( CParticleEmitter, m_flSpeed, FIELD_FLOAT ),
	DEFINE_FIELD( CParticleEmitter, m_iNoise, FIELD_INTEGER ),
	DEFINE_FIELD( CParticleEmitter, m_iCount, FIELD_INTEGER ),
	DEFINE_FIELD( CParticleEmitter, m_iRndCount, FIELD_INTEGER ),
	DEFINE_FIELD( CParticleEmitter, m_flFrequency, FIELD_FLOAT ),
	DEFINE_FIELD( CParticleEmitter, m_iszTexture, FIELD_STRING ),
	DEFINE_FIELD( CParticleEmitter, m_flGravity, FIELD_FLOAT ),
	DEFINE_FIELD( CParticleEmitter, m_iType, FIELD_INTEGER ),
	DEFINE_FIELD( CParticleEmitter, m_flScale, FIELD_FLOAT ),
	DEFINE_FIELD( CParticleEmitter, m_flScaleSpeed, FIELD_FLOAT ),
	DEFINE_FIELD( CParticleEmitter, m_flFadeSpeed, FIELD_FLOAT ),
	DEFINE_FIELD( CParticleEmitter, m_flLife, FIELD_FLOAT ),
	DEFINE_FIELD( CParticleEmitter, m_bOn, FIELD_BOOLEAN ),
	DEFINE_FIELD( CParticleEmitter, m_iPreciseFade, FIELD_INTEGER ),
};

IMPLEMENT_SAVERESTORE( CParticleEmitter, CPointEntity )

void CParticleEmitter::KeyValue( KeyValueData *pkvd )
{
	if( FStrEq( pkvd->szKeyName, "target_origin" ))
		m_iszOrigin = ALLOC_STRING( pkvd->szValue );
	else if( FStrEq( pkvd->szKeyName, "target_direction" ))
		m_iszDirection = ALLOC_STRING( pkvd->szValue );
	else if( FStrEq( pkvd->szKeyName, "particle_speed" ))
		m_flSpeed = (float)atoi( pkvd->szValue );
	else if( FStrEq( pkvd->szKeyName, "particle_noise" ))
		m_iNoise = atoi( pkvd->szValue );
	else if( FStrEq( pkvd->szKeyName, "particle_count" ))
		m_iCount = atoi( pkvd->szValue );
	else if( FStrEq( pkvd->szKeyName, "rnd_count" ))
		m_iRndCount = atoi( pkvd->szValue );
	else if( FStrEq( pkvd->szKeyName, "frequency" ))
		m_flFrequency = (float)atoi( pkvd->szValue );
	else if( FStrEq( pkvd->szKeyName, "particle_texture" ))
		m_iszTexture = ALLOC_STRING( pkvd->szValue );
	else if( FStrEq( pkvd->szKeyName, "particle_gravity" ))
		m_flGravity = atof( pkvd->szValue );
	else if( FStrEq( pkvd->szKeyName, "particle_type" ))
		m_iType = atoi( pkvd->szValue );
	else if( FStrEq( pkvd->szKeyName, "particle_scale" ))
		m_flScale = (float)atoi( pkvd->szValue );
	else if( FStrEq( pkvd->szKeyName, "scale_speed" ))
		m_flScaleSpeed = (float)atoi( pkvd->szValue );
	else if( FStrEq( pkvd->szKeyName, "fade_speed" ))
		m_flFadeSpeed = (float)atoi( pkvd->szValue );
	else if( FStrEq( pkvd->szKeyName, "particle_life" ))
		m_flLife = (float)atoi( pkvd->szValue );
	else if( FStrEq( pkvd->szKeyName, "particle_avelocity" ))
	{
		// retail: only a non-zero vector replaces pev->avelocity
		Vector avel;
		UTIL_StringToVector( avel, pkvd->szValue );
		if( avel != g_vecZero )
			pev->avelocity = avel;
	}
	else if( FStrEq( pkvd->szKeyName, "preciseFade" ))
		m_iPreciseFade = atoi( pkvd->szValue );
	else
	{
		CPointEntity::KeyValue( pkvd );
		return;
	}
	pkvd->fHandled = TRUE;
}

void CParticleEmitter::Precache( void )
{
	if( m_iszTexture )
		m_iSprite = PRECACHE_MODEL( STRING( m_iszTexture ));
}

void CParticleEmitter::Spawn( void )
{
	Precache();
	pev->solid = SOLID_NOT;
	pev->movetype = MOVETYPE_NONE;
	pev->effects |= EF_NODRAW;
	UTIL_SetOrigin( pev, pev->origin );
	UTIL_SetSize( pev, g_vecZero, g_vecZero );

	m_bOn = FALSE;
	SetUse( &CParticleEmitter::UseToggle );
	if( FBitSet( pev->spawnflags, SF_PARTICLE_START_ON ))
	{
		SetThink( &CParticleEmitter::SpewThink );
		m_bOn = TRUE;
		pev->nextthink = gpGlobals->time + 0.1f;
		if( NF_DEBUG( NF_DBG_EFFECTS ))
			ALERT( at_console, "nf_debug: particle_emitter %s starts on (count %d freq %.0f life %.0f %s)\n",
				STRING( pev->targetname ), m_iCount, m_flFrequency, m_flLife,
				m_iszTexture ? STRING( m_iszTexture ) : "-" );
	}
}

void CParticleEmitter::UseToggle( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	// retail ignores the use type: every use flips the emitter
	if( !m_bOn )
	{
		m_bOn = TRUE;
		SetThink( &CParticleEmitter::SpewThink );
		pev->nextthink = gpGlobals->time + m_flFrequency / 10.0f;
	}
	else
	{
		m_bOn = FALSE;
		SetThink( NULL );
	}

	if( NF_DEBUG( NF_DBG_EFFECTS ))
		ALERT( at_console, "nf_debug: particle_emitter %s %s (count %d freq %.0f life %.0f %s)\n",
			STRING( pev->targetname ), m_bOn ? "on" : "off", m_iCount, m_flFrequency, m_flLife,
			m_iszTexture ? STRING( m_iszTexture ) : "-" );
}

static CBaseEntity *NF_ParticleTarget( string_t name )
{
	if( FStrEq( STRING( name ), "player" ))
		return UTIL_PlayerByIndex( 1 );
	return UTIL_FindEntityByTargetname( NULL, STRING( name ));
}

static int NF_ToByte( float f )
{
	return (int)( f > 255.0f ? 255.0f : ( f < 0.0f ? 0.0f : f ));
}

void CParticleEmitter::SpewThink( void )
{
	if( !m_bOn )
	{
		SetThink( NULL );
		return;
	}

	Vector vecOrigin;
	if( FStrEq( STRING( m_iszOrigin ), "none" ))
		vecOrigin = pev->origin;
	else
	{
		CBaseEntity *pOrigin = UTIL_FindEntityByTargetname( NULL, STRING( m_iszOrigin ));
		if( !pOrigin )
		{
			ALERT( at_console, "ERROR!: CParticleEmitter BAD targetOrigin at: %f, %f, %f\n",
				pev->origin.x, pev->origin.y, pev->origin.z );
			return;
		}
		vecOrigin = pOrigin->pev->origin;
	}

	CBaseEntity *pDirection = NF_ParticleTarget( m_iszDirection );
	if( !pDirection )
	{
		ALERT( at_console, "ERROR!: CParticleEmitter BAD targetdirection at: %f, %f, %f\n",
			pev->origin.x, pev->origin.y, pev->origin.z );
		return;
	}
	Vector vecDir = ( pDirection->pev->origin - vecOrigin ).Normalize();
	int count = m_iCount + RANDOM_LONG( -m_iRndCount, m_iRndCount );

	// rendercolor 0 0 0 = white
	Vector color = pev->rendercolor;
	if( color == g_vecZero )
		color = Vector( 255, 255, 255 );

	MESSAGE_BEGIN( MSG_PVS, gmsgNFParticles, pev->origin );
		WRITE_COORD( vecOrigin.x );
		WRITE_COORD( vecOrigin.y );
		WRITE_COORD( vecOrigin.z );
		WRITE_COORD( vecDir.x );
		WRITE_COORD( vecDir.y );
		WRITE_COORD( vecDir.z );
		WRITE_SHORT( m_iSprite );
		WRITE_BYTE( count );
		WRITE_BYTE( (int)m_flSpeed );
		WRITE_BYTE( m_iNoise );
		WRITE_BYTE( pev->rendermode );
		WRITE_SHORT( (int)m_flGravity );	// retail truncates: ".1" is 0
		WRITE_BYTE( (int)m_flScale );
		WRITE_BYTE( (int)m_flScaleSpeed );
		WRITE_BYTE( (int)pev->renderamt );
		WRITE_BYTE( (int)m_flFadeSpeed );
		WRITE_BYTE( (int)m_flLife );
		WRITE_BYTE( m_iType );
		WRITE_BYTE( pev->spawnflags );
		WRITE_COORD( pev->angles.x );
		WRITE_COORD( pev->angles.y );
		WRITE_COORD( pev->angles.z );
		WRITE_COORD( pev->avelocity.x );
		WRITE_COORD( pev->avelocity.y );
		WRITE_COORD( pev->avelocity.z );
		WRITE_BYTE( NF_ToByte( color.x ));
		WRITE_BYTE( NF_ToByte( color.y ));
		WRITE_BYTE( NF_ToByte( color.z ));
		WRITE_BYTE( m_iPreciseFade );
	MESSAGE_END();

	if( m_flFrequency == 0.0f )
		m_bOn = FALSE;	// one burst
	else
		pev->nextthink = gpGlobals->time + m_flFrequency / 10.0f;
}
