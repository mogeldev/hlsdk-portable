/*
nf_wheel.cpp - James Bond 007: Nightfire (PC) weapon/gadget selector

Retail ID-order traversal: client.dll
0x41046c10/0x41046d10; icons: 0x41045b00. Layout, immediate selection,
toggle command and display lifetime are [assumed], not retail parity.
*/
#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"
#include "ammohistory.h"
#include "nf_hud.h"
#include "nf_itemmeta.h"
#include "nf_debug.h"
#include "nf_wheel.h"
#include <stdio.h>
#include <string.h>

extern WEAPON *gpActiveSel;
extern int g_weaponselect;
extern int g_iUser1;

struct nf_wheel_item_t
{
	int wheel, selectable, firearm;
	bool known;
};
static nf_wheel_item_t s_items[MAX_WEAPONS];
static HSPRITE s_ring, s_large[MAX_WEAPONS], s_small[MAX_WEAPONS];
static bool s_iconAttempted[MAX_WEAPONS];
static bool s_ready, s_locked, s_intermission;
static int s_mode, s_last[4], s_active, s_pending;
static float s_until, s_pendingUntil, s_nextSound, s_lastTime;
static cvar_t *s_enable;

bool NF_WheelEnabled( void )
{
	return s_ready && s_enable && s_enable->value != 0;
}

void NF_WheelReset( void )
{
	s_mode = NF_WHEEL_WEAPONS;
	memset( s_last, 0, sizeof( s_last ) );
	s_active = s_pending = 0;
	s_until = s_pendingUntil = s_nextSound = s_lastTime = 0;
	s_locked = true;
	s_intermission = false;
}

void NF_WheelMapReset( void )
{
	NF_WheelReset();
	s_ready = false;
	memset( s_items, 0, sizeof( s_items ) );
}

static bool NF_WheelBlocked( void )
{
	return s_locked || s_intermission || gHUD.m_fPlayerDead || g_iUser1 || gHUD.m_iIntermission ||
		gEngfuncs.IsSpectateOnly() || !gHUD.m_pCvarDraw || !gHUD.m_pCvarDraw->value ||
		( gHUD.m_iHideHUDDisplay & ( HIDEHUD_WEAPONS | HIDEHUD_ALL ) );
}

static bool NF_WheelUsable( int id, int mode )
{
	if( id <= 0 || id >= MAX_WEAPONS || id >= 31 )
		return false;
	WEAPON *weapon = gWR.GetWeapon( id );
	return s_items[id].known && s_items[id].selectable && s_items[id].wheel == mode &&
		weapon->iId == id && ( (unsigned int)gHUD.m_iWeaponBits & ( 1u << id ) ) &&
		( ( mode == NF_WHEEL_GADGETS && ( id == 21 || id == 22 ) ) || gWR.HasAmmo( weapon ) );
}

static int NF_WheelFind( int start, int direction, int mode )
{
	int id = start >= 1 && start <= 30 ? start : ( direction > 0 ? 30 : 1 );
	for( int n = 0; n < 30; n++ )
	{
		id += direction;
		if( id > 30 ) id = 1;
		if( id < 1 ) id = 30;
		if( NF_WheelUsable( id, mode ) )
			return id;
	}
	return 0;
}

static void NF_WheelExpire( float now )
{
	if( now < s_lastTime )
	{
		s_pending = 0;
		s_until = s_nextSound = 0;
	}
	s_lastTime = now;
	if( s_pending && ( now >= s_pendingUntil || !NF_WheelUsable( s_pending, s_mode ) ) )
	{
		if( NF_DEBUG( NF_DBG_WEAPONS ) )
			gEngfuncs.Con_Printf( "nf_debug: wheel request expired id %d active %d\n", s_pending, s_active );
		s_pending = 0;
		if( s_active > 0 && s_active < MAX_WEAPONS && s_items[s_active].wheel )
		{
			s_mode = s_items[s_active].wheel;
			s_last[s_mode] = s_active;
		}
		else
			s_last[s_mode] = 0;
	}
}

