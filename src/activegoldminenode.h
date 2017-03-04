// Copyright (c) 2015-2017 The Arctic Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ACTIVEGOLDMINENODE_H
#define ACTIVEGOLDMINENODE_H

#include "net.h"
#include "key.h"
#include "wallet/wallet.h"

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
        GOLDMINENODE_REMOTE  = 1,
        GOLDMINENODE_LOCAL   = 2
    };

private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    goldminenode_type_enum_t eType;

    bool fPingerEnabled;

    /// Ping Goldminenode
    bool SendGoldminenodePing();

public:
    // Keys for the active Goldminenode
    CPubKey pubKeyGoldminenode;
    CKey keyGoldminenode;

    // Initialized while registering Goldminenode
    CTxIn vin;
    CService service;

    int nState; // should be one of ACTIVE_GOLDMINENODE_XXXX
    std::string strNotCapableReason;

    CActiveGoldminenode()
        : eType(GOLDMINENODE_UNKNOWN),
          fPingerEnabled(false),
          pubKeyGoldminenode(),
          keyGoldminenode(),
          vin(),
          service(),
          nState(ACTIVE_GOLDMINENODE_INITIAL)
    {}

    /// Manage state of active Goldminenode
    void ManageState();

    std::string GetStateString() const;
    std::string GetStatus() const;
    std::string GetTypeString() const;

private:
    void ManageStateInitial();
    void ManageStateRemote();
    void ManageStateLocal();
};

#endif
