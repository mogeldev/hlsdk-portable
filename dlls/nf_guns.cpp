/*
nf_guns.cpp - James Bond 007: Nightfire (PC) table-driven bullet weapons

MP9 (weapon_mp9), silenced MP9 (weapon_mp9_silenced), SIG552
(weapon_commando) and P90 (weapon_pdw90), with their ammo. The values come
from the retail game.dll (classes CMP9, CCommando, CPDW90; addresses in
docs/retail-game-logic.md of the project repo). Like the PP9 they are
reworked Half-Life weapons: the view models play their own fire / reload /
draw sounds and the muzzle flash as model events, so the fire event only
adds the view animation, the shell, the bullets and, for other players, the
sound.

Built into the server and the client (weapon prediction).
*/

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "nodes.h"
#include "player.h"
#include "nf_weapons.h"

//=========================================================
// weapon descriptions
//=========================================================

// MP9: retail id 5, "9mm" 200, clip 32, cycle 0.1, spread 0.04, reload 2.0 s /
// 2.33 s (empty); the two-handed sequence set (0-8) is used, the one-handed
// set (9-17) and the alt-fire sequences are not yet [open]
const nf_gun_info_t g_nfGunMP9 =
{
	NF_WEAPON_MP9, "weapon_mp9",
	"models/v_mp9.mdl", "models/p_mp9.mdl", "models/w_mp9.mdl", "mp9",
	"9mm", 200, 32, 32,
	2, 0,
	0.1f, 0.04f, 0.0f, 0,
	"sk_plr_mp9_bullet", 9, FALSE, "weapons/mp9_fire1.wav", 1.0f,
	{ 0, 1, 2 }, { 31.0f / 15.0f, 46.0f / 15.0f, 46.0f / 15.0f },
	3, 7, 8, 5, 6, 2.0f, 2.33f,
	-1, -1, 0.0f, FALSE
};

// silenced MP9: retail id 6, own view model, otherwise the MP9 [assumed:
// same timings, the retail class shares CMP9's code]
const nf_gun_info_t g_nfGunMP9Silenced =
{
	NF_WEAPON_MP9_SILENCED, "weapon_mp9_silenced",
	"models/v_mp9_silenced.mdl", "models/p_mp9.mdl", "models/w_mp9_silenced.mdl", "mp9",
	"9mm", 200, 32, 32,
	2, 1,
	0.1f, 0.04f, 0.0f, 0,
	"sk_plr_mp9_bullet", 9, TRUE, "weapons/mp9_fire_sil1.wav", 1.0f,
	{ 0, 1, 2 }, { 31.0f / 15.0f, 46.0f / 15.0f, 46.0f / 15.0f },
	3, 7, 8, 5, 6, 2.0f, 2.33f,
	-1, -1, 0.0f, FALSE
};

// SIG552: retail id 7, "556mm" 200, clip 30, cycle 0.075; fire mode
// (m_nWeaponMode) 0 = 3-round burst, spread 0.01; 1 = automatic, spread
// 0.04; the secondary attack switches it (sequences SEMI / AUTO, selector
// shown by the "switch" bodygroup); reload 1.63 s / 2.13 s (empty). The
// laser sight (events/commandolaser.sc) is not done yet [open]
const nf_gun_info_t g_nfGunCommando =
{
	NF_WEAPON_COMMANDO, "weapon_commando",
	"models/v_commando.mdl", "models/p_commando.mdl", "models/w_commando.mdl", "sig552",
	"556mm", 200, 30, 30,
	3, 0,
	0.075f, 0.01f, 0.04f, 3,
	"sk_plr_commando_bullet", 11, FALSE, "weapons/sig552_fire1.wav", 1.2f,
	{ 0, 1, -1 }, { 69.0f / 15.0f, 41.0f / 15.0f, 0.0f },
	2, 5, 6, 4, 3, 1.63f, 2.13f,
	8, 7, 34.0f / 30.0f, TRUE
};

// P90: retail id 8, "28mm" 300, clip 50, cycle 0.065, spread 0.04, reload
// 2.06 s / 2.5 s (empty)
const nf_gun_info_t g_nfGunPDW90 =
{
	NF_WEAPON_PDW90, "weapon_pdw90",
	"models/v_pdw90.mdl", "models/p_pdw90.mdl", "models/w_pdw90.mdl", "pdw90",
	"28mm", 300, 50, 50,
	2, 2,
	0.065f, 0.04f, 0.0f, 0,
	"sk_plr_pdw90_bullet", 13, FALSE, "weapons/p90_fire1.wav", 1.0f,
	{ 0, 1, 2 }, { 41.0f / 15.0f, 69.0f / 15.0f, 41.0f / 15.0f },
	3, 6, 7, 5, 4, 2.06f, 2.5f,
	-1, -1, 0.0f, FALSE
};

