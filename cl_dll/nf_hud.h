/*
nf_hud.h - James Bond 007: Nightfire (PC) in-game HUD (health iris, ammo, crosshair)
*/
#pragma once
#ifndef NF_HUD_H
#define NF_HUD_H

void NF_HudVidInit( void );		// CHud::VidInit (sprites are freed on a map change)
void NF_HudDraw( float flTime );	// CHud::Redraw
bool NF_HudActive( void );		// the retail images were found
void NF_HudFlash( void );		// hint: flash the health iris with beeps

// draw an image sprite (engine fork: image files as one-frame sprites) as a TriAPI quad
void NF_DrawImage( HSPRITE hspr, int x, int y, int w, int h, float alpha );
void NF_DrawImagePart( HSPRITE hspr, int x, int y, int w, int h, float s0, float t0, float s1, float t1, float alpha );

#endif // NF_HUD_H
