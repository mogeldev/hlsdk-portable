/* Retail UP11 darts and laser rifle. Server-authoritative moving projectiles. */
#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "weapons.h"
#include "player.h"
#include "effects.h"
#include "items.h"
#include "nf_weapons.h"
#include "nf_debug.h"

extern void RadiusDamage( Vector src, entvars_t *inflictor, entvars_t *attacker,
	float damage, float radius, int ignoreClass, int damageType );

class CNightfireSpaceSuit : public CItem
{
public:
	void Precache( void )
	{
		if( !FStringNull( pev->model )) PRECACHE_MODEL( STRING( pev->model ));
		else PRECACHE_MODEL( "models/hanging_spacesuit.mdl" );
		PRECACHE_SOUND( "player/armor_vest.wav" );
	}
	void Spawn( void )
	{
		Precache();
		SET_MODEL( edict(), FStringNull( pev->model ) ? "models/hanging_spacesuit.mdl" : STRING( pev->model ));
		CItem::Spawn();
		UTIL_SetSize( pev, Vector( -10, -10, 0 ), Vector( 10, 10, 32 ));
	}
	BOOL MyTouch( CBasePlayer *player )
	{
		if( player->m_fNFSpaceSuit || player->pev->deadflag != DEAD_NO ) return FALSE;
		player->m_fNFSpaceSuit = TRUE;
		player->pev->weapons |= ( 1 << WEAPON_SUIT );
		player->GiveNamedItem( "weapon_laserrifle" );
		player->SelectItem( "weapon_laserrifle" );
		EMIT_SOUND( player->edict(), CHAN_ITEM, "player/armor_vest.wav", 1, ATTN_NORM );
		if( NF_DEBUG( NF_DBG_ITEMS )) ALERT( at_console, "nf_debug: space suit player %d equipped 1\n", player->entindex() );
		return TRUE;
	}
};
LINK_ENTITY_TO_CLASS( item_space_suit, CNightfireSpaceSuit )

class CNightfireProjectile : public CBaseEntity
{
public:
	void Spawn( void );
	void Precache( void );
	void EXPORT Impact( CBaseEntity *other );
	void EXPORT Flight( void );
	void EXPORT Expire( void ) { UTIL_Remove( this ); }
	int Save( CSave &save );
	int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];
