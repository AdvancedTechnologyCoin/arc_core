// Copyright (c) 2015-2017 The ARC developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activegoldminenode.h"
#include "checkpoints.h"
#include "validation.h"
#include "goldminenode.h"
#include "goldminenode-payments.h"
#include "goldminenode-sync.h"
#include "goldminenodeman.h"
#include "netfulfilledman.h"
#include "spork.h"
#include "ui_interface.h"
#include "util.h"

class CGoldminenodeSync;
CGoldminenodeSync goldminenodeSync;

void CGoldminenodeSync::Fail()
{
    nTimeLastFailure = GetTime();
    nRequestedGoldminenodeAssets = GOLDMINENODE_SYNC_FAILED;
}

void CGoldminenodeSync::Reset()
{
    nRequestedGoldminenodeAssets = GOLDMINENODE_SYNC_INITIAL;
    nRequestedGoldminenodeAttempt = 0;
    nTimeAssetSyncStarted = GetTime();
    nTimeLastBumped = GetTime();
    nTimeLastFailure = 0;
}

void CGoldminenodeSync::BumpAssetLastTime(std::string strFuncName)
{
    if(IsSynced() || IsFailed()) return;
    nTimeLastBumped = GetTime();
    LogPrint("mnsync", "CGoldminenodeSync::BumpAssetLastTime -- %s\n", strFuncName);
}

std::string CGoldminenodeSync::GetAssetName()
{
    switch(nRequestedGoldminenodeAssets)
    {
        case(GOLDMINENODE_SYNC_INITIAL):      return "GOLDMINENODE_SYNC_INITIAL";
        case(GOLDMINENODE_SYNC_WAITING):      return "GOLDMINENODE_SYNC_WAITING";
        case(GOLDMINENODE_SYNC_LIST):         return "GOLDMINENODE_SYNC_LIST";
        case(GOLDMINENODE_SYNC_MNW):          return "GOLDMINENODE_SYNC_MNW";
        case(GOLDMINENODE_SYNC_FAILED):       return "GOLDMINENODE_SYNC_FAILED";
        case GOLDMINENODE_SYNC_FINISHED:      return "GOLDMINENODE_SYNC_FINISHED";
        default:                            return "UNKNOWN";
    }
}

void CGoldminenodeSync::SwitchToNextAsset(CConnman& connman)
{
    switch(nRequestedGoldminenodeAssets)
    {
        case(GOLDMINENODE_SYNC_FAILED):
            throw std::runtime_error("Can't switch to next asset from failed, should use Reset() first!");
            break;
        case(GOLDMINENODE_SYNC_INITIAL):
            ClearFulfilledRequests(connman);
            nRequestedGoldminenodeAssets = GOLDMINENODE_SYNC_WAITING;
            LogPrintf("CGoldminenodeSync::SwitchToNextAsset -- Starting %s\n", GetAssetName());
            break;
        case(GOLDMINENODE_SYNC_WAITING):
            ClearFulfilledRequests(connman);
            LogPrintf("CGoldminenodeSync::SwitchToNextAsset -- Completed %s in %llds\n", GetAssetName(), GetTime() - nTimeAssetSyncStarted);
            nRequestedGoldminenodeAssets = GOLDMINENODE_SYNC_LIST;
            LogPrintf("CGoldminenodeSync::SwitchToNextAsset -- Starting %s\n", GetAssetName());
            break;
        case(GOLDMINENODE_SYNC_LIST):
            LogPrintf("CGoldminenodeSync::SwitchToNextAsset -- Completed %s in %llds\n", GetAssetName(), GetTime() - nTimeAssetSyncStarted);
            nRequestedGoldminenodeAssets = GOLDMINENODE_SYNC_MNW;
            LogPrintf("CGoldminenodeSync::SwitchToNextAsset -- Starting %s\n", GetAssetName());
            break;
        case(GOLDMINENODE_SYNC_MNW):
            LogPrintf("CGoldminenodeSync::SwitchToNextAsset -- Completed %s in %llds\n", GetAssetName(), GetTime() - nTimeAssetSyncStarted);
            nRequestedGoldminenodeAssets = GOLDMINENODE_SYNC_FINISHED;
            uiInterface.NotifyAdditionalDataSyncProgressChanged(1);
            //try to activate our goldminenode if possible
            activeGoldminenode.ManageState(connman);

            // TODO: Find out whether we can just use LOCK instead of:
            // TRY_LOCK(cs_vNodes, lockRecv);
            // if(lockRecv) { ... }

            connman.ForEachNode(CConnman::AllNodes, [](CNode* pnode) {
                netfulfilledman.AddFulfilledRequest(pnode->addr, "full-sync");
            });
            LogPrintf("CGoldminenodeSync::SwitchToNextAsset -- Sync has finished\n");

            break;
    }
    nRequestedGoldminenodeAttempt = 0;
    nTimeAssetSyncStarted = GetTime();
    BumpAssetLastTime("CGoldminenodeSync::SwitchToNextAsset");
}

