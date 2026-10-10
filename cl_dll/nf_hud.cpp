/*
nf_hud.cpp - James Bond 007: Nightfire (PC) in-game HUD: health iris, ammo
panel, crosshair and sniper scope

The retail client draws its HUD from plain images (gui/hud/640_*.png, loaded
by the engine fork as one-frame sprites) at their own pixel size, no scaling
(retail client.dll, docs/retail/hud.md "In-game HUD"):

* Health (CHealthHudPanel, ctor 0x41040790, layout 0x410405f0): lower left
  corner, 640_health_iris_circle.png under one of the iris frames
  640_health_Iris_08 ... _00, frame = health / 24 (0x410406f0, at most 8;
  the "Health" message, the retail player has 200 health). A hint flashes
  640_health_Iris_09 on top: four on/off steps of 1 s with
  common/hint_beep.wav (0x41040740 / 0x41040510).
* Ammo (CAmmoHudPanel, factory 0x4103b8b0; one panel per weapon id - 1):
  lower right corner, 640_ammo_circle.png under 640_<ammo>_clip.png. The
  magazine weapons (CStaggeredClipAmmoPanel 0x41044780) add
  640_<ammo>_clip_back.png and _clip_front.png (rounds, two columns): with
  r rounds gone the back image shows its rows 0 .. y1 - step * (r / 2) and
  the front one rows 0 .. y0 - step * (r - r / 2) (0x41044610). Minigun and
  shotgun (CClipAmmoPanel 0x4103e270) show one _clip_front.png, rows
  0 .. y0 - step * (r / div) (0x4103e190).
* Crosshair (CCrosshairPanel 0x4103ec00): 640_crosshair_01.png centred.
* Colours: the grey images are tinted with gui/hud/colors.txt (read at
  0x4104b52e, four "r, g, b" lines, team 0 = lines 1 and 3): the iris and
  ammo circles with the first colour, the iris blades with the third
  (matches retail PC screenshots: dark blue discs, light blue blades). The
  coloured images (clips, rounds, crosshair, Iris_09) are drawn as they are.
* The three-digit count (CAmmoCountPanel 0x4103b560, 640_hud_numbers.png)
  belongs to the weapon wheel (created with weapon_wheel / gadget_wheel at
  0x4103bee3) and is not drawn here.
* Use icon (ctor 0x41040c40, setter 0x41040bf0, layout 0x41040b10; message
  "SetHudIcon", handler 0x41049d00): one byte, 0 hides, 1-5 show
  640_use / _use_level_trans / _use_pda / _use_qworm / _use_watch at their
  own size, centred at the bottom edge. Sent by trigger_changelevelicon (2)
  and the server's view check (5 on a laser target, dlls/nf_lasertarget.cpp).
* Progress bar (CProgressPanel 0x41043340, message "Progress", handler
  0x41049b70 -> 0x41048ac0): short entindex, coord max, byte visible;
  640_progress_meter_back.png with _front.png over it, filled from the left
  to value * width / max, value = the target entity's fuser1 (its health,
  layout 0x41043150: centred, 64 px above the bottom edge [assumed: of the
  image's bottom]); hidden only by byte 0.
* Stinger (CStingerPanel, ctor 0x41044ad0, message "ShowStinger" from
  trigger_bondmoment, no data): 640_stinger.png (the 007 logo) at its own
  size in the top left corner for 5 s (think 0x41044a30), no fade.
* Mission stats ("ScoreInfoS", hook 0x41049530 -> CSinglePlayerScoreboard
  0x4104e3d0): only kept, for the mission score screen that the menu
  (mainui ui_nf_missionscores) draws during the changelevel. The cvar
  nf_scoreinfo holds "frags enemies nonlethal shots hits moments
  totalmoments secrets totalsecrets time partime"; partime = titles.txt
  PARTIME minutes * 60 (retail 0x4104e7e0, missing -> 1800 s).

Without the images the port's text panels (hud_redraw.cpp) stay.
*/

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "hud.h"
#include "cl_util.h"
#include "triangleapi.h"
#include "parsemsg.h"
#include "nf_hud.h"
#include "nf_debug.h"