private:
	Vector m_vecPrevious;
	float m_flExpire;
};
TYPEDESCRIPTION CNightfireProjectile::m_SaveData[] =
{
	DEFINE_FIELD( CNightfireProjectile, m_vecPrevious, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( CNightfireProjectile, m_flExpire, FIELD_TIME ),
};
IMPLEMENT_SAVERESTORE( CNightfireProjectile, CBaseEntity )
LINK_ENTITY_TO_CLASS( up11_dart, CNightfireProjectile )
LINK_ENTITY_TO_CLASS( laser_beam, CNightfireProjectile )
LINK_ENTITY_TO_CLASS( ei_grenade, CNightfireProjectile )

void CNightfireProjectile::Precache( void )
{
	PRECACHE_MODEL( "models/w_up11_dart.mdl" );
	PRECACHE_MODEL( "models/w_laserbeam.mdl" );
	PRECACHE_MODEL( "sprites/energyball.spz" );
	PRECACHE_SOUND( "gadgets/grapple_hit.wav" );
	PRECACHE_SOUND( "weapons/laser_grenade1.wav" );
}

void CNightfireProjectile::Spawn( void )
{
	Precache();
	pev->movetype = MOVETYPE_FLY;
	pev->solid = SOLID_BBOX;
	BOOL dart = FClassnameIs( pev, "up11_dart" );
	BOOL grenade = FClassnameIs( pev, "ei_grenade" );
	SET_MODEL( edict(), dart ? "models/w_up11_dart.mdl" : grenade ? "sprites/energyball.spz" : "models/w_laserbeam.mdl" );
	UTIL_SetSize( pev, g_vecZero, g_vecZero );
	if( !dart )
	{
		pev->rendermode = kRenderTransAdd;
		pev->renderamt = 255;
		pev->scale = grenade ? 1.0f : 0.5f;
		pev->effects |= EF_FULLBRIGHT;
	}
	SetTouch( &CNightfireProjectile::Impact );
	m_vecPrevious = pev->origin;
	m_flExpire = gpGlobals->time + ( dart ? 10.0f : 2.0f );
	SetThink( &CNightfireProjectile::Flight );
	pev->nextthink = gpGlobals->time + 0.01f;
}

void CNightfireProjectile::Flight( void )
{
	if( gpGlobals->time >= m_flExpire || UTIL_PointContents( pev->origin ) == CONTENTS_SKY )
	{
		UTIL_Remove( this );
		return;
	}
	TraceResult tr;
	UTIL_TraceLine( m_vecPrevious, pev->origin, dont_ignore_monsters, pev->owner, &tr );
	if( tr.flFraction < 1 )
	{
		UTIL_SetOrigin( pev, tr.vecEndPos );
		CBaseEntity *other = CBaseEntity::Instance( tr.pHit );
		if( other ) { Impact( other ); return; }
	}
	m_vecPrevious = pev->origin;
	pev->nextthink = gpGlobals->time + 0.01f;
}

void CNightfireProjectile::Impact( CBaseEntity *other )
{
	if( other->edict() == pev->owner ) return;
	SetTouch( NULL );
	entvars_t *attacker = pev->owner ? VARS( pev->owner ) : pev;
	BOOL grenade = FClassnameIs( pev, "ei_grenade" );
	BOOL dart = FClassnameIs( pev, "up11_dart" );
	if( grenade )
	{
		EMIT_SOUND( edict(), CHAN_WEAPON, "weapons/laser_grenade1.wav", 0.55f, ATTN_NORM );
		RadiusDamage( pev->origin, pev, attacker, pev->dmg, pev->dmg * 2.5f, CLASS_NONE, DMG_BLAST );
		UTIL_Sparks( pev->origin );
	}
	else if( other->pev->takedamage != DAMAGE_NO )
	{
		TraceResult tr = UTIL_GetGlobalTrace();
		ClearMultiDamage();
		other->TraceAttack( attacker, pev->dmg, pev->velocity.Normalize(), &tr, dart ? DMG_BULLET : DMG_ENERGYBEAM | DMG_NEVERGIB );
		ApplyMultiDamage( pev, attacker );
	}
	else if( dart )
	{
		pev->solid = SOLID_NOT;
		pev->velocity = g_vecZero;
		pev->movetype = MOVETYPE_NONE;
		SetThink( &CNightfireProjectile::Expire );
		pev->nextthink = gpGlobals->time + 10;
		EMIT_SOUND( edict(), CHAN_WEAPON, "gadgets/grapple_hit.wav", 1, ATTN_NORM );
		return;
	}
	if( NF_DEBUG( NF_DBG_WEAPONS ))
		ALERT( at_console, "nf_debug: projectile hit class %s target %s damage %.1f\n", STRING( pev->classname ), STRING( other->pev->classname ), pev->dmg );
	UTIL_Remove( this );
}

static void NF_ShootProjectile( CBasePlayer *player, const char *name, float speed, float damage )
{
	UTIL_MakeVectors( player->pev->v_angle + player->pev->punchangle );
	Vector direction = gpGlobals->v_forward;
	Vector src = player->GetGunPosition();
	CNightfireProjectile *p = (CNightfireProjectile *)CBaseEntity::Create( name, src, player->pev->v_angle, player->edict() );
	if( !p ) return;
	p->pev->velocity = direction * speed;
	p->pev->dmg = damage;
	p->pev->angles = UTIL_VecToAngles( direction );
}

void NF_ShootUP11( CBasePlayer *player )
{
	NF_ShootProjectile( player, "up11_dart", 2000, 100 );
}

class CNightfireLaserRifle : public CBasePlayerWeapon
{
public:
	void Spawn( void );
	void Precache( void );
	int GetItemInfo( ItemInfo *p );
	int iItemSlot( void ) { return 4; }
	BOOL Deploy( void );
	void Holster( int skiplocal = 0 );
	void PrimaryAttack( void );
	void SecondaryAttack( void );
	void ItemPostFrame( void );
	void WeaponIdle( void );
	BOOL IsUseable( void ) { return TRUE; }
	BOOL CanDeploy( void ) { return TRUE; }
	BOOL UseDecrement( void ) { return FALSE; }
	int Save( CSave &save );
	int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];
private:
	void SetBattery( void );
	void ReleaseCharge( void );
	float m_flBattery, m_flCharge, m_flLastUpdate, m_flChargeStart;
	int m_iCharging;
	BOOL m_bRecharge;
};

