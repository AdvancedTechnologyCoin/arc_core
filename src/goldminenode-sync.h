// Copyright (c) 2015-2017 The Arctic Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef GOLDMINENODE_SYNC_H
#define GOLDMINENODE_SYNC_H

#include "chain.h"
#include "net.h"

#include <univalue.h>

class CGoldminenodeSync;

static const int GOLDMINENODE_SYNC_FAILED          = -1;
static const int GOLDMINENODE_SYNC_INITIAL         = 0;
static const int GOLDMINENODE_SYNC_SPORKS          = 1;
static const int GOLDMINENODE_SYNC_LIST            = 2;
static const int GOLDMINENODE_SYNC_MNW             = 3;
static const int GOLDMINENODE_SYNC_GOVERNANCE      = 4;
static const int GOLDMINENODE_SYNC_GOVOBJ          = 10;
static const int GOLDMINENODE_SYNC_GOVOBJ_VOTE     = 11;
static const int GOLDMINENODE_SYNC_FINISHED        = 999;

static const int GOLDMINENODE_SYNC_TICK_SECONDS    = 6;
static const int GOLDMINENODE_SYNC_TIMEOUT_SECONDS = 30; // our blocks are 2.5 minutes so 30 seconds should be fine

static const int GOLDMINENODE_SYNC_ENOUGH_PEERS    = 6;

extern CGoldminenodeSync goldminenodeSync;

//
// CGoldminenodeSync : Sync goldminenode assets in stages
//

class CGoldminenodeSync
{
private:
    // Keep track of current asset
    int nRequestedGoldminenodeAssets;
    // Count peers we've requested the asset from
    int nRequestedGoldminenodeAttempt;

    // Time when current goldminenode asset sync started
    int64_t nTimeAssetSyncStarted;

    // Last time when we received some goldminenode asset ...
    int64_t nTimeLastGoldminenodeList;
    int64_t nTimeLastPaymentVote;
    int64_t nTimeLastGovernanceItem;
    // ... or failed
    int64_t nTimeLastFailure;

    // How many times we failed
    int nCountFailures;

    // Keep track of current block index
    const CBlockIndex *pCurrentBlockIndex;

    bool CheckNodeHeight(CNode* pnode, bool fDisconnectStuckNodes = false);
    void Fail();
    void ClearFulfilledRequests();

public:
    CGoldminenodeSync() { Reset(); }

    void AddedGoldminenodeList() { nTimeLastGoldminenodeList = GetTime(); }
    void AddedPaymentVote() { nTimeLastPaymentVote = GetTime(); }
    void AddedGovernanceItem() { nTimeLastGovernanceItem = GetTime(); };

    void SendGovernanceSyncRequest(CNode* pnode);

    bool IsFailed() { return nRequestedGoldminenodeAssets == GOLDMINENODE_SYNC_FAILED; }
    bool IsBlockchainSynced(bool fBlockAccepted = false);
    bool IsGoldminenodeListSynced() { return nRequestedGoldminenodeAssets > GOLDMINENODE_SYNC_LIST; }
    bool IsWinnersListSynced() { return nRequestedGoldminenodeAssets > GOLDMINENODE_SYNC_MNW; }
    bool IsSynced() { return nRequestedGoldminenodeAssets == GOLDMINENODE_SYNC_FINISHED; }

    int GetAssetID() { return nRequestedGoldminenodeAssets; }
    int GetAttempt() { return nRequestedGoldminenodeAttempt; }
    std::string GetAssetName();
    std::string GetSyncStatus();

    void Reset();
    void SwitchToNextAsset();

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    void ProcessTick();

    void UpdatedBlockTip(const CBlockIndex *pindex);
};

#endif
