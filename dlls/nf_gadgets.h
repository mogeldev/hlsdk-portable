#pragma once
#ifndef NF_GADGETS_H
#define NF_GADGETS_H
BOOL NF_GadgetCommand( CBaseEntity *entity, const char *command );
void NF_GadgetReset( CBasePlayer *player );
void NF_GadgetSync( CBasePlayer *player );
BOOL NF_GadgetBatteryThink( CBasePlayer *player );
int NF_GadgetVisionTarget( edict_t *host, edict_t *target );
#define NF_VISION_CHARACTER 0x4e465643
#define NF_RENDERFX_XRAY 64
string_t NF_PhotoCharacterTarget( CBaseEntity *entity );
void NF_PhotoCharacterPose( CBaseEntity *entity, BOOL enabled );
#endif
