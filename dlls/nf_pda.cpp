/*
nf_pda.cpp - James Bond 007: Nightfire (PC) PDA (weapon_pda) and lock targets (item_locktarget)

Retail CPDA (game.dll vtable 0x4213295C) and CLockTarget (vtable 0x42120454),
docs/retail/locktarget.md in the project repo.

The PDA (weapon id 24, v_pda.mdl, no ammo) opens with the primary or the
secondary attack (select_options, 1.275 s). While the button is held it
looks for an item_locktarget within 100 units of the eye that is not
unlocked, has a clear line from the eye and lies within ~45 degrees of the
view, and fires it by name with USE_ON (options_idle, again every 3.33 s).
It re-checks every 0.5 s and releases the lock (USE_OFF) when it is out of
range or view; releasing the button closes the PDA (deselect_options,
USE_OFF). Server side only like the watch (no client prediction).

The lock target: health = seconds to unlock (default 5). A second after the
spawn it turns red (fixedlight 255 0 0, EF_FIXEDLIGHT, TE_ELIGHT radius 20).
USE_ON starts the countdown and the player's progress bar ("Progress",
fuser1 = seconds left), USE_OFF cancels it. Done: unlocksound, target
fired (USE_TOGGLE, activator the lock), unlockbody / unlockskin, green
(fixedlight 0 255 0, TE_ELIGHT at attachment 0), the lockactivate sequence
and then lockunlocked; removed unless spawnflag 2 (all map targets have it).
Locked by a master: lockedtarget is fired instead. Keys: model (default
models/padlock.mdl), health, target, unlocksound (default misc/padlock.wav),
unlockbody, unlockskin, lockedtarget, lockidle, lockactivate, lockunlocked,
master; spawnflags 1 drop to the floor, 2 keep after unlocking. The use icon
(640_use_pda) is in dlls/nf_lasertarget.cpp.
*/

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "weapons.h"
#include "player.h"
#include "nf_weapons.h"
#include "nf_debug.h"

extern int gmsgNFProgress;

#define SF_LOCKTARGET_DROP	1
#define SF_LOCKTARGET_KEEP	2

#define PDA_RANGE		100.0f	// retail: eye to lock origin
#define PDA_MIN_DOT		0.7f	// retail: view forward . direction to the lock

static void NF_SendLockProgress( CBaseEntity *pLock, BOOL show )
{
	CBaseEntity *pPlayer = CBaseEntity::Instance( pLock->pev->enemy );
	if( !pPlayer || !pPlayer->IsPlayer())
		return;

	MESSAGE_BEGIN( MSG_ONE, gmsgNFProgress, NULL, pPlayer->pev );
		WRITE_SHORT( pLock->entindex());
		WRITE_COORD( pLock->pev->health );
		WRITE_BYTE( show ? 1 : 0 );
	MESSAGE_END();
}

static void NF_SendLockLight( int key, const Vector &origin, int r, int g, int b )
{
	MESSAGE_BEGIN( MSG_BROADCAST, SVC_TEMPENTITY );
		WRITE_BYTE( TE_ELIGHT );
		WRITE_SHORT( key );
		WRITE_COORD( origin.x );
		WRITE_COORD( origin.y );
		WRITE_COORD( origin.z );
		WRITE_COORD( 20 );	// radius
		WRITE_BYTE( r );
		WRITE_BYTE( g );
		WRITE_BYTE( b );
		WRITE_BYTE( 0 );	// life
		WRITE_COORD( 0 );	// decay
	MESSAGE_END();
}

//=========================================================
// item_locktarget
//=========================================================
class CLockTarget : public CBaseToggle
{
public:
	void Spawn( void );
	void Precache( void );
	void KeyValue( KeyValueData *pkvd );
	void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );
	void EXPORT StartThink( void );
	void EXPORT ProgressThink( void );
	void EXPORT AnimateThink( void );
	void Unlock( void );

	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];

	void SetSequenceByName( string_t name );

	string_t m_iszUnlockSound;
	string_t m_iszLockedTarget;
	int m_nUnlockBody;
	int m_nUnlockSkin;
	BOOL m_fActiveUnlock;
	float m_flUnlockTime;
	BOOL m_fUnlocked;
	string_t m_iszLockIdle;
	string_t m_iszLockActivate;
	string_t m_iszLockUnlocked;
	BOOL m_fUnlockingAnim;
};

