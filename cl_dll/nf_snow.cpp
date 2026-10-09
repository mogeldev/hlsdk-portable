/*
Nightfire snow: occupied volumes, independent automatic settings, 2x2 atlas.
Retail formulas and deliberate safety/transport deviations: docs/retail/snow.md.
*/
#include <math.h>
#include <string.h>
#include "hud.h"
#include "cl_util.h"
#include "const.h"
#include "com_model.h"
#include "triangleapi.h"
#include "parsemsg.h"
#include "event_api.h"
#include "pm_defs.h"
#include "pmtrace.h"
#include "nf_snow.h"
#include "nf_debug.h"

extern vec3_t v_origin, v_angles;

#define NF_SNOW_ZONES 128
#define NF_SNOW_FLAKES 4096
#define NF_SNOW_ATTEMPTS 512

struct nf_snow_zone_t
{
	vec3_t mins, maxs;
	float next;
};

struct nf_snow_flake_t
{
	bool used;
	vec3_t origin, velocity, mins, maxs;
	float die, size;
	int frame;
};

static nf_snow_zone_t s_zones[NF_SNOW_ZONES];
static nf_snow_flake_t s_flakes[NF_SNOW_FLAKES];
static int s_zoneCount, s_nextFlake, s_nextZone, s_live, s_emitted, s_rejected;
static float s_distance, s_interval, s_lastTime;
static int s_amount;
static bool s_haveTime;
static struct model_s *s_model;

static void NF_ClearSnow( void )
{
	memset( s_flakes, 0, sizeof( s_flakes ));
	for( int i = 0; i < s_zoneCount; i++ ) s_zones[i].next = 0.0f;
	s_nextFlake = s_nextZone = s_live = s_emitted = s_rejected = 0;
	s_haveTime = false;
}

void NF_SnowVidInit( void )
{
	s_zoneCount = 0;
	s_distance = 1000.0f;
	s_interval = 0.9f;
	s_amount = 10;
	s_model = NULL;
	NF_ClearSnow();
}

static int __MsgFunc_SnowInfo( const char *name, int size, void *buffer )
{
	if( size != 12 ) return 1;
	BEGIN_READ( buffer, size );
	float distance = READ_FLOAT(), interval = READ_FLOAT();
	int amount = READ_LONG();
	// Comparisons also reject NaN; cap pathological map settings and zero intervals.
	s_distance = distance >= 0.0f && distance <= 360000.0f ? distance : 0.0f;
	s_interval = interval >= 0.01f && interval <= 3600.0f ? interval : 0.01f;
	s_amount = amount > 0 ? ( amount > 10000 ? 10000 : amount ) : 0;
	for( int i = 0; i < s_zoneCount; i++ ) s_zones[i].next = 0.0f;
	if( NF_DEBUG( NF_DBG_EFFECTS ))
		gEngfuncs.Con_Printf( "nf_debug: cl: snow amount %d interval %.3f distance %.0f\n", s_amount, s_interval, s_distance );
	return 1;
}

static int __MsgFunc_SnowZone( const char *name, int size, void *buffer )
{
	if( size != 1 && size != 25 ) return 1;
	BEGIN_READ( buffer, size );
	int action = READ_BYTE();
	if( action == 0 && size == 1 )
	{
		s_zoneCount = 0;
		NF_ClearSnow();
	}
	else if( action == 1 && size == 25 && s_zoneCount < NF_SNOW_ZONES )
	{
		nf_snow_zone_t zone;
		for( int i = 0; i < 3; i++ ) zone.mins[i] = READ_FLOAT();
		for( int i = 0; i < 3; i++ ) zone.maxs[i] = READ_FLOAT();
		for( int i = 0; i < 3; i++ )
			if( !( zone.mins[i] >= -1048576.0f && zone.maxs[i] <= 1048576.0f && zone.mins[i] <= zone.maxs[i] )) return 1;
		zone.next = 0.0f;
		s_zones[s_zoneCount++] = zone;
	}
	return 1;
}