extern float g_lastFOV;

enum
{
	NF_AMMO_NONE = 0,
	NF_AMMO_IMAGE,		// circle + clip image only
	NF_AMMO_STAGGERED,	// + back / front rounds (clip, y0 front, y1 back, step)
	NF_AMMO_CLIP,		// + front rounds (clip, y0, step, div)
	NF_AMMO_CYLINDER
};

typedef struct
{
	int type;
	const char *name;
	int clip, a, b, c;
} nf_ammopanel_t;

// retail factory 0x4103b8b0, index = weapon id - 1
static const nf_ammopanel_t s_ammoPanels[32] =
{
	{ NF_AMMO_IMAGE, "ammo_dukes" },			// 1 dukes
	{ NF_AMMO_STAGGERED, "ammo_p99", 16, 105, 105, 5 },	// 2 PP9
	{ NF_AMMO_STAGGERED, "ammo_p99", 16, 105, 105, 5 },	// 3 Kowloon
	{ NF_AMMO_STAGGERED, "ammo_raptor", 9, 35, 31, 7 },	// 4 Raptor
	{ NF_AMMO_STAGGERED, "ammo_mp9", 32, 116, 116, 4 },	// 5 MP9
	{ NF_AMMO_STAGGERED, "ammo_mp9", 32, 116, 116, 4 },	// 6 MP9 silenced
	{ NF_AMMO_STAGGERED, "ammo_commando", 30, 105, 105, 3 },	// 7 SIG552
	{ NF_AMMO_STAGGERED, "ammo_pdw90", 50, 113, 113, 2 },	// 8 P90
	{ NF_AMMO_CLIP, "ammo_minigun", 100, 42, 2, 5 },	// 9 minigun
	{ NF_AMMO_CLIP, "ammo_shotgun", 8, 113, 7, 1 },		// 10 Frinesi
	{ NF_AMMO_CYLINDER, "ammo_up11", 5 },			// 11 UP11
	{ NF_AMMO_STAGGERED, "ammo_l96a1", 10, 95, 95, 5 },	// 12 L96A1
	{ NF_AMMO_STAGGERED, "ammo_l96a1", 10, 95, 95, 5 },	// 13 L96A1 winter
	{ NF_AMMO_IMAGE, "ammo_smoke" },			// 14
	{ NF_AMMO_IMAGE, "ammo_flash" },			// 15
	{ NF_AMMO_IMAGE, "ammo_frag" },				// 16
	{ NF_AMMO_IMAGE, "ammo_trip" },				// 17
	{ NF_AMMO_IMAGE, "ammo_ronin" },			// 18 Ronin
	{ NF_AMMO_CYLINDER, "ammo_grenade_launcher", 6 },	// 19
	{ NF_AMMO_CYLINDER, "ammo_rocket", 4 },		// 20
	{ NF_AMMO_IMAGE, "ammo_battery" },			// 21 watch
	{ NF_AMMO_IMAGE, "ammo_battery" },			// 22 taser
	{ NF_AMMO_IMAGE, "ammo_dart" },				// 23 pen
	{ NF_AMMO_IMAGE, "ammo_battery" },			// 24 PDA
	{ NF_AMMO_IMAGE, "ammo_film" },				// 25 lighter
	{ NF_AMMO_IMAGE, "ammo_grapple" },			// 26 grapple
	{ NF_AMMO_IMAGE, "ammo_qworm" },			// 27 Q-worm
	{ NF_AMMO_NONE, NULL },					// 28
	{ NF_AMMO_IMAGE, "ammo_battery" },			// 29 laser rifle
	{ NF_AMMO_NONE, NULL },
	{ NF_AMMO_NONE, NULL },
	{ NF_AMMO_IMAGE, "ammo_hat" },				// 32
};

