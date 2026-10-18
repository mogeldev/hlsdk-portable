/* Nightfire PC launchers. Retail constants: docs/retail/arsenal-explosives.md. */
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

void NF_Explode( CGrenade *grenade, TraceResult *trace, int damageType )
{
	entvars_t *pev = grenade->pev;
	if( pev->effects & EF_NODRAW ) return;
	entvars_t *attacker = pev->owner ? VARS( pev->owner ) : pev;
	pev->owner = NULL;
	pev->solid = SOLID_NOT;
	pev->takedamage = DAMAGE_NO;
	pev->effects |= EF_NODRAW;
	pev->velocity = g_vecZero;
	if( trace->flFraction < 1.0f )
		UTIL_SetOrigin( pev, trace->vecEndPos + trace->vecPlaneNormal * 2.0f );
	MESSAGE_BEGIN( MSG_PAS, SVC_TEMPENTITY, pev->origin );
		WRITE_BYTE( TE_EXPLOSION );
		WRITE_COORD( pev->origin.x ); WRITE_COORD( pev->origin.y ); WRITE_COORD( pev->origin.z );
		WRITE_SHORT( UTIL_PointContents( pev->origin ) == CONTENTS_WATER ? g_sModelIndexWExplosion : g_sModelIndexFireball );
		WRITE_BYTE( Q_min( 255, Q_max( 1, (int)(( pev->dmg - 50 ) * 0.6f ))));
		WRITE_BYTE( 15 ); WRITE_BYTE( TE_EXPLFLAG_NOSOUND );
	MESSAGE_END();
	CSoundEnt::InsertSound( bits_SOUND_COMBAT, pev->origin, 1024, 3 );
	RadiusDamage( pev->origin, pev, attacker, pev->dmg, pev->dmg * 2.5f, CLASS_NONE, damageType );
	EMIT_SOUND( grenade->edict(), CHAN_WEAPON, "weapons/grenade_explode.wav", 1, 0.4f );
	if( NF_DEBUG( NF_DBG_WEAPONS ))
		ALERT( at_console, "nf_debug: %s blast damage %.0f owner %d\n", STRING( pev->classname ), pev->dmg, ENTINDEX( ENT( attacker )));
	grenade->SetTouch( NULL );
	grenade->SetThink( &CBaseEntity::SUB_Remove );
	pev->nextthink = gpGlobals->time + 0.1f;
}

class CNFRocket : public CGrenade
{
public:
	void Spawn( void );
	void Precache( void );
	void EXPORT FlyThink( void );
	void EXPORT RocketTouch( CBaseEntity *other );
	void UpdateOnRemove( void );
	int Save( CSave &save );
	int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];
	BOOL Guided( void ) { return FClassnameIs( pev, "smart_rocket" ); }
	void GuideView( BOOL on );
	EHANDLE m_hTarget, m_hViewOwner;
	Vector m_vecLastView;
	BOOL m_bViewSet;
};
LINK_ENTITY_TO_CLASS( dumb_rocket, CNFRocket )
LINK_ENTITY_TO_CLASS( smart_rocket, CNFRocket )
TYPEDESCRIPTION CNFRocket::m_SaveData[] = { DEFINE_FIELD( CNFRocket, m_hTarget, FIELD_EHANDLE ), DEFINE_FIELD( CNFRocket, m_hViewOwner, FIELD_EHANDLE ), DEFINE_FIELD( CNFRocket, m_vecLastView, FIELD_VECTOR ) };
IMPLEMENT_SAVERESTORE( CNFRocket, CGrenade )

