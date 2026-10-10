#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "weapons.h"
#include "studio.h"
#include "nf_debug.h"
#include "nf_traversal.h"
#include "nf_deathcamera.h"

class CNFDeathCamera : public CPointEntity
{
public:
	void Spawn( void );
	void Precache( void );
	int ObjectCaps( void ) { return 0; }
	int Save( CSave &save );
	int Restore( CRestore &restore );
	void UpdateOnRemove( void );
	static TYPEDESCRIPTION m_SaveData[];
	BOOL Begin( CBaseEntity *victim, CBasePlayer *player, BOOL fixedOrigin );
	void End( void );
	void Apply( CBasePlayer *player );
	void EXPORT CameraThink( void );
	void Report( void );

	EHANDLE m_hPlayer, m_hVictim, m_hWeapon;
	float m_flStart;
	int m_iHideHUD, m_iFlags, m_iEffects;
	string_t m_iszViewModel, m_iszWeaponModel, m_iszMap;
	BOOL m_active;
};

LINK_ENTITY_TO_CLASS( env_deathcamera, CNFDeathCamera )

TYPEDESCRIPTION CNFDeathCamera::m_SaveData[] =
{
	DEFINE_FIELD( CNFDeathCamera, m_hPlayer, FIELD_EHANDLE ),
	DEFINE_FIELD( CNFDeathCamera, m_hVictim, FIELD_EHANDLE ),
	DEFINE_FIELD( CNFDeathCamera, m_hWeapon, FIELD_EHANDLE ),
	DEFINE_FIELD( CNFDeathCamera, m_flStart, FIELD_TIME ),
	DEFINE_FIELD( CNFDeathCamera, m_iHideHUD, FIELD_INTEGER ),
	DEFINE_FIELD( CNFDeathCamera, m_iFlags, FIELD_INTEGER ),
	DEFINE_FIELD( CNFDeathCamera, m_iEffects, FIELD_INTEGER ),
	DEFINE_FIELD( CNFDeathCamera, m_iszViewModel, FIELD_MODELNAME ),
	DEFINE_FIELD( CNFDeathCamera, m_iszWeaponModel, FIELD_MODELNAME ),
	DEFINE_FIELD( CNFDeathCamera, m_iszMap, FIELD_STRING ),
	DEFINE_FIELD( CNFDeathCamera, m_active, FIELD_BOOLEAN ),
};

IMPLEMENT_SAVERESTORE( CNFDeathCamera, CPointEntity )

void CNFDeathCamera::Precache( void )
{
	PRECACHE_MODEL( "models/shell.mdl" );
}

void CNFDeathCamera::Spawn( void )
{
	Precache();
	pev->movetype = MOVETYPE_NOCLIP;
	pev->solid = SOLID_NOT;
	pev->rendermode = kRenderTransTexture;
	pev->renderamt = 0;
}

static Vector NF_DeathCameraAim( CBaseEntity *victim )
{
	studiohdr_t *header = (studiohdr_t *)GET_MODEL_PTR( victim->edict() );
	if( header && header->ident == IDSTUDIOHEADER )
	{
		mstudiobone_t *bones = (mstudiobone_t *)((byte *)header + header->boneindex);
		for( int i = 0; i < header->numbones; ++i )
		{
			if( stricmp( bones[i].name, "Bip01 Spine" )) continue;
			Vector origin, angles;
			GET_BONE_POSITION( victim->edict(), i, origin, angles );
			return origin;
		}
	}
	return victim->Center();
}

