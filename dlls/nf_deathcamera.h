#pragma once
#ifndef NF_DEATHCAMERA_H
#define NF_DEATHCAMERA_H

BOOL NF_DeathCameraStart( CBaseEntity *victim, entvars_t *attacker, const char *name );
void NF_DeathCameraReset( CBasePlayer *player );
void NF_DeathCameraPlayerThink( CBasePlayer *player );
BOOL NF_DeathCameraCommand( CBaseEntity *entity, const char *command );

#endif
