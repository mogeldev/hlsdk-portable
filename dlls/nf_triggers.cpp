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

trigger_objective
	Retail CTriggerObjective (game.dll, Use 0x42011b30, KeyValue
	0x42011a90). Keys: "idkey" (objective number), "completed" (0/1),
	"duration" (seconds, atoi in retail), "message" (title shown in the
	message box), "netname" (title of the entry in the objective list).
	Use adds the objective to the global list (CGlobalState, retail
	0x421ad6c0: id, title = netname or else message, map name, completed)
	or, when it is there already and "completed" is set, marks it done;
	then sends "Objective" to everybody: reset 0, id, message, netname,
	box flag (spawnflags & 1), list flag (!(spawnflags & 2)), duration,
	completed. After a save game is loaded the list is sent again (reset
	flag on the first entry, titles only); after a new game the client
	list and hints are cleared (retail 0x420d3a10 / 0x420d3ab0 from the
	player's client update). Precaches common/obj_open.wav and
	obj_close.wav (played by the client).

trigger_playerfreeze
	Retail CTriggerPlayerFreeze (Use 0x42011f20, Spawn 0x42012030,
	Restore 0x42011fd0, PlayerFreezeDelay 0x42011f80). Removed in
	multiplayer. Each Use flips the state (starts with control enabled) and
	calls EnableControl on all players (FL_FROZEN). After a restore with
	the player frozen it applies the state again after 0.5 s. Spawnflag 1
	sets a global (0x4215b7c4, also reset by the world spawn) whose
	meaning is not traced; not ported.

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
	1.25); 2 also sets the retail cvars sv_newunit (not in this port) and
	sv_iamdone = 1 (unlocks MISSION SELECT in the menu) and sends the engine
	command "CL_GameSuccess" (leave the game, m9_outro). Status 0 = mission failed (retail CBondRules vtable
	slot 29, 0x420c2900, and Think 0x420c2260): common/bond_death.wav at
	the player (ambient, vol 1, attn 1.25), fade to black (5 s, hold 15 s),
	the player loses all items (0x420a6b40), 10 s later a second fade
	(2 s, hold 2 s) and the engine command "CL_QuitToMenu" (leave the
	game, load dialog). The timer lives in this entity instead of the game
	rules. Not ported: the client message "SetBlends" 0 and svc_cdtrack 3
	(music), and the global byte 0x4215b7c0 that every Use sets.

trigger_changelevelicon
	Retail CChangeLevelIcon (Spawn 0x420080b0, PlayerTouch 0x42007fe0,
	PlayerTouchThink 0x42007f30): a trigger brush at the level exits (49
	in 28 maps, most over a trigger_changelevel; key "master" on 11, e.g.
	the exit opens after the objectives). A player touching it (master
	triggered) gets "SetHudIcon" 2, the client's 640_use_level_trans.png
	at the bottom centre; 0.5 s after the last touch "SetHudIcon" 0 hides
	it. Retail also keeps the icon in a player field (+0xc48) so its
	"look at a usable object" icon does not replace it, and
	CChangeLevel::ExecuteChangeLevel sends 0; this port has no use icon
	yet and the client clears the icon on every map load.
*/

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "skill.h"
#include "saverestore.h"
#include "gamerules.h"
#include "shake.h"
#include "nf_debug.h"
#include "nf_triggers.h"

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

#define NF_MAX_OBJECTIVES	64

typedef struct
{
	int id;
	char name[64];		// title of the list entry (netname, else message)
	char levelName[32];	// map that added it
	int completed;
} nf_objective_t;

typedef struct
{
	int count;
	nf_objective_t list[NF_MAX_OBJECTIVES];	// in the order they were added (retail: newest first)
} nf_objectives_t;

static nf_objectives_t s_objectives;
static BOOL s_bObjectivesResend;	// send the whole list to the client (after a restore)
static BOOL s_bObjectivesClear;	// clear the client list and hints (new game)

