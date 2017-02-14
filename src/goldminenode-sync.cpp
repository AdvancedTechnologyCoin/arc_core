// Copyright (c) 2015-2017 The Arctic Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activegoldminenode.h"
#include "checkpoints.h"
#include "governance.h"
#include "main.h"
#include "goldminenode.h"
#include "goldminenode-payments.h"
#include "goldminenode-sync.h"
#include "goldminenodeman.h"
#include "netfulfilledman.h"
#include "spork.h"
#include "util.h"

class CGoldminenodeSync;
CGoldminenodeSync goldminenodeSync;

void ReleaseNodes(const std::vector<CNode*> &vNodesCopy)
{
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodesCopy)
        pnode->Release();
}

bool CGoldminenodeSync::CheckNodeHeight(CNode* pnode, bool fDisconnectStuckNodes)
{
    CNodeStateStats stats;
    if(!GetNodeStateStats(pnode->id, stats) || stats.nCommonHeight == -1 || stats.nSyncHeight == -1) return false; // not enough info about this peer

    // Check blocks and headers, allow a small error margin of 1 block
    if(pCurrentBlockIndex->nHeight - 1 > stats.nCommonHeight) {
        // This peer probably stuck, don't sync any additional data from it
        if(fDisconnectStuckNodes) {
            // Disconnect to free this connection slot for another peer.
            pnode->fDisconnect = true;
            LogPrintf("CGoldminenodeSync::CheckNodeHeight -- disconnecting from stuck peer, nHeight=%d, nCommonHeight=%d, peer=%d\n",
                        pCurrentBlockIndex->nHeight, stats.nCommonHeight, pnode->id);
        } else {
            LogPrintf("CGoldminenodeSync::CheckNodeHeight -- skipping stuck peer, nHeight=%d, nCommonHeight=%d, peer=%d\n",
                        pCurrentBlockIndex->nHeight, stats.nCommonHeight, pnode->id);
        }
        return false;
    }
    else if(pCurrentBlockIndex->nHeight < stats.nSyncHeight - 1) {
        // This peer announced more headers than we have blocks currently
        LogPrintf("CGoldminenodeSync::CheckNodeHeight -- skipping peer, who announced more headers than we have blocks currently, nHeight=%d, nSyncHeight=%d, peer=%d\n",
                    pCurrentBlockIndex->nHeight, stats.nSyncHeight, pnode->id);
        return false;
    }

    return true;
}

