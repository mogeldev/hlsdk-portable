/*
nf_enemy.cpp - James Bond 007: Nightfire (PC) enemy_generic for the Xash3D port

The Nightfire generic enemy is a descendant of the Half-Life human grunt: its
models use the grunt's animation event numbers (2 reload, 3 kick, 4/5/6 shot,
7 grenade toss, 11 drop gun) and sequence names (standing_mgun,
crouching_mgun, throwgrenade, ...), and label sequences with Half-Life
activity numbers. So enemy_generic derives from CHGrunt and keeps its AI
(cover, reload, grenades, squads) and replaces what is Half-Life content:
model, weapons, sounds, sentences, gibs.

Keys (no Nightfire FGD; inferred from the retail maps, assumptions):
  model            character model (commando, hazmat_light, black_ops, ...)
  netname          squad name (HLSDK squads use netname as well)
  sight_dist       how far the enemy sees (512..4096 on m5/m7)
  Spawnflags       capitalised (parsed into pev->spawnflags anyway: entvars
                   keys match case-insensitively); the low bits match the
                   HLSDK monster flags (4 hit monsterclip, 32 squad leader,
                   128 wait for script), the high bits (0x4000, 0x400000,
                   0x2000000) are unknown and masked off in Spawn
  deathtarget      fired when the enemy dies
  primary_weapon   weapon id, see g_nfEnemyWeapons (from the retail game.dll:
                   the precache switch of the enemy class, case = id - 1)
  gun_index        value of the model's "weapons" bodygroup (0 = no gun)
  num_grenades     hand grenades to throw
  char_name        voice directory: sound/<char_name>/pain|die|combatN.wav
  voice_pitch      voice pitch (100 on m5)
  minpatroldist / maxpatroldist  straight-line range to the next patrol node
                   (maxpatroldist 0 = stand guard)
  maxpatrolpath    longest node-graph route to the next patrol node
  waitpatroltime   seconds to wait at each patrol node
  TriggerTarget / TriggerCondition: parsed by CBaseMonster; the Nightfire
                   condition values (e.g. 4362) are not HLSDK conditions

Patrols (from the retail game.dll, the node picker at 0x4203b390): patrol
nodes are info_node with nodetype 16 or 32 (what 32 adds is unknown; it does
not mean running); their skin is the route group. Idle enemies walk the
route, alerted ones run (the retail "Walk Patrol" / "Run Patrol" schedules
are chosen by the enemy's state). The enemy walks to the nearest (by route length) patrol node of
the group of the node it stands on, skipping the node it is on, the previous
one and the last 10 visited, within the patrol distances.

Damage and health come from the retail skill.cfg (sk_enemy_*, sk_<weapon>_*),
whose cvars are registered by NF_RegisterSkillCvars().
*/

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "schedule.h"
#include "squadmonster.h"
#include "soundent.h"
#include "weapons.h"
#include "studio.h"
#include "animation.h"
#include "hgrunt.h"
#include "nodes.h"
#include "nf_debug.h"

// HLSDK monster spawnflags that Nightfire maps use with the same meaning
#define NF_ENEMY_SPAWNFLAGS_HL	0x3FF
#define NF_ENEMY_MAX_VOICE	9	// the retail code formats %s/pain%d.wav up to 9
#define NF_PATROL_HISTORY	10	// visited patrol nodes remembered (retail: 10)
#define NF_PATROL_MAX_CAND	20	// candidates sorted by route length (retail: 20)

enum
{
	SCHED_NF_PATROL = LAST_COMMON_SCHEDULE + 100,
};

enum
{
	TASK_NF_PATROL_MOVE = LAST_COMMON_TASK + 100,	// pick the next patrol node, start moving
	TASK_NF_PATROL_WAIT,				// stand waitpatroltime at the node
};

//=========================================================
// skill.cfg cvars of the Nightfire characters
//=========================================================
static const char *const g_nfSkillCvars[] =
{
	"sk_enemy_health", "sk_enemy_kick", "sk_enemy_pellets", "sk_enemy_gspeed",
	"sk_enemy_rocket", "sk_commando_bullet", "sk_mp9_bullet", "sk_pdw90_bullet",
	"sk_kowloon_bullet", "sk_buckshot_bullet", "sk_sniper_bullet",
	"sk_raptor_bullet", "sk_minigun_bullet", "sk_alerted_bullet", "sk_laser_bolt",
	// player weapons (dlls/nf_pp9.cpp, dlls/nf_guns.cpp)
	"sk_plr_pp9_bullet", "sk_plr_mp9_bullet", "sk_plr_commando_bullet", "sk_plr_pdw90_bullet",
	"sk_plr_kowloon_bullet", "sk_plr_raptor_bullet", "sk_plr_sniper_bullet",	// sk_plr_buckshot: Half-Life registers it
	"sk_plr_minigun_bullet",
};

static cvar_t g_nfSkill[ARRAYSIZE( g_nfSkillCvars ) * 3];
static char g_nfSkillNames[ARRAYSIZE( g_nfSkillCvars ) * 3][32];