TYPEDESCRIPTION CLockTarget::m_SaveData[] =
{
	DEFINE_FIELD( CLockTarget, m_iszUnlockSound, FIELD_STRING ),
	DEFINE_FIELD( CLockTarget, m_iszLockedTarget, FIELD_STRING ),
	DEFINE_FIELD( CLockTarget, m_nUnlockBody, FIELD_INTEGER ),
	DEFINE_FIELD( CLockTarget, m_nUnlockSkin, FIELD_INTEGER ),
	DEFINE_FIELD( CLockTarget, m_fActiveUnlock, FIELD_BOOLEAN ),
	DEFINE_FIELD( CLockTarget, m_flUnlockTime, FIELD_TIME ),
	DEFINE_FIELD( CLockTarget, m_fUnlocked, FIELD_BOOLEAN ),
	DEFINE_FIELD( CLockTarget, m_iszLockIdle, FIELD_STRING ),
	DEFINE_FIELD( CLockTarget, m_iszLockActivate, FIELD_STRING ),
	DEFINE_FIELD( CLockTarget, m_iszLockUnlocked, FIELD_STRING ),
	DEFINE_FIELD( CLockTarget, m_fUnlockingAnim, FIELD_BOOLEAN ),
};

int CLockTarget::Save( CSave &save )
{
	if( !CBaseToggle::Save( save ))
		return 0;
	return save.WriteFields( "CLockTarget", this, m_SaveData, ARRAYSIZE( m_SaveData ));
}

// retail 0x4207B020: a locked target turns red again 1.1 s after a load; retail also
// does it while unlocking, which replaces ProgressThink and leaves the lock stuck
int CLockTarget::Restore( CRestore &restore )
{
	if( !CBaseToggle::Restore( restore ))
		return 0;
	int status = restore.ReadFields( "CLockTarget", this, m_SaveData, ARRAYSIZE( m_SaveData ));
	if( pev->deadflag == DEAD_NO && !m_fActiveUnlock )
	{
		SetThink( &CLockTarget::StartThink );
		pev->nextthink = gpGlobals->time + 1.1f;
	}
	return status;
}

LINK_ENTITY_TO_CLASS( item_locktarget, CLockTarget )

// retail 0x4207A870
void CLockTarget::KeyValue( KeyValueData *pkvd )
{
	if( FStrEq( pkvd->szKeyName, "unlocksound" ))
		m_iszUnlockSound = ALLOC_STRING( pkvd->szValue );
	else if( FStrEq( pkvd->szKeyName, "unlockbody" ))
		m_nUnlockBody = atoi( pkvd->szValue );
	else if( FStrEq( pkvd->szKeyName, "unlockskin" ))
		m_nUnlockSkin = atoi( pkvd->szValue );
	else if( FStrEq( pkvd->szKeyName, "lockedtarget" ))
		m_iszLockedTarget = ALLOC_STRING( pkvd->szValue );
	else if( FStrEq( pkvd->szKeyName, "lockidle" ))
		m_iszLockIdle = ALLOC_STRING( pkvd->szValue );
	else if( FStrEq( pkvd->szKeyName, "lockactivate" ))
		m_iszLockActivate = ALLOC_STRING( pkvd->szValue );
	else if( FStrEq( pkvd->szKeyName, "lockunlocked" ))
		m_iszLockUnlocked = ALLOC_STRING( pkvd->szValue );
	else
	{
		CBaseToggle::KeyValue( pkvd );
		return;
	}
	pkvd->fHandled = TRUE;
}

// retail 0x4207A9E0
void CLockTarget::Precache( void )
{
	if( FStringNull( pev->model ))
		pev->model = MAKE_STRING( "models/padlock.mdl" );
	PRECACHE_MODEL( STRING( pev->model ));
	if( FStringNull( m_iszUnlockSound ))
		m_iszUnlockSound = MAKE_STRING( "misc/padlock.wav" );
	PRECACHE_SOUND( STRING( m_iszUnlockSound ));
}

