/* Nightfire searchlight visuals using retail sprites and attachment endpoints. */
#include <math.h>
#include <string.h>
#include "hud.h"
#include "cl_util.h"
#include "const.h"
#include "cl_entity.h"
#include "com_model.h"
#include "triangleapi.h"
#include "event_api.h"
#include "r_efx.h"
#include "dlight.h"
#include "pm_defs.h"
#include "pmtrace.h"
#include "nf_debug.h"
#include "nf_searchlight.h"

extern vec3_t v_origin, v_angles;
#define NF_SEARCHLIGHT_MARKER 0x4e46534c
#define NF_SEARCHLIGHT_COUNT 64

struct nf_searchlight_t
{
	int index, message;
	vec3_t start, axis;
	float brightness;
};
static nf_searchlight_t s_lights[NF_SEARCHLIGHT_COUNT];
static struct model_s *s_beam, *s_corona;
static int s_rendered;

void NF_SearchlightVidInit( void )
{
	memset( s_lights, 0, sizeof( s_lights ));
	s_beam = s_corona = NULL;
	s_rendered = 0;
}

static void NF_SearchlightReport( void )
{
	if( NF_DEBUG( NF_DBG_EFFECTS )) gEngfuncs.Con_Printf( "nf_debug: cl: searchlights rendered %d sprites %d/%d\n", s_rendered, s_beam != NULL, s_corona != NULL );
}

void NF_SearchlightInit( void )
{
	gEngfuncs.pfnAddCommand( "nf_searchlightvisualinfo", NF_SearchlightReport );
}

void NF_SearchlightEntity( cl_entity_t *entity )
{
	if( entity->curstate.iuser1 != NF_SEARCHLIGHT_MARKER ) return;
	nf_searchlight_t *slot = NULL;
	for( int i = 0; i < NF_SEARCHLIGHT_COUNT; i++ )
		if( s_lights[i].index == entity->index ) { slot = &s_lights[i]; break; }
	if( !slot )
		for( int i = 0; i < NF_SEARCHLIGHT_COUNT; i++ )
			if( !s_lights[i].index ) { slot = &s_lights[i]; break; }
	if( !slot )
		for( int i = 0; i < NF_SEARCHLIGHT_COUNT; i++ )
			if( s_lights[i].message != entity->curstate.messagenum ) { slot = &s_lights[i]; break; }
	if( !slot ) return;
	if( slot->index != entity->index ) { memset( slot, 0, sizeof( *slot )); slot->index = entity->index; }
	slot->message = entity->curstate.messagenum;
	if( !entity->curstate.iuser2 ) { slot->message = -1; slot->brightness = 0; return; }
	VectorCopy( entity->curstate.vuser1, slot->start );
	VectorSubtract( entity->curstate.vuser2, slot->start, slot->axis );
	float length = Length( slot->axis );
	if( !( length > 0.01f && length < 10000 )) { slot->message = -1; return; }
	VectorScale( slot->axis, 1.0f / length, slot->axis );
}

static void NF_SearchQuad( const vec3_t a, const vec3_t b, const vec3_t side, float width, float alpha )
{
	vec3_t p;
	gEngfuncs.pTriAPI->Color4f( 0.4f, 0.4f, 0.4f, alpha );
	gEngfuncs.pTriAPI->Begin( TRI_QUADS );
	gEngfuncs.pTriAPI->TexCoord2f( 0, 0 ); VectorMA( a, -2, side, p ); gEngfuncs.pTriAPI->Vertex3fv( p );
	gEngfuncs.pTriAPI->TexCoord2f( 1, 0 ); VectorMA( a, 2, side, p ); gEngfuncs.pTriAPI->Vertex3fv( p );
	gEngfuncs.pTriAPI->TexCoord2f( 1, 1 ); VectorMA( b, width, side, p ); gEngfuncs.pTriAPI->Vertex3fv( p );
	gEngfuncs.pTriAPI->TexCoord2f( 0, 1 ); VectorMA( b, -width, side, p ); gEngfuncs.pTriAPI->Vertex3fv( p );
	gEngfuncs.pTriAPI->End();
}

