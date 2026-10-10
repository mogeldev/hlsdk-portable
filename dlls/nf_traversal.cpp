#include <math.h>
#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "weapons.h"
#include "nf_debug.h"
#include "nf_traversal.h"
#include "../pm_shared/nf_traversal.h"

extern DLL_GLOBAL BOOL g_fGameOver;

static BOOL NF_TraversalBrush( CBaseEntity *entity )
{
	return entity && ( FClassnameIs( entity->pev, "func_huggable" ) ||
		FClassnameIs( entity->pev, "func_handoverhand" ));
}

static CBaseEntity *NF_TraversalEntity( int state )
{
	int index = NF_TRAVERSAL_ENTITY( state );
	if( index <= gpGlobals->maxClients || index >= gpGlobals->maxEntities ) return NULL;
	return CBaseEntity::Instance( INDEXENT( index ));
}

static BOOL NF_TraversalValid( CBaseEntity *entity, int state )
{
	if( !NF_TraversalBrush( entity ) || ( entity->pev->flags & FL_KILLME )) return FALSE;
	int phase = NF_TRAVERSAL_PHASE( state );
	if( phase < NF_TRAVERSAL_WALL_MOUNT || phase > NF_TRAVERSAL_CABLE_TURN_REVERSE ) return FALSE;
	if( phase <= NF_TRAVERSAL_WALL_ACTIVE )
		return FClassnameIs( entity->pev, "func_huggable" ) && entity->pev->iuser1 == NF_TRAVERSAL_WALL;
	return FClassnameIs( entity->pev, "func_handoverhand" ) && entity->pev->iuser1 == NF_TRAVERSAL_CABLE &&
		( entity->pev->vuser2 - entity->pev->vuser1 ).Length() > 0.01f;
}

class CNFTraversalBrush : public CBaseEntity
{
public:
	void Spawn( void );
	void KeyValue( KeyValueData *data );
	void Use( CBaseEntity *activator, CBaseEntity *caller, USE_TYPE type, float value );
	void EXPORT ResolveThink( void );
	void UpdateOnRemove( void );
	int ObjectCaps( void ) { return CBaseEntity::ObjectCaps() & ~FCAP_ACROSS_TRANSITION; }
	int Save( CSave &save );
	int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];

private:
	void Bridge( void );
	void ReleasePlayers( void );
	string_t m_firstTarget, m_lastTarget;
	int m_marker, m_problem;
	BOOL m_ready;
	Vector m_first, m_last, m_orientation;
	float m_idealExitYaw;
};

LINK_ENTITY_TO_CLASS( func_handoverhand, CNFTraversalBrush )
LINK_ENTITY_TO_CLASS( func_huggable, CNFTraversalBrush )