// the named sequence (0 if the model lacks it), from frame 0
void CLockTarget::SetSequenceByName( string_t name )
{
	pev->sequence = LookupSequence( STRING( name ));
	if( pev->sequence == -1 )
		pev->sequence = 0;
	ResetSequenceInfo();
	pev->frame = 0;
}

// retail 0x4207B080
void CLockTarget::Spawn( void )
{
	Precache();

	pev->solid = SOLID_BBOX;
	pev->movetype = MOVETYPE_NONE;
	pev->frame = 0;
	pev->deadflag = DEAD_NO;	// DEAD_DEAD = unlocked, the PDA skips it
	pev->takedamage = DAMAGE_NO;
	SET_MODEL( ENT( pev ), STRING( pev->model ));
	UTIL_SetSize( pev, Vector( -6, -6, 0 ), Vector( 6, 6, 6 ));
	UTIL_SetOrigin( pev, pev->origin );

	if( FBitSet( pev->spawnflags, SF_LOCKTARGET_DROP ) && DROP_TO_FLOOR( ENT( pev )) == 0 )
	{
		ALERT( at_error, "Item %s fell out of level at %f,%f,%f\n", STRING( pev->classname ), pev->origin.x, pev->origin.y, pev->origin.z );
		UTIL_Remove( this );
		return;
	}

	if( pev->health == 0 )
		pev->health = 5;

	m_fActiveUnlock = FALSE;
	m_fUnlocked = FALSE;
	m_fUnlockingAnim = FALSE;
	SetThink( &CLockTarget::StartThink );
	pev->nextthink = gpGlobals->time + 1.0f;
	if( !FStringNull( m_iszLockIdle ))
		SetSequenceByName( m_iszLockIdle );
}

// retail 0x4207AD50: red; AnimateThink only runs after an unlock (no nextthink, as in retail)
void CLockTarget::StartThink( void )
{
	NF_SendLockLight( entindex(), pev->origin, 255, 0, 0 );
	pev->effects |= EF_FIXEDLIGHT;
	pev->fixedlight = Vector( 255, 0, 0 );
	SetThink( &CLockTarget::AnimateThink );
}

// retail 0x4207AA50: after lockactivate has played once, lockunlocked
void CLockTarget::AnimateThink( void )
{
	StudioFrameAdvance();
	if( m_fUnlockingAnim && m_fSequenceFinished )
	{
		if( !m_fSequenceLoops )
		{
			if( !FStringNull( m_iszLockUnlocked ))
				SetSequenceByName( m_iszLockUnlocked );
			else
			{
				pev->frame = 0;
				ResetSequenceInfo();
			}
		}
		m_fUnlockingAnim = FALSE;
	}
	pev->nextthink = gpGlobals->time + 1.0f;
}

// retail 0x4207AE30, every 0.1 s while unlocking
void CLockTarget::ProgressThink( void )
{
	StudioFrameAdvance();
	if( gpGlobals->time > m_flUnlockTime )
	{
		Unlock();
		return;
	}
	pev->fuser1 = m_flUnlockTime - gpGlobals->time;	// the client's bar reads it
	pev->nextthink = gpGlobals->time + 0.1f;
}

