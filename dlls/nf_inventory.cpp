/*
nf_inventory.cpp - James Bond 007: Nightfire (PC) inventory admission

Retail firearms classification and four-item limit: game.dll 0x420582c0,
0x420a3f40; see project docs/retail/weapon-selection.md.
*/
#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "weapons.h"
#include "player.h"
#include "cdll_dll.h"
#include "nf_itemmeta.h"
#include "nf_inventory.h"
#include "nf_debug.h"

int gmsgNFItemInfo;
int gmsgNFWheelLock;

class CNightfireWeaponBag : public CWeaponBox
{
public:
	CNightfireWeaponBag() : m_count( 0 ) { memset( m_weapons, 0, sizeof( m_weapons ) ); }
	void KeyValue( KeyValueData *kv )
	{
		if( !strncmp( kv->szKeyName, "weapon_", 7 ) )
		{
			if( m_count < MAX_WEAPONS && atoi( kv->szValue ) > 0 )
				m_weapons[m_count++] = ALLOC_STRING( kv->szKeyName );
			kv->fHandled = TRUE;
		}
		else if( !strcmp( kv->szKeyName, "9mm" ) || !strcmp( kv->szKeyName, "556mm" ) ||
			!strcmp( kv->szKeyName, "762mm" ) || !strcmp( kv->szKeyName, "440" ) ||
			!strcmp( kv->szKeyName, "28mm" ) || !strcmp( kv->szKeyName, "buckshot" ) ||
			!strcmp( kv->szKeyName, "minigun" ) || !strcmp( kv->szKeyName, "up11" ) ||
			!strcmp( kv->szKeyName, "rocket" ) || !strcmp( kv->szKeyName, "launchgren" ) ||
			!strcmp( kv->szKeyName, "fraggren" ) || !strcmp( kv->szKeyName, "flashgren" ) ||
			!strcmp( kv->szKeyName, "smokegren" ) || !strcmp( kv->szKeyName, "bondmine" ) ||
			!strcmp( kv->szKeyName, "dart" ) )
			CWeaponBox::KeyValue( kv );
		else CBaseEntity::KeyValue( kv );
	}
	void Spawn( void )
	{
		CWeaponBox::Spawn();
		for( int i = 0; i < m_count; i++ )
		{
			CBaseEntity *entity = CBaseEntity::Create( STRING( m_weapons[i] ), pev->origin, pev->angles );
			if( !entity ) continue;
			CBasePlayerItem *item = (CBasePlayerItem *)entity;
			if( !item->GetWeaponPtr() || !PackWeapon( item ) ) UTIL_Remove( entity );
		}
		m_count = 0;
		if( NF_DEBUG( NF_DBG_ITEMS ) )
			ALERT( at_console, "nf_debug: weaponbag spawned %s\n", STRING( pev->targetname ) );
	}
private:
	string_t m_weapons[MAX_WEAPONS];
	int m_count;
};
LINK_ENTITY_TO_CLASS( weaponbag, CNightfireWeaponBag )

BOOL NF_CanAddItem( CBasePlayer *player, CBasePlayerItem *item )
{
	const nf_itemmeta_t *incoming = NF_ItemMeta( item->m_iId, STRING( item->pev->classname ) );
	if( player->m_fNFSpaceSuit )
		return incoming && incoming->id == 29;
	if( !incoming || !incoming->firearm )
		return TRUE;

	int count = 0;
	for( int slot = 0; slot < MAX_ITEM_TYPES; slot++ )
	{
		for( CBasePlayerItem *owned = player->m_rgpPlayerItems[slot]; owned; owned = owned->m_pNext )
		{
			if( FClassnameIs( owned->pev, STRING( item->pev->classname ) ) )
				return TRUE;
			const nf_itemmeta_t *meta = NF_ItemMeta( owned->m_iId, STRING( owned->pev->classname ) );
			if( meta && meta->firearm )
				count++;
		}
	}
	if( count < NF_FIREARM_LIMIT )
		return TRUE;

	if( gpGlobals->time >= player->m_flNFWeaponsFullNext )
	{
		// Port transport: HL TextMsg rather than the retail HudMsg payload.
		ClientPrint( player->pev, HUD_PRINTCENTER, "#HUD_WeaponsFull" );
		player->m_flNFWeaponsFullNext = gpGlobals->time + 5.0f;
		if( NF_DEBUG( NF_DBG_ITEMS ) )
			ALERT( at_console, "nf_debug: firearm pickup denied %s count %d\n", incoming->classname, count );
	}
	return FALSE;
}

void NF_SendWheelLock( CBasePlayer *player )
{
	int locked = player->pev->deadflag != DEAD_NO || ( player->pev->flags & FL_FROZEN ) ||
		player->m_hNFDeathCamera || player->pev->iuser4 ||
		player->m_fNFTraversalHolstered || player->IsObserver() ||
		( player->m_iHideHUD & ( HIDEHUD_WEAPONS | HIDEHUD_ALL ) ) ||
		( player->m_pActiveItem && !player->m_pActiveItem->CanHolster() );
	if( locked == player->m_iNFWheelLockSent )
		return;
	MESSAGE_BEGIN( MSG_ONE, gmsgNFWheelLock, NULL, player->pev );
		WRITE_BYTE( locked ? 1 : 0 );
	MESSAGE_END();
	player->m_iNFWheelLockSent = locked;
}

BOOL NF_InventoryCommand( CBaseEntity *entity, const char *command )
{
	if( !FStrEq( command, "nf_inventoryinfo" ) ) return FALSE;
	if( !NF_DEBUG( NF_DBG_ITEMS ) || !entity || !entity->IsPlayer() ) return TRUE;
	CBasePlayer *player = (CBasePlayer *)entity;
	int count = 0, firearms = 0;
	for( int slot = 0; slot < MAX_ITEM_TYPES; slot++ )
	{
		for( CBasePlayerItem *item = player->m_rgpPlayerItems[slot]; item; item = item->m_pNext )
		{
			const nf_itemmeta_t *meta = NF_ItemMeta( item->m_iId, STRING( item->pev->classname ) );
			CBasePlayerWeapon *weapon = (CBasePlayerWeapon *)item->GetWeaponPtr();
			int ammo = weapon ? weapon->PrimaryAmmoIndex() : -1;
			ALERT( at_console, "nf_debug: inventory item id %d class %s wheel %d firearm %d clip %d ammo %d deploy %d\n",
				item->m_iId, STRING( item->pev->classname ), meta ? meta->wheel : 0,
				meta ? meta->firearm : 0, weapon ? weapon->m_iClip : -1,
				ammo >= 0 ? player->m_rgAmmo[ammo] : -1, item->CanDeploy() );
			count++;
			if( meta && meta->firearm ) firearms++;
		}
	}
	ALERT( at_console, "nf_debug: inventory view origin %.2f %.2f %.2f eye %.2f %.2f %.2f angles %.2f %.2f %.2f\n",
		player->pev->origin.x, player->pev->origin.y, player->pev->origin.z,
		player->EyePosition().x, player->EyePosition().y, player->EyePosition().z,
		player->pev->v_angle.x, player->pev->v_angle.y, player->pev->v_angle.z );
	ALERT( at_console, "nf_debug: inventory player %d items %d firearms %d active %s frozen %d traversal %d\n",
		player->entindex(), count, firearms,
		player->m_pActiveItem ? STRING( player->m_pActiveItem->pev->classname ) : "-",
		( player->pev->flags & FL_FROZEN ) != 0, player->pev->iuser4 );
	return TRUE;
}
