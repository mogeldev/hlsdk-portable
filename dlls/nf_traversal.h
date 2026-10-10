#ifndef NF_TRAVERSAL_H
#define NF_TRAVERSAL_H

class CBaseEntity;
class CBasePlayer;

void NF_TraversalPreThink( CBasePlayer *player );
void NF_TraversalPostThink( CBasePlayer *player );
void NF_TraversalReset( CBasePlayer *player, BOOL redeploy );
void NF_TraversalRestore( CBasePlayer *player, BOOL transition );
BOOL NF_TraversalCommand( CBaseEntity *player, const char *command );

#endif