void NF_SearchlightRender( void )
{
	s_rendered = 0;
	cl_entity_t *player = gEngfuncs.GetLocalPlayer();
	if( !player ) return;
	for( int i = 0; i < NF_SEARCHLIGHT_COUNT; i++ )
	{
		nf_searchlight_t *light = &s_lights[i];
		if( !light->index || light->message != player->curstate.messagenum ) continue;
		cl_entity_t *entity = gEngfuncs.GetEntityByIndex( light->index );
		if( !entity || entity->curstate.iuser1 != NF_SEARCHLIGHT_MARKER || !entity->curstate.iuser2 ) continue;
		if( !s_beam ) s_beam = gEngfuncs.CL_LoadModel( "sprites/spotlight_beam.spz", NULL );
		if( !s_corona ) s_corona = gEngfuncs.CL_LoadModel( "sprites/corona_spotlight.spz", NULL );
		if( !s_beam || !s_corona ) continue;
		// Prefer the interpolated model pose; packed attachment positions provide a fallback.
		if(( entity->attachment[1] - entity->attachment[0] ).Length() > 0.01f )
		{
			light->start = entity->attachment[0];
			light->axis = ( entity->attachment[1] - entity->attachment[0] ).Normalize();
		}
		vec3_t end, view, side, up;
		VectorMA( light->start, 8192, light->axis, end );
		pmtrace_t trace;
		gEngfuncs.pEventAPI->EV_SetTraceHull( 2 );
		gEngfuncs.pEventAPI->EV_PlayerTrace( light->start, end, PM_STUDIO_IGNORE, -1, &trace );
		VectorCopy( trace.endpos, end );
		VectorSubtract( end, light->start, view );
		float distance = Length( view );
		if( distance < 1 ) continue;
		VectorSubtract( v_origin, light->start, view );
		side = CrossProduct( light->axis, view );
		if( Length( side ) < 0.01f ) { AngleVectors( v_angles, NULL, side, up ); }
		else VectorScale( side, 1.0f / Length( side ), side );
		float width = Q_max( 10.0f, Q_min( 40.0f, distance / 16.0f ));
		gEngfuncs.pTriAPI->RenderMode( kRenderTransAdd );
		gEngfuncs.pTriAPI->CullFace( TRI_NONE );
		if( gEngfuncs.pTriAPI->SpriteTexture( s_beam, 0 )) NF_SearchQuad( light->start, end, side, width, 0.7f );
		// GoldSrc cannot reproduce the private projected-light renderer: bounded endpoint light.
		dlight_t *dl = gEngfuncs.pEfxAPI->CL_AllocDlight( 0x4e460000 + light->index );
		VectorMA( end, -2, light->axis, dl->origin );
		dl->radius = 110; dl->color.r = dl->color.g = dl->color.b = 255;
		dl->die = gEngfuncs.GetClientTime() + 0.1f;
		float viewDistance = Length( view );
		float facing = viewDistance > 0 ? DotProduct( light->axis, view ) / viewDistance : 0;
		float desired = 0;
		if( facing > 0.5f )
		{
			gEngfuncs.pEventAPI->EV_PlayerTrace( light->start, v_origin, PM_STUDIO_IGNORE, -1, &trace );
			if( trace.fraction == 1 ) desired = viewDistance > 1856 ? 64 : Q_max( 0.0f, ( 1856 - viewDistance ) * 0.13739f );
		}
		float fade = 1840.0f * Q_max( 0.0f, Q_min( 0.1f, (float)gHUD.m_flTimeDelta ));
		light->brightness += Q_max( -fade, Q_min( fade, desired - light->brightness ));
		if( light->brightness > 0 && gEngfuncs.pTriAPI->SpriteTexture( s_corona, 0 ))
		{
			vec3_t right, a, b;
			AngleVectors( v_angles, NULL, right, up );
			VectorMA( light->start, -32, up, a ); VectorMA( light->start, 32, up, b );
			gEngfuncs.pTriAPI->Color4f( 1, 1, 1, light->brightness / 255 );
			gEngfuncs.pTriAPI->Begin( TRI_QUADS );
			gEngfuncs.pTriAPI->TexCoord2f( 0, 1 ); VectorMA( a, -32, right, side ); gEngfuncs.pTriAPI->Vertex3fv( side );
			gEngfuncs.pTriAPI->TexCoord2f( 1, 1 ); VectorMA( a, 32, right, side ); gEngfuncs.pTriAPI->Vertex3fv( side );
			gEngfuncs.pTriAPI->TexCoord2f( 1, 0 ); VectorMA( b, 32, right, side ); gEngfuncs.pTriAPI->Vertex3fv( side );
			gEngfuncs.pTriAPI->TexCoord2f( 0, 0 ); VectorMA( b, -32, right, side ); gEngfuncs.pTriAPI->Vertex3fv( side );
			gEngfuncs.pTriAPI->End();
		}
		s_rendered++;
	}
	gEngfuncs.pTriAPI->CullFace( TRI_FRONT );
	gEngfuncs.pTriAPI->RenderMode( kRenderNormal );
}