// called from GameDLLInit before skill.cfg is executed
void NF_RegisterSkillCvars( void )
{
	for( size_t i = 0; i < ARRAYSIZE( g_nfSkill ); i++ )
	{
		snprintf( g_nfSkillNames[i], sizeof( g_nfSkillNames[i] ), "%s%d",
			g_nfSkillCvars[i / 3], (int)( i % 3 ) + 1 );
		g_nfSkill[i].name = g_nfSkillNames[i];
		g_nfSkill[i].string = (char *)"0";
		CVAR_REGISTER( &g_nfSkill[i] );
	}
}

float NF_SkillValue( const char *base )
{
	int level = g_iSkillLevel >= 1 && g_iSkillLevel <= 3 ? g_iSkillLevel : 2;
	return CVAR_GET_FLOAT( UTIL_VarArgs( "%s%d", base, level ));
}

//=========================================================
// weapons by primary_weapon id
//=========================================================
struct nf_enemy_weapon_t
{
	const char *fire;	// sound pattern with %d, 1..numfire
	int numfire;
	const char *reload;	// NULL: none
	const char *damage;	// skill cvar base
	int clip;		// [assumed] rounds before reloading
	BOOL buckshot;		// pellets from sk_enemy_pellets
	BOOL sniper;		// standing_sniper instead of the mgun sequences
};

// [assumed] clip sizes and spread; sounds and damage cvars from game.dll /
// skill.cfg. Ids 10 (Stinger rockets) and 12 (laser) are not implemented.
static const nf_enemy_weapon_t g_nfEnemyWeapons[] =
{
	{ NULL },											// 0 none
	{ "weapons/sig552_fire%d.wav", 3, "weapons/sig552_reload_empty.wav", "sk_commando_bullet", 30 },	// 1 SIG552
	{ "weapons/frinesi_fire%d.wav", 1, "weapons/frinesi_reload1.wav", "sk_buckshot_bullet", 8, TRUE },	// 2 Frinesi
	{ "weapons/mp9_fire%d.wav", 3, "weapons/mp9_reload_empty.wav", "sk_mp9_bullet", 32 },		// 3 MP9
	{ "weapons/mp9_fire_sil%d.wav", 2, "weapons/mp9_reload_empty.wav", "sk_mp9_bullet", 32 },		// 4 MP9 silenced
	{ "weapons/l96_fire%d.wav", 2, "weapons/l96_reload_empty.wav", "sk_sniper_bullet", 5, FALSE, TRUE },	// 5 L96
	{ "weapons/l96_fire%d.wav", 2, "weapons/l96_reload_empty.wav", "sk_sniper_bullet", 5, FALSE, TRUE },	// 6 L96
	{ "weapons/pp9_fire%d.wav", 2, "weapons/pp9_reload_empty.wav", "sk_kowloon_bullet", 16 },		// 7 PP9
	{ "weapons/p90_fire%d.wav", 3, "weapons/p90_reload.wav", "sk_pdw90_bullet", 50 },			// 8 P90
	{ "weapons/raptor_fire%d.wav", 1, "weapons/raptor_reload_empty.wav", "sk_raptor_bullet", 6 },	// 9 Raptor
	{ NULL },											// 10 Stinger
	{ "weapons/mini_fire.wav", 1, NULL, "sk_minigun_bullet", 200 },					// 11 minigun
	{ NULL },											// 12 laser
};

class CNightfireEnemy : public CHGrunt
{
public:
	void Spawn( void );
	void Precache( void );
	void KeyValue( KeyValueData *pkvd );
	void SetYawSpeed( void );
	void HandleAnimEvent( MonsterEvent_t *pEvent );
	BOOL CheckRangeAttack1( float flDot, float flDist );
	void SetActivity( Activity NewActivity );
	void PrescheduleThink( void );
	void PainSound( void );
	void DeathSound( void );
	void GibMonster( void );
	BOOL ShouldGibMonster( int iGib ) { return FALSE; }	// Nightfire has no gib models
	Activity GetDeathActivity( void );
	int TakeDamage( entvars_t *pevInflictor, entvars_t *pevAttacker, float flDamage, int bitsDamageType );
	void TraceAttack( entvars_t *pevAttacker, float flDamage, Vector vecDir, TraceResult *ptr, int bitsDamageType );
	void Killed( entvars_t *pevAttacker, int iGib );
	BOOL FOkToSpeak( void ) { return FALSE; }	// no HG_* sentence groups in Nightfire
	Schedule_t *GetSchedule( void );
	Schedule_t *GetScheduleOfType( int Type );
	void StartTask( Task_t *pTask );
	void RunTask( Task_t *pTask );

	CUSTOM_SCHEDULES

	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];

private:
	const nf_enemy_weapon_t *Weapon( void );
	int FindBodygroup( const char *name );
	void NFShoot( void );
	void PlayVoice( const char *kind, int count, float attn );
	int CountVoice( const char *kind );
	BOOL StartPatrolMove( void );
	BOOL InPatrolHistory( int node );

	float m_flSightDist;
	string_t m_iszDeathTarget;
	string_t m_iszCharName;
	int m_iWeapon;
	int m_iGunIndex;
	int m_cGrenades;
	int m_cPain, m_cDie, m_cCombat;
	float m_flNextCombatTalk;

	float m_flMinPatrolDist;
	float m_flMaxPatrolDist;
	float m_flMaxPatrolPath;
	float m_flWaitPatrolTime;
	int m_iPatrolNodes[NF_PATROL_HISTORY];	// visited, ring buffer, -1 = empty
	int m_iPatrolIdx;
	int m_iPatrolTarget;
	float m_flNextPatrolTime;
	int m_bitsLastDamage;	// damage type of the latest hit (death animation)
};

