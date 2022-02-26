// Copyright (c) 2017-2022 The Advanced Technology Coin
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ACTIVEGOLDMINENODE_H
#define ACTIVEGOLDMINENODE_H

#include "chainparams.h"
#include "key.h"
#include "net.h"
#include "primitives/transaction.h"

class CActiveGoldminenode;

static const int ACTIVE_GOLDMINENODE_INITIAL          = 0; // initial state
static const int ACTIVE_GOLDMINENODE_SYNC_IN_PROCESS  = 1;
static const int ACTIVE_GOLDMINENODE_INPUT_TOO_NEW    = 2;
static const int ACTIVE_GOLDMINENODE_NOT_CAPABLE      = 3;
static const int ACTIVE_GOLDMINENODE_STARTED          = 4;

extern CActiveGoldminenode activeGoldminenode;

// Responsible for activating the Goldminenode and pinging the network
class CActiveGoldminenode
{
public:
    enum goldminenode_type_enum_t {
        GOLDMINENODE_UNKNOWN = 0,
        GOLDMINENODE_REMOTE  = 1
    };

private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    goldminenode_type_enum_t eType;

    bool fPingerEnabled;

    /// Ping Goldminenode
    bool SendGoldminenodePing(CConnman& connman);

    //  sentinel ping data
    int64_t nSentinelPingTime;
    uint32_t nSentinelVersion;

public:
    // Keys for the active Goldminenode
    CPubKey pubKeyGoldminenode;
    CKey keyGoldminenode;

    // Initialized while registering Goldminenode
    COutPoint outpoint;
    CService service;

    int nState; // should be one of ACTIVE_GOLDMINENODE_XXXX
    std::string strNotCapableReason;


    CActiveGoldminenode()
        : eType(GOLDMINENODE_UNKNOWN),
          fPingerEnabled(false),
          pubKeyGoldminenode(),
          keyGoldminenode(),
          outpoint(),
          service(),
          nState(ACTIVE_GOLDMINENODE_INITIAL)
    {}

    /// Manage state of active Goldminenode
    void ManageState(CConnman& connman);

    std::string GetStateString() const;
    std::string GetStatus() const;
    std::string GetTypeString() const;

    bool UpdateSentinelPing(int version);

private:
    void ManageStateInitial(CConnman& connman);
    void ManageStateRemote();
};

#endif