TYPEDESCRIPTION CNFTraversalBrush::m_SaveData[] =
{
	DEFINE_FIELD( CNFTraversalBrush, m_firstTarget, FIELD_STRING ),
	DEFINE_FIELD( CNFTraversalBrush, m_lastTarget, FIELD_STRING ),
	DEFINE_FIELD( CNFTraversalBrush, m_marker, FIELD_INTEGER ),
	DEFINE_FIELD( CNFTraversalBrush, m_problem, FIELD_INTEGER ),
	DEFINE_FIELD( CNFTraversalBrush, m_ready, FIELD_BOOLEAN ),
	DEFINE_FIELD( CNFTraversalBrush, m_first, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( CNFTraversalBrush, m_last, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( CNFTraversalBrush, m_orientation, FIELD_VECTOR ),
	DEFINE_FIELD( CNFTraversalBrush, m_idealExitYaw, FIELD_FLOAT ),
};

void CNFTraversalBrush::Bridge( void )
{
	pev->angles = g_vecZero;
	pev->solid = SOLID_NOT;
	pev->movetype = MOVETYPE_NONE;
	pev->skin = CONTENTS_LADDER;
	pev->rendermode = kRenderTransTexture;
	pev->renderamt = 0;
	pev->effects &= ~EF_NODRAW;
	pev->iuser1 = m_ready ? m_marker : NF_TRAVERSAL_DISABLED;
	if( FClassnameIs( pev, "func_huggable" ))
	{
		pev->vuser1 = Vector( 0, m_idealExitYaw, 0 );
		pev->vuser2 = m_orientation;
	}
	else
	{
		pev->vuser1 = m_first;
		pev->vuser2 = m_last;
	}
	UTIL_SetOrigin( pev, pev->origin );
}

void CNFTraversalBrush::Spawn( void )
{
	PRECACHE_MODEL( "models/3rd_person.mdl" );
	m_orientation = pev->angles;
	m_ready = FClassnameIs( pev, "func_huggable" );
	m_marker = m_ready ? NF_TRAVERSAL_WALL :
		( pev->spawnflags & 1 ? NF_TRAVERSAL_DISABLED : NF_TRAVERSAL_CABLE );
	pev->angles = g_vecZero;
	SET_MODEL( edict(), STRING( pev->model ));
	Bridge();
	if( !m_ready )
	{
		SetThink( &CNFTraversalBrush::ResolveThink );
		pev->nextthink = gpGlobals->time + 0.1f;
	}
}

void CNFTraversalBrush::KeyValue( KeyValueData *data )
{
	if( FStrEq( data->szKeyName, "firsttarget" )) m_firstTarget = ALLOC_STRING( data->szValue );
	else if( FStrEq( data->szKeyName, "lasttarget" )) m_lastTarget = ALLOC_STRING( data->szValue );
	else if( FStrEq( data->szKeyName, "idealexityaw" )) m_idealExitYaw = atof( data->szValue );
	else { CBaseEntity::KeyValue( data ); return; }
	data->fHandled = TRUE;
}

void CNFTraversalBrush::ResolveThink( void )
{
	CBaseEntity *first = m_firstTarget ? UTIL_FindEntityByTargetname( NULL, STRING( m_firstTarget )) : NULL;
	CBaseEntity *last = m_lastTarget ? UTIL_FindEntityByTargetname( NULL, STRING( m_lastTarget )) : NULL;
	if( first && last && ( last->pev->origin - first->pev->origin ).Length() > 0.01f )
	{
		m_first = first->pev->origin;
		m_last = last->pev->origin;
		m_ready = TRUE;
		m_problem = 0;
		Bridge();
		SetThink( NULL );
		pev->nextthink = 0;
		if( NF_DEBUG( NF_DBG_TRAVERSAL ))
			ALERT( at_console, "nf_debug: traversal resolved %s marker %d\n", STRING( pev->targetname ), pev->iuser1 );
		return;
	}
	int problem = !first ? 1 : ( !last ? 2 : 3 );
	if( problem != m_problem )
	{
		m_problem = problem;
		ALERT( at_console, "Nightfire traversal %s: %s (%s -> %s); cable remains disabled\n",
			STRING( pev->targetname ), problem == 1 ? "missing first target" :
			( problem == 2 ? "missing last target" : "coincident endpoints" ),
			STRING( m_firstTarget ), STRING( m_lastTarget ));
	}
	Bridge();
	// Keep malformed maps inert; retries also accommodate delayed target creation.
	pev->nextthink = gpGlobals->time + 1.0f;
}

void CNFTraversalBrush::ReleasePlayers( void )
{
	for( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *player = (CBasePlayer *)UTIL_PlayerByIndex( i );
		if( player && ( player->m_hNFTraversalBrush == this ||
			( player->pev->iuser4 && NF_TRAVERSAL_ENTITY( player->pev->iuser4 ) == entindex() )))
			NF_TraversalReset( player, TRUE );
	}
}

void CNFTraversalBrush::Use( CBaseEntity *activator, CBaseEntity *caller, USE_TYPE type, float value )
{
	if( FClassnameIs( pev, "func_huggable" )) return;
	if( type == USE_OFF ) m_marker = NF_TRAVERSAL_CABLE;
	else if( type == USE_ON ) m_marker = NF_TRAVERSAL_DISABLED;
	else m_marker = m_marker == NF_TRAVERSAL_CABLE ? NF_TRAVERSAL_DISABLED : NF_TRAVERSAL_CABLE;
	Bridge();
	if( m_marker == NF_TRAVERSAL_DISABLED ) ReleasePlayers();
	if( NF_DEBUG( NF_DBG_TRAVERSAL ))
		ALERT( at_console, "nf_debug: traversal use %s type %d marker %d ready %d\n",
			STRING( pev->targetname ), type, pev->iuser1, m_ready );
}

void CNFTraversalBrush::UpdateOnRemove( void )
{
	ReleasePlayers();
	CBaseEntity::UpdateOnRemove();
}

int CNFTraversalBrush::Save( CSave &save )
{
	return CBaseEntity::Save( save ) && save.WriteFields( "NFTraversalBrush", this, m_SaveData, ARRAYSIZE( m_SaveData ));
}

int CNFTraversalBrush::Restore( CRestore &restore )
{
	if( !CBaseEntity::Restore( restore )) return 0;
	int result = restore.ReadFields( "NFTraversalBrush", this, m_SaveData, ARRAYSIZE( m_SaveData ));
	if( !result ) return 0;
	PRECACHE_MODEL( "models/3rd_person.mdl" );
	Bridge();
	if( !m_ready )
	{
		SetThink( &CNFTraversalBrush::ResolveThink );
		pev->nextthink = gpGlobals->time + 0.1f;
	}
	return result;
}

void NF_TraversalReset( CBasePlayer *player, BOOL redeploy )
{
	if( !player ) return;
	int oldState = player->pev->iuser4 ? player->pev->iuser4 : player->m_iNFTraversalState;
	int phase = NF_TRAVERSAL_PHASE( oldState );
	player->pev->iuser4 = 0;
	player->m_iNFTraversalState = 0;
	player->m_iNFTraversalSyncedState = 0;
	player->m_hNFTraversalBrush = NULL;
	if( oldState )
		player->pev->fuser4 = phase <= NF_TRAVERSAL_WALL_ACTIVE ? -NF_TRAVERSAL_WALL_COOLDOWN_MS : -NF_TRAVERSAL_CABLE_COOLDOWN_MS;
	player->m_flNFTraversalTimer = player->pev->fuser4;
	if( player->m_fNFTraversalHolstered )
	{
		player->m_fNFTraversalHolstered = FALSE;
		Vector mins = player->pev->mins, maxs = player->pev->maxs;
		if( player->m_iszNFTraversalModel ) SET_MODEL( player->edict(), STRING( player->m_iszNFTraversalModel ));
		UTIL_SetSize( player->pev, mins, maxs );
		player->pev->body = player->m_iNFTraversalBody;
		player->pev->skin = player->m_iNFTraversalSkin;
		BOOL sameWeapon = player->m_pActiveItem && player->m_hNFTraversalWeapon == player->m_pActiveItem;
		player->pev->viewmodel = sameWeapon ? player->m_iszNFTraversalViewModel : 0;
		player->pev->weaponmodel = sameWeapon ? player->m_iszNFTraversalWeaponModel : 0;
		player->pev->sequence = 0;
		player->pev->gaitsequence = 0;
		player->pev->frame = 0;
		player->pev->framerate = 1;
		player->ResetSequenceInfo();
		if( redeploy && player->IsAlive() && !player->IsObserver() && player->m_pActiveItem )
			player->m_pActiveItem->Deploy();
	}
	player->m_hNFTraversalWeapon = NULL;
	player->m_iszNFTraversalModel = 0;
	player->m_iszNFTraversalViewModel = 0;
	player->m_iszNFTraversalWeaponModel = 0;
	player->m_iszNFTraversalMap = 0;
	if( oldState && NF_DEBUG( NF_DBG_TRAVERSAL ))
		ALERT( at_console, "nf_debug: traversal detach player %d phase %d cooldown %.0f\n", player->entindex(), phase, player->pev->fuser4 );
}

void NF_TraversalPreThink( CBasePlayer *player )
{
	if( !player->pev->iuser4 )
	{
		if( player->m_fNFTraversalHolstered && ( g_fGameOver || !player->IsAlive() ||
			player->IsObserver() || player->pev->movetype != MOVETYPE_WALK ))
			NF_TraversalReset( player, player->IsAlive() && !player->IsObserver() );
		return;
	}
	CBaseEntity *brush = player->m_hNFTraversalBrush;
	if( !brush && !player->m_fNFTraversalHolstered )
		brush = NF_TraversalEntity( player->pev->iuser4 );
	if( g_fGameOver || !player->IsAlive() || player->IsObserver() || player->pev->movetype != MOVETYPE_WALK ||
		!NF_TraversalValid( brush, player->pev->iuser4 ) ||
		brush->entindex() != NF_TRAVERSAL_ENTITY( player->pev->iuser4 ))
		NF_TraversalReset( player, player->IsAlive() && !player->IsObserver() );
}

static const char *NF_TraversalSequence( CBasePlayer *player )
{
	int phase = NF_TRAVERSAL_PHASE( player->pev->iuser4 );
	if( phase == NF_TRAVERSAL_WALL_MOUNT ) return "wallstrafemount";
	if( phase == NF_TRAVERSAL_WALL_ACTIVE )
	{
		if( player->pev->button & IN_MOVERIGHT ) return "wallhugright";
		if( player->pev->button & IN_MOVELEFT ) return "wallstrafeleft";
		return "wallstrafeidle";
	}
	if( phase == NF_TRAVERSAL_CABLE_MOUNT ) return "cablegrapplemount";
	if( phase == NF_TRAVERSAL_CABLE_TURN_FORWARD || phase == NF_TRAVERSAL_CABLE_TURN_REVERSE ) return "cablegrappleturn";
	return player->pev->button & IN_FORWARD ? "cablegrapplecycle" : "cablegrappleidle";
}

void NF_TraversalPostThink( CBasePlayer *player )
{
	NF_TraversalPreThink( player );
	if( !player->pev->iuser4 )
	{
		if( player->m_fNFTraversalHolstered ) NF_TraversalReset( player, TRUE );
		player->m_iNFTraversalState = 0;
		player->m_flNFTraversalTimer = player->pev->fuser4;
		return;
	}
	CBaseEntity *brush = NF_TraversalEntity( player->pev->iuser4 );
	if( !NF_TraversalValid( brush, player->pev->iuser4 )) { NF_TraversalReset( player, TRUE ); return; }
	player->m_hNFTraversalBrush = brush;
	if( !player->m_fNFTraversalHolstered )
	{
		player->m_iszNFTraversalMap = gpGlobals->mapname;
		player->m_iszNFTraversalModel = player->pev->model;
		player->m_iszNFTraversalViewModel = player->pev->viewmodel;
		player->m_iszNFTraversalWeaponModel = player->pev->weaponmodel;
		player->m_iNFTraversalBody = player->pev->body;
		player->m_iNFTraversalSkin = player->pev->skin;
		player->m_hNFTraversalWeapon = player->m_pActiveItem;
		player->m_fNFTraversalHolstered = TRUE;
		if( player->m_pActiveItem ) player->m_pActiveItem->Holster();
		Vector mins = player->pev->mins, maxs = player->pev->maxs;
		SET_MODEL( player->edict(), "models/3rd_person.mdl" );
		UTIL_SetSize( player->pev, mins, maxs );
		player->pev->body = player->pev->skin = 0;
		player->pev->fov = player->m_iFOV = 0;
		player->ResetAutoaim();
	}
	player->pev->viewmodel = player->pev->weaponmodel = 0;
	player->pev->gaitsequence = 0;
	int sequence = player->LookupSequence( NF_TraversalSequence( player ));
	if( sequence >= 0 && ( player->pev->sequence != sequence || player->m_fSequenceFinished ))
	{
		player->pev->sequence = sequence;
		player->pev->frame = 0;
		player->ResetSequenceInfo();
	}
	int phase = NF_TRAVERSAL_PHASE( player->pev->iuser4 );
	if( phase <= NF_TRAVERSAL_WALL_ACTIVE ) player->pev->angles = brush->pev->vuser2;
	else
	{
		Vector direction = brush->pev->vuser2 - brush->pev->vuser1;
		float yaw = atan2( direction.y, direction.x ) * 180.0f / 3.14159265358979323846f;
		if( phase == NF_TRAVERSAL_CABLE_REVERSE || phase == NF_TRAVERSAL_CABLE_TURN_REVERSE ) yaw += 180;
		player->pev->angles = Vector( 0, yaw, 0 );
	}
	if( player->m_iNFTraversalSyncedState != player->pev->iuser4 )
	{
		player->pev->v_angle = player->pev->angles;
		player->pev->fixangle = TRUE;
	}
	float duration = phase == NF_TRAVERSAL_WALL_MOUNT ? NF_TRAVERSAL_WALL_MOUNT_MS :
		( phase == NF_TRAVERSAL_CABLE_MOUNT ? NF_TRAVERSAL_CABLE_MOUNT_MS : NF_TRAVERSAL_TURN_MS );
	if( phase == NF_TRAVERSAL_WALL_MOUNT || phase == NF_TRAVERSAL_CABLE_MOUNT ||
		phase == NF_TRAVERSAL_CABLE_TURN_FORWARD || phase == NF_TRAVERSAL_CABLE_TURN_REVERSE )
	{
		player->pev->frame = Q_max( 0.0f, Q_min( 255.0f, 255.0f * ( 1.0f - player->pev->fuser4 / duration )));
		player->pev->framerate = 0;
	}
	else player->pev->framerate = 1;
	if( player->m_iNFTraversalSyncedState != player->pev->iuser4 && NF_DEBUG( NF_DBG_TRAVERSAL ))
		ALERT( at_console, "nf_debug: traversal player %d mode %d brush %d timer %.0f yaw %.1f\n",
			player->entindex(), NF_TRAVERSAL_PHASE( player->pev->iuser4 ), brush->entindex(), player->pev->fuser4, player->pev->v_angle.y );
	player->m_iNFTraversalSyncedState = player->pev->iuser4;
	player->m_iNFTraversalState = player->pev->iuser4;
	player->m_flNFTraversalTimer = player->pev->fuser4;
	player->m_flFallVelocity = 0;
}

void NF_TraversalRestore( CBasePlayer *player, BOOL transition )
{
	player->pev->iuser4 = player->m_iNFTraversalState;
	player->pev->fuser4 = player->m_flNFTraversalTimer;
	CBaseEntity *brush = player->m_hNFTraversalBrush;
	if( player->m_iszNFTraversalMap && !FStrEq( STRING( player->m_iszNFTraversalMap ), STRING( gpGlobals->mapname ))) transition = TRUE;
	if( transition || ( player->pev->iuser4 && !NF_TraversalBrush( brush )) ||
		( !player->pev->iuser4 && player->m_fNFTraversalHolstered ))
	{
		// Weapons restore after the player: keep the holster record so the first
		// PostThink (!iuser4 && holstered) restores models and redeploys safely.
		player->pev->iuser4 = player->m_iNFTraversalState = player->m_iNFTraversalSyncedState = 0;
		player->m_hNFTraversalBrush = NULL;
		player->pev->fuser4 = player->m_flNFTraversalTimer = 0;
		return;
	}
	if( player->pev->iuser4 )
		player->pev->iuser4 = player->m_iNFTraversalState = NF_TRAVERSAL_STATE( brush->entindex(), NF_TRAVERSAL_PHASE( player->m_iNFTraversalState ));
	player->m_iNFTraversalSyncedState = player->pev->iuser4;
	if( !player->pev->iuser4 ) player->m_hNFTraversalBrush = NULL;
}

BOOL NF_TraversalCommand( CBaseEntity *entity, const char *command )
{
	if( !FStrEq( command, "nf_traversalinfo" )) return FALSE;
	if( entity && entity->IsPlayer() )
	{
		CBasePlayer *player = (CBasePlayer *)entity;
		CBasePlayerWeapon *weapon = player->m_pActiveItem ? (CBasePlayerWeapon *)player->m_pActiveItem->GetWeaponPtr() : NULL;
		ALERT( at_console, "nf_debug: traversalinfo player %d mode %d brush %d origin %.2f %.2f %.2f yaw %.2f timer %.0f model %s viewmodel %s weapon %s clip %d\n",
			player->entindex(), NF_TRAVERSAL_PHASE( player->pev->iuser4 ), NF_TRAVERSAL_ENTITY( player->pev->iuser4 ),
			player->pev->origin.x, player->pev->origin.y, player->pev->origin.z, player->pev->v_angle.y, player->pev->fuser4,
			STRING( player->pev->model ), STRING( player->pev->viewmodel ),
			player->m_pActiveItem ? STRING( player->m_pActiveItem->pev->classname ) : "-", weapon ? weapon->m_iClip : -1 );
		ALERT( at_console, "nf_debug: traversalinfo physics gravity %.3f movetype %d velocity %.2f %.2f %.2f buttons %d sequence %d name %s frame %.2f modelyaw %.2f holstered %d savedbrush %d\n",
			player->pev->gravity, player->pev->movetype, player->pev->velocity.x, player->pev->velocity.y, player->pev->velocity.z,
			player->pev->button, player->pev->sequence, player->pev->iuser4 ? NF_TraversalSequence( player ) : "-",
			player->pev->frame, player->pev->angles.y,
			player->m_fNFTraversalHolstered, player->m_hNFTraversalBrush ? player->m_hNFTraversalBrush->entindex() : 0 );
	}
	const char *classes[] = { "func_huggable", "func_handoverhand" };
	for( int i = 0; i < 2; ++i )
	{
		CBaseEntity *brush = NULL;
		while(( brush = UTIL_FindEntityByClassname( brush, classes[i] )) != NULL )
		{
			if( CMD_ARGC() > 1 && !FStrEq( CMD_ARGV( 1 ), STRING( brush->pev->targetname ))) continue;
			ALERT( at_console, "nf_debug: traversalinfo brush %d class %s name %s marker %d first %.2f %.2f %.2f last %.2f %.2f %.2f mins %.2f %.2f %.2f maxs %.2f %.2f %.2f\n",
				brush->entindex(), classes[i], STRING( brush->pev->targetname ), brush->pev->iuser1,
				brush->pev->vuser1.x, brush->pev->vuser1.y, brush->pev->vuser1.z,
				brush->pev->vuser2.x, brush->pev->vuser2.y, brush->pev->vuser2.z,
				brush->pev->absmin.x, brush->pev->absmin.y, brush->pev->absmin.z,
				brush->pev->absmax.x, brush->pev->absmax.y, brush->pev->absmax.z );
		}
	}
	return TRUE;
}
