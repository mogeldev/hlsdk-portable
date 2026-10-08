/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*	
*	This product contains software technology licensed from Id 
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc. 
*	All Rights Reserved.
*
*   Use, distribution, and modification of this source code and/or resulting
*   object code is restricted to non-commercial enhancements to products from
*   Valve LLC.  All other use, distribution, or modification is prohibited
*   without written permission from Valve LLC.
*
****/
/*

===== lights.cpp ========================================================

  spawn and think functions for editor-placed lights

*/

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "nf_env.h"
#include "nf_debug.h"

#define SF_NF_ENTITY_LIGHT 2

class CLight : public CPointEntity
{
public:
	virtual void KeyValue( KeyValueData* pkvd ); 
	virtual void Spawn( void );
	void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );
	void EXPORT EntityLightThink( void );
	void SendEntityLight( CBasePlayer *pPlayer = NULL );
	BOOL HasEntityLight( void ) { return m_bNFEntityLight; }

	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );

	static TYPEDESCRIPTION m_SaveData[];

private:
	int m_iStyle;
	string_t m_iszPattern;
	Vector m_vecNFColor;
	int m_iNFRadius;
	BOOL m_bNFEntityLight;
};

LINK_ENTITY_TO_CLASS( light, CLight )
LINK_ENTITY_TO_CLASS( entity_light, CLight )

TYPEDESCRIPTION	CLight::m_SaveData[] =
{
	DEFINE_FIELD( CLight, m_iStyle, FIELD_INTEGER ),
	DEFINE_FIELD( CLight, m_iszPattern, FIELD_STRING ),
	DEFINE_FIELD( CLight, m_vecNFColor, FIELD_VECTOR ),
	DEFINE_FIELD( CLight, m_iNFRadius, FIELD_INTEGER ),
	DEFINE_FIELD( CLight, m_bNFEntityLight, FIELD_BOOLEAN ),
};

IMPLEMENT_SAVERESTORE( CLight, CPointEntity )

//
// Cache user-entity-field values until spawn is called.
//
void CLight::KeyValue( KeyValueData* pkvd )
{
	if( FStrEq(pkvd->szKeyName, "style" ) )
	{
		m_iStyle = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq(pkvd->szKeyName, "pitch" ) )
	{
		pev->angles.x = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq(pkvd->szKeyName, "pattern" ) )
	{
		m_iszPattern = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "_light" ))
	{
		int r = 0, g = 0, b = 0, radius = 0;
		int count = sscanf( pkvd->szValue, "%d %d %d %d", &r, &g, &b, &radius );
		if( count == 1 ) g = b = r;
		// Retail uses colour bytes and caps the coord radius at 250.
		// Keep malformed inputs defined instead of copying its uninitialised radius.
		m_vecNFColor = Vector( (byte)r, (byte)g, (byte)b );
		m_iNFRadius = radius < 0 ? 0 : (radius > 250 ? 250 : radius);
		pkvd->fHandled = TRUE;
	}
	else
	{
		CPointEntity::KeyValue( pkvd );
	}
}

/*QUAKED light (0 1 0) (-8 -8 -8) (8 8 8) LIGHT_START_OFF
Non-displayed light.
Default light value is 300
Default style is 0
If targeted, it will toggle between on or off.
*/

void CLight::Spawn( void )
{
	m_bNFEntityLight = FBitSet( pev->spawnflags, SF_NF_ENTITY_LIGHT ) &&
		!FBitSet( pev->spawnflags, SF_LIGHT_START_OFF );
	if( m_bNFEntityLight )
	{
		SetThink( &CLight::EntityLightThink );
		pev->nextthink = gpGlobals->time + 10.0f;
		if( NF_DEBUG( NF_DBG_EFFECTS ))
			ALERT( at_console, "nf_debug: entity_light %d '%s' starts on radius %d color %.0f %.0f %.0f\n",
				entindex(), STRING( pev->targetname ), m_iNFRadius,
				m_vecNFColor.x, m_vecNFColor.y, m_vecNFColor.z );
	}
	if( FStringNull( pev->targetname ) )
	{
		if( m_bNFEntityLight ) return;
		// inert light
		REMOVE_ENTITY(ENT( pev ) );
		return;
	}

	if( m_iStyle >= 32 )
	{
		//CHANGE_METHOD(ENT(pev), em_use, light_use);
		if( FBitSet( pev->spawnflags, SF_LIGHT_START_OFF ) )
			LIGHT_STYLE( m_iStyle, "a" );
		else if( m_iszPattern )
			LIGHT_STYLE( m_iStyle, STRING( m_iszPattern ) );
		else
			LIGHT_STYLE( m_iStyle, "m" );
	}
}