TYPEDESCRIPTION CNightfireEnemy::m_SaveData[] =
{
	DEFINE_FIELD( CNightfireEnemy, m_flSightDist, FIELD_FLOAT ),
	DEFINE_FIELD( CNightfireEnemy, m_iszDeathTarget, FIELD_STRING ),
	DEFINE_FIELD( CNightfireEnemy, m_iszCharName, FIELD_STRING ),
	DEFINE_FIELD( CNightfireEnemy, m_iWeapon, FIELD_INTEGER ),
	DEFINE_FIELD( CNightfireEnemy, m_iGunIndex, FIELD_INTEGER ),
	DEFINE_FIELD( CNightfireEnemy, m_cGrenades, FIELD_INTEGER ),
	DEFINE_FIELD( CNightfireEnemy, m_flNextCombatTalk, FIELD_TIME ),
	DEFINE_FIELD( CNightfireEnemy, m_flMinPatrolDist, FIELD_FLOAT ),
	DEFINE_FIELD( CNightfireEnemy, m_flMaxPatrolDist, FIELD_FLOAT ),
	DEFINE_FIELD( CNightfireEnemy, m_flMaxPatrolPath, FIELD_FLOAT ),
	DEFINE_FIELD( CNightfireEnemy, m_flWaitPatrolTime, FIELD_FLOAT ),
	DEFINE_ARRAY( CNightfireEnemy, m_iPatrolNodes, FIELD_INTEGER, NF_PATROL_HISTORY ),
	DEFINE_FIELD( CNightfireEnemy, m_iPatrolIdx, FIELD_INTEGER ),
	DEFINE_FIELD( CNightfireEnemy, m_iPatrolTarget, FIELD_INTEGER ),
	DEFINE_FIELD( CNightfireEnemy, m_flNextPatrolTime, FIELD_TIME ),
	DEFINE_FIELD( CNightfireEnemy, m_bitsLastDamage, FIELD_INTEGER ),
};

IMPLEMENT_SAVERESTORE( CNightfireEnemy, CHGrunt )

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
	else if( FStrEq( pkvd->szKeyName, "char_name" ))
	{
		m_iszCharName = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "primary_weapon" ))
	{
		m_iWeapon = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "gun_index" ))
	{
		m_iGunIndex = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "num_grenades" ))
	{
		m_cGrenades = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "voice_pitch" ))
	{
		m_voicePitch = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "minpatroldist" ))
	{
		m_flMinPatrolDist = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "maxpatroldist" ))
	{
		m_flMaxPatrolDist = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "maxpatrolpath" ))
	{
		m_flMaxPatrolPath = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "waitpatroltime" ))
	{
		m_flWaitPatrolTime = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else
		CHGrunt::KeyValue( pkvd );
}

// weapon pickup dropped for a primary_weapon id; NULL while the player
// weapon does not exist yet
static const char *NF_EnemyWeaponPickup( int id )
{
	switch( id )
	{
	case 1: return "weapon_commando";	// SIG552
	case 2: return "weapon_frinesi";
	case 5: return "weapon_l96a1";
	case 6: return "weapon_l96a1";		// [assumed] not the winter version
	case 9: return "weapon_raptor";
	case 11: return "weapon_minigun";
	case 3: return "weapon_mp9";
	case 4: return "weapon_mp9_silenced";
	case 7: return "weapon_pp9";
	case 8: return "weapon_pdw90";		// P90
	default: return NULL;
	}
}

const nf_enemy_weapon_t *CNightfireEnemy::Weapon( void )
{
	if( m_iWeapon <= 0 || m_iWeapon >= (int)ARRAYSIZE( g_nfEnemyWeapons ) || !g_nfEnemyWeapons[m_iWeapon].fire )
		return NULL;
	return &g_nfEnemyWeapons[m_iWeapon];
}

int CNightfireEnemy::FindBodygroup( const char *name )
{
	studiohdr_t *phdr = (studiohdr_t *)GET_MODEL_PTR( ENT( pev ));
	if( !phdr )
		return -1;

	mstudiobodyparts_t *pbp = (mstudiobodyparts_t *)( (byte *)phdr + phdr->bodypartindex );
	for( int i = 0; i < phdr->numbodyparts; i++ )
	{
		if( !stricmp( pbp[i].name, name ))
			return i;
	}
	return -1;
}

int CNightfireEnemy::CountVoice( const char *kind )
{
	if( FStringNull( m_iszCharName ))
		return 0;

	int n = 0;
	while( n < NF_ENEMY_MAX_VOICE )
	{
		int len = 0;
		byte *data = LOAD_FILE_FOR_ME( UTIL_VarArgs( "sound/%s/%s%d.wav", STRING( m_iszCharName ), kind, n + 1 ), &len );
		if( !data )
			break;
		FREE_FILE( data );
		PRECACHE_SOUND( UTIL_VarArgs( "%s/%s%d.wav", STRING( m_iszCharName ), kind, n + 1 ));
		n++;
	}
	return n;
}