void CNFRocket::Precache( void )
{
	PRECACHE_MODEL( "models/w_rocket.mdl" );
	PRECACHE_SOUND( "weapons/rocket1.wav" );
	PRECACHE_SOUND( "weapons/grenade_explode.wav" );
}
void CNFRocket::Spawn( void )
{
	Precache();
	SET_MODEL( edict(), "models/w_rocket.mdl" );
	UTIL_SetSize( pev, g_vecZero, g_vecZero );
	pev->movetype = MOVETYPE_FLY;
	pev->solid = SOLID_BBOX;
	pev->dmg = NF_SkillValue( "sk_plr_rocket" );
	pev->dmgtime = gpGlobals->time + 10;
	UTIL_MakeVectors( pev->angles );
	pev->velocity = gpGlobals->v_forward * 550;
	SetTouch( &CNFRocket::RocketTouch );
	SetThink( &CNFRocket::FlyThink );
	pev->nextthink = gpGlobals->time + 0.1f;
	m_bViewSet = FALSE;
	if( Guided() ) GuideView( TRUE );
	EMIT_SOUND( edict(), CHAN_VOICE, "weapons/rocket1.wav", 1, 0.5f );
	MESSAGE_BEGIN( MSG_BROADCAST, SVC_TEMPENTITY );
		WRITE_BYTE( TE_BEAMFOLLOW ); WRITE_SHORT( entindex() ); WRITE_SHORT( g_sModelIndexSmoke );
		WRITE_BYTE( 10 ); WRITE_BYTE( 3 ); WRITE_BYTE( 220 ); WRITE_BYTE( 220 ); WRITE_BYTE( 220 ); WRITE_BYTE( 180 );
	MESSAGE_END();
}
// retail CSmartRocket::IgniteThink 0x420e6d00: the owner's view is switched to the rocket
void CNFRocket::GuideView( BOOL on )
{
	// NF_Explode clears pev->owner, so the player is remembered separately
	CBaseEntity *owner = on ? ( pev->owner ? CBaseEntity::Instance( pev->owner ) : NULL ) : (CBaseEntity *)m_hViewOwner;
	if( !owner || !owner->IsPlayer() ) return;
	if( on )
	{
		m_hViewOwner = owner;
		SET_VIEW( owner->edict(), edict() );
		m_vecLastView = owner->pev->v_angle;
		m_bViewSet = TRUE;
	}
	else if( m_bViewSet )
	{
		SET_VIEW( owner->edict(), owner->edict() );
		m_bViewSet = FALSE;
	}
}
void CNFRocket::UpdateOnRemove( void )
{
	GuideView( FALSE );
	STOP_SOUND( edict(), CHAN_VOICE, "weapons/rocket1.wav" );
	CGrenade::UpdateOnRemove();
}
void CNFRocket::RocketTouch( CBaseEntity *other )
{
	if( other->edict() == pev->owner ) return;
	STOP_SOUND( edict(), CHAN_VOICE, "weapons/rocket1.wav" );
	GuideView( FALSE );
	TraceResult trace = UTIL_GetGlobalTrace();
	NF_Explode( this, &trace, DMG_BLAST );
}
void CNFRocket::FlyThink( void )
{
	// not IsInWorld(): its +-4096 box is smaller than some Nightfire maps; outside the map the contents are solid
	if( UTIL_PointContents( pev->origin ) == CONTENTS_SOLID || gpGlobals->time >= pev->dmgtime ) { UTIL_Remove( this ); return; }
	CBaseEntity *owner = pev->owner ? CBaseEntity::Instance( pev->owner ) : NULL;
	if( Guided() && owner && owner->IsPlayer() && owner->IsAlive() )
	{
		// retail CSmartRocket::FollowThink 0x420e6960: the owner steers. The turn rate is the owner's view change
		// times 1.5 (the retail movement-key steering, 30 deg/s, is not ported: user wants mouse only); the speed stays 550.
		Vector view = owner->pev->v_angle, delta = ( view - m_vecLastView ) * 1.5f;
		m_vecLastView = view;
		for( int i = 0; i < 2; i++ )
		{
			while( delta[i] > 180 ) delta[i] -= 360;
			while( delta[i] < -180 ) delta[i] += 360;
		}
		if( fabs( delta.x ) > 100 ) delta.x = 0;
		if( fabs( delta.y ) > 100 ) delta.y = 0;
		// the retail factor is read as "1.5 times the view change per think": degrees per second = change * 1.5 / 0.1 [assumed]
		pev->avelocity = Vector( delta.x * 10.0f, delta.y * 10.0f, 0 );
		UTIL_MakeVectors( pev->angles );
		pev->velocity = gpGlobals->v_forward * 550;
		owner->pev->velocity.x = owner->pev->velocity.y = 0;
	}
	else if( Guided() )
		GuideView( FALSE );
	pev->nextthink = gpGlobals->time + 0.1f;
}