BOOL CNFDeathCamera::Begin( CBaseEntity *victim, CBasePlayer *player, BOOL fixedOrigin )
{
	if( m_active || !victim || !player || !player->IsAlive() || player->IsObserver() ||
		player->m_hNFDeathCamera || player->m_pTank || ( player->pev->flags & FL_FROZEN )) return FALSE;
	Vector target = NF_DeathCameraAim( victim );
	if( !fixedOrigin )
	{
		UTIL_MakeVectors( victim->pev->angles );
		Vector candidates[] = { target + gpGlobals->v_forward * 92, target - gpGlobals->v_forward * 92,
			target + gpGlobals->v_right * 92, target - gpGlobals->v_right * 92, target + gpGlobals->v_up * 92 };
		BOOL found = FALSE;
		for( int i = 0; i < ARRAYSIZE( candidates ); ++i )
		{
			TraceResult trace;
			UTIL_TraceLine( target, candidates[i], ignore_monsters, victim->edict(), &trace );
			if( trace.fStartSolid || trace.fAllSolid || trace.flFraction < 1.0f ) continue;
			UTIL_SetOrigin( pev, candidates[i] );
			found = TRUE;
			break;
		}
		if( !found ) return FALSE;
	}
	NF_TraversalReset( player, TRUE );
	pev->angles = UTIL_VecToAngles( target - pev->origin );
	pev->angles.x = -pev->angles.x;
	SET_MODEL( edict(), "models/shell.mdl" );
	pev->velocity = pev->avelocity = g_vecZero;
	m_hPlayer = player;
	m_hVictim = victim;
	m_hWeapon = player->m_pActiveItem;
	m_flStart = gpGlobals->time;
	m_iHideHUD = player->m_iHideHUD;
	m_iFlags = player->pev->flags & FL_FROZEN;
	m_iEffects = player->pev->effects & EF_NODRAW;
	m_iszViewModel = player->pev->viewmodel;
	m_iszWeaponModel = player->pev->weaponmodel;
	m_iszMap = gpGlobals->mapname;
	m_active = TRUE;
	player->m_hNFDeathCamera = this;
	player->m_fNFDeathCamera = TRUE;
	player->m_hNFDeathCameraWeapon = m_hWeapon;
	player->m_iNFDeathCameraHUD = m_iHideHUD;
	player->m_iNFDeathCameraFlags = m_iFlags;
	player->m_iNFDeathCameraEffects = m_iEffects;
	player->m_iszNFDeathCameraViewModel = m_iszViewModel;
	player->m_iszNFDeathCameraWeaponModel = m_iszWeaponModel;
	Apply( player );
	SetThink( &CNFDeathCamera::CameraThink );
	pev->nextthink = gpGlobals->time + 0.1f;
	if( NF_DEBUG( NF_DBG_TRIGGERS ))
		ALERT( at_console, "nf_debug: deathcamera start %s victim %s player %d fixed %d at %.1f %.1f %.1f\n",
			STRING( pev->targetname ), STRING( victim->pev->targetname ), player->entindex(), fixedOrigin,
			pev->origin.x, pev->origin.y, pev->origin.z );
	return TRUE;
}

void CNFDeathCamera::Apply( CBasePlayer *player )
{
	player->EnableControl( FALSE );
	g_engfuncs.pfnSetPhysicsKeyValue( player->edict(), "nf_deathcam", "1" );
	player->pev->velocity = player->pev->avelocity = player->pev->basevelocity = g_vecZero;
	player->pev->button = 0;
	player->m_iHideHUD |= HIDEHUD_ALL;
	player->pev->effects |= EF_NODRAW;
	player->pev->viewmodel = player->pev->weaponmodel = 0;
	SET_VIEW( player->edict(), edict() );
}

static void NF_DeathCameraReleasePlayer( CBasePlayer *player )
{
	if( !player || !player->m_fNFDeathCamera ) return;
	player->m_fNFDeathCamera = FALSE;
	player->m_hNFDeathCamera = NULL;
	g_engfuncs.pfnSetPhysicsKeyValue( player->edict(), "nf_deathcam", "0" );
	SET_VIEW( player->edict(), player->edict() );
	player->pev->flags = ( player->pev->flags & ~FL_FROZEN ) | player->m_iNFDeathCameraFlags;
	player->pev->effects = ( player->pev->effects & ~EF_NODRAW ) | player->m_iNFDeathCameraEffects;
	player->m_iHideHUD = player->m_iNFDeathCameraHUD;
	if( player->IsAlive() && !player->IsObserver() && player->m_hNFDeathCameraWeapon == player->m_pActiveItem )
	{
		player->pev->viewmodel = player->m_iszNFDeathCameraViewModel;
		player->pev->weaponmodel = player->m_iszNFDeathCameraWeaponModel;
	}
	player->m_hNFDeathCameraWeapon = NULL;
	player->m_iszNFDeathCameraViewModel = player->m_iszNFDeathCameraWeaponModel = 0;
}

void CNFDeathCamera::End( void )
{
	if( !m_active ) return;
	m_active = FALSE;
	CBasePlayer *player = (CBasePlayer *)(CBaseEntity *)m_hPlayer;
	if( player && player->m_hNFDeathCamera == this ) NF_DeathCameraReleasePlayer( player );
	m_hPlayer = NULL;
	if( NF_DEBUG( NF_DBG_TRIGGERS ))
		ALERT( at_console, "nf_debug: deathcamera end %s elapsed %.2f\n", STRING( pev->targetname ), gpGlobals->time - m_flStart );
	SetThink( &CBaseEntity::SUB_Remove );
	pev->nextthink = gpGlobals->time;
}

