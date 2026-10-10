/* Nightfire melee, pen, Q-worm and Q-Specs. Retail contracts: arsenal-gadgets.md. */
#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "player.h"
#include "nf_weapons.h"
#include "nf_gadgets.h"
#include "nf_debug.h"

extern int gmsgNFVisionMode, gmsgNFCameraMode, gmsgFlashBattery;

class CNFGadget : public CBasePlayerWeapon
{
public:
	virtual int ID( void ) = 0;
	virtual const char *ViewModel( void ) = 0;
	virtual const char *WorldModel( void ) { return "models/w_kowloon.mdl"; }
	virtual const char *Ammo( void ) { return NULL; }
	virtual int Position( void ) = 0;
	virtual int DrawSequence( void ) { return 0; }
	virtual int IdleSequence( void ) { return 0; }
	void Spawn( void ) { Precache(); m_iId = ID(); m_iClip = WEAPON_NOCLIP; m_iDefaultAmmo = Ammo() ? 5 : 0; SET_MODEL( edict(), WorldModel() ); FallInit(); }
	void Precache( void ) { PRECACHE_MODEL( ViewModel() ); PRECACHE_MODEL( WorldModel() ); }
	int iItemSlot( void ) { return ID() == NF_WEAPON_DUKES ? 2 : ID() == NF_WEAPON_GLASSES ? 0 : 1; }
	int GetItemInfo( ItemInfo *p )
	{
		p->pszName = STRING( pev->classname ); p->pszAmmo1 = Ammo(); p->pszAmmo2 = NULL;
		p->iMaxAmmo1 = Ammo() ? 10 : -1; p->iMaxAmmo2 = -1; p->iMaxClip = WEAPON_NOCLIP;
		p->iSlot = ID() == NF_WEAPON_GLASSES ? 5 : iItemSlot() - 1; p->iPosition = Position(); p->iFlags = 0;
		p->iId = m_iId = ID(); p->iWeight = ID() == NF_WEAPON_DUKES ? 0 : -1; return 1;
	}
	int AddToPlayer( CBasePlayer *player )
	{
		if( !CBasePlayerWeapon::AddToPlayer( player )) return FALSE;
		MESSAGE_BEGIN( MSG_ONE, gmsgWeapPickup, NULL, player->pev ); WRITE_BYTE( m_iId ); MESSAGE_END(); return TRUE;
	}
	BOOL Deploy( void ) { m_flTimeWeaponIdle = gpGlobals->time + 1; return DefaultDeploy( ViewModel(), "", DrawSequence(), "onehanded" ); }
	BOOL IsUseable( void ) { return TRUE; }
	BOOL CanDeploy( void ) { return TRUE; }
	BOOL UseDecrement( void ) { return FALSE; }
	void WeaponIdle( void ) { if( gpGlobals->time >= m_flTimeWeaponIdle ) { SendWeaponAnim( IdleSequence() ); m_flTimeWeaponIdle = gpGlobals->time + 5; } }
};