bool CGoldminenodeSync::IsBlockchainSynced(bool fBlockAccepted)
{
    static bool fBlockchainSynced = false;
    static int64_t nTimeLastProcess = GetTime();
    static int nSkipped = 0;
    static bool fFirstBlockAccepted = false;

    // if the last call to this function was more than 60 minutes ago (client was in sleep mode) reset the sync process
    if(GetTime() - nTimeLastProcess > 60*60) {
        Reset();
        fBlockchainSynced = false;
    }

    if(!pCurrentBlockIndex || !pindexBestHeader || fImporting || fReindex) return false;

    if(fBlockAccepted) {
        // this should be only triggered while we are still syncing
        if(!IsSynced()) {
            // we are trying to download smth, reset blockchain sync status
            if(fDebug) LogPrintf("CGoldminenodeSync::IsBlockchainSynced -- reset\n");
            fFirstBlockAccepted = true;
            fBlockchainSynced = false;
            nTimeLastProcess = GetTime();
            return false;
        }
    } else {
        // skip if we already checked less than 1 tick ago
        if(GetTime() - nTimeLastProcess < GOLDMINENODE_SYNC_TICK_SECONDS) {
            nSkipped++;
            return fBlockchainSynced;
        }
    }

    if(fDebug) LogPrintf("CGoldminenodeSync::IsBlockchainSynced -- state before check: %ssynced, skipped %d times\n", fBlockchainSynced ? "" : "not ", nSkipped);

    nTimeLastProcess = GetTime();
    nSkipped = 0;

    if(fBlockchainSynced) return true;

    if(fCheckpointsEnabled && pCurrentBlockIndex->nHeight < Checkpoints::GetTotalBlocksEstimate(Params().Checkpoints()))
        return false;

    std::vector<CNode*> vNodesCopy;
    {
        LOCK(cs_vNodes);
        vNodesCopy = vNodes;
        BOOST_FOREACH(CNode* pnode, vNodesCopy)
            pnode->AddRef();
    }

    // We have enough peers and assume most of them are synced
    if(vNodes.size() >= GOLDMINENODE_SYNC_ENOUGH_PEERS) {
        // Check to see how many of our peers are (almost) at the same height as we are
        int nNodesAtSameHeight = 0;
        BOOST_FOREACH(CNode* pnode, vNodesCopy)
        {
            // Make sure this peer is presumably at the same height
            if(!CheckNodeHeight(pnode)) continue;
            nNodesAtSameHeight++;
            // if we have decent number of such peers, most likely we are synced now
            if(nNodesAtSameHeight >= GOLDMINENODE_SYNC_ENOUGH_PEERS) {
                LogPrintf("CGoldminenodeSync::IsBlockchainSynced -- found enough peers on the same height as we are, done\n");
                fBlockchainSynced = true;
                ReleaseNodes(vNodesCopy);
                return true;
            }
        }
    }
    ReleaseNodes(vNodesCopy);

    // wait for at least one new block to be accepted
    if(!fFirstBlockAccepted) return false;

    // same as !IsInitialBlockDownload() but no cs_main needed here
    int64_t nMaxBlockTime = std::max(pCurrentBlockIndex->GetBlockTime(), pindexBestHeader->GetBlockTime());
    fBlockchainSynced = pindexBestHeader->nHeight - pCurrentBlockIndex->nHeight < 24 * 6 &&
                        GetTime() - nMaxBlockTime < Params().MaxTipAge();

    return fBlockchainSynced;
}

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
    nTimeLastGoldminenodeList = GetTime();
    nTimeLastPaymentVote = GetTime();
    nTimeLastGovernanceItem = GetTime();
    nTimeLastFailure = 0;
    nCountFailures = 0;
}

std::string CGoldminenodeSync::GetAssetName()
{
    switch(nRequestedGoldminenodeAssets)
    {
        case(GOLDMINENODE_SYNC_INITIAL):      return "GOLDMINENODE_SYNC_INITIAL";
        case(GOLDMINENODE_SYNC_SPORKS):       return "GOLDMINENODE_SYNC_SPORKS";
        case(GOLDMINENODE_SYNC_LIST):         return "GOLDMINENODE_SYNC_LIST";
        case(GOLDMINENODE_SYNC_MNW):          return "GOLDMINENODE_SYNC_MNW";
        case(GOLDMINENODE_SYNC_GOVERNANCE):   return "GOLDMINENODE_SYNC_GOVERNANCE";
        case(GOLDMINENODE_SYNC_FAILED):       return "GOLDMINENODE_SYNC_FAILED";
        case GOLDMINENODE_SYNC_FINISHED:      return "GOLDMINENODE_SYNC_FINISHED";
        default:                            return "UNKNOWN";
    }
}

