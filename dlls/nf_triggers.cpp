/*
nf_triggers.cpp - James Bond 007: Nightfire (PC) HUD and mission triggers

trigger_hudmessage
	Retail CTriggerHudMessage (game.dll, Use 0x42011010, KeyValue
	0x42010fa0). "message" is not text but the name of a title in
	maps/<map>.tit (loaded by the engine, see CL_InitMapTitles), shown by
	the client. Keys: "duration" (seconds, atoi in retail), "skilllevel"
	(shown only while the skill cvar is <= it; 0 = always). Spawnflags:
	1 = show the timed message box for "duration" seconds, 2 = do not add
	it to the hint section of the objectives panel (default: hint section
	only, with a beep). The message goes to everybody; without a player
	the retail game warns and drops it.

trigger_togglehud
	Retail CToggleHud::HudToggleUse (0x420a4f20) sends "ToggleHud" to all
	clients: USE_OFF hides the HUD panels, USE_ON shows them, anything else
	flips them; the activator does not matter. Here the state is kept per
	player in m_iHideHUD (HIDEHUD_ALL) so it survives save/restore.

trigger_playmovie
	Retail CTriggerPlayMovie::Use (0x42011df0): "message" = movie name
	(movies/<name>[.avi]); sends "PlayMovie" with the name to player 1, the
	activator does not matter. The client hands it to the engine
	(nf_playmovie), which plays it full-screen while the game waits.

trigger_endgame
	Retail CTriggerEndGame (Use 0x42010900, KeyValue 0x420108b0, Precache
	0x420108a0). Key "status": 0 (default) mission failed, 1 success
	(m3_japan01, never fired), 2 game won (m9_space01 "endgame"). Status 1
	and 2 play common/stinger.wav at the player (ambient, vol 1, attn
	1.25); 2 also sets the retail cvars sv_newunit / sv_iamdone (not in
	this port) and sends the engine command "CL_GameSuccess" (leave the
	game, m9_outro). Status 0 (bond_death.wav, 5 s fade to black, a client
	message, game rules timer +10 s; gamerules vtable slot 29, 0x420c2900,
	then cd track 3) is not implemented yet: only the nf_debug line.
*/

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "skill.h"
#include "saverestore.h"
#include "nf_debug.h"

#define SF_HUDMESSAGE_TIMED	1
#define SF_HUDMESSAGE_NOHINT	2

class CTriggerHudMessage : public CPointEntity
{
public:
	void KeyValue( KeyValueData *pkvd );
	void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );

	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];

	float m_flDuration;
	int m_iSkillLevel;
};

LINK_ENTITY_TO_CLASS( trigger_hudmessage, CTriggerHudMessage )

TYPEDESCRIPTION CTriggerHudMessage::m_SaveData[] =
{
	DEFINE_FIELD( CTriggerHudMessage, m_flDuration, FIELD_FLOAT ),
	DEFINE_FIELD( CTriggerHudMessage, m_iSkillLevel, FIELD_INTEGER ),
};

IMPLEMENT_SAVERESTORE( CTriggerHudMessage, CPointEntity )

void CTriggerHudMessage::KeyValue( KeyValueData *pkvd )
{
	if( FStrEq( pkvd->szKeyName, "duration" ))
	{
		// retail: atoi; kept as float so "1.5" (4 map entities) is not cut to 1
		m_flDuration = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "skilllevel" ))
	{
		m_iSkillLevel = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else
		CPointEntity::KeyValue( pkvd );
}

void CTriggerHudMessage::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	if( m_iSkillLevel > 0 && gSkillData.iSkillLevel > m_iSkillLevel )
	{
		if( NF_DEBUG( NF_DBG_TRIGGERS ))
			ALERT( at_console, "nf_debug: hudmessage %s skipped (skill %d > %d)\n",
				STRING( pev->targetname ), gSkillData.iSkillLevel, m_iSkillLevel );
		return;
	}

	if( FStringNull( pev->message ))
		return;

	CBaseEntity *pPlayer = ( pActivator && pActivator->IsPlayer( )) ? pActivator : UTIL_PlayerByIndex( 1 );
	if( !pPlayer )
	{
		ALERT( at_console, "trigger_hudmessage %s used when player not initialized!\n", STRING( pev->targetname ));
		return;
	}

	int timed = ( pev->spawnflags & SF_HUDMESSAGE_TIMED ) ? 1 : 0;
	int hint = ( pev->spawnflags & SF_HUDMESSAGE_NOHINT ) ? 0 : 1;
	int tenths = (int)( m_flDuration * 10.0f + 0.5f );
	if( tenths < 0 )
		tenths = 0;
	else if( tenths > 255 )
		tenths = 255;

	MESSAGE_BEGIN( MSG_ALL, gmsgNFHudMsg );
		WRITE_STRING( STRING( pev->message ));
		WRITE_BYTE( timed );
		WRITE_BYTE( hint );
		WRITE_BYTE( tenths );	// tenths of a second (retail: whole seconds)
	MESSAGE_END();

	if( NF_DEBUG( NF_DBG_TRIGGERS ))
		ALERT( at_console, "nf_debug: hudmessage %s \"%s\" timed %d hint %d duration %.1f\n",
			STRING( pev->targetname ), STRING( pev->message ), timed, hint, tenths / 10.0f );
}

