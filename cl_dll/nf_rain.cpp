/*
Nightfire rain in the map's env_rain emission rectangles.
Retail parameters: docs/retail/rain.md. Simulation is frame-rate independent;
bounded pools/budgets avoid the retail patched client's FPS-dependent density.
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
#include "nf_rain.h"
#include "nf_debug.h"

extern vec3_t v_origin, v_angles;

#define NF_RAIN_ZONES 128
#define NF_RAIN_DROPS 4096
#define NF_RAIN_SPLASHES 512
#define NF_RAIN_ATTEMPTS 256

struct nf_rain_zone_t
{
	vec3_t mins, maxs;
	float remainder;
};

struct nf_rain_drop_t
{
	bool used;
	bool splash;
	vec3_t origin, velocity, impact, normal;
	float die, width, alpha;
};

struct nf_rain_splash_t
{
	bool used;
	vec3_t origin, normal;
	float birth, size, alpha;
};

static nf_rain_zone_t s_zones[NF_RAIN_ZONES];
static nf_rain_drop_t s_drops[NF_RAIN_DROPS];
static nf_rain_splash_t s_splashes[NF_RAIN_SPLASHES];
static int s_zoneCount, s_nextDrop, s_nextSplash, s_nextZone;
static bool s_enabled;
static float s_density, s_speed, s_xDegrees, s_yDegrees, s_alpha, s_distance;
static float s_lastTime;
static bool s_haveTime;
static struct model_s *s_dropModel, *s_splashModel;

static void NF_ClearRainParticles( void )
{
	memset( s_drops, 0, sizeof( s_drops ));
	memset( s_splashes, 0, sizeof( s_splashes ));
	for( int i = 0; i < s_zoneCount; i++ ) s_zones[i].remainder = 0.0f;
	s_nextDrop = s_nextSplash = s_nextZone = 0;
	s_haveTime = false;
}

void NF_RainVidInit( void )
{
	s_zoneCount = 0;
	s_enabled = true;
	s_density = 5.0f;
	s_speed = 1000.0f;
	s_xDegrees = s_yDegrees = 0.0f;
	s_alpha = 150.0f;
	s_distance = 360000.0f;
	s_dropModel = s_splashModel = NULL;
	NF_ClearRainParticles();
}

static int __MsgFunc_RainInfo( const char *name, int size, void *buffer )
{
	if( size != 11 ) return 1;
	BEGIN_READ( buffer, size );
	s_enabled = READ_BYTE() != 0;
	s_density = (float)READ_BYTE();
	s_xDegrees = (float)READ_BYTE() - 90.0f;
	s_yDegrees = (float)READ_BYTE() - 90.0f;
	s_speed = (float)READ_SHORT();
	s_alpha = (float)READ_BYTE();
	s_distance = (float)READ_LONG();
	if( s_speed < 0.0f ) s_speed = 0.0f;
	if( s_distance < 0.0f ) s_distance = 0.0f;
	if( NF_DEBUG( NF_DBG_EFFECTS ))
		gEngfuncs.Con_Printf( "nf_debug: cl: rain state %d density %.0f speed %.0f wind %.0f %.0f alpha %.0f distance %.0f\n",
			s_enabled, s_density, s_speed, s_xDegrees, s_yDegrees, s_alpha, s_distance );
	return 1;
}

static int __MsgFunc_RainZone( const char *name, int size, void *buffer )
{
	if( size != 1 && size != 25 ) return 1;
	BEGIN_READ( buffer, size );
	int action = READ_BYTE();
	if( action == 0 && size == 1 )
	{
		s_zoneCount = 0;
		NF_ClearRainParticles();
	}
	else if( action == 1 && size == 25 && s_zoneCount < NF_RAIN_ZONES )
	{
		nf_rain_zone_t *zone = &s_zones[s_zoneCount++];
		for( int i = 0; i < 3; i++ ) zone->mins[i] = (float)READ_LONG();
		for( int i = 0; i < 3; i++ ) zone->maxs[i] = (float)READ_LONG();
		for( int i = 0; i < 3; i++ )
			if( zone->mins[i] > zone->maxs[i] )
			{
				float value = zone->mins[i];
				zone->mins[i] = zone->maxs[i];
				zone->maxs[i] = value;
			}
		zone->remainder = 0.0f;
	}
	return 1;
}

void NF_RainInit( void )
{
	HOOK_MESSAGE( RainInfo );
	HOOK_MESSAGE( RainZone );
	NF_RainVidInit();
}

static nf_rain_drop_t *NF_AllocRainDrop( void )
{
	for( int i = 0; i < NF_RAIN_DROPS; i++ )
	{
		int index = ( s_nextDrop + i ) % NF_RAIN_DROPS;
		if( !s_drops[index].used )
		{
			s_nextDrop = ( index + 1 ) % NF_RAIN_DROPS;
			return &s_drops[index];
		}
	}
	return NULL;
}

static void NF_RainSplash( const nf_rain_drop_t *drop, float time )
{
	if( !drop->splash ) return;
	for( int i = 0; i < NF_RAIN_SPLASHES; i++ )
	{
		int index = ( s_nextSplash + i ) % NF_RAIN_SPLASHES;
		nf_rain_splash_t *splash = &s_splashes[index];
		if( splash->used ) continue;
		s_nextSplash = ( index + 1 ) % NF_RAIN_SPLASHES;
		splash->used = true;
		VectorCopy( drop->normal, splash->normal );
		VectorMA( drop->impact, 0.5f, drop->normal, splash->origin );
		splash->birth = time;
		splash->size = gEngfuncs.pfnRandomFloat( 5.0f, 15.0f );
		splash->alpha = drop->alpha;
		return;
	}
}

static bool NF_SpawnRainDrop( nf_rain_zone_t *zone, float time )
{
	vec3_t start, end, velocity;
	start[0] = gEngfuncs.pfnRandomFloat( zone->mins[0], zone->maxs[0] );
	start[1] = gEngfuncs.pfnRandomFloat( zone->mins[1], zone->maxs[1] );
	start[2] = zone->mins[2];
	float dx = start[0] - v_origin[0], dy = start[1] - v_origin[1];
	float heightScale = ( start[2] - v_origin[2] ) / 433.0f;
	if( heightScale < 1.0f ) heightScale = 1.0f;
	if( dx * dx + dy * dy > s_distance * s_distance * heightScale ) return true;

	nf_rain_drop_t *drop = NF_AllocRainDrop();
	if( !drop ) return false;
	velocity[0] = -s_xDegrees * s_speed / 90.0f;
	velocity[1] = s_yDegrees * s_speed / 90.0f;
	float xWind = (float)fabs( velocity[0] ), yWind = (float)fabs( velocity[1] );
	velocity[2] = -s_speed + ( xWind > yWind ? xWind : yWind );
	if( velocity[2] > -1.0f ) return true;
	float travel = 2400.0f / (float)sqrt( DotProduct( velocity, velocity ));
	VectorMA( start, travel, velocity, end );
	pmtrace_t trace;
	gEngfuncs.pEventAPI->EV_PlayerTrace( start, end, PM_STUDIO_IGNORE, -1, &trace );
	if( trace.startsolid || trace.allsolid ) return true;

	memset( drop, 0, sizeof( *drop ));
	drop->used = true;
	VectorCopy( start, drop->origin );
	VectorCopy( velocity, drop->velocity );
	VectorCopy( trace.endpos, drop->impact );
	VectorCopy( trace.plane.normal, drop->normal );
	drop->splash = trace.fraction < 1.0f && trace.plane.normal[2] > 0.5f;
	drop->die = time + travel * trace.fraction;
	drop->width = gEngfuncs.pfnRandomFloat( 0.8f, 1.6f );
	drop->alpha = s_alpha;
	return true;
}

static void NF_UpdateRain( float time, float dt )
{
	for( int i = 0; i < NF_RAIN_DROPS; i++ )
	{
		nf_rain_drop_t *drop = &s_drops[i];
		if( !drop->used ) continue;
		if( time >= drop->die )
		{
			NF_RainSplash( drop, time );
			drop->used = false;
		}
		else VectorMA( drop->origin, dt, drop->velocity, drop->origin );
	}
	for( int i = 0; i < NF_RAIN_SPLASHES; i++ )
		if( s_splashes[i].used && time - s_splashes[i].birth >= 7.0f / 30.0f ) s_splashes[i].used = false;
	if( !s_enabled || s_speed <= 0.0f || s_density <= 0.0f || s_alpha <= 0.0f ) return;

	gEngfuncs.pEventAPI->EV_SetUpPlayerPrediction( false, false );
	gEngfuncs.pEventAPI->EV_PushPMStates();
	gEngfuncs.pEventAPI->EV_SetTraceHull( 2 );
	int attempts = 0;
	for( int z = 0; z < s_zoneCount; z++ )
	{
		int index = ( s_nextZone + z ) % s_zoneCount;
		nf_rain_zone_t *zone = &s_zones[index];
		float dx = zone->maxs[0] - zone->mins[0], dy = zone->maxs[1] - zone->mins[1];
		zone->remainder += s_density * (float)sqrt( dx * dx + dy * dy ) / 5.0f * dt;
		int count = (int)zone->remainder;
		zone->remainder -= (float)count;
		while( count-- > 0 && attempts < NF_RAIN_ATTEMPTS )
		{
			attempts++;
			if( !NF_SpawnRainDrop( zone, time ))
			{
				attempts = NF_RAIN_ATTEMPTS;
				break;
			}
		}
	}
	if( s_zoneCount ) s_nextZone = ( s_nextZone + 1 ) % s_zoneCount;
	gEngfuncs.pEventAPI->EV_PopPMStates();
}

static void NF_RainVertex( const vec3_t origin, const vec3_t right, const vec3_t up, float x, float y, float u, float v )
{
	gEngfuncs.pTriAPI->TexCoord2f( u, v );
	gEngfuncs.pTriAPI->Vertex3f( origin[0] + right[0] * x + up[0] * y,
		origin[1] + right[1] * x + up[1] * y, origin[2] + right[2] * x + up[2] * y );
}

static void NF_DrawRainDrops( void )
{
	if( !s_dropModel || !gEngfuncs.pTriAPI->SpriteTexture( s_dropModel, 0 )) return;
	gEngfuncs.pTriAPI->Begin( TRI_QUADS );
	for( int i = 0; i < NF_RAIN_DROPS; i++ )
	{
		const nf_rain_drop_t *drop = &s_drops[i];
		if( !drop->used ) continue;
		vec3_t up, right, eye;
		VectorCopy( drop->velocity, up );
		VectorNormalize( up );
		VectorSubtract( v_origin, drop->origin, eye );
		right = CrossProduct( up, eye );
		if( VectorNormalize( right ) < 0.001f )
		{
			vec3_t forward, viewUp;
			AngleVectors( v_angles, forward, right, viewUp );
		}
		float width = drop->width * 0.5f, length = drop->width * 25.0f;
		gEngfuncs.pTriAPI->Color4f( 150.0f / 255.0f, 150.0f / 255.0f, 150.0f / 255.0f, drop->alpha / 255.0f );
		NF_RainVertex( drop->origin, right, up, -width, 0, 0, 1 );
		NF_RainVertex( drop->origin, right, up, -width, -length, 0, 0 );
		NF_RainVertex( drop->origin, right, up, width, -length, 1, 0 );
		NF_RainVertex( drop->origin, right, up, width, 0, 1, 1 );
	}
	gEngfuncs.pTriAPI->End();
}

static void NF_DrawRainSplashes( float time )
{
	if( !s_splashModel ) return;
	for( int frame = 0; frame < s_splashModel->numframes; frame++ )
	{
		if( !gEngfuncs.pTriAPI->SpriteTexture( s_splashModel, frame )) continue;
		gEngfuncs.pTriAPI->Begin( TRI_QUADS );
		for( int i = 0; i < NF_RAIN_SPLASHES; i++ )
		{
			const nf_rain_splash_t *splash = &s_splashes[i];
			if( !splash->used || (int)(( time - splash->birth ) * 30.0f ) != frame ) continue;
			vec3_t right, up, axis = { 0.0f, 1.0f, 0.0f };
			right = CrossProduct( axis, splash->normal );
			VectorNormalize( right );
			up = CrossProduct( splash->normal, right );
			float h = splash->size * 0.5f;
			gEngfuncs.pTriAPI->Color4f( 150.0f / 255.0f, 150.0f / 255.0f, 150.0f / 255.0f, splash->alpha / 255.0f );
			NF_RainVertex( splash->origin, right, up, -h, -h, 0, 1 );
			NF_RainVertex( splash->origin, right, up, -h, h, 0, 0 );
			NF_RainVertex( splash->origin, right, up, h, h, 1, 0 );
			NF_RainVertex( splash->origin, right, up, h, -h, 1, 1 );
		}
		gEngfuncs.pTriAPI->End();
	}
}

void NF_RainRender( void )
{
	if( !s_zoneCount ) return;
	if( !s_dropModel ) s_dropModel = (struct model_s *)gEngfuncs.GetSpritePointer( SPR_Load( "sprites/rain_drop.spz" ));
	if( !s_splashModel ) s_splashModel = (struct model_s *)gEngfuncs.GetSpritePointer( SPR_Load( "sprites/rain_splash.spz" ));
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
			NF_ClearRainParticles();
			dt = 0.0f;
			s_haveTime = true;
		}
		s_lastTime = time;
		NF_UpdateRain( time, dt );
	}
	gEngfuncs.pTriAPI->RenderMode( kRenderTransAdd );
	gEngfuncs.pTriAPI->CullFace( TRI_NONE );
	NF_DrawRainDrops();
	NF_DrawRainSplashes( time );
	gEngfuncs.pTriAPI->RenderMode( kRenderNormal );
	gEngfuncs.pTriAPI->CullFace( TRI_FRONT );
}
