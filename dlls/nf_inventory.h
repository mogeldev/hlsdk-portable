/*
nf_inventory.h - James Bond 007: Nightfire (PC) inventory admission
*/
#pragma once
#ifndef NF_INVENTORY_H
#define NF_INVENTORY_H

class CBasePlayer;
class CBasePlayerItem;
class CBaseEntity;
BOOL NF_CanAddItem( CBasePlayer *player, CBasePlayerItem *item );
void NF_SendWheelLock( CBasePlayer *player );
BOOL NF_InventoryCommand( CBaseEntity *entity, const char *command );
extern int gmsgNFItemInfo;
extern int gmsgNFWheelLock;

#endif // NF_INVENTORY_H
