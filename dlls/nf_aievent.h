/*
nf_aievent.h - James Bond 007: Nightfire (PC) AI events (info_aievent, dlls/nf_aievent.cpp)
*/
#pragma once
#ifndef NF_AIEVENT_H
#define NF_AIEVENT_H

// eventtype (retail CAIEvent +0x87C, docs/retail/aievent.md)
enum
{
	NF_AIEVENT_DEATH = 1,	// death spot: the bound character plays m_iszPlay when it dies there
	NF_AIEVENT_REPEAT = 2,	// play m_iszPlay animcount times (no map uses it)
	NF_AIEVENT_BOSS = 3,	// boss navigation point (enemy_drake, enemy_rook_boss)
	NF_AIEVENT_ALARM = 4,	// alarm: run there, play alarm_trigger (its event 1003 fires alarmbutton)
	NF_AIEVENT_COVER = 5,	// cover / idle spot (crouching_wait, corner idles)
	NF_AIEVENT_VENT = 6,	// like 5, wakes characters when the player is visible from it
};

// spawnflags
#define SF_NF_AIEVENT_NO_DROP		128	// don't drop to the floor at spawn
#define SF_NF_AIEVENT_DEATH_YAW		256	// type 1: snap the character's yaw to the event yaw
#define SF_NF_AIEVENT_FACE		512	// on arrival face the event yaw
#define SF_NF_AIEVENT_NOT_ENEMIES	1024	// ignored by enemies
#define SF_NF_AIEVENT_NOT_ALLIES	2048	// ignored by non-enemies

class CAIEvent : public CBaseDelay
{
public:
	void Spawn( void );
	void KeyValue( KeyValueData *pkvd );
	void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );
	int ObjectCaps( void ) { return CBaseDelay::ObjectCaps() & ~FCAP_ACROSS_TRANSITION; }

	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];

	void Trigger( CBaseEntity *pActivator );	// wake one character within m_flRadius
	void EXPORT DetectEnemyThink( void );		// type 6
	BOOL InViewCone( const Vector &vecSpot );	// retail FInViewCone with m_flFieldOfView 0.7
	void SequenceDone( CBaseEntity *pCharacter );	// fire target after the event animation

	int m_iEventType;
	float m_flAngleRange;
	int m_iAnimCount;
	int m_iCounter;
	string_t m_iszUseTarget;
	string_t m_iszPlay;
	string_t m_iszEntity;
	float m_flRadius;
	BOOL m_fLocked;			// in use by a character
	float m_flNextValidTime;
};

// in dlls/nf_enemy.cpp: the character side of CBaseEntity::ActivateAIEvent
// (retail vtable +0xF8) for enemy_generic
BOOL NF_EnemyActivateAIEvent( CBaseEntity *pEntity, CAIEvent *pEvent, CBaseEntity *pActivator );

#endif // NF_AIEVENT_H
