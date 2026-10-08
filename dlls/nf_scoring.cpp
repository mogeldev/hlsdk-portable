/*
nf_scoring.cpp - James Bond 007: Nightfire (PC) Bond moments, secrets and the
mission stats

trigger_bondmoment / trigger_bondsecret
	Retail CBondMoment / CBondSecret (game.dll, base CBaseScoring; ScoreUse
	0x420bb580 / 0x420bb670, project docs/retail/scoring.md). Point
	entities fired by the map. Key "score_sound" (KeyValue 0x420bb370).
	Use: the activator if it is a player, else player 1; the player's Bond
	moment (or secret) count goes up, the sound plays at the player
	(CHAN_STATIC, vol 1, attn 1.25; the secret always plays
	common/secret.wav, its score_sound is ignored), "message" is shown as
	a HUD text if set (no map sets it), a Bond moment also sends
	"ShowStinger" (the client shows the 007 logo for 5 s). Removed in
	single player, so each fires once.
	Then the score update ("ScoreInfoS").

env_scoring
	Retail CEnvScoring (KeyValue 0x420bb730, empty Spawn): the level
	totals "bondmoments" / "bondsecrets" in the first map of each mission
	(and m6_escape07). The player's InitHUD (retail UpdateClientData
	0x420A9540) copies them into the stats.

Mission stats (retail CBasePlayer +0xBFC..+0xC20, all saved): shots fired
(one per FireBullets call; a shot that only hits something else that takes
damage, e.g. a breakable, is taken back), shots that hit a monster, enemies
(each enemy_generic once, counted at the first player update after a map
load; retail helicopters count 5, not ported), non-lethal takedowns (an
enemy killed by club / shock / paralyze damage; any other kill is a frag of
the killer), damage taken, Bond moments / secrets and their totals, mission
start (time + 1.1). Reset by the player's Spawn in single player and on the
next map after a changelevel that ends the mission (trigger_changelevel
spawnflag 8, dlls/triggers.cpp; the engine shows the scores) or a map whose
worldspawn has "newunit" (m2_airfield01, m4_infiltrate01, m6_escape01,
m7_island01, m9_space01). "ScoreInfoS" (MSG_ONE, after InitHUD,
every shot, kill, damage, moment and secret): byte entindex, shorts frags,
deaths, enemies, non-lethal, shots, hits, damage taken, favourite weapon
(retail: the item with the most shots; here always -1), bytes moments,
total moments, secrets, total secrets, float mission time (retail
WRITE_COORD is a raw float; written as a long). The client keeps the last
one for the mission score screen (cl_dll/nf_hud.cpp, mainui).
*/

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "player.h"
#include "saverestore.h"
#include "gamerules.h"
#include "nf_debug.h"

extern int gmsgNFShowStinger;

class CNFBaseScoring : public CPointEntity
{
public:
	void Spawn( void );
	void KeyValue( KeyValueData *pkvd );
	void EXPORT ScoreUse( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );

	virtual BOOL IsSecret( void ) { return FALSE; }

	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];

	string_t m_iszScoreSound;
};

TYPEDESCRIPTION CNFBaseScoring::m_SaveData[] =
{
	DEFINE_FIELD( CNFBaseScoring, m_iszScoreSound, FIELD_STRING ),
};

IMPLEMENT_SAVERESTORE( CNFBaseScoring, CPointEntity )

void CNFBaseScoring::Spawn( void )
{
	Precache();
	CPointEntity::Spawn();
	SetUse( &CNFBaseScoring::ScoreUse );
}

void CNFBaseScoring::KeyValue( KeyValueData *pkvd )
{
	if( FStrEq( pkvd->szKeyName, "score_sound" ))
	{
		m_iszScoreSound = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else
		CPointEntity::KeyValue( pkvd );
}

void CNFBaseScoring::ScoreUse( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	CBaseEntity *pEntity = ( pActivator && pActivator->IsPlayer( )) ? pActivator : UTIL_PlayerByIndex( 1 );
	if( !pEntity )
		return;

	CBasePlayer *pPlayer = (CBasePlayer *)pEntity;
	const char *sound;

	if( IsSecret( ))
	{
		pPlayer->m_iNFSecrets++;
		sound = "common/secret.wav";
	}
	else
	{
		pPlayer->m_iNFBondMoments++;
		sound = m_iszScoreSound ? STRING( m_iszScoreSound ) : NULL;
	}

	if( NF_DEBUG( NF_DBG_TRIGGERS ))
		ALERT( at_console, "nf_debug: %s %s: moments %d, secrets %d\n", STRING( pev->classname ), STRING( pev->targetname ),
			pPlayer->m_iNFBondMoments, pPlayer->m_iNFSecrets );

	if( sound && *sound )
		EMIT_SOUND_DYN( pPlayer->edict(), CHAN_STATIC, sound, 1.0f, 1.25f, 0, PITCH_NORM );

	if( pev->message && *STRING( pev->message ))
		UTIL_ShowMessage( STRING( pev->message ), pPlayer );

	if( !IsSecret( ))
	{
		MESSAGE_BEGIN( MSG_ONE, gmsgNFShowStinger, NULL, pPlayer->edict( ));
		MESSAGE_END();
	}

	pPlayer->NFSendScoreInfo();

	if( !g_pGameRules->IsMultiplayer( ))
		UTIL_Remove( this );
}

class CNFBondMoment : public CNFBaseScoring
{
public:
	void Precache( void )
	{
		if( m_iszScoreSound && *STRING( m_iszScoreSound ))
			PRECACHE_SOUND( STRING( m_iszScoreSound ));
	}
};

class CNFBondSecret : public CNFBaseScoring
{
public:
	void Precache( void ) { PRECACHE_SOUND( "common/secret.wav" ); }
	BOOL IsSecret( void ) { return TRUE; }
};

LINK_ENTITY_TO_CLASS( trigger_bondmoment, CNFBondMoment )
LINK_ENTITY_TO_CLASS( trigger_bondsecret, CNFBondSecret )

//=========================================================
// env_scoring: level totals
//=========================================================
class CNFEnvScoring : public CPointEntity
{
public:
	void Spawn( void ) { }
	void KeyValue( KeyValueData *pkvd );

	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];

	int m_iBondMoments;
	int m_iBondSecrets;
};