// retail 0x4207AAF0 (UnlockThink)
void CLockTarget::Unlock( void )
{
	if( !FBitSet( pev->spawnflags, SF_LOCKTARGET_KEEP ))
	{
		SetThink( &CBaseEntity::SUB_Remove );
		pev->nextthink = gpGlobals->time + 0.1f;
	}

	if( NF_DEBUG( NF_DBG_ITEMS ))
		ALERT( at_console, "nf_debug: item_locktarget %s unlocked, fires '%s'\n", STRING( pev->targetname ), STRING( pev->target ));
	EMIT_SOUND_DYN( ENT( pev ), CHAN_STATIC, STRING( m_iszUnlockSound ), 1.0f, 0.8f, 0, PITCH_NORM );
	SUB_UseTargets( this, USE_TOGGLE, 0 );
	if( m_nUnlockBody > 0 )
		SetBodygroup( 0, m_nUnlockBody );
	if( m_nUnlockSkin > 0 )
		pev->skin = m_nUnlockSkin;

	Vector vecOrigin = pev->origin, vecAngles;	// models without attachments keep the origin
	GetAttachment( 0, vecOrigin, vecAngles );
	NF_SendLockLight( entindex() + 0x1000, vecOrigin, 0, 255, 0 );
	pev->effects |= EF_FIXEDLIGHT;
	pev->fixedlight = Vector( 0, 255, 0 );
	m_fUnlocked = TRUE;
	pev->deadflag = DEAD_DEAD;
	NF_SendLockProgress( this, FALSE );

	if( !FStringNull( m_iszLockActivate ))
	{
		pev->sequence = LookupSequence( STRING( m_iszLockActivate ));
		if( pev->sequence == -1 )
			pev->sequence = 0;
		else
			m_fUnlockingAnim = TRUE;
		ResetSequenceInfo();
		pev->frame = 0;
		SetThink( &CLockTarget::AnimateThink );
		pev->nextthink = gpGlobals->time + 0.1f;
	}
}

// retail 0x4207AE90: the PDA fires the lock by name, USE_ON starts and USE_OFF cancels
void CLockTarget::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	if( m_fUnlocked )
		return;

	if( !UTIL_IsMasterTriggered( m_sMaster, pActivator ))
	{
		if( !FStringNull( m_iszLockedTarget ))
			FireTargets( STRING( m_iszLockedTarget ), pActivator, pCaller, useType, value );
		return;
	}

	if( useType == USE_ON )
	{
		if( m_fActiveUnlock )
			return;
		SetThink( &CLockTarget::ProgressThink );
		pev->nextthink = gpGlobals->time + 0.1f;
		NF_SendLockProgress( this, TRUE );
		m_flUnlockTime = gpGlobals->time + pev->health;
		pev->fuser1 = pev->health;
		m_fActiveUnlock = TRUE;
		if( NF_DEBUG( NF_DBG_ITEMS ))
			ALERT( at_console, "nf_debug: item_locktarget %s unlocking, %.1f s\n", STRING( pev->targetname ), pev->health );
	}
	else if( useType == USE_OFF && m_fActiveUnlock )
	{
		SetThink( NULL );
		NF_SendLockProgress( this, FALSE );
		m_fActiveUnlock = FALSE;
		if( NF_DEBUG( NF_DBG_ITEMS ))
			ALERT( at_console, "nf_debug: item_locktarget %s unlock cancelled\n", STRING( pev->targetname ));
	}
}

//=========================================================
// weapon_pda
//=========================================================
enum nf_pda_e
{
	PDA_IDLE1 = 0,
	PDA_DRAW,
	PDA_HOLSTER,
	PDA_SELECT_OPTIONS,
	PDA_DESELECT_OPTIONS,
	PDA_OPTIONS_IDLE,
};

class CNightfirePDA : public CBasePlayerWeapon
{
public:
	void Spawn( void );
	void Precache( void );
	int iItemSlot( void ) { return 1; }
	int GetItemInfo( ItemInfo *p );
	int AddToPlayer( CBasePlayer *pPlayer );
	BOOL Deploy( void );
	void Holster( int skiplocal = 0 );
	void PrimaryAttack( void );
	void SecondaryAttack( void ) { PrimaryAttack(); }	// retail 0x420E21F0
	void WeaponIdle( void );
	BOOL IsUseable( void ) { return TRUE; }
	BOOL CanDeploy( void ) { return TRUE; }

	virtual BOOL UseDecrement( void ) { return FALSE; }

	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];

private:
	BOOL Facing( CBaseEntity *pLock );
	BOOL InSight( CBaseEntity *pLock, const Vector &vecSrc );
	void UseLock( CBaseEntity *pLock, USE_TYPE useType );
	void Release( void );

	BOOL m_fActive;		// a lock is being unlocked
	BOOL m_fOpen;		// the options are open
	EHANDLE m_hLockTarget;
	float m_flLastActiveIdle;
};

