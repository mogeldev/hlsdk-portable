/*
nf_hudmsg.cpp - James Bond 007: Nightfire (PC) level messages ("HudMsg")
and mission objectives ("Objective")

Sent by trigger_hudmessage (dlls/nf_triggers.cpp): title name, timed flag,
hint flag, duration in tenths of a second. The retail client
(client.dll 0x410486d0) looks the name up with TextMessageGet (a leading
'#' is skipped; unknown names are shown as they are). The engine fills that
table from maps/<map>.tit. With the hint flag the text goes into the hint
section of the objectives panel (CObjectiveOverviewPanel, a ring of 4
lines, 0x41041f60) and common/hint_beep.wav plays (0x41040740); with the
timed flag it is shown in the message box (CObjectivePanel) for the given
time. An empty name clears the hint section. The HUD toggle does not hide
either panel.

"Objective" (trigger_objective, dlls/nf_triggers.cpp; retail client.dll
0x41048590): reset byte (clear the list, the hints and the box), id (255 =
nothing more), message title, list title, box byte, list byte, seconds,
completed. With a message: the box byte shows the message in the box for
the given time with common/obj_open.wav (not while the overview is open);
the list byte adds the entry (list title, else the message title; an id
that is there already keeps its text, 0x410417c0). The completed byte is
always applied (check / circle icon in retail).

Objective overview (CObjectiveOverviewPanel): in single player the
scoreboard key (+showscores) opens it (retail 0x41046e10 toggles it on
press and closes it on release, 0x41046e50); it lists the objectives and
the hint section. NF_ObjectivesShowOverview is called from input.cpp.

Look: the retail images (the engine fork loads image files given to
SPR_Load as one-frame sprites), drawn with TriAPI, scaled by screen height
/ 768 like retail, left margin screen height / 30. Message box
(CObjectivePanel, 0x41042510): gui/hud/hud_objective_bg.png (1024x128),
bottom 72 px above the screen bottom; objective overview
(CObjectiveOverviewPanel, 0x410422b0): gui/hud/hud_objectives_hint_bg.png
(512x512, "OBJECTIVES:" and "HINTS:" are part of the image), bottom 80 px
above the screen bottom, entries with gui/hud/check.png / circle.png. Both
are moved up when they would cover the port's health panel. Text offsets
inside the panels, the slide-in animation and the retail font are not
traced [assumed]. Like retail there is no hint box on the screen: a hint
only beeps, flashes the health iris and goes into the hint section, which
keeps the last 4 lines (retail ring, 0x41041f60).
*/

#include <string.h>
#include <stdio.h>

#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"
#include "nf_debug.h"
#include "nf_hudmsg.h"
#include "nf_hud.h"

#define NF_MSG_LINES		12
#define NF_MSG_LINELEN		256
#define NF_MSG_DEFAULT_TIME	5.0f

typedef struct
{
	char lines[NF_MSG_LINES][NF_MSG_LINELEN];
	int numLines;
	float endTime;
} nf_msgbox_t;

#define NF_HINT_LINES		4	// retail ring of 4 lines x 256 chars

static char s_hints[NF_HINT_LINES][NF_MSG_LINELEN];	// hint section, oldest first
static int s_hintCount;
static nf_msgbox_t s_timed;	// timed message box (centre)
static char s_text[2048];	// last received text

#define NF_OBJ_MAX		64
#define NF_OBJ_TEXTLEN		256

typedef struct
{
	int id;
	char text[NF_OBJ_TEXTLEN];
	int completed;
} nf_objentry_t;

static nf_objentry_t s_obj[NF_OBJ_MAX];	// objective list, in arrival order
static int s_objCount;
static bool s_overview;			// objective overview open

// retail panel images (0 = not found: plain boxes)
static HSPRITE s_hBoxBg, s_hOverviewBg, s_hCheck, s_hCircle;

// retail layout: images are scaled by screen height / 768
static float NF_PanelScale( void )
{
	return ScreenHeight / 768.0f;
}

// top of the port's text health panel (hud_redraw.cpp DrawNightfireStatus);
// with the retail HUD the panels keep their retail place (over the iris, like retail)
static int NF_HealthPanelTop( void )
{
	if( NF_HudActive( ))
		return ScreenHeight;
	return ScreenHeight - YRES( 58 );
}

