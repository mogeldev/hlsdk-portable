/*
nf_hudmsg.h - James Bond 007: Nightfire (PC) level messages ("HudMsg")
*/
#pragma once
#ifndef NF_HUDMSG_H
#define NF_HUDMSG_H

void NF_HudMsgInit( void );		// CHud::Init
void NF_HudMsgReset( void );		// CHud::VidInit (map change)
void NF_HudMsgDraw( float flTime );	// CHud::Redraw

#endif // NF_HUDMSG_H
