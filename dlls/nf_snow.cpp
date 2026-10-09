/*
Nightfire occupied snow volumes and automatic info_snow settings.
Retail behavior and port transport: docs/retail/snow.md.
*/
#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "nf_env.h"
#include "nf_debug.h"

extern int gmsgNFSnowInfo;
extern int gmsgNFSnowZone;

class CSnowZone : public CBaseEntity
{
public:
	void Spawn( void );
};

LINK_ENTITY_TO_CLASS( env_snow, CSnowZone )

void CSnowZone::Spawn( void )
{
	pev->solid = SOLID_NOT;
	pev->movetype = MOVETYPE_NONE;
	pev->effects |= EF_NODRAW;
	SET_MODEL( ENT( pev ), STRING( pev->model ));
	UTIL_SetOrigin( pev, pev->origin );
}

class CSnowInfo : public CPointEntity
{
public:
	void Spawn( void );
	void KeyValue( KeyValueData *pkvd );
	void EXPORT ApplyThink( void );
	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];

	float m_flDistance;
	float m_flInterval;
	int m_iAmount;
	BOOL m_bSelected;
};

LINK_ENTITY_TO_CLASS( info_snow, CSnowInfo )

TYPEDESCRIPTION CSnowInfo::m_SaveData[] =
{
	DEFINE_FIELD( CSnowInfo, m_flDistance, FIELD_FLOAT ),
	DEFINE_FIELD( CSnowInfo, m_flInterval, FIELD_FLOAT ),
	DEFINE_FIELD( CSnowInfo, m_iAmount, FIELD_INTEGER ),
	DEFINE_FIELD( CSnowInfo, m_bSelected, FIELD_BOOLEAN ),
};

IMPLEMENT_SAVERESTORE( CSnowInfo, CPointEntity )

void CSnowInfo::KeyValue( KeyValueData *pkvd )
{
	if( FStrEq( pkvd->szKeyName, "snow_distance" )) m_flDistance = (float)atoi( pkvd->szValue );
	else if( FStrEq( pkvd->szKeyName, "snow_interval" )) m_flInterval = atof( pkvd->szValue );
	else if( FStrEq( pkvd->szKeyName, "snow_amount" )) m_iAmount = atoi( pkvd->szValue );
	else
	{
		CPointEntity::KeyValue( pkvd );
		return;
	}
	pkvd->fHandled = TRUE;
}

void CSnowInfo::Spawn( void )
{
	CPointEntity::Spawn();
	m_bSelected = FALSE;
	SetThink( &CSnowInfo::ApplyThink );
	pev->nextthink = gpGlobals->time + 0.5f;
}

static void NF_WriteSnowFloat( float value )
{
	// Retail coords are float32; GoldSrc WRITE_COORD would truncate/overflow.
	int bits;
	memcpy( &bits, &value, sizeof( bits ));
	WRITE_LONG( bits );
}

static void NF_SendSnowInfo( CSnowInfo *info, CBasePlayer *player )
{
	if( player ) MESSAGE_BEGIN( MSG_ONE, gmsgNFSnowInfo, NULL, player->pev );
	else MESSAGE_BEGIN( MSG_ALL, gmsgNFSnowInfo );
		NF_WriteSnowFloat( info ? info->m_flDistance : 1000.0f );
		NF_WriteSnowFloat( info ? info->m_flInterval : 0.9f );
		WRITE_LONG( info ? info->m_iAmount : 10 );
	MESSAGE_END();
}

void CSnowInfo::ApplyThink( void )
{
	CSnowInfo *info = NULL;
	while(( info = (CSnowInfo *)UTIL_FindEntityByClassname( info, "info_snow" )) != NULL )
		info->m_bSelected = FALSE;
	m_bSelected = TRUE;
	NF_SendSnowInfo( this, NULL );
	SetThink( NULL );
	if( NF_DEBUG( NF_DBG_EFFECTS ))
		ALERT( at_console, "nf_debug: info_snow %s amount %d interval %.3f distance %.0f\n",
			STRING( pev->targetname ), m_iAmount, m_flInterval, m_flDistance );
}

void NF_SnowUpdateClient( CBasePlayer *player )
{
	MESSAGE_BEGIN( MSG_ONE, gmsgNFSnowZone, NULL, player->pev );
		WRITE_BYTE( 0 );
	MESSAGE_END();

	CBaseEntity *zone = NULL;
	int count = 0;
	while(( zone = UTIL_FindEntityByClassname( zone, "env_snow" )) != NULL && count < 128 )
	{
		MESSAGE_BEGIN( MSG_ONE, gmsgNFSnowZone, NULL, player->pev );
			WRITE_BYTE( 1 );
			for( int i = 0; i < 3; i++ ) NF_WriteSnowFloat( zone->pev->origin[i] + zone->pev->mins[i] );
			for( int i = 0; i < 3; i++ ) NF_WriteSnowFloat( zone->pev->origin[i] + zone->pev->maxs[i] );
		MESSAGE_END();
		count++;
	}
	CSnowInfo *selected = NULL;
	CSnowInfo *info = NULL;
	while(( info = (CSnowInfo *)UTIL_FindEntityByClassname( info, "info_snow" )) != NULL )
		if( info->m_bSelected ) selected = info;
	NF_SendSnowInfo( selected, player );
	if( NF_DEBUG( NF_DBG_EFFECTS ))
		ALERT( at_console, "nf_debug: snow HUD-init zones %d controller %s\n", count,
			selected ? STRING( selected->pev->targetname ) : "default" );
}