class CNFDukes : public CNFGadget
{
public:
	int ID( void ) { return NF_WEAPON_DUKES; }
	int Position( void ) { return 0; }
	const char *ViewModel( void ) { return "models/v_dukes.mdl"; }
	const char *WorldModel( void ) { return "models/w_mp9.mdl"; }
	void Precache( void )
	{
		CNFGadget::Precache();
		for( int i = 1; i <= 3; i++ ) PRECACHE_SOUND( UTIL_VarArgs( "weapons/dukes_impact%d.wav", i ));
	}
	void PrimaryAttack( void )
	{
		UTIL_MakeVectors( m_pPlayer->pev->v_angle + m_pPlayer->pev->punchangle );
		Vector src = m_pPlayer->GetGunPosition(), end = src + gpGlobals->v_forward * 64;
		TraceResult tr;
		UTIL_TraceLine( src, end, dont_ignore_monsters, m_pPlayer->edict(), &tr );
		if( tr.flFraction == 1 ) UTIL_TraceHull( src, end, dont_ignore_monsters, head_hull, m_pPlayer->edict(), &tr );
		CBaseEntity *hit = tr.flFraction < 1 ? CBaseEntity::Instance( tr.pHit ) : NULL;
		if( hit )
		{
			ClearMultiDamage(); hit->TraceAttack( m_pPlayer->pev, 50, gpGlobals->v_forward, &tr, DMG_CLUB );
			ApplyMultiDamage( pev, m_pPlayer->pev );
			EMIT_SOUND( m_pPlayer->edict(), CHAN_WEAPON, UTIL_VarArgs( "weapons/dukes_impact%d.wav", RANDOM_LONG( 1, 3 )), 1, ATTN_NORM );
		}
		SendWeaponAnim( RANDOM_LONG( 1, 2 )); m_pPlayer->SetAnimation( PLAYER_ATTACK1 );
		m_flNextPrimaryAttack = m_flNextSecondaryAttack = gpGlobals->time + ( hit ? 0.5f : 1.0f );
		m_flTimeWeaponIdle = gpGlobals->time + 2;
		if( NF_DEBUG( NF_DBG_WEAPONS )) ALERT( at_console, "nf_debug: dukes hit %s damage 50\n", hit ? STRING( hit->pev->classname ) : "none" );
	}
	void SecondaryAttack( void ) { PrimaryAttack(); }
};
LINK_ENTITY_TO_CLASS( weapon_dukes, CNFDukes )

class CNFDart : public CBaseEntity
{
public:
	void Spawn( void )
	{
		Precache(); pev->movetype = MOVETYPE_FLY; pev->solid = SOLID_BBOX;
		SET_MODEL( edict(), "models/w_dart_tip.mdl" ); UTIL_SetSize( pev, g_vecZero, g_vecZero );
		SetTouch( &CNFDart::Hit ); SetThink( &CBaseEntity::SUB_Remove ); pev->nextthink = gpGlobals->time + 10;
	}
	void Precache( void ) { PRECACHE_MODEL( "models/w_dart_tip.mdl" ); PRECACHE_SOUND( "gadgets/grapple_hit.wav" ); }
	void EXPORT Hit( CBaseEntity *other )
	{
		if( other->edict() == pev->owner ) return;
		entvars_t *attacker = pev->owner ? VARS( pev->owner ) : pev;
		if( other->pev->takedamage != DAMAGE_NO )
		{
			// Retail dartpen: susceptible living character receives health-sized DMG_PARALYZE; other targets receive 5 bullet damage.
			BOOL character = other->IsAlive() && ( FClassnameIs( other->pev, "enemy_generic" ) || FClassnameIs( other->pev, "npc_aigeneric" ));
			other->TakeDamage( pev, attacker, character ? other->pev->health : 5, character ? DMG_PARALYZE : DMG_BULLET );
			if( NF_DEBUG( NF_DBG_WEAPONS )) ALERT( at_console, "nf_debug: pen impact %s tranquilized %d\n", STRING( other->pev->classname ), character );
		}
		EMIT_SOUND( edict(), CHAN_BODY, "gadgets/grapple_hit.wav", 1, ATTN_NORM );
		SetTouch( NULL ); UTIL_Remove( this );
	}
};
LINK_ENTITY_TO_CLASS( dartpen, CNFDart )
LINK_ENTITY_TO_CLASS( dart, CNFDart )

