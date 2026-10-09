/* Nightfire Taser rules and enemy interaction (docs/retail/taser.md). */
#pragma once
#ifndef NF_TASER_H
#define NF_TASER_H

#define NF_TASER_RANGE 62.0f
#define NF_TASER_HOLD_RANGE 94.0f
#define NF_TASER_CHARGE_MAX 100.0f
#define NF_TASER_CHARGE_RATE 33.0f
#define NF_TASER_DAMAGE_INTERVAL 0.1f

inline float NF_TaserCharge( float charge, float elapsed, bool firing )
{
	if( elapsed < 0 )
		elapsed = 0;
	charge += elapsed * ( firing ? -NF_TASER_CHARGE_RATE : NF_TASER_CHARGE_RATE );
	return charge < 0 ? 0 : charge > NF_TASER_CHARGE_MAX ? NF_TASER_CHARGE_MAX : charge;
}

class CBaseEntity;
BOOL NF_TaserAcquire( CBaseEntity *target, CBaseEntity *weapon );
BOOL NF_TaserHeld( CBaseEntity *target, CBaseEntity *weapon );
void NF_TaserRelease( CBaseEntity *target, CBaseEntity *weapon );

#endif