const nf_gun_info_t *NF_GunInfo( int id )
{
	static const nf_gun_info_t *const guns[] = { &g_nfGunMP9, &g_nfGunMP9Silenced, &g_nfGunCommando, &g_nfGunPDW90 };
	for( size_t i = 0; i < ARRAYSIZE( guns ); i++ )
	{
		if( guns[i]->id == id )
			return guns[i];
	}
	return NULL;
}

LINK_ENTITY_TO_CLASS( weapon_mp9, CNightfireMP9 )
LINK_ENTITY_TO_CLASS( weapon_mp9_silenced, CNightfireMP9Silenced )
LINK_ENTITY_TO_CLASS( weapon_commando, CNightfireCommando )
LINK_ENTITY_TO_CLASS( weapon_pdw90, CNightfirePDW90 )

//=========================================================
// CNightfireGun
//=========================================================
void CNightfireGun::Spawn( void )
{
	Precache();
	m_iId = Info()->id;
	SET_MODEL( ENT( pev ), Info()->wmodel );
	m_iDefaultAmmo = Info()->default_give;
	FallInit();
}

void CNightfireGun::Precache( void )
{
	const nf_gun_info_t *g = Info();

	PRECACHE_MODEL( g->vmodel );
	PRECACHE_MODEL( g->pmodel );
	PRECACHE_MODEL( g->wmodel );
	PRECACHE_MODEL( "models/shell.mdl" );
	PRECACHE_SOUND( g->fire_sound );
	PRECACHE_SOUND( "items/9mmclip1.wav" );

	m_usFire = PRECACHE_EVENT( 1, "events/nfgun.sc" );
}

int CNightfireGun::GetItemInfo( ItemInfo *p )
{
	const nf_gun_info_t *g = Info();

	p->pszName = STRING( pev->classname );
	p->pszAmmo1 = g->ammo;
	p->iMaxAmmo1 = g->max_ammo;
	p->pszAmmo2 = NULL;
	p->iMaxAmmo2 = -1;
	p->iMaxClip = g->max_clip;
	p->iSlot = g->slot;
	p->iPosition = g->position;
	p->iFlags = 0;
	p->iId = m_iId = g->id;
	p->iWeight = 15;	// [assumed] HL MP5 weight

	return 1;
}

int CNightfireGun::AddToPlayer( CBasePlayer *pPlayer )
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

BOOL CNightfireGun::Deploy( void )
{
	m_fInAttack = 0;
	return DefaultDeploy( Info()->vmodel, Info()->pmodel, Info()->seq_draw, Info()->animext, UseDecrement() ? 1 : 0, Body());
}

void CNightfireGun::Holster( int skiplocal )
{
	m_fInReload = FALSE;
	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.5f;
	SendWeaponAnim( Info()->seq_holster, UseDecrement() ? 1 : 0, Body());
}

void CNightfireGun::PrimaryAttack( void )
{
	const nf_gun_info_t *g = Info();

	if( m_iClip <= 0 )
	{
		PlayEmptySound();
		m_flNextPrimaryAttack = GetNextAttackDelay( 0.2f );
		return;
	}

	// the retail weapons do not fire under water
	if( m_pPlayer->pev->waterlevel == 3 )
	{
		PlayEmptySound();
		m_flNextPrimaryAttack = GetNextAttackDelay( 0.15f );
		return;
	}

	// burst: only so many rounds per trigger pull (WeaponIdle resets it)
	int burst = m_fireState == 0 ? g->burst : 0;
	if( burst > 0 && m_fInAttack >= burst )
		return;
	m_fInAttack++;

	m_iClip--;

	m_pPlayer->pev->effects = (int)( m_pPlayer->pev->effects ) | EF_MUZZLEFLASH;
	m_pPlayer->SetAnimation( PLAYER_ATTACK1 );

	if( g->quiet )
	{
		m_pPlayer->m_iWeaponVolume = QUIET_GUN_VOLUME;
		m_pPlayer->m_iWeaponFlash = DIM_GUN_FLASH;
	}
	else
	{
		m_pPlayer->m_iWeaponVolume = NORMAL_GUN_VOLUME;
		m_pPlayer->m_iWeaponFlash = NORMAL_GUN_FLASH;
	}

	int flags;
#if CLIENT_WEAPONS
	flags = FEV_NOTHOST;
#else
	flags = 0;
#endif

	int damage = 0;
#ifndef CLIENT_DLL
	damage = (int)NF_SkillValue( g->damage );
	if( damage < 1 )
		damage = g->fallback_damage;	// skill.cfg missing
#endif

	float spread = ( m_fireState == 1 && g->spread_mode1 > 0 ) ? g->spread_mode1 : g->spread;
	Vector vecSrc = m_pPlayer->GetGunPosition();
	Vector vecAiming = m_pPlayer->GetAutoaimVector( AUTOAIM_5DEGREES );
	Vector vecDir = m_pPlayer->FireBulletsPlayer( 1, vecSrc, vecAiming, Vector( spread, spread, spread ), 8192,
		BULLET_PLAYER_MP5, 0, damage, m_pPlayer->pev, m_pPlayer->random_seed );

	// iparam1 weapon id, iparam2 fire sequence | body << 8
	PLAYBACK_EVENT_FULL( flags, m_pPlayer->edict(), m_usFire, 0.0, g_vecZero, g_vecZero, vecDir.x, vecDir.y,
		g->id, g->seq_fire | ( Body() << 8 ), ( m_iClip == 0 ) ? 1 : 0, 0 );

	m_flNextPrimaryAttack = GetNextAttackDelay( g->cycle );
	if( m_flNextSecondaryAttack < m_flNextPrimaryAttack )
		m_flNextSecondaryAttack = m_flNextPrimaryAttack;
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10, 15 );
}