TYPEDESCRIPTION CNFEnvScoring::m_SaveData[] =
{
	DEFINE_FIELD( CNFEnvScoring, m_iBondMoments, FIELD_INTEGER ),
	DEFINE_FIELD( CNFEnvScoring, m_iBondSecrets, FIELD_INTEGER ),
};

IMPLEMENT_SAVERESTORE( CNFEnvScoring, CPointEntity )

void CNFEnvScoring::KeyValue( KeyValueData *pkvd )
{
	if( FStrEq( pkvd->szKeyName, "bondmoments" ))
	{
		m_iBondMoments = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "bondsecrets" ))
	{
		m_iBondSecrets = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else
		CPointEntity::KeyValue( pkvd );
}

LINK_ENTITY_TO_CLASS( env_scoring, CNFEnvScoring )

//=========================================================
// mission stats
//=========================================================
BOOL g_fNFNewUnit;
BOOL g_fNFCountEnemies;

void CBasePlayer::NFResetStats( void )
{
	m_iNFShotsTaken = m_iNFShotsHit = 0;
	m_iNFTotalEnemies = m_iNFNonLethal = 0;
	m_iNFDamageTaken = 0;
	m_iNFTotalMoments = m_iNFTotalSecrets = 0;
	m_iNFBondMoments = m_iNFSecrets = 0;
	pev->frags = 0;
	m_flNFMissionStart = gpGlobals->time + 1.1f;

	if( NF_DEBUG( NF_DBG_TRIGGERS ))
		ALERT( at_console, "nf_debug: mission stats reset\n" );
}

void CBasePlayer::NFSendScoreInfo( void )
{
	union { float f; int i; } time;

	if( !gmsgNFScoreInfoS || g_pGameRules->IsMultiplayer( ))
		return;

	time.f = gpGlobals->time - m_flNFMissionStart;

	MESSAGE_BEGIN( MSG_ONE, gmsgNFScoreInfoS, NULL, edict( ));
		WRITE_BYTE( entindex( ));
		WRITE_SHORT( (int)pev->frags );
		WRITE_SHORT( m_iDeaths );
		WRITE_SHORT( m_iNFTotalEnemies );
		WRITE_SHORT( m_iNFNonLethal );
		WRITE_SHORT( m_iNFShotsTaken );
		WRITE_SHORT( m_iNFShotsHit );
		WRITE_SHORT( m_iNFDamageTaken );
		WRITE_SHORT( -1 );	// favourite weapon, not ported
		WRITE_BYTE( m_iNFBondMoments );
		WRITE_BYTE( m_iNFTotalMoments );
		WRITE_BYTE( m_iNFSecrets );
		WRITE_BYTE( m_iNFTotalSecrets );
		WRITE_LONG( time.i );
	MESSAGE_END();
}

void CBasePlayer::NFCountShot( int iHit )
{
	// retail FireBullets 0x42042E91 / 0x420436B5 / 0x420436BD
	if( iHit != 2 )
		m_iNFShotsTaken++;
	if( iHit == 1 )
		m_iNFShotsHit++;
	NFSendScoreInfo();
}

void NF_ScoringCountEnemies( CBasePlayer *pPlayer )
{
	g_fNFCountEnemies = FALSE;

	for( int i = 1; i < gpGlobals->maxEntities; i++ )
	{
		edict_t *pEdict = INDEXENT( i );
		CBaseEntity *pEntity;

		if( !pEdict || pEdict->free )
			continue;
		pEntity = CBaseEntity::Instance( pEdict );
		if( pEntity && pEntity->MyMonsterPointer( ))
			pEntity->MyMonsterPointer()->NFCountEnemy( pPlayer );
	}

	if( NF_DEBUG( NF_DBG_TRIGGERS ))
		ALERT( at_console, "nf_debug: mission stats: %d enemies\n",pPlayer->m_iNFTotalEnemies );
}

void NF_ScoringInitHUD( CBasePlayer *pPlayer )
{
	if( g_pGameRules->IsMultiplayer( ))
		return;

	CNFEnvScoring *pScoring = (CNFEnvScoring *)UTIL_FindEntityByClassname( NULL, "env_scoring" );

	if( pScoring )
	{
		pPlayer->m_iNFTotalMoments = pScoring->m_iBondMoments;
		pPlayer->m_iNFTotalSecrets = pScoring->m_iBondSecrets;
	}

	if( NF_DEBUG( NF_DBG_TRIGGERS ))
		ALERT( at_console, "nf_debug: mission stats: moments %d / %d, secrets %d / %d, enemies %d\n", pPlayer->m_iNFBondMoments,
			pPlayer->m_iNFTotalMoments, pPlayer->m_iNFSecrets, pPlayer->m_iNFTotalSecrets, pPlayer->m_iNFTotalEnemies );

	pPlayer->NFSendScoreInfo();
}