// word-wrap text (with '\n' paragraphs) into box lines no wider than maxWidth pixels
static void NF_WrapText( nf_msgbox_t *box, const char *text, int maxWidth )
{
	char line[NF_MSG_LINELEN];
	int len = 0;

	box->numLines = 0;
	line[0] = '\0';

	for( const char *p = text; ; )
	{
		// next word (or a forced break)
		while( *p == ' ' || *p == '\t' || *p == '\r' )
			p++;

		if( *p == '\0' || *p == '\n' )
		{
			if( len > 0 || *p == '\n' )
			{
				if( box->numLines < NF_MSG_LINES )
					strcpy( box->lines[box->numLines++], line );
				len = 0;
				line[0] = '\0';
			}
			if( *p == '\0' )
				break;
			p++;
			continue;
		}

		const char *w = p;
		while( *p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' )
			p++;
		int wlen = (int)( p - w );

		char candidate[NF_MSG_LINELEN];
		if( len > 0 )
			snprintf( candidate, sizeof( candidate ), "%s %.*s", line, wlen, w );
		else
			snprintf( candidate, sizeof( candidate ), "%.*s", wlen, w );

		int width, height;
		GetConsoleStringSize( candidate, &width, &height );

		if( len > 0 && width > maxWidth )
		{
			if( box->numLines < NF_MSG_LINES )
				strcpy( box->lines[box->numLines++], line );
			snprintf( line, sizeof( line ), "%.*s", wlen, w );
		}
		else
			strcpy( line, candidate );
		len = (int)strlen( line );
	}

	// drop trailing empty lines
	while( box->numLines > 0 && box->lines[box->numLines - 1][0] == '\0' )
		box->numLines--;
}

// retail 0x41041f60: each line of the text (split at '\n', 255 chars) goes
// into the ring; the oldest line drops out
static void NF_HintAdd( const char *text )
{
	for( const char *p = text; *p; )
	{
		const char *end = strchr( p, '\n' );
		int len = end ? (int)( end - p ) : (int)strlen( p );
		if( len > 0 && p[len - 1] == '\r' )
			len--;	// retail drops the character before the line break
		if( len > NF_MSG_LINELEN - 1 )
			len = NF_MSG_LINELEN - 1;

		if( s_hintCount == NF_HINT_LINES )
		{
			memmove( s_hints[0], s_hints[1], sizeof( s_hints[0] ) * ( NF_HINT_LINES - 1 ));
			s_hintCount--;
		}
		memcpy( s_hints[s_hintCount], p, len );
		s_hints[s_hintCount][len] = '\0';
		s_hintCount++;

		if( !end )
			break;
		p = end + 1;
	}
}

static void NF_HintClear( void )
{
	s_hintCount = 0;
}

// text width inside the message box
static int NF_TimedWidth( void )
{
	if( s_hBoxBg )
		return (int)( 1024 * NF_PanelScale() * 0.8f );
	return XRES( 420 );
}

// text of a title from maps/<map>.tit (a leading '#' is skipped; unknown names stay as they are)
static const char *NF_TitleText( const char *name, bool *found )
{
	client_textmessage_t *msg = gEngfuncs.pfnTextMessageGet( name[0] == '#' ? name + 1 : name );
	if( found )
		*found = msg && msg->pMessage;
	return ( msg && msg->pMessage ) ? msg->pMessage : name;
}

static int __MsgFunc_Objective( const char *pszName, int iSize, void *pbuf )
{
	char message[64], listName[64];

	BEGIN_READ( pbuf, iSize );
	int reset = READ_BYTE();
	if( reset )
	{
		s_objCount = 0;
		NF_HintClear();
		s_timed.endTime = 0.0f;
	}

	int id = READ_BYTE();
	if( id == 255 )
		return 1;

	strncpy( message, READ_STRING(), sizeof( message ) - 1 );
	message[sizeof( message ) - 1] = '\0';
	strncpy( listName, READ_STRING(), sizeof( listName ) - 1 );
	listName[sizeof( listName ) - 1] = '\0';
	int box = READ_BYTE();
	int list = READ_BYTE();
	int duration = READ_BYTE();
	int completed = READ_BYTE();

	bool found = false;
	if( message[0] )
	{
		if( box )
		{
			strncpy( s_text, NF_TitleText( message, &found ), sizeof( s_text ) - 1 );
			s_text[sizeof( s_text ) - 1] = '\0';
			NF_WrapText( &s_timed, s_text, NF_TimedWidth() - XRES( 24 ));
			if( !s_overview )
			{
				s_timed.endTime = gHUD.m_flTime + ( duration > 0 ? duration : NF_MSG_DEFAULT_TIME );
				gEngfuncs.pfnPlaySoundByName( "common/obj_open.wav", 1.0f );
			}
		}

		int i;
		for( i = 0; i < s_objCount; i++ )
		{
			if( s_obj[i].id == id )
				break;
		}
		if( list && i == s_objCount && s_objCount < NF_OBJ_MAX )
		{
			nf_objentry_t *e = &s_obj[s_objCount++];
			e->id = id;
			strncpy( e->text, NF_TitleText( listName[0] ? listName : message, NULL ), sizeof( e->text ) - 1 );
			e->text[sizeof( e->text ) - 1] = '\0';
			e->completed = 0;
		}
	}

	for( int i = 0; i < s_objCount; i++ )
	{
		if( s_obj[i].id == id )
			s_obj[i].completed = completed;
	}

	if( NF_DEBUG( NF_DBG_TRIGGERS ))
		gEngfuncs.Con_Printf( "nf_debug: objective %d '%s' %s list '%s' box %d list %d duration %d completed %d reset %d (%d in list)\n",
			id, message, message[0] && box ? ( found ? "found" : "NOT FOUND" ) : "-", listName, box, list, duration, completed, reset, s_objCount );
	return 1;
}

void NF_ObjectivesShowOverview( bool show )
{
	if( show == s_overview )
		return;

	s_overview = show;
	if( show )
		s_timed.endTime = 0.0f;	// the box closes under the overview
	gEngfuncs.pfnPlaySoundByName( show ? "common/obj_open.wav" : "common/obj_close.wav", 1.0f );
}

static int __MsgFunc_HudMsg( const char *pszName, int iSize, void *pbuf )
{
	char name[256];

	BEGIN_READ( pbuf, iSize );
	strncpy( name, READ_STRING(), sizeof( name ) - 1 );
	name[sizeof( name ) - 1] = '\0';
	int timed = READ_BYTE();
	int hint = READ_BYTE();
	float duration = READ_BYTE() / 10.0f;

	if( !name[0] )
	{
		NF_HintClear();
		return 1;
	}

	bool found;
	const char *text = NF_TitleText( name, &found );
	strncpy( s_text, text, sizeof( s_text ) - 1 );
	s_text[sizeof( s_text ) - 1] = '\0';

	if( duration <= 0.0f )
		duration = NF_MSG_DEFAULT_TIME;

	if( hint )
	{
		NF_HintAdd( s_text );
		gEngfuncs.pfnPlaySoundByName( "common/hint_beep.wav", 1.0f );
		NF_HudFlash();	// retail 0x41040740: the health iris flashes
	}

	if( timed )
	{
		NF_WrapText( &s_timed, s_text, NF_TimedWidth() - XRES( 24 ));
		s_timed.endTime = gHUD.m_flTime + duration;
	}

	if( NF_DEBUG( NF_DBG_TRIGGERS ))
		gEngfuncs.Con_Printf( "nf_debug: hudmsg '%s' %s timed %d hint %d duration %.1f (%d hint lines)\n",
			name, found ? "found" : "NOT FOUND", timed, hint, duration, s_hintCount );
	return 1;
}

static void NF_DrawBox( const nf_msgbox_t *box, int x, int y, int width, int lineHeight, int padX, int padY )
{
	int height = box->numLines * lineHeight + padY * 2;

	gEngfuncs.pfnFillRGBABlend( x, y, width, height, 8, 12, 15, 210 );
	FillRGBA( x, y, width, YRES( 2 ), 58, 180, 166, 255 );

	DrawSetTextColor( 1.0f, 1.0f, 1.0f );
	for( int i = 0; i < box->numLines; i++ )
		DrawConsoleString( x + padX, y + padY + i * lineHeight, box->lines[i] );
}

// objective overview with the retail image (lower left, see the header)
static void NF_DrawOverviewImage( int lineHeight )
{
	static nf_msgbox_t entry[NF_OBJ_MAX];
	float s = NF_PanelScale();
	int size = (int)( 512 * s );
	int x = ScreenHeight / 30;
	int bottom = ScreenHeight - 80;
	if( bottom > NF_HealthPanelTop() - YRES( 4 ))
		bottom = NF_HealthPanelTop() - YRES( 4 );
	int y = bottom - size;

	NF_DrawImage( s_hOverviewBg, x, y, size, size, 1.0f );

	// objectives: below the "OBJECTIVES:" label, above the hint section (image rows 44-355)
	int icon = (int)( 24 * s );
	int textX = x + (int)( 52 * s );
	int textW = size - (int)( 70 * s );
	int ty = y + (int)( 44 * s );
	int listEnd = y + (int)( 355 * s );

	for( int i = 0; i < s_objCount && ty + lineHeight <= listEnd; i++ )
	{
		NF_WrapText( &entry[i], s_obj[i].text, textW );
		HSPRITE mark = s_obj[i].completed ? s_hCheck : s_hCircle;
		if( mark )
			NF_DrawImage( mark, x + (int)( 18 * s ), ty + ( lineHeight - icon ) / 2, icon, icon, 1.0f );

		if( s_obj[i].completed )
			DrawSetTextColor( 0.7f, 0.7f, 0.7f );
		else
			DrawSetTextColor( 1.0f, 1.0f, 1.0f );
		for( int l = 0; l < entry[i].numLines && ty + lineHeight <= listEnd; l++, ty += lineHeight )
			DrawConsoleString( textX, ty, entry[i].lines[l] );
		ty += lineHeight / 3;
	}

	// hints: below the "HINTS:" label (image rows 395-500)
	ty = y + (int)( 395 * s );
	int hintEnd = y + (int)( 500 * s );
	DrawSetTextColor( 1.0f, 1.0f, 1.0f );
	for( int i = 0; i < s_hintCount; i++ )
	{
		nf_msgbox_t hint;
		NF_WrapText( &hint, s_hints[i], size - (int)( 44 * s ));
		for( int l = 0; l < hint.numLines && ty + lineHeight <= hintEnd; l++, ty += lineHeight )
			DrawConsoleString( x + (int)( 22 * s ), ty, hint.lines[l] );
	}
}

// objective overview: centred plain panel with the objective list and the hint section
static void NF_DrawOverview( int lineHeight )
{
	static nf_msgbox_t entry[NF_OBJ_MAX];
	static nf_msgbox_t hint;
	if( s_hOverviewBg )
	{
		NF_DrawOverviewImage( lineHeight );
		return;
	}

	int width = XRES( 460 );
	int padX = XRES( 14 ), padY = YRES( 10 ), mark = XRES( 18 );
	int lines = 1;	// title

	for( int i = 0; i < s_objCount; i++ )
	{
		NF_WrapText( &entry[i], s_obj[i].text, width - padX * 2 - mark );
		lines += entry[i].numLines > 0 ? entry[i].numLines : 1;
	}
	if( s_objCount == 0 )
		lines++;

	hint.numLines = 0;
	for( int i = 0; i < s_hintCount; i++ )
	{
		nf_msgbox_t part;
		NF_WrapText( &part, s_hints[i], width - padX * 2 - mark );
		for( int l = 0; l < part.numLines && hint.numLines < NF_MSG_LINES; l++ )
			strcpy( hint.lines[hint.numLines++], part.lines[l] );
	}
	if( hint.numLines > 0 )
		lines += 1 + hint.numLines;

	int height = lines * lineHeight + padY * 2 + ( hint.numLines > 0 ? lineHeight / 2 : 0 );
	int x = ( ScreenWidth - width ) / 2;
	int y = ( ScreenHeight - height ) / 3;

	gEngfuncs.pfnFillRGBABlend( x, y, width, height, 8, 12, 15, 225 );
	FillRGBA( x, y, width, YRES( 2 ), 58, 180, 166, 255 );

	int ty = y + padY;
	DrawSetTextColor( 0.83f, 0.69f, 0.22f );
	DrawConsoleString( x + padX, ty, "OBJECTIVES" );
	ty += lineHeight;

	if( s_objCount == 0 )
	{
		DrawSetTextColor( 0.6f, 0.6f, 0.6f );
		DrawConsoleString( x + padX + mark, ty, "(none)" );
		ty += lineHeight;
	}

	for( int i = 0; i < s_objCount; i++ )
	{
		// marker: filled = completed (retail check.png), outline = open (circle.png)
		int box = lineHeight / 2;
		int bx = x + padX, by = ty + ( lineHeight - box ) / 2 - YRES( 1 );
		if( s_obj[i].completed )
			FillRGBA( bx, by, box, box, 58, 180, 166, 255 );
		else
		{
			FillRGBA( bx, by, box, 1, 200, 200, 200, 255 );
			FillRGBA( bx, by + box - 1, box, 1, 200, 200, 200, 255 );
			FillRGBA( bx, by, 1, box, 200, 200, 200, 255 );
			FillRGBA( bx + box - 1, by, 1, box, 200, 200, 200, 255 );
		}

		if( s_obj[i].completed )
			DrawSetTextColor( 0.6f, 0.6f, 0.6f );
		else
			DrawSetTextColor( 1.0f, 1.0f, 1.0f );
		int n = entry[i].numLines > 0 ? entry[i].numLines : 1;
		for( int l = 0; l < entry[i].numLines; l++ )
			DrawConsoleString( x + padX + mark, ty + l * lineHeight, entry[i].lines[l] );
		ty += n * lineHeight;
	}

	if( hint.numLines > 0 )
	{
		ty += lineHeight / 2;
		DrawSetTextColor( 0.83f, 0.69f, 0.22f );
		DrawConsoleString( x + padX, ty, "HINT" );
		ty += lineHeight;
		DrawSetTextColor( 1.0f, 1.0f, 1.0f );
		for( int l = 0; l < hint.numLines; l++ )
			DrawConsoleString( x + padX + mark, ty + l * lineHeight, hint.lines[l] );
	}
}

void NF_HudMsgInit( void )
{
	HOOK_MESSAGE( HudMsg );
	HOOK_MESSAGE( Objective );
	NF_HudMsgReset();
}

void NF_HudMsgReset( void )
{
	// the objective list and the hints stay (retail: the panel keeps them):
	// they last across level changes and the server resets / resends them
	// after a new game or a load
	memset( &s_timed, 0, sizeof( s_timed ));
	s_overview = false;

	// sprites are freed on a map change: load the panel images again
	s_hBoxBg = SPR_Load( "gui/hud/hud_objective_bg.png" );
	s_hOverviewBg = SPR_Load( "gui/hud/hud_objectives_hint_bg.png" );
	s_hCheck = SPR_Load( "gui/hud/check.png" );
	s_hCircle = SPR_Load( "gui/hud/circle.png" );
}

void NF_HudMsgDraw( float flTime )
{
	if( !gHUD.m_pCvarDraw || !gHUD.m_pCvarDraw->value || gEngfuncs.IsSpectateOnly() )
		return;

	int w, lineHeight;
	GetConsoleStringSize( "Ay", &w, &lineHeight );
	lineHeight += YRES( 2 );

	if( s_overview )
	{
		NF_DrawOverview( lineHeight );
		return;
	}

	if( s_timed.numLines > 0 && flTime < s_timed.endTime )
	{
		if( s_hBoxBg )
		{
			// retail message box, lower left; the text centred in the image height
			float s = NF_PanelScale();
			int w = (int)( 1024 * s ), h = (int)( 128 * s );
			int bottom = ScreenHeight - 72;
			if( bottom > NF_HealthPanelTop() - YRES( 4 ))
				bottom = NF_HealthPanelTop() - YRES( 4 );
			int x = ScreenHeight / 30, y = bottom - h;
			NF_DrawImage( s_hBoxBg, x, y, w, h, 1.0f );

			int lines = s_timed.numLines;
			int ty = y + ( h - lines * lineHeight ) / 2;
			if( ty < y )
				ty = y;
			DrawSetTextColor( 1.0f, 1.0f, 1.0f );
			for( int i = 0; i < lines; i++ )
				DrawConsoleString( x + (int)( 40 * s ), ty + i * lineHeight, s_timed.lines[i] );
		}
		else
		{
			int width = NF_TimedWidth();
			NF_DrawBox( &s_timed, ( ScreenWidth - width ) / 2, ScreenHeight * 3 / 10, width, lineHeight, XRES( 12 ), YRES( 10 ));
		}
	}
}