class CNFPen : public CNFGadget
{
public:
	int ID( void ) { return NF_WEAPON_PEN; }
	int DrawSequence( void ) { return 5; }
	int Position( void ) { return 4; }
	BOOL Hat( void ) { return m_pPlayer && m_pPlayer->m_fNFOddjob; }
	const char *ViewModel( void ) { return Hat() ? "models/v_hat.mdl" : "models/v_pen.mdl"; }
	BOOL Deploy( void ) { return DefaultDeploy( ViewModel(), Hat() ? "models/p_hat.mdl" : "models/p_pen.mdl", m_iPrimaryAmmoType > 0 && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] > 0 ? 5 : 7, Hat() ? "hat" : "dartpen" ); }
	const char *WorldModel( void ) { return "models/w_pen.mdl"; }
	const char *Ammo( void ) { return "dart"; }
	void Precache( void ) { CNFGadget::Precache(); PRECACHE_MODEL( "models/v_pen.mdl" ); PRECACHE_MODEL( "models/v_hat.mdl" ); PRECACHE_MODEL( "models/p_pen.mdl" ); PRECACHE_MODEL( "models/p_hat.mdl" ); UTIL_PrecacheOther( "dartpen" ); PRECACHE_SOUND( "gadgets/tranq_fire.wav" ); }
	void PrimaryAttack( void )
	{
		if( m_pending || m_iPrimaryAmmoType <= 0 || m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0 ) { m_flNextPrimaryAttack = gpGlobals->time + 0.2f; return; }
		m_pending = 1; m_fireTime = gpGlobals->time + 0.5f;
		SendWeaponAnim( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] == 1 ? 4 : 3 ); m_pPlayer->SetAnimation( PLAYER_ATTACK1 );
		EMIT_SOUND( m_pPlayer->edict(), CHAN_WEAPON, "gadgets/tranq_fire.wav", 1, ATTN_NORM );
		m_flNextPrimaryAttack = gpGlobals->time + 3.33f;
	}
	void ItemPostFrame( void )
	{
		if( m_pending && gpGlobals->time >= m_fireTime )
		{
			m_pending = 0;
			if( m_pPlayer->IsAlive() && m_iPrimaryAmmoType > 0 && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] > 0 )
			{
				UTIL_MakeVectors( m_pPlayer->pev->v_angle + m_pPlayer->pev->punchangle );
				CBaseEntity *dart = CBaseEntity::Create( "dartpen", m_pPlayer->GetGunPosition() + gpGlobals->v_forward * 16, m_pPlayer->pev->v_angle, m_pPlayer->edict() );
				if( dart ) { dart->pev->velocity = gpGlobals->v_forward * 2000; m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType]--; }
				if( NF_DEBUG( NF_DBG_WEAPONS )) ALERT( at_console, "nf_debug: pen fired ammo %d\n", m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] );
			}
		}
		CNFGadget::ItemPostFrame();
	}
	void Holster( int skiplocal = 0 ) { m_pending = 0; m_fireTime = 0; CBasePlayerWeapon::Holster( skiplocal ); }
	int Save( CSave &save ); int Restore( CRestore &restore ); static TYPEDESCRIPTION m_SaveData[];
	BOOL m_pending; float m_fireTime;
};
TYPEDESCRIPTION CNFPen::m_SaveData[] = { DEFINE_FIELD( CNFPen, m_pending, FIELD_BOOLEAN ), DEFINE_FIELD( CNFPen, m_fireTime, FIELD_TIME ) };
IMPLEMENT_SAVERESTORE( CNFPen, CBasePlayerWeapon )
LINK_ENTITY_TO_CLASS( weapon_pen, CNFPen )
class CNFDartAmmo : public CBasePlayerAmmo
{
public:
	void Spawn( void ) { Precache(); SET_MODEL( edict(), "models/w_ammo_darts.mdl" ); CBasePlayerAmmo::Spawn(); }
	void Precache( void ) { PRECACHE_MODEL( "models/w_ammo_darts.mdl" ); PRECACHE_SOUND( "items/9mmclip1.wav" ); }
	BOOL AddAmmo( CBaseEntity *other ) { if( other->GiveAmmo( 3, "dart", 10 ) == -1 ) return FALSE; EMIT_SOUND( edict(), CHAN_ITEM, "items/9mmclip1.wav", 1, ATTN_NORM ); return TRUE; }
};
LINK_ENTITY_TO_CLASS( ammo_darts, CNFDartAmmo )

