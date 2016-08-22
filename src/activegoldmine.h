// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef ACTIVEGOLDMINE_H
#define ACTIVEGOLDMINE_H

#include "sync.h"
#include "net.h"
#include "key.h"
#include "init.h"
#include "wallet.h"
#include "spysend.h"
#include "goldmine.h"

#define ACTIVE_GOLDMINE_INITIAL                     0 // initial state
#define ACTIVE_GOLDMINE_SYNC_IN_PROCESS             1
#define ACTIVE_GOLDMINE_INPUT_TOO_NEW               2
#define ACTIVE_GOLDMINE_NOT_CAPABLE                 3
#define ACTIVE_GOLDMINE_STARTED                     4

// Responsible for activating the Goldmine and pinging the network
class CActiveGoldmine
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    /// Ping Goldmine
    bool SendGoldminePing(std::string& errorMessage);

    /// Create Goldmine broadcast, needs to be relayed manually after that
    bool CreateBroadcast(CTxIn vin, CService service, CKey key, CPubKey pubKey, CKey keyGoldmine, CPubKey pubKeyGoldmine, std::string &errorMessage, CGoldmineBroadcast &mnb);

    /// Get 1000ARC input that can be used for the Goldmine
    bool GetGoldMineVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash, std::string strOutputIndex);
    bool GetVinFromOutput(COutput out, CTxIn& vin, CPubKey& pubkey, CKey& secretKey);

public:
	// Initialized by init.cpp
	// Keys for the main Goldmine
	CPubKey pubKeyGoldmine;

	// Initialized while registering Goldmine
	CTxIn vin;
    CService service;

    int status;
    std::string notCapableReason;

    CActiveGoldmine()
    {        
        status = ACTIVE_GOLDMINE_INITIAL;
    }

    /// Manage status of main Goldmine
    void ManageStatus(); 
    std::string GetStatus();

    /// Create Goldmine broadcast, needs to be relayed manually after that
    bool CreateBroadcast(std::string strService, std::string strKey, std::string strTxHash, std::string strOutputIndex, std::string& errorMessage, CGoldmineBroadcast &mnb, bool fOffline = false);

    /// Get 1000ARC input that can be used for the Goldmine
    bool GetGoldMineVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey);
    vector<COutput> SelectCoinsGoldmine();

    /// Enable cold wallet mode (run a Goldmine with no funds)
    bool EnableHotColdGoldMine(CTxIn& vin, CService& addr);
};

#endif