std::string CGoldminenodeSync::GetSyncStatus()
{
    switch (goldminenodeSync.nRequestedGoldminenodeAssets) {
        case GOLDMINENODE_SYNC_INITIAL:       return _("Synchroning blockchain...");
        case GOLDMINENODE_SYNC_WAITING:       return _("Synchronization pending...");
        case GOLDMINENODE_SYNC_LIST:          return _("Synchronizing goldminenodes...");
        case GOLDMINENODE_SYNC_MNW:           return _("Synchronizing goldminenode payments...");
        case GOLDMINENODE_SYNC_FAILED:        return _("Synchronization failed");
        case GOLDMINENODE_SYNC_FINISHED:      return _("Synchronization finished");
        default:                            return "";
    }
}

void CGoldminenodeSync::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (strCommand == NetMsgType::SYNCSTATUSCOUNT) { //Sync status count

        //do not care about stats if sync process finished or failed
        if(IsSynced() || IsFailed()) return;

        int nItemID;
        int nCount;
        vRecv >> nItemID >> nCount;

        LogPrintf("SYNCSTATUSCOUNT -- got inventory count: nItemID=%d  nCount=%d  peer=%d\n", nItemID, nCount, pfrom->id);
    }
}

void CGoldminenodeSync::ClearFulfilledRequests(CConnman& connman)
{
    // TODO: Find out whether we can just use LOCK instead of:
    // TRY_LOCK(cs_vNodes, lockRecv);
    // if(!lockRecv) return;

    connman.ForEachNode(CConnman::AllNodes, [](CNode* pnode) {
        netfulfilledman.RemoveFulfilledRequest(pnode->addr, "spork-sync");
        netfulfilledman.RemoveFulfilledRequest(pnode->addr, "goldminenode-list-sync");
        netfulfilledman.RemoveFulfilledRequest(pnode->addr, "goldminenode-payment-sync");
        netfulfilledman.RemoveFulfilledRequest(pnode->addr, "full-sync");
    });
}