void CNightfireEnemy::PlayVoice( const char *kind, int count, float attn )
{
	if( count <= 0 || FStringNull( m_iszCharName ))
		return;

	EMIT_SOUND_DYN( ENT( pev ), CHAN_VOICE,
		UTIL_VarArgs( "%s/%s%d.wav", STRING( m_iszCharName ), kind, RANDOM_LONG( 1, count )),
		1, attn, 0, m_voicePitch );
}

void CNightfireEnemy::Precache( void )
{
	PRECACHE_MODEL( STRING( pev->model ));

	const nf_enemy_weapon_t *w = Weapon();
	if( w )
	{
		for( int i = 1; i <= w->numfire; i++ )
			PRECACHE_SOUND( UTIL_VarArgs( w->fire, i ));
		if( w->reload )
			PRECACHE_SOUND( w->reload );
	}

	m_cPain = CountVoice( "pain" );
	m_cDie = CountVoice( "die" );
	m_cCombat = CountVoice( "combat" );

	m_iBrassShell = PRECACHE_MODEL( "models/shell.mdl" );
	if( m_cGrenades > 0 )
	{
		PRECACHE_MODEL( "models/w_frag_grenade.mdl" );
		// CGrenade::ShootTimed sets the HL model before we replace it; W_Precache
		// no longer precaches it (HL hand grenade unregistered). The file does
		// not exist in Nightfire, but a late precache would warn every throw.
		PRECACHE_MODEL( "models/w_grenade.mdl" );
	}
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

	// "Spawnflags" (capitalised) already landed in pev->spawnflags:
	// EntvarsKeyvalue compares key names case-insensitively
	pev->spawnflags &= NF_ENEMY_SPAWNFLAGS_HL;

	pev->solid = SOLID_SLIDEBOX;
	pev->movetype = MOVETYPE_STEP;
	m_bloodColor = BLOOD_COLOR_RED;
	pev->effects = 0;
	if( pev->health <= 0 )
		pev->health = NF_SkillValue( "sk_enemy_health" );
	if( pev->health <= 0 )
		pev->health = 50;	// skill.cfg missing
	m_flFieldOfView = VIEW_FIELD_WIDE;
	m_MonsterState = MONSTERSTATE_NONE;
	m_flNextGrenadeCheck = gpGlobals->time + 1;
	m_flNextPainTime = gpGlobals->time;
	m_iSentence = -1;
	m_afCapability = bits_CAP_SQUAD | bits_CAP_TURN_HEAD | bits_CAP_DOORS_GROUP;
	m_fEnemyEluded = FALSE;
	m_fFirstEncounter = TRUE;	// squad leader signals (SignalSuppress) on the first encounter
	m_HackedGunPos = Vector( 0, 0, 55 );
	if( m_voicePitch < 50 || m_voicePitch > 200 )
		m_voicePitch = PITCH_NORM;

	// CHGrunt picks its sequences and attacks by these bits; the shotgun and
	// grenade launcher bits stay off (no such sequences in the models)
	const nf_enemy_weapon_t *w = Weapon();
	pev->weapons = w ? HGRUNT_9MMAR : 0;
	if( m_cGrenades > 0 )
		pev->weapons |= HGRUNT_HANDGRENADE;
	m_cClipSize = w ? w->clip : 0;
	m_cAmmoLoaded = m_cClipSize;

	int group = FindBodygroup( "weapons" );
	if( group >= 0 )
		SetBodygroup( group, m_iGunIndex );

	for( int i = 0; i < NF_PATROL_HISTORY; i++ )
		m_iPatrolNodes[i] = -1;
	m_iPatrolIdx = 0;
	m_iPatrolTarget = -1;
	if( m_flMaxPatrolPath <= 0 )
		m_flMaxPatrolPath = 2048;	// [assumed] the value on most retail enemies

	MonsterInit();

	if( NF_DEBUG( NF_DBG_MONSTERS ))
		ALERT( at_console, "nf_debug: spawn enemy '%s' %s (%s) at %.0f %.0f %.0f weapon %d grenades %d spawnflags 0x%x\n",
			STRING( pev->targetname ), STRING( m_iszCharName ), STRING( pev->model ), pev->origin.x, pev->origin.y, pev->origin.z,
			m_iWeapon, m_cGrenades, pev->spawnflags );

	// MonsterInit resets the sight distance to the HLSDK default
	if( m_flSightDist > 0 )
		m_flDistLook = m_flSightDist;
}

void CNightfireEnemy::SetYawSpeed( void )
{
	switch( m_Activity )
	{
	case ACT_RANGE_ATTACK1:
	case ACT_RANGE_ATTACK2:
	case ACT_RELOAD:
		pev->yaw_speed = 150;	// keep tracking the target while shooting
		break;
	default:
		pev->yaw_speed = 180;
		break;
	}
}

