/*
nf_lasertarget.cpp - James Bond 007: Nightfire (PC) laser targets and the use icon

item_lasertarget (padlocks, vent clasps, safe dials) and item_padlock are
burnt open with the watch laser (dlls/nf_watch.cpp): only the laser takes
their health, one unit per laser frame in retail (here 60 per second);
bullets and blasts do nothing (takedamage stays off). While the health
drops the target glows red and the lasering player sees the progress bar
("Progress"); at 0 it plays breaksound, fires target (USE_TOGGLE), sets
breakbody / breakskin and is removed (spawnflag 2: kept). Retail CLaserTarget
(game.dll vtable 0x4211FEC4) and CPadlock: docs/retail/lasertarget.md.

Keys: model (default models/padlock.mdl), health (default 60), target,
breaksound (default misc/padlock.wav), breakbody, breakskin; spawnflags 1
drop to the floor, 2 keep after breaking. fixedlight / Effects 256
(EF_FIXEDLIGHT, the model light floor) are plain entvars; the glow is the
retail fixedlight heat-up with EF_FIXEDLIGHT set while it lasts.

The player's use icon (retail 0x420c2d50): every frame a 96-unit trace
along the view; an entity that has one shows its icon ("SetHudIcon": 5 =
watch for laser targets); not while the level change icon (2) shows.
*/

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "nf_debug.h"
#include "nf_lasertarget.h"

#define SF_LASERTARGET_DROP	1
#define SF_LASERTARGET_KEEP	2

extern int gmsgNFProgress;

class CLaserTarget : public CBaseToggle
{
public:
	void Spawn( void );
	void Precache( void );
	void KeyValue( KeyValueData *pkvd );
	void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );
	void EXPORT BreakThink( void );

	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];

	void ShowProgress( BOOL show );

	string_t m_iszBreakSound;
	int m_nBreakBody;
	int m_nBreakSkin;
	float m_flLastHealth;
	int m_nPrevEffects;		// retail +0xFC: effects before the glow
	Vector m_vecPrevFixedLight;	// retail +0x100: fixedlight before the glow
	BOOL m_fGlowing;
	BOOL m_fProgressVisible;
	BOOL m_fBroken;
};

TYPEDESCRIPTION CLaserTarget::m_SaveData[] =
{
	DEFINE_FIELD( CLaserTarget, m_iszBreakSound, FIELD_STRING ),
	DEFINE_FIELD( CLaserTarget, m_nBreakBody, FIELD_INTEGER ),
	DEFINE_FIELD( CLaserTarget, m_nBreakSkin, FIELD_INTEGER ),
	DEFINE_FIELD( CLaserTarget, m_flLastHealth, FIELD_FLOAT ),
	DEFINE_FIELD( CLaserTarget, m_nPrevEffects, FIELD_INTEGER ),
	DEFINE_FIELD( CLaserTarget, m_vecPrevFixedLight, FIELD_VECTOR ),
	DEFINE_FIELD( CLaserTarget, m_fGlowing, FIELD_BOOLEAN ),
	DEFINE_FIELD( CLaserTarget, m_fProgressVisible, FIELD_BOOLEAN ),
	DEFINE_FIELD( CLaserTarget, m_fBroken, FIELD_BOOLEAN ),
};

IMPLEMENT_SAVERESTORE( CLaserTarget, CBaseToggle )

LINK_ENTITY_TO_CLASS( item_lasertarget, CLaserTarget )
LINK_ENTITY_TO_CLASS( item_padlock, CLaserTarget )	// retail CPadlock: the same without keys

void CLaserTarget::KeyValue( KeyValueData *pkvd )
{
	if( FStrEq( pkvd->szKeyName, "breaksound" ))
	{
		m_iszBreakSound = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "breakbody" ))
	{
		m_nBreakBody = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "breakskin" ))
	{
		m_nBreakSkin = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else
		CBaseToggle::KeyValue( pkvd );
}