TYPEDESCRIPTION CNightfirePDA::m_SaveData[] =
{
	DEFINE_FIELD( CNightfirePDA, m_fActive, FIELD_BOOLEAN ),
	DEFINE_FIELD( CNightfirePDA, m_fOpen, FIELD_BOOLEAN ),
	DEFINE_FIELD( CNightfirePDA, m_hLockTarget, FIELD_EHANDLE ),
	DEFINE_FIELD( CNightfirePDA, m_flLastActiveIdle, FIELD_TIME ),
};

IMPLEMENT_SAVERESTORE( CNightfirePDA, CBasePlayerWeapon )

LINK_ENTITY_TO_CLASS( weapon_pda, CNightfirePDA )

// retail 0x420E2310
void CNightfirePDA::Spawn( void )
{
	Precache();
	m_iId = NF_WEAPON_PDA;
	m_iClip = WEAPON_NOCLIP;	// no magazine: an empty clip would "reload" instead of WeaponIdle
	SET_MODEL( ENT( pev ), "models/w_kowloon.mdl" );	// retail: the PDA has no world model
	m_fOpen = m_fActive = FALSE;
	FallInit();
}

// retail 0x420E2100; the sounds are v_pda.mdl events
void CNightfirePDA::Precache( void )
{
	PRECACHE_MODEL( "models/v_pda.mdl" );
	PRECACHE_MODEL( "models/w_kowloon.mdl" );
	PRECACHE_SOUND( "gadgets/pda_draw.wav" );
	PRECACHE_SOUND( "gadgets/pda_fire.wav" );
	PRECACHE_SOUND( "gadgets/pda_fire2.wav" );
}

// retail 0x420E2140: id 24, no ammo, no clip
int CNightfirePDA::GetItemInfo( ItemInfo *p )
{
	p->pszName = STRING( pev->classname );
	p->pszAmmo1 = NULL;
	p->iMaxAmmo1 = -1;
	p->pszAmmo2 = NULL;
	p->iMaxAmmo2 = -1;
	p->iMaxClip = WEAPON_NOCLIP;
	p->iSlot = 0;		// [assumed] bucket 1 after the watch and the grapple (retail: gadget wheel)
	p->iPosition = 2;
	p->iFlags = 0;
	p->iId = m_iId = NF_WEAPON_PDA;
	p->iWeight = -1;	// never auto-selected
	return 1;
}

int CNightfirePDA::AddToPlayer( CBasePlayer *pPlayer )
{
	if( CBasePlayerWeapon::AddToPlayer( pPlayer ))
	{
		MESSAGE_BEGIN( MSG_ONE, gmsgWeapPickup, NULL, pPlayer->pev );
			WRITE_BYTE( m_iId );
		MESSAGE_END();
		return TRUE;
	}
	return FALSE;
}

// retail 0x420E21B0
BOOL CNightfirePDA::Deploy( void )
{
	m_fOpen = m_fActive = FALSE;
	m_iClip = WEAPON_NOCLIP;
	m_flTimeWeaponIdle = gpGlobals->time + 1.34f;
	return DefaultDeploy( "models/v_pda.mdl", "", PDA_DRAW, "onehanded" );
}

// retail 0x420E2060 clears m_fOpen before it tests it, so a lock kept unlocking after a
// weapon switch; here the lock is released
void CNightfirePDA::Holster( int skiplocal )
{
	if( m_fOpen )
		Release();
	m_fOpen = m_fActive = FALSE;
	m_flTimeWeaponIdle = gpGlobals->time + 10.0f;
	CBasePlayerWeapon::Holster( skiplocal );
}

void CNightfirePDA::UseLock( CBaseEntity *pLock, USE_TYPE useType )
{
	if( useType == USE_ON )
		pLock->pev->enemy = m_pPlayer->edict();	// the progress bar goes to him
	if( !FStringNull( pLock->pev->targetname ))
		FireTargets( STRING( pLock->pev->targetname ), m_pPlayer, m_pPlayer, useType, 0 );
	else
		pLock->Use( m_pPlayer, m_pPlayer, useType, 0 );	// retail: a lock without a name cannot be unlocked
}

