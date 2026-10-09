/*
Nightfire rain: brush emission zones and the map's info_rain controller.
Retail findings and wire-format deviations: docs/retail/rain.md.
*/
#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "nf_env.h"
#include "nf_debug.h"

extern int gmsgNFRainInfo;
extern int gmsgNFRainZone;

class CRainZone : public CBaseEntity
{
public:
	void Spawn( void );
};

LINK_ENTITY_TO_CLASS( env_rain, CRainZone )

void CRainZone::Spawn( void )
{
	pev->solid = SOLID_NOT;
	pev->movetype = MOVETYPE_NONE;
	pev->effects |= EF_NODRAW;
	SET_MODEL( ENT( pev ), STRING( pev->model ));
	UTIL_SetOrigin( pev, pev->origin );
}

class CRainInfo : public CPointEntity
{
public:
	void Spawn( void );
	void KeyValue( KeyValueData *pkvd );
	void EXPORT ApplyThink( void );
	void EXPORT ApplyUse( CBaseEntity *activator, CBaseEntity *caller, USE_TYPE type, float value );
	void Apply( void );
	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];

	int m_iDensity;
	int m_iState;
	int m_iSpeed;
	int m_iXDegrees;
	int m_iYDegrees;
	int m_iAlpha;
	int m_iDistance;
	BOOL m_bSelected;
};

LINK_ENTITY_TO_CLASS( info_rain, CRainInfo )

TYPEDESCRIPTION CRainInfo::m_SaveData[] =
{
	DEFINE_FIELD( CRainInfo, m_iDensity, FIELD_INTEGER ),
	DEFINE_FIELD( CRainInfo, m_iState, FIELD_INTEGER ),
	DEFINE_FIELD( CRainInfo, m_iSpeed, FIELD_INTEGER ),
	DEFINE_FIELD( CRainInfo, m_iXDegrees, FIELD_INTEGER ),
	DEFINE_FIELD( CRainInfo, m_iYDegrees, FIELD_INTEGER ),
	DEFINE_FIELD( CRainInfo, m_iAlpha, FIELD_INTEGER ),
	DEFINE_FIELD( CRainInfo, m_iDistance, FIELD_INTEGER ),
	DEFINE_FIELD( CRainInfo, m_bSelected, FIELD_BOOLEAN ),
};

IMPLEMENT_SAVERESTORE( CRainInfo, CPointEntity )

void CRainInfo::KeyValue( KeyValueData *pkvd )
{
	int value = atoi( pkvd->szValue );
	if( FStrEq( pkvd->szKeyName, "density" )) m_iDensity = value;
	else if( FStrEq( pkvd->szKeyName, "state" )) m_iState = value;
	else if( FStrEq( pkvd->szKeyName, "fallspeed" )) m_iSpeed = value;
	else if( FStrEq( pkvd->szKeyName, "xdegrees" )) m_iXDegrees = value;
	else if( FStrEq( pkvd->szKeyName, "ydegrees" )) m_iYDegrees = value;
	else if( FStrEq( pkvd->szKeyName, "transluceny" )) m_iAlpha = value;
	else if( FStrEq( pkvd->szKeyName, "raindistance" )) m_iDistance = value;
	else
	{
		CPointEntity::KeyValue( pkvd );
		return;
	}
	pkvd->fHandled = TRUE;
}

void CRainInfo::Spawn( void )
{
	CPointEntity::Spawn();
	SetUse( &CRainInfo::ApplyUse );
	m_bSelected = FALSE;
	if( pev->spawnflags & 1 )
	{
		SetThink( &CRainInfo::ApplyThink );
		pev->nextthink = gpGlobals->time + 1.0f;
	}
}

static int NF_RainClamp( int value, int lo, int hi )
{
	return value < lo ? lo : ( value > hi ? hi : value );
}

static void NF_SendRainInfo( CRainInfo *info, CBasePlayer *player )
{
	if( player )
		MESSAGE_BEGIN( MSG_ONE, gmsgNFRainInfo, NULL, player->pev );
	else
		MESSAGE_BEGIN( MSG_ALL, gmsgNFRainInfo );
		WRITE_BYTE( info ? info->m_iState != 0 : 1 );
		WRITE_BYTE( info ? NF_RainClamp( info->m_iDensity, 0, 255 ) : 5 );
		WRITE_BYTE( info ? NF_RainClamp( info->m_iXDegrees, -90, 90 ) + 90 : 90 );
		WRITE_BYTE( info ? NF_RainClamp( info->m_iYDegrees, -90, 90 ) + 90 : 90 );
		WRITE_SHORT( info ? NF_RainClamp( info->m_iSpeed, 0, 32767 ) : 1000 );
		WRITE_BYTE( info ? NF_RainClamp( info->m_iAlpha, 0, 255 ) : 150 );
		WRITE_LONG( info ? NF_RainClamp( info->m_iDistance, 0, 360000 ) : 360000 );
	MESSAGE_END();
}

void CRainInfo::Apply( void )
{
	CRainInfo *info = NULL;
	while(( info = (CRainInfo *)UTIL_FindEntityByClassname( info, "info_rain" )) != NULL )
		info->m_bSelected = FALSE;
	m_bSelected = TRUE;
	NF_SendRainInfo( this, NULL );
	if( NF_DEBUG( NF_DBG_EFFECTS ))
		ALERT( at_console, "nf_debug: info_rain %s state %d density %d speed %d wind %d %d alpha %d distance %d\n",
			STRING( pev->targetname ), m_iState, m_iDensity, m_iSpeed, m_iXDegrees, m_iYDegrees, m_iAlpha, m_iDistance );
}

void CRainInfo::ApplyThink( void )
{
	Apply();
	SetThink( NULL );
}

void CRainInfo::ApplyUse( CBaseEntity *activator, CBaseEntity *caller, USE_TYPE type, float value )
{
	// Retail applies the stored state; repeated uses do not toggle it.
	Apply();
}

void NF_RainUpdateClient( CBasePlayer *player )
{
	// Reset also clears rain inherited from the previous map or saved timeline.
	MESSAGE_BEGIN( MSG_ONE, gmsgNFRainZone, NULL, player->pev );
		WRITE_BYTE( 0 );
	MESSAGE_END();

	CBaseEntity *zone = NULL;
	int count = 0;
	while(( zone = UTIL_FindEntityByClassname( zone, "env_rain" )) != NULL && count < 128 )
	{
		MESSAGE_BEGIN( MSG_ONE, gmsgNFRainZone, NULL, player->pev );
			WRITE_BYTE( 1 );
			for( int i = 0; i < 3; i++ ) WRITE_LONG( (int)( zone->pev->origin[i] + zone->pev->mins[i] ));
			for( int i = 0; i < 3; i++ ) WRITE_LONG( (int)( zone->pev->origin[i] + zone->pev->maxs[i] ));
		MESSAGE_END();
		count++;
	}

	CRainInfo *selected = NULL;
	CRainInfo *info = NULL;
	while(( info = (CRainInfo *)UTIL_FindEntityByClassname( info, "info_rain" )) != NULL )
		if( info->m_bSelected ) selected = info;
	NF_SendRainInfo( selected, player );
	if( NF_DEBUG( NF_DBG_EFFECTS ))
		ALERT( at_console, "nf_debug: rain HUD-init zones %d controller %s\n", count,
			selected ? STRING( selected->pev->targetname ) : "default" );
}