static HSPRITE s_hIrisCircle, s_hIris[9], s_hIrisFlash;
static HSPRITE s_hAmmoCircle, s_hCrosshair, s_hSniperScope;
static int s_iScopeWeapon, s_iScopeFOV;
static HSPRITE s_hClip[32], s_hBack[32], s_hFront[32];
static HSPRITE s_hCylinder[32][6];
static bool s_bLoaded;

// use icon (0x41040c40): index 1-5, 0 = hidden
#define NF_USEICONS 6
static const char *s_useIcons[NF_USEICONS] =
{
	NULL, "use", "use_level_trans", "use_pda", "use_qworm", "use_watch"
};
static HSPRITE s_hUseIcon[NF_USEICONS];
static int s_iUseIcon;

// progress bar (0x41043340): laser target entity and its full value
static HSPRITE s_hProgressBack, s_hProgressFront;
static int s_iProgressEnt;
static float s_flProgressMax;
static bool s_bProgress;

// stinger (0x41044ad0): shown until this time
static HSPRITE s_hStinger;
static float s_flStingerEnd;

// gui/hud/colors.txt (retail defaults if missing): 0 = team 0 base, 1 = team 1 base,
// 2 = team 0 accent, 3 = team 1 accent
static float s_colors[4][3] =
{
	{ 40 / 255.0f, 60 / 255.0f, 1.0f },
	{ 1.0f, 60 / 255.0f, 40 / 255.0f },
	{ 10 / 255.0f, 170 / 255.0f, 1.0f },
	{ 1.0f, 170 / 255.0f, 10 / 255.0f },
};
static const float s_white[3] = { 1.0f, 1.0f, 1.0f };

// hint flash (0x41040740): step 1..4, odd steps show 640_health_Iris_09
static int s_iFlashStep;
static float s_flFlashNext;

static int NF_SpriteW( HSPRITE h )
{
	return h ? SPR_Width( h, 0 ) : 0;
}

static int NF_SpriteH( HSPRITE h )
{
	return h ? SPR_Height( h, 0 ) : 0;
}

// textured quad, the image modulated by rgb (white = as it is)
static void NF_DrawQuad( HSPRITE hspr, int x, int y, int w, int h, float s0, float t0, float s1, float t1, const float *rgb, float alpha )
{
	const struct model_s *model = hspr ? gEngfuncs.GetSpritePointer( hspr ) : NULL;
	if( !model || w <= 0 || h <= 0 )
		return;

	gEngfuncs.pTriAPI->SpriteTexture( (struct model_s *)model, 0 );
	gEngfuncs.pTriAPI->RenderMode( kRenderTransAlpha );
	gEngfuncs.pTriAPI->CullFace( TRI_NONE );
	gEngfuncs.pTriAPI->Color4f( rgb[0], rgb[1], rgb[2], alpha );
	gEngfuncs.pTriAPI->Begin( TRI_QUADS );
		gEngfuncs.pTriAPI->TexCoord2f( s0, t0 );
		gEngfuncs.pTriAPI->Vertex3f( x, y, 0.0f );
		gEngfuncs.pTriAPI->TexCoord2f( s1, t0 );
		gEngfuncs.pTriAPI->Vertex3f( x + w, y, 0.0f );
		gEngfuncs.pTriAPI->TexCoord2f( s1, t1 );
		gEngfuncs.pTriAPI->Vertex3f( x + w, y + h, 0.0f );
		gEngfuncs.pTriAPI->TexCoord2f( s0, t1 );
		gEngfuncs.pTriAPI->Vertex3f( x, y + h, 0.0f );
	gEngfuncs.pTriAPI->End();
	gEngfuncs.pTriAPI->RenderMode( kRenderNormal );
}

void NF_DrawImage( HSPRITE hspr, int x, int y, int w, int h, float alpha )
{
	NF_DrawQuad( hspr, x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, s_white, alpha );
}

void NF_DrawImagePart( HSPRITE hspr, int x, int y, int w, int h, float s0, float t0, float s1, float t1, float alpha )
{
	NF_DrawQuad( hspr, x, y, w, h, s0, t0, s1, t1, s_white, alpha );
}