static void NF_WheelChoose( int id, int direction )
{
	if( !NF_WheelEnabled() || NF_WheelBlocked() || id <= 0 || id >= MAX_WEAPONS )
		return;
	int mode = s_items[id].wheel;
	if( !NF_WheelUsable( id, mode ) )
		return;
	float now = gEngfuncs.GetClientTime();
	s_mode = mode;
	s_last[mode] = id;
	s_until = now + 2.0f; // [assumed] transient ring, no attack-to-confirm.
	gpActiveSel = NULL;
	g_weaponselect = 0; // all choices use the authoritative server path, including gadgets.
	if( id != s_active || s_pending )
	{
		ServerCmd( gWR.GetWeapon( id )->szName );
		if( NF_DEBUG( NF_DBG_WEAPONS ) )
			gEngfuncs.Con_Printf( "nf_debug: wheel request id %d mode %d\n", id, mode );
		s_pending = id;
		s_pendingUntil = now + 1.0f; // [assumed] rejected/no-ack request recovery.
	}
	if( now >= s_nextSound )
	{
		PlaySound( direction < 0 ? "common/weapon_switch2.wav" : "common/weapon_switch1.wav", 1 );
		s_nextSound = now + 0.5f;
	}
}

void NF_WheelCycle( int direction )
{
	if( !NF_WheelEnabled() || NF_WheelBlocked() )
		return;
	NF_WheelExpire( gEngfuncs.GetClientTime() );
	int id = NF_WheelFind( s_last[s_mode], direction < 0 ? -1 : 1, s_mode );
	if( id ) NF_WheelChoose( id, direction );
}

void NF_WheelActive( int id )
{
	if( id <= 0 || id >= MAX_WEAPONS )
	{
		s_active = s_pending = 0;
		s_until = 0;
		return;
	}
	bool changed = s_active != id;
	s_active = id;
	if( changed && NF_DEBUG( NF_DBG_WEAPONS ) )
		gEngfuncs.Con_Printf( "nf_debug: wheel active id %d\n", id );
	if( !changed && s_pending != id )
		return;
	if( s_pending && s_pending != id )
		return; // an older server response must not cancel a newer queued choice.
	s_pending = 0;
	int mode = s_items[id].known ? s_items[id].wheel : 0;
	if( mode == NF_WHEEL_WEAPONS || mode == NF_WHEEL_GADGETS )
	{
		s_mode = mode;
		s_last[mode] = id;
		if( changed ) s_until = gEngfuncs.GetClientTime() + 2.0f;
	}
	else
		s_until = 0;
}

void NF_WheelMode( int mode )
{
	if( !NF_WheelEnabled() || NF_WheelBlocked() ||
		( mode != NF_WHEEL_WEAPONS && mode != NF_WHEEL_GADGETS ) ) return;
	NF_WheelExpire( gEngfuncs.GetClientTime() );
	int id = s_last[mode];
	if( !NF_WheelUsable( id, mode ) ) id = NF_WheelFind( 0, 1, mode );
	if( id ) NF_WheelChoose( id, 1 );
}

static void NF_WheelToggle( void )
{
	NF_WheelMode( s_mode == NF_WHEEL_WEAPONS ? NF_WHEEL_GADGETS : NF_WHEEL_WEAPONS );
}

static void NF_WheelWeapons( void ) { NF_WheelMode( NF_WHEEL_WEAPONS ); }
static void NF_WheelGadgets( void ) { NF_WheelMode( NF_WHEEL_GADGETS ); }

static void NF_WheelSelectItem( void )
{
	if( gEngfuncs.Cmd_Argc() != 2 || !NF_WheelEnabled() || NF_WheelBlocked() )
		return;
	NF_WheelExpire( gEngfuncs.GetClientTime() );
	const char *name = gEngfuncs.Cmd_Argv( 1 );
	for( int id = 1; id < MAX_WEAPONS && id < 31; id++ )
	{
		WEAPON *weapon = gWR.GetWeapon( id );
		if( weapon->iId == id && !strcmp( name, weapon->szName ) )
		{
			NF_WheelChoose( id, 1 );
			return;
		}
	}
}

static int NF_MsgItemInfo( const char *name, int size, void *buffer )
{
	if( size != 4 ) return 0;
	BEGIN_READ( buffer, size );
	int id = READ_BYTE(), mode = READ_BYTE(), selectable = READ_BYTE(), firearm = READ_BYTE();
	if( id <= 0 || id >= MAX_WEAPONS || id >= 31 ||
		( mode != 0 && mode != 1 && mode != 3 ) || selectable > 1 || firearm > 1 )
		return 0;
	s_items[id].wheel = mode;
	s_items[id].selectable = selectable;
	s_items[id].firearm = firearm;
	s_items[id].known = true;
	s_ready = true;
	return 1;
}

