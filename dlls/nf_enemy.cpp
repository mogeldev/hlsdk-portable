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

AI events (info_aievent, dlls/nf_aievent.cpp; retail GetSchedule 0x420634B0,
FindClosestAIEvent 0x4203BAB0, docs/retail/aievent.md): an alerted enemy that
hears something, or one that gets a new enemy, runs to the nearest free alarm
event (type 4), faces it (spawnflag 512) and plays its sequence; the
alarm_trigger sequence fires "alarmbutton" by its animation event 1003. A
named alarm event that is triggered sends one enemy within its radius.
An armed enemy with a new enemy and no alarm nearby takes the nearest free
cover event (type 5) that faces the player and is hidden from him, plays its
sequence there and stays in cover (retail GetCustomSchedule 0x42065730,
HandleCustomActivity 0x4205F170), by the sequence name:
  crouching_wait   crouch, stand up for 3 shots (standing_mgun), crouch again
  l/r_corner_idle  step out of the corner, 1..ammo/3 shots, step back
  vent_peek        (type 6, sent by the event when it sees the player) vent_shoot
It leaves cover (combat as usual) when the enemy sees it there or comes
within a quarter of the sight distance (crouch / vent: at most 128).
[not yet] corner grenade / roll-out, vent grenade, corner deaths.
  excludeaievents  bit mask, 1 << (eventtype - 1): event types never used
  initeventid      event type bound at spawn [not yet]

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
#include "nf_aievent.h"
#include "nf_taser.h"
#include "player.h"

// HLSDK monster spawnflags that Nightfire maps use with the same meaning
#define NF_ENEMY_SPAWNFLAGS_HL	0x3FF
#define NF_ENEMY_MAX_VOICE	9	// the retail code formats %s/pain%d.wav up to 9
#define NF_PATROL_HISTORY	10	// visited patrol nodes remembered (retail: 10)
#define NF_PATROL_MAX_CAND	20	// candidates sorted by route length (retail: 20)

enum
{
	SCHED_NF_PATROL = LAST_COMMON_SCHEDULE + 100,
	SCHED_NF_AIEVENT_PLAY,		// retail 51 "Grunt Goto AIEvent Play"
	SCHED_NF_COVER_WAIT,		// retail 79 / 80 / 86 "Wall / Duck / Duct Wait"
	SCHED_NF_DUCK_ATTACK,		// retail 78 "Duck Attack"
	SCHED_NF_WALL_ATTACK,		// retail 74 "Wall Attack"
	SCHED_NF_DUCT_ATTACK,		// retail 87 "Duct Attack"
};

// cover style at a type 5/6 event, by its sequence (retail SetCustomEvent 0x4205F090)
enum
{
	NF_COVER_NONE = 0,
	NF_COVER_LCORNER = 3,
	NF_COVER_CROUCH = 4,
	NF_COVER_RCORNER = 5,
	NF_COVER_VENT = 6,
};

enum
{
	TASK_NF_PATROL_MOVE = LAST_COMMON_TASK + 100,	// pick the next patrol node, start moving
	TASK_NF_PATROL_WAIT,				// stand waitpatroltime at the node
	TASK_NF_AIEVENT_PATH,				// retail 100: route to the event
	TASK_NF_AIEVENT_ARRIVE,				// retail 102/104: fire usetarget, pick the yaw
	TASK_NF_AIEVENT_PLAY,				// retail 103: play m_iszPlay, then the event's SequenceDone
	TASK_NF_COVER_STAND,				// retail 130: stand up from the crouch
	TASK_NF_COVER_CROUCH,				// retail 131: crouch again
	TASK_NF_CORNER_FIRE,				// retail 134: step out, fire, step back
	TASK_NF_TASER,
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
	"sk_plr_minigun_bullet", "sk_plr_taser",
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
	void UpdateOnRemove( void );
	void NFCountEnemy( CBaseEntity *pPlayer );
	BOOL TaserAcquire( CBaseEntity *weapon );
	BOOL TaserHeld( CBaseEntity *weapon );
	void TaserRelease( CBaseEntity *weapon );
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
public:
	CAIEvent *FindClosestAIEvent( int iType, BOOL fSkipLOS, BOOL fNeedFacing );
	BOOL ActivateAIEvent( CAIEvent *pEvent );
	void ReleaseAIEvent( BOOL fSkip );
	CAIEvent *AIEvent( void ) { return (CAIEvent *)(CBaseEntity *)m_hAIEvent; }
	BOOL SetCoverEvent( CAIEvent *pEvent );
private:
	Schedule_t *GetCoverSchedule( void );
	Schedule_t *LeaveCover( void );
	const char *CoverSequence( Activity act );
	BOOL InCover( void ) { return m_fAIEventDone && m_iCoverID != NF_COVER_NONE && m_hAIEvent != 0; }

	// mission stats (retail CBaseCharacter m_fCounted +0x3D5 / m_fDispatched +0x3D4)
	BOOL m_fNFCounted;
	BOOL m_fNFDispatched;
	EHANDLE m_hTaserWeapon;

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

