/*
nf_env.cpp - James Bond 007: Nightfire (PC) map fog

env_fog
	Retail: a client-side entity. client.dll reads it from the entity
	string itself (0x41015790): "rendercolor" and "waterfogcolor" with
	"%u %u %u" (colour bytes, alpha 255 when the scan succeeds, else all
	0), "fogstart", "fogend", "waterfogstart", "waterfogend" with atof, into
	globals at 0x410B5818..0x410B589B. The shader system uses them as a
	linear fog (fogEquation 1, FogParameters 1 / (end - start), 0x4101d8e0);
	"density" (1.0 in 23 maps) is not read. The sky is not fogged.
	The port keeps it a server entity and sends the values to the client
	("Fog", see NF_FogUpdateClient; client: cl_dll/nf_fog.cpp) whenever the
	player's HUD is initialised (spawn, level change, load).
*/

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "nf_env.h"
#include "nf_debug.h"

extern int gmsgNFFog;

class CEnvFog : public CPointEntity
{
public:
	void Spawn( void );
	void KeyValue( KeyValueData *pkvd );

	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];

	float m_flStart;	// colour: "rendercolor" (pev, set by the engine)
	float m_flEnd;
	BOOL m_bWaterColor;
	Vector m_vecWaterColor;
	float m_flWaterStart;
	float m_flWaterEnd;
};

LINK_ENTITY_TO_CLASS( env_fog, CEnvFog )

TYPEDESCRIPTION CEnvFog::m_SaveData[] =
{
	DEFINE_FIELD( CEnvFog, m_flStart, FIELD_FLOAT ),
	DEFINE_FIELD( CEnvFog, m_flEnd, FIELD_FLOAT ),
	DEFINE_FIELD( CEnvFog, m_bWaterColor, FIELD_BOOLEAN ),
	DEFINE_FIELD( CEnvFog, m_vecWaterColor, FIELD_VECTOR ),
	DEFINE_FIELD( CEnvFog, m_flWaterStart, FIELD_FLOAT ),
	DEFINE_FIELD( CEnvFog, m_flWaterEnd, FIELD_FLOAT ),
};

IMPLEMENT_SAVERESTORE( CEnvFog, CPointEntity )

static BOOL NF_ScanColor( const char *value, Vector &color )
{
	unsigned int r, g, b;

	if( sscanf( value, "%u %u %u", &r, &g, &b ) != 3 )
	{
		color = g_vecZero;
		return FALSE;
	}
	color = Vector( (float)( r & 255 ), (float)( g & 255 ), (float)( b & 255 ));
	return TRUE;
}

void CEnvFog::KeyValue( KeyValueData *pkvd )
{
	if( FStrEq( pkvd->szKeyName, "fogstart" ))
	{
		m_flStart = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "fogend" ))
	{
		m_flEnd = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "waterfogcolor" ))
	{
		m_bWaterColor = NF_ScanColor( pkvd->szValue, m_vecWaterColor );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "waterfogstart" ))
	{
		m_flWaterStart = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "waterfogend" ))
	{
		m_flWaterEnd = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else
		CPointEntity::KeyValue( pkvd );
}

void CEnvFog::Spawn( void )
{
	CPointEntity::Spawn();
}

static void NF_WriteFog( BOOL on, const Vector &color, float start, float end )
{
	WRITE_BYTE( on ? 1 : 0 );
	WRITE_BYTE( (int)color.x );
	WRITE_BYTE( (int)color.y );
	WRITE_BYTE( (int)color.z );
	WRITE_LONG( (int)start );
	WRITE_LONG( (int)end );
}

void NF_FogUpdateClient( CBasePlayer *pPlayer )
{
	CEnvFog *pFog = (CEnvFog *)UTIL_FindEntityByClassname( NULL, "env_fog" );

	// no env_fog: switch the fog of the previous map off
	BOOL on = pFog && pFog->m_flEnd > pFog->m_flStart;
	BOOL water = pFog && pFog->m_bWaterColor && pFog->m_flWaterEnd > pFog->m_flWaterStart;

	MESSAGE_BEGIN( MSG_ONE, gmsgNFFog, NULL, pPlayer->pev );
		NF_WriteFog( on, pFog ? pFog->pev->rendercolor : g_vecZero, pFog ? pFog->m_flStart : 0.0f, pFog ? pFog->m_flEnd : 0.0f );
		NF_WriteFog( water, pFog ? pFog->m_vecWaterColor : g_vecZero, pFog ? pFog->m_flWaterStart : 0.0f, pFog ? pFog->m_flWaterEnd : 0.0f );
	MESSAGE_END();

	if( NF_DEBUG( NF_DBG_TRIGGERS ))
	{
		if( pFog )
			ALERT( at_console, "nf_debug: env_fog %s color %.0f %.0f %.0f start %.0f end %.0f water %s\n",
				on ? "on" : "off", pFog->pev->rendercolor.x, pFog->pev->rendercolor.y, pFog->pev->rendercolor.z,
				pFog->m_flStart, pFog->m_flEnd, water ? "on" : "off" );
		else
			ALERT( at_console, "nf_debug: env_fog none\n" );
	}
}