BOOL CNightfireEnemy::CheckRangeAttack1( float flDot, float flDist )
{
	if( !Weapon())
		return FALSE;
	return CHGrunt::CheckRangeAttack1( flDot, flDist );
}

// Nightfire numbers the flinches after ACT_FLINCH_STOMACH differently from
// Half-Life (73 groin, 74/75 left/right upper arm, 76/77 left/right leg).
#define NF_ACT_FLINCH_LEFTARM	74
#define NF_ACT_FLINCH_RIGHTARM	75
#define NF_ACT_FLINCH_LEFTLEG	76
#define NF_ACT_FLINCH_RIGHTLEG	77

void CNightfireEnemy::SetActivity( Activity NewActivity )
{
	const nf_enemy_weapon_t *w = Weapon();
	int iSequence;

	// Activities the grunt AI asks for that the Nightfire models lack or
	// number differently. Without a sequence the monster falls back to
	// sequence 0 (idle1, 4.3 s) and a TASK_PLAY_SEQUENCE waits for it.
	switch( NewActivity )
	{
	case ACT_RANGE_ATTACK1:
		// CHGrunt looks for standing_mp5 / crouching_mp5
		iSequence = LookupSequence( w && w->sniper ? "standing_sniper" : m_fStanding ? "standing_mgun" : "crouching_mgun" );
		if( iSequence <= ACTIVITY_NOT_AVAILABLE )
			iSequence = LookupActivity( NewActivity );
		break;
	case ACT_SIGNAL1:
	case ACT_SIGNAL2:
	case ACT_SIGNAL3:
	case ACT_VICTORY_DANCE:
	case ACT_SPECIAL_ATTACK1:
		// no hand-signal sequences in the models; a 2-frame combat pose
		// keeps the signal schedules (first encounter, found enemy) short
		iSequence = LookupSequence( "combatidle" );
		break;
	case ACT_DIEVIOLENT:
		// explosions throw the enemy: die_explosion1-6 (activity 39 also
		// holds die_shotgun1/2 and die_falling_loop)
		iSequence = ACTIVITY_NOT_AVAILABLE;
		if( m_bitsLastDamage & DMG_BLAST )
		{
			int seqs[6], n = 0;
			for( int i = 1; i <= 6; i++ )
			{
				int s = LookupSequence( UTIL_VarArgs( "die_explosion%d", i ));
				if( s > ACTIVITY_NOT_AVAILABLE )
					seqs[n++] = s;
			}
			if( n )
				iSequence = seqs[RANDOM_LONG( 0, n - 1 )];
		}
		if( iSequence <= ACTIVITY_NOT_AVAILABLE )
			iSequence = LookupActivity( NewActivity );
		break;
	case ACT_SMALL_FLINCH:
	case ACT_BIG_FLINCH:	// 27 is the electrocution loop in Nightfire
		iSequence = LookupActivity( ACT_FLINCH_CHEST );
		break;
	case ACT_FLINCH_LEFTARM:
		iSequence = LookupActivity( (Activity)NF_ACT_FLINCH_LEFTARM );
		break;
	case ACT_FLINCH_RIGHTARM:
		iSequence = LookupActivity( (Activity)NF_ACT_FLINCH_RIGHTARM );
		break;
	case ACT_FLINCH_LEFTLEG:
		iSequence = LookupActivity( (Activity)NF_ACT_FLINCH_LEFTLEG );
		break;
	case ACT_FLINCH_RIGHTLEG:
		iSequence = LookupActivity( (Activity)NF_ACT_FLINCH_RIGHTLEG );
		break;
	default:
		CHGrunt::SetActivity( NewActivity );
		return;
	}

	if( iSequence <= ACTIVITY_NOT_AVAILABLE )
		iSequence = LookupActivity( ACT_IDLE_ANGRY );

	// m_Activity keeps the requested activity, or MaintainSchedule would
	// call SetActivity again every think
	m_Activity = NewActivity;
	if( iSequence > ACTIVITY_NOT_AVAILABLE )
	{
		if( pev->sequence != iSequence || !m_fSequenceLoops )
			pev->frame = 0;
		pev->sequence = iSequence;
		ResetSequenceInfo();
		SetYawSpeed();
	}
	else
		pev->sequence = 0;
}

void CNightfireEnemy::NFShoot( void )
{
	const nf_enemy_weapon_t *w = Weapon();
	if( !w || m_hEnemy == 0 )
		return;

	Vector vecShootOrigin = GetGunPosition();
	Vector vecShootDir = ShootAtEnemy( vecShootOrigin );

	UTIL_MakeVectors( pev->angles );

	int damage = (int)NF_SkillValue( w->damage );
	if( damage < 1 )
		damage = 1;	// 0 would make FireBullets fall back to HL skill data

	if( w->buckshot )
	{
		int pellets = (int)NF_SkillValue( "sk_enemy_pellets" );
		FireBullets( pellets > 0 ? pellets : 4, vecShootOrigin, vecShootDir, VECTOR_CONE_15DEGREES, 2048, BULLET_MONSTER_MP5, 0, damage );
	}
	else
	{
		Vector vecShellVelocity = gpGlobals->v_right * RANDOM_FLOAT( 40, 90 ) + gpGlobals->v_up * RANDOM_FLOAT( 75, 200 ) + gpGlobals->v_forward * RANDOM_FLOAT( -40, 40 );
		EjectBrass( vecShootOrigin - vecShootDir * 24, vecShellVelocity, pev->angles.y, m_iBrassShell, TE_BOUNCE_SHELL );
		FireBullets( 1, vecShootOrigin, vecShootDir, w->sniper ? VECTOR_CONE_1DEGREES : VECTOR_CONE_6DEGREES, 4096, BULLET_MONSTER_MP5, 2, damage );
	}

	EMIT_SOUND( ENT( pev ), CHAN_WEAPON, UTIL_VarArgs( w->fire, RANDOM_LONG( 1, w->numfire )), 1, ATTN_NORM );
	CSoundEnt::InsertSound( bits_SOUND_COMBAT, pev->origin, 384, 0.3 );

	pev->effects |= EF_MUZZLEFLASH;
	m_cAmmoLoaded--;

	Vector angDir = UTIL_VecToAngles( vecShootDir );
	SetBlending( 0, angDir.x );
}

