/*
nf_hudmsg.cpp - James Bond 007: Nightfire (PC) level messages ("HudMsg")

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

Not the retail look yet: plain boxes in the style of the text HUD fallback
(hud_redraw.cpp); the hint stays on screen for the duration (retail: kept
in the panel) [assumed].
*/

#include <string.h>
#include <stdio.h>

#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"
#include "nf_debug.h"
#include "nf_hudmsg.h"

#define NF_MSG_LINES		12
#define NF_MSG_LINELEN		256
#define NF_MSG_DEFAULT_TIME	5.0f

typedef struct
{
	char lines[NF_MSG_LINES][NF_MSG_LINELEN];
	int numLines;
	float endTime;
} nf_msgbox_t;

static nf_msgbox_t s_hint;	// hint section (lower left)
static nf_msgbox_t s_timed;	// timed message box (centre)
static char s_text[2048];	// last received text

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

static int NF_HintWidth( void )
{
	return XRES( 300 );
}

static int NF_TimedWidth( void )
{
	return XRES( 420 );
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
		s_hint.numLines = 0;
		s_hint.endTime = 0.0f;
		return 1;
	}

	client_textmessage_t *msg = gEngfuncs.pfnTextMessageGet( name[0] == '#' ? name + 1 : name );
	const char *text = ( msg && msg->pMessage ) ? msg->pMessage : name;
	strncpy( s_text, text, sizeof( s_text ) - 1 );
	s_text[sizeof( s_text ) - 1] = '\0';

	if( duration <= 0.0f )
		duration = NF_MSG_DEFAULT_TIME;

	if( hint )
	{
		NF_WrapText( &s_hint, s_text, NF_HintWidth() - XRES( 18 ));
		s_hint.endTime = gHUD.m_flTime + duration;
		gEngfuncs.pfnPlaySoundByName( "common/hint_beep.wav", 1.0f );
	}

	if( timed )
	{
		NF_WrapText( &s_timed, s_text, NF_TimedWidth() - XRES( 24 ));
		s_timed.endTime = gHUD.m_flTime + duration;
	}

	if( NF_DEBUG( NF_DBG_TRIGGERS ))
		gEngfuncs.Con_Printf( "nf_debug: hudmsg '%s' %s timed %d hint %d duration %.1f\n",
			name, msg ? "found" : "NOT FOUND", timed, hint, duration );
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

void NF_HudMsgInit( void )
{
	HOOK_MESSAGE( HudMsg );
	NF_HudMsgReset();
}

void NF_HudMsgReset( void )
{
	memset( &s_hint, 0, sizeof( s_hint ));
	memset( &s_timed, 0, sizeof( s_timed ));
}

void NF_HudMsgDraw( float flTime )
{
	if( !gHUD.m_pCvarDraw || !gHUD.m_pCvarDraw->value || gEngfuncs.IsSpectateOnly() )
		return;

	int w, lineHeight;
	GetConsoleStringSize( "Ay", &w, &lineHeight );
	lineHeight += YRES( 2 );

	if( s_hint.numLines > 0 && flTime < s_hint.endTime )
	{
		int height = s_hint.numLines * lineHeight + YRES( 7 ) * 2;
		int y = ScreenHeight - YRES( 58 ) - YRES( 8 ) - height;	// above the health panel
		NF_DrawBox( &s_hint, XRES( 16 ), y, NF_HintWidth(), lineHeight, XRES( 9 ), YRES( 7 ));
	}

	if( s_timed.numLines > 0 && flTime < s_timed.endTime )
	{
		int width = NF_TimedWidth();
		NF_DrawBox( &s_timed, ( ScreenWidth - width ) / 2, ScreenHeight * 3 / 10, width, lineHeight, XRES( 12 ), YRES( 10 ));
	}
}