static TYPEDESCRIPTION gObjectivesSaveData[] =
{
	DEFINE_FIELD( nf_objectives_t, count, FIELD_INTEGER ),
};

static TYPEDESCRIPTION gObjectiveSaveData[] =
{
	DEFINE_FIELD( nf_objective_t, id, FIELD_INTEGER ),
	DEFINE_ARRAY( nf_objective_t, name, FIELD_CHARACTER, 64 ),
	DEFINE_ARRAY( nf_objective_t, levelName, FIELD_CHARACTER, 32 ),
	DEFINE_FIELD( nf_objective_t, completed, FIELD_INTEGER ),
};

static nf_objective_t *NF_ObjectiveFind( int id )
{
	for( int i = 0; i < s_objectives.count; i++ )
	{
		if( s_objectives.list[i].id == id )
			return &s_objectives.list[i];
	}
	return NULL;
}

int NF_ObjectivesSave( CSave &save )
{
	if( !save.WriteFields( "NFOBJ", &s_objectives, gObjectivesSaveData, ARRAYSIZE( gObjectivesSaveData )))
		return 0;

	for( int i = 0; i < s_objectives.count; i++ )
	{
		if( !save.WriteFields( "OENT", &s_objectives.list[i], gObjectiveSaveData, ARRAYSIZE( gObjectiveSaveData )))
			return 0;
	}
	return 1;
}

void NF_ObjectivesRestore( CRestore &restore )
{
	// saves made before the objectives were ported have no "NFOBJ" (ReadFields rewinds)
	if( !restore.ReadFields( "NFOBJ", &s_objectives, gObjectivesSaveData, ARRAYSIZE( gObjectivesSaveData )))
	{
		s_objectives.count = 0;
		return;
	}

	int count = s_objectives.count;
	s_objectives.count = 0;
	for( int i = 0; i < count && s_objectives.count < NF_MAX_OBJECTIVES; i++ )
	{
		if( !restore.ReadFields( "OENT", &s_objectives.list[s_objectives.count], gObjectiveSaveData, ARRAYSIZE( gObjectiveSaveData )))
			break;
		s_objectives.count++;
	}

	if( s_objectives.count > 0 )
		s_bObjectivesResend = TRUE;
}

void NF_ObjectivesClear( void )
{
	memset( &s_objectives, 0, sizeof( s_objectives ));
	s_bObjectivesResend = FALSE;
	s_bObjectivesClear = TRUE;
}

void NF_ObjectivesUpdateClient( CBasePlayer *pPlayer )
{
	if( s_bObjectivesResend )
	{
		for( int i = 0; i < s_objectives.count; i++ )
		{
			const nf_objective_t *obj = &s_objectives.list[i];

			MESSAGE_BEGIN( MSG_ONE, gmsgNFObjective, NULL, pPlayer->pev );
				WRITE_BYTE( i == 0 );	// reset the client list first
				WRITE_BYTE( obj->id );
				WRITE_STRING( obj->name );
				WRITE_STRING( "" );
				WRITE_BYTE( 0 );	// no message box
				WRITE_BYTE( 1 );	// list entry
				WRITE_BYTE( 0 );
				WRITE_BYTE( obj->completed );
			MESSAGE_END();
		}

		if( NF_DEBUG( NF_DBG_TRIGGERS ))
			ALERT( at_console, "nf_debug: objectives resent (%d)\n", s_objectives.count );
		s_bObjectivesResend = FALSE;
		s_bObjectivesClear = FALSE;
	}

	if( s_bObjectivesClear )
	{
		MESSAGE_BEGIN( MSG_ONE, gmsgNFObjective, NULL, pPlayer->pev );
			WRITE_BYTE( 1 );	// reset
			WRITE_BYTE( 255 );	// no entry
		MESSAGE_END();

		MESSAGE_BEGIN( MSG_ONE, gmsgNFHudMsg, NULL, pPlayer->pev );
			WRITE_STRING( "" );	// clear the hint section
			WRITE_BYTE( 0 );
			WRITE_BYTE( 0 );
			WRITE_BYTE( 0 );
		MESSAGE_END();

		s_bObjectivesClear = FALSE;
	}
}

