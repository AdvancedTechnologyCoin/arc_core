// Copyright (c) 2015-2016 The Arctic developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "activegoldmine.h"
#include "goldmine-sync.h"
#include "goldmine-payments.h"
#include "goldmine-evolution.h"
#include "goldmine.h"
#include "goldmineman.h"
#include "spork.h"
#include "util.h"
#include "addrman.h"

class CGoldmineSync;
CGoldmineSync goldmineSync;

CGoldmineSync::CGoldmineSync()
{
    Reset();
}

bool CGoldmineSync::IsSynced()
{
    return RequestedGoldmineAssets == GOLDMINE_SYNC_FINISHED;
}

bool CGoldmineSync::IsBlockchainSynced()
{
    static bool fBlockchainSynced = false;
    static int64_t lastProcess = GetTime();

    // if the last call to this function was more than 60 minutes ago (client was in sleep mode) reset the sync process
    if(GetTime() - lastProcess > 60*60) {
        Reset();
        fBlockchainSynced = false;
    }
    lastProcess = GetTime();

    if(fBlockchainSynced) return true;

    if (fImporting || fReindex) return false;

    TRY_LOCK(cs_main, lockMain);
    if(!lockMain) return false;

    CBlockIndex* pindex = chainActive.Tip();
    if(pindex == NULL) return false;


    if(pindex->nTime + 60*60 < GetTime())
        return false;

    fBlockchainSynced = true;

    return true;
}

void CGoldmineSync::Reset()
{   
    lastGoldmineList = 0;
    lastGoldmineWinner = 0;
    lastEvolutionItem = 0;
    mapSeenSyncMNB.clear();
    mapSeenSyncMNW.clear();
    mapSeenSyncEvolution.clear();
    lastFailure = 0;
    nCountFailures = 0;
    sumGoldmineList = 0;
    sumGoldmineWinner = 0;
    sumEvolutionItemProp = 0;
    sumEvolutionItemFin = 0;
    countGoldmineList = 0;
    countGoldmineWinner = 0;
    countEvolutionItemProp = 0;
    countEvolutionItemFin = 0;
    RequestedGoldmineAssets = GOLDMINE_SYNC_INITIAL;
    RequestedGoldmineAttempt = 0;
    nAssetSyncStarted = GetTime();
}

void CGoldmineSync::AddedGoldmineList(uint256 hash)
{
    if(gmineman.mapSeenGoldmineBroadcast.count(hash)) {
        if(mapSeenSyncMNB[hash] < GOLDMINE_SYNC_THRESHOLD) {
            lastGoldmineList = GetTime();
            mapSeenSyncMNB[hash]++;
        }
    } else {
        lastGoldmineList = GetTime();
        mapSeenSyncMNB.insert(make_pair(hash, 1));
    }
}

void CGoldmineSync::AddedGoldmineWinner(uint256 hash)
{
    if(goldminePayments.mapGoldminePayeeVotes.count(hash)) {
        if(mapSeenSyncMNW[hash] < GOLDMINE_SYNC_THRESHOLD) {
            lastGoldmineWinner = GetTime();
            mapSeenSyncMNW[hash]++;
        }
    } else {
        lastGoldmineWinner = GetTime();
        mapSeenSyncMNW.insert(make_pair(hash, 1));
    }
}

void CGoldmineSync::AddedEvolutionItem(uint256 hash)
{
    if(evolution.mapSeenGoldmineEvolutionProposals.count(hash) || evolution.mapSeenGoldmineEvolutionVotes.count(hash) ||
            evolution.mapSeenFinalizedEvolutions.count(hash) || evolution.mapSeenFinalizedEvolutionVotes.count(hash)) {
        if(mapSeenSyncEvolution[hash] < GOLDMINE_SYNC_THRESHOLD) {
            lastEvolutionItem = GetTime();
            mapSeenSyncEvolution[hash]++;
        }
    } else {
        lastEvolutionItem = GetTime();
        mapSeenSyncEvolution.insert(make_pair(hash, 1));
    }
}

bool CGoldmineSync::IsEvolutionPropEmpty()
{
    return sumEvolutionItemProp==0 && countEvolutionItemProp>0;
}