void CNFDeathCamera::CameraThink( void )
{
	CBasePlayer *player = (CBasePlayer *)(CBaseEntity *)m_hPlayer;
	if( !player || !player->IsAlive() || player->IsObserver() || !m_hVictim ||
		player->m_hNFDeathCamera != this || !FStrEq( STRING( m_iszMap ), STRING( gpGlobals->mapname )) || gpGlobals->time - m_flStart >= 2.5f )
	{
		End();
		return;
	}
	// SET_VIEW is not saved client state; reapply after load without restarting the timer.
	Apply( player );
	pev->nextthink = gpGlobals->time + 0.1f;
}

void CNFDeathCamera::UpdateOnRemove( void )
{
	End();
	CPointEntity::UpdateOnRemove();
}

BOOL NF_DeathCameraStart( CBaseEntity *victim, entvars_t *attacker, const char *name )
{
	CBaseEntity *entity = attacker ? CBaseEntity::Instance( attacker ) : NULL;
	if( !victim || !entity || !entity->IsPlayer() ) return FALSE;
	CBasePlayer *player = (CBasePlayer *)entity;
	if( name && name[0] )
	{
		CBaseEntity *camera = UTIL_FindEntityByTargetname( NULL, name );
		if( !camera || !FClassnameIs( camera->pev, "env_deathcamera" )) return FALSE;
		return ((CNFDeathCamera *)camera)->Begin( victim, player, TRUE );
	}
	CNFDeathCamera *camera = (CNFDeathCamera *)CBaseEntity::Create( "env_deathcamera", victim->pev->origin, g_vecZero, NULL );
	if( !camera ) return FALSE;
	if( camera->Begin( victim, player, FALSE )) return TRUE;
	UTIL_Remove( camera );
	return FALSE;
}

void NF_DeathCameraReset( CBasePlayer *player )
{
	CBaseEntity *camera = player ? (CBaseEntity *)player->m_hNFDeathCamera : NULL;
	if( camera && FClassnameIs( camera->pev, "env_deathcamera" )) ((CNFDeathCamera *)camera)->End();
	NF_DeathCameraReleasePlayer( player );
}

void NF_DeathCameraPlayerThink( CBasePlayer *player )
{
	CBaseEntity *entity = player ? (CBaseEntity *)player->m_hNFDeathCamera : NULL;
	if( !entity || !FClassnameIs( entity->pev, "env_deathcamera" )) { NF_DeathCameraReset( player ); return; }
	if( entity->pev->flags & FL_KILLME ) { NF_DeathCameraReset( player ); return; }
	CNFDeathCamera *camera = (CNFDeathCamera *)entity;
	if( !camera->m_active || !player->IsAlive() || player->IsObserver() || !camera->m_hVictim ||
		!FStrEq( STRING( camera->m_iszMap ), STRING( gpGlobals->mapname ))) { NF_DeathCameraReset( player ); return; }
	camera->Apply( player );
}

void CNFDeathCamera::Report( void )
{
	ALERT( at_console, "nf_debug: deathcamera info %s active %d player %d victim %d elapsed %.2f\n",
		STRING( pev->targetname ), m_active, m_hPlayer ? m_hPlayer->entindex() : 0,
		m_hVictim ? m_hVictim->entindex() : 0, m_active ? gpGlobals->time - m_flStart : 0 );
}

BOOL NF_DeathCameraCommand( CBaseEntity *entity, const char *command )
{
	if( !FStrEq( command, "nf_deathcamerainfo" )) return FALSE;
	if( !NF_DEBUG( NF_DBG_TRIGGERS )) return TRUE;
	if( entity && entity->IsPlayer() )
	{
		CBasePlayer *player = (CBasePlayer *)entity;
		ALERT( at_console, "nf_debug: deathcamera player %d camera %d frozen %d nodraw %d hud %d viewmodel %s weapon %s clip %d\n",
			player->entindex(), player->m_hNFDeathCamera ? player->m_hNFDeathCamera->entindex() : 0,
			( player->pev->flags & FL_FROZEN ) != 0, ( player->pev->effects & EF_NODRAW ) != 0,
			player->m_iHideHUD, STRING( player->pev->viewmodel ),
			player->m_pActiveItem ? STRING( player->m_pActiveItem->pev->classname ) : "-",
			player->m_pActiveItem && player->m_pActiveItem->GetWeaponPtr() ?
				((CBasePlayerWeapon *)player->m_pActiveItem->GetWeaponPtr())->m_iClip : -1 );
	}
	CBaseEntity *camera = NULL;
	while(( camera = UTIL_FindEntityByClassname( camera, "env_deathcamera" )) != NULL )
		((CNFDeathCamera *)camera)->Report();
	return TRUE;
}