void CNightfirePDA::Release( void )
{
	CBaseEntity *pLock = m_hLockTarget;
	if( pLock )
		UseLock( pLock, USE_OFF );
	m_hLockTarget = NULL;
	m_fActive = FALSE;
}

// retail 0x420E2380
BOOL CNightfirePDA::Facing( CBaseEntity *pLock )
{
	UTIL_MakeVectors( m_pPlayer->pev->v_angle );
	Vector vecDir = ( pLock->pev->origin - ( m_pPlayer->pev->origin + m_pPlayer->pev->view_ofs )).Normalize();
	return DotProduct( gpGlobals->v_forward, vecDir ) > PDA_MIN_DOT;
}

// clear line from the eye to 2 units above the lock origin, then Facing
BOOL CNightfirePDA::InSight( CBaseEntity *pLock, const Vector &vecSrc )
{
	TraceResult tr;
	UTIL_TraceLine( vecSrc, pLock->pev->origin + Vector( 0, 0, 2 ), ignore_monsters, dont_ignore_glass, m_pPlayer->edict(), &tr );
	if( tr.flFraction < 1.0f && tr.pHit != pLock->edict())
		return FALSE;
	return Facing( pLock );
}

// retail 0x420E2490
void CNightfirePDA::PrimaryAttack( void )
{
	Vector vecSrc = m_pPlayer->pev->origin + m_pPlayer->pev->view_ofs;

	if( !m_fOpen )
	{
		m_flNextPrimaryAttack = m_flTimeWeaponIdle = gpGlobals->time + 1.275f;
		SendWeaponAnim( PDA_SELECT_OPTIONS );
		m_flLastActiveIdle = 0;
		m_fOpen = TRUE;
		m_pPlayer->SetAnimation( PLAYER_ATTACK1 );
		return;
	}

	if( !m_fActive )
	{
		CBaseEntity *pLock = NULL;
		while(( pLock = UTIL_FindEntityByClassname( pLock, "item_locktarget" )) != NULL )
		{
			if( FStringNull( pLock->pev->target ))
			{
				ALERT( at_error, "item_locktarget: No lock targets defined.\n" );
				continue;
			}
			if(( pLock->pev->origin - vecSrc ).Length() > PDA_RANGE || pLock->pev->deadflag == DEAD_DEAD )
				continue;
			if( !InSight( pLock, vecSrc ))
				continue;

			m_hLockTarget = pLock;
			m_fActive = TRUE;
			UseLock( pLock, USE_ON );
			SendWeaponAnim( PDA_OPTIONS_IDLE );
			m_flLastActiveIdle = gpGlobals->time;
			return;
		}
		if( !m_hLockTarget )
		{
			m_flNextPrimaryAttack = gpGlobals->time + 0.5f;
			return;
		}
	}

	CBaseEntity *pLock = m_hLockTarget;
	if( !pLock )
		return;

	if(( pLock->pev->origin - vecSrc ).Length() > PDA_RANGE || !InSight( pLock, vecSrc ))
		m_fActive = FALSE;

	m_flNextPrimaryAttack = gpGlobals->time + 0.5f;
	if( !m_fActive )
		UseLock( pLock, USE_OFF );
	else if( gpGlobals->time - m_flLastActiveIdle >= 3.33f )
	{
		UseLock( pLock, USE_ON );
		SendWeaponAnim( PDA_OPTIONS_IDLE );
		m_flLastActiveIdle = gpGlobals->time;
	}
	m_flTimeWeaponIdle = gpGlobals->time + 0.1f;
}

// retail 0x420E2220: the button is up
void CNightfirePDA::WeaponIdle( void )
{
	if( m_fOpen )
	{
		Release();
		SendWeaponAnim( PDA_DESELECT_OPTIONS );
		m_fOpen = FALSE;
		m_flTimeWeaponIdle = m_flNextPrimaryAttack = gpGlobals->time + 1.45f;
		return;
	}

	if( m_flTimeWeaponIdle > gpGlobals->time )
		return;
	m_flTimeWeaponIdle = gpGlobals->time + RANDOM_FLOAT( 2.6f, 5.0f );
	SendWeaponAnim( PDA_IDLE1 );
}
