/*
nf_materials.cpp - James Bond 007: Nightfire (PC) surface materials

sound/debris.txt (assets.007) tags about 2500 texture paths with a material
letter (E stone/default, G breakable glass, U unbreakable glass, W wood,
S snow, R grass, P plaster, M metal, C carpet, D sand, I dirt, N no effects).
The retail client (client.dll 0x41052f50) reads it, upper-cases the letter
and, for each line, tags every loaded texture of that name (the last line
wins); textures it does not list get the generic decal. The impact decal is
"{d_<material>_0" plus a random 1-4 (0x41052560); N makes no decal and no
debris.

Built into the server and the client.
*/

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nf_materials.h"

#define NF_MAX_MATERIALS	4096
#define NF_MATERIAL_NAME	64	// the retail compare length is 63

typedef struct
{
	char name[NF_MATERIAL_NAME];	// lower case
	char material;
	int line;
} nf_material_t;

static nf_material_t s_materials[NF_MAX_MATERIALS];
static int s_count;
static int s_loaded;

static int NF_CompareMaterial( const void *a, const void *b )
{
	const nf_material_t *ma = (const nf_material_t *)a;
	const nf_material_t *mb = (const nf_material_t *)b;
	int c = strcmp( ma->name, mb->name );

	if( c )
		return c;
	return ma->line - mb->line;
}

static void NF_LowerCopy( char *dst, const char *src, size_t size )
{
	size_t i;

	for( i = 0; i + 1 < size && src[i]; i++ )
		dst[i] = (char)tolower( (unsigned char)src[i] );
	dst[i] = '\0';
}

void NF_LoadMaterials( const char *buffer )
{
	const char *p = buffer;
	int line = 0;

	s_count = 0;
	s_loaded = 1;

	while( p && *p && s_count < NF_MAX_MATERIALS )
	{
		const char *eol = p;
		char letter, name[NF_MATERIAL_NAME];
		size_t n = 0;

		while( *eol && *eol != '\n' )
			eol++;

		// "<letter> <texture path>", "//" comments
		while( p < eol && isspace( (unsigned char)*p ))
			p++;
		if( p < eol && !( p[0] == '/' && p + 1 < eol && p[1] == '/' ) && isalpha( (unsigned char)*p ))
		{
			letter = (char)toupper( (unsigned char)*p++ );
			if( p < eol && isspace( (unsigned char)*p ))
			{
				while( p < eol && isspace( (unsigned char)*p ))
					p++;
				while( p < eol && !isspace( (unsigned char)*p ) && n + 1 < sizeof( name ))
					name[n++] = *p++;
				name[n] = '\0';

				if( n )
				{
					nf_material_t *m = &s_materials[s_count++];
					NF_LowerCopy( m->name, name, sizeof( m->name ));
					m->material = letter;
					m->line = line;
				}
			}
		}

		line++;
		p = *eol ? eol + 1 : eol;
	}

	qsort( s_materials, s_count, sizeof( s_materials[0] ), NF_CompareMaterial );
}

int NF_MaterialsLoaded( void )
{
	return s_loaded;
}

int NF_MaterialCount( void )
{
	return s_count;
}

char NF_TextureMaterial( const char *texture )
{
	char name[NF_MATERIAL_NAME];
	int lo = 0, hi = s_count - 1, found = -1;

	if( !texture || !texture[0] || !s_count )
		return 0;

	// masked BSP30 names carry a '{' prefix the paths do not have
	if( texture[0] == '{' )
		texture++;
	NF_LowerCopy( name, texture, sizeof( name ));

	// the last line for a name wins (entries are sorted by name, then line)
	while( lo <= hi )
	{
		int mid = ( lo + hi ) / 2;
		int c = strcmp( s_materials[mid].name, name );

		if( c <= 0 )
		{
			if( !c )
				found = mid;
			lo = mid + 1;
		}
		else
			hi = mid - 1;
	}

	return found >= 0 ? s_materials[found].material : 0;
}

const char *NF_ImpactDecal( char material, int breakable, int rnd )
{
	static char decal[32];
	const char *base;

	switch( material )
	{
	case 'N': return NULL;	// no effects
	case 'E': base = "stone"; break;
	case 'G':
	case 'U': base = breakable ? "glass_break" : "glass_unbreak"; break;
	case 'W': base = "wood"; break;
	case 'S': base = "snow"; break;
	case 'R': base = "grass"; break;
	case 'M': base = "metal"; break;
	case 'C': base = "carpet"; break;
	case 'D': base = "sand"; break;
	case 'P': base = "plaster"; break;
	case 'I': base = "dirt"; break;
	default: base = "generic"; break;	// not listed, unknown letter
	}

	if( rnd < 1 ) rnd = 1;
	if( rnd > 4 ) rnd = 4;
	snprintf( decal, sizeof( decal ), "{d_%s_0%d", base, rnd );
	return decal;
}