static void NF_SnowReport( void )
{
	if( !NF_DEBUG( NF_DBG_EFFECTS )) return;
	int outside = 0;
	vec3_t lo = { 1048576.0f, 1048576.0f, 1048576.0f }, hi = { -1048576.0f, -1048576.0f, -1048576.0f };
	for( int i = 0; i < NF_SNOW_FLAKES; i++ )
	{
		const nf_snow_flake_t *flake = &s_flakes[i];
		if( !flake->used ) continue;
		bool out = false;
		for( int axis = 0; axis < 3; axis++ )
		{
			if( flake->origin[axis] < flake->mins[axis] || flake->origin[axis] > flake->maxs[axis] ) out = true;
			if( flake->origin[axis] < lo[axis] ) lo[axis] = flake->origin[axis];
			if( flake->origin[axis] > hi[axis] ) hi[axis] = flake->origin[axis];
		}
		if( out ) outside++;
	}
	if( !s_live )
	{
		VectorClear( lo );
		VectorClear( hi );
	}
	gEngfuncs.Con_Printf( "nf_debug: cl: snow report zones %d amount %d interval %.3f distance %.0f live %d emitted %d rejected %d outside %d z %.1f %.1f sprite %d\n",
		s_zoneCount, s_amount, s_interval, s_distance, s_live, s_emitted, s_rejected, outside, lo[2], hi[2], s_model != NULL );
}

void NF_SnowInit( void )
{
	HOOK_MESSAGE( SnowInfo );
	HOOK_MESSAGE( SnowZone );
	gEngfuncs.pfnAddCommand( "nf_snowinfo", NF_SnowReport );
	NF_SnowVidInit();
}

static nf_snow_flake_t *NF_AllocSnowFlake( void )
{
	for( int i = 0; i < NF_SNOW_FLAKES; i++ )
	{
		int index = ( s_nextFlake + i ) % NF_SNOW_FLAKES;
		if( !s_flakes[index].used )
		{
			s_nextFlake = ( index + 1 ) % NF_SNOW_FLAKES;
			return &s_flakes[index];
		}
	}
	return NULL;
}

static bool NF_SpawnSnowFlake( const nf_snow_zone_t *zone, const vec3_t mins, const vec3_t maxs, float time )
{
	nf_snow_flake_t *flake = NF_AllocSnowFlake();
	if( !flake ) return false;
	vec3_t origin, top;
	for( int i = 0; i < 3; i++ ) origin[i] = gEngfuncs.pfnRandomFloat( mins[i], maxs[i] );
	VectorCopy( origin, top );
	top[2] = zone->maxs[2];
	pmtrace_t trace;
	gEngfuncs.pEventAPI->EV_PlayerTrace( origin, top, PM_STUDIO_IGNORE, -1, &trace );
	if( trace.startsolid || trace.allsolid || trace.fraction < 1.0f )
	{
		s_rejected++;
		return true;
	}
	memset( flake, 0, sizeof( *flake ));
	flake->used = true;
	VectorCopy( origin, flake->origin );
	VectorCopy( mins, flake->mins );
	VectorCopy( maxs, flake->maxs );
	float speed = gEngfuncs.pfnRandomFloat( 30.0f, 70.0f );
	flake->velocity[0] = gEngfuncs.pfnRandomFloat( -speed, speed ) * 0.5f;
	flake->velocity[1] = gEngfuncs.pfnRandomFloat( -speed, speed ) * 0.5f;
	flake->die = time + 10.0f;
	flake->size = gEngfuncs.pfnRandomFloat( 2.0f, 6.0f );
	flake->frame = gEngfuncs.pfnRandomLong( 0, 3 );
	s_live++;
	s_emitted++;
	return true;
}

