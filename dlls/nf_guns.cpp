/*
nf_guns.cpp - James Bond 007: Nightfire (PC) table-driven bullet weapons

MP9 (weapon_mp9), silenced MP9 (weapon_mp9_silenced), SIG552
(weapon_commando), P90 (weapon_pdw90), Kowloon (weapon_kowloon), Raptor
(weapon_raptor), Frinesi (weapon_frinesi), L96A1 (weapon_l96a1, winter
version weapon_l96a1_winter) and the minigun (weapon_minigun), with their
ammo. The values come from the retail game.dll (classes CMP9, CCommando,
CPDW90, CKowloon, CRaptor, CFrinesi, CL96A1, CMinigun; addresses in
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
	{ 0, 1, 2, -1 }, { 31.0f / 15.0f, 46.0f / 15.0f, 46.0f / 15.0f, 0.0f },
	3, 7, 8, 5, 6, 2.0f, 2.33f,
	-1, -1, 0.0f, FALSE,
	-1, 0.0f, FALSE, 0, FALSE, 0, 0.0f, BULLET_PLAYER_MP5, -1, 0, 0.0f
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
	{ 0, 1, 2, -1 }, { 31.0f / 15.0f, 46.0f / 15.0f, 46.0f / 15.0f, 0.0f },
	3, 7, 8, 5, 6, 2.0f, 2.33f,
	-1, -1, 0.0f, FALSE,
	-1, 0.0f, FALSE, 0, FALSE, 0, 0.0f, BULLET_PLAYER_MP5, -1, 0, 0.0f
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
	{ 0, 1, -1, -1 }, { 69.0f / 15.0f, 41.0f / 15.0f, 0.0f, 0.0f },
	2, 5, 6, 4, 3, 1.63f, 2.13f,
	8, 7, 34.0f / 30.0f, TRUE,
	-1, 0.0f, FALSE, 0, FALSE, 0, 0.0f, BULLET_PLAYER_MP5, -1, 0, 0.0f
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
	{ 0, 1, 2, -1 }, { 41.0f / 15.0f, 69.0f / 15.0f, 41.0f / 15.0f, 0.0f },
	3, 6, 7, 5, 4, 2.06f, 2.5f,
	-1, -1, 0.0f, FALSE,
	-1, 0.0f, FALSE, 0, FALSE, 0, 0.0f, BULLET_PLAYER_MP5, -1, 0, 0.0f
};

// Kowloon: retail id 3, "9mm" 200, clip 16, cycle 0.25, spread 0 (the PP9's
// fire helper), damage sk_plr_kowloon_bullet; reload 2.0 s either way;
// idle only with rounds in the clip (retail idle3 3.0625 s, idle1 3.75 s,
// idle2 2.5 s)
const nf_gun_info_t g_nfGunKowloon =
{
	NF_WEAPON_KOWLOON, "weapon_kowloon",
	"models/v_kowloon.mdl", "models/p_kowloon.mdl", "models/w_kowloon.mdl", "pistol",
	"9mm", 200, 16, 16,
	1, 1,
	0.25f, 0.0f, 0.0f, 0,
	"sk_plr_kowloon_bullet", 9, FALSE, "weapons/kowloon_fire1.wav", 2.0f,
	{ 0, 1, 2, -1 }, { 3.75f, 2.5f, 3.0625f, 0.0f },
	3, 7, 8, 6, 5, 2.0f, 2.0f,
	-1, -1, 0.0f, FALSE,
	4, 0.0f, TRUE, 0, FALSE, 0, 0.0f, BULLET_PLAYER_9MM, -1, 0, 0.0f
};

// Raptor: retail id 4, ammo "440" 72, clip 9, cycle 0.4, spread 0, damage
// sk_plr_raptor_bullet; reload 2.0 s / 2.6 s (empty, own sequence); idle
// delays retail: sequence length + 0-5 s, only with rounds in the clip
const nf_gun_info_t g_nfGunRaptor =
{
	NF_WEAPON_RAPTOR, "weapon_raptor",
	"models/v_raptor.mdl", "models/p_raptor.mdl", "models/w_raptor.mdl", "pistol",
	"440", 72, 9, 9,
	1, 2,
	0.4f, 0.0f, 0.0f, 0,
	"sk_plr_raptor_bullet", 20, FALSE, "weapons/raptor_fire1.wav", 4.0f,
	{ 0, 1, 2, 3 }, { 4.73f, 3.33f, 2.33f, 2.0f },
	4, 12, 13, 5, 7, 2.0f, 2.6f,
	-1, -1, 0.0f, FALSE,
	6, 5.0f, TRUE, 0, FALSE, 0, 0.0f, BULLET_PLAYER_357, -1, 0, 0.0f
};

// Frinesi: retail id 10, "buckshot" 125, clip 8, 16 shells with the gun;
// primary 5 pellets in a 10 degree cone, 0.23 s; secondary ("shoot_big")
// 10 pellets in 5 degrees, 1.0 s; range 2048; damage per pellet
// sk_plr_buckshot * (1 - distance / 2048)^2, at least 1 (NF_FireBuckshot);
// reloads shell by shell (start 0.4 s, 0.33 s per shell, pump 1.5 s);
// idle delays retail: 6-8 s, 4.6-6.6 s, 6-8 s, 1.33-3.33 s
const nf_gun_info_t g_nfGunFrinesi =
{
	NF_WEAPON_FRINESI, "weapon_frinesi",
	"models/v_frinesi.mdl", "models/p_frinesi.mdl", "models/w_frinesi.mdl", "shotgun",
	"buckshot", 125, 8, 16,
	3, 1,
	0.23f, 0.08716f, 0.0f, 0,
	"sk_plr_buckshot", 8, FALSE, "weapons/frinesi_fire.wav", 5.0f,
	{ 0, 8, 9, 10 }, { 6.0f, 4.6f, 6.0f, 1.33f },
	1, 6, 7, 3, 3, 0.33f, 0.33f,
	-1, -1, 0.0f, FALSE,
	-1, 2.0f, FALSE, 0, FALSE, 5, 2048.0f, BULLET_PLAYER_BUCKSHOT, 2, 10, 0.04362f
};

// L96A1: retail id 12, "762mm" 50, clip 10, cycle 1.65, spread 0, damage
// sk_plr_sniper_bullet, loud (volume 1280); the view model has no fire
// sound event, the fire event plays l96_fire1; reload 3.1 s / 4.5 s
// ("reload_charge" when empty); zoom in CNightfireSniper
const nf_gun_info_t g_nfGunL96 =
{
	NF_WEAPON_L96A1, "weapon_l96a1",
	"models/v_l96_sniper.mdl", "models/p_l96_sniper.mdl", "models/w_l96_sniper.mdl", "sniper_rifle",
	"762mm", 50, 10, 10,
	3, 2,
	1.65f, 0.0f, 0.0f, 0,
	"sk_plr_sniper_bullet", 100, FALSE, "weapons/l96_fire1.wav", 5.0f,
	{ 0, 1, -1, -1 }, { 3.75f, 2.5f, 0.0f, 0.0f },
	2, 7, 6, 4, 5, 3.1f, 4.5f,
	-1, -1, 0.0f, FALSE,
	3, 0.0f, TRUE, 1280, TRUE, 0, 0.0f, BULLET_PLAYER_357, -1, 0, 0.0f
};

// L96A1 winter: retail id 13, own view / world model, the L96A1's player
// model and code
const nf_gun_info_t g_nfGunL96Winter =
{
	NF_WEAPON_L96A1_WINTER, "weapon_l96a1_winter",
	"models/v_l96_sniper_winter.mdl", "models/p_l96_sniper.mdl", "models/w_l96_sniper_winter.mdl", "sniper_rifle",
	"762mm", 50, 10, 10,
	3, 3,
	1.65f, 0.0f, 0.0f, 0,
	"sk_plr_sniper_bullet", 100, FALSE, "weapons/l96_fire1.wav", 5.0f,
	{ 0, 1, -1, -1 }, { 3.75f, 2.5f, 0.0f, 0.0f },
	2, 7, 6, 4, 5, 3.1f, 4.5f,
	-1, -1, 0.0f, FALSE,
	3, 0.0f, TRUE, 1280, TRUE, 0, 0.0f, BULLET_PLAYER_357, -1, 0, 0.0f
};

// minigun: retail id 9, ammo "minigun" 200, clip 100, cycle 0.075, spread
// 0.04, damage sk_plr_minigun_bullet; spin-up 0.66 s, spin-down 1.13 s;
// reload 4.73 s; idle delay 8-16 s. The laser sight (events/minigunlaser.sc)
// is not done yet [open]
const nf_gun_info_t g_nfGunMinigun =
{
	NF_WEAPON_MINIGUN, "weapon_minigun",
	"models/v_mini.mdl", "models/p_mini.mdl", "models/w_mini.mdl", "mini",
	"minigun", 200, 100, 100,
	3, 4,
	0.075f, 0.04f, 0.0f, 0,
	"sk_plr_minigun_bullet", 15, FALSE, "weapons/mini_fire.wav", 0.5f,
	{ 0, 1, -1, -1 }, { 8.0f, 8.0f, 0.0f, 0.0f },
	3, 6, 7, 5, 5, 4.73f, 4.73f,
	-1, -1, 0.0f, FALSE,
	-1, 8.0f, FALSE, 0, FALSE, 0, 0.0f, BULLET_PLAYER_MP5, -1, 0, 0.0f
};

const nf_gun_info_t *NF_GunInfo( int id )
{
	static const nf_gun_info_t *const guns[] =
	{
		&g_nfGunMP9, &g_nfGunMP9Silenced, &g_nfGunCommando, &g_nfGunPDW90, &g_nfGunKowloon,
		&g_nfGunRaptor, &g_nfGunFrinesi, &g_nfGunL96, &g_nfGunL96Winter, &g_nfGunMinigun
	};
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
LINK_ENTITY_TO_CLASS( weapon_kowloon, CNightfireKowloon )
LINK_ENTITY_TO_CLASS( weapon_raptor, CNightfireRaptor )
LINK_ENTITY_TO_CLASS( weapon_frinesi, CNightfireFrinesi )
LINK_ENTITY_TO_CLASS( weapon_l96a1, CNightfireL96 )
LINK_ENTITY_TO_CLASS( weapon_l96a1_winter, CNightfireL96Winter )
LINK_ENTITY_TO_CLASS( weapon_minigun, CNightfireMinigun )

#ifndef CLIENT_DLL
// retail buckshot (FireBulletsPlayer bullet type 6, 0x42043318): every
// pellet does damage * (1 - fraction)^2, at least 1, with fraction = the
// hit distance over the range. Otherwise FireBulletsPlayer's loop.
static void NF_FireBuckshot( CBasePlayer *pPlayer, int pellets, Vector vecSrc, Vector vecDirShooting, float spread, float range, float damage, int shared_rand )
{
	TraceResult tr;

	ClearMultiDamage();
	gMultiDamage.type = DMG_BULLET | DMG_NEVERGIB;

	for( int iShot = 1; iShot <= pellets; iShot++ )
	{
		// circular gaussian spread, as in FireBulletsPlayer
		float x = UTIL_SharedRandomFloat( shared_rand + iShot, -0.5f, 0.5f ) + UTIL_SharedRandomFloat( shared_rand + ( 1 + iShot ), -0.5f, 0.5f );
		float y = UTIL_SharedRandomFloat( shared_rand + ( 2 + iShot ), -0.5f, 0.5f ) + UTIL_SharedRandomFloat( shared_rand + ( 3 + iShot ), -0.5f, 0.5f );

		Vector vecDir = vecDirShooting + x * spread * gpGlobals->v_right + y * spread * gpGlobals->v_up;
		Vector vecEnd = vecSrc + vecDir * range;
		UTIL_TraceLine( vecSrc, vecEnd, dont_ignore_monsters, pPlayer->edict(), &tr );

		if( tr.flFraction != 1.0f )
		{
			CBaseEntity *pEntity = CBaseEntity::Instance( tr.pHit );
			float f = 1.0f - tr.flFraction;
			float flDamage = Q_max( damage * f * f, 1.0f );

			if( pEntity )
				pEntity->TraceAttack( pPlayer->pev, flDamage, vecDir.Normalize(), &tr, DMG_BULLET | DMG_NEVERGIB );
			TEXTURETYPE_PlaySound( &tr, vecSrc, vecEnd, BULLET_PLAYER_BUCKSHOT );
			DecalGunshot( &tr, BULLET_PLAYER_BUCKSHOT );
		}
	}
	ApplyMultiDamage( pPlayer->pev, pPlayer->pev );
}
#endif

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
	m_fInSpecialReload = 0;
	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.5f;
	SendWeaponAnim( Info()->seq_holster, UseDecrement() ? 1 : 0, Body());
}

void CNightfireGun::FireRound( int pellets, float spread, int seq )
{
	const nf_gun_info_t *g = Info();

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
		m_pPlayer->m_iWeaponVolume = g->volume ? g->volume : NORMAL_GUN_VOLUME;
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

	float range = g->range > 0.0f ? g->range : 8192.0f;
	Vector vecSrc = m_pPlayer->GetGunPosition();
	Vector vecAiming = m_pPlayer->GetAutoaimVector( AUTOAIM_5DEGREES );
	Vector vecDir = g_vecZero;
#ifndef CLIENT_DLL
	if( g->bullet == BULLET_PLAYER_BUCKSHOT )
		NF_FireBuckshot( m_pPlayer, pellets, vecSrc, vecAiming, spread, range, damage, m_pPlayer->random_seed );
	else
#endif
	vecDir = m_pPlayer->FireBulletsPlayer( pellets, vecSrc, vecAiming, Vector( spread, spread, spread ), range,
		g->bullet, 0, damage, m_pPlayer->pev, m_pPlayer->random_seed );

	if( m_iClip == 0 && seq == g->seq_fire && g->seq_fire_last >= 0 )
		seq = g->seq_fire_last;

	// iparam1 weapon id, iparam2 fire sequence | body << 8
	PLAYBACK_EVENT_FULL( flags, m_pPlayer->edict(), m_usFire, 0.0, g_vecZero, g_vecZero, vecDir.x, vecDir.y,
		g->id, seq | ( Body() << 8 ), ( m_iClip == 0 ) ? 1 : 0, 0 );
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

	float spread = ( m_fireState == 1 && g->spread_mode1 > 0 ) ? g->spread_mode1 : g->spread;
	FireRound( g->pellets > 0 ? g->pellets : 1, spread, g->seq_fire );

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

	// the retail pistols and the L96A1 keep still with an empty clip
	if( g->idle_needs_clip && m_iClip == 0 )
		return;

	int count = 0;
	while( count < 4 && g->seq_idle[count] >= 0 )
		count++;
	if( !count )
		return;

	int i = UTIL_SharedRandomLong( m_pPlayer->random_seed, 0, count - 1 );
	SendWeaponAnim( g->seq_idle[i], UseDecrement() ? 1 : 0, Body());
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + g->idle_time[i];
	if( g->idle_rand > 0.0f )
		m_flTimeWeaponIdle += UTIL_SharedRandomFloat( m_pPlayer->random_seed + 1, 0.0f, g->idle_rand );
}

//=========================================================
// Frinesi (retail CFrinesi, 0x420d7f00-0x420d89a0)
//=========================================================
enum nf_frinesi_e
{
	FRINESI_IDLE1 = 0,
	FRINESI_SHOOT,
	FRINESI_SHOOT_BIG,
	FRINESI_RELOAD,		// one shell
	FRINESI_PUMP,
	FRINESI_START_RELOAD,
	FRINESI_DRAW,
	FRINESI_HOLSTER,
};

void CNightfireFrinesi::Fire( BOOL big )
{
	const nf_gun_info_t *g = Info();

	if( m_pPlayer->pev->waterlevel == 3 )
	{
		PlayEmptySound();
		m_flNextPrimaryAttack = m_flNextSecondaryAttack = GetNextAttackDelay( 0.15f );
		return;
	}

	if( m_iClip <= 0 )
	{
		Reload();
		if( m_iClip == 0 )
			PlayEmptySound();
		return;
	}

	FireRound( big ? g->pellets2 : g->pellets, big ? g->spread2 : g->spread, big ? g->seq_fire2 : g->seq_fire );
	m_fInSpecialReload = 0;

	// the big shot works the pump itself (its sequence has the cock sound)
	m_flNextPrimaryAttack = m_flNextSecondaryAttack = GetNextAttackDelay( big ? 1.0f : g->cycle );
	if( big )
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + ( m_iClip ? 5.0f : 1.0f );
	else
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 0.5f;
}

void CNightfireFrinesi::Reload( void )
{
	const nf_gun_info_t *g = Info();

	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0 || m_iClip == g->max_clip )
		return;

	// not while a shot is still cycling
	if( m_flNextPrimaryAttack > UTIL_WeaponTimeBase())
		return;

	if( m_fInSpecialReload == 0 )
	{
		SendWeaponAnim( FRINESI_START_RELOAD, UseDecrement() ? 1 : 0, Body());
		m_fInSpecialReload = 1;
		m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.4f;
		m_flTimeWeaponIdle = m_flNextPrimaryAttack = m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + 0.4f;
		return;
	}

	if( m_fInSpecialReload == 1 )
	{
		if( m_flTimeWeaponIdle > UTIL_WeaponTimeBase())
			return;

		// one shell
		m_fInSpecialReload = 2;
		SendWeaponAnim( FRINESI_RELOAD, UseDecrement() ? 1 : 0, Body());
		m_flTimeWeaponIdle = m_flNextPrimaryAttack = m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + g->reload_time;
		return;
	}

	// the shell is in
	m_iClip++;
	m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType]--;
	m_fInSpecialReload = 1;
}

void CNightfireFrinesi::WeaponIdle( void )
{
	const nf_gun_info_t *g = Info();

	ResetEmptySound();
	m_pPlayer->GetAutoaimVector( AUTOAIM_5DEGREES );

	if( m_flTimeWeaponIdle > UTIL_WeaponTimeBase())
		return;

	int ammo = m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType];
	if( m_iClip == 0 && m_fInSpecialReload == 0 && ammo )
	{
		Reload();
		return;
	}

	if( m_fInSpecialReload != 0 )
	{
		if( m_iClip < g->max_clip && ammo )
		{
			Reload();
			return;
		}

		// done: work the pump
		SendWeaponAnim( FRINESI_PUMP, UseDecrement() ? 1 : 0, Body());
		m_fInSpecialReload = 0;
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 1.5f;
		return;
	}

	CNightfireGun::WeaponIdle();
}

//=========================================================
// L96A1 zoom (retail CL96A1, 0x420dd580-0x420ddf00)
//=========================================================
void CNightfireSniper::SetFOV( int fov )
{
	m_pPlayer->pev->fov = m_pPlayer->m_iFOV = fov;
}

void CNightfireSniper::SecondaryAttack( void )
{
	// retail: off -> FOV 40 -> FOV 10 -> off (76, the retail default)
	m_fireState = ( m_fireState + 1 ) % 3;
	SetFOV( ZoomFOV());
	m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + 0.3f;
}

void CNightfireSniper::Holster( int skiplocal )
{
	m_fireState = 0;
	SetFOV( 0 );
	CNightfireGun::Holster( skiplocal );
}

void CNightfireSniper::Reload( void )
{
	CNightfireGun::Reload();

	// retail: the zoom drops for the reload and comes back afterwards
	// (CBasePlayerWeapon::ItemPostFrame, player +0x8B4 / +0x8B8)
	if( m_fInReload && m_fireState )
		SetFOV( 0 );
}

void CNightfireSniper::ItemPostFrame( void )
{
	CNightfireGun::ItemPostFrame();

	if( m_fireState && !m_fInReload && m_pPlayer->m_iFOV != ZoomFOV())
		SetFOV( ZoomFOV());
}

//=========================================================
// minigun (retail CMinigun, 0x420e08a0-0x420e1100)
//=========================================================
enum nf_minigun_e
{
	MINIGUN_IDLE1 = 0,
	MINIGUN_IDLE2,
	MINIGUN_SPINUP,
	MINIGUN_FIRE,
	MINIGUN_SPINDOWN,
	MINIGUN_RELOAD,
	MINIGUN_DRAW,
	MINIGUN_HOLSTER,
	MINIGUN_SPIN,		// turning without ammo
};

// spin states (retail +0xDC)
enum
{
	MINIGUN_STOPPED = 0,
	MINIGUN_SPINNING_UP,
	MINIGUN_FIRING,
	MINIGUN_SPINNING_DOWN,
	MINIGUN_EMPTY,
};

BOOL CNightfireMinigun::Deploy( void )
{
	pev->fuser1 = 0.0f;
	return CNightfireGun::Deploy();
}

void CNightfireMinigun::Holster( int skiplocal )
{
	m_fInAttack = MINIGUN_STOPPED;
	pev->fuser1 = 0.0f;
	CNightfireGun::Holster( skiplocal );
}

void CNightfireMinigun::PrimaryAttack( void )
{
	const nf_gun_info_t *g = Info();

	if( m_pPlayer->pev->waterlevel == 3 )
	{
		PlayEmptySound();
		m_flNextPrimaryAttack = GetNextAttackDelay( 0.15f );
		return;
	}

	if( m_fInAttack == MINIGUN_STOPPED )
	{
		m_fInAttack = MINIGUN_SPINNING_UP;
		pev->fuser1 = 0.66f;
		SendWeaponAnim( MINIGUN_SPINUP, UseDecrement() ? 1 : 0, Body());
		m_flNextPrimaryAttack = GetNextAttackDelay( g->cycle );
		return;
	}

	// still spinning up, or spinning down (retail: a spin-down only lets go
	// of the trigger once it is over, then the gun fires at once)
	if(( m_fInAttack == MINIGUN_SPINNING_UP || m_fInAttack == MINIGUN_SPINNING_DOWN ) && pev->fuser1 > 0.0f )
		return;

	if( m_iClip <= 0 )
	{
		// the barrels turn without ammo (retail restarts the sequence every
		// 0.15 s; started once here so it does not stutter)
		if( m_fInAttack != MINIGUN_EMPTY )
			SendWeaponAnim( MINIGUN_SPIN, UseDecrement() ? 1 : 0, Body());
		m_fInAttack = MINIGUN_EMPTY;
		m_flNextPrimaryAttack = GetNextAttackDelay( 0.15f );
		return;
	}

	m_fInAttack = MINIGUN_FIRING;
	pev->fuser1 = 0.0f;
	FireRound( 1, g->spread, g->seq_fire );

	m_flNextPrimaryAttack = GetNextAttackDelay( g->cycle );
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + UTIL_SharedRandomFloat( m_pPlayer->random_seed, 5, 15 );
}

void CNightfireMinigun::Reload( void )
{
	CNightfireGun::Reload();

	if( m_fInReload )
	{
		m_fInAttack = MINIGUN_STOPPED;
		pev->fuser1 = 0.0f;
	}
}

void CNightfireMinigun::WeaponIdle( void )
{
	if( m_fInAttack != MINIGUN_STOPPED && m_fInAttack != MINIGUN_SPINNING_DOWN )
	{
		// trigger released: spin down
		m_fInAttack = MINIGUN_SPINNING_DOWN;
		pev->fuser1 = 1.13f;
		SendWeaponAnim( MINIGUN_SPINDOWN, UseDecrement() ? 1 : 0, Body());
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 0.4f;
		return;
	}

	if( m_fInAttack == MINIGUN_SPINNING_DOWN && pev->fuser1 > 0.0f )
	{
		m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + pev->fuser1;
		return;
	}

	// stopped (CNightfireGun::WeaponIdle clears m_fInAttack)
	CNightfireGun::WeaponIdle();
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
NF_AMMO( CNightfireAmmoKowloon, "models/w_ammo_kowloon.mdl", "9mm", 16, 200 )
NF_AMMO( CNightfireAmmoRaptor, "models/w_ammo_raptor.mdl", "440", 9, 72 )
NF_AMMO( CNightfireAmmoShotgun, "models/w_ammo_shotgun.mdl", "buckshot", 20, 125 )
NF_AMMO( CNightfireAmmoSniper, "models/w_ammo_sniper.mdl", "762mm", 10, 50 )
NF_AMMO( CNightfireAmmoMinigun, "models/w_ammo_mini.mdl", "minigun", 100, 200 )

LINK_ENTITY_TO_CLASS( ammo_mp9, CNightfireAmmoMP9 )
LINK_ENTITY_TO_CLASS( ammo_commando, CNightfireAmmoCommando )
LINK_ENTITY_TO_CLASS( ammo_pdw90, CNightfireAmmoPDW90 )
LINK_ENTITY_TO_CLASS( ammo_kowloon, CNightfireAmmoKowloon )
LINK_ENTITY_TO_CLASS( ammo_raptor, CNightfireAmmoRaptor )
LINK_ENTITY_TO_CLASS( ammo_shotgun, CNightfireAmmoShotgun )
LINK_ENTITY_TO_CLASS( ammo_sniper, CNightfireAmmoSniper )
// the retail ammo table says ammo_minigun, the maps place ammo_mini
LINK_ENTITY_TO_CLASS( ammo_mini, CNightfireAmmoMinigun )
LINK_ENTITY_TO_CLASS( ammo_minigun, CNightfireAmmoMinigun )
#endif
