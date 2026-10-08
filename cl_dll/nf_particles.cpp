/*
nf_particles.cpp - James Bond 007: Nightfire (PC) emitter particles ("Particles")

Sent by particle_emitter (dlls/nf_particles.cpp). Port of the retail client
particle code (client.dll; docs/retail/particles.md):
	0x4104A250	MsgFunc_Particles: reads the message, preciseFade
			overrides the fade speed with renderamt / (preciseFade * 30)
	0x41058160	spawns count particles: direction + noise (x, y
			+-noise / 10, z 0..min(noise * 0.15, 1)), speed
			2 * speed * 0.8..1.2; particle_type 0 only
	0x41054B20	per-frame update: sprite frame (15 fps loop, or a
			random fixed frame with spawnflag 8), scale grows by
			scale_speed / 10 * 30 per second from scale / 10 (without
			scale_speed the size is particle_scale), alpha falls by
			fade_speed * 30 per second, roll from the avelocity, gravity
			sv_gravity * particle_gravity / 10, spawnflag 64 wobbles x / y
	0x41055AE0	draw: a quad of side "scale" world units facing the view
			(view pitch / yaw, the particle's roll), sprite rendermode,
			rendercolor and alpha
The retail pool (0x41051000) holds 0xFFF70 / 0xB8 = 5698 particles; when it
is full new particles are dropped. Spawnflags 2 / 4 ask for world collision,
but the retail collision code (0x410569E0) only acts for flags the emitter
never sets, so particles fly through walls there too.
*/

#include <math.h>
#include <string.h>

#include "hud.h"
#include "cl_util.h"
#include "const.h"
#include "com_model.h"
#include "triangleapi.h"
#include "parsemsg.h"
#include "nf_particles.h"
#include "nf_debug.h"

extern vec3_t v_angles;

#define NF_MAX_PARTICLES	5698
#define NF_PARTICLE_FPS		15

// emitter spawnflags the client reads
#define NF_PF_RANDOM_FRAME	8	// a random fixed sprite frame
#define NF_PF_WOBBLE		64	// x / y wobble

struct nf_particle_t
{
	bool used;
	struct model_s *model;
	int frame;
	int numframes;		// animated when > 1 and fps set
	float fps;
	int rendermode;
	int flags;		// emitter spawnflags
	float birth;
	float die;
	vec3_t org;
	vec3_t vel;
	float gravity;		// particle_gravity / 10
	float scale;		// quad side in world units
	float scale0;
	float scaleSpeed;
	float alpha;
	float alpha0;
	float fadeSpeed;
	vec3_t avel;
	float roll;
	byte color[3];
	float seed;		// wobble phase (retail: the particle's address)
};

static nf_particle_t s_particles[NF_MAX_PARTICLES];
static int s_iUsed;		// live particles
static float s_flLastUpdate;
static bool s_bFullReported;	// nf_debug: pool full printed once per map

static nf_particle_t *NF_AllocParticle( void )
{
	for( int i = 0; i < NF_MAX_PARTICLES; i++ )
	{
		if( !s_particles[i].used )
		{
			memset( &s_particles[i], 0, sizeof( s_particles[i] ));
			s_particles[i].used = true;
			s_particles[i].seed = (float)i * 184.0f;
			s_iUsed++;
			return &s_particles[i];
		}
	}

	if( NF_DEBUG( NF_DBG_EFFECTS ) && !s_bFullReported )
		gEngfuncs.Con_Printf( "nf_debug: particles: pool full (%d)\n", NF_MAX_PARTICLES );
	s_bFullReported = true;
	return NULL;
}

