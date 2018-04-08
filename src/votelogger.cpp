/*
 * votelogger.cpp
 *
 *  Created on: Dec 31, 2017
 *      Author: nullifiedcat
 */

#include "common.hpp"

namespace votelogger
{

static CatVar enabled(CV_SWITCH, "votelog", "0", "Log votes");
static CatVar requeue(CV_SWITCH, "votelog_requeue", "1", "Auto requeue on vote kick", "Auto requeue on vote kick");

void user_message(bf_read &buffer, int type)
{
	bool islocalplayer = false;
    if (!enabled)
        return;

    switch (type)
    {
    case 45:
        // Call Vote Failed
        break;
    case 46:
    {
    	islocalplayer = false;
        unsigned char caller = buffer.ReadByte();
        // unknown
        buffer.ReadByte();
        char reason[64];
        char name[64];
        buffer.ReadString(reason, 64, false, nullptr);
        buffer.ReadString(name, 64, false, nullptr);
        unsigned char eid = buffer.ReadByte();
        buffer.Seek(0);
        eid >>= 1;

        unsigned steamID = 0;
        player_info_s info;
        if (g_IEngine->GetPlayerInfo(eid, &info))
        {
            steamID = info.friendsID;
        }
        if (eid == LOCAL_E->m_IDX)
        	islocalplayer = true;

        logging::Info("Vote called to kick %s [U:1:%u] for %s", name, steamID,
                      reason);
        break;
    }
    case 47:
        logging::Info("Vote passed");
        if (islocalplayer && requeue)
        	tfmm::queue_start();
        break;
    case 48:
        logging::Info("Vote failed");
        break;
    case 49:
        logging::Info("VoteSetup?");
        break;
    }
}
}
