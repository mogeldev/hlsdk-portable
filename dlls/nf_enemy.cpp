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
  Spawnflags       capitalised; the low bits match the HLSDK monster flags
                   (4 hit monsterclip, 32 squad leader, 128 wait for script),
                   the high bits (0x4000, 0x400000, 0x2000000) are unknown
                   and dropped
  deathtarget      fired when the enemy dies
  primary_weapon   weapon id, see g_nfEnemyWeapons (from the retail game.dll:
                   the precache switch of the enemy class, case = id - 1)
  gun_index        value of the model's "weapons" bodygroup (0 = no gun)
  num_grenades     hand grenades to throw
  char_name        voice directory: sound/<char_name>/pain|die|combatN.wav
  voice_pitch      voice pitch (100 on m5)
  TriggerTarget / TriggerCondition: parsed by CBaseMonster; the Nightfire
                   condition values (e.g. 4362) are not HLSDK conditions

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

// HLSDK monster spawnflags that Nightfire maps use with the same meaning
#define NF_ENEMY_SPAWNFLAGS_HL	0x3FF
#define NF_ENEMY_MAX_VOICE	9	// the retail code formats %s/pain%d.wav up to 9

//=========================================================
// skill.cfg cvars of the Nightfire characters
//=========================================================
static const char *const g_nfSkillCvars[] =
{
	"sk_enemy_health", "sk_enemy_kick", "sk_enemy_pellets", "sk_enemy_gspeed",
	"sk_enemy_rocket", "sk_commando_bullet", "sk_mp9_bullet", "sk_pdw90_bullet",
	"sk_kowloon_bullet", "sk_buckshot_bullet", "sk_sniper_bullet",
	"sk_raptor_bullet", "sk_minigun_bullet", "sk_alerted_bullet", "sk_laser_bolt",
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

static float NF_SkillValue( const char *base )
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
	{ "weapons/mp9_fire%d.wav", 3, "weapons/mp9_reload_empty.wav", "sk_mp9_bullet", 30 },		// 3 MP9
	{ "weapons/mp9_fire_sil%d.wav", 2, "weapons/mp9_reload_empty.wav", "sk_mp9_bullet", 30 },		// 4 MP9 silenced
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
	void TraceAttack( entvars_t *pevAttacker, float flDamage, Vector vecDir, TraceResult *ptr, int bitsDamageType );
	void Killed( entvars_t *pevAttacker, int iGib );
	BOOL FOkToSpeak( void ) { return FALSE; }	// no HG_* sentence groups in Nightfire

	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];

private:
	const nf_enemy_weapon_t *Weapon( void );
	int FindBodygroup( const char *name );
	void NFShoot( void );
	void PlayVoice( const char *kind, int count, float attn );
	int CountVoice( const char *kind );

	float m_flSightDist;
	string_t m_iszDeathTarget;
	string_t m_iszCharName;
	int m_iWeapon;
	int m_iGunIndex;
	int m_cGrenades;
	int m_cPain, m_cDie, m_cCombat;
	float m_flNextCombatTalk;
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
	else if( FStrEq( pkvd->szKeyName, "Spawnflags" ))
	{
		// capitalised in Nightfire maps; the engine only parses "spawnflags"
		pev->spawnflags = atoi( pkvd->szValue ) & NF_ENEMY_SPAWNFLAGS_HL;
		pkvd->fHandled = TRUE;
	}
	else
		CHGrunt::KeyValue( pkvd );
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
		PRECACHE_MODEL( "models/w_frag_grenade.mdl" );
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

	MonsterInit();

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
	case 13:	// throw a grenade back
	case 14:	// kick a grenade away
		// [not yet] Nightfire weapon pickups and grenade return do not exist yet
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