static int NF_MsgWheelLock( const char *name, int size, void *buffer )
{
	if( size != 1 ) return 0;
	BEGIN_READ( buffer, size );
	s_locked = READ_BYTE() != 0;
	if( s_locked )
	{
		s_until = 0;
		s_pending = 0;
	}
	return 1;
}

void NF_WheelInit( void )
{
	s_enable = CVAR_CREATE( "nf_weaponwheel", "1", FCVAR_ARCHIVE );
	gEngfuncs.pfnHookUserMsg( "NFItemInfo", NF_MsgItemInfo );
	gEngfuncs.pfnHookUserMsg( "NFWheelLock", NF_MsgWheelLock );
	gEngfuncs.pfnAddCommand( "SelectItem", NF_WheelSelectItem );
	gEngfuncs.pfnAddCommand( "gadget_toggle", NF_WheelToggle );
	gEngfuncs.pfnAddCommand( "weapon_menu", NF_WheelWeapons );
	gEngfuncs.pfnAddCommand( "gadget_menu", NF_WheelGadgets );
	NF_WheelMapReset();
}

void NF_WheelVidInit( void )
{
	// VidInit also runs on video-mode changes; preserve server metadata and locks.
	s_until = 0;
	memset( s_large, 0, sizeof( s_large ) );
	memset( s_small, 0, sizeof( s_small ) );
	s_ring = SPR_Load( "gui/hud/640_hud_ring.png" );
	memset( s_iconAttempted, 0, sizeof( s_iconAttempted ) );
}

static void NF_WheelLoadIcon( int id )
{
	if( id <= 0 || id >= MAX_WEAPONS || s_iconAttempted[id] ) return;
	const nf_itemmeta_t *meta = NF_ItemMeta( id, gWR.GetWeapon( id )->szName );
	if( !meta || !meta->icon ) return;
	s_iconAttempted[id] = true;
	char path[128];
	snprintf( path, sizeof( path ), "gui/hud/640_weapon_%s_lg.png", meta->icon );
	s_large[id] = SPR_Load( path );
	snprintf( path, sizeof( path ), "gui/hud/640_weapon_%s_sm.png", meta->icon );
	s_small[id] = SPR_Load( path );
}

static void NF_WheelImage( HSPRITE image, float cx, float cy, float scale, float alpha )
{
	if( !image ) return;
	int w = (int)( SPR_Width( image, 0 ) * scale );
	int h = (int)( SPR_Height( image, 0 ) * scale );
	NF_DrawImage( image, (int)cx - w / 2, (int)cy - h / 2, w, h, alpha );
}

void NF_WheelDraw( float time, int intermission )
{
	s_intermission = intermission != 0;
	if( !NF_WheelEnabled() || intermission || NF_WheelBlocked() ) return;
	float now = gEngfuncs.GetClientTime();
	NF_WheelExpire( now );
	if( now >= s_until ) return;
	int id = s_last[s_mode];
	if( !NF_WheelUsable( id, s_mode ) ) return;
	NF_WheelLoadIcon( id );
	float scale = ScreenWidth / 640.0f;
	if( ScreenHeight / 480.0f < scale ) scale = ScreenHeight / 480.0f;
	float x = ScreenWidth * 0.5f, y = ScreenHeight - 104.0f * scale;
	// [assumed] centred ring and two neighbours; exact retail panel geometry unresolved.
	NF_WheelImage( s_ring, x, y, scale, 0.85f );
	NF_WheelImage( s_large[id], x, y, scale, s_pending ? 0.65f : 1.0f );
	int prev = NF_WheelFind( id, -1, s_mode ), next = NF_WheelFind( id, 1, s_mode );
	int neighbours[2] = { prev, next };
	for( int n = 0; n < 2; n++ )
	{
		int other = neighbours[n];
		if( !other || other == id || ( n == 1 && other == prev ) ) continue;
		NF_WheelLoadIcon( other );
		NF_WheelImage( s_small[other], x + ( n ? 96.0f : -96.0f ) * scale, y, scale, 0.65f );
	}
	WEAPON *weapon = gWR.GetWeapon( id );
	char text[160];
	snprintf( text, sizeof( text ), "%s%s  %d / %d", s_pending ? "... " : "",
		weapon->szName, weapon->iClip, weapon->iAmmoType >= 0 ? gWR.CountAmmo( weapon->iAmmoType ) : 0 );
	int width = gHUD.DrawHudStringLen( text );
	gHUD.DrawHudString( (int)x - width / 2, (int)( y + 64.0f * scale ), ScreenWidth, text, 100, 180, 255 );
}