bool CGoldmineSync::IsEvolutionFinEmpty()
{
    return sumEvolutionItemFin==0 && countEvolutionItemFin>0;
}

void CGoldmineSync::GetNextAsset()
{
    switch(RequestedGoldmineAssets)
    {
        case(GOLDMINE_SYNC_INITIAL):
        case(GOLDMINE_SYNC_FAILED): // should never be used here actually, use Reset() instead
            ClearFulfilledRequest();
            RequestedGoldmineAssets = GOLDMINE_SYNC_SPORKS;
            break;
        case(GOLDMINE_SYNC_SPORKS):
            RequestedGoldmineAssets = GOLDMINE_SYNC_LIST;
            break;
        case(GOLDMINE_SYNC_LIST):
            RequestedGoldmineAssets = GOLDMINE_SYNC_MNW;
            break;
        case(GOLDMINE_SYNC_MNW):
            RequestedGoldmineAssets = GOLDMINE_SYNC_EVOLUTION;
            break;
        case(GOLDMINE_SYNC_EVOLUTION):
            LogPrintf("CGoldmineSync::GetNextAsset - Sync has finished\n");
            RequestedGoldmineAssets = GOLDMINE_SYNC_FINISHED;
            break;
    }
    RequestedGoldmineAttempt = 0;
    nAssetSyncStarted = GetTime();
}

std::string CGoldmineSync::GetSyncStatus()
{
    switch (goldmineSync.RequestedGoldmineAssets) {
        case GOLDMINE_SYNC_INITIAL: return _("Synchronization pending...");
        case GOLDMINE_SYNC_SPORKS: return _("Synchronizing sporks...");
        case GOLDMINE_SYNC_LIST: return _("Synchronizing goldmines...");
        case GOLDMINE_SYNC_MNW: return _("Synchronizing goldmine winners...");
        case GOLDMINE_SYNC_EVOLUTION: return _("Synchronizing evolutions...");
        case GOLDMINE_SYNC_FAILED: return _("Synchronization failed");
        case GOLDMINE_SYNC_FINISHED: return _("Synchronization finished");
    }
    return "";
}

void CGoldmineSync::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (strCommand == "ssc") { //Sync status count
        int nItemID;
        int nCount;
        vRecv >> nItemID >> nCount;

        if(RequestedGoldmineAssets >= GOLDMINE_SYNC_FINISHED) return;

        //this means we will receive no further communication
        switch(nItemID)
        {
            case(GOLDMINE_SYNC_LIST):
                if(nItemID != RequestedGoldmineAssets) return;
                sumGoldmineList += nCount;
                countGoldmineList++;
                break;
            case(GOLDMINE_SYNC_MNW):
                if(nItemID != RequestedGoldmineAssets) return;
                sumGoldmineWinner += nCount;
                countGoldmineWinner++;
                break;
            case(GOLDMINE_SYNC_EVOLUTION_PROP):
                if(RequestedGoldmineAssets != GOLDMINE_SYNC_EVOLUTION) return;
                sumEvolutionItemProp += nCount;
                countEvolutionItemProp++;
                break;
            case(GOLDMINE_SYNC_EVOLUTION_FIN):
                if(RequestedGoldmineAssets != GOLDMINE_SYNC_EVOLUTION) return;
                sumEvolutionItemFin += nCount;
                countEvolutionItemFin++;
                break;
        }
        
        LogPrintf("CGoldmineSync:ProcessMessage - ssc - got inventory count %d %d\n", nItemID, nCount);
    }
}

void CGoldmineSync::ClearFulfilledRequest()
{
    TRY_LOCK(cs_vNodes, lockRecv);
    if(!lockRecv) return;

    BOOST_FOREACH(CNode* pnode, vNodes)
    {
        pnode->ClearFulfilledRequest("getspork");
        pnode->ClearFulfilledRequest("gmsync");
        pnode->ClearFulfilledRequest("mnwsync");
        pnode->ClearFulfilledRequest("busync");
    }
}