void CGoldminenodeSync::ProcessTick(CConnman& connman)
{
    static int nTick = 0;
    if(nTick++ % GOLDMINENODE_SYNC_TICK_SECONDS != 0) return;

    // reset the sync process if the last call to this function was more than 60 minutes ago (client was in sleep mode)
    static int64_t nTimeLastProcess = GetTime();
    if(GetTime() - nTimeLastProcess > 60*60) {
        LogPrintf("CGoldminenodeSync::HasSyncFailures -- WARNING: no actions for too long, restarting sync...\n");
        Reset();
        SwitchToNextAsset(connman);
        nTimeLastProcess = GetTime();
        return;
    }
    nTimeLastProcess = GetTime();

    // reset sync status in case of any other sync failure
    if(IsFailed()) {
        if(nTimeLastFailure + (1*60) < GetTime()) { // 1 minute cooldown after failed sync
            LogPrintf("CGoldminenodeSync::HasSyncFailures -- WARNING: failed to sync, trying again...\n");
            Reset();
            SwitchToNextAsset(connman);
        }
        return;
    }

    // gradually request the rest of the votes after sync finished
    if(IsSynced()) {
        std::vector<CNode*> vNodesCopy = connman.CopyNodeVector();
        connman.ReleaseNodeVector(vNodesCopy);
        return;
    }

    // Calculate "progress" for LOG reporting / GUI notification
    double nSyncProgress = double(nRequestedGoldminenodeAttempt + (nRequestedGoldminenodeAssets - 1) * 8) / (8*4);
    LogPrintf("CGoldminenodeSync::ProcessTick -- nTick %d nRequestedGoldminenodeAssets %d nRequestedGoldminenodeAttempt %d nSyncProgress %f\n", nTick, nRequestedGoldminenodeAssets, nRequestedGoldminenodeAttempt, nSyncProgress);
    uiInterface.NotifyAdditionalDataSyncProgressChanged(nSyncProgress);

    std::vector<CNode*> vNodesCopy = connman.CopyNodeVector();

    BOOST_FOREACH(CNode* pnode, vNodesCopy)
    {
        // Don't try to sync any data from outbound "goldminenode" connections -
        // they are temporary and should be considered unreliable for a sync process.
        // Inbound connection this early is most likely a "goldminenode" connection
        // initiated from another node, so skip it too.
        if(pnode->fGoldminenode || (fGoldmineNode && pnode->fInbound)) continue;

        // QUICK MODE (REGTEST ONLY!)
        if(Params().NetworkIDString() == CBaseChainParams::REGTEST)
        {
            if(nRequestedGoldminenodeAttempt <= 2) {
                connman.PushMessageWithVersion(pnode, INIT_PROTO_VERSION, NetMsgType::GETSPORKS); //get current network sporks
            } else if(nRequestedGoldminenodeAttempt < 4) {
                mnodeman.DsegUpdate(pnode, connman);
            } else if(nRequestedGoldminenodeAttempt < 6) {
                int nMnCount = mnodeman.CountGoldminenodes();
                connman.PushMessage(pnode, NetMsgType::GOLDMINENODEPAYMENTSYNC, nMnCount); //sync payment votes
            } else {
                nRequestedGoldminenodeAssets = GOLDMINENODE_SYNC_FINISHED;
            }
            nRequestedGoldminenodeAttempt++;
            connman.ReleaseNodeVector(vNodesCopy);
            return;
        }

        // NORMAL NETWORK MODE - TESTNET/MAINNET
        {
            if(netfulfilledman.HasFulfilledRequest(pnode->addr, "full-sync")) {
                // We already fully synced from this node recently,
                // disconnect to free this connection slot for another peer.
                pnode->fDisconnect = true;
                LogPrintf("CGoldminenodeSync::ProcessTick -- disconnecting from recently synced peer %d\n", pnode->id);
                continue;
            }

            // SPORK : ALWAYS ASK FOR SPORKS AS WE SYNC

            if(!netfulfilledman.HasFulfilledRequest(pnode->addr, "spork-sync")) {
                // always get sporks first, only request once from each peer
                netfulfilledman.AddFulfilledRequest(pnode->addr, "spork-sync");
                // get current network sporks
                connman.PushMessageWithVersion(pnode, INIT_PROTO_VERSION, NetMsgType::GETSPORKS);
                LogPrintf("CGoldminenodeSync::ProcessTick -- nTick %d nRequestedGoldminenodeAssets %d -- requesting sporks from peer %d\n", nTick, nRequestedGoldminenodeAssets, pnode->id);
            }

            // INITIAL TIMEOUT

            if(nRequestedGoldminenodeAssets == GOLDMINENODE_SYNC_WAITING) {
                if(GetTime() - nTimeLastBumped > GOLDMINENODE_SYNC_TIMEOUT_SECONDS) {
                    // At this point we know that:
                    // a) there are peers (because we are looping on at least one of them);
                    // b) we waited for at least GOLDMINENODE_SYNC_TIMEOUT_SECONDS since we reached
                    //    the headers tip the last time (i.e. since we switched from
                    //     GOLDMINENODE_SYNC_INITIAL to GOLDMINENODE_SYNC_WAITING and bumped time);
                    // c) there were no blocks (UpdatedBlockTip, NotifyHeaderTip) or headers (AcceptedBlockHeader)
                    //    for at least GOLDMINENODE_SYNC_TIMEOUT_SECONDS.
                    // We must be at the tip already, let's move to the next asset.
                    SwitchToNextAsset(connman);
                }
            }

            // MNLIST : SYNC GOLDMINENODE LIST FROM OTHER CONNECTED CLIENTS

            if(nRequestedGoldminenodeAssets == GOLDMINENODE_SYNC_LIST) {
                LogPrint("goldminenode", "CGoldminenodeSync::ProcessTick -- nTick %d nRequestedGoldminenodeAssets %d nTimeLastGoldminenodeList %lld GetTime() %lld diff %lld\n", nTick, nRequestedGoldminenodeAssets, nTimeLastBumped, GetTime(), GetTime() - nTimeLastBumped);
                // check for timeout first
                if(GetTime() - nTimeLastBumped > GOLDMINENODE_SYNC_TIMEOUT_SECONDS) {
                    LogPrintf("CGoldminenodeSync::ProcessTick -- nTick %d nRequestedGoldminenodeAssets %d -- timeout\n", nTick, nRequestedGoldminenodeAssets);
                    if (nRequestedGoldminenodeAttempt == 0) {
                        LogPrintf("CGoldminenodeSync::ProcessTick -- ERROR: failed to sync %s\n", GetAssetName());
                        // there is no way we can continue without goldminenode list, fail here and try later
                        Fail();
                        connman.ReleaseNodeVector(vNodesCopy);
                        return;
                    }
                    SwitchToNextAsset(connman);
                    connman.ReleaseNodeVector(vNodesCopy);
                    return;
                }

                // only request once from each peer
                if(netfulfilledman.HasFulfilledRequest(pnode->addr, "goldminenode-list-sync")) continue;
                netfulfilledman.AddFulfilledRequest(pnode->addr, "goldminenode-list-sync");

                if (pnode->nVersion < mnpayments.GetMinGoldminenodePaymentsProto()) continue;
                nRequestedGoldminenodeAttempt++;

                mnodeman.DsegUpdate(pnode, connman);

                connman.ReleaseNodeVector(vNodesCopy);
                return; //this will cause each peer to get one request each six seconds for the various assets we need
            }

            // MNW : SYNC GOLDMINENODE PAYMENT VOTES FROM OTHER CONNECTED CLIENTS

            if(nRequestedGoldminenodeAssets == GOLDMINENODE_SYNC_MNW) {
                LogPrint("mnpayments", "CGoldminenodeSync::ProcessTick -- nTick %d nRequestedGoldminenodeAssets %d nTimeLastBumped %lld GetTime() %lld diff %lld\n", nTick, nRequestedGoldminenodeAssets, nTimeLastBumped, GetTime(), GetTime() - nTimeLastBumped);
                // check for timeout first
                // This might take a lot longer than GOLDMINENODE_SYNC_TIMEOUT_SECONDS due to new blocks,
                // but that should be OK and it should timeout eventually.
                if(GetTime() - nTimeLastBumped > GOLDMINENODE_SYNC_TIMEOUT_SECONDS) {
                    LogPrintf("CGoldminenodeSync::ProcessTick -- nTick %d nRequestedGoldminenodeAssets %d -- timeout\n", nTick, nRequestedGoldminenodeAssets);
                    if (nRequestedGoldminenodeAttempt == 0) {
                        LogPrintf("CGoldminenodeSync::ProcessTick -- ERROR: failed to sync %s\n", GetAssetName());
                        // probably not a good idea to proceed without winner list
                        Fail();
                        connman.ReleaseNodeVector(vNodesCopy);
                        return;
                    }
                    SwitchToNextAsset(connman);
                    connman.ReleaseNodeVector(vNodesCopy);
                    return;
                }

                // check for data
                // if mnpayments already has enough blocks and votes, switch to the next asset
                // try to fetch data from at least two peers though
                if(nRequestedGoldminenodeAttempt > 1 && mnpayments.IsEnoughData()) {
                    LogPrintf("CGoldminenodeSync::ProcessTick -- nTick %d nRequestedGoldminenodeAssets %d -- found enough data\n", nTick, nRequestedGoldminenodeAssets);
                    SwitchToNextAsset(connman);
                    connman.ReleaseNodeVector(vNodesCopy);
                    return;
                }

                // only request once from each peer
                if(netfulfilledman.HasFulfilledRequest(pnode->addr, "goldminenode-payment-sync")) continue;
                netfulfilledman.AddFulfilledRequest(pnode->addr, "goldminenode-payment-sync");

                if(pnode->nVersion < mnpayments.GetMinGoldminenodePaymentsProto()) continue;
                nRequestedGoldminenodeAttempt++;

                // ask node for all payment votes it has (new nodes will only return votes for future payments)
                connman.PushMessage(pnode, NetMsgType::GOLDMINENODEPAYMENTSYNC, mnpayments.GetStorageLimit());
                // ask node for missing pieces only (old nodes will not be asked)
                mnpayments.RequestLowDataPaymentBlocks(pnode, connman);

                connman.ReleaseNodeVector(vNodesCopy);
                return; //this will cause each peer to get one request each six seconds for the various assets we need
            }
        }
    }
    // looped through all nodes, release them
    connman.ReleaseNodeVector(vNodesCopy);
}