static void NF_UpdateSnow( float time, float dt )
{
	gEngfuncs.pEventAPI->EV_SetUpPlayerPrediction( false, false );
	gEngfuncs.pEventAPI->EV_PushPMStates();
	gEngfuncs.pEventAPI->EV_SetTraceHull( 2 );
	float gravity = gEngfuncs.pfnGetGravity() * 0.03f;
	for( int i = 0; i < NF_SNOW_FLAKES; i++ )
	{
		nf_snow_flake_t *flake = &s_flakes[i];
		if( !flake->used ) continue;
		vec3_t next;
		VectorMA( flake->origin, dt, flake->velocity, next );
		bool dead = time >= flake->die;
		for( int axis = 0; axis < 3; axis++ )
			if( next[axis] <= flake->mins[axis] || next[axis] >= flake->maxs[axis] ) dead = true;
		if( !dead && dt > 0.0f )
		{
			pmtrace_t trace;
			gEngfuncs.pEventAPI->EV_PlayerTrace( flake->origin, next, PM_STUDIO_IGNORE, -1, &trace );
			dead = trace.startsolid || trace.allsolid || trace.fraction < 1.0f;
		}
		if( dead )
		{
			flake->used = false;
			s_live--;
		}
		else
		{
			VectorCopy( next, flake->origin );
			flake->velocity[2] -= gravity * dt;
		}
	}
	int attempts = 0;
	if( s_amount > 0 && s_distance > 0.0f )
		for( int z = 0; z < s_zoneCount; z++ )
		{
			nf_snow_zone_t *zone = &s_zones[( s_nextZone + z ) % s_zoneCount];
			if( time < zone->next ) continue;
			zone->next = time + s_interval;
			if( !gEngfuncs.pTriAPI->BoxInPVS( zone->mins, zone->maxs )) continue;
			vec3_t mins, maxs;
			VectorCopy( zone->mins, mins );
			VectorCopy( zone->maxs, maxs );
			const float window[2] = { 0.52532199f * s_distance, 0.85090352f * s_distance };
			for( int axis = 0; axis < 2; axis++ )
			{
				if( mins[axis] < v_origin[axis] - window[axis] ) mins[axis] = v_origin[axis] - window[axis];
				if( maxs[axis] > v_origin[axis] + window[axis] ) maxs[axis] = v_origin[axis] + window[axis];
			}
			if( mins[0] >= maxs[0] || mins[1] >= maxs[1] ) continue;
			float count = s_amount * ( zone->maxs[0] - zone->mins[0] ) * ( zone->maxs[1] - zone->mins[1] ) / 1048576.0f;
			int budget = count < NF_SNOW_ATTEMPTS ? 2 * (int)count : NF_SNOW_ATTEMPTS;
			while( budget-- > 0 && attempts < NF_SNOW_ATTEMPTS )
			{
				attempts++;
				if( !NF_SpawnSnowFlake( zone, mins, maxs, time ))
				{
					attempts = NF_SNOW_ATTEMPTS;
					break;
				}
			}
		}
	if( s_zoneCount ) s_nextZone = ( s_nextZone + 1 ) % s_zoneCount;
	gEngfuncs.pEventAPI->EV_PopPMStates();
}

static void NF_SnowVertex( const nf_snow_flake_t *flake, const vec3_t right, const vec3_t up, float x, float y, float u, float v )
{
	gEngfuncs.pTriAPI->TexCoord2f( u, v );
	gEngfuncs.pTriAPI->Vertex3f( flake->origin[0] + flake->size * ( right[0] * x + up[0] * y ),
		flake->origin[1] + flake->size * ( right[1] * x + up[1] * y ), flake->origin[2] + flake->size * ( right[2] * x + up[2] * y ));
}

void NF_SnowRender( void )
{
	if( !s_zoneCount ) return;
	if( !s_model ) s_model = (struct model_s *)gEngfuncs.GetSpritePointer( SPR_Load( "sprites/snowflake_01.spz" ));
	float time = gEngfuncs.GetClientTime();
	if( !s_haveTime )
	{
		s_lastTime = time;
		s_haveTime = true;
	}
	if( time != s_lastTime )
	{
		float dt = time - s_lastTime;
		if( dt < 0.0f || dt > 0.25f )
		{
			NF_ClearSnow();
			dt = 0.0f;
			s_haveTime = true;
		}
		s_lastTime = time;
		NF_UpdateSnow( time, dt );
	}
	if( !s_model || !gEngfuncs.pTriAPI->SpriteTexture( s_model, 0 )) return;
	vec3_t forward, right, up;
	AngleVectors( v_angles, forward, right, up );
	gEngfuncs.pTriAPI->RenderMode( kRenderTransTexture );
	gEngfuncs.pTriAPI->CullFace( TRI_NONE );
	gEngfuncs.pTriAPI->Color4f( 1.0f, 1.0f, 1.0f, 1.0f );
	gEngfuncs.pTriAPI->Begin( TRI_QUADS );
	for( int i = 0; i < NF_SNOW_FLAKES; i++ )
	{
		const nf_snow_flake_t *flake = &s_flakes[i];
		if( !flake->used ) continue;
		float u = ( flake->frame & 1 ) * 0.5f, v = ( flake->frame >> 1 ) * 0.5f;
		NF_SnowVertex( flake, right, up, -1, -1, u, v + 0.5f );
		NF_SnowVertex( flake, right, up, -1, 1, u, v );
		NF_SnowVertex( flake, right, up, 1, 1, u + 0.5f, v );
		NF_SnowVertex( flake, right, up, 1, -1, u + 0.5f, v + 0.5f );
	}
	gEngfuncs.pTriAPI->End();
	gEngfuncs.pTriAPI->RenderMode( kRenderNormal );
	gEngfuncs.pTriAPI->CullFace( TRI_FRONT );
}