void CNightfireEnemy::HandleAnimEvent( MonsterEvent_t *pEvent )
{
	switch( pEvent->event )
	{
	case HGRUNT_AE_BURST1:
	case HGRUNT_AE_BURST2:
	case HGRUNT_AE_BURST3:
		// every Nightfire shot event is one round with its own sound
		NFShoot();
		break;
	case HGRUNT_AE_RELOAD:
	{
		const nf_enemy_weapon_t *w = Weapon();
		if( w && w->reload )
			EMIT_SOUND( ENT( pev ), CHAN_WEAPON, w->reload, 1, ATTN_NORM );
		m_cAmmoLoaded = m_cClipSize;
		ClearConditions( bits_COND_NO_AMMO_LOADED );
		break;
	}
	case HGRUNT_AE_GREN_TOSS:
	{
		UTIL_MakeVectors( pev->angles );
		CGrenade *pGrenade = CGrenade::ShootTimed( pev, GetGunPosition(), m_vecTossVelocity, 3.5 );
		if( pGrenade )
		{
			SET_MODEL( ENT( pGrenade->pev ), "models/w_frag_grenade.mdl" );
			UTIL_SetSize( pGrenade->pev, g_vecZero, g_vecZero );
		}

		m_fThrowGrenade = FALSE;
		m_flNextGrenadeCheck = gpGlobals->time + 6;
		if( --m_cGrenades <= 0 )
			pev->weapons &= ~HGRUNT_HANDGRENADE;
		break;
	}
	case HGRUNT_AE_KICK:
	{
		CBaseEntity *pHurt = Kick();
		if( pHurt )
		{
			UTIL_MakeVectors( pev->angles );
			pHurt->pev->punchangle.x = 15;
			pHurt->pev->velocity = pHurt->pev->velocity + gpGlobals->v_forward * 100 + gpGlobals->v_up * 50;
			pHurt->TakeDamage( pev, pev, NF_SkillValue( "sk_enemy_kick" ), DMG_CLUB );
		}
		break;
	}
	case HGRUNT_AE_DROP_GUN:
	{
		// the dying enemy drops his weapon as a pickup (event 11 at the
		// start of the death sequences) and the gun leaves his hand
		const char *pickup = NF_EnemyWeaponPickup( m_iWeapon );
		int group = FindBodygroup( "weapons" );
		if( pickup && group >= 0 && GetBodygroup( group ) != 0 )
		{
			Vector vecGunPos, vecGunAngles;
			GetAttachment( 0, vecGunPos, vecGunAngles );
			SetBodygroup( group, 0 );
			DropItem( pickup, vecGunPos, vecGunAngles );
		}
		break;
	}
	case 13:	// throw a grenade back
	case 14:	// kick a grenade away
		// [not yet] grenade throw-back / kick
		break;
	default:
		CSquadMonster::HandleAnimEvent( pEvent );
		break;
	}
}

void CNightfireEnemy::PrescheduleThink( void )
{
	if( HasConditions( bits_COND_NEW_ENEMY ) && gpGlobals->time > m_flNextCombatTalk )
	{
		PlayVoice( "combat", m_cCombat, ATTN_NORM );
		m_flNextCombatTalk = gpGlobals->time + RANDOM_FLOAT( 6, 10 );
	}

	CHGrunt::PrescheduleThink();
}

void CNightfireEnemy::PainSound( void )
{
	if( gpGlobals->time > m_flNextPainTime )
	{
		PlayVoice( "pain", m_cPain, ATTN_NORM );
		m_flNextPainTime = gpGlobals->time + 1;
	}
}

void CNightfireEnemy::DeathSound( void )
{
	PlayVoice( "die", m_cDie, ATTN_IDLE );
}

int CNightfireEnemy::TakeDamage( entvars_t *pevInflictor, entvars_t *pevAttacker, float flDamage, int bitsDamageType )
{
	// m_bitsDamageType accumulates over the enemy's life; the death
	// animation depends on the hit that kills
	m_bitsLastDamage = bitsDamageType;
	return CHGrunt::TakeDamage( pevInflictor, pevAttacker, flDamage, bitsDamageType );
}

