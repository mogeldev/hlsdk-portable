/* Q-Specs and lighter overlays. No renderer/ABI changes. */
#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"
#include "nf_hud.h"
#include "nf_vision.h"
#include "event_api.h"
#include "r_efx.h"
#include "dlight.h"
#include "cl_entity.h"
static int s_mode, s_camera;
static HSPRITE s_vision[3], s_view, s_focus;
static float s_flash;
static int VisionMessage( const char *, int size, void *data ) { BEGIN_READ( data, size ); s_mode = READ_BYTE(); if( s_mode > 3 ) s_mode = 0; return 1; }
static int CameraMessage( const char *, int size, void *data ) { BEGIN_READ( data, size ); int state = READ_BYTE(); if( state == 3 ) s_flash = gHUD.m_flTime + 0.12f; else s_camera = state; return 1; }
static void Toggle( void ) { gEngfuncs.pfnServerCmd( "uvvision\n" ); }
static void Mode( void ) { gEngfuncs.pfnServerCmd( "modeswitch\n" ); }
void NF_VisionInit( void )
{
	gEngfuncs.pfnHookUserMsg( "VisionMode", VisionMessage ); gEngfuncs.pfnHookUserMsg( "CameraMode", CameraMessage );
	gEngfuncs.pfnAddCommand( "uvvision", Toggle ); gEngfuncs.pfnAddCommand( "ActivateGlasses", Toggle );
	gEngfuncs.pfnAddCommand( "modeswitch", Mode ); gEngfuncs.pfnAddCommand( "GlassesMode", Mode );
}
void NF_VisionReset( void ) { s_mode = s_camera = 0; s_flash = 0; for( int i = 0; i < 3; i++ ) s_vision[i] = 0; s_view = s_focus = 0; }
void NF_VisionEntity( cl_entity_t *entity )
{
	if( s_mode < 2 || !entity || entity->curstate.iuser4 != 0x4e465643 ) return;
		entity->curstate.renderfx = s_mode == 3 ? 64 : kRenderFxGlowShell;
		entity->curstate.renderamt = s_mode == 3 ? 160 : 8;
	entity->curstate.rendercolor.r = s_mode == 2 ? 255 : 80;
	entity->curstate.rendercolor.g = s_mode == 2 ? 80 : 180;
	entity->curstate.rendercolor.b = s_mode == 2 ? 40 : 255;
}
void NF_VisionDraw( float time )
{
	if( gHUD.m_Health.m_iHealth <= 0 ) return;
	if( s_mode )
	{
		const char *paths[] = { "sprites/textures/hud_nv.png", "sprites/textures/hud_ir.png", "sprites/textures/hud_xray.png" };
		int mode = s_mode - 1;
		if( !s_vision[mode] ) s_vision[mode] = SPR_Load( paths[mode] );
		NF_DrawImage( s_vision[mode], 0, 0, ScreenWidth, ScreenHeight, 0.65f );
		cl_entity_t *local = gEngfuncs.GetLocalPlayer();
		if( local )
		{
			dlight_t *light = gEngfuncs.pEfxAPI->CL_AllocDlight( 0x4e4600 + local->index );
			light->origin = local->origin; light->radius = 600; light->die = time + 0.1f;
			light->color.r = mode == 1 ? 255 : 80; light->color.g = mode == 0 ? 255 : 100; light->color.b = mode == 2 ? 255 : 80;
		}
	}
	if( s_camera )
	{
		if( !s_view ) s_view = SPR_Load( "gui/hud/640_camera_overlay.PNG" );
		if( !s_focus ) s_focus = SPR_Load( "gui/hud/640_camera_focus_overlay.PNG" );
		NF_DrawImage( s_view, 0, 0, ScreenWidth, ScreenHeight, 1 );
		if( s_camera == 2 ) NF_DrawImage( s_focus, 0, 0, ScreenWidth, ScreenHeight, 1 );
	}
	if( time < s_flash ) gEngfuncs.pfnFillRGBA( 0, 0, ScreenWidth, ScreenHeight, 255, 255, 255, 180 );
}