void CLaserTarget::Precache( void )
{
	if( FClassnameIs( pev, "item_padlock" ) || FStringNull( pev->model ))
		pev->model = MAKE_STRING( "models/padlock.mdl" );
	PRECACHE_MODEL( STRING( pev->model ));
	if( FStringNull( m_iszBreakSound ))
		m_iszBreakSound = MAKE_STRING( "misc/padlock.wav" );
	PRECACHE_SOUND( STRING( m_iszBreakSound ));
}

void CLaserTarget::Spawn( void )
{
	Precache();

	pev->solid = SOLID_BBOX;
	pev->movetype = MOVETYPE_NONE;
	pev->frame = 0;
	pev->takedamage = DAMAGE_NO;	// only the watch laser takes health
	SET_MODEL( ENT( pev ), STRING( pev->model ));
	if( FClassnameIs( pev, "item_padlock" ))
		UTIL_SetSize( pev, Vector( -4, -4, 0 ), Vector( 4, 4, 8 ));
	else
		UTIL_SetSize( pev, Vector( -6, -6, 0 ), Vector( 6, 6, 6 ));
	UTIL_SetOrigin( pev, pev->origin );

	if( FBitSet( pev->spawnflags, SF_LASERTARGET_DROP ) && DROP_TO_FLOOR( ENT( pev )) == 0 )
	{
		ALERT( at_error, "%s fell out of level at %f,%f,%f\n", STRING( pev->classname ), pev->origin.x, pev->origin.y, pev->origin.z );
		UTIL_Remove( this );
		return;
	}

	if( pev->health <= 0 )
		pev->health = 60;
	pev->max_health = pev->health;
	pev->fuser1 = pev->health;
	m_flLastHealth = pev->health;

	SetThink( &CLaserTarget::BreakThink );
	pev->nextthink = gpGlobals->time + 0.1f;
}

void CLaserTarget::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	// retail 0x42079950: removed, nothing fired
	SetThink( &CBaseEntity::SUB_Remove );
	pev->nextthink = gpGlobals->time + 0.1f;
}

void CLaserTarget::ShowProgress( BOOL show )
{
	m_fProgressVisible = show;
	CBaseEntity *pPlayer = CBaseEntity::Instance( pev->enemy );
	if( !pPlayer || !pPlayer->IsPlayer())
		return;

	MESSAGE_BEGIN( MSG_ONE, gmsgNFProgress, NULL, pPlayer->pev );
		WRITE_SHORT( entindex());
		WRITE_COORD( pev->max_health );
		WRITE_BYTE( show ? 1 : 0 );
	MESSAGE_END();
}

// retail 0x42079aa0, every 0.1 s
void CLaserTarget::BreakThink( void )
{
	pev->nextthink = gpGlobals->time + 0.1f;
	if( m_fBroken )
		return;

	if( pev->health != m_flLastHealth )
	{
		// lasered since the last tick: progress bar, glow up
		if( !m_fProgressVisible )
			ShowProgress( TRUE );
		if( !m_fGlowing )
		{
			m_nPrevEffects = pev->effects;
			m_vecPrevFixedLight = pev->fixedlight;
			pev->effects |= EF_FIXEDLIGHT;
			m_fGlowing = TRUE;
		}
		// heats up to red (retail 0x42079aa0)
		pev->fixedlight.x = Q_min( pev->fixedlight.x + 25, 255 );
		pev->fixedlight.y = Q_max( pev->fixedlight.y - 25, 0 );
		pev->fixedlight.z = Q_max( pev->fixedlight.z - 25, 0 );
		m_flLastHealth = pev->health;
		pev->fuser1 = pev->health;	// the client's bar reads it
	}
	else
	{
		if( m_fProgressVisible )
			ShowProgress( FALSE );
		if( m_fGlowing )
		{
			pev->fixedlight.x = Q_max( pev->fixedlight.x - 10, m_vecPrevFixedLight.x );
			pev->fixedlight.y = Q_min( pev->fixedlight.y + 10, m_vecPrevFixedLight.y );
			pev->fixedlight.z = Q_min( pev->fixedlight.z + 10, m_vecPrevFixedLight.z );
			if( pev->fixedlight == m_vecPrevFixedLight )
			{
				pev->effects = m_nPrevEffects;
				m_fGlowing = FALSE;
			}
		}
	}

	if( pev->health >= 1 )
		return;

	// broken
	m_fBroken = TRUE;
	if( NF_DEBUG( NF_DBG_ITEMS ))
		ALERT( at_console, "nf_debug: %s %s broken, fires '%s'\n", STRING( pev->classname ), STRING( pev->model ), STRING( pev->target ));
	EMIT_SOUND_DYN( ENT( pev ), CHAN_STATIC, STRING( m_iszBreakSound ), 1.0f, 0.8f, 0, PITCH_NORM );
	SUB_UseTargets( CBaseEntity::Instance( pev->enemy ), USE_TOGGLE, 0 );
	if( m_nBreakBody > 0 )
		SetBodygroup( 0, m_nBreakBody );
	if( m_nBreakSkin > 0 )
		pev->skin = m_nBreakSkin;
	if( m_fProgressVisible )
		ShowProgress( FALSE );

	if( FBitSet( pev->spawnflags, SF_LASERTARGET_KEEP ))
		SetThink( NULL );
	else
		SetThink( &CBaseEntity::SUB_Remove );
}