Activity CNightfireEnemy::GetDeathActivity( void )
{
	if( pev->deadflag != DEAD_NO )
		return m_IdealActivity;

	// In the retail game explosions throw enemies away with a death
	// animation (user, retail playthrough); the HL death activity choice
	// never uses ACT_DIEVIOLENT.
	if(( m_bitsLastDamage & DMG_BLAST ) && LookupActivity( ACT_DIEVIOLENT ) > ACTIVITY_NOT_AVAILABLE )
		return ACT_DIEVIOLENT;

	return CHGrunt::GetDeathActivity();
}

void CNightfireEnemy::GibMonster( void )
{
	// CHGrunt would throw Half-Life weapons
	CBaseMonster::GibMonster();
}

void CNightfireEnemy::TraceAttack( entvars_t *pevAttacker, float flDamage, Vector vecDir, TraceResult *ptr, int bitsDamageType )
{
	// CHGrunt treats bodygroup 1 as the helmet group; Nightfire's is "heads"
	CSquadMonster::TraceAttack( pevAttacker, flDamage, vecDir, ptr, bitsDamageType );
}

void CNightfireEnemy::Killed( entvars_t *pevAttacker, int iGib )
{
	if( !FStringNull( m_iszDeathTarget ))
		FireTargets( STRING( m_iszDeathTarget ), CBaseEntity::Instance( pevAttacker ), this, USE_TOGGLE, 0 );

	CHGrunt::Killed( pevAttacker, iGib );
}

//=========================================================
// Patrols
//=========================================================
Task_t tlNFPatrol[] =
{
	{ TASK_NF_PATROL_MOVE, (float)0 },
	{ TASK_WAIT_FOR_MOVEMENT, (float)0 },
	{ TASK_NF_PATROL_WAIT, (float)0 },
};

Schedule_t slNFPatrol[] =
{
	{
		tlNFPatrol,
		ARRAYSIZE( tlNFPatrol ),
		bits_COND_NEW_ENEMY |
		bits_COND_SEE_ENEMY |
		bits_COND_SEE_FEAR |
		bits_COND_LIGHT_DAMAGE |
		bits_COND_HEAVY_DAMAGE |
		bits_COND_PROVOKED |
		bits_COND_HEAR_SOUND,
		bits_SOUND_COMBAT |
		bits_SOUND_PLAYER |
		bits_SOUND_DANGER,
		"NFPatrol"
	},
};

DEFINE_CUSTOM_SCHEDULES( CNightfireEnemy )
{
	slNFPatrol,
};

IMPLEMENT_CUSTOM_SCHEDULES( CNightfireEnemy, CHGrunt )

BOOL CNightfireEnemy::InPatrolHistory( int node )
{
	for( int i = 0; i < NF_PATROL_HISTORY; i++ )
	{
		if( m_iPatrolNodes[i] == node )
			return TRUE;
	}
	return FALSE;
}

// Pick the next patrol node and start moving there (retail: the node picker
// at 0x4203b390). Candidates: patrol nodes of the current node's group (both
// groups > 0 must match), not the current/previous/recently visited node,
// straight-line distance in [min, max], route length in [min, maxpath];
// tried nearest route first until a route can be built.
BOOL CNightfireEnemy::StartPatrolMove( void )
{
	if( m_flMaxPatrolDist <= 0 || !WorldGraph.m_fGraphPresent || !WorldGraph.m_fGraphPointersSet )
		return FALSE;

	// Standing on the last patrol target: that is the current node.
	// FindNearestNode may return a plain node beside it, which would make
	// the target itself the next candidate (instant arrival, endless loop).
	int iCur;
	if( m_iPatrolTarget >= 0 && m_iPatrolTarget < WorldGraph.m_cNodes &&
		( WorldGraph.m_pNodes[m_iPatrolTarget].m_vecOrigin - pev->origin ).Length2D() < 64 )
		iCur = m_iPatrolTarget;
	else
		iCur = WorldGraph.FindNearestNode( pev->origin, this );
	if( iCur < 0 )
		return FALSE;

	const int iHull = WorldGraph.HullIndex( this );
	const int iGroup = WorldGraph.m_pNodes[iCur].m_iNFGroup;
	const int iPrev = m_iPatrolNodes[( m_iPatrolIdx + NF_PATROL_HISTORY - 1 ) % NF_PATROL_HISTORY];

	int cand[NF_PATROL_MAX_CAND];
	float candLen[NF_PATROL_MAX_CAND];
	int nCand = 0;

	for( int i = 0; i < WorldGraph.m_cNodes; i++ )
	{
		const CNode &node = WorldGraph.m_pNodes[i];

		if( i == iCur || i == iPrev || !( node.m_afNFNodeType & NF_NODE_PATROL ) || InPatrolHistory( i ))
			continue;
		if( iGroup > 0 && node.m_iNFGroup > 0 && node.m_iNFGroup != iGroup )
			continue;

		float flDist = ( node.m_vecOrigin - pev->origin ).Length();
		if( flDist < m_flMinPatrolDist || flDist > m_flMaxPatrolDist )
			continue;
		if( ( node.m_vecOrigin - pev->origin ).Length2D() < 32 )
			continue;	// already there

		int iPath[MAX_PATH_SIZE];
		int nPath = WorldGraph.FindShortestPath( iPath, iCur, i, iHull, m_afCapability );
		if( nPath < 2 )
			continue;

		float flLen = 0;
		for( int k = 1; k < nPath; k++ )
			flLen += ( WorldGraph.m_pNodes[iPath[k]].m_vecOrigin - WorldGraph.m_pNodes[iPath[k - 1]].m_vecOrigin ).Length();
		if( flLen < m_flMinPatrolDist || flLen > m_flMaxPatrolPath )
			continue;

		// insert sorted by route length, keeping the nearest NF_PATROL_MAX_CAND
		int k;
		if( nCand < NF_PATROL_MAX_CAND )
			k = nCand++;
		else if( flLen < candLen[NF_PATROL_MAX_CAND - 1] )
			k = NF_PATROL_MAX_CAND - 1;
		else
			continue;
		while( k > 0 && candLen[k - 1] > flLen )
		{
			cand[k] = cand[k - 1];
			candLen[k] = candLen[k - 1];
			k--;
		}
		cand[k] = i;
		candLen[k] = flLen;
	}

	for( int c = 0; c < nCand; c++ )
	{
		const CNode &node = WorldGraph.m_pNodes[cand[c]];

		// the retail AI picks "Walk Patrol" or "Run Patrol" by the enemy's
		// state, not by the node type: run only when alerted
		Activity act = m_MonsterState == MONSTERSTATE_ALERT ? ACT_RUN : ACT_WALK;

		if( !MoveToLocation( act, 2, node.m_vecOrigin ))
			continue;

		m_iPatrolTarget = cand[c];
		// remember the patrol node we are leaving
		if(( WorldGraph.m_pNodes[iCur].m_afNFNodeType & NF_NODE_PATROL ) && !InPatrolHistory( iCur ))
		{
			m_iPatrolNodes[m_iPatrolIdx] = iCur;
			m_iPatrolIdx = ( m_iPatrolIdx + 1 ) % NF_PATROL_HISTORY;
		}
		return TRUE;
	}

	return FALSE;
}

