/*
nf_triggers.h - James Bond 007: Nightfire (PC) mission objectives

The objective list belongs to the global game state like in retail
(CGlobalState, "OENT" records after the "GENT" ones): it survives level
changes and is stored in save games (dlls/nf_triggers.cpp).
*/
#pragma once
#ifndef NF_TRIGGERS_H
#define NF_TRIGGERS_H

class CSave;
class CRestore;
class CBasePlayer;

int NF_ObjectivesSave( CSave &save );			// CGlobalState::Save
void NF_ObjectivesRestore( CRestore &restore );	// CGlobalState::Restore
void NF_ObjectivesClear( void );			// CGlobalState::ClearStates (new game, load)
void NF_ObjectivesUpdateClient( CBasePlayer *pPlayer );	// CBasePlayer::UpdateClientData

#endif // NF_TRIGGERS_H