class CNFDrive : public CBaseAnimating
{
public:
	void Precache( void ) { if( FStringNull( pev->model )) pev->model = MAKE_STRING( "models/worm_drive.mdl" ); PRECACHE_MODEL( STRING( pev->model )); if( !m_sound ) m_sound = MAKE_STRING( "misc/padlock.wav" ); PRECACHE_SOUND( STRING( m_sound )); }
	void Spawn( void )
	{
		Precache(); SET_MODEL( edict(), STRING( pev->model )); pev->solid = SOLID_BBOX; pev->movetype = MOVETYPE_NONE;
		UTIL_SetSize( pev, Vector( -6, -6, 0 ), Vector( 6, 6, 6 )); UTIL_SetOrigin( pev, pev->origin );
		if( pev->spawnflags & 1 ) DROP_TO_FLOOR( edict() );
		pev->health = pev->health > 0 ? pev->health : 5; pev->deadflag = DEAD_DEAD; pev->skin = 1;
		SetBodygroup( 0, m_body ); Sequence( "idle_closed" );
	}
	void KeyValue( KeyValueData *kv )
	{
		if( FStrEq( kv->szKeyName, "decodesound" )) m_sound = ALLOC_STRING( kv->szValue );
		else if( FStrEq( kv->szKeyName, "bodytype" )) m_body = atoi( kv->szValue );
		else { CBaseAnimating::KeyValue( kv ); return; } kv->fHandled = TRUE;
	}
	int ObjectCaps( void ) { return CBaseAnimating::ObjectCaps() | FCAP_IMPULSE_USE; }
	void Sequence( const char *name ) { pev->sequence = LookupSequence( name ); if( pev->sequence < 0 ) pev->sequence = 0; pev->frame = 0; ResetSequenceInfo(); }
	void Use( CBaseEntity *activator, CBaseEntity *caller, USE_TYPE type, float value )
	{
		if( m_inserted || m_done ) return;
		if( type == USE_ON && activator && activator->IsPlayer() && caller && FClassnameIs( caller->pev, "weapon_qworm" ))
		{
			if( !m_open ) return;
			if( value > 0 ) { m_inserted = TRUE; m_inserting = FALSE; SetBodygroup( 1, 1 ); EMIT_SOUND( edict(), CHAN_ITEM, STRING( m_sound ), 1, ATTN_NORM ); }
			else m_inserting = TRUE;
		}
		else if( type == USE_OFF && m_inserting ) { m_inserting = FALSE; return; }
		else if( m_inserting ) return;
		if( !m_inserting ) { m_open = !m_open; pev->deadflag = m_open ? DEAD_NO : DEAD_DEAD; Sequence( m_open ? "open" : "close" ); }
		SetThink( &CNFDrive::Animate ); pev->nextthink = gpGlobals->time + 0.1f;
		if( NF_DEBUG( NF_DBG_ITEMS )) ALERT( at_console, "nf_debug: drive %s open %d inserting %d inserted %d done %d\n", STRING( pev->targetname ), m_open, m_inserting, m_inserted, m_done );
	}
	void EXPORT Animate( void )
	{
		StudioFrameAdvance(); pev->nextthink = gpGlobals->time + 0.1f;
		if( !m_fSequenceFinished ) return;
		Sequence( m_open ? "idle_open" : "idle_closed" );
		if( !m_open && m_inserted && !m_done )
		{
			m_done = TRUE; SetThink( NULL ); SUB_UseTargets( this, USE_TOGGLE, 0 );
			if( NF_DEBUG( NF_DBG_ITEMS )) ALERT( at_console, "nf_debug: drive %s completed target %s\n", STRING( pev->targetname ), STRING( pev->target ));
		}
		else SetThink( NULL );
	}
	int Save( CSave &save ); int Restore( CRestore &restore ); static TYPEDESCRIPTION m_SaveData[];
	BOOL m_open, m_inserting, m_inserted, m_done;
	string_t m_sound; int m_body;
};
TYPEDESCRIPTION CNFDrive::m_SaveData[] = {
	DEFINE_FIELD( CNFDrive, m_open, FIELD_BOOLEAN ), DEFINE_FIELD( CNFDrive, m_inserting, FIELD_BOOLEAN ),
	DEFINE_FIELD( CNFDrive, m_inserted, FIELD_BOOLEAN ), DEFINE_FIELD( CNFDrive, m_done, FIELD_BOOLEAN ),
	DEFINE_FIELD( CNFDrive, m_sound, FIELD_STRING ), DEFINE_FIELD( CNFDrive, m_body, FIELD_INTEGER ),
};
IMPLEMENT_SAVERESTORE( CNFDrive, CBaseAnimating )
LINK_ENTITY_TO_CLASS( item_drivetarget, CNFDrive )

