/*
nf_scoring.cpp - James Bond 007: Nightfire (PC) Bond moments and secrets

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
	Not ported: the score update ("ScoreInfoS", stats for the end of
	mission screen of env_scoring).
*/

#include "extdll.h"
#include "util.h"
#include "cbase.h"
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
