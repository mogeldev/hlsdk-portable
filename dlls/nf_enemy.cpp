/*
nf_enemy.cpp - James Bond 007: Nightfire (PC) enemy_generic for the Xash3D port

Stage 1: the enemy appears with its model, idles, sees and hears, chases,
flinches and dies, driven by the stock HLSDK monster and squad AI. It has no
weapon yet (primary_weapon / gun_index / num_grenades are ignored).

The Nightfire character models label their sequences with Half-Life activity
numbers (1 idle, 3 walk, 4 run, 15/16 turn, 27 big flinch, 28 range attack,
36-39 death, ...), so the AI picks sequences by activity unchanged.

Keys (no Nightfire FGD; inferred from the retail maps, assumptions):
  model            character model (commando, hazmat_light, black_ops, ...)
  netname          squad name (HLSDK squads use netname as well)
  sight_dist       how far the enemy sees (512..4096 on m5/m7)
  Spawnflags       capitalised; the low bits match the HLSDK monster flags
                   (4 hit monsterclip, 32 squad leader, 128 wait for script),
                   the high bits (0x4000, 0x400000, 0x2000000) are unknown
                   and dropped
  deathtarget      fired when the enemy dies
  TriggerTarget / TriggerCondition: parsed by CBaseMonster; the Nightfire
                   condition values (e.g. 4362) are not HLSDK conditions
*/

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "schedule.h"
#include "squadmonster.h"
#include "soundent.h"

// HLSDK monster spawnflags that Nightfire maps use with the same meaning
#define NF_ENEMY_SPAWNFLAGS_HL	0x3FF

class CNightfireEnemy : public CSquadMonster
{
public:
	void Spawn( void );
	void Precache( void );
	void KeyValue( KeyValueData *pkvd );
	void SetYawSpeed( void );
	int Classify( void );
	int ISoundMask( void );
	void Killed( entvars_t *pevAttacker, int iGib );

	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];

private:
	float m_flSightDist;
	string_t m_iszDeathTarget;
};

TYPEDESCRIPTION CNightfireEnemy::m_SaveData[] =
{
	DEFINE_FIELD( CNightfireEnemy, m_flSightDist, FIELD_FLOAT ),
	DEFINE_FIELD( CNightfireEnemy, m_iszDeathTarget, FIELD_STRING ),
};

IMPLEMENT_SAVERESTORE( CNightfireEnemy, CSquadMonster )

LINK_ENTITY_TO_CLASS( enemy_generic, CNightfireEnemy )

void CNightfireEnemy::KeyValue( KeyValueData *pkvd )
{
	if( FStrEq( pkvd->szKeyName, "sight_dist" ))
	{
		m_flSightDist = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "deathtarget" ))
	{
		m_iszDeathTarget = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "Spawnflags" ))
	{
		// capitalised in Nightfire maps; the engine only parses "spawnflags"
		pev->spawnflags = atoi( pkvd->szValue ) & NF_ENEMY_SPAWNFLAGS_HL;
		pkvd->fHandled = TRUE;
	}
	else
		CSquadMonster::KeyValue( pkvd );
}

int CNightfireEnemy::Classify( void )
{
	return CLASS_HUMAN_MILITARY;
}

void CNightfireEnemy::SetYawSpeed( void )
{
	switch( m_Activity )
	{
	case ACT_RUN:
	case ACT_WALK:
		pev->yaw_speed = 180;
		break;
	default:
		pev->yaw_speed = 120;
		break;
	}
}

int CNightfireEnemy::ISoundMask( void )
{
	return bits_SOUND_WORLD | bits_SOUND_COMBAT | bits_SOUND_PLAYER | bits_SOUND_DANGER;
}

void CNightfireEnemy::Precache( void )
{
	PRECACHE_MODEL( STRING( pev->model ));
}

void CNightfireEnemy::Spawn( void )
{
	if( FStringNull( pev->model ))
	{
		ALERT( at_error, "enemy_generic at %.0f %.0f %.0f has no model\n",
			(double)pev->origin.x, (double)pev->origin.y, (double)pev->origin.z );
		REMOVE_ENTITY( ENT( pev ));
		return;
	}

	Precache();
	SET_MODEL( ENT( pev ), STRING( pev->model ));
	UTIL_SetSize( pev, VEC_HUMAN_HULL_MIN, VEC_HUMAN_HULL_MAX );

	pev->solid = SOLID_SLIDEBOX;
	pev->movetype = MOVETYPE_STEP;
	m_bloodColor = BLOOD_COLOR_RED;
	if( pev->health <= 0 )
		pev->health = 50;	// [assumed] no health key in the maps
	m_flFieldOfView = VIEW_FIELD_WIDE;
	m_MonsterState = MONSTERSTATE_NONE;
	m_afCapability = bits_CAP_SQUAD | bits_CAP_TURN_HEAD | bits_CAP_DOORS_GROUP;

	MonsterInit();

	// MonsterInit resets the sight distance to the HLSDK default
	if( m_flSightDist > 0 )
		m_flDistLook = m_flSightDist;
}

void CNightfireEnemy::Killed( entvars_t *pevAttacker, int iGib )
{
	if( !FStringNull( m_iszDeathTarget ))
		FireTargets( STRING( m_iszDeathTarget ), CBaseEntity::Instance( pevAttacker ), this, USE_TOGGLE, 0 );

	CSquadMonster::Killed( pevAttacker, iGib );
}