	int m_iNFSpawnflags;		// Spawnflags before the HL mask (0x100000: no alarm search when alerted)
	int m_dwExcludeAIEvents;	// excludeaievents
	int m_iInitEventID;		// initeventid
	EHANDLE m_hAIEvent;		// event walked to / used
	BOOL m_fAIEventDone;		// its sequence is done (retail memory 0x4000)
	BOOL m_fAIEventSkip;		// no more event searches (retail +0x374)
	int m_iCoverID;			// NF_COVER_* of the event (retail m_iCustomEventID)
	BOOL m_fCoverStand;		// standing up from the crouch (retail memory 0x800)
	int m_iFirePhase;		// corner fire: 0 none, 1 step, 2 fire, 3 return
	int m_cCornerShots;
	float m_flNextCoverFire;	// retail m_flNextRangeAttack
	int m_iCoverIdleSeq;		// cover idle sequence playing (not saved)
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
	DEFINE_FIELD( CNightfireEnemy, m_iNFSpawnflags, FIELD_INTEGER ),
	DEFINE_FIELD( CNightfireEnemy, m_dwExcludeAIEvents, FIELD_INTEGER ),
	DEFINE_FIELD( CNightfireEnemy, m_iInitEventID, FIELD_INTEGER ),
	DEFINE_FIELD( CNightfireEnemy, m_hAIEvent, FIELD_EHANDLE ),
	DEFINE_FIELD( CNightfireEnemy, m_fAIEventDone, FIELD_BOOLEAN ),
	DEFINE_FIELD( CNightfireEnemy, m_fAIEventSkip, FIELD_BOOLEAN ),
	DEFINE_FIELD( CNightfireEnemy, m_iCoverID, FIELD_INTEGER ),
	DEFINE_FIELD( CNightfireEnemy, m_fCoverStand, FIELD_BOOLEAN ),
	DEFINE_FIELD( CNightfireEnemy, m_iFirePhase, FIELD_INTEGER ),
	DEFINE_FIELD( CNightfireEnemy, m_cCornerShots, FIELD_INTEGER ),
	DEFINE_FIELD( CNightfireEnemy, m_flNextCoverFire, FIELD_TIME ),
	DEFINE_FIELD( CNightfireEnemy, m_fNFCounted, FIELD_BOOLEAN ),
	DEFINE_FIELD( CNightfireEnemy, m_fNFDispatched, FIELD_BOOLEAN ),
	DEFINE_FIELD( CNightfireEnemy, m_hTaserWeapon, FIELD_EHANDLE ),
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
	else if( FStrEq( pkvd->szKeyName, "excludeaievents" ))
	{
		m_dwExcludeAIEvents = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "initeventid" ))
	{
		m_iInitEventID = atoi( pkvd->szValue );
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
	m_iNFSpawnflags = pev->spawnflags;
	pev->spawnflags &= NF_ENEMY_SPAWNFLAGS_HL;

	pev->solid = SOLID_SLIDEBOX;
	pev->movetype = MOVETYPE_STEP;
	m_bloodColor = BLOOD_COLOR_RED;
	pev->effects &= EF_FIXEDLIGHT;	// the map's fixedlight flag stays [assumed: retail keeps the key value]
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

	// at a cover event: its own idle / fire sequences
	const char *pszCover = InCover() ? CoverSequence( NewActivity ) : NULL;
	if( pszCover && LookupSequence( pszCover ) > ACTIVITY_NOT_AVAILABLE )
	{
		iSequence = LookupSequence( pszCover );
		if( NewActivity == ACT_IDLE )
			m_iCoverIdleSeq = iSequence;
		m_Activity = NewActivity;
		if( pev->sequence != iSequence || !m_fSequenceLoops )
			pev->frame = 0;
		pev->sequence = iSequence;
		ResetSequenceInfo();
		SetYawSpeed();
		return;
	}

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
	case ACT_COMBAT_IDLE:	// cover: stand up from the crouch
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
		iSequence = m_hTaserWeapon != 0 ? LookupSequence( "electrocution" ) : LookupActivity( ACT_FLINCH_CHEST );
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
	m_flNextCoverFire = gpGlobals->time + 0.5f;	// [assumed] retail m_flNextRangeAttack

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
	// MonsterThink replaces a finished ACT_IDLE sequence by the model's idle
	// (LookupActivity): pick the cover idle again
	if( InCover() && m_Activity == ACT_IDLE && pev->sequence != m_iCoverIdleSeq )
		SetActivity( ACT_IDLE );

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
	if( NF_DEBUG( NF_DBG_MONSTERS ))
		ALERT( at_console, "nf_debug: enemy '%s' takes %.1f damage (type 0x%x) from %s, health %.0f\n", STRING( pev->targetname ),
			flDamage, bitsDamageType, pevInflictor ? STRING( pevInflictor->classname ) : "-", pev->health );

	BOOL fWasAlive = !HasMemory( bits_MEMORY_KILLED );
	int ret = CHGrunt::TakeDamage( pevInflictor, pevAttacker, flDamage, bitsDamageType );

	// mission stats (retail CBaseCharacter::TakeDamage 0x42043C30): a kill by
	// club / shock / paralyze damage is a non-lethal takedown of the
	// player, any other kill a frag of the killer (CBondRules::MonsterKilled).
	// By the memory bit Killed sets (the death flag follows with the death
	// animation, and BecomeDead gives the corpse health again)
	if( fWasAlive && HasMemory( bits_MEMORY_KILLED ) && pevAttacker )
	{
		CBaseEntity *pAttacker = CBaseEntity::Instance( pevAttacker );
		CBasePlayer *pPlayer = ( pAttacker && pAttacker->IsPlayer( )) ? (CBasePlayer *)pAttacker : NULL;

		if( bitsDamageType & ( DMG_CLUB | DMG_SHOCK | DMG_PARALYZE ))
		{
			if( pPlayer && !m_fNFDispatched )
				pPlayer->m_iNFNonLethal++;
			m_fNFDispatched = TRUE;
		}
		else
		{
			pevAttacker->frags += 1;
			if( pPlayer )
				pPlayer->NFSendScoreInfo();
		}
		if( NF_DEBUG( NF_DBG_MONSTERS ))
			ALERT( at_console, "nf_debug: enemy '%s' %s by %s\n", STRING( pev->targetname ),
				( bitsDamageType & ( DMG_CLUB | DMG_SHOCK | DMG_PARALYZE )) ? "taken down" : "killed, a frag", STRING( pevAttacker->classname ));
	}
	return ret;
}

