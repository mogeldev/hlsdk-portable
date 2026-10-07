/*
nf_fog.cpp - James Bond 007: Nightfire (PC) map fog ("Fog")

Sent by env_fog (dlls/nf_env.cpp) when the HUD is initialised: on flag,
colour bytes, start, end, then the same for the view under water. The retail
client (client.dll 0x41015790) reads env_fog from the entity string and
draws a linear fog; the sky is not fogged. The port sets it every frame
through TriAPI Fog (engine: GL_LINEAR while the fog density is 0, which
FogParams forces; the sky skip comes from FogParams' skybox flag and the
Nightfire sky dome, which never fogs). Under water the water values are
used when they are valid, else the fog is switched off and the engine's
own water fog (if any) is left alone.
*/

#include <string.h>
#include <stdio.h>

#include "hud.h"
#include "cl_util.h"
#include "const.h"
#include "triangleapi.h"
#include "parsemsg.h"
#include "nf_fog.h"
#include "nf_debug.h"

extern vec3_t v_origin;

struct nf_fog_t
{
	int on;
	float color[3];
	float start;
	float end;
};

static nf_fog_t s_fog;		// "rendercolor", "fogstart", "fogend"
static nf_fog_t s_waterFog;	// "waterfogcolor", "waterfogstart", "waterfogend"
static bool s_bApplied;		// TriAPI fog was switched on
static int s_iLastWater = -1;	// nf_debug: print the land / water switch

static void NF_ReadFog( nf_fog_t &fog )
{
	fog.on = READ_BYTE();
	fog.color[0] = (float)READ_BYTE();
	fog.color[1] = (float)READ_BYTE();
	fog.color[2] = (float)READ_BYTE();
	fog.start = (float)READ_LONG();
	fog.end = (float)READ_LONG();
}

static int __MsgFunc_Fog( const char *pszName, int iSize, void *pbuf )
{
	BEGIN_READ( pbuf, iSize );
	NF_ReadFog( s_fog );
	NF_ReadFog( s_waterFog );

	if( NF_DEBUG( NF_DBG_TRIGGERS ))
		gEngfuncs.Con_Printf( "nf_debug: fog %d color %.0f %.0f %.0f start %.0f end %.0f water %d\n",
			s_fog.on, s_fog.color[0], s_fog.color[1], s_fog.color[2], s_fog.start, s_fog.end, s_waterFog.on );
	return 1;
}

void NF_FogInit( void )
{
	HOOK_MESSAGE( Fog );
}

void NF_FogVidInit( void )
{
	// a new map: no fog until its env_fog says so
	memset( &s_fog, 0, sizeof( s_fog ));
	memset( &s_waterFog, 0, sizeof( s_waterFog ));
}

void NF_FogRender( void )
{
	int contents = gEngfuncs.PM_PointContents( v_origin, NULL );
	bool water = contents <= CONTENTS_WATER && contents >= CONTENTS_LAVA;
	const nf_fog_t *fog = water ? &s_waterFog : &s_fog;

	if( NF_DEBUG( NF_DBG_TRIGGERS ) && s_iLastWater != (int)water )
		gEngfuncs.Con_Printf( "nf_debug: fog view %s (contents %d) -> %s\n", water ? "under water" : "above water",
			contents, fog->on ? ( water ? "water fog" : "map fog" ) : "off" );
	s_iLastWater = (int)water;

	if( fog->on )
	{
		// linear, sky not fogged (FogParams before Fog: Fog picks the mode from the density)
		float color[3] = { fog->color[0], fog->color[1], fog->color[2] };
		gEngfuncs.pTriAPI->FogParams( 0.0f, 0 );
		gEngfuncs.pTriAPI->Fog( color, fog->start, fog->end, 1 );
		s_bApplied = true;
	}
	else if( s_bApplied )
	{
		float color[3] = { 0.0f, 0.0f, 0.0f };
		gEngfuncs.pTriAPI->Fog( color, 0.0f, 0.0f, 0 );
		s_bApplied = false;
	}
}