class CNFLauncher : public CBasePlayerWeapon
{
public:
	BOOL Rocket( void ) { return m_iId == NF_WEAPON_ROCKETLAUNCHER; }
	const char *Name( void ) { return Rocket() ? "rocketlauncher" : "grenadelauncher"; }
	const char *Ammo( void ) { return Rocket() ? "rocket" : "launchgren"; }
	int Clip( void ) { return Rocket() ? 4 : 6; }
	void Spawn( void );
	void Precache( void );
	int GetItemInfo( ItemInfo *info );
	int iItemSlot( void ) { return 5; }
	int AddToPlayer( CBasePlayer *player );
	BOOL Deploy( void );
	void PrimaryAttack( void ) { Fire( FALSE ); }
	void SecondaryAttack( void ) { Fire( TRUE ); }
	void Fire( BOOL alternate );
	void Reload( void );
	void WeaponIdle( void );
	BOOL UseDecrement( void ) { return FALSE; }
	int Save( CSave &save ); int Restore( CRestore &restore ); static TYPEDESCRIPTION m_SaveData[];
};
TYPEDESCRIPTION CNFLauncher::m_SaveData[] =
{
	DEFINE_FIELD( CNFLauncher, m_flNextPrimaryAttack, FIELD_TIME ),
	DEFINE_FIELD( CNFLauncher, m_flNextSecondaryAttack, FIELD_TIME ),
	DEFINE_FIELD( CNFLauncher, m_flTimeWeaponIdle, FIELD_TIME ),
	DEFINE_FIELD( CNFLauncher, m_fInReload, FIELD_BOOLEAN ),
};
IMPLEMENT_SAVERESTORE( CNFLauncher, CBasePlayerWeapon )
LINK_ENTITY_TO_CLASS( weapon_rocketlauncher, CNFLauncher )
LINK_ENTITY_TO_CLASS( weapon_grenadelauncher, CNFLauncher )
void CNFLauncher::Spawn( void )
{
	m_iId = FClassnameIs( pev, "weapon_rocketlauncher" ) ? NF_WEAPON_ROCKETLAUNCHER : NF_WEAPON_GRENADELAUNCHER;
	Precache();
	SET_MODEL( edict(), UTIL_VarArgs( "models/w_%s.mdl", Name() ));
	m_iDefaultAmmo = Clip();
	FallInit();
}
void CNFLauncher::Precache( void )
{
	if( !m_iId ) m_iId = FClassnameIs( pev, "weapon_rocketlauncher" ) ? NF_WEAPON_ROCKETLAUNCHER : NF_WEAPON_GRENADELAUNCHER;
	PRECACHE_MODEL( UTIL_VarArgs( "models/v_%s.mdl", Name() ));
	PRECACHE_MODEL( UTIL_VarArgs( "models/p_%s.mdl", Name() ));
	PRECACHE_MODEL( UTIL_VarArgs( "models/w_%s.mdl", Name() ));
	PRECACHE_SOUND( "weapons/grenade_launcher_fire.wav" );
	UTIL_PrecacheOther( Rocket() ? "dumb_rocket" : "frag_grenade" );
	if( Rocket() ) UTIL_PrecacheOther( "smart_rocket" );
	else PRECACHE_MODEL( "models/w_grenade_projectile.mdl" );
}
int CNFLauncher::GetItemInfo( ItemInfo *info )
{
	info->pszName = STRING( pev->classname ); info->pszAmmo1 = Ammo();
	info->iMaxAmmo1 = Rocket() ? 16 : 12; info->pszAmmo2 = NULL; info->iMaxAmmo2 = -1;
	info->iMaxClip = Clip(); info->iSlot = 4; info->iPosition = Rocket() ? 4 : 3;
	info->iId = m_iId; info->iFlags = 0; info->iWeight = 20;
	return 1;
}
int CNFLauncher::AddToPlayer( CBasePlayer *player )
{
	if( !CBasePlayerWeapon::AddToPlayer( player )) return FALSE;
	MESSAGE_BEGIN( MSG_ONE, gmsgWeapPickup, NULL, player->pev ); WRITE_BYTE( m_iId ); MESSAGE_END();
	return TRUE;
}
BOOL CNFLauncher::Deploy( void )
{
	return DefaultDeploy( Rocket() ? "models/v_rocketlauncher.mdl" : "models/v_grenadelauncher.mdl", Rocket() ? "models/p_rocketlauncher.mdl" : "models/p_grenadelauncher.mdl", 4, Rocket() ? "rpg" : "grenadelauncher" );
}
void CNFLauncher::Reload( void )
{
	DefaultReload( Clip(), 3, Rocket() ? 3.63f : 3.6f );
}
void CNFLauncher::WeaponIdle( void )
{
	ResetEmptySound();
	if( m_flTimeWeaponIdle > gpGlobals->time ) return;
	SendWeaponAnim( 0 ); m_flTimeWeaponIdle = gpGlobals->time + 3;
}
void CNFLauncher::Fire( BOOL alternate )
{
	if( m_pPlayer->pev->waterlevel == 3 || m_iClip <= 0 )
	{
		PlayEmptySound(); m_flNextPrimaryAttack = m_flNextSecondaryAttack = gpGlobals->time + 0.2f; return;
	}
	UTIL_MakeVectors( m_pPlayer->pev->v_angle + m_pPlayer->pev->punchangle );
	Vector forward = gpGlobals->v_forward;
	Vector origin = m_pPlayer->GetGunPosition() + forward * 16;
	if( Rocket() )
	{
		// AT-420 Sentinel: primary = guided missile (the player steers it, see CNFRocket::FlyThink), secondary = unguided
		// [mi6-hq.com/sections/nfgame/w_at420; the retail vtable has the guided launch in slot 104]
		CBaseEntity *flying = NULL;
		while(( flying = UTIL_FindEntityByClassname( flying, "smart_rocket" )) != NULL )
			if( flying->pev->owner == m_pPlayer->edict() ) return;
		CNFRocket *rocket = (CNFRocket *)CBaseEntity::Create( alternate ? "dumb_rocket" : "smart_rocket", origin, m_pPlayer->pev->v_angle, m_pPlayer->edict() );
		if( !rocket ) return;
		if( NF_DEBUG( NF_DBG_WEAPONS )) ALERT( at_console, "nf_debug: launcher %s target 0 clip %d\n", alternate ? "dumb_rocket" : "smart_rocket", m_iClip - 1 );
	}
	else
	{
		if( !NF_LaunchGrenade( m_pPlayer->pev, origin, forward * 800, alternate )) return;
		if( NF_DEBUG( NF_DBG_WEAPONS )) ALERT( at_console, "nf_debug: launcher frag_grenade mode %s clip %d\n", alternate ? "timed" : "contact", m_iClip - 1 );
		m_pPlayer->pev->velocity = m_pPlayer->pev->velocity - forward * 96;
	}
	--m_iClip;
	SendWeaponAnim( 2 );
	// the rocket plays its own flight sound and stops it on impact; a copy on the player's channel was never stopped
	if( !Rocket() ) EMIT_SOUND( m_pPlayer->edict(), CHAN_WEAPON, "weapons/grenade_launcher_fire.wav", 1, ATTN_NORM );
	m_pPlayer->m_iWeaponVolume = Rocket() ? 1280 : 768;
	m_pPlayer->m_iWeaponFlash = BRIGHT_GUN_FLASH;
	m_pPlayer->SetAnimation( PLAYER_ATTACK1 );
	m_flNextPrimaryAttack = m_flNextSecondaryAttack = gpGlobals->time + ( Rocket() ? 0.93f : 1 );
	m_flTimeWeaponIdle = gpGlobals->time + 1;
}