#define SF_OBJECTIVE_BOX	1
#define SF_OBJECTIVE_NOLIST	2

class CTriggerObjective : public CPointEntity
{
public:
	void Spawn( void );
	void Precache( void );
	void KeyValue( KeyValueData *pkvd );
	void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );

	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];

	int m_iId;
	BOOL m_bCompleted;
	int m_iDuration;
};

LINK_ENTITY_TO_CLASS( trigger_objective, CTriggerObjective )

TYPEDESCRIPTION CTriggerObjective::m_SaveData[] =
{
	DEFINE_FIELD( CTriggerObjective, m_iId, FIELD_INTEGER ),
	DEFINE_FIELD( CTriggerObjective, m_bCompleted, FIELD_BOOLEAN ),
	DEFINE_FIELD( CTriggerObjective, m_iDuration, FIELD_INTEGER ),
};

IMPLEMENT_SAVERESTORE( CTriggerObjective, CPointEntity )

void CTriggerObjective::Spawn( void )
{
	Precache();
	CPointEntity::Spawn();
}

void CTriggerObjective::Precache( void )
{
	PRECACHE_SOUND( "common/obj_open.wav" );
	PRECACHE_SOUND( "common/obj_close.wav" );
}

void CTriggerObjective::KeyValue( KeyValueData *pkvd )
{
	if( FStrEq( pkvd->szKeyName, "idkey" ))
	{
		m_iId = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "completed" ))
	{
		m_bCompleted = atoi( pkvd->szValue ) != 0;
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "duration" ))
	{
		m_iDuration = atoi( pkvd->szValue );	// retail: atoi, "2.5" -> 2 s
		pkvd->fHandled = TRUE;
	}
	else
		CPointEntity::KeyValue( pkvd );
}

void CTriggerObjective::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	CBaseEntity *pPlayer = ( pActivator && pActivator->IsPlayer( )) ? pActivator : UTIL_PlayerByIndex( 1 );
	if( !pPlayer )
		ALERT( at_console, "Objective Added before player initialized\n" );

	nf_objective_t *obj = NF_ObjectiveFind( m_iId );
	if( !obj )
	{
		if( s_objectives.count < NF_MAX_OBJECTIVES )
		{
			obj = &s_objectives.list[s_objectives.count++];
			obj->id = m_iId;
			string_t title = !FStringNull( pev->netname ) ? pev->netname : pev->message;
			strncpy( obj->name, STRING( title ), sizeof( obj->name ) - 1 );
			obj->name[sizeof( obj->name ) - 1] = '\0';
			strncpy( obj->levelName, STRING( gpGlobals->mapname ), sizeof( obj->levelName ) - 1 );
			obj->levelName[sizeof( obj->levelName ) - 1] = '\0';
			obj->completed = m_bCompleted;
		}
		else
			ALERT( at_console, "trigger_objective %s: more than %d objectives\n", STRING( pev->targetname ), NF_MAX_OBJECTIVES );
	}
	else if( m_bCompleted )
		obj->completed = TRUE;

	if( !pPlayer )
	{
		ALERT( at_console, "trigger_objective %s used when player not initialized!\n", STRING( pev->message ));
		return;
	}

	int box = ( pev->spawnflags & SF_OBJECTIVE_BOX ) ? 1 : 0;
	int list = ( pev->spawnflags & SF_OBJECTIVE_NOLIST ) ? 0 : 1;
	int duration = m_iDuration < 0 ? 0 : ( m_iDuration > 255 ? 255 : m_iDuration );

	MESSAGE_BEGIN( MSG_ALL, gmsgNFObjective );
		WRITE_BYTE( 0 );	// no reset
		WRITE_BYTE( m_iId );
		WRITE_STRING( STRING( pev->message ));
		WRITE_STRING( STRING( pev->netname ));
		WRITE_BYTE( box );
		WRITE_BYTE( list );
		WRITE_BYTE( duration );	// whole seconds
		WRITE_BYTE( m_bCompleted ? 1 : 0 );
	MESSAGE_END();

	if( NF_DEBUG( NF_DBG_TRIGGERS ))
		ALERT( at_console, "nf_debug: objective %s id %d \"%s\" list \"%s\" box %d list %d duration %d completed %d (%d in list)\n",
			STRING( pev->targetname ), m_iId, STRING( pev->message ), STRING( pev->netname ),
			box, list, duration, m_bCompleted ? 1 : 0, s_objectives.count );
}