void CLight::SendEntityLight( CBasePlayer *pPlayer )
{
	// A stable key replaces this light on HUD init / reconnect instead of
	// allocating duplicate ten-second lights. Its index belongs to this
	// point entity, so it cannot attach to a studio model in the renderer.
	if( pPlayer )
		MESSAGE_BEGIN( MSG_ONE, SVC_TEMPENTITY, NULL, pPlayer->pev );
	else
		MESSAGE_BEGIN( MSG_ALL, SVC_TEMPENTITY );
		WRITE_BYTE( TE_ELIGHT );
		WRITE_SHORT( entindex() );
		WRITE_COORD( pev->origin.x );
		WRITE_COORD( pev->origin.y );
		WRITE_COORD( pev->origin.z );
		WRITE_COORD( m_iNFRadius );
		WRITE_BYTE( (int)m_vecNFColor.x );
		WRITE_BYTE( (int)m_vecNFColor.y );
		WRITE_BYTE( (int)m_vecNFColor.z );
		WRITE_BYTE( 100 );
		WRITE_COORD( 0 );
	MESSAGE_END();
	if( NF_DEBUG( NF_DBG_EFFECTS ) && !FStringNull( pev->targetname ))
		ALERT( at_console, "nf_debug: entity_light '%s' sent radius %d color %.0f %.0f %.0f (%s)\n",
			STRING( pev->targetname ), m_iNFRadius, m_vecNFColor.x, m_vecNFColor.y,
			m_vecNFColor.z, pPlayer ? "HUD init" : "refresh" );
}

void CLight::EntityLightThink( void )
{
	SendEntityLight();
	pev->nextthink = gpGlobals->time + 10.0f;
}

void NF_EntityLightsUpdateClient( CBasePlayer *pPlayer )
{
	const char *classes[] = { "entity_light", "light", "light_spot", "light_environment" };
	int count = 0;
	for( int i = 0; i < ARRAYSIZE( classes ); ++i )
	{
		CBaseEntity *pEntity = NULL;
		while(( pEntity = UTIL_FindEntityByClassname( pEntity, classes[i] )) != NULL )
		{
			CLight *pLight = (CLight *)pEntity;
			if( pLight->HasEntityLight() )
			{
				pLight->SendEntityLight( pPlayer );
				++count;
			}
		}
	}
	if( NF_DEBUG( NF_DBG_EFFECTS ))
		ALERT( at_console, "nf_debug: entity_light HUD init player %d: %d lights\n",
			pPlayer->entindex(), count );
}

void CLight::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	if( m_iStyle >= 32 )
	{
		if( !ShouldToggle( useType, !FBitSet( pev->spawnflags, SF_LIGHT_START_OFF ) ) )
			return;

		if( FBitSet( pev->spawnflags, SF_LIGHT_START_OFF ) )
		{
			if( m_iszPattern )
				LIGHT_STYLE( m_iStyle, STRING( m_iszPattern ) );
			else
				LIGHT_STYLE( m_iStyle, "m" );
			ClearBits( pev->spawnflags, SF_LIGHT_START_OFF );
		}
		else
		{
			LIGHT_STYLE( m_iStyle, "a" );
			SetBits( pev->spawnflags, SF_LIGHT_START_OFF );
		}
	}
}

//
// shut up spawn functions for new spotlights
//
LINK_ENTITY_TO_CLASS( light_spot, CLight )

class CEnvLight : public CLight
{
public:
	void KeyValue( KeyValueData* pkvd ); 
	void Spawn( void );
};

LINK_ENTITY_TO_CLASS( light_environment, CEnvLight )

void CEnvLight::KeyValue( KeyValueData* pkvd )
{
	if( FStrEq(pkvd->szKeyName, "_light" ) )
	{
		int r, g, b, v, j;
		char szColor[64];
		j = sscanf( pkvd->szValue, "%d %d %d %d\n", &r, &g, &b, &v );
		if( j == 1 )
		{
			g = b = r;
		}
		else if( j == 4 )
		{
			float vf = v / 255.0f;
			r *= vf;
			g *= vf;
			b *= vf;
		}

		// simulate qrad direct, ambient,and gamma adjustments, as well as engine scaling
		r = (int)( pow( r / 114.0f, 0.6f ) * 264.0f );
		g = (int)( pow( g / 114.0f, 0.6f ) * 264.0f );
		b = (int)( pow( b / 114.0f, 0.6f ) * 264.0f );

		pkvd->fHandled = TRUE;
		sprintf( szColor, "%d", r );
		CVAR_SET_STRING( "sv_skycolor_r", szColor );
		sprintf( szColor, "%d", g );
		CVAR_SET_STRING( "sv_skycolor_g", szColor );
		sprintf( szColor, "%d", b );
		CVAR_SET_STRING( "sv_skycolor_b", szColor );
	}
	else
	{
		CLight::KeyValue( pkvd );
	}
}

void CEnvLight::Spawn( void )
{
	char szVector[64];
	UTIL_MakeAimVectors( pev->angles );

	sprintf( szVector, "%f", (double)gpGlobals->v_forward.x );
	CVAR_SET_STRING( "sv_skyvec_x", szVector );
	sprintf( szVector, "%f", (double)gpGlobals->v_forward.y );
	CVAR_SET_STRING( "sv_skyvec_y", szVector );
	sprintf( szVector, "%f", (double)gpGlobals->v_forward.z );
	CVAR_SET_STRING( "sv_skyvec_z", szVector );

	CLight::Spawn();
}