void CGoldminenodeSync::AcceptedBlockHeader(const CBlockIndex *pindexNew)
{
    LogPrint("mnsync", "CGoldminenodeSync::AcceptedBlockHeader -- pindexNew->nHeight: %d\n", pindexNew->nHeight);

    if (!IsBlockchainSynced()) {
        // Postpone timeout each time new block header arrives while we are still syncing blockchain
        BumpAssetLastTime("CGoldminenodeSync::AcceptedBlockHeader");
    }
}

void CGoldminenodeSync::NotifyHeaderTip(const CBlockIndex *pindexNew, bool fInitialDownload, CConnman& connman)
{
    LogPrint("mnsync", "CGoldminenodeSync::NotifyHeaderTip -- pindexNew->nHeight: %d fInitialDownload=%d\n", pindexNew->nHeight, fInitialDownload);

    if (IsFailed() || IsSynced() || !pindexBestHeader)
        return;

    if (!IsBlockchainSynced()) {
        // Postpone timeout each time new block arrives while we are still syncing blockchain
        BumpAssetLastTime("CGoldminenodeSync::NotifyHeaderTip");
    }
}

void CGoldminenodeSync::UpdatedBlockTip(const CBlockIndex *pindexNew, bool fInitialDownload, CConnman& connman)
{
    LogPrint("mnsync", "CGoldminenodeSync::UpdatedBlockTip -- pindexNew->nHeight: %d fInitialDownload=%d\n", pindexNew->nHeight, fInitialDownload);

    if (IsFailed() || IsSynced() || !pindexBestHeader)
        return;

    if (!IsBlockchainSynced()) {
        // Postpone timeout each time new block arrives while we are still syncing blockchain
        BumpAssetLastTime("CGoldminenodeSync::UpdatedBlockTip");
    }

    if (fInitialDownload) {
        // switched too early
        if (IsBlockchainSynced()) {
            Reset();
        }

        // no need to check any further while still in IBD mode
        return;
    }

    // Note: since we sync headers first, it should be ok to use this
    static bool fReachedBestHeader = false;
    bool fReachedBestHeaderNew = pindexNew->GetBlockHash() == pindexBestHeader->GetBlockHash();

    if (fReachedBestHeader && !fReachedBestHeaderNew) {
        // Switching from true to false means that we previousely stuck syncing headers for some reason,
        // probably initial timeout was not enough,
        // because there is no way we can update tip not having best header
        Reset();
        fReachedBestHeader = false;
        return;
    }

    fReachedBestHeader = fReachedBestHeaderNew;

    LogPrint("mnsync", "CGoldminenodeSync::UpdatedBlockTip -- pindexNew->nHeight: %d pindexBestHeader->nHeight: %d fInitialDownload=%d fReachedBestHeader=%d\n",
                pindexNew->nHeight, pindexBestHeader->nHeight, fInitialDownload, fReachedBestHeader);

    if (!IsBlockchainSynced() && fReachedBestHeader) {
        // Reached best header while being in initial mode.
        // We must be at the tip already, let's move to the next asset.
        SwitchToNextAsset(connman);
    }
}