class CTriggerPlayerFreeze : public CPointEntity
{
public:
	void Spawn( void );
	void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );
	void EXPORT PlayerFreezeDelay( void );

	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];

	void Apply( void );

	BOOL m_bControl;	// players may move
};

LINK_ENTITY_TO_CLASS( trigger_playerfreeze, CTriggerPlayerFreeze )

TYPEDESCRIPTION CTriggerPlayerFreeze::m_SaveData[] =
{
	DEFINE_FIELD( CTriggerPlayerFreeze, m_bControl, FIELD_BOOLEAN ),
};

int CTriggerPlayerFreeze::Save( CSave &save )
{
	if( !CPointEntity::Save( save ))
		return 0;
	return save.WriteFields( "CTriggerPlayerFreeze", this, m_SaveData, ARRAYSIZE( m_SaveData ));
}

int CTriggerPlayerFreeze::Restore( CRestore &restore )
{
	if( !CPointEntity::Restore( restore ))
		return 0;
	int status = restore.ReadFields( "CTriggerPlayerFreeze", this, m_SaveData, ARRAYSIZE( m_SaveData ));

	// retail: freeze the restored player again a little later
	if( status && !m_bControl )
	{
		SetThink( &CTriggerPlayerFreeze::PlayerFreezeDelay );
		pev->nextthink = gpGlobals->time + 0.5f;
	}
	return status;
}

void CTriggerPlayerFreeze::Spawn( void )
{
	if( g_pGameRules->IsMultiplayer( ))
	{
		REMOVE_ENTITY( ENT( pev ));
		return;
	}
	m_bControl = TRUE;
	CPointEntity::Spawn();
}

void CTriggerPlayerFreeze::Apply( void )
{
	for( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *pPlayer = (CBasePlayer *)UTIL_PlayerByIndex( i );
		if( pPlayer )
			pPlayer->EnableControl( m_bControl );
	}
}

void CTriggerPlayerFreeze::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	m_bControl = !m_bControl;
	Apply();

	if( NF_DEBUG( NF_DBG_TRIGGERS ))
		ALERT( at_console, "nf_debug: playerfreeze %s -> player %s\n", STRING( pev->targetname ), m_bControl ? "free" : "frozen" );
}

void CTriggerPlayerFreeze::PlayerFreezeDelay( void )
{
	Apply();
	SetThink( NULL );
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
	void EXPORT QuitToMenuThink( void );

	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];

	void MissionFailed( CBasePlayer *pPlayer );

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
	PRECACHE_SOUND( "common/bond_death.wav" );
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
		ALERT( at_console, "nf_debug: endgame %s status %d\n", STRING( pev->targetname ), m_iStatus );

	if( !pPlayer )
		return;

	if( m_iStatus == NF_ENDGAME_FAILED )
	{
		MissionFailed( (CBasePlayer *)pPlayer );
		return;
	}

	UTIL_EmitAmbientSound( pPlayer->edict(), pPlayer->pev->origin, "common/stinger.wav", 1.0f, 1.25f, 0, PITCH_NORM );

	if( m_iStatus == NF_ENDGAME_WON )
	{
		// retail: sv_iamdone 1 unlocks MISSION SELECT in the front end (the
		// port's menu registers it as an archived cvar)
		SERVER_COMMAND( "sv_iamdone 1\n" );
		SERVER_COMMAND( "CL_GameSuccess\n" );
	}
}

