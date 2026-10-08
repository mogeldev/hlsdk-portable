/*
nf_particles.h - James Bond 007: Nightfire (PC) emitter particles (particle_emitter, cl_dll/nf_particles.cpp)
*/
#pragma once
#ifndef NF_PARTICLES_H
#define NF_PARTICLES_H

void NF_ParticlesInit( void );		// CHud::Init (message hook: Particles)
void NF_ParticlesVidInit( void );	// CHud::VidInit (a new map: no particles)
void NF_ParticlesRender( void );	// HUD_DrawTransparentTriangles

#endif // NF_PARTICLES_H