// a whole image at its own size, its bottom-left corner at (x, bottom)
static void NF_DrawCorner( HSPRITE h, int x, int bottom, const float *rgb )
{
	NF_DrawQuad( h, x, bottom - NF_SpriteH( h ), NF_SpriteW( h ), NF_SpriteH( h ), 0.0f, 0.0f, 1.0f, 1.0f, rgb, 1.0f );
}

// draw rows 0 .. rows of an image, the image anchored at (x, y)
static void NF_DrawImageTop( HSPRITE h, int x, int y, int rows )
{
	int w = NF_SpriteW( h ), ht = NF_SpriteH( h );
	if( ht <= 0 || rows <= 0 )
		return;
	if( rows > ht )
		rows = ht;
	NF_DrawQuad( h, x, y, w, rows, 0.0f, 0.0f, 1.0f, (float)rows / ht, s_white, 1.0f );
}

// retail 0x4104b52e: up to four "r, g, b" lines, each clamped to 0..255
static void NF_LoadColors( void )
{
	int length = 0;
	char *file = (char *)gEngfuncs.COM_LoadFile( (char *)"gui/hud/colors.txt", 5, &length );
	if( !file )
		return;

	const char *p = file;
	for( int i = 0; i < 4 && p && *p; i++ )
	{
		int c[3];
		if( sscanf( p, "%d , %d , %d", &c[0], &c[1], &c[2] ) != 3 )
			break;
		for( int k = 0; k < 3; k++ )
			s_colors[i][k] = ( c[k] < 0 ? 0 : c[k] > 255 ? 255 : c[k] ) / 255.0f;
		p = strchr( p, '\n' );
		if( p )
			p++;
	}
	gEngfuncs.COM_FreeFile( file );
}

void NF_HudVidInit( void )
{
	char name[96];

	// sprites are freed on every map change
	s_hIrisCircle = SPR_Load( "gui/hud/640_health_iris_circle.png" );
	for( int i = 0; i < 9; i++ )
	{
		snprintf( name, sizeof( name ), "gui/hud/640_health_Iris_%02d.png", i );
		s_hIris[i] = SPR_Load( name );
	}
	s_hIrisFlash = SPR_Load( "gui/hud/640_health_Iris_09.png" );
	s_hAmmoCircle = SPR_Load( "gui/hud/640_ammo_circle.png" );
	s_hCrosshair = SPR_Load( "gui/hud/640_crosshair_01.png" );
	s_hSniperScope = SPR_Load( "gui/hud/sniper_1024.png" );
	s_iScopeWeapon = s_iScopeFOV = 0;

	for( int i = 0; i < 32; i++ )
	{
		const nf_ammopanel_t *p = &s_ammoPanels[i];
		s_hClip[i] = s_hBack[i] = s_hFront[i] = 0;
		memset( s_hCylinder[i], 0, sizeof( s_hCylinder[i] ) );
		if( p->type == NF_AMMO_NONE )
			continue;

		snprintf( name, sizeof( name ), "gui/hud/640_%s_clip.png", p->name );
		s_hClip[i] = SPR_Load( name );
		if( p->type == NF_AMMO_CYLINDER )
		{
			for( int frame = 0; frame < p->clip && frame < 6; frame++ )
			{
				snprintf( name, sizeof( name ), "gui/hud/640_%s_clip_%02d.png", p->name, frame );
				s_hCylinder[i][frame] = SPR_Load( name );
			}
		}
		if( p->type == NF_AMMO_STAGGERED )
		{
			snprintf( name, sizeof( name ), "gui/hud/640_%s_clip_back.png", p->name );
			s_hBack[i] = SPR_Load( name );
		}
		if( p->type == NF_AMMO_STAGGERED || p->type == NF_AMMO_CLIP )
		{
			snprintf( name, sizeof( name ), "gui/hud/640_%s_clip_front.png", p->name );
			s_hFront[i] = SPR_Load( name );
		}
	}

	for( int i = 1; i < NF_USEICONS; i++ )
	{
		snprintf( name, sizeof( name ), "gui/hud/640_%s.png", s_useIcons[i] );
		s_hUseIcon[i] = SPR_Load( name );
	}

	s_hProgressBack = SPR_Load( "gui/hud/640_progress_meter_back.png" );
	s_hProgressFront = SPR_Load( "gui/hud/640_progress_meter_front.png" );
	s_hStinger = SPR_Load( "gui/hud/640_stinger.png" );

	s_bLoaded = s_hIrisCircle && s_hIris[0] && s_hAmmoCircle;
	NF_LoadColors();
	s_iFlashStep = 0;
	s_iUseIcon = 0;	// retail: the level change sends 0
	s_bProgress = false;
	s_flStingerEnd = 0.0f;
}