class CNFQWorm : public CNFGadget
{
public:
	int ID( void ) { return NF_WEAPON_QWORM; }
	int DrawSequence( void ) { return 8; }
	int IdleSequence( void ) { return 10; }
	int Position( void ) { return 6; }
	const char *ViewModel( void ) { return "models/v_qworm.mdl"; }
	BOOL Valid( CNFDrive *drive )
	{
		if( !drive || !drive->m_open || drive->m_inserted || !drive->pev->targetname ) return FALSE;
		Vector eye = m_pPlayer->EyePosition(), dir = drive->pev->origin - eye;
		UTIL_MakeVectors( m_pPlayer->pev->v_angle );
		if( dir.Length() > 100 || DotProduct( dir.Normalize(), gpGlobals->v_forward ) <= 0.7f ) return FALSE;
		TraceResult tr; UTIL_TraceLine( eye, drive->pev->origin + Vector( 0, 0, 2 ), ignore_monsters, dont_ignore_glass, m_pPlayer->edict(), &tr );
		return tr.flFraction == 1 || tr.pHit == drive->edict();
	}
	void DriveUse( CNFDrive *drive, USE_TYPE type, float value ) { if( drive && drive->pev->targetname ) FireTargets( STRING( drive->pev->targetname ), m_pPlayer, this, type, value ); }
	void Cancel( void ) { CNFDrive *drive = (CNFDrive *)(CBaseEntity *)m_drive; DriveUse( drive, USE_OFF, 0 ); m_drive = NULL; m_stage = 0; m_stageTime = 0; }
	void PrimaryAttack( void )
	{
		if( m_stage ) return;
		CBaseEntity *entity = NULL;
		while(( entity = UTIL_FindEntityByClassname( entity, "item_drivetarget" )) != NULL )
		{
			CNFDrive *drive = (CNFDrive *)entity;
			if( !Valid( drive ) || drive->m_inserting ) continue;
			m_drive = drive; m_stage = 1; m_stageTime = gpGlobals->time + 1.33f;
			DriveUse( drive, USE_ON, 0 ); SendWeaponAnim( 12 ); break;
		}
		m_flNextPrimaryAttack = gpGlobals->time + 0.5f;
	}
	void ItemPostFrame( void )
	{
		if( m_stage )
		{
			CNFDrive *drive = (CNFDrive *)(CBaseEntity *)m_drive;
			if( !(m_pPlayer->pev->button & IN_ATTACK) || !m_pPlayer->IsAlive() || !Valid( drive )) Cancel();
			else if( gpGlobals->time >= m_stageTime )
			{
				if( m_stage == 1 ) { m_stage = 2; m_stageTime = gpGlobals->time + 2.33f; SendWeaponAnim( 14 ); }
				else { DriveUse( drive, USE_ON, 1 ); m_drive = NULL; m_stage = 0; m_stageTime = 0; m_flNextPrimaryAttack = gpGlobals->time + 1; }
			}
			return;
		}
		CNFGadget::ItemPostFrame();
	}
	void SecondaryAttack( void ) { PrimaryAttack(); }
	void Holster( int skiplocal = 0 ) { Cancel(); CBasePlayerWeapon::Holster( skiplocal ); }
	void UpdateOnRemove( void ) { Cancel(); CBasePlayerWeapon::UpdateOnRemove(); }
	int Save( CSave &save ); int Restore( CRestore &restore ); static TYPEDESCRIPTION m_SaveData[];
	EHANDLE m_drive; int m_stage; float m_stageTime;
};
TYPEDESCRIPTION CNFQWorm::m_SaveData[] = { DEFINE_FIELD( CNFQWorm, m_drive, FIELD_EHANDLE ), DEFINE_FIELD( CNFQWorm, m_stage, FIELD_INTEGER ), DEFINE_FIELD( CNFQWorm, m_stageTime, FIELD_TIME ) };
IMPLEMENT_SAVERESTORE( CNFQWorm, CBasePlayerWeapon )
LINK_ENTITY_TO_CLASS( weapon_qworm, CNFQWorm )

