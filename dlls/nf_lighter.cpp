/* Nightfire camera: real character camera targets and info_picture_target markers. */
#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "weapons.h"
#include "player.h"
#include "nf_weapons.h"
#include "nf_gadgets.h"
#include "nf_debug.h"

// retail closes the camera with the literal normal view FOV 76 (the client default_fov_dontchange default)
#define NF_LIGHTER_FOV_NORMAL 76
extern int gmsgNFCameraMode;
class CNFPictureTarget : public CPointEntity {};
LINK_ENTITY_TO_CLASS( info_picture_target, CNFPictureTarget )
class CNFLighter : public CBasePlayerWeapon
{
public:
	void Spawn( void ) { Precache(); m_iId = NF_WEAPON_LIGHTER; m_iClip = WEAPON_NOCLIP; SET_MODEL( edict(), "models/w_kowloon.mdl" ); FallInit(); }
	void Precache( void ) { PRECACHE_MODEL( "models/v_lighter.mdl" ); PRECACHE_MODEL( "models/w_kowloon.mdl" ); PRECACHE_SOUND( "gadgets/nightvision_on.wav" ); PRECACHE_SOUND( "gadgets/nightvision_off.wav" ); }
	int iItemSlot( void ) { return 1; }
	int GetItemInfo( ItemInfo *p )
	{
		p->pszName = STRING( pev->classname ); p->pszAmmo1 = p->pszAmmo2 = NULL;
		p->iMaxAmmo1 = p->iMaxAmmo2 = -1; p->iMaxClip = WEAPON_NOCLIP;
		p->iSlot = 0; p->iPosition = 5; p->iFlags = 0; p->iId = m_iId = NF_WEAPON_LIGHTER; p->iWeight = -1; return 1;
	}
	int AddToPlayer( CBasePlayer *player ) { if( !CBasePlayerWeapon::AddToPlayer( player )) return FALSE; MESSAGE_BEGIN( MSG_ONE, gmsgWeapPickup, NULL, player->pev ); WRITE_BYTE( m_iId ); MESSAGE_END(); return TRUE; }
	BOOL UseDecrement( void ) { return FALSE; }
	BOOL CanDeploy( void ) { return TRUE; }
	BOOL IsUseable( void ) { return TRUE; }
	BOOL Deploy( void ) { End(); return DefaultDeploy( "models/v_lighter.mdl", "", 6, "onehanded" ); }
	void Overlay( int state ) { if( !m_pPlayer ) return; MESSAGE_BEGIN( MSG_ONE, gmsgNFCameraMode, NULL, m_pPlayer->pev ); WRITE_BYTE( state ); MESSAGE_END(); }
	void End( void )
	{
		NF_PhotoCharacterPose( m_pose, FALSE ); m_pose = NULL;
		if( m_camera && m_pPlayer )
		{
			m_pPlayer->pev->fov = m_pPlayer->m_iFOV = NF_LIGHTER_FOV_NORMAL;
			if( NF_DEBUG( NF_DBG_WEAPONS )) ALERT( at_console, "nf_debug: camera closed fov %d\n", m_pPlayer->m_iFOV );
		}
		m_camera = FALSE; m_ready = 0; Overlay( 0 );
	}
	void Holster( int skiplocal = 0 )
	{
		End();
		if( m_pPlayer ) m_pPlayer->pev->fov = m_pPlayer->m_iFOV = NF_LIGHTER_FOV_NORMAL;	// retail 0x420DFD70 resets unconditionally
		CBasePlayerWeapon::Holster( skiplocal );
	}
	void UpdateOnRemove( void ) { End(); CBasePlayerWeapon::UpdateOnRemove(); }
	void SecondaryAttack( void )
	{
		if( m_ready > gpGlobals->time ) return;
		if( m_camera ) { SendWeaponAnim( 5 ); End(); }
		else { m_camera = TRUE; m_ready = gpGlobals->time + 1.96f; SendWeaponAnim( 3 ); }
		m_flNextPrimaryAttack = gpGlobals->time + 1.96f; m_flNextSecondaryAttack = gpGlobals->time + 2.96f;
	}
	BOOL MarkerVisible( CBaseEntity *entity )
	{
		Vector eye = m_pPlayer->EyePosition(), dir = entity->pev->origin - eye;
		UTIL_MakeVectors( m_pPlayer->pev->v_angle );
		if( dir.Length() > 100 || DotProduct( gpGlobals->v_forward, dir.Normalize() ) <= 0.7f ) return FALSE;
		TraceResult tr; UTIL_TraceLine( eye, entity->pev->origin, ignore_monsters, dont_ignore_glass, m_pPlayer->edict(), &tr );
		if( tr.flFraction != 1 ) return FALSE;
		Vector f, r, u; UTIL_MakeVectorsPrivate( entity->pev->angles, f, r, u );
		return DotProduct( f.Make2D().Normalize(), (eye - entity->pev->origin).Make2D().Normalize() ) > -0.7f;
	}
	CBaseEntity *Character( void )
	{
		UTIL_MakeVectors( m_pPlayer->pev->v_angle ); TraceResult tr;
		Vector eye = m_pPlayer->EyePosition();
		UTIL_TraceLine( eye, eye + gpGlobals->v_forward * 100, dont_ignore_monsters, dont_ignore_glass, m_pPlayer->edict(), &tr );
		CBaseEntity *entity = tr.flFraction < 1 ? CBaseEntity::Instance( tr.pHit ) : NULL;
		if( NF_DEBUG( NF_DBG_WEAPONS ) && entity != m_dbgHit )
		{
			m_dbgHit = entity;
			ALERT( at_console, "nf_debug: camera aim %s fraction %.2f hitgroup %d\n", entity ? STRING( entity->pev->classname ) : "none", tr.flFraction, tr.iHitgroup );
		}
		return NF_PhotoCharacterTarget( entity ) ? entity : NULL;
	}
	void PrimaryAttack( void )
	{
		if( !m_camera ) { SecondaryAttack(); return; }
		if( m_ready > gpGlobals->time ) return;
		CBaseEntity *entity = Character(); string_t target = NF_PhotoCharacterTarget( entity );
		if( !target )
		{
			entity = NULL;
			while(( entity = UTIL_FindEntityByClassname( entity, "info_picture_target" )) != NULL )
				if( entity->pev->target && MarkerVisible( entity )) { target = entity->pev->target; break; }
		}
		if( target ) FireTargets( STRING( target ), m_pPlayer, m_pPlayer, USE_TOGGLE, 0 );
		Overlay( 3 ); SendWeaponAnim( 8 ); m_flNextPrimaryAttack = gpGlobals->time + 1; m_flTimeWeaponIdle = gpGlobals->time + 0.15f;
		if( NF_DEBUG( NF_DBG_WEAPONS )) ALERT( at_console, "nf_debug: camera shutter target %s\n", target ? STRING( target ) : "none" );
	}
	void ItemPostFrame( void )
	{
		if( m_camera && !m_pPlayer->IsAlive() ) { End(); return; }
		if( m_camera && gpGlobals->time >= m_ready )
		{
			m_pPlayer->pev->fov = m_pPlayer->m_iFOV = 60;
			CBaseEntity *entity = Character();
			if( entity != (CBaseEntity *)m_pose ) { NF_PhotoCharacterPose( m_pose, FALSE ); m_pose = entity; NF_PhotoCharacterPose( entity, TRUE ); }
			if( gpGlobals->time >= m_flTimeWeaponIdle ) Overlay( entity ? 2 : 1 );
		}
		CBasePlayerWeapon::ItemPostFrame();
	}
	// retail CLighter 0x420DFD50: no holster during the open transition or in camera mode
	BOOL CanHolster( void ) { return !m_camera; }
	// retail WeaponIdle 0x420E0190: anim 0/1 every 8-16 s; the camera-mode idle (anim 4) is not ported
	// because this port's shutter overlay already uses the idle timer
	void WeaponIdle( void )
	{
		if( gpGlobals->time < m_flTimeWeaponIdle ) return;
		if( m_camera ) { m_flTimeWeaponIdle = gpGlobals->time + 5; return; }
		SendWeaponAnim( RANDOM_LONG( 0, 1 ));
		m_flTimeWeaponIdle = gpGlobals->time + RANDOM_FLOAT( 8, 16 );
	}
	int Save( CSave &save ); int Restore( CRestore &restore ); static TYPEDESCRIPTION m_SaveData[];
	BOOL m_camera; float m_ready; EHANDLE m_pose; CBaseEntity *m_dbgHit;
};
TYPEDESCRIPTION CNFLighter::m_SaveData[] = { DEFINE_FIELD( CNFLighter, m_camera, FIELD_BOOLEAN ), DEFINE_FIELD( CNFLighter, m_ready, FIELD_TIME ), DEFINE_FIELD( CNFLighter, m_pose, FIELD_EHANDLE ) };
IMPLEMENT_SAVERESTORE( CNFLighter, CBasePlayerWeapon )
LINK_ENTITY_TO_CLASS( weapon_lighter, CNFLighter )