static int __MsgFunc_ShowStinger( const char *pszName, int iSize, void *pbuf )
{
	s_flStingerEnd = gHUD.m_flTime + 5.0f;
	if( NF_DEBUG( NF_DBG_TRIGGERS ))
		gEngfuncs.Con_Printf( "nf_debug: cl: stinger shown\n" );
	return 1;
}

static int __MsgFunc_ScoreInfoS( const char *pszName, int iSize, void *pbuf )
{
	char buf[128];
	int frags, enemies, nonlethal, shots, hits, moments, totalMoments, secrets, totalSecrets;
	float time, par = 0.0f;
	client_textmessage_t *partime = gEngfuncs.pfnTextMessageGet( "PARTIME" );

	BEGIN_READ( pbuf, iSize );
	READ_BYTE();	// entindex
	frags = READ_SHORT();
	READ_SHORT();	// deaths, not shown
	enemies = READ_SHORT();
	nonlethal = READ_SHORT();
	shots = READ_SHORT();
	hits = READ_SHORT();
	READ_SHORT();	// damage taken, not shown
	READ_SHORT();	// favourite weapon, not shown
	moments = READ_BYTE();
	totalMoments = READ_BYTE();
	secrets = READ_BYTE();
	totalSecrets = READ_BYTE();
	time = READ_FLOAT();

	if( partime && partime->pMessage )
		par = atoi( partime->pMessage ) * 60.0f;

	snprintf( buf, sizeof( buf ), "%d %d %d %d %d %d %d %d %d %.1f %.0f", frags, enemies, nonlethal, shots, hits,
		moments, totalMoments, secrets, totalSecrets, time, par );
	gEngfuncs.Cvar_Set( "nf_scoreinfo", buf );
	if( NF_DEBUG( NF_DBG_TRIGGERS ))
		gEngfuncs.Con_Printf( "nf_debug: cl: scoreinfo %s\n", buf );
	return 1;
}

static int __MsgFunc_Progress( const char *pszName, int iSize, void *pbuf )
{
	BEGIN_READ( pbuf, iSize );
	s_iProgressEnt = READ_SHORT();
	s_flProgressMax = READ_COORD();
	s_bProgress = READ_BYTE() != 0;
	if( NF_DEBUG( NF_DBG_ITEMS ))
		gEngfuncs.Con_Printf( "nf_debug: cl: progress ent %d max %.0f %s\n", s_iProgressEnt, s_flProgressMax, s_bProgress ? "shown" : "hidden" );
	return 1;
}

static int __MsgFunc_SetHudIcon( const char *pszName, int iSize, void *pbuf )
{
	BEGIN_READ( pbuf, iSize );
	int icon = READ_BYTE();

	// retail 0x41040bf0: values from 6 up are ignored
	if( icon < NF_USEICONS )
		s_iUseIcon = icon;
	return 1;
}

void NF_HudInit( void )
{
	HOOK_MESSAGE( SetHudIcon );
	HOOK_MESSAGE( Progress );
	HOOK_MESSAGE( ShowStinger );
	HOOK_MESSAGE( ScoreInfoS );
	gEngfuncs.pfnRegisterVariable( "nf_scoreinfo", "", 0 );
}

bool NF_HudActive( void )
{
	return s_bLoaded;
}