void CGoldmineSync::Process()
{
    static int tick = 0;

    if(tick++ % GOLDMINE_SYNC_TIMEOUT != 0) return;

    if(IsSynced()) {
        /* 
            Resync if we lose all goldmines from sleep/wake or failure to sync originally
        */
        if(gmineman.CountEnabled() == 0) {
            Reset();
        } else
            return;
    }

    //try syncing again
    if(RequestedGoldmineAssets == GOLDMINE_SYNC_FAILED && lastFailure + (1*60) < GetTime()) {
        Reset();
    } else if (RequestedGoldmineAssets == GOLDMINE_SYNC_FAILED) {
        return;
    }

    if(fDebug) LogPrintf("CGoldmineSync::Process() - tick %d RequestedGoldmineAssets %d\n", tick, RequestedGoldmineAssets);

    if(RequestedGoldmineAssets == GOLDMINE_SYNC_INITIAL) GetNextAsset();

    // sporks synced but blockchain is not, wait until we're almost at a recent block to continue
    if(Params().NetworkID() != CBaseChainParams::REGTEST &&
            !IsBlockchainSynced() && RequestedGoldmineAssets > GOLDMINE_SYNC_SPORKS) return;

    TRY_LOCK(cs_vNodes, lockRecv);
    if(!lockRecv) return;

    BOOST_FOREACH(CNode* pnode, vNodes)
    {
        if(Params().NetworkID() == CBaseChainParams::REGTEST){
            if(RequestedGoldmineAttempt <= 2) {
                pnode->PushMessage("getsporks"); //get current network sporks
            } else if(RequestedGoldmineAttempt < 4) {
                gmineman.DsegUpdate(pnode); 
            } else if(RequestedGoldmineAttempt < 6) {
                int nMnCount = gmineman.CountEnabled();
                pnode->PushMessage("mnget", nMnCount); //sync payees
                uint256 n = 0;
                pnode->PushMessage("mnvs", n); //sync goldmine votes
            } else {
                RequestedGoldmineAssets = GOLDMINE_SYNC_FINISHED;
            }
            RequestedGoldmineAttempt++;
            return;
        }

        //set to synced
        if(RequestedGoldmineAssets == GOLDMINE_SYNC_SPORKS){
            if(pnode->HasFulfilledRequest("getspork")) continue;
            pnode->FulfilledRequest("getspork");

            pnode->PushMessage("getsporks"); //get current network sporks
            if(RequestedGoldmineAttempt >= 2) GetNextAsset();
            RequestedGoldmineAttempt++;
            
            return;
        }

        if (pnode->nVersion >= goldminePayments.GetMinGoldminePaymentsProto()) {

            if(RequestedGoldmineAssets == GOLDMINE_SYNC_LIST) {
                if(fDebug) LogPrintf("CGoldmineSync::Process() - lastGoldmineList %lld (GetTime() - GOLDMINE_SYNC_TIMEOUT) %lld\n", lastGoldmineList, GetTime() - GOLDMINE_SYNC_TIMEOUT);
                if(lastGoldmineList > 0 && lastGoldmineList < GetTime() - GOLDMINE_SYNC_TIMEOUT*2 && RequestedGoldmineAttempt >= GOLDMINE_SYNC_THRESHOLD){ //hasn't received a new item in the last five seconds, so we'll move to the
                    GetNextAsset();
                    return;
                }

                if(pnode->HasFulfilledRequest("gmsync")) continue;
                pnode->FulfilledRequest("gmsync");

                // timeout
                if(lastGoldmineList == 0 &&
                (RequestedGoldmineAttempt >= GOLDMINE_SYNC_THRESHOLD*3 || GetTime() - nAssetSyncStarted > GOLDMINE_SYNC_TIMEOUT*5)) {
                    if(IsSporkActive(SPORK_8_GOLDMINE_PAYMENT_ENFORCEMENT)) {
                        LogPrintf("CGoldmineSync::Process - ERROR - Sync has failed, will retry later\n");
                        RequestedGoldmineAssets = GOLDMINE_SYNC_FAILED;
                        RequestedGoldmineAttempt = 0;
                        lastFailure = GetTime();
                        nCountFailures++;
                    } else {
                        GetNextAsset();
                    }
                    return;
                }

                if(RequestedGoldmineAttempt >= GOLDMINE_SYNC_THRESHOLD*3) return;

                gmineman.DsegUpdate(pnode);
                RequestedGoldmineAttempt++;
                return;
            }

            if(RequestedGoldmineAssets == GOLDMINE_SYNC_MNW) {
                if(lastGoldmineWinner > 0 && lastGoldmineWinner < GetTime() - GOLDMINE_SYNC_TIMEOUT*2 && RequestedGoldmineAttempt >= GOLDMINE_SYNC_THRESHOLD){ //hasn't received a new item in the last five seconds, so we'll move to the
                    GetNextAsset();
                    return;
                }

                if(pnode->HasFulfilledRequest("mnwsync")) continue;
                pnode->FulfilledRequest("mnwsync");

                // timeout
                if(lastGoldmineWinner == 0 &&
                (RequestedGoldmineAttempt >= GOLDMINE_SYNC_THRESHOLD*3 || GetTime() - nAssetSyncStarted > GOLDMINE_SYNC_TIMEOUT*5)) {
                    if(IsSporkActive(SPORK_8_GOLDMINE_PAYMENT_ENFORCEMENT)) {
                        LogPrintf("CGoldmineSync::Process - ERROR - Sync has failed, will retry later\n");
                        RequestedGoldmineAssets = GOLDMINE_SYNC_FAILED;
                        RequestedGoldmineAttempt = 0;
                        lastFailure = GetTime();
                        nCountFailures++;
                    } else {
                        GetNextAsset();
                    }
                    return;
                }

                if(RequestedGoldmineAttempt >= GOLDMINE_SYNC_THRESHOLD*3) return;

                CBlockIndex* pindexPrev = chainActive.Tip();
                if(pindexPrev == NULL) return;

                int nMnCount = gmineman.CountEnabled();
                pnode->PushMessage("mnget", nMnCount); //sync payees
                RequestedGoldmineAttempt++;

                return;
            }
        }

        if (pnode->nVersion >= MIN_EVOLUTION_PEER_PROTO_VERSION) {

            if(RequestedGoldmineAssets == GOLDMINE_SYNC_EVOLUTION){
                //we'll start rejecting votes if we accidentally get set as synced too soon
                if(lastEvolutionItem > 0 && lastEvolutionItem < GetTime() - GOLDMINE_SYNC_TIMEOUT*2 && RequestedGoldmineAttempt >= GOLDMINE_SYNC_THRESHOLD){ //hasn't received a new item in the last five seconds, so we'll move to the
                    //LogPrintf("CGoldmineSync::Process - HasNextFinalizedEvolution %d nCountFailures %d IsEvolutionPropEmpty %d\n", evolution.HasNextFinalizedEvolution(), nCountFailures, IsEvolutionPropEmpty());
                    //if(evolution.HasNextFinalizedEvolution() || nCountFailures >= 2 || IsEvolutionPropEmpty()) {
                        GetNextAsset();

                        //try to activate our goldmine if possible
                        activeGoldmine.ManageStatus();
                    // } else { //we've failed to sync, this state will reject the next evolution block
                    //     LogPrintf("CGoldmineSync::Process - ERROR - Sync has failed, will retry later\n");
                    //     RequestedGoldmineAssets = GOLDMINE_SYNC_FAILED;
                    //     RequestedGoldmineAttempt = 0;
                    //     lastFailure = GetTime();
                    //     nCountFailures++;
                    // }
                    return;
                }

                // timeout
                if(lastEvolutionItem == 0 &&
                (RequestedGoldmineAttempt >= GOLDMINE_SYNC_THRESHOLD*3 || GetTime() - nAssetSyncStarted > GOLDMINE_SYNC_TIMEOUT*5)) {
                    // maybe there is no evolutions at all, so just finish syncing
                    GetNextAsset();
                    activeGoldmine.ManageStatus();
                    return;
                }

                if(pnode->HasFulfilledRequest("busync")) continue;
                pnode->FulfilledRequest("busync");

                if(RequestedGoldmineAttempt >= GOLDMINE_SYNC_THRESHOLD*3) return;

                uint256 n = 0;
                pnode->PushMessage("mnvs", n); //sync goldmine votes
                RequestedGoldmineAttempt++;
                
                return;
            }

        }
    }
}
