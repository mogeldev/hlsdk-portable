/*
nf_debug.h - James Bond 007: Nightfire port diagnostics

The cvar nf_debug (registered by the engine fork, by the server DLL if the
engine lacks it) is a bit mask of diagnostic areas. The diagnostics only
print; they never change behaviour. Server prints go to the console, client
prints carry the "cl:" prefix (AlertMessage). Documented in AGENTS.md of the
project repo.
*/
#pragma once
#ifndef NF_DEBUG_H
#define NF_DEBUG_H

#define NF_DBG_WEAPONS	1	// fire rounds, client prediction state, fire events
#define NF_DBG_DECALS	2	// impact texture / material / decal
#define NF_DBG_MONSTERS	4	// enemy spawn, "stuck in wall" details
#define NF_DBG_TRIGGERS	8	// HUD / mission triggers: fired, skipped, message shown
#define NF_DBG_ITEMS	16	// pickups: health before / after

// current nf_debug value (server: dlls/nf_debug.cpp, client: cl_dll/hl/hl_weapons.cpp)
int NF_DebugBits( void );

#define NF_DEBUG( bits ) ( NF_DebugBits() & ( bits ))

#endif // NF_DEBUG_H
