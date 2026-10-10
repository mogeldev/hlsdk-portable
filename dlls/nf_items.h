#pragma once
#ifndef NF_ITEMS_H
#define NF_ITEMS_H

BOOL NF_ItemCommand( CBaseEntity *player, const char *command );
BOOL NF_ItemUsePoint( CBaseEntity *entity, const Vector &source, Vector &point );

#endif
