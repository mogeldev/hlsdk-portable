/*
nf_fog.h - James Bond 007: Nightfire (PC) map fog (env_fog, cl_dll/nf_fog.cpp)
*/
#pragma once
#ifndef NF_FOG_H
#define NF_FOG_H

void NF_FogInit( void );	// CHud::Init (message hook: Fog)
void NF_FogVidInit( void );	// CHud::VidInit (no fog until the new map's env_fog)
void NF_FogRender( void );	// HUD_DrawNormalTriangles

#endif // NF_FOG_H