static void NF_SpawnParticles( const vec3_t origin, const vec3_t dir, int count, float speed, int noise,
	int rendermode, float gravity, float scale, float scaleSpeed, float alpha, float fadeSpeed,
	float life, int type, int sprite, int flags, const vec3_t angles, const vec3_t avel, const byte *color )
{
	if( type != 0 )
	{
		gEngfuncs.Con_Printf( "Unsupported Particle Type '%d'\n", type );
		return;
	}

	struct model_s *model = gEngfuncs.pfnGetModelByIndex( sprite );
	if( !model )
		return;

	float n = (float)noise / 10.0f;
	float nz = n * 1.5f;
	if( nz > 1.0f )
		nz = 1.0f;
	float speed2 = speed * 2.0f;
	float now = gEngfuncs.GetClientTime();

	for( int i = 0; i < count; i++ )
	{
		vec3_t d;
		d[0] = dir[0] + gEngfuncs.pfnRandomFloat( -n, n );
		d[1] = dir[1] + gEngfuncs.pfnRandomFloat( -n, n );
		d[2] = dir[2] + gEngfuncs.pfnRandomFloat( 0.0f, nz );
		float s = gEngfuncs.pfnRandomFloat( speed2 * 0.8f, speed2 * 1.2f );

		nf_particle_t *p = NF_AllocParticle();
		if( !p )
			return;

		VectorCopy( origin, p->org );
		VectorScale( d, s, p->vel );
		p->model = model;
		p->rendermode = rendermode;
		p->flags = flags;
		p->birth = now;
		p->die = now + life;
		p->gravity = gravity;
		p->scale = scale;
		p->scale0 = scale / 10.0f;
		p->scaleSpeed = scaleSpeed / 10.0f;
		p->alpha = p->alpha0 = alpha;
		p->fadeSpeed = fadeSpeed;
		VectorCopy( avel, p->avel );
		p->roll = angles[2];
		p->color[0] = color[0];
		p->color[1] = color[1];
		p->color[2] = color[2];

		if( flags & NF_PF_RANDOM_FRAME )
			p->frame = gEngfuncs.pfnRandomLong( 0, model->numframes - 1 );
		else if( model->numframes > 1 )
		{
			p->fps = NF_PARTICLE_FPS;
			p->numframes = model->numframes;
		}
	}
}

static int __MsgFunc_Particles( const char *pszName, int iSize, void *pbuf )
{
	vec3_t origin, dir, angles, avel;
	byte color[3];

	BEGIN_READ( pbuf, iSize );
	origin[0] = READ_COORD();
	origin[1] = READ_COORD();
	origin[2] = READ_COORD();
	dir[0] = READ_COORD();
	dir[1] = READ_COORD();
	dir[2] = READ_COORD();
	int sprite = READ_SHORT();
	int count = READ_BYTE();
	float speed = (float)READ_BYTE();
	int noise = READ_BYTE();
	int rendermode = READ_BYTE();
	float gravity = (float)READ_SHORT();
	float scale = (float)READ_BYTE();
	float scaleSpeed = (float)READ_BYTE();
	float alpha = (float)READ_BYTE();
	float fadeSpeed = (float)READ_BYTE();
	float life = (float)READ_BYTE();
	int type = READ_BYTE();
	int flags = READ_BYTE();
	angles[0] = READ_COORD();
	angles[1] = READ_COORD();
	angles[2] = READ_COORD();
	avel[0] = READ_COORD();
	avel[1] = READ_COORD();
	avel[2] = READ_COORD();
	color[0] = READ_BYTE();
	color[1] = READ_BYTE();
	color[2] = READ_BYTE();
	int preciseFade = READ_BYTE();

	if( preciseFade )
		fadeSpeed = alpha / (float)( preciseFade * 30 );

	if( NF_DEBUG( NF_DBG_EFFECTS ))
	{
		struct model_s *model = gEngfuncs.pfnGetModelByIndex( sprite );
		gEngfuncs.Con_Printf( "nf_debug: cl: particles %d %s at %.0f %.0f %.0f dir %.2f %.2f %.2f (live %d)\n",
			count, model ? model->name : "?", origin[0], origin[1], origin[2], dir[0], dir[1], dir[2], s_iUsed );
	}

	NF_SpawnParticles( origin, dir, count, speed, noise, rendermode, gravity / 10.0f, scale, scaleSpeed,
		alpha, fadeSpeed, life / 10.0f, type, sprite, flags, angles, avel, color );
	return 1;
}

void NF_ParticlesInit( void )
{
	HOOK_MESSAGE( Particles );
}

void NF_ParticlesVidInit( void )
{
	memset( s_particles, 0, sizeof( s_particles ));
	s_iUsed = 0;
	s_flLastUpdate = 0.0f;
	s_bFullReported = false;
}