bool NF_HudScopeActive( void )
{
	int id = gHUD.m_Ammo.GetCurrentWeaponId();
	return s_bLoaded && s_hSniperScope && gHUD.m_pCvarDraw && gHUD.m_pCvarDraw->value &&
		!( gHUD.m_iHideHUDDisplay & ( HIDEHUD_ALL | HIDEHUD_WEAPONS )) &&
		!gHUD.m_fPlayerDead && !gEngfuncs.IsSpectateOnly() &&
		( id == 12 || id == 13 ) &&
		( g_lastFOV == 40.0f || g_lastFOV == 10.0f );
}

void NF_HudFlash( void )
{
	// retail 0x41040740: beep now, then the on/off steps
	s_iFlashStep = 1;
	s_flFlashNext = gHUD.m_flTime + 1.0f;
}

static void NF_DrawHealth( float flTime )
{
	int health = gHUD.m_Health.m_iHealth;
	int frame = health / 24;	// retail 0x410406f0
	if( frame < 0 )
		frame = 0;
	if( frame > 8 )
		frame = 8;

	// team 0 colours: the circle with the base colour, the blades with the accent
	NF_DrawCorner( s_hIrisCircle, 0, ScreenHeight, s_colors[0] );
	NF_DrawCorner( s_hIris[8 - frame], 0, ScreenHeight, s_colors[2] );

	// hint flash (0x41040510): four 1 s steps, the beep with each odd one
	if( s_iFlashStep > 0 && flTime >= s_flFlashNext )
	{
		s_iFlashStep++;
		s_flFlashNext = flTime + 1.0f;
		if( s_iFlashStep > 4 )
			s_iFlashStep = 0;
		else if( s_iFlashStep & 1 )
			gEngfuncs.pfnPlaySoundByName( "common/hint_beep.wav", 1.0f );
	}
	if( s_iFlashStep & 1 )
		NF_DrawCorner( s_hIrisFlash, 0, ScreenHeight, s_white );
}

static void NF_DrawAmmo( void )
{
	int id = gHUD.m_Ammo.GetCurrentWeaponId();
	if( id < 1 || id > 32 )
		return;

	const nf_ammopanel_t *p = &s_ammoPanels[id - 1];
	if( p->type == NF_AMMO_NONE )
		return;

	int rounds = gHUD.m_Ammo.GetCurrentWeaponClip();
	HSPRITE clip = s_hClip[id - 1];
	if( p->type == NF_AMMO_CYLINDER && rounds > 0 && rounds <= p->clip )
	{
		// Retail CCylinderAmmoPanel selects the texture at capacity - rounds.
		HSPRITE cylinder = s_hCylinder[id - 1][p->clip - rounds];
		if( cylinder ) clip = cylinder;
	}
	int w = clip ? NF_SpriteW( clip ) : NF_SpriteW( s_hAmmoCircle );
	int h = clip ? NF_SpriteH( clip ) : NF_SpriteH( s_hAmmoCircle );
	int x = ScreenWidth - w, y = ScreenHeight - h;

	// CAmmoImagePanel 0x4103c060: the circle under the clip image, both in the clip image's rectangle
	NF_DrawQuad( s_hAmmoCircle, x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, s_colors[0], 1.0f );
	NF_DrawImage( clip, x, y, w, h, 1.0f );

	if( rounds >= 0 && p->clip > 0 )
	{
		int gone = p->clip - rounds;
		if( gone < 0 )
			gone = 0;

		if( p->type == NF_AMMO_STAGGERED )
		{
			HSPRITE back = s_hBack[id - 1], front = s_hFront[id - 1];
			NF_DrawImageTop( back, ScreenWidth - NF_SpriteW( back ), ScreenHeight - NF_SpriteH( back ), p->b - p->c * ( gone / 2 ));
			NF_DrawImageTop( front, ScreenWidth - NF_SpriteW( front ), ScreenHeight - NF_SpriteH( front ), p->a - p->c * ( gone - gone / 2 ));
		}
		else if( p->type == NF_AMMO_CLIP )
		{
			HSPRITE front = s_hFront[id - 1];
			int div = p->c > 0 ? p->c : 1;
			NF_DrawImageTop( front, ScreenWidth - NF_SpriteW( front ), ScreenHeight - NF_SpriteH( front ), p->a - p->b * ( gone / div ));
		}
	}

}

