/*
nf_wheel.h - James Bond 007: Nightfire (PC) weapon/gadget selector
*/
#pragma once
#ifndef NF_WHEEL_H
#define NF_WHEEL_H

void NF_WheelInit( void );
void NF_WheelReset( void );
void NF_WheelMapReset( void );
void NF_WheelVidInit( void );
bool NF_WheelEnabled( void );
void NF_WheelCycle( int direction );
void NF_WheelMode( int mode );
void NF_WheelActive( int id );
void NF_WheelDraw( float time, int intermission );

#endif // NF_WHEEL_H