void CTriggerEndGame::MissionFailed( CBasePlayer *pPlayer )
{
	UTIL_EmitAmbientSound( pPlayer->edict(), pPlayer->pev->origin, "common/bond_death.wav", 1.0f, 1.25f, 0, PITCH_NORM );
	UTIL_ScreenFade( pPlayer, g_vecZero, 5.0f, 15.0f, 255, FFADE_OUT );
	pPlayer->RemoveAllItems( FALSE );

	// retail: game rules timer, time + 10
	SetThink( &CTriggerEndGame::QuitToMenuThink );
	pev->nextthink = gpGlobals->time + 10.0f;
}

void CTriggerEndGame::QuitToMenuThink( void )
{
	SetThink( NULL );

	CBaseEntity *pPlayer = UTIL_PlayerByIndex( 1 );
	if( pPlayer )
		UTIL_ScreenFade( pPlayer, g_vecZero, 2.0f, 2.0f, 255, FFADE_OUT );

	if( NF_DEBUG( NF_DBG_TRIGGERS ))
		ALERT( at_console, "nf_debug: endgame %s mission failed -> CL_QuitToMenu\n", STRING( pev->targetname ));
	SERVER_COMMAND( "CL_QuitToMenu\n" );
}

#define NF_HUDICON_NONE		0
#define NF_HUDICON_LEVELTRANS	2	// client 640_use_level_trans.png

class CChangeLevelIcon : public CBaseToggle
{
public:
	void Spawn( void );
	void EXPORT PlayerTouch( CBaseEntity *pOther );
	void EXPORT PlayerTouchThink( void );

	BOOL m_bShown;		// retail +0xfc
	EHANDLE m_hPlayer;	// retail +0x100
};

LINK_ENTITY_TO_CLASS( trigger_changelevelicon, CChangeLevelIcon )

void CChangeLevelIcon::Spawn( void )
{
	// retail 0x420080b0: a trigger brush (CBaseTrigger::InitTrigger without the movedir)
	pev->solid = SOLID_TRIGGER;
	pev->movetype = MOVETYPE_NONE;
	SET_MODEL( ENT( pev ), STRING( pev->model ));
	if( CVAR_GET_FLOAT( "showtriggers" ) == 0 )
		SetBits( pev->effects, EF_NODRAW );

	SetTouch( &CChangeLevelIcon::PlayerTouch );
}

void CChangeLevelIcon::PlayerTouch( CBaseEntity *pOther )
{
	// retail 0x42007fe0
	if( !pOther || !pOther->IsPlayer( ) || !UTIL_IsMasterTriggered( m_sMaster, pOther ))
		return;

	if( !m_bShown )
	{
		MESSAGE_BEGIN( MSG_ONE, gmsgNFSetHudIcon, NULL, pOther->pev );
			WRITE_BYTE( NF_HUDICON_LEVELTRANS );
		MESSAGE_END();

		m_bShown = TRUE;
		m_hPlayer = pOther;
		SetThink( &CChangeLevelIcon::PlayerTouchThink );

		if( NF_DEBUG( NF_DBG_TRIGGERS ))
			ALERT( at_console, "nf_debug: changelevelicon %s shown\n", STRING( pev->model ));
	}

	// hidden again 0.5 s after the last touch
	pev->nextthink = gpGlobals->time + 0.5f;
}

void CChangeLevelIcon::PlayerTouchThink( void )
{
	// retail 0x42007f30
	if( m_bShown )
	{
		CBaseEntity *pPlayer = m_hPlayer;

		if( pPlayer )
		{
			MESSAGE_BEGIN( MSG_ONE, gmsgNFSetHudIcon, NULL, pPlayer->pev );
				WRITE_BYTE( NF_HUDICON_NONE );
			MESSAGE_END();
		}

		if( NF_DEBUG( NF_DBG_TRIGGERS ))
			ALERT( at_console, "nf_debug: changelevelicon %s hidden\n", STRING( pev->model ));
	}

	m_bShown = FALSE;
	m_hPlayer = NULL;
	SetThink( NULL );
}
