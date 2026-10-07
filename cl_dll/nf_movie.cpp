/*
nf_movie.cpp - James Bond 007: Nightfire (PC) cutscenes ("PlayMovie")

Sent by trigger_playmovie (dlls/nf_triggers.cpp) with the movie name. The
retail client (PlayMovie_class, handler 0x410466e0) passes the name to an
engine function that plays movies/<name>[.avi] at once; here that is the
engine command nf_playmovie (engine fork, cl_video.c).
*/

#include <stdio.h>
#include <string.h>
#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"
#include "nf_movie.h"

static int __MsgFunc_PlayMovie( const char *pszName, int iSize, void *pbuf )
{
	char name[64];
	char cmd[96];

	BEGIN_READ( pbuf, iSize );
	strncpy( name, READ_STRING(), sizeof( name ) - 1 );
	name[sizeof( name ) - 1] = '\0';

	// the name ends up in a command line: no quotes or separators
	if( !name[0] || strpbrk( name, "\";\n\r" ))
		return 1;

	snprintf( cmd, sizeof( cmd ), "nf_playmovie \"%s\"\n", name );
	gEngfuncs.pfnClientCmd( cmd );
	return 1;
}

void NF_MovieInit( void )
{
	HOOK_MESSAGE( PlayMovie );
}