void CGoldminenodeSync::SwitchToNextAsset()
{
    switch(nRequestedGoldminenodeAssets)
    {
        case(GOLDMINENODE_SYNC_FAILED):
            throw std::runtime_error("Can't switch to next asset from failed, should use Reset() first!");
            break;
        case(GOLDMINENODE_SYNC_INITIAL):
            ClearFulfilledRequests();
            nRequestedGoldminenodeAssets = GOLDMINENODE_SYNC_SPORKS;
            LogPrintf("CGoldminenodeSync::SwitchToNextAsset -- Starting %s\n", GetAssetName());
            break;
        case(GOLDMINENODE_SYNC_SPORKS):
            nTimeLastGoldminenodeList = GetTime();
            nRequestedGoldminenodeAssets = GOLDMINENODE_SYNC_LIST;
            LogPrintf("CGoldminenodeSync::SwitchToNextAsset -- Starting %s\n", GetAssetName());
            break;
        case(GOLDMINENODE_SYNC_LIST):
            nTimeLastPaymentVote = GetTime();
            nRequestedGoldminenodeAssets = GOLDMINENODE_SYNC_MNW;
            LogPrintf("CGoldminenodeSync::SwitchToNextAsset -- Starting %s\n", GetAssetName());
            break;
        case(GOLDMINENODE_SYNC_MNW):
            nTimeLastGovernanceItem = GetTime();
            nRequestedGoldminenodeAssets = GOLDMINENODE_SYNC_GOVERNANCE;
            LogPrintf("CGoldminenodeSync::SwitchToNextAsset -- Starting %s\n", GetAssetName());
            break;
        case(GOLDMINENODE_SYNC_GOVERNANCE):
            LogPrintf("CGoldminenodeSync::SwitchToNextAsset -- Sync has finished\n");
            nRequestedGoldminenodeAssets = GOLDMINENODE_SYNC_FINISHED;
            uiInterface.NotifyAdditionalDataSyncProgressChanged(1);
            //try to activate our goldminenode if possible
            activeGoldminenode.ManageState();

            TRY_LOCK(cs_vNodes, lockRecv);
            if(!lockRecv) return;

            BOOST_FOREACH(CNode* pnode, vNodes) {
                netfulfilledman.AddFulfilledRequest(pnode->addr, "full-sync");
            }

            break;
    }
    nRequestedGoldminenodeAttempt = 0;
    nTimeAssetSyncStarted = GetTime();
}

