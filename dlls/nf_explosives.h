#pragma once
#ifndef NF_EXPLOSIVES_H
#define NF_EXPLOSIVES_H

#ifndef CLIENT_DLL
CGrenade *NF_LaunchGrenade( entvars_t *owner, Vector origin, Vector velocity, BOOL timed );
void NF_Explode( CGrenade *grenade, TraceResult *trace, int damageType );
BOOL NF_SmokeOccludes( Vector start, Vector end );
void NF_BlindEnemy( CBaseEntity *entity, float duration );
BOOL NF_HasRonin( CBasePlayer *player );
BOOL NF_ExplosivesCommand( CBaseEntity *player, const char *command );
#endif

#endif