void CNightfireEnemy::NFCountEnemy( CBaseEntity *pPlayer )
{
	if( m_fNFCounted )
		return;

	m_fNFCounted = TRUE;
	( (CBasePlayer *)pPlayer )->m_iNFTotalEnemies++;
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
	if( NF_DEBUG( NF_DBG_MONSTERS ))
		ALERT( at_console, "nf_debug: enemy '%s' hit in hitgroup %d: %.1f damage before the hitgroup factor\n", STRING( pev->targetname ),
			ptr->iHitgroup, flDamage );
	CSquadMonster::TraceAttack( pevAttacker, flDamage, vecDir, ptr, bitsDamageType );
}

void CNightfireEnemy::Killed( entvars_t *pevAttacker, int iGib )
{
	TaserRelease( m_hTaserWeapon );
	// [assumed] an event not reached yet is free again (retail releases
	// only events whose sequence is done, in HandleCustomActivity)
	if( !m_fAIEventDone || m_iCoverID != NF_COVER_NONE )
		ReleaseAIEvent( FALSE );

	if( NF_DEBUG( NF_DBG_MONSTERS ))
		ALERT( at_console, "nf_debug: enemy '%s' killed%s at %.0f %.0f %.0f (gib %d)\n", STRING( pev->targetname ),
			HasMemory( bits_MEMORY_KILLED ) ? " again (corpse hit)" : "", pev->origin.x, pev->origin.y, pev->origin.z, iGib );

	// a blast on the corpse calls Killed again (CBaseMonster::DeadTakeDamage,
	// GIB_ALWAYS): fire the death target only on the first death
	if( !FStringNull( m_iszDeathTarget ) && !HasMemory( bits_MEMORY_KILLED ))
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

//=========================================================
// AI events
//=========================================================
Task_t tlNFAIEventPlay[] =
{
	{ TASK_NF_AIEVENT_PATH, (float)0 },
	{ TASK_WAIT_FOR_MOVEMENT, (float)0 },
	{ TASK_NF_AIEVENT_ARRIVE, (float)0 },
	{ TASK_FACE_IDEAL, (float)0 },
	{ TASK_NF_AIEVENT_PLAY, (float)0 },
};

Schedule_t slNFAIEventPlay[] =
{
	{
		tlNFAIEventPlay,
		ARRAYSIZE( tlNFAIEventPlay ),
		bits_COND_HEAVY_DAMAGE,		// retail interrupt mask 0x200
		0,
		"NFAIEventPlay"
	},
};

// retail interrupt mask 0x10130201 without SPECIAL1 ("cannot fire now")
#define NF_COVER_INTERRUPTS ( bits_COND_NO_AMMO_LOADED | bits_COND_HEAVY_DAMAGE | bits_COND_NEW_ENEMY | bits_COND_HEAR_SOUND | bits_COND_ENEMY_DEAD )

// retail 79 / 80 / 86: idle in cover for a moment (the wait: 2 s, vent 0.75 s, random)
Task_t tlNFCoverWait[] =
{
	{ TASK_SET_ACTIVITY, (float)ACT_IDLE },
	{ TASK_WAIT_RANDOM, (float)2 },
};

Schedule_t slNFCoverWait[] =
{
	{ tlNFCoverWait, ARRAYSIZE( tlNFCoverWait ), NF_COVER_INTERRUPTS, bits_SOUND_DANGER, "NFCoverWait" },
};

// retail 78: stand up 0.3 s, three shots standing, crouch, wait
Task_t tlNFDuckAttack[] =
{
	{ TASK_FACE_ENEMY, (float)0 },
	{ TASK_NF_COVER_STAND, (float)0.3 },
	{ TASK_RANGE_ATTACK1, (float)0 },
	{ TASK_RANGE_ATTACK1, (float)0 },
	{ TASK_RANGE_ATTACK1, (float)0 },
	{ TASK_NF_COVER_CROUCH, (float)0 },
	{ TASK_SET_ACTIVITY, (float)ACT_IDLE },
	{ TASK_WAIT_RANDOM, (float)3 },
};

Schedule_t slNFDuckAttack[] =
{
	{ tlNFDuckAttack, ARRAYSIZE( tlNFDuckAttack ), NF_COVER_INTERRUPTS, bits_SOUND_DANGER, "NFDuckAttack" },
};

// retail 74: corner fire (step, 1..ammo/3 shots, return), wait
Task_t tlNFWallAttack[] =
{
	{ TASK_NF_CORNER_FIRE, (float)0 },
	{ TASK_SET_ACTIVITY, (float)ACT_IDLE },
	{ TASK_WAIT_RANDOM, (float)3 },
};

Schedule_t slNFWallAttack[] =
{
	{ tlNFWallAttack, ARRAYSIZE( tlNFWallAttack ), NF_COVER_INTERRUPTS, bits_SOUND_DANGER, "NFWallAttack" },
};

// retail 87: one vent_shoot
Task_t tlNFDuctAttack[] =
{
	{ TASK_RANGE_ATTACK1, (float)0 },
};

Schedule_t slNFDuctAttack[] =
{
	{ tlNFDuctAttack, ARRAYSIZE( tlNFDuctAttack ), NF_COVER_INTERRUPTS, bits_SOUND_DANGER, "NFDuctAttack" },
};

Task_t tlNFTaser[] =
{
	{ TASK_STOP_MOVING, 0 },
	{ TASK_NF_TASER, 0 },
};

Schedule_t slNFTaser[] =
{
	{ tlNFTaser, ARRAYSIZE( tlNFTaser ), 0, 0, "NFTaser" },
};

void CNightfireEnemy::UpdateOnRemove( void )
{
	TaserRelease( m_hTaserWeapon );
	CHGrunt::UpdateOnRemove();
}

BOOL CNightfireEnemy::TaserHeld( CBaseEntity *weapon )
{
	return weapon && (CBaseEntity *)m_hTaserWeapon == weapon;
}

BOOL CNightfireEnemy::TaserAcquire( CBaseEntity *weapon )
{
	if( !weapon || !IsAlive() || HasMemory( bits_MEMORY_KILLED ) ||
		m_MonsterState == MONSTERSTATE_SCRIPT || ( m_hTaserWeapon != 0 && !TaserHeld( weapon )))
		return FALSE;
	if( TaserHeld( weapon ))
		return TRUE;
	ReleaseAIEvent( FALSE );
	m_hTaserWeapon = weapon;
	RouteClear();
	pev->velocity = g_vecZero;
	ChangeSchedule( slNFTaser );
	if( NF_DEBUG( NF_DBG_MONSTERS ))
		ALERT( at_console, "nf_debug: enemy '%s' taser held\n", STRING( pev->targetname ));
	return TRUE;
}

void CNightfireEnemy::TaserRelease( CBaseEntity *weapon )
{
	if( !TaserHeld( weapon ))
		return;
	m_hTaserWeapon = NULL;
	STOP_SOUND( edict(), CHAN_STATIC, "misc/electrocution.wav" );
	if( IsAlive() && !HasMemory( bits_MEMORY_KILLED ) && m_pSchedule == slNFTaser )
	{
		ClearSchedule();
		m_IdealActivity = ACT_IDLE;
	}
	if( NF_DEBUG( NF_DBG_MONSTERS ))
		ALERT( at_console, "nf_debug: enemy '%s' taser released\n", STRING( pev->targetname ));
}

BOOL NF_TaserAcquire( CBaseEntity *target, CBaseEntity *weapon )
{
	return target && FClassnameIs( target->pev, "enemy_generic" ) && ((CNightfireEnemy *)target)->TaserAcquire( weapon );
}

BOOL NF_TaserHeld( CBaseEntity *target, CBaseEntity *weapon )
{
	return target && FClassnameIs( target->pev, "enemy_generic" ) && ((CNightfireEnemy *)target)->TaserHeld( weapon );
}

void NF_TaserRelease( CBaseEntity *target, CBaseEntity *weapon )
{
	if( target && FClassnameIs( target->pev, "enemy_generic" ))
		((CNightfireEnemy *)target)->TaserRelease( weapon );
}

DEFINE_CUSTOM_SCHEDULES( CNightfireEnemy )
{
	slNFPatrol,
	slNFAIEventPlay,
	slNFCoverWait,
	slNFDuckAttack,
	slNFWallAttack,
	slNFDuctAttack,
	slNFTaser,
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

// retail CBaseCharacter::FindClosestAIEvent 0x4203BAB0: the nearest free
// event of the type within min(512, sight distance) and within its own
// radius; fNeedFacing: the player is in the event's view cone; !fSkipLOS:
// the event is hidden from the player. probability, anglerange and the
// cooldown are not checked.
CAIEvent *CNightfireEnemy::FindClosestAIEvent( int iType, BOOL fSkipLOS, BOOL fNeedFacing )
{
	if( iType < 1 || ( m_dwExcludeAIEvents & ( 1 << ( iType - 1 ))))
		return NULL;

	CBaseEntity *pPlayer = UTIL_PlayerByIndex( 1 );
	CAIEvent *pBest = NULL;
	float flBest = 9999.99f;
	CBaseEntity *pEntity = NULL;

	while(( pEntity = UTIL_FindEntityInSphere( pEntity, pev->origin, Q_min( 512.0f, m_flDistLook ))) != NULL )
	{
		if( !FClassnameIs( pEntity->pev, "info_aievent" ))
			continue;
		CAIEvent *pEvent = (CAIEvent *)pEntity;
		if( FBitSet( pEvent->pev->spawnflags, SF_NF_AIEVENT_NOT_ENEMIES ))
			continue;

		if( pPlayer )
		{
			if( fNeedFacing && !pEvent->InViewCone( pPlayer->pev->origin ))
				continue;
			if( !fSkipLOS )
			{
				TraceResult tr;
				UTIL_TraceLine( pEvent->pev->origin, pPlayer->pev->origin, ignore_monsters, pEvent->edict(), &tr );
				if( tr.flFraction == 1.0f )
					continue;
			}
		}

		if( pEvent->m_fLocked || pEvent->m_iEventType != iType )
			continue;

		float flDist = ( pEvent->pev->origin - pev->origin ).Length();
		if( pEvent->m_flRadius > 0 && flDist > pEvent->m_flRadius )
			continue;
		if( flDist < flBest )
		{
			flBest = flDist;
			pBest = pEvent;
		}
	}
	return pBest;
}

// lock the event and go there (retail: schedule 51)
BOOL CNightfireEnemy::ActivateAIEvent( CAIEvent *pEvent )
{
	if( !pEvent || !IsAlive() || m_hAIEvent != 0 || m_MonsterState == MONSTERSTATE_SCRIPT )
		return FALSE;

	pEvent->m_fLocked = TRUE;
	pEvent->m_iCounter = 0;
	m_hAIEvent = pEvent;
	m_fAIEventDone = FALSE;
	if( NF_DEBUG( NF_DBG_MONSTERS ))
		ALERT( at_console, "nf_debug: enemy '%s' goes to aievent type %d '%s' at %.0f %.0f %.0f (%.0f away)\n", STRING( pev->targetname ),
			pEvent->m_iEventType, STRING( pEvent->m_iszPlay ), pEvent->pev->origin.x, pEvent->pev->origin.y, pEvent->pev->origin.z,
			( pEvent->pev->origin - pev->origin ).Length());
	ChangeSchedule( GetScheduleOfType( SCHED_NF_AIEVENT_PLAY ));
	return TRUE;
}

// retail task 107: unlock the event; fSkip: no more event searches
void CNightfireEnemy::ReleaseAIEvent( BOOL fSkip )
{
	CAIEvent *pEvent = AIEvent();
	if( pEvent )
	{
		pEvent->m_fLocked = FALSE;
		if( NF_DEBUG( NF_DBG_MONSTERS ))
			ALERT( at_console, "nf_debug: enemy '%s' releases aievent type %d '%s'\n", STRING( pev->targetname ),
				pEvent->m_iEventType, STRING( pEvent->m_iszPlay ));
	}
	m_hAIEvent = NULL;
	m_fAIEventDone = FALSE;
	m_iCoverID = NF_COVER_NONE;
	m_fCoverStand = FALSE;
	m_iFirePhase = 0;
	if( fSkip )
		m_fAIEventSkip = TRUE;
}

// retail CGenericEnemy::SetCustomEvent 0x4205F090: types 5 / 6, the style by
// the sequence name ([assumed] by prefix: the maps also use l_corner_idle1 /
// r_corner_idle1)
BOOL CNightfireEnemy::SetCoverEvent( CAIEvent *pEvent )
{
	if( pEvent->m_iEventType != NF_AIEVENT_COVER && pEvent->m_iEventType != NF_AIEVENT_VENT )
		return FALSE;

	const char *pszPlay = STRING( pEvent->m_iszPlay );
	if( !strncmp( pszPlay, "l_corner_idle", 13 ))
		m_iCoverID = NF_COVER_LCORNER;
	else if( !strncmp( pszPlay, "crouching_wait", 14 ))
		m_iCoverID = NF_COVER_CROUCH;
	else if( !strncmp( pszPlay, "r_corner_idle", 13 ))
		m_iCoverID = NF_COVER_RCORNER;
	else if( !strncmp( pszPlay, "vent_peek", 9 ))
		m_iCoverID = NF_COVER_VENT;
	else
		m_iCoverID = NF_COVER_NONE;
	return TRUE;
}

// retail HandleCustomActivity 0x4205F170
const char *CNightfireEnemy::CoverSequence( Activity act )
{
	const nf_enemy_weapon_t *w = Weapon();
	const char *pszStanding = w && w->sniper ? "standing_sniper" : "standing_mgun";
	BOOL fLeft = m_iCoverID == NF_COVER_LCORNER;

	switch( act )
	{
	case ACT_IDLE:
	case ACT_IDLE_ANGRY:	// retail 1 and 47
		switch( m_iCoverID )
		{
		case NF_COVER_CROUCH:
			m_fCoverStand = FALSE;
			return RANDOM_LONG( 0, 4 ) ? "crouching_wait" : "crouching_wait2";
		case NF_COVER_LCORNER: return "l_corner_idle";
		case NF_COVER_RCORNER: return "r_corner_idle";
		case NF_COVER_VENT: return "vent_idle";
		}
		break;
	case ACT_RANGE_ATTACK1:
		switch( m_iCoverID )
		{
		case NF_COVER_CROUCH:
			m_fCoverStand = TRUE;
			return pszStanding;
		case NF_COVER_LCORNER:
		case NF_COVER_RCORNER:
			switch( m_iFirePhase )
			{
			case 1: return fLeft ? "l_corner_fire_step" : "r_corner_fire_step";
			case 2: return fLeft ? "l_corner_fire" : "r_corner_fire";
			case 3: return fLeft ? "l_corner_fire_return" : "r_corner_fire_return";
			}
			return w && w->sniper ? pszStanding : fLeft ? "l_corner_fire" : "r_corner_fire";
		case NF_COVER_VENT: return w && w->sniper ? pszStanding : "vent_shoot";
		}
		break;
	case ACT_RANGE_ATTACK2:
		switch( m_iCoverID )
		{
		case NF_COVER_CROUCH: return pszStanding;
		case NF_COVER_LCORNER: return "l_corner_grenade_throw";
		case NF_COVER_RCORNER: return "r_corner_grenade_throw";
		case NF_COVER_VENT: return "vent_grenade";
		}
		break;
	default:
		break;
	}
	return NULL;
}

// retail release: free the event, combat, Combat Face; a later new enemy may
// search again
Schedule_t *CNightfireEnemy::LeaveCover( void )
{
	if( NF_DEBUG( NF_DBG_MONSTERS ))
		ALERT( at_console, "nf_debug: enemy '%s' leaves cover %d\n", STRING( pev->targetname ), m_iCoverID );
	ReleaseAIEvent( FALSE );
	m_fAIEventSkip = FALSE;
	if( UTIL_PlayerByIndex( 1 ) == NULL )
	{
		SetState( MONSTERSTATE_IDLE );
		return CHGrunt::GetScheduleOfType( SCHED_IDLE_STAND );
	}
	m_MonsterState = m_IdealMonsterState = MONSTERSTATE_COMBAT;
	return CHGrunt::GetScheduleOfType( SCHED_COMBAT_FACE );
}

// retail CGenericEnemy::GetCustomSchedule 0x42065730 (ids 3/4/5/6)
Schedule_t *CNightfireEnemy::GetCoverSchedule( void )
{
	CBaseEntity *pPlayer = UTIL_PlayerByIndex( 1 );
	if( !pPlayer )
		return LeaveCover();

	float flTooClose = m_flDistLook / 4;
	if( m_iCoverID == NF_COVER_CROUCH || m_iCoverID == NF_COVER_VENT )
		flTooClose = Q_min( flTooClose, 128.0f );

	if( m_hEnemy == 0 )
	{
		// peek: crouch from the standing eye, corner 32 units to the side, vent 24 up
		Vector vecPeek = pev->origin;
		if( m_iCoverID == NF_COVER_CROUCH )
			vecPeek = vecPeek + Vector( 0, 0, 64 );
		else if( m_iCoverID == NF_COVER_VENT )
			vecPeek = vecPeek + Vector( 0, 0, 24 );
		else
		{
			UTIL_MakeVectors( pev->angles );
			vecPeek = vecPeek + pev->view_ofs + gpGlobals->v_right * ( m_iCoverID == NF_COVER_LCORNER ? 32 : -32 );
		}
		TraceResult tr;
		UTIL_TraceLine( vecPeek, pPlayer->EyePosition(), ignore_monsters, ENT( pev ), &tr );
		if( tr.flFraction < 1.0f )
			return slNFCoverWait;

		m_hEnemy = pPlayer;
		m_vecEnemyLKP = pPlayer->pev->origin;
		if(( pPlayer->pev->origin - pev->origin ).Length() < flTooClose )
			return LeaveCover();
	}

	if( HasConditions( bits_COND_NO_AMMO_LOADED ))
		return CHGrunt::GetScheduleOfType( SCHED_RELOAD );

	// cover is useless when the enemy sees him (crouch: at crouched height) or is close
	CBaseEntity *pEnemy = m_hEnemy;
	BOOL fVisible;
	if( m_iCoverID == NF_COVER_CROUCH )
	{
		TraceResult tr;
		UTIL_TraceLine( pev->origin + Vector( 0, 0, 36 ), pEnemy->EyePosition(), ignore_monsters, ENT( pev ), &tr );
		fVisible = tr.flFraction == 1.0f || tr.pHit == pEnemy->edict();
	}
	else
		fVisible = FVisible( pEnemy );
	if( fVisible || ( pEnemy->pev->origin - pev->origin ).Length() < flTooClose )
		return LeaveCover();

	if( gpGlobals->time < m_flNextCoverFire )
		return slNFCoverWait;
	switch( m_iCoverID )
	{
	case NF_COVER_CROUCH:
		return slNFDuckAttack;
	case NF_COVER_VENT:
		return slNFDuctAttack;
	default:
		m_iFirePhase = 0;
		return m_cAmmoLoaded < 6 ? CHGrunt::GetScheduleOfType( SCHED_RELOAD ) : slNFWallAttack;
	}
}

// retail CBaseCharacter::ActivateAIEvent 0x42037C50 (vtable +0xF8): only
// alarms; the enemy override adds type 6 [not yet]
BOOL NF_EnemyActivateAIEvent( CBaseEntity *pEntity, CAIEvent *pEvent, CBaseEntity *pActivator )
{
	if( !FClassnameIs( pEntity->pev, "enemy_generic" ))
		return FALSE;
	CNightfireEnemy *pEnemy = (CNightfireEnemy *)pEntity;
	if( pEvent->m_iEventType == NF_AIEVENT_ALARM )
		return pEnemy->ActivateAIEvent( pEvent );

	// retail CGenericEnemy 0x42064DF0: a vent for an enemy that already has
	// an enemy [details assumed]
	if( pEvent->m_iEventType == NF_AIEVENT_VENT && pEnemy->m_hEnemy != 0 && !pEnemy->AIEvent() &&
		pEnemy->SetCoverEvent( pEvent ) && pEnemy->ActivateAIEvent( pEvent ))
		return TRUE;
	return FALSE;
}

Schedule_t *CNightfireEnemy::GetSchedule( void )
{
	if( IsAlive() && m_hTaserWeapon != 0 )
		return slNFTaser;
	if( IsAlive() && InCover())
	{
		Schedule_t *pCover = GetCoverSchedule();
		if( NF_DEBUG( NF_DBG_MONSTERS ))
			ALERT( at_console, "nf_debug: enemy '%s' cover %d: %s (ammo %d)\n", STRING( pev->targetname ), m_iCoverID,
				pCover ? pCover->pName : "-", m_cAmmoLoaded );
		return pCover;
	}

	if( m_hAIEvent != 0 && !m_fAIEventDone )
	{
		// a state change (e.g. alert -> combat) on the way: keep going
		if( m_pSchedule == slNFAIEventPlay && !HasConditions( bits_COND_HEAVY_DAMAGE | bits_COND_TASK_FAILED ))
			return m_pSchedule;
		// interrupted before the sequence ended (heavy damage): free the
		// event and fight [assumed]
		ReleaseAIEvent( TRUE );
	}

	// retail GetSchedule 0x420634B0: alerted by a sound, or a new enemy in
	// combat: run to the nearest alarm (cover events: [not yet])
	if( IsAlive() && m_hAIEvent == 0 && !m_fAIEventSkip &&
		(( m_MonsterState == MONSTERSTATE_ALERT && HasConditions( bits_COND_HEAR_SOUND ) && !( m_iNFSpawnflags & 0x100000 )) ||
		( m_MonsterState == MONSTERSTATE_COMBAT && HasConditions( bits_COND_NEW_ENEMY ))))
	{
		CAIEvent *pEvent = FindClosestAIEvent( NF_AIEVENT_ALARM, TRUE, FALSE );
		if( NF_DEBUG( NF_DBG_MONSTERS ))
			ALERT( at_console, "nf_debug: enemy '%s' (state %d) looks for an alarm within %.0f at %.0f %.0f %.0f: %s\n", STRING( pev->targetname ),
				m_MonsterState, Q_min( 512.0f, m_flDistLook ), pev->origin.x, pev->origin.y, pev->origin.z, pEvent ? "found" : "none" );
		if( pEvent && ActivateAIEvent( pEvent ))
			return m_pSchedule;

		// an armed enemy with a new enemy: cover facing the player, hidden from him
		if( m_MonsterState == MONSTERSTATE_COMBAT && Weapon())
		{
			pEvent = FindClosestAIEvent( NF_AIEVENT_COVER, FALSE, TRUE );
			if( pEvent && SetCoverEvent( pEvent ) && ActivateAIEvent( pEvent ))
				return m_pSchedule;
			m_iCoverID = NF_COVER_NONE;
		}
	}

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
	if( IsAlive() && m_hTaserWeapon != 0 )
		return slNFTaser;
	if( Type == SCHED_NF_PATROL )
		return slNFPatrol;
	if( Type == SCHED_NF_AIEVENT_PLAY )
		return slNFAIEventPlay;

	// a cover schedule failed (e.g. no line of fire): leave cover
	if( Type == SCHED_FAIL && InCover())
		return LeaveCover();

	// retail 56 "GotoAIEventFailed": release the event, search no more
	if( Type == SCHED_FAIL && m_pSchedule == slNFAIEventPlay )
	{
		if( NF_DEBUG( NF_DBG_MONSTERS ))
			ALERT( at_console, "nf_debug: enemy '%s' failed aievent task %d (%.0f away)\n", STRING( pev->targetname ),
				m_iScheduleIndex, AIEvent() ? ( AIEvent()->pev->origin - pev->origin ).Length2D() : 0.0f );
		ReleaseAIEvent( TRUE );
	}

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
	case TASK_NF_TASER:
		m_IdealActivity = ACT_BIG_FLINCH;
		SetActivity( ACT_BIG_FLINCH );
		EMIT_SOUND( edict(), CHAN_STATIC, "misc/electrocution.wav", 1, ATTN_NORM );
		break;
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
	case TASK_NF_AIEVENT_PATH:
	{
		CAIEvent *pEvent = AIEvent();
		if( !pEvent )
		{
			TaskFail();
			break;
		}
		if(( pEvent->pev->origin - pev->origin ).Length2D() < 16 || MoveToLocation( ACT_RUN, 2, pEvent->pev->origin ))
			TaskComplete();
		else
			TaskFail();
		break;
	}
	case TASK_NF_AIEVENT_ARRIVE:
	{
		CAIEvent *pEvent = AIEvent();
		if( !pEvent )
		{
			TaskFail();
			break;
		}
		if( NF_DEBUG( NF_DBG_MONSTERS ))
			ALERT( at_console, "nf_debug: enemy '%s' at aievent type %d (%.0f away)\n", STRING( pev->targetname ),
				pEvent->m_iEventType, ( pEvent->pev->origin - pev->origin ).Length2D());
		// retail IsAtAIEvent 0x420375D0: types 3/4 use the usetarget entity
		if(( pEvent->m_iEventType == NF_AIEVENT_BOSS || pEvent->m_iEventType == NF_AIEVENT_ALARM ) && !FStringNull( pEvent->m_iszUseTarget ))
		{
			CBaseEntity *pTarget = UTIL_FindEntityByTargetname( NULL, STRING( pEvent->m_iszUseTarget ));
			if( pTarget )
				pTarget->Use( this, this, USE_ON, 0 );
		}
		pev->ideal_yaw = FBitSet( pEvent->pev->spawnflags, SF_NF_AIEVENT_FACE ) ? pEvent->pev->angles.y : pev->angles.y;
		TaskComplete();
		break;
	}
	case TASK_NF_COVER_STAND:
		m_fCoverStand = TRUE;
		m_IdealActivity = ACT_COMBAT_IDLE;	// retail 62, not remapped: stand up
		m_flWaitFinished = gpGlobals->time + pTask->flData;
		break;
	case TASK_NF_COVER_CROUCH:
		m_fCoverStand = FALSE;
		m_IdealActivity = ACT_IDLE;
		TaskComplete();
		break;
	case TASK_NF_CORNER_FIRE:
		m_iFirePhase = 1;
		m_IdealActivity = ACT_RANGE_ATTACK1;
		SetActivity( ACT_RANGE_ATTACK1 );
		break;
	case TASK_NF_AIEVENT_PLAY:
	{
		CAIEvent *pEvent = AIEvent();
		int iSequence = pEvent ? LookupSequence( STRING( pEvent->m_iszPlay )) : ACTIVITY_NOT_AVAILABLE;
		if( iSequence <= ACTIVITY_NOT_AVAILABLE )
		{
			if( pEvent )
			{
				ALERT( at_aiconsole, "enemy '%s' (%s) has no sequence '%s'\n", STRING( pev->targetname ), STRING( pev->model ), STRING( pEvent->m_iszPlay ));
				m_fAIEventDone = TRUE;
				pEvent->SequenceDone( this );
			}
			TaskComplete();
			break;
		}
		// a sequence without an activity: MaintainSchedule must not replace it
		m_IdealActivity = m_Activity = ACT_RESET;
		pev->sequence = iSequence;
		pev->frame = 0;
		ResetSequenceInfo();
		break;
	}
	default:
		CHGrunt::StartTask( pTask );
		break;
	}
}

void CNightfireEnemy::RunTask( Task_t *pTask )
{
	switch( pTask->iTask )
	{
	case TASK_NF_TASER:
		if( m_hTaserWeapon == 0 )
		{
			STOP_SOUND( edict(), CHAN_STATIC, "misc/electrocution.wav" );
			TaskComplete();
		}
		else
			pev->velocity = g_vecZero;
		break;
	case TASK_NF_PATROL_WAIT:
		if( gpGlobals->time >= m_flWaitFinished )
			TaskComplete();
		break;
	case TASK_NF_COVER_STAND:
		if( gpGlobals->time >= m_flWaitFinished )
			TaskComplete();
		break;
	case TASK_NF_CORNER_FIRE:
		// retail 134: the next phase when a sequence ends; the shots come from
		// the fire sequence's animation events
		if( !m_fSequenceFinished )
			break;
		if( m_iFirePhase == 1 )
		{
			m_cCornerShots = RANDOM_LONG( 1, Q_max( 1, m_cAmmoLoaded / 3 ));
			m_iFirePhase = 2;
		}
		else if( m_iFirePhase == 2 )
		{
			if( --m_cCornerShots <= 0 || m_cAmmoLoaded <= 0 )
				m_iFirePhase = 3;
		}
		else
		{
			m_iFirePhase = 0;
			TaskComplete();
			break;
		}
		SetActivity( ACT_RANGE_ATTACK1 );
		break;
	case TASK_NF_AIEVENT_PLAY:
	{
		CAIEvent *pEvent = AIEvent();
		if( !pEvent )
		{
			TaskFail();
			break;
		}
		if( !m_fSequenceFinished )
			break;
		// retail CAIEvent::SequenceDone 0x42014A70: type 2 repeats animcount times
		if( pEvent->m_iEventType == NF_AIEVENT_REPEAT && ++pEvent->m_iCounter < pEvent->m_iAnimCount )
		{
			pev->frame = 0;
			ResetSequenceInfo();
			break;
		}
		m_fAIEventDone = TRUE;
		pEvent->SequenceDone( this );
		if( NF_DEBUG( NF_DBG_MONSTERS ) && m_iCoverID != NF_COVER_NONE )
			ALERT( at_console, "nf_debug: enemy '%s' in cover %d\n", STRING( pev->targetname ), m_iCoverID );
		TaskComplete();
		break;
	}
	default:
		CHGrunt::RunTask( pTask );
		break;
	}
}