class CTriggerToggleHud : public CPointEntity
{
public:
	void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );
};

LINK_ENTITY_TO_CLASS( trigger_togglehud, CTriggerToggleHud )

void CTriggerToggleHud::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	for( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *pPlayer = (CBasePlayer *)UTIL_PlayerByIndex( i );
		if( !pPlayer )
			continue;

		BOOL hide;
		if( useType == USE_OFF )
			hide = TRUE;
		else if( useType == USE_ON )
			hide = FALSE;
		else
			hide = !( pPlayer->m_iHideHUD & HIDEHUD_ALL );

		if( hide )
			pPlayer->m_iHideHUD |= HIDEHUD_ALL;
		else
			pPlayer->m_iHideHUD &= ~HIDEHUD_ALL;

		if( NF_DEBUG( NF_DBG_TRIGGERS ))
			ALERT( at_console, "nf_debug: togglehud %s use %d -> hud %s\n",
				STRING( pev->targetname ), (int)useType, hide ? "hidden" : "shown" );
	}
}

class CTriggerPlayMovie : public CPointEntity
{
public:
	void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );
};

LINK_ENTITY_TO_CLASS( trigger_playmovie, CTriggerPlayMovie )

void CTriggerPlayMovie::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	if( FStringNull( pev->message ))
		return;

	CBaseEntity *pPlayer = UTIL_PlayerByIndex( 1 );
	if( !pPlayer )
		return;

	MESSAGE_BEGIN( MSG_ONE, gmsgNFPlayMovie, NULL, pPlayer->pev );
		WRITE_STRING( STRING( pev->message ));
	MESSAGE_END();

	if( NF_DEBUG( NF_DBG_TRIGGERS ))
		ALERT( at_console, "nf_debug: playmovie %s \"%s\"\n", STRING( pev->targetname ), STRING( pev->message ));
}

#define NF_ENDGAME_FAILED	0
#define NF_ENDGAME_SUCCESS	1
#define NF_ENDGAME_WON		2

class CTriggerEndGame : public CPointEntity
{
public:
	void Spawn( void );
	void Precache( void );
	void KeyValue( KeyValueData *pkvd );
	void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );

	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];

	int m_iStatus;
};

LINK_ENTITY_TO_CLASS( trigger_endgame, CTriggerEndGame )

TYPEDESCRIPTION CTriggerEndGame::m_SaveData[] =
{
	DEFINE_FIELD( CTriggerEndGame, m_iStatus, FIELD_INTEGER ),
};

IMPLEMENT_SAVERESTORE( CTriggerEndGame, CPointEntity )

void CTriggerEndGame::Spawn( void )
{
	Precache();
	CPointEntity::Spawn();
}

void CTriggerEndGame::Precache( void )
{
	PRECACHE_SOUND( "common/stinger.wav" );
}

void CTriggerEndGame::KeyValue( KeyValueData *pkvd )
{
	if( FStrEq( pkvd->szKeyName, "status" ))
	{
		m_iStatus = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else
		CPointEntity::KeyValue( pkvd );
}

void CTriggerEndGame::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	// retail: the activator if it is a player, else player 1
	CBaseEntity *pPlayer = ( pActivator && pActivator->IsPlayer( )) ? pActivator : UTIL_PlayerByIndex( 1 );

	if( NF_DEBUG( NF_DBG_TRIGGERS ))
		ALERT( at_console, "nf_debug: endgame %s status %d%s\n", STRING( pev->targetname ), m_iStatus,
			m_iStatus == NF_ENDGAME_FAILED ? " (mission failed: not implemented)" : "" );

	if( !pPlayer || m_iStatus == NF_ENDGAME_FAILED )
		return;

	UTIL_EmitAmbientSound( pPlayer->edict(), pPlayer->pev->origin, "common/stinger.wav", 1.0f, 1.25f, 0, PITCH_NORM );

	if( m_iStatus == NF_ENDGAME_WON )
		SERVER_COMMAND( "CL_GameSuccess\n" );
}