std::string CGoldminenodeSync::GetSyncStatus()
{
    switch (goldminenodeSync.nRequestedGoldminenodeAssets) {
        case GOLDMINENODE_SYNC_INITIAL:       return _("Synchronization pending...");
        case GOLDMINENODE_SYNC_SPORKS:        return _("Synchronizing sporks...");
        case GOLDMINENODE_SYNC_LIST:          return _("Synchronizing goldminenodes...");
        case GOLDMINENODE_SYNC_MNW:           return _("Synchronizing goldminenode payments...");
        case GOLDMINENODE_SYNC_GOVERNANCE:    return _("Synchronizing governance objects...");
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

void CGoldminenodeSync::ClearFulfilledRequests()
{
    TRY_LOCK(cs_vNodes, lockRecv);
    if(!lockRecv) return;

    BOOST_FOREACH(CNode* pnode, vNodes)
    {
        netfulfilledman.RemoveFulfilledRequest(pnode->addr, "spork-sync");
        netfulfilledman.RemoveFulfilledRequest(pnode->addr, "goldminenode-list-sync");
        netfulfilledman.RemoveFulfilledRequest(pnode->addr, "goldminenode-payment-sync");
        netfulfilledman.RemoveFulfilledRequest(pnode->addr, "governance-sync");
        netfulfilledman.RemoveFulfilledRequest(pnode->addr, "full-sync");
    }
}

void CGoldminenodeSync::ProcessTick()
{
    static int nTick = 0;
    if(nTick++ % GOLDMINENODE_SYNC_TICK_SECONDS != 0) return;
    if(!pCurrentBlockIndex) return;

    //the actual count of goldminenodes we have currently
    int nMnCount = mnodeman.CountGoldminenodes();

    if(fDebug) LogPrintf("CGoldminenodeSync::ProcessTick -- nTick %d nMnCount %d\n", nTick, nMnCount);

    // RESET SYNCING INCASE OF FAILURE
    {
        if(IsSynced()) {
            /*
                Resync if we lose all goldminenodes from sleep/wake or failure to sync originally
            */
            if(nMnCount == 0) {
                LogPrintf("CGoldminenodeSync::ProcessTick -- WARNING: not enough data, restarting sync\n");
                Reset();
            } else {
                std::vector<CNode*> vNodesCopy;
                {
                    LOCK(cs_vNodes);
                    vNodesCopy = vNodes;
                    BOOST_FOREACH(CNode* pnode, vNodesCopy)
                        pnode->AddRef();
                }
                governance.RequestGovernanceObjectVotes(vNodesCopy);
                ReleaseNodes(vNodesCopy);
                return;
            }
        }

        //try syncing again
        if(IsFailed()) {
            if(nTimeLastFailure + (1*60) < GetTime()) { // 1 minute cooldown after failed sync
                Reset();
            }
            return;
        }
    }

    // INITIAL SYNC SETUP / LOG REPORTING
    double nSyncProgress = double(nRequestedGoldminenodeAttempt + (nRequestedGoldminenodeAssets - 1) * 8) / (8*4);
    LogPrintf("CGoldminenodeSync::ProcessTick -- nTick %d nRequestedGoldminenodeAssets %d nRequestedGoldminenodeAttempt %d nSyncProgress %f\n", nTick, nRequestedGoldminenodeAssets, nRequestedGoldminenodeAttempt, nSyncProgress);
    uiInterface.NotifyAdditionalDataSyncProgressChanged(nSyncProgress);

    // sporks synced but blockchain is not, wait until we're almost at a recent block to continue
    if(Params().NetworkIDString() != CBaseChainParams::REGTEST &&
            !IsBlockchainSynced() && nRequestedGoldminenodeAssets > GOLDMINENODE_SYNC_SPORKS)
    {
        LogPrintf("CGoldminenodeSync::ProcessTick -- nTick %d nRequestedGoldminenodeAssets %d nRequestedGoldminenodeAttempt %d -- blockchain is not synced yet\n", nTick, nRequestedGoldminenodeAssets, nRequestedGoldminenodeAttempt);
        nTimeLastGoldminenodeList = GetTime();
        nTimeLastPaymentVote = GetTime();
        nTimeLastGovernanceItem = GetTime();
        return;
    }

    if(nRequestedGoldminenodeAssets == GOLDMINENODE_SYNC_INITIAL ||
        (nRequestedGoldminenodeAssets == GOLDMINENODE_SYNC_SPORKS && IsBlockchainSynced()))
    {
        SwitchToNextAsset();
    }

    std::vector<CNode*> vNodesCopy;
    {
        LOCK(cs_vNodes);
        vNodesCopy = vNodes;
        BOOST_FOREACH(CNode* pnode, vNodesCopy)
            pnode->AddRef();
    }

    BOOST_FOREACH(CNode* pnode, vNodesCopy)
    {
        // QUICK MODE (REGTEST ONLY!)
        if(Params().NetworkIDString() == CBaseChainParams::REGTEST)
        {
            if(nRequestedGoldminenodeAttempt <= 2) {
                pnode->PushMessage(NetMsgType::GETSPORKS); //get current network sporks
            } else if(nRequestedGoldminenodeAttempt < 4) {
                mnodeman.DsegUpdate(pnode);
            } else if(nRequestedGoldminenodeAttempt < 6) {
                int nMnCount = mnodeman.CountGoldminenodes();
                pnode->PushMessage(NetMsgType::GOLDMINENODEPAYMENTSYNC, nMnCount); //sync payment votes
                SendGovernanceSyncRequest(pnode);
            } else {
                nRequestedGoldminenodeAssets = GOLDMINENODE_SYNC_FINISHED;
            }
            nRequestedGoldminenodeAttempt++;
            ReleaseNodes(vNodesCopy);
            return;
        }

        // NORMAL NETWORK MODE - TESTNET/MAINNET
        {
            if(netfulfilledman.HasFulfilledRequest(pnode->addr, "full-sync")) {
                // we already fully synced from this node recently,
                // disconnect to free this connection slot for a new node
                pnode->fDisconnect = true;
                LogPrintf("CGoldminenodeSync::ProcessTick -- disconnecting from recently synced peer %d\n", pnode->id);
                continue;
            }

            // Make sure this peer is presumably at the same height
            if(!CheckNodeHeight(pnode, true)) continue;

            // SPORK : ALWAYS ASK FOR SPORKS AS WE SYNC (we skip this mode now)

            if(!netfulfilledman.HasFulfilledRequest(pnode->addr, "spork-sync")) {
                // only request once from each peer
                netfulfilledman.AddFulfilledRequest(pnode->addr, "spork-sync");
                // get current network sporks
                pnode->PushMessage(NetMsgType::GETSPORKS);
                LogPrintf("CGoldminenodeSync::ProcessTick -- nTick %d nRequestedGoldminenodeAssets %d -- requesting sporks from peer %d\n", nTick, nRequestedGoldminenodeAssets, pnode->id);
                continue; // always get sporks first, switch to the next node without waiting for the next tick
            }

            // MNLIST : SYNC GOLDMINENODE LIST FROM OTHER CONNECTED CLIENTS

            if(nRequestedGoldminenodeAssets == GOLDMINENODE_SYNC_LIST) {
                LogPrint("goldminenode", "CGoldminenodeSync::ProcessTick -- nTick %d nRequestedGoldminenodeAssets %d nTimeLastGoldminenodeList %lld GetTime() %lld diff %lld\n", nTick, nRequestedGoldminenodeAssets, nTimeLastGoldminenodeList, GetTime(), GetTime() - nTimeLastGoldminenodeList);
                // check for timeout first
                if(nTimeLastGoldminenodeList < GetTime() - GOLDMINENODE_SYNC_TIMEOUT_SECONDS) {
                    LogPrintf("CGoldminenodeSync::ProcessTick -- nTick %d nRequestedGoldminenodeAssets %d -- timeout\n", nTick, nRequestedGoldminenodeAssets);
                    if (nRequestedGoldminenodeAttempt == 0) {
                        LogPrintf("CGoldminenodeSync::ProcessTick -- ERROR: failed to sync %s\n", GetAssetName());
                        // there is no way we can continue without goldminenode list, fail here and try later
                        Fail();
                        ReleaseNodes(vNodesCopy);
                        return;
                    }
                    SwitchToNextAsset();
                    ReleaseNodes(vNodesCopy);
                    return;
                }

                // only request once from each peer
                if(netfulfilledman.HasFulfilledRequest(pnode->addr, "goldminenode-list-sync")) continue;
                netfulfilledman.AddFulfilledRequest(pnode->addr, "goldminenode-list-sync");

                if (pnode->nVersion < mnpayments.GetMinGoldminenodePaymentsProto()) continue;
                nRequestedGoldminenodeAttempt++;

                mnodeman.DsegUpdate(pnode);

                ReleaseNodes(vNodesCopy);
                return; //this will cause each peer to get one request each six seconds for the various assets we need
            }

            // MNW : SYNC GOLDMINENODE PAYMENT VOTES FROM OTHER CONNECTED CLIENTS

            if(nRequestedGoldminenodeAssets == GOLDMINENODE_SYNC_MNW) {
                LogPrint("mnpayments", "CGoldminenodeSync::ProcessTick -- nTick %d nRequestedGoldminenodeAssets %d nTimeLastPaymentVote %lld GetTime() %lld diff %lld\n", nTick, nRequestedGoldminenodeAssets, nTimeLastPaymentVote, GetTime(), GetTime() - nTimeLastPaymentVote);
                // check for timeout first
                // This might take a lot longer than GOLDMINENODE_SYNC_TIMEOUT_SECONDS minutes due to new blocks,
                // but that should be OK and it should timeout eventually.
                if(nTimeLastPaymentVote < GetTime() - GOLDMINENODE_SYNC_TIMEOUT_SECONDS) {
                    LogPrintf("CGoldminenodeSync::ProcessTick -- nTick %d nRequestedGoldminenodeAssets %d -- timeout\n", nTick, nRequestedGoldminenodeAssets);
                    if (nRequestedGoldminenodeAttempt == 0) {
                        LogPrintf("CGoldminenodeSync::ProcessTick -- ERROR: failed to sync %s\n", GetAssetName());
                        // probably not a good idea to proceed without winner list
                        Fail();
                        ReleaseNodes(vNodesCopy);
                        return;
                    }
                    SwitchToNextAsset();
                    ReleaseNodes(vNodesCopy);
                    return;
                }

                // check for data
                // if mnpayments already has enough blocks and votes, switch to the next asset
                // try to fetch data from at least two peers though
                if(nRequestedGoldminenodeAttempt > 1 && mnpayments.IsEnoughData()) {
                    LogPrintf("CGoldminenodeSync::ProcessTick -- nTick %d nRequestedGoldminenodeAssets %d -- found enough data\n", nTick, nRequestedGoldminenodeAssets);
                    SwitchToNextAsset();
                    ReleaseNodes(vNodesCopy);
                    return;
                }

                // only request once from each peer
                if(netfulfilledman.HasFulfilledRequest(pnode->addr, "goldminenode-payment-sync")) continue;
                netfulfilledman.AddFulfilledRequest(pnode->addr, "goldminenode-payment-sync");

                if(pnode->nVersion < mnpayments.GetMinGoldminenodePaymentsProto()) continue;
                nRequestedGoldminenodeAttempt++;

                // ask node for all payment votes it has (new nodes will only return votes for future payments)
                pnode->PushMessage(NetMsgType::GOLDMINENODEPAYMENTSYNC, mnpayments.GetStorageLimit());
                // ask node for missing pieces only (old nodes will not be asked)
                mnpayments.RequestLowDataPaymentBlocks(pnode);

                ReleaseNodes(vNodesCopy);
                return; //this will cause each peer to get one request each six seconds for the various assets we need
            }

            // GOVOBJ : SYNC GOVERNANCE ITEMS FROM OUR PEERS

            if(nRequestedGoldminenodeAssets == GOLDMINENODE_SYNC_GOVERNANCE) {
                LogPrint("mnpayments", "CGoldminenodeSync::ProcessTick -- nTick %d nRequestedGoldminenodeAssets %d nTimeLastPaymentVote %lld GetTime() %lld diff %lld\n", nTick, nRequestedGoldminenodeAssets, nTimeLastPaymentVote, GetTime(), GetTime() - nTimeLastPaymentVote);

                // check for timeout first
                if(GetTime() - nTimeLastGovernanceItem > GOLDMINENODE_SYNC_TIMEOUT_SECONDS) {
                    LogPrintf("CGoldminenodeSync::ProcessTick -- nTick %d nRequestedGoldminenodeAssets %d -- timeout\n", nTick, nRequestedGoldminenodeAssets);
                    if(nRequestedGoldminenodeAttempt == 0) {
                        LogPrintf("CGoldminenodeSync::ProcessTick -- WARNING: failed to sync %s\n", GetAssetName());
                        // it's kind of ok to skip this for now, hopefully we'll catch up later?
                    }
                    SwitchToNextAsset();
                    ReleaseNodes(vNodesCopy);
                    return;
                }

                // check for data
                // if(nCountEvolutionItemProp > 0 && nCountEvolutionItemFin)
                // {
                //     if(governance.CountProposalInventoryItems() >= (nSumEvolutionItemProp / nCountEvolutionItemProp)*0.9)
                //     {
                //         if(governance.CountFinalizedInventoryItems() >= (nSumEvolutionItemFin / nCountEvolutionItemFin)*0.9)
                //         {
                //             SwitchToNextAsset();
                //             return;
                //         }
                //     }
                // }

                // only request obj sync once from each peer, then request votes on per-obj basis
                if(netfulfilledman.HasFulfilledRequest(pnode->addr, "governance-sync")) {
                    governance.RequestGovernanceObjectVotes(pnode);
                    continue;
                }
                netfulfilledman.AddFulfilledRequest(pnode->addr, "governance-sync");

                if (pnode->nVersion < MIN_GOVERNANCE_PEER_PROTO_VERSION) continue;
                nRequestedGoldminenodeAttempt++;

                SendGovernanceSyncRequest(pnode);

                ReleaseNodes(vNodesCopy);
                return; //this will cause each peer to get one request each six seconds for the various assets we need
            }
        }
    }
    // looped through all nodes, release them
    ReleaseNodes(vNodesCopy);
}

void CGoldminenodeSync::SendGovernanceSyncRequest(CNode* pnode)
{
    if(pnode->nVersion >= GOVERNANCE_FILTER_PROTO_VERSION) {
        CBloomFilter filter;
        filter.clear();

        pnode->PushMessage(NetMsgType::MNGOVERNANCESYNC, uint256(), filter);
    }
    else {
        pnode->PushMessage(NetMsgType::MNGOVERNANCESYNC, uint256());
    }
}

void CGoldminenodeSync::UpdatedBlockTip(const CBlockIndex *pindex)
{
    pCurrentBlockIndex = pindex;
}
