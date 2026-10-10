#pragma once
#ifndef NF_SEARCHLIGHT_H
#define NF_SEARCHLIGHT_H

#define NF_SEARCHLIGHT_MARKER 0x4e46534c
BOOL NF_SearchlightCommand( CBaseEntity *player, const char *command );
void NF_EnemySearchlightAlarm( CBaseEntity *entity, CBaseEntity *target, float distance );
BOOL NF_EnemyCorpseSpotted( CBaseEntity *entity, BOOL mark );

#endif
