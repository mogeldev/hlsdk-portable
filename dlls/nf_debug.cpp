/*
nf_debug.cpp - James Bond 007: Nightfire port diagnostics (server side)

See nf_debug.h. The engine fork registers nf_debug; with another engine the
server DLL registers it itself.
*/

#include "extdll.h"
#include "util.h"
#include "nf_debug.h"

static cvar_t nf_debug_local = { "nf_debug", "0", 0, 0, NULL };
static cvar_t *s_nf_debug;

int NF_DebugBits( void )
{
	if( !s_nf_debug )
	{
		s_nf_debug = CVAR_GET_POINTER( "nf_debug" );
		if( !s_nf_debug )
		{
			CVAR_REGISTER( &nf_debug_local );
			s_nf_debug = CVAR_GET_POINTER( "nf_debug" );
		}
	}
	return s_nf_debug ? (int)s_nf_debug->value : 0;
}