TYPEDESCRIPTION CNightfireLaserRifle::m_SaveData[] =
{
	DEFINE_FIELD( CNightfireLaserRifle, m_flBattery, FIELD_FLOAT ),
	DEFINE_FIELD( CNightfireLaserRifle, m_flCharge, FIELD_FLOAT ),
	DEFINE_FIELD( CNightfireLaserRifle, m_flLastUpdate, FIELD_TIME ),
	DEFINE_FIELD( CNightfireLaserRifle, m_flChargeStart, FIELD_TIME ),
	DEFINE_FIELD( CNightfireLaserRifle, m_iCharging, FIELD_INTEGER ),
	DEFINE_FIELD( CNightfireLaserRifle, m_bRecharge, FIELD_BOOLEAN ),
};
IMPLEMENT_SAVERESTORE( CNightfireLaserRifle, CBasePlayerWeapon )
LINK_ENTITY_TO_CLASS( weapon_laserrifle, CNightfireLaserRifle )

void CNightfireLaserRifle::Precache( void )
{
	PRECACHE_MODEL( "models/v_laserrifle.mdl" );
	PRECACHE_MODEL( "models/p_laserrifle.mdl" );
	PRECACHE_MODEL( "models/w_laserrifle.mdl" );
	UTIL_PrecacheOther( "laser_beam" );
	UTIL_PrecacheOther( "ei_grenade" );
	PRECACHE_SOUND( "weapons/laser_draw.wav" );
	PRECACHE_SOUND( "weapons/laser_grenade1.wav" );
	PRECACHE_SOUND( "weapons/laser_grenade2.wav" );
}

void CNightfireLaserRifle::Spawn( void )
{
	Precache();
	m_iId = NF_WEAPON_LASERRIFLE;
	m_iClip = WEAPON_NOCLIP;
	m_iDefaultAmmo = 100;
	m_flBattery = 100;
	m_flLastUpdate = gpGlobals->time;
	SET_MODEL( edict(), "models/w_laserrifle.mdl" );
	FallInit();
}

int CNightfireLaserRifle::GetItemInfo( ItemInfo *p )
{
	p->pszName = STRING( pev->classname );
	p->pszAmmo1 = "battery";
	p->iMaxAmmo1 = 100;
	p->pszAmmo2 = NULL;
	p->iMaxAmmo2 = -1;
	p->iMaxClip = WEAPON_NOCLIP;
	p->iSlot = 3;
	p->iPosition = 5;
	p->iId = m_iId = NF_WEAPON_LASERRIFLE;
	p->iFlags = 0;
	p->iWeight = -1;
	return 1;
}

void CNightfireLaserRifle::SetBattery( void )
{
	if( m_iPrimaryAmmoType > 0 ) m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] = (int)m_flBattery;
}

BOOL CNightfireLaserRifle::Deploy( void )
{
	m_iCharging = 0;
	m_flLastUpdate = gpGlobals->time;
	SetBattery();
	return DefaultDeploy( "models/v_laserrifle.mdl", "models/p_laserrifle.mdl", 6, "mini" );
}

void CNightfireLaserRifle::Holster( int skiplocal )
{
	m_iCharging = 0;
	m_flCharge = 0;
	CBasePlayerWeapon::Holster( skiplocal );
}

