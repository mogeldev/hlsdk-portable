#pragma once
#ifndef NF_SEARCHLIGHT_CLIENT_H
#define NF_SEARCHLIGHT_CLIENT_H

struct cl_entity_s;
void NF_SearchlightInit( void );
void NF_SearchlightVidInit( void );
void NF_SearchlightEntity( struct cl_entity_s *entity );
void NF_SearchlightRender( void );

#endif
