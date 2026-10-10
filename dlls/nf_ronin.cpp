/* Nightfire suitcase turret and remote. Server authoritative, save-safe ownership. */
#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "player.h"
#include "soundent.h"
#include "nf_weapons.h"
#include "nf_explosives.h"
#include "nf_debug.h"

class CNFRoninTurret : public CGrenade
{
public:
	void Spawn( void ); void Precache( void );
	void EXPORT TurretThink( void );
	void EXPORT TurretUse( CBaseEntity *activator, CBaseEntity *caller, USE_TYPE type, float value );
	void EXPORT DeathThink( void );
	void Killed( entvars_t *attacker, int gib );
	void KeyValue( KeyValueData *data );
	int Classify( void ) { return pev->owner ? CLASS_PLAYER_ALLY : CLASS_MACHINE; }
	int Save( CSave &save ); int Restore( CRestore &restore ); static TYPEDESCRIPTION m_SaveData[];
	BOOL m_fActive;
	float m_flDeploy, m_flNextShot;
	string_t m_iszDeathTarget;
	EHANDLE m_hTarget;
};
LINK_ENTITY_TO_CLASS( enemy_ronin_turret, CNFRoninTurret )
TYPEDESCRIPTION CNFRoninTurret::m_SaveData[] =
{
	DEFINE_FIELD( CNFRoninTurret, m_fActive, FIELD_BOOLEAN ),
	DEFINE_FIELD( CNFRoninTurret, m_flDeploy, FIELD_TIME ),
	DEFINE_FIELD( CNFRoninTurret, m_flNextShot, FIELD_TIME ),
	DEFINE_FIELD( CNFRoninTurret, m_iszDeathTarget, FIELD_STRING ),
	DEFINE_FIELD( CNFRoninTurret, m_hTarget, FIELD_EHANDLE ),
};
int CNFRoninTurret::Save( CSave &save )
{
	if( !CGrenade::Save( save )) return 0;
	return save.WriteFields( "CNFRoninTurret", this, m_SaveData, ARRAYSIZE( m_SaveData ));
}
int CNFRoninTurret::Restore( CRestore &restore )
{
	if( !CGrenade::Restore( restore ) || !restore.ReadFields( "CNFRoninTurret", this, m_SaveData, ARRAYSIZE( m_SaveData ))) return 0;
	pev->iuser1 = m_fActive;
	pev->iuser2 = pev->sequence != 2;
	return 1;
}
BOOL NF_HasRonin( CBasePlayer *player )
{
	CBaseEntity *turret = NULL;
	while(( turret = UTIL_FindEntityByClassname( turret, "enemy_ronin_turret" )) != NULL )
		if( turret->pev->owner == player->edict() && turret->IsAlive() ) return TRUE;
	return FALSE;
}
void CNFRoninTurret::KeyValue( KeyValueData *data )
{
	if( FStrEq( data->szKeyName, "deathtarget" )) { m_iszDeathTarget = ALLOC_STRING( data->szValue ); data->fHandled = TRUE; }
	else CGrenade::KeyValue( data );
}
void CNFRoninTurret::Precache( void )
{
	PRECACHE_MODEL( "models/w_ronin.mdl" );
	PRECACHE_SOUND( "weapons/ronin_powerup.wav" ); PRECACHE_SOUND( "weapons/ronin_powerdown.wav" );
	PRECACHE_SOUND( "weapons/ronin_searching.wav" ); PRECACHE_SOUND( "weapons/ronin_fire1.wav" );
	PRECACHE_SOUND( "weapons/ronin_fire2.wav" ); PRECACHE_SOUND( "weapons/ronin_fire3.wav" );
	PRECACHE_SOUND( "weapons/grenade_explode.wav" );
}
void CNFRoninTurret::Spawn( void )
{
	Precache(); SET_MODEL( edict(), "models/w_ronin.mdl" );
	UTIL_SetSize( pev, Vector( -14, -12, 0 ), Vector( 14, 12, 16 ));
	pev->movetype = MOVETYPE_TOSS; pev->solid = SOLID_BBOX;
	pev->takedamage = DAMAGE_YES;
	if( pev->health <= 0 ) pev->health = NF_SkillValue( "sk_plr_ronin_health" );
	pev->dmg = NF_SkillValue( "sk_plr_ronin_charge" );
	pev->flags |= FL_MONSTER;
	pev->view_ofs = Vector( 0, 0, 14 );
	m_fActive = pev->targetname == iStringNull;
	m_flDeploy = gpGlobals->time + 1.83f;
	pev->sequence = 2; pev->framerate = 1; ResetSequenceInfo();
	SetUse( &CNFRoninTurret::TurretUse ); SetThink( &CNFRoninTurret::TurretThink );
	pev->nextthink = gpGlobals->time + 0.1f;
}
void CNFRoninTurret::TurretUse( CBaseEntity *activator, CBaseEntity *caller, USE_TYPE type, float value )
{
	if( !IsAlive() ) return;
	if( value == 2 ) { Killed( activator ? activator->pev : pev, GIB_NEVER ); return; }
	BOOL enabled = type == USE_ON ? TRUE : type == USE_OFF ? FALSE : !m_fActive;
	if( enabled == m_fActive ) return;
	m_fActive = enabled; pev->iuser1 = enabled; m_hTarget = NULL;
	EMIT_SOUND( edict(), CHAN_BODY, enabled ? "weapons/ronin_powerup.wav" : "weapons/ronin_powerdown.wav", 1, ATTN_NORM );
	if( NF_DEBUG( NF_DBG_WEAPONS )) ALERT( at_console, "nf_debug: ronin active %d owner %d\n", m_fActive, pev->owner ? ENTINDEX( pev->owner ) : 0 );
}
void CNFRoninTurret::Killed( entvars_t *attacker, int gib )
{
	if( pev->deadflag != DEAD_NO ) return;
	pev->deadflag = DEAD_DEAD; pev->takedamage = DAMAGE_NO; m_fActive = FALSE;
	if( m_iszDeathTarget ) FireTargets( STRING( m_iszDeathTarget ), CBaseEntity::Instance( attacker ), this, USE_TOGGLE, 0 );
	SetThink( &CNFRoninTurret::DeathThink ); pev->nextthink = gpGlobals->time + 0.1f;
}
void CNFRoninTurret::DeathThink( void )
{
	TraceResult trace;
	UTIL_TraceLine( pev->origin + Vector( 0, 0, 8 ), pev->origin - Vector( 0, 0, 32 ), ignore_monsters, edict(), &trace );
	if( NF_DEBUG( NF_DBG_WEAPONS )) ALERT( at_console, "nf_debug: ronin destroyed owner %d\n", pev->owner ? ENTINDEX( pev->owner ) : 0 );
	NF_Explode( this, &trace, DMG_BLAST );
}
void CNFRoninTurret::TurretThink( void )
{
	if( !IsInWorld() ) { UTIL_Remove( this ); return; }
	StudioFrameAdvance();
	pev->nextthink = gpGlobals->time + 0.1f;
	if( gpGlobals->time < m_flDeploy || !( pev->flags & FL_ONGROUND )) return;
	if( pev->sequence == 2 )
	{
		pev->sequence = 4; pev->frame = 0; pev->iuser2 = 1; pev->iuser1 = m_fActive; ResetSequenceInfo();
		if( NF_DEBUG( NF_DBG_WEAPONS )) ALERT( at_console, "nf_debug: ronin deployed owner %d health %.0f\n", pev->owner ? ENTINDEX( pev->owner ) : 0, pev->health );
	}
	if( !m_fActive ) return;
	Vector source = pev->origin + pev->view_ofs;
	CBaseEntity *candidate = NULL, *target = NULL;
	float nearest = 2048;
	while(( candidate = UTIL_FindEntityInSphere( candidate, source, 2048 )) != NULL )
	{
		if( candidate == this || candidate->edict() == pev->owner || !candidate->IsAlive() || candidate->pev->deadflag != DEAD_NO || candidate->pev->health <= 0 || candidate->pev->takedamage == DAMAGE_NO ) continue;
		if( !( candidate->pev->flags & ( FL_MONSTER | FL_CLIENT )) || FClassnameIs( candidate->pev, "enemy_ronin_turret" )) continue;
		if( pev->owner ? candidate->IsPlayer() || candidate->Classify() == CLASS_PLAYER_ALLY : !candidate->IsPlayer() ) continue;
		Vector goal = candidate->BodyTarget( source );
		float distance = ( goal - source ).Length();
		TraceResult trace; UTIL_TraceLine( source, goal, dont_ignore_monsters, edict(), &trace );
		if( distance < nearest && !NF_SmokeOccludes( source, goal ) && ( trace.flFraction == 1 || trace.pHit == candidate->edict() )) { target = candidate; nearest = distance; }
	}
	if( !target ) { m_hTarget = NULL; return; }
	if( (CBaseEntity *)m_hTarget != target )
	{
		m_hTarget = target;
		m_flNextShot = gpGlobals->time + 0.3f;
		if( NF_DEBUG( NF_DBG_WEAPONS )) ALERT( at_console, "nf_debug: ronin acquired %s\n", STRING( target->pev->targetname ));
	}
	Vector aim = ( target->BodyTarget( source ) - source ).Normalize();
	Vector angles = UTIL_VecToAngles( aim );
	pev->angles.y = angles.y;
	SetBoneController( 0, -angles.x );
	if( gpGlobals->time < m_flNextShot ) return;
	TraceResult trace; UTIL_TraceLine( source, source + aim * 2048, dont_ignore_monsters, edict(), &trace );
	if( trace.pHit )
	{
		CBaseEntity *hit = CBaseEntity::Instance( trace.pHit );
		if( hit ) { ClearMultiDamage(); hit->TraceAttack( pev->owner ? VARS( pev->owner ) : pev, NF_SkillValue( "sk_plr_ronin_shoot" ), aim, &trace, DMG_BULLET ); ApplyMultiDamage( pev, pev->owner ? VARS( pev->owner ) : pev ); }
	}
	EMIT_SOUND( edict(), CHAN_WEAPON, UTIL_VarArgs( "weapons/ronin_fire%d.wav", RANDOM_LONG( 1, 3 )), 1, ATTN_NORM );
	pev->effects |= EF_MUZZLEFLASH; pev->sequence = 3; pev->frame = 0; ResetSequenceInfo();
	m_flNextShot = gpGlobals->time + 0.1f;
	if( NF_DEBUG( NF_DBG_WEAPONS )) ALERT( at_console, "nf_debug: ronin shot target %s health %.0f\n", STRING( target->pev->targetname ), target->pev->health );
}