void CNightfireLaserRifle::PrimaryAttack( void )
{
	if( m_iCharging ) return;
	if( m_flBattery <= 0 ) { m_bRecharge = TRUE; return; }
	float damage = NF_SkillValue( "sk_plr_laser" );
	NF_ShootProjectile( m_pPlayer, "laser_beam", 1500, damage > 0 ? damage : 20 );
	m_flBattery = max( 0.0f, m_flBattery - 5 );
	SetBattery();
	SendWeaponAnim( 2 );
	m_pPlayer->SetAnimation( PLAYER_ATTACK1 );
	m_flNextPrimaryAttack = m_flNextSecondaryAttack = gpGlobals->time + 0.25f;
	m_flTimeWeaponIdle = gpGlobals->time + 0.5f;
	if( NF_DEBUG( NF_DBG_WEAPONS )) ALERT( at_console, "nf_debug: laser fire battery %.1f speed 1500\n", m_flBattery );
}

void CNightfireLaserRifle::SecondaryAttack( void )
{
	if( m_iCharging || m_flBattery < 50 ) return;
	m_iCharging = 1;
	m_flCharge = 0;
	m_flChargeStart = m_flLastUpdate = gpGlobals->time;
	SendWeaponAnim( 3 );
	m_flNextPrimaryAttack = m_flNextSecondaryAttack = gpGlobals->time + 0.05f;
}

void CNightfireLaserRifle::ReleaseCharge( void )
{
	float damage = NF_SkillValue( "sk_plr_eigrenade" );
	NF_ShootProjectile( m_pPlayer, "ei_grenade", 800, damage > 0 ? damage : 50 );
	SendWeaponAnim( 5 );
	m_pPlayer->SetAnimation( PLAYER_ATTACK1 );
	if( NF_DEBUG( NF_DBG_WEAPONS )) ALERT( at_console, "nf_debug: laser grenade battery %.1f charge %.1f speed 800\n", m_flBattery, m_flCharge );
	m_iCharging = 0;
	m_flCharge = 0;
	m_flNextPrimaryAttack = m_flNextSecondaryAttack = gpGlobals->time + 0.09f;
	m_flTimeWeaponIdle = gpGlobals->time + 0.5f;
}

void CNightfireLaserRifle::ItemPostFrame( void )
{
	float dt = max( 0.0f, gpGlobals->time - m_flLastUpdate );
	m_flLastUpdate = gpGlobals->time;
	if( m_iCharging )
	{
		float cost = min( 20.0f - m_flCharge, min( m_flBattery, dt * 20 ) );
		m_flBattery -= cost;
		m_flCharge += cost;
		if( m_iCharging == 1 && gpGlobals->time - m_flChargeStart >= 0.26f )
		{
			m_iCharging = 2;
			SendWeaponAnim( 4 );
		}
		if( !( m_pPlayer->pev->button & IN_ATTACK2 ) || gpGlobals->time - m_flChargeStart >= 1.0f )
		{
			if( m_iCharging == 2 ) ReleaseCharge();
			else { m_iCharging = 0; m_flCharge = 0; SendWeaponAnim( 5 ); }
		}
		SetBattery();
		return;
	}
	if( m_bRecharge || !( m_pPlayer->pev->button & ( IN_ATTACK | IN_ATTACK2 )))
	{
		// Retail adds 0.5 per idle frame; normalize its 100 Hz behavior to time.
		m_flBattery = min( 100.0f, m_flBattery + dt * 50 );
		if( m_flBattery >= 100 ) m_bRecharge = FALSE;
	}
	SetBattery();
	CBasePlayerWeapon::ItemPostFrame();
}

void CNightfireLaserRifle::WeaponIdle( void )
{
	if( gpGlobals->time < m_flTimeWeaponIdle ) return;
	SendWeaponAnim( RANDOM_LONG( 0, 1 ));
	m_flTimeWeaponIdle = gpGlobals->time + RANDOM_FLOAT( 8, 16 );
}