class CNFLauncherAmmo : public CBasePlayerAmmo
{
public:
	BOOL Rocket( void ) { return FClassnameIs( pev, "ammo_rocketlauncher" ); }
	void Spawn( void ) { Precache(); SET_MODEL( edict(), Rocket() ? "models/w_ammo_rocketlauncher.mdl" : "models/w_ammo_grenadelauncher.mdl" ); CBasePlayerAmmo::Spawn(); }
	void Precache( void ) { PRECACHE_MODEL( Rocket() ? "models/w_ammo_rocketlauncher.mdl" : "models/w_ammo_grenadelauncher.mdl" ); PRECACHE_SOUND( "items/9mmclip1.wav" ); }
	BOOL AddAmmo( CBaseEntity *other )
	{
		if( other->GiveAmmo( Rocket() ? 4 : 6, Rocket() ? "rocket" : "launchgren", Rocket() ? 16 : 12 ) == -1 ) return FALSE;
		EMIT_SOUND( edict(), CHAN_ITEM, "items/9mmclip1.wav", 1, ATTN_NORM ); return TRUE;
	}
};
LINK_ENTITY_TO_CLASS( ammo_rocketlauncher, CNFLauncherAmmo )
LINK_ENTITY_TO_CLASS( ammo_grenadelauncher, CNFLauncherAmmo )

BOOL NF_ExplosivesCommand( CBaseEntity *player, const char *command )
{
	if( !FStrEq( command, "nf_explosivesinfo" )) return FALSE;
	if( !NF_DEBUG( NF_DBG_WEAPONS )) return TRUE;
	const char *classes[] = { "dumb_rocket", "smart_rocket", "frag_grenade", "flash_grenade", "smoke_grenade", "character_bondmine", "enemy_ronin_turret" };
	for( int i = 0; i < ARRAYSIZE( classes ); ++i )
	{
		CBaseEntity *entity = NULL; int count = 0;
		while(( entity = UTIL_FindEntityByClassname( entity, classes[i] )) != NULL )
		{
			++count;
			ALERT( at_console, "nf_debug: explosive class %s id %d owner %d state %d mode %d health %.0f remaining %.2f origin %.0f %.0f %.0f\n",
				classes[i], entity->entindex(), entity->pev->owner ? ENTINDEX( entity->pev->owner ) : 0,
				entity->pev->iuser1, entity->pev->iuser2, entity->pev->health, Q_max( 0.0f, entity->pev->dmgtime - gpGlobals->time ),
				entity->pev->origin.x, entity->pev->origin.y, entity->pev->origin.z );
		}
		ALERT( at_console, "nf_debug: explosive count %s %d\n", classes[i], count );
	}
	return TRUE;
}
