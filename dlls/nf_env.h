/*
nf_env.h - James Bond 007: Nightfire (PC) map fog (env_fog, dlls/nf_env.cpp)
*/
#pragma once
#ifndef NF_ENV_H
#define NF_ENV_H

class CBasePlayer;

void NF_FogUpdateClient( CBasePlayer *pPlayer );	// CBasePlayer::UpdateClientData (HUD init)

#endif // NF_ENV_H