class CNFRonin : public CBasePlayerWeapon
{
public:
	void Spawn( void ); void Precache( void );
	int GetItemInfo( ItemInfo *info ); int iItemSlot( void ) { return 5; }
	int AddToPlayer( CBasePlayer *player ); int AddDuplicate( CBasePlayerItem *original );
	void EXPORT RoninTouch( CBaseEntity *other ) { if( other->IsPlayer() && NF_HasRonin( (CBasePlayer *)other )) return; DefaultTouch( other ); }
	void EXPORT RoninFallThink( void );
	BOOL CanDeploy( void ) { return m_hTurret != 0 || CBasePlayerWeapon::CanDeploy(); }
	BOOL IsUseable( void ) { return CanDeploy(); }
	BOOL Deploy( void );
	void PrimaryAttack( void ); void SecondaryAttack( void );
	void WeaponIdle( void );
	BOOL UseDecrement( void ) { return FALSE; }
	int Save( CSave &save ); int Restore( CRestore &restore ); static TYPEDESCRIPTION m_SaveData[];
	EHANDLE m_hTurret;
	void Place( BOOL toss );
};
LINK_ENTITY_TO_CLASS( weapon_ronin, CNFRonin )
TYPEDESCRIPTION CNFRonin::m_SaveData[] =
{
	DEFINE_FIELD( CNFRonin, m_hTurret, FIELD_EHANDLE ),
	DEFINE_FIELD( CNFRonin, m_flNextPrimaryAttack, FIELD_TIME ),
	DEFINE_FIELD( CNFRonin, m_flNextSecondaryAttack, FIELD_TIME ),
	DEFINE_FIELD( CNFRonin, m_flTimeWeaponIdle, FIELD_TIME ),
};
IMPLEMENT_SAVERESTORE( CNFRonin, CBasePlayerWeapon )
void CNFRonin::Spawn( void )
{
	Precache(); m_iId = NF_WEAPON_RONIN; m_iDefaultAmmo = 1;
	SET_MODEL( edict(), "models/w_ammo_ronin.mdl" ); FallInit();
	SetThink( &CNFRonin::RoninFallThink );
}
void CNFRonin::RoninFallThink( void )
{
	CBasePlayerItem::FallThink();
	if( pev->solid == SOLID_TRIGGER ) SetTouch( &CNFRonin::RoninTouch );
}
void CNFRonin::Precache( void )
{
	PRECACHE_MODEL( "models/v_ronin.mdl" ); PRECACHE_MODEL( "models/p_ronin.mdl" );
	PRECACHE_MODEL( "models/w_ronin.mdl" ); PRECACHE_MODEL( "models/w_ammo_ronin.mdl" );
	PRECACHE_MODEL( "models/p_detonator.mdl" ); UTIL_PrecacheOther( "enemy_ronin_turret" );
}
int CNFRonin::GetItemInfo( ItemInfo *info )
{
	info->pszName = "weapon_ronin"; info->pszAmmo1 = "ronin"; info->iMaxAmmo1 = 1;
	info->pszAmmo2 = NULL; info->iMaxAmmo2 = -1; info->iMaxClip = WEAPON_NOCLIP;
	info->iId = m_iId = NF_WEAPON_RONIN; info->iSlot = 4; info->iPosition = 6;
	info->iFlags = ITEM_FLAG_LIMITINWORLD | ITEM_FLAG_EXHAUSTIBLE | ITEM_FLAG_SELECTONEMPTY; info->iWeight = -10; return 1;
}
int CNFRonin::AddToPlayer( CBasePlayer *player )
{
	if( NF_HasRonin( player ) || !CBasePlayerWeapon::AddToPlayer( player )) return FALSE;
	MESSAGE_BEGIN( MSG_ONE, gmsgWeapPickup, NULL, player->pev ); WRITE_BYTE( m_iId ); MESSAGE_END(); return TRUE;
}
int CNFRonin::AddDuplicate( CBasePlayerItem *original )
{
	CNFRonin *weapon = (CNFRonin *)original;
	if( weapon->m_pPlayer && NF_HasRonin( weapon->m_pPlayer )) return FALSE;
	return CBasePlayerWeapon::AddDuplicate( original );
}
BOOL CNFRonin::Deploy( void )
{
	return DefaultDeploy( "models/v_ronin.mdl", m_hTurret ? "models/p_detonator.mdl" : "models/p_ronin.mdl", m_hTurret ? 12 : 6, m_hTurret ? "detonator" : "ronin" );
}
void CNFRonin::PrimaryAttack( void )
{
	if( m_hTurret ) { m_hTurret->Use( m_pPlayer, this, USE_TOGGLE, 0 ); SendWeaponAnim( 10 ); m_flNextPrimaryAttack = m_flNextSecondaryAttack = gpGlobals->time + 1; }
	else Place( TRUE );
}
void CNFRonin::SecondaryAttack( void )
{
	if( m_hTurret )
	{
		m_hTurret->Use( m_pPlayer, this, USE_ON, 2 ); m_hTurret = NULL; SendWeaponAnim( 11 );
		m_flNextPrimaryAttack = m_flNextSecondaryAttack = m_flTimeWeaponIdle = gpGlobals->time + 4;
	}
	else Place( FALSE );
}
void CNFRonin::Place( BOOL toss )
{
	m_flNextPrimaryAttack = m_flNextSecondaryAttack = gpGlobals->time + 0.5f;
	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0 || NF_HasRonin( m_pPlayer )) return;
	UTIL_MakeVectors( m_pPlayer->pev->v_angle );
	Vector forward = gpGlobals->v_forward, start = m_pPlayer->GetGunPosition();
	Vector origin = start + forward * 32;
	TraceResult trace;
	UTIL_TraceHull( start, origin, dont_ignore_monsters, head_hull, m_pPlayer->edict(), &trace );
	if( trace.flFraction < 1 || trace.fStartSolid ) return;
	if( !toss )
	{
		UTIL_TraceLine( origin, origin - Vector( 0, 0, 128 ), dont_ignore_monsters, m_pPlayer->edict(), &trace );
		if( trace.flFraction == 1 || trace.vecPlaneNormal.z < 0.7f ) return;
		origin = trace.vecEndPos + Vector( 0, 0, 1 );
	}
	CNFRoninTurret *turret = (CNFRoninTurret *)CBaseEntity::Create( "enemy_ronin_turret", origin, Vector( 0, m_pPlayer->pev->v_angle.y, 0 ), m_pPlayer->edict() );
	if( !turret ) return;
	turret->m_fActive = FALSE;
	if( toss ) turret->pev->velocity = forward * 274 + m_pPlayer->pev->velocity + Vector( 0, 0, 150 );
	m_hTurret = turret;
	--m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType];
	SendWeaponAnim( toss ? 4 : 5 );
	m_pPlayer->pev->weaponmodel = MAKE_STRING( "models/p_detonator.mdl" );
	m_pPlayer->SetAnimation( PLAYER_ATTACK1 );
	m_flNextPrimaryAttack = m_flNextSecondaryAttack = gpGlobals->time + 1.83f;
	m_flTimeWeaponIdle = gpGlobals->time + 2.6f;
	if( NF_DEBUG( NF_DBG_WEAPONS )) ALERT( at_console, "nf_debug: ronin placed toss %d ammo %d\n", toss, m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] );
}
void CNFRonin::WeaponIdle( void )
{
	if( m_flTimeWeaponIdle > gpGlobals->time ) return;
	if( !m_hTurret && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0 ) { RetireWeapon(); return; }
	SendWeaponAnim( m_hTurret ? 8 : 0 ); m_flTimeWeaponIdle = gpGlobals->time + 3;
}
