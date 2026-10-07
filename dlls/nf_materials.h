/*
nf_materials.h - James Bond 007: Nightfire (PC) surface materials

The retail client reads sound/debris.txt ("<letter> <texture path>") and
picks impact decals and debris by the material of the hit texture (retail
client.dll 0x41052f50 loader, 0x41052560 decal names). Shared by the server
(dlls/weapons.cpp DecalGunshot) and the client (cl_dll/ev_hldm.cpp).
*/
#pragma once
#ifndef NF_MATERIALS_H
#define NF_MATERIALS_H

// parse sound/debris.txt; buffer may be NULL (file missing)
void NF_LoadMaterials( const char *buffer );
int NF_MaterialsLoaded( void );
int NF_MaterialCount( void );	// 0: no debris.txt (not Nightfire data)

// material letter of a texture path (upper case), 0 = not listed
char NF_TextureMaterial( const char *texture );

// impact decal for a material letter: "{d_metal_0<1-4>", ...; NULL = none
// (N = no effects); breakable picks the breakable-glass decal for glass
const char *NF_ImpactDecal( char material, int breakable, int rnd );

#endif // NF_MATERIALS_H