class CNFGlasses : public CNFGadget
{
public:
	int ID( void ) { return NF_WEAPON_GLASSES; }
	int DrawSequence( void ) { return 1; }
	int Position( void ) { return 0; }
	const char *ViewModel( void ) { return "models/v_glasses.mdl"; }
	void Precache( void ) { CNFGadget::Precache(); PRECACHE_SOUND( "gadgets/nightvision_on.wav" ); PRECACHE_SOUND( "gadgets/nightvision_off.wav" ); PRECACHE_SOUND( "gadgets/nightvision_draw.wav" ); PRECACHE_SOUND( "gadgets/nightvision_loop.wav" ); }
	BOOL Deploy( void ) { Toggle(); return CNFGadget::Deploy(); }
	void PrimaryAttack( void ) { Toggle(); m_flNextPrimaryAttack = gpGlobals->time + 0.5f; }
	void SecondaryAttack( void ) { Mode(); m_flNextSecondaryAttack = gpGlobals->time + 0.5f; }
	void Send( void )
	{
		if( !m_pPlayer ) return;
		MESSAGE_BEGIN( MSG_ONE, gmsgNFVisionMode, NULL, m_pPlayer->pev ); WRITE_BYTE( m_on ? m_mode : 0 ); MESSAGE_END();
		if( m_on ) m_pPlayer->pev->effects |= EF_NIGHTVISION; else m_pPlayer->pev->effects &= ~EF_NIGHTVISION;
		if( NF_DEBUG( NF_DBG_WEAPONS )) ALERT( at_console, "nf_debug: glasses on %d mode %d\n", m_on, m_mode );
	}
	void Toggle( void )
	{
		if( !m_pPlayer || (!m_on && m_pPlayer->m_iFlashBattery <= 0) ) return;
		m_on = !m_on; if( !m_mode ) m_mode = 1;
		m_batteryTime = gpGlobals->time + (m_on ? Interval() : 0.29f);
		EMIT_SOUND( m_pPlayer->edict(), CHAN_ITEM, m_on ? "gadgets/nightvision_on.wav" : "gadgets/nightvision_off.wav", 1, ATTN_NORM );
		if( m_on ) EMIT_SOUND( m_pPlayer->edict(), CHAN_STATIC, "gadgets/nightvision_loop.wav", 0.8f, ATTN_NORM );
		else STOP_SOUND( m_pPlayer->edict(), CHAN_STATIC, "gadgets/nightvision_loop.wav" ); Send();
	}
	float Interval( void ) { return m_mode == 3 ? 0.09f : m_mode == 2 ? 0.15f : 0.19f; }
	void Battery( void )
	{
		if( !m_pPlayer ) return;
		if( m_batteryTime == 0 ) m_batteryTime = gpGlobals->time + (m_on ? Interval() : 0.29f);
		if( gpGlobals->time >= m_batteryTime )
		{
			if( m_on ) m_pPlayer->m_iFlashBattery = Q_max( m_pPlayer->m_iFlashBattery - 1, 0 );
			else m_pPlayer->m_iFlashBattery = Q_min( m_pPlayer->m_iFlashBattery + 1, 100 );
			m_batteryTime = gpGlobals->time + (m_on ? Interval() : 0.29f);
			if( m_on && m_pPlayer->m_iFlashBattery == 0 ) Toggle();
		}
		if( m_battery != m_pPlayer->m_iFlashBattery )
		{
			m_battery = m_pPlayer->m_iFlashBattery;
			MESSAGE_BEGIN( MSG_ONE, gmsgFlashBattery, NULL, m_pPlayer->pev ); WRITE_BYTE( m_battery ); MESSAGE_END();
			if( NF_DEBUG( NF_DBG_WEAPONS ) && (m_battery == 0 || m_battery == 100) ) ALERT( at_console, "nf_debug: glasses battery %d on %d\n", m_battery, m_on );
		}
	}
	void Mode( void ) { if( m_on ) { m_mode = m_mode % 3 + 1; m_batteryTime = gpGlobals->time + Interval(); Send(); } }
	void Reset( void ) { if( m_on && m_pPlayer ) STOP_SOUND( m_pPlayer->edict(), CHAN_STATIC, "gadgets/nightvision_loop.wav" ); m_on = FALSE; Send(); }
	void UpdateOnRemove( void ) { Reset(); CBasePlayerWeapon::UpdateOnRemove(); }
	int Save( CSave &save ); int Restore( CRestore &restore ); static TYPEDESCRIPTION m_SaveData[];
	BOOL m_on; int m_mode; float m_batteryTime; int m_battery;
};
TYPEDESCRIPTION CNFGlasses::m_SaveData[] = { DEFINE_FIELD( CNFGlasses, m_on, FIELD_BOOLEAN ), DEFINE_FIELD( CNFGlasses, m_mode, FIELD_INTEGER ), DEFINE_FIELD( CNFGlasses, m_batteryTime, FIELD_TIME ), DEFINE_FIELD( CNFGlasses, m_battery, FIELD_INTEGER ) };
IMPLEMENT_SAVERESTORE( CNFGlasses, CBasePlayerWeapon )
LINK_ENTITY_TO_CLASS( gadget_nightvision, CNFGlasses )