void CNightfireGun::SecondaryAttack( void )
{
	const nf_gun_info_t *g = Info();

	if( g->seq_mode0 < 0 )
	{
		m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + 0.5f;
		return;
	}

	// switch the fire mode (SIG552: burst <-> automatic)
	m_fireState = !m_fireState;
	SendWeaponAnim( m_fireState ? g->seq_mode1 : g->seq_mode0, UseDecrement() ? 1 : 0, Body());

	m_flNextPrimaryAttack = m_flNextSecondaryAttack = GetNextAttackDelay( g->mode_time );
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + g->mode_time;
}

void CNightfireGun::Reload( void )
{
	const nf_gun_info_t *g = Info();

	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0 || m_iClip == g->max_clip )
		return;

	int iResult;
	if( m_iClip == 0 )
		iResult = DefaultReload( g->max_clip, g->seq_reload_empty, g->reload_empty_time, Body());
	else
		iResult = DefaultReload( g->max_clip, g->seq_reload, g->reload_time, Body());

	if( iResult )
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10, 15 );
}

void CNightfireGun::WeaponIdle( void )
{
	const nf_gun_info_t *g = Info();

	// called only while neither attack button is held: next trigger pull
	m_fInAttack = 0;

	ResetEmptySound();
	m_pPlayer->GetAutoaimVector( AUTOAIM_5DEGREES );

	if( m_flTimeWeaponIdle > UTIL_WeaponTimeBase())
		return;

	int count = 0;
	while( count < 3 && g->seq_idle[count] >= 0 )
		count++;
	if( !count )
		return;

	int i = UTIL_SharedRandomLong( m_pPlayer->random_seed, 0, count - 1 );
	SendWeaponAnim( g->seq_idle[i], UseDecrement() ? 1 : 0, Body());
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + g->idle_time[i];
}

//=========================================================
// ammo (retail AddAmmo amounts)
//=========================================================
#ifndef CLIENT_DLL
class CNightfireAmmo : public CBasePlayerAmmo
{
public:
	virtual const char *Model( void ) = 0;
	virtual const char *Type( void ) = 0;
	virtual int Give( void ) = 0;
	virtual int Max( void ) = 0;

	void Spawn( void )
	{
		Precache();
		SET_MODEL( ENT( pev ), Model());
		CBasePlayerAmmo::Spawn();
	}

	void Precache( void )
	{
		PRECACHE_MODEL( Model());
		PRECACHE_SOUND( "items/9mmclip1.wav" );
	}

	BOOL AddAmmo( CBaseEntity *pOther )
	{
		if( pOther->GiveAmmo( Give(), Type(), Max()) != -1 )
		{
			EMIT_SOUND( ENT( pev ), CHAN_ITEM, "items/9mmclip1.wav", 1, ATTN_NORM );
			return TRUE;
		}
		return FALSE;
	}
};

#define NF_AMMO( cls, model, type, give, max ) \
class cls : public CNightfireAmmo \
{ \
public: \
	const char *Model( void ) { return model; } \
	const char *Type( void ) { return type; } \
	int Give( void ) { return give; } \
	int Max( void ) { return max; } \
};

NF_AMMO( CNightfireAmmoMP9, "models/w_ammo_mp9.mdl", "9mm", 32, 200 )
NF_AMMO( CNightfireAmmoCommando, "models/w_ammo_commando.mdl", "556mm", 30, 200 )
NF_AMMO( CNightfireAmmoPDW90, "models/w_ammo_pdw90.mdl", "28mm", 50, 300 )

LINK_ENTITY_TO_CLASS( ammo_mp9, CNightfireAmmoMP9 )
LINK_ENTITY_TO_CLASS( ammo_commando, CNightfireAmmoCommando )
LINK_ENTITY_TO_CLASS( ammo_pdw90, CNightfireAmmoPDW90 )
#endif
