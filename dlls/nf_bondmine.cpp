/* Nightfire laser/proximity Bond mines, including authored character_bondmine. */
#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "player.h"
#include "effects.h"
#include "nf_weapons.h"
#include "nf_explosives.h"
#include "nf_debug.h"

class CNFBondMine : public CGrenade
{
public:
	void Spawn( void );
	void Precache( void );
	void EXPORT MineThink( void );
	void EXPORT ExplodeThink( void );
	void Killed( entvars_t *attacker, int gib );
	void UpdateOnRemove( void );
	void KeyValue( KeyValueData *data );
	int Save( CSave &save ); int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];
	BOOL m_fProximity;
	EHANDLE m_hSupport, m_hBeam;
	Vector m_vecSupportOrigin, m_vecSupportAngles;
	float m_flLength;
	int m_iState;
	BOOL m_fHadSupport;
};
LINK_ENTITY_TO_CLASS( character_bondmine, CNFBondMine )
TYPEDESCRIPTION CNFBondMine::m_SaveData[] =
{
	DEFINE_FIELD( CNFBondMine, m_fProximity, FIELD_BOOLEAN ),
	DEFINE_FIELD( CNFBondMine, m_hSupport, FIELD_EHANDLE ),
	DEFINE_FIELD( CNFBondMine, m_hBeam, FIELD_EHANDLE ),
	DEFINE_FIELD( CNFBondMine, m_vecSupportOrigin, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( CNFBondMine, m_vecSupportAngles, FIELD_VECTOR ),
	DEFINE_FIELD( CNFBondMine, m_flLength, FIELD_FLOAT ),
	DEFINE_FIELD( CNFBondMine, m_iState, FIELD_INTEGER ),
	DEFINE_FIELD( CNFBondMine, m_fHadSupport, FIELD_BOOLEAN ),
};
int CNFBondMine::Save( CSave &save )
{
	if( !CGrenade::Save( save )) return 0;
	return save.WriteFields( "CNFBondMine", this, m_SaveData, ARRAYSIZE( m_SaveData ));
}
int CNFBondMine::Restore( CRestore &restore )
{
	if( !CGrenade::Restore( restore ) || !restore.ReadFields( "CNFBondMine", this, m_SaveData, ARRAYSIZE( m_SaveData ))) return 0;
	pev->iuser1 = m_iState;
	pev->iuser2 = m_fProximity;
	return 1;
}
void CNFBondMine::KeyValue( KeyValueData *data )
{
	if( FStrEq( data->szKeyName, "proximity" )) { m_fProximity = atoi( data->szValue ) != 0; data->fHandled = TRUE; }
	else CGrenade::KeyValue( data );
}
void CNFBondMine::Precache( void )
{
	PRECACHE_MODEL( "models/w_tripmine.mdl" );
	PRECACHE_MODEL( "sprites/dustyred.spz" );
	PRECACHE_SOUND( "weapons/mine_deploy.wav" ); PRECACHE_SOUND( "weapons/mine_charge.wav" );
	PRECACHE_SOUND( "weapons/mine_activate.wav" ); PRECACHE_SOUND( "weapons/grenade_explode.wav" );
	PRECACHE_SOUND( "misc/new_message.wav" );
}
void CNFBondMine::Spawn( void )
{
	Precache();
	SET_MODEL( edict(), "models/w_tripmine.mdl" );
	UTIL_SetSize( pev, Vector( -8, -8, 0 ), Vector( 8, 8, 16 ));
	pev->movetype = MOVETYPE_NONE; pev->solid = SOLID_NOT;
	pev->takedamage = DAMAGE_YES; pev->health = 1;
	pev->dmg = NF_SkillValue( "sk_plr_tripmine" );
	pev->dmgtime = gpGlobals->time + (( pev->spawnflags & 1 ) ? 1 : 2.5f );
	SetThink( &CNFBondMine::MineThink ); pev->nextthink = gpGlobals->time + 0.1f;
	EMIT_SOUND( edict(), CHAN_BODY, "weapons/mine_charge.wav", 0.2f, ATTN_NORM );
}
void CNFBondMine::UpdateOnRemove( void )
{
	if( m_hBeam ) UTIL_Remove( m_hBeam );
	STOP_SOUND( edict(), CHAN_BODY, "weapons/mine_charge.wav" );
	CGrenade::UpdateOnRemove();
}
void CNFBondMine::Killed( entvars_t *attacker, int gib )
{
	if( m_iState == 2 ) return;
	m_iState = 2; pev->iuser1 = 2; pev->takedamage = DAMAGE_NO;
	if( m_hBeam ) { UTIL_Remove( m_hBeam ); m_hBeam = NULL; }
	SetThink( &CNFBondMine::ExplodeThink ); pev->nextthink = gpGlobals->time + 0.1f;
	if( NF_DEBUG( NF_DBG_WEAPONS )) ALERT( at_console, "nf_debug: bondmine triggered mode %s\n", m_fProximity ? "proximity" : "laser" );
}
void CNFBondMine::ExplodeThink( void )
{
	TraceResult trace;
	UTIL_TraceLine( pev->origin, pev->origin + Vector( 0, 0, -32 ), ignore_monsters, edict(), &trace );
	NF_Explode( this, &trace, DMG_BLAST );
}
void CNFBondMine::MineThink( void )
{
	UTIL_MakeAimVectors( pev->angles );
	Vector direction = gpGlobals->v_forward;
	TraceResult trace;
	if( !m_iState )
	{
		if( gpGlobals->time < pev->dmgtime ) { pev->nextthink = gpGlobals->time + 0.1f; return; }
		// Support uses a handle rather than pev->owner: the latter is damage attribution.
		UTIL_TraceLine( pev->origin + direction * 8, pev->origin - direction * 32, ignore_monsters, edict(), &trace );
		if( trace.flFraction < 1 && trace.pHit )
		{
			m_hSupport = CBaseEntity::Instance( trace.pHit );
			if( m_hSupport ) { m_vecSupportOrigin = m_hSupport->pev->origin; m_vecSupportAngles = m_hSupport->pev->angles; }
		}
		UTIL_TraceLine( pev->origin + direction * 8, pev->origin + direction * 2048, dont_ignore_monsters, edict(), &trace );
		m_flLength = trace.flFraction;
		if( !m_fProximity )
		{
			CBeam *beam = CBeam::BeamCreate( "sprites/dustyred.spz", 2 );
			if( beam ) { beam->PointsInit( pev->origin + direction * 8, trace.vecEndPos ); beam->SetColor( 255, 64, 64 ); beam->SetBrightness( 64 ); m_hBeam = beam; }
		}
		m_fHadSupport = m_hSupport != 0;
		m_iState = 1; pev->iuser1 = 1; pev->iuser2 = m_fProximity; pev->solid = SOLID_BBOX;
		EMIT_SOUND( edict(), CHAN_WEAPON, "weapons/mine_activate.wav", 0.5f, ATTN_NORM );
		if( NF_DEBUG( NF_DBG_WEAPONS )) ALERT( at_console, "nf_debug: bondmine armed mode %s\n", m_fProximity ? "proximity" : "laser" );
	}
	else
	{
		if( ( m_fHadSupport && !m_hSupport ) || ( m_hSupport && ( m_hSupport->pev->origin != m_vecSupportOrigin || m_hSupport->pev->angles != m_vecSupportAngles ))) { Killed( pev, GIB_NEVER ); return; }
		if( m_fProximity )
		{
			CBaseEntity *candidate = NULL;
			while(( candidate = UTIL_FindEntityInSphere( candidate, pev->origin, 128 )) != NULL )
			{
				if( !candidate->IsAlive() || !( candidate->pev->flags & ( FL_CLIENT | FL_MONSTER )) || candidate->edict() == pev->owner ) continue;
				UTIL_TraceLine( pev->origin + direction * 8, candidate->BodyTarget( pev->origin ), dont_ignore_monsters, edict(), &trace );
				if( trace.flFraction == 1 || trace.pHit == candidate->edict() ) { Killed( pev, GIB_NEVER ); return; }
			}
		}
		else
		{
			UTIL_TraceLine( pev->origin + direction * 8, pev->origin + direction * 2048, dont_ignore_monsters, edict(), &trace );
			if( fabs( trace.flFraction - m_flLength ) > 0.001f ) { Killed( pev, GIB_NEVER ); return; }
		}
	}
	pev->nextthink = gpGlobals->time + 0.1f;
}

class CNFBondMineWeapon : public CBasePlayerWeapon
{
public:
	void Spawn( void ); void Precache( void );
	int GetItemInfo( ItemInfo *info ); int iItemSlot( void ) { return 5; }
	int AddToPlayer( CBasePlayer *player );
	BOOL Deploy( void ) { return DefaultDeploy( "models/v_tripmine.mdl", "models/p_tripmine.mdl", m_fireState ? 12 : 5, "trip" ); }
	void PrimaryAttack( void );
	void SecondaryAttack( void ) { m_fireState = !m_fireState; SendWeaponAnim( m_fireState ? 11 : 4 ); m_flNextPrimaryAttack = m_flNextSecondaryAttack = gpGlobals->time + 1; }
	void WeaponIdle( void ) { if( m_flTimeWeaponIdle <= gpGlobals->time ) { SendWeaponAnim( m_fireState ? 7 : 0 ); m_flTimeWeaponIdle = gpGlobals->time + 3; } }
	BOOL UseDecrement( void ) { return FALSE; }
	int Save( CSave &save ); int Restore( CRestore &restore ); static TYPEDESCRIPTION m_SaveData[];
};
TYPEDESCRIPTION CNFBondMineWeapon::m_SaveData[] =
{
	DEFINE_FIELD( CNFBondMineWeapon, m_fireState, FIELD_INTEGER ),
	DEFINE_FIELD( CNFBondMineWeapon, m_flNextPrimaryAttack, FIELD_TIME ),
	DEFINE_FIELD( CNFBondMineWeapon, m_flNextSecondaryAttack, FIELD_TIME ),
	DEFINE_FIELD( CNFBondMineWeapon, m_flTimeWeaponIdle, FIELD_TIME ),
};
IMPLEMENT_SAVERESTORE( CNFBondMineWeapon, CBasePlayerWeapon )
LINK_ENTITY_TO_CLASS( weapon_bondmine, CNFBondMineWeapon )
LINK_ENTITY_TO_CLASS( ammo_bondmine, CNFBondMineWeapon )
void CNFBondMineWeapon::Spawn( void )
{
	Precache(); m_iId = NF_WEAPON_BONDMINE;
	BOOL ammo = FClassnameIs( pev, "ammo_bondmine" );
	m_iDefaultAmmo = ammo ? 2 : 1;
	SET_MODEL( edict(), ammo ? "models/w_ammo_tripmine.mdl" : "models/w_tripmine.mdl" );
	pev->classname = MAKE_STRING( "weapon_bondmine" ); FallInit();
}
void CNFBondMineWeapon::Precache( void )
{
	PRECACHE_MODEL( "models/v_tripmine.mdl" ); PRECACHE_MODEL( "models/p_tripmine.mdl" );
	PRECACHE_MODEL( "models/w_tripmine.mdl" ); PRECACHE_MODEL( "models/w_ammo_tripmine.mdl" );
	UTIL_PrecacheOther( "character_bondmine" );
}
int CNFBondMineWeapon::GetItemInfo( ItemInfo *info )
{
	info->pszName = "weapon_bondmine"; info->pszAmmo1 = "bondmine"; info->iMaxAmmo1 = 5;
	info->pszAmmo2 = NULL; info->iMaxAmmo2 = -1; info->iMaxClip = WEAPON_NOCLIP;
	info->iId = m_iId = NF_WEAPON_BONDMINE; info->iSlot = 4; info->iPosition = 5;
	info->iFlags = ITEM_FLAG_LIMITINWORLD | ITEM_FLAG_EXHAUSTIBLE; info->iWeight = -10; return 1;
}
int CNFBondMineWeapon::AddToPlayer( CBasePlayer *player )
{
	if( !CBasePlayerWeapon::AddToPlayer( player )) return FALSE;
	MESSAGE_BEGIN( MSG_ONE, gmsgWeapPickup, NULL, player->pev ); WRITE_BYTE( m_iId ); MESSAGE_END(); return TRUE;
}
void CNFBondMineWeapon::PrimaryAttack( void )
{
	m_flNextPrimaryAttack = gpGlobals->time + 0.3f;
	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0 ) return;
	UTIL_MakeVectors( m_pPlayer->pev->v_angle );
	TraceResult trace;
	Vector start = m_pPlayer->GetGunPosition();
	UTIL_TraceLine( start, start + gpGlobals->v_forward * 128, dont_ignore_monsters, m_pPlayer->edict(), &trace );
	CBaseEntity *support = trace.pHit ? CBaseEntity::Instance( trace.pHit ) : NULL;
	if( trace.flFraction == 1 || trace.fStartSolid || !support || support->IsPlayer() || support->MyMonsterPointer() ) return;
	CNFBondMine *mine = (CNFBondMine *)CBaseEntity::Create( "character_bondmine", trace.vecEndPos + trace.vecPlaneNormal * 8, UTIL_VecToAngles( trace.vecPlaneNormal ), m_pPlayer->edict() );
	if( !mine ) return;
	mine->m_fProximity = m_fireState != 0;
	mine->m_hSupport = support; mine->m_vecSupportOrigin = support->pev->origin; mine->m_vecSupportAngles = support->pev->angles;
	--m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType];
	SendWeaponAnim( m_fireState ? 10 : 3 ); m_pPlayer->SetAnimation( PLAYER_ATTACK1 );
	m_flNextPrimaryAttack = m_flNextSecondaryAttack = gpGlobals->time + 1;
	m_flTimeWeaponIdle = gpGlobals->time + 1;
	if( NF_DEBUG( NF_DBG_WEAPONS )) ALERT( at_console, "nf_debug: bondmine placed mode %s ammo %d\n", m_fireState ? "proximity" : "laser", m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] );
	if( !m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] ) RetireWeapon();
}