static CNFGlasses *NF_Glasses( CBasePlayer *player )
{
	for( int slot = 0; slot < MAX_ITEM_TYPES; slot++ ) for( CBasePlayerItem *item = player->m_rgpPlayerItems[slot]; item; item = item->m_pNext )
		if( item->m_iId == NF_WEAPON_GLASSES ) return (CNFGlasses *)item;
	return NULL;
}
int NF_GadgetVisionTarget( edict_t *host, edict_t *target )
{
	CBaseEntity *viewer = CBaseEntity::Instance( host ), *entity = CBaseEntity::Instance( target );
	if( !viewer || !viewer->IsPlayer() || !viewer->IsAlive() || !entity || !entity->IsAlive() || (entity->pev->flags & FL_KILLME) ) return FALSE;
	if( !(FClassnameIs( target, "enemy_generic" ) || FClassnameIs( target, "npc_aigeneric" ))) return FALSE;
	CNFGlasses *glasses = NF_Glasses( (CBasePlayer *)viewer );
	return glasses && glasses->m_on && glasses->m_mode >= 2 && (entity->pev->origin - viewer->pev->origin).Length() <= 1024 ? glasses->m_mode : 0;
}
void NF_GadgetReset( CBasePlayer *player ) { CNFGlasses *glasses = NF_Glasses( player ); if( glasses ) glasses->Reset(); }
BOOL NF_GadgetBatteryThink( CBasePlayer *player ) { CNFGlasses *glasses = NF_Glasses( player ); if( !glasses || player->FlashlightIsOn() ) return FALSE; glasses->Battery(); return TRUE; }
void NF_GadgetSync( CBasePlayer *player ) { CNFGlasses *glasses = NF_Glasses( player ); if( glasses ) glasses->Send(); }
BOOL NF_GadgetCommand( CBaseEntity *entity, const char *command )
{
	BOOL oddjob = FStrEq( command, "oddjob" );
	BOOL toggle = FStrEq( command, "uvvision" ) || FStrEq( command, "ActivateGlasses" );
	BOOL mode = FStrEq( command, "modeswitch" ) || FStrEq( command, "GlassesMode" );
	BOOL info = FStrEq( command, "nf_gadgetinfo" );
	BOOL driveInfo = FStrEq( command, "nf_driveinfo" );
	if( !toggle && !mode && !info && !driveInfo && !oddjob ) return FALSE;
	if( !entity || !entity->IsPlayer() ) return TRUE;
	CBasePlayer *player = (CBasePlayer *)entity;
	CNFGlasses *glasses = NF_Glasses( player );
	if( oddjob )
	{
		if( !player->IsAlive() || player->m_hNFDeathCamera != 0 || (player->pev->flags & FL_FROZEN) ) return TRUE;
		player->m_fNFOddjob = !player->m_fNFOddjob;
		if( player->m_pActiveItem && player->m_pActiveItem->m_iId == NF_WEAPON_PEN ) player->m_pActiveItem->Deploy();
		if( NF_DEBUG( NF_DBG_WEAPONS )) ALERT( at_console, "nf_debug: pen oddjob %d\n", player->m_fNFOddjob );
		return TRUE;
	}
	if( driveInfo )
	{
		if( !NF_DEBUG( NF_DBG_WEAPONS )) return TRUE;
		CBaseEntity *entity = NULL;
		while(( entity = UTIL_FindEntityByClassname( entity, "item_drivetarget" )) != NULL )
		{
			if( CMD_ARGC() > 1 && !FStrEq( CMD_ARGV( 1 ), STRING( entity->pev->targetname ))) continue;
			CNFDrive *drive = (CNFDrive *)entity;
			ALERT( at_console, "nf_debug: driveinfo %s open %d inserting %d inserted %d done %d\n", STRING( entity->pev->targetname ), drive->m_open, drive->m_inserting, drive->m_inserted, drive->m_done );
			Vector eye = player->EyePosition(), dir = drive->pev->origin - eye;
			UTIL_MakeVectors( player->pev->v_angle );
			float dot = DotProduct( dir.Normalize(), gpGlobals->v_forward );
			TraceResult trace;
			UTIL_TraceLine( eye, drive->pev->origin + Vector( 0, 0, 2 ), ignore_monsters, dont_ignore_glass, player->edict(), &trace );
			ALERT( at_console, "nf_debug: driveinfo aim origin %.1f %.1f %.1f distance %.1f dot %.3f trace %.3f hit %d\n",
				drive->pev->origin.x, drive->pev->origin.y, drive->pev->origin.z, dir.Length(), dot,
				trace.flFraction, trace.pHit ? ENTINDEX( trace.pHit ) : -1 );
		}
		return TRUE;
	}
	if( info )
	{
		if( !NF_DEBUG( NF_DBG_WEAPONS )) return TRUE;
		ALERT( at_console, "nf_debug: gadgetinfo glasses owned %d on %d mode %d battery %d\n", glasses != NULL, glasses ? glasses->m_on : 0, glasses ? glasses->m_mode : 0, player->m_iFlashBattery );
		return TRUE;
	}
	if( !player->IsAlive() || player->m_hNFDeathCamera != 0 || (player->pev->flags & FL_FROZEN) ) return TRUE;
	if( glasses ) { if( toggle ) glasses->Toggle(); else glasses->Mode(); }
	return TRUE;
}