// retail CWatch 0x420ecd60: one laser frame on the hit entity
void NF_LaserHit( CBaseEntity *pEntity, CBasePlayer *pPlayer, float flAmount )
{
	if( !pEntity || !( FClassnameIs( pEntity->pev, "item_lasertarget" ) || FClassnameIs( pEntity->pev, "item_padlock" )))
		return;
	if( pEntity->pev->health < 1 )
		return;

	pEntity->pev->health -= flAmount;
	pEntity->pev->enemy = pPlayer->edict();
	if( NF_DEBUG( NF_DBG_ITEMS ) && (int)( pEntity->pev->health + flAmount ) / 10 != (int)pEntity->pev->health / 10 )
		ALERT( at_console, "nf_debug: laser on %s, health %.0f / %.0f\n", STRING( pEntity->pev->classname ),
			pEntity->pev->health, pEntity->pev->max_health );
}

// retail DisplayHudInformation (vtable +0x114)
static int NF_HudIconOf( CBaseEntity *pEntity )
{
	if( FClassnameIs( pEntity->pev, "item_lasertarget" ) && pEntity->pev->health >= 1 )
		return NF_HUDICON_WATCH;
	if( FClassnameIs( pEntity->pev, "item_locktarget" ))
		return NF_HUDICON_PDA;	// retail CLockTarget 0x4207A860, also once unlocked
	return NF_HUDICON_NONE;
}

void NF_SetHudIcon( CBasePlayer *pPlayer, int icon )
{
	if( pPlayer->m_iNFHudIcon == icon )
		return;
	pPlayer->m_iNFHudIcon = icon;
	MESSAGE_BEGIN( MSG_ONE, gmsgNFSetHudIcon, NULL, pPlayer->pev );
		WRITE_BYTE( icon );
	MESSAGE_END();
}

// retail 0x420c2d50, from CBasePlayer::PostThink
void NF_UseIconThink( CBasePlayer *pPlayer )
{
	if( pPlayer->m_iNFHudIcon == NF_HUDICON_LEVELTRANS )
		return;

	UTIL_MakeVectors( pPlayer->pev->v_angle );
	Vector vecSrc = pPlayer->pev->origin + pPlayer->pev->view_ofs;
	TraceResult tr;
	UTIL_TraceLine( vecSrc, vecSrc + gpGlobals->v_forward * 96, dont_ignore_monsters, pPlayer->edict(), &tr );

	int icon = NF_HUDICON_NONE;
	CBaseEntity *pHit = CBaseEntity::Instance( tr.pHit );
	if( tr.flFraction < 1.0f && pHit && !FNullEnt( tr.pHit ))
		icon = NF_HudIconOf( pHit );
	NF_SetHudIcon( pPlayer, icon );
}