Schedule_t *CNightfireEnemy::GetSchedule( void )
{
	if(( m_MonsterState == MONSTERSTATE_IDLE || m_MonsterState == MONSTERSTATE_ALERT ) &&
		m_flMaxPatrolDist > 0 && gpGlobals->time >= m_flNextPatrolTime &&
		!HasConditions( bits_COND_NEW_ENEMY | bits_COND_SEE_ENEMY | bits_COND_SEE_FEAR |
			bits_COND_LIGHT_DAMAGE | bits_COND_HEAVY_DAMAGE | bits_COND_PROVOKED |
			bits_COND_HEAR_SOUND | bits_COND_ENEMY_DEAD ))
		return GetScheduleOfType( SCHED_NF_PATROL );

	return CHGrunt::GetSchedule();
}

Schedule_t *CNightfireEnemy::GetScheduleOfType( int Type )
{
	if( Type == SCHED_NF_PATROL )
		return slNFPatrol;

	// a patrol route that fails on the way (blocked): stand a moment and
	// try again instead of the grunt's fail schedule (TASK_WAIT_PVS)
	if( Type == SCHED_FAIL && m_pSchedule == slNFPatrol )
	{
		m_flNextPatrolTime = gpGlobals->time + RANDOM_FLOAT( 3, 6 );
		return CHGrunt::GetScheduleOfType( SCHED_IDLE_STAND );
	}

	return CHGrunt::GetScheduleOfType( Type );
}

void CNightfireEnemy::StartTask( Task_t *pTask )
{
	switch( pTask->iTask )
	{
	case TASK_NF_PATROL_MOVE:
		if( StartPatrolMove())
		{
			TaskComplete();
			break;
		}

		// everything visited (or nothing reachable): forget the history and
		// try once more, else stand for a while
		for( int i = 0; i < NF_PATROL_HISTORY; i++ )
			m_iPatrolNodes[i] = -1;
		m_iPatrolIdx = 0;
		if( !StartPatrolMove())
		{
			// stand for a while; no TaskFail: the grunt's fail schedule ends
			// in TASK_WAIT_PVS, which waits as long as a player is in the PVS
			m_flNextPatrolTime = gpGlobals->time + RANDOM_FLOAT( 5, 10 );
		}
		TaskComplete();
		break;
	case TASK_NF_PATROL_WAIT:
		if( m_flWaitPatrolTime <= 0 )
		{
			TaskComplete();
			break;
		}
		m_IdealActivity = m_MonsterState == MONSTERSTATE_ALERT ? ACT_IDLE_ANGRY : ACT_IDLE;
		m_flWaitFinished = gpGlobals->time + m_flWaitPatrolTime;
		break;
	default:
		CHGrunt::StartTask( pTask );
		break;
	}
}

void CNightfireEnemy::RunTask( Task_t *pTask )
{
	switch( pTask->iTask )
	{
	case TASK_NF_PATROL_WAIT:
		if( gpGlobals->time >= m_flWaitFinished )
			TaskComplete();
		break;
	default:
		CHGrunt::RunTask( pTask );
		break;
	}
}