void NF_HudDraw( float flTime )
{
	bool scoped = NF_HudScopeActive();
	int scopeWeapon = scoped ? gHUD.m_Ammo.GetCurrentWeaponId() : 0;
	int scopeFOV = scoped ? (int)g_lastFOV : 0;
	if( scopeWeapon != s_iScopeWeapon || scopeFOV != s_iScopeFOV )
	{
		if( NF_DEBUG( NF_DBG_WEAPONS ))
			gEngfuncs.Con_Printf( "nf_debug: cl: sniper scope %s id %d fov %d\n", scoped ? "on" : "off", scopeWeapon, scopeFOV );
		s_iScopeWeapon = scopeWeapon;
		s_iScopeFOV = scopeFOV;
	}
	if( !s_bLoaded || !gHUD.m_pCvarDraw || !gHUD.m_pCvarDraw->value ||
		( gHUD.m_iHideHUDDisplay & HIDEHUD_ALL ) || gEngfuncs.IsSpectateOnly() )
		return;

	if( scoped )
		NF_DrawImage( s_hSniperScope, 0, 0, ScreenWidth, ScreenHeight, 1.0f );

	if( !( gHUD.m_iHideHUDDisplay & HIDEHUD_HEALTH ))
		NF_DrawHealth( flTime );

	if( !( gHUD.m_iHideHUDDisplay & HIDEHUD_WEAPONS ))
	{
		NF_DrawAmmo();

		if( !scoped && s_hCrosshair && gHUD.m_Ammo.GetCurrentWeaponId() > 0 )
		{
			int w = NF_SpriteW( s_hCrosshair ), h = NF_SpriteH( s_hCrosshair );
			NF_DrawImage( s_hCrosshair, ( ScreenWidth - w ) / 2, ( ScreenHeight - h ) / 2, w, h, 1.0f );
		}
	}

	// use icon, layout 0x41040b10: centred on the bottom edge
	if( s_iUseIcon > 0 && s_hUseIcon[s_iUseIcon] )
	{
		HSPRITE h = s_hUseIcon[s_iUseIcon];
		NF_DrawCorner( h, ( ScreenWidth - NF_SpriteW( h )) / 2, ScreenHeight, s_white );
	}

	// progress bar, paint 0x410430b0: the front image cut to value / max
	cl_entity_t *pTarget = s_bProgress ? gEngfuncs.GetEntityByIndex( s_iProgressEnt ) : NULL;
	if( pTarget && s_hProgressBack && s_hProgressFront && s_flProgressMax > 0 )
	{
		int w = NF_SpriteW( s_hProgressBack ), h = NF_SpriteH( s_hProgressBack );
		int x = ( ScreenWidth - w ) / 2, y = ScreenHeight - 64 - h;
		NF_DrawImage( s_hProgressBack, x, y, w, h, 1.0f );

		int fw = NF_SpriteW( s_hProgressFront ), fh = NF_SpriteH( s_hProgressFront );
		float frac = pTarget->curstate.fuser1 / s_flProgressMax;
		frac = frac < 0 ? 0 : frac > 1 ? 1 : frac;
		int fill = (int)( fw * frac );
		if( fill > 0 )
			NF_DrawImagePart( s_hProgressFront, ( ScreenWidth - fw ) / 2, y + ( h - fh ) / 2, fill, fh, 0.0f, 0.0f, frac, 1.0f, 1.0f );
	}

	// stinger, layout 0x41044a60: top left corner, own size
	if( s_hStinger && flTime < s_flStingerEnd )
		NF_DrawImage( s_hStinger, 0, 0, NF_SpriteW( s_hStinger ), NF_SpriteH( s_hStinger ), 1.0f );
}
