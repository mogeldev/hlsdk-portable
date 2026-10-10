#pragma once
#ifndef NF_CHARACTER_H
#define NF_CHARACTER_H

struct nf_character_change_t
{
	string_t triggerTarget;
	int triggerCondition;
	float minPatrol, maxPatrol, maxPath, waitPatrol;
	string_t deathCamera, rescueTarget;
	int excludeEvents;
	string_t cameraTarget, deathTarget;
	float sight;
	int initEvent, primaryWeapon, secondaryWeapon, gunIndex;
	int spawnflags;
	string_t hostageGroup;
};

BOOL NF_EnemyChangeCharacter( CBaseEntity *entity, const nf_character_change_t &change );
BOOL NF_CharacterCommand( CBaseEntity *player, const char *command );

#endif