static void NF_UpdateParticle( nf_particle_t *p, float time, float dt, float svGravity )
{
	float age = time - p->birth;

	// sprite frame: looped at 15 fps
	if( p->fps != 0.0f && p->numframes )
	{
		float f = age * p->fps;
		while( f >= (float)p->numframes )
			f -= (float)p->numframes;
		p->frame = (int)f;
		if( p->frame < 0 )
			p->frame = 0;
	}

	if( p->scaleSpeed != 0.0f )
	{
		p->scale = p->scale0 + age * p->scaleSpeed * 30.0f;
		if( p->scale <= 0.0001f )
			p->die = time;
	}

	if( p->fadeSpeed != 0.0f )
	{
		p->alpha = p->alpha0 - age * p->fadeSpeed * 30.0f;
		if( p->alpha <= 1.0f )
			p->die = time;
	}

	if( p->avel[0] != 0.0f || p->avel[1] != 0.0f || p->avel[2] != 0.0f )
		p->roll = age * Length( p->avel ) * 30.0f * p->avel[2];

	float g = -( svGravity * dt ) * p->gravity;
	if( p->vel[0] == 0.0f && p->vel[1] == 0.0f && p->vel[2] == 0.0f && g == 0.0f )
		return;

	if( p->flags & NF_PF_WOBBLE )
	{
		// retail: per frame, not scaled by the frame time, and the
		// velocity step below is applied a second time
		p->org[0] += dt * p->vel[0] + 2.0f * sin( time * 2.5f + p->seed );
		p->org[1] += dt * p->vel[1] + sin( time * 3.75f + p->seed );
		p->org[2] += dt * p->vel[2];
	}
	VectorMA( p->org, dt, p->vel, p->org );
	p->vel[2] += g;
}

static void NF_DrawParticle( const nf_particle_t *p )
{
	if( !gEngfuncs.pTriAPI->SpriteTexture( p->model, p->frame ))
		return;

	vec3_t angles, forward, right, up;
	angles[0] = v_angles[0];
	angles[1] = v_angles[1];
	angles[2] = p->roll;
	AngleVectors( angles, forward, right, up );

	float h = p->scale * 0.5f;
	float alpha = p->alpha > 255.0f ? 255.0f : ( p->alpha < 0.0f ? 0.0f : p->alpha );

	gEngfuncs.pTriAPI->RenderMode( p->rendermode );
	gEngfuncs.pTriAPI->Color4ub( p->color[0], p->color[1], p->color[2], (unsigned char)alpha );
	gEngfuncs.pTriAPI->Begin( TRI_QUADS );
	gEngfuncs.pTriAPI->TexCoord2f( 0.0f, 1.0f );
	gEngfuncs.pTriAPI->Vertex3f( p->org[0] - right[0] * h - up[0] * h, p->org[1] - right[1] * h - up[1] * h, p->org[2] - right[2] * h - up[2] * h );
	gEngfuncs.pTriAPI->TexCoord2f( 0.0f, 0.0f );
	gEngfuncs.pTriAPI->Vertex3f( p->org[0] - right[0] * h + up[0] * h, p->org[1] - right[1] * h + up[1] * h, p->org[2] - right[2] * h + up[2] * h );
	gEngfuncs.pTriAPI->TexCoord2f( 1.0f, 0.0f );
	gEngfuncs.pTriAPI->Vertex3f( p->org[0] + right[0] * h + up[0] * h, p->org[1] + right[1] * h + up[1] * h, p->org[2] + right[2] * h + up[2] * h );
	gEngfuncs.pTriAPI->TexCoord2f( 1.0f, 1.0f );
	gEngfuncs.pTriAPI->Vertex3f( p->org[0] + right[0] * h - up[0] * h, p->org[1] + right[1] * h - up[1] * h, p->org[2] + right[2] * h - up[2] * h );
	gEngfuncs.pTriAPI->End();
}

void NF_ParticlesRender( void )
{
	if( !s_iUsed )
		return;

	// update once per client frame (the transparent pass can run more than once)
	float time = gEngfuncs.GetClientTime();
	if( time != s_flLastUpdate )
	{
		float dt = s_flLastUpdate > 0.0f ? time - s_flLastUpdate : 0.0f;
		if( dt < 0.0f || dt > 0.25f )
			dt = 0.0f;	// level change or a long stall
		float svGravity = gEngfuncs.pfnGetGravity();
		s_flLastUpdate = time;

		for( int i = 0; i < NF_MAX_PARTICLES; i++ )
		{
			nf_particle_t *p = &s_particles[i];
			if( !p->used )
				continue;
			if( time >= p->die )
			{
				p->used = false;
				s_iUsed--;
				continue;
			}
			NF_UpdateParticle( p, time, dt, svGravity );
		}
	}

	gEngfuncs.pTriAPI->CullFace( TRI_NONE );
	for( int i = 0; i < NF_MAX_PARTICLES; i++ )
	{
		if( s_particles[i].used && time < s_particles[i].die )
			NF_DrawParticle( &s_particles[i] );
	}
	gEngfuncs.pTriAPI->RenderMode( kRenderNormal );
	gEngfuncs.pTriAPI->CullFace( TRI_FRONT );
}
