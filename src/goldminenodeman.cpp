// Copyright (c) 2017-2022 The Advanced Technology Coin
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activegoldminenode.h"
#include "addrman.h"
#include "alert.h"
#include "clientversion.h"
#include "goldminenode-payments.h"
#include "goldminenode-sync.h"
#include "goldminenodeman.h"
#include "messagesigner.h"
#include "netfulfilledman.h"
#include "netmessagemaker.h"
#ifdef ENABLE_WALLET
#include "privatesend-client.h"
#endif // ENABLE_WALLET
#include "script/standard.h"
#include "ui_interface.h"
#include "util.h"
#include "warnings.h"

/** Goldminenode manager */
CGoldminenodeMan mnodeman;

const std::string CGoldminenodeMan::SERIALIZATION_VERSION_STRING = "CGoldminenodeMan-Version-8";
const int CGoldminenodeMan::LAST_PAID_SCAN_BLOCKS = 100;

struct CompareLastPaidBlock
{
    bool operator()(const std::pair<int, const CGoldminenode*>& t1,
                    const std::pair<int, const CGoldminenode*>& t2) const
    {
        return (t1.first != t2.first) ? (t1.first < t2.first) : (t1.second->outpoint < t2.second->outpoint);
    }
};

struct CompareScoreMN
{
    bool operator()(const std::pair<arith_uint256, const CGoldminenode*>& t1,
                    const std::pair<arith_uint256, const CGoldminenode*>& t2) const
    {
        return (t1.first != t2.first) ? (t1.first < t2.first) : (t1.second->outpoint < t2.second->outpoint);
    }
};

struct CompareByAddr

{
    bool operator()(const CGoldminenode* t1,
                    const CGoldminenode* t2) const
    {
        return t1->addr < t2->addr;
    }
};

CGoldminenodeMan::CGoldminenodeMan():
    cs(),
    mapGoldminenodes(),
    mAskedUsForGoldminenodeList(),
    mWeAskedForGoldminenodeList(),
    mWeAskedForGoldminenodeListEntry(),
    mWeAskedForVerification(),
    mMnbRecoveryRequests(),
    mMnbRecoveryGoodReplies(),
    listScheduledMnbRequestConnections(),
    fGoldminenodesAdded(false),
    fGoldminenodesRemoved(false),
    nLastSentinelPingTime(0),
    mapSeenGoldminenodeBroadcast(),
    mapSeenGoldminenodePing(),
    nDsqCount(0)
{}

bool CGoldminenodeMan::Add(CGoldminenode &mn)
{
    LOCK(cs);

    if (Has(mn.outpoint)) return false;

    LogPrint("goldminenode", "CGoldminenodeMan::Add -- Adding new Goldminenode: addr=%s, %i now\n", mn.addr.ToString(), size() + 1);
    mapGoldminenodes[mn.outpoint] = mn;
    fGoldminenodesAdded = true;
    return true;
}

void CGoldminenodeMan::AskForMN(CNode* pnode, const COutPoint& outpoint, CConnman& connman)
{
    if(!pnode) return;

    CNetMsgMaker msgMaker(pnode->GetSendVersion());
    LOCK(cs);

    CService addrSquashed = Params().AllowMultiplePorts() ? (CService)pnode->addr : CService(pnode->addr, 0);
    auto it1 = mWeAskedForGoldminenodeListEntry.find(outpoint);
    if (it1 != mWeAskedForGoldminenodeListEntry.end()) {
        auto it2 = it1->second.find(addrSquashed);
        if (it2 != it1->second.end()) {
            if (GetTime() < it2->second) {
                // we've asked recently, should not repeat too often or we could get banned
                return;
            }
            // we asked this node for this outpoint but it's ok to ask again already
            LogPrintf("CGoldminenodeMan::AskForMN -- Asking same peer %s for missing goldminenode entry again: %s\n", addrSquashed.ToString(), outpoint.ToStringShort());
        } else {
            // we already asked for this outpoint but not this node
            LogPrintf("CGoldminenodeMan::AskForMN -- Asking new peer %s for missing goldminenode entry: %s\n", addrSquashed.ToString(), outpoint.ToStringShort());
        }
    } else {
        // we never asked any node for this outpoint
        LogPrintf("CGoldminenodeMan::AskForMN -- Asking peer %s for missing goldminenode entry for the first time: %s\n", addrSquashed.ToString(), outpoint.ToStringShort());
    }
    mWeAskedForGoldminenodeListEntry[outpoint][addrSquashed] = GetTime() + DSEG_UPDATE_SECONDS;

   // if (pnode->GetSendVersion() == 70209) {
        connman.PushMessage(pnode, msgMaker.Make(NetMsgType::DSEG, CTxIn(outpoint)));
   // } else {
  //      connman.PushMessage(pnode, msgMaker.Make(NetMsgType::DSEG, outpoint));
   // }
}

bool CGoldminenodeMan::AllowMixing(const COutPoint &outpoint)
{
    LOCK(cs);
    CGoldminenode* pmn = Find(outpoint);
    if (!pmn) {
        return false;
    }
    nDsqCount++;
    pmn->nLastDsq = nDsqCount;
    pmn->fAllowMixingTx = true;

    return true;
}

bool CGoldminenodeMan::DisallowMixing(const COutPoint &outpoint)
{
    LOCK(cs);
    CGoldminenode* pmn = Find(outpoint);
    if (!pmn) {
        return false;
    }
    pmn->fAllowMixingTx = false;

    return true;
}

bool CGoldminenodeMan::PoSeBan(const COutPoint &outpoint)
{
    LOCK(cs);
    CGoldminenode* pmn = Find(outpoint);
    if (!pmn) {
        return false;
    }
    pmn->PoSeBan();

    return true;
}

void CGoldminenodeMan::Check()
{
    LOCK2(cs_main, cs);

    LogPrint("goldminenode", "CGoldminenodeMan::Check -- nLastSentinelPingTime=%d, IsSentinelPingActive()=%d\n", nLastSentinelPingTime, IsSentinelPingActive());

    for (auto& mnpair : mapGoldminenodes) {
        // NOTE: internally it checks only every GOLDMINENODE_CHECK_SECONDS seconds
        // since the last time, so expect some MNs to skip this
        mnpair.second.Check();
    }
}

void CGoldminenodeMan::CheckAndRemove(CConnman& connman)
{
    if(!goldminenodeSync.IsGoldminenodeListSynced()) return;

    LogPrintf("CGoldminenodeMan::CheckAndRemove\n");

    {
        // Need LOCK2 here to ensure consistent locking order because code below locks cs_main
        // in CheckMnbAndUpdateGoldminenodeList()
        LOCK2(cs_main, cs);

        Check();

        // Remove spent goldminenodes, prepare structures and make requests to reasure the state of inactive ones
        rank_pair_vec_t vecGoldminenodeRanks;
        // ask for up to MNB_RECOVERY_MAX_ASK_ENTRIES goldminenode entries at a time
        int nAskForMnbRecovery = MNB_RECOVERY_MAX_ASK_ENTRIES;
        std::map<COutPoint, CGoldminenode>::iterator it = mapGoldminenodes.begin();
        while (it != mapGoldminenodes.end()) {
            CGoldminenodeBroadcast mnb = CGoldminenodeBroadcast(it->second);
            uint256 hash = mnb.GetHash();
            // If collateral was spent ...
            if (it->second.IsOutpointSpent()) {
                LogPrint("goldminenode", "CGoldminenodeMan::CheckAndRemove -- Removing Goldminenode: %s  addr=%s  %i now\n", it->second.GetStateString(), it->second.addr.ToString(), size() - 1);

                // erase all of the broadcasts we've seen from this txin, ...
                mapSeenGoldminenodeBroadcast.erase(hash);
                mWeAskedForGoldminenodeListEntry.erase(it->first);

                // and finally remove it from the list
                mapGoldminenodes.erase(it++);
                fGoldminenodesRemoved = true;
            } else {
                bool fAsk = (nAskForMnbRecovery > 0) &&
                            goldminenodeSync.IsSynced() &&
                            it->second.IsNewStartRequired() &&
                            !IsMnbRecoveryRequested(hash) &&
                            !IsArgSet("-connect");
                if(fAsk) {
                    // this mn is in a non-recoverable state and we haven't asked other nodes yet
                    std::set<CService> setRequested;
                    // calulate only once and only when it's needed
                    if(vecGoldminenodeRanks.empty()) {
                        int nRandomBlockHeight = GetRandInt(nCachedBlockHeight);
                        GetGoldminenodeRanks(vecGoldminenodeRanks, nRandomBlockHeight);
                    }
                    bool fAskedForMnbRecovery = false;
                    // ask first MNB_RECOVERY_QUORUM_TOTAL goldminenodes we can connect to and we haven't asked recently
                    for(int i = 0; setRequested.size() < MNB_RECOVERY_QUORUM_TOTAL && i < (int)vecGoldminenodeRanks.size(); i++) {
                        // avoid banning
                        if(mWeAskedForGoldminenodeListEntry.count(it->first) && mWeAskedForGoldminenodeListEntry[it->first].count(vecGoldminenodeRanks[i].second.addr)) continue;
                        // didn't ask recently, ok to ask now
                        CService addr = vecGoldminenodeRanks[i].second.addr;
                        setRequested.insert(addr);
                        listScheduledMnbRequestConnections.push_back(std::make_pair(addr, hash));
                        fAskedForMnbRecovery = true;
                    }
                    if(fAskedForMnbRecovery) {
                        LogPrint("goldminenode", "CGoldminenodeMan::CheckAndRemove -- Recovery initiated, goldminenode=%s\n", it->first.ToStringShort());
                        nAskForMnbRecovery--;
                    }
                    // wait for mnb recovery replies for MNB_RECOVERY_WAIT_SECONDS seconds
                    mMnbRecoveryRequests[hash] = std::make_pair(GetTime() + MNB_RECOVERY_WAIT_SECONDS, setRequested);
                }
                ++it;
            }
        }

        // proces replies for GOLDMINENODE_NEW_START_REQUIRED goldminenodes
        LogPrint("goldminenode", "CGoldminenodeMan::CheckAndRemove -- mMnbRecoveryGoodReplies size=%d\n", (int)mMnbRecoveryGoodReplies.size());
        std::map<uint256, std::vector<CGoldminenodeBroadcast> >::iterator itMnbReplies = mMnbRecoveryGoodReplies.begin();
        while(itMnbReplies != mMnbRecoveryGoodReplies.end()){
            if(mMnbRecoveryRequests[itMnbReplies->first].first < GetTime()) {
                // all nodes we asked should have replied now
                if(itMnbReplies->second.size() >= MNB_RECOVERY_QUORUM_REQUIRED) {
                    // majority of nodes we asked agrees that this mn doesn't require new mnb, reprocess one of new mnbs
                    LogPrint("goldminenode", "CGoldminenodeMan::CheckAndRemove -- reprocessing mnb, goldminenode=%s\n", itMnbReplies->second[0].outpoint.ToStringShort());
                    // mapSeenGoldminenodeBroadcast.erase(itMnbReplies->first);
                    int nDos;
                    itMnbReplies->second[0].fRecovery = true;
                    CheckMnbAndUpdateGoldminenodeList(NULL, itMnbReplies->second[0], nDos, connman);
                }
                LogPrint("goldminenode", "CGoldminenodeMan::CheckAndRemove -- removing mnb recovery reply, goldminenode=%s, size=%d\n", itMnbReplies->second[0].outpoint.ToStringShort(), (int)itMnbReplies->second.size());
                mMnbRecoveryGoodReplies.erase(itMnbReplies++);
            } else {
                ++itMnbReplies;
            }
        }
    }
    {
        // no need for cm_main below
        LOCK(cs);

        auto itMnbRequest = mMnbRecoveryRequests.begin();
        while(itMnbRequest != mMnbRecoveryRequests.end()){
            // Allow this mnb to be re-verified again after MNB_RECOVERY_RETRY_SECONDS seconds
            // if mn is still in GOLDMINENODE_NEW_START_REQUIRED state.
            if(GetTime() - itMnbRequest->second.first > MNB_RECOVERY_RETRY_SECONDS) {
                mMnbRecoveryRequests.erase(itMnbRequest++);
            } else {
                ++itMnbRequest;
            }
        }

        // check who's asked for the Goldminenode list
        auto it1 = mAskedUsForGoldminenodeList.begin();
        while(it1 != mAskedUsForGoldminenodeList.end()){
            if((*it1).second < GetTime()) {
                mAskedUsForGoldminenodeList.erase(it1++);
            } else {
                ++it1;
            }
        }

        // check who we asked for the Goldminenode list
        it1 = mWeAskedForGoldminenodeList.begin();
        while(it1 != mWeAskedForGoldminenodeList.end()){
            if((*it1).second < GetTime()){
                mWeAskedForGoldminenodeList.erase(it1++);
            } else {
                ++it1;
            }
        }

        // check which Goldminenodes we've asked for
        auto it2 = mWeAskedForGoldminenodeListEntry.begin();
        while(it2 != mWeAskedForGoldminenodeListEntry.end()){
            auto it3 = it2->second.begin();
            while(it3 != it2->second.end()){
                if(it3->second < GetTime()){
                    it2->second.erase(it3++);
                } else {
                    ++it3;
                }
            }
            if(it2->second.empty()) {
                mWeAskedForGoldminenodeListEntry.erase(it2++);
            } else {
                ++it2;
            }
        }

        auto it3 = mWeAskedForVerification.begin();
        while(it3 != mWeAskedForVerification.end()){
            if(it3->second.nBlockHeight < nCachedBlockHeight - MAX_POSE_BLOCKS) {
                mWeAskedForVerification.erase(it3++);
            } else {
                ++it3;
            }
        }

        // NOTE: do not expire mapSeenGoldminenodeBroadcast entries here, clean them on mnb updates!

        // remove expired mapSeenGoldminenodePing
        std::map<uint256, CGoldminenodePing>::iterator it4 = mapSeenGoldminenodePing.begin();
        while(it4 != mapSeenGoldminenodePing.end()){
            if((*it4).second.IsExpired()) {
                LogPrint("goldminenode", "CGoldminenodeMan::CheckAndRemove -- Removing expired Goldminenode ping: hash=%s\n", (*it4).second.GetHash().ToString());
                mapSeenGoldminenodePing.erase(it4++);
            } else {
                ++it4;
            }
        }

        // remove expired mapSeenGoldminenodeVerification
        std::map<uint256, CGoldminenodeVerification>::iterator itv2 = mapSeenGoldminenodeVerification.begin();
        while(itv2 != mapSeenGoldminenodeVerification.end()){
            if((*itv2).second.nBlockHeight < nCachedBlockHeight - MAX_POSE_BLOCKS){
                LogPrint("goldminenode", "CGoldminenodeMan::CheckAndRemove -- Removing expired Goldminenode verification: hash=%s\n", (*itv2).first.ToString());
                mapSeenGoldminenodeVerification.erase(itv2++);
            } else {
                ++itv2;
            }
        }

        LogPrintf("CGoldminenodeMan::CheckAndRemove -- %s\n", ToString());
    }

    if(fGoldminenodesRemoved) {
        NotifyGoldminenodeUpdates(connman);
    }
}

void CGoldminenodeMan::Clear()
{
    LOCK(cs);
    mapGoldminenodes.clear();
    mAskedUsForGoldminenodeList.clear();
    mWeAskedForGoldminenodeList.clear();
    mWeAskedForGoldminenodeListEntry.clear();
    mapSeenGoldminenodeBroadcast.clear();
    mapSeenGoldminenodePing.clear();
    nDsqCount = 0;
    nLastSentinelPingTime = 0;
}

int CGoldminenodeMan::CountGoldminenodes(int nProtocolVersion)
{
    LOCK(cs);
    int nCount = 0;
    nProtocolVersion = nProtocolVersion == -1 ? mnpayments.GetMinGoldminenodePaymentsProto() : nProtocolVersion;

    for (const auto& mnpair : mapGoldminenodes) {
        if(mnpair.second.nProtocolVersion < nProtocolVersion) continue;
        nCount++;
    }

    return nCount;
}

int CGoldminenodeMan::CountEnabled(int nProtocolVersion)
{
    LOCK(cs);
    int nCount = 0;
    nProtocolVersion = nProtocolVersion == -1 ? mnpayments.GetMinGoldminenodePaymentsProto() : nProtocolVersion;

    for (const auto& mnpair : mapGoldminenodes) {
        if(mnpair.second.nProtocolVersion < nProtocolVersion || !mnpair.second.IsEnabled()) continue;
        nCount++;
    }

    return nCount;
}

/* Only IPv4 goldminenodes are allowed in 12.1, saving this for later
int CGoldminenodeMan::CountByIP(int nNetworkType)
{
    LOCK(cs);
    int nNodeCount = 0;

    for (const auto& mnpair : mapGoldminenodes)
        if ((nNetworkType == NET_IPV4 && mnpair.second.addr.IsIPv4()) ||
            (nNetworkType == NET_TOR  && mnpair.second.addr.IsTor())  ||
            (nNetworkType == NET_IPV6 && mnpair.second.addr.IsIPv6())) {
                nNodeCount++;
        }

    return nNodeCount;
}
*/

void CGoldminenodeMan::DsegUpdate(CNode* pnode, CConnman& connman)
{
    CNetMsgMaker msgMaker(pnode->GetSendVersion());
    LOCK(cs);

    CService addrSquashed = Params().AllowMultiplePorts() ? (CService)pnode->addr : CService(pnode->addr, 0);
    if(Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if(!(pnode->addr.IsRFC1918() || pnode->addr.IsLocal())) {
            auto it = mWeAskedForGoldminenodeList.find(addrSquashed);
            if(it != mWeAskedForGoldminenodeList.end() && GetTime() < (*it).second) {
                LogPrintf("CGoldminenodeMan::DsegUpdate -- we already asked %s for the list; skipping...\n", addrSquashed.ToString());
                return;
            }
        }
    }

//    if (pnode->GetSendVersion() == 70209) {
        connman.PushMessage(pnode, msgMaker.Make(NetMsgType::DSEG, CTxIn()));
 //   } else {
 //       connman.PushMessage(pnode, msgMaker.Make(NetMsgType::DSEG, COutPoint()));
 //   }
    int64_t askAgain = GetTime() + DSEG_UPDATE_SECONDS;
    mWeAskedForGoldminenodeList[addrSquashed] = askAgain;

    LogPrint("goldminenode", "CGoldminenodeMan::DsegUpdate -- asked %s for the list\n", pnode->addr.ToString());
}

CGoldminenode* CGoldminenodeMan::Find(const COutPoint &outpoint)
{
    LOCK(cs);
    auto it = mapGoldminenodes.find(outpoint);
    return it == mapGoldminenodes.end() ? NULL : &(it->second);
}

bool CGoldminenodeMan::Get(const COutPoint& outpoint, CGoldminenode& goldminenodeRet)
{
    // Theses mutexes are recursive so double locking by the same thread is safe.
    LOCK(cs);
    auto it = mapGoldminenodes.find(outpoint);
    if (it == mapGoldminenodes.end()) {
        return false;
    }

    goldminenodeRet = it->second;
    return true;
}

bool CGoldminenodeMan::GetGoldminenodeInfo(const COutPoint& outpoint, goldminenode_info_t& mnInfoRet)
{
    LOCK(cs);
    auto it = mapGoldminenodes.find(outpoint);
    if (it == mapGoldminenodes.end()) {
        return false;
    }
    mnInfoRet = it->second.GetInfo();
    return true;
}

bool CGoldminenodeMan::GetGoldminenodeInfo(const CPubKey& pubKeyGoldminenode, goldminenode_info_t& mnInfoRet)
{
    LOCK(cs);
    for (const auto& mnpair : mapGoldminenodes) {
        if (mnpair.second.pubKeyGoldminenode == pubKeyGoldminenode) {
            mnInfoRet = mnpair.second.GetInfo();
            return true;
        }
    }
    return false;
}

bool CGoldminenodeMan::GetGoldminenodeInfo(const CScript& payee, goldminenode_info_t& mnInfoRet)
{
    LOCK(cs);
    for (const auto& mnpair : mapGoldminenodes) {
        CScript scriptCollateralAddress = GetScriptForDestination(mnpair.second.pubKeyCollateralAddress.GetID());
        if (scriptCollateralAddress == payee) {
            mnInfoRet = mnpair.second.GetInfo();
            return true;
        }
    }
    return false;
}

bool CGoldminenodeMan::Has(const COutPoint& outpoint)
{
    LOCK(cs);
    return mapGoldminenodes.find(outpoint) != mapGoldminenodes.end();
}

//
// Deterministically select the oldest/best goldminenode to pay on the network
//
bool CGoldminenodeMan::GetNextGoldminenodeInQueueForPayment(bool fFilterSigTime, int& nCountRet, goldminenode_info_t& mnInfoRet)
{
    return GetNextGoldminenodeInQueueForPayment(nCachedBlockHeight, fFilterSigTime, nCountRet, mnInfoRet);
}

bool CGoldminenodeMan::GetNextGoldminenodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCountRet, goldminenode_info_t& mnInfoRet)
{
    mnInfoRet = goldminenode_info_t();
    nCountRet = 0;

    if (!goldminenodeSync.IsWinnersListSynced()) {
        // without winner list we can't reliably find the next winner anyway
        return false;
    }

    // Need LOCK2 here to ensure consistent locking order because the GetBlockHash call below locks cs_main
    LOCK2(cs_main,cs);

    std::vector<std::pair<int, const CGoldminenode*> > vecGoldminenodeLastPaid;

    /*
        Make a vector with all of the last paid times
    */

    int nMnCount = CountGoldminenodes();

    for (const auto& mnpair : mapGoldminenodes) {
        if(!mnpair.second.IsValidForPayment()) continue;

        //check protocol version
        if(mnpair.second.nProtocolVersion < mnpayments.GetMinGoldminenodePaymentsProto()) continue;

        //it's in the list (up to 8 entries ahead of current block to allow propagation) -- so let's skip it
        if(mnpayments.IsScheduled(mnpair.second, nBlockHeight)) continue;

        //it's too new, wait for a cycle
        if(fFilterSigTime && mnpair.second.sigTime + (nMnCount*2.6*60) > GetAdjustedTime()) continue;

        //make sure it has at least as many confirmations as there are goldminenodes
        if(GetUTXOConfirmations(mnpair.first) < nMnCount) continue;

        vecGoldminenodeLastPaid.push_back(std::make_pair(mnpair.second.GetLastPaidBlock(), &mnpair.second));
    }

    nCountRet = (int)vecGoldminenodeLastPaid.size();

    //when the network is in the process of upgrading, don't penalize nodes that recently restarted
    if(fFilterSigTime && nCountRet < nMnCount/3)
        return GetNextGoldminenodeInQueueForPayment(nBlockHeight, false, nCountRet, mnInfoRet);

    // Sort them low to high
    sort(vecGoldminenodeLastPaid.begin(), vecGoldminenodeLastPaid.end(), CompareLastPaidBlock());

    uint256 blockHash;
    if(!GetBlockHash(blockHash, nBlockHeight - 101)) {
        LogPrintf("CGoldminenode::GetNextGoldminenodeInQueueForPayment -- ERROR: GetBlockHash() failed at nBlockHeight %d\n", nBlockHeight - 101);
        return false;
    }
    // Look at 1/10 of the oldest nodes (by last payment), calculate their scores and pay the best one
    //  -- This doesn't look at who is being paid in the +8-10 blocks, allowing for double payments very rarely
    //  -- 1/100 payments should be a double payment on mainnet - (1/(3000/10))*2
    //  -- (chance per block * chances before IsScheduled will fire)
    int nTenthNetwork = nMnCount/10;
    int nCountTenth = 0;
    arith_uint256 nHighest = 0;
    const CGoldminenode *pBestGoldminenode = NULL;
    for (const auto& s : vecGoldminenodeLastPaid) {
        arith_uint256 nScore = s.second->CalculateScore(blockHash);
        if(nScore > nHighest){
            nHighest = nScore;
            pBestGoldminenode = s.second;
        }
        nCountTenth++;
        if(nCountTenth >= nTenthNetwork) break;
    }
    if (pBestGoldminenode) {
        mnInfoRet = pBestGoldminenode->GetInfo();
    }
    return mnInfoRet.fInfoValid;
}

goldminenode_info_t CGoldminenodeMan::FindRandomNotInVec(const std::vector<COutPoint> &vecToExclude, int nProtocolVersion)
{
    LOCK(cs);

    nProtocolVersion = nProtocolVersion == -1 ? mnpayments.GetMinGoldminenodePaymentsProto() : nProtocolVersion;

    int nCountEnabled = CountEnabled(nProtocolVersion);
    int nCountNotExcluded = nCountEnabled - vecToExclude.size();

    LogPrintf("CGoldminenodeMan::FindRandomNotInVec -- %d enabled goldminenodes, %d goldminenodes to choose from\n", nCountEnabled, nCountNotExcluded);
    if(nCountNotExcluded < 1) return goldminenode_info_t();

    // fill a vector of pointers
    std::vector<const CGoldminenode*> vpGoldminenodesShuffled;
    for (const auto& mnpair : mapGoldminenodes) {
        vpGoldminenodesShuffled.push_back(&mnpair.second);
    }

    FastRandomContext insecure_rand;
    // shuffle pointers
    std::random_shuffle(vpGoldminenodesShuffled.begin(), vpGoldminenodesShuffled.end(), insecure_rand);
    bool fExclude;

    // loop through
    for (const auto& pmn : vpGoldminenodesShuffled) {
        if(pmn->nProtocolVersion < nProtocolVersion || !pmn->IsEnabled()) continue;
        fExclude = false;
        for (const auto& outpointToExclude : vecToExclude) {
            if(pmn->outpoint == outpointToExclude) {
                fExclude = true;
                break;
            }
        }
        if(fExclude) continue;
        // found the one not in vecToExclude
        LogPrint("goldminenode", "CGoldminenodeMan::FindRandomNotInVec -- found, goldminenode=%s\n", pmn->outpoint.ToStringShort());
        return pmn->GetInfo();
    }

    LogPrint("goldminenode", "CGoldminenodeMan::FindRandomNotInVec -- failed\n");
    return goldminenode_info_t();
}

bool CGoldminenodeMan::GetGoldminenodeScores(const uint256& nBlockHash, CGoldminenodeMan::score_pair_vec_t& vecGoldminenodeScoresRet, int nMinProtocol)
{
    vecGoldminenodeScoresRet.clear();

    if (!goldminenodeSync.IsGoldminenodeListSynced())
        return false;

    AssertLockHeld(cs);

    if (mapGoldminenodes.empty())
        return false;

    // calculate scores
    for (const auto& mnpair : mapGoldminenodes) {
        if (mnpair.second.nProtocolVersion >= nMinProtocol) {
            vecGoldminenodeScoresRet.push_back(std::make_pair(mnpair.second.CalculateScore(nBlockHash), &mnpair.second));
        }
    }

    sort(vecGoldminenodeScoresRet.rbegin(), vecGoldminenodeScoresRet.rend(), CompareScoreMN());
    return !vecGoldminenodeScoresRet.empty();
}

bool CGoldminenodeMan::GetGoldminenodeRank(const COutPoint& outpoint, int& nRankRet, int nBlockHeight, int nMinProtocol)
{
    nRankRet = -1;

    if (!goldminenodeSync.IsGoldminenodeListSynced())
        return false;

    // make sure we know about this block
    uint256 nBlockHash = uint256();
    if (!GetBlockHash(nBlockHash, nBlockHeight)) {
        LogPrintf("CGoldminenodeMan::%s -- ERROR: GetBlockHash() failed at nBlockHeight %d\n", __func__, nBlockHeight);
        return false;
    }

    LOCK(cs);

    score_pair_vec_t vecGoldminenodeScores;
    if (!GetGoldminenodeScores(nBlockHash, vecGoldminenodeScores, nMinProtocol))
        return false;

    int nRank = 0;
    for (const auto& scorePair : vecGoldminenodeScores) {
        nRank++;
        if(scorePair.second->outpoint == outpoint) {
            nRankRet = nRank;
            return true;
        }
    }

    return false;
}

bool CGoldminenodeMan::GetGoldminenodeRanks(CGoldminenodeMan::rank_pair_vec_t& vecGoldminenodeRanksRet, int nBlockHeight, int nMinProtocol)
{
    vecGoldminenodeRanksRet.clear();

    if (!goldminenodeSync.IsGoldminenodeListSynced())
        return false;

    // make sure we know about this block
    uint256 nBlockHash = uint256();
    if (!GetBlockHash(nBlockHash, nBlockHeight)) {
        LogPrintf("CGoldminenodeMan::%s -- ERROR: GetBlockHash() failed at nBlockHeight %d\n", __func__, nBlockHeight);
        return false;
    }

    LOCK(cs);

    score_pair_vec_t vecGoldminenodeScores;
    if (!GetGoldminenodeScores(nBlockHash, vecGoldminenodeScores, nMinProtocol))
        return false;

    int nRank = 0;
    for (const auto& scorePair : vecGoldminenodeScores) {
        nRank++;
        vecGoldminenodeRanksRet.push_back(std::make_pair(nRank, *scorePair.second));
    }

    return true;
}

void CGoldminenodeMan::ProcessGoldminenodeConnections(CConnman& connman)
{
    //we don't care about this for regtest
    if(Params().NetworkIDString() == CBaseChainParams::REGTEST) return;

    connman.ForEachNode(CConnman::AllNodes, [](CNode* pnode) {
#ifdef ENABLE_WALLET
        if(pnode->fGoldminenode && !privateSendClient.IsMixingGoldminenode(pnode)) {
#else
        if(pnode->fGoldminenode) {
#endif // ENABLE_WALLET
            LogPrintf("Closing Goldminenode connection: peer=%d, addr=%s\n", pnode->id, pnode->addr.ToString());
            pnode->fDisconnect = true;
        }
    });
}

std::pair<CService, std::set<uint256> > CGoldminenodeMan::PopScheduledMnbRequestConnection()
{
    LOCK(cs);
    if(listScheduledMnbRequestConnections.empty()) {
        return std::make_pair(CService(), std::set<uint256>());
    }

    std::set<uint256> setResult;

    listScheduledMnbRequestConnections.sort();
    std::pair<CService, uint256> pairFront = listScheduledMnbRequestConnections.front();

    // squash hashes from requests with the same CService as the first one into setResult
    std::list< std::pair<CService, uint256> >::iterator it = listScheduledMnbRequestConnections.begin();
    while(it != listScheduledMnbRequestConnections.end()) {
        if(pairFront.first == it->first) {
            setResult.insert(it->second);
            it = listScheduledMnbRequestConnections.erase(it);
        } else {
            // since list is sorted now, we can be sure that there is no more hashes left
            // to ask for from this addr
            break;
        }
    }
    return std::make_pair(pairFront.first, setResult);
}

void CGoldminenodeMan::ProcessPendingMnbRequests(CConnman& connman)
{
    std::pair<CService, std::set<uint256> > p = PopScheduledMnbRequestConnection();
    if (!(p.first == CService() || p.second.empty())) {
        if (connman.IsGoldminenodeOrDisconnectRequested(p.first)) return;
        mapPendingMNB.insert(std::make_pair(p.first, std::make_pair(GetTime(), p.second)));
        connman.AddPendingGoldminenode(p.first);
    }

    std::map<CService, std::pair<int64_t, std::set<uint256> > >::iterator itPendingMNB = mapPendingMNB.begin();
    while (itPendingMNB != mapPendingMNB.end()) {
        bool fDone = connman.ForNode(itPendingMNB->first, [&](CNode* pnode) {
            // compile request vector
            std::vector<CInv> vToFetch;
            std::set<uint256>& setHashes = itPendingMNB->second.second;
            std::set<uint256>::iterator it = setHashes.begin();
            while(it != setHashes.end()) {
                if(*it != uint256()) {
                    vToFetch.push_back(CInv(MSG_GOLDMINENODE_ANNOUNCE, *it));
                    LogPrint("goldminenode", "-- asking for mnb %s from addr=%s\n", it->ToString(), pnode->addr.ToString());
                }
                ++it;
            }

            // ask for data
            CNetMsgMaker msgMaker(pnode->GetSendVersion());
            connman.PushMessage(pnode, msgMaker.Make(NetMsgType::GETDATA, vToFetch));
            return true;
        });

        int64_t nTimeAdded = itPendingMNB->second.first;
        if (fDone || (GetTime() - nTimeAdded > 15)) {
            if (!fDone) {
                LogPrint("goldminenode", "CGoldminenodeMan::%s -- failed to connect to %s\n", __func__, itPendingMNB->first.ToString());
            }
            mapPendingMNB.erase(itPendingMNB++);
        } else {
            ++itPendingMNB;
        }
    }
    LogPrint("goldminenode", "%s -- mapPendingMNB size: %d\n", __func__, mapPendingMNB.size());
}

void CGoldminenodeMan::ProcessMessage(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv, CConnman& connman)
{
    if(fLiteMode) return; // disable all Arc specific functionality

    if (strCommand == NetMsgType::MNANNOUNCE) { //Goldminenode Broadcast

        CGoldminenodeBroadcast mnb;
        vRecv >> mnb;

        pfrom->setAskFor.erase(mnb.GetHash());

        if(!goldminenodeSync.IsBlockchainSynced()) return;

        LogPrint("goldminenode", "MNANNOUNCE -- Goldminenode announce, goldminenode=%s\n", mnb.outpoint.ToStringShort());

        int nDos = 0;

        if (CheckMnbAndUpdateGoldminenodeList(pfrom, mnb, nDos, connman)) {
            // use announced Goldminenode as a peer
            connman.AddNewAddress(CAddress(mnb.addr, NODE_NETWORK), pfrom->addr, 2*60*60);
        } else if(nDos > 0) {
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), nDos);
        }

        if(fGoldminenodesAdded) {
            NotifyGoldminenodeUpdates(connman);
        }
    } else if (strCommand == NetMsgType::MNPING) { //Goldminenode Ping

        CGoldminenodePing mnp;
        vRecv >> mnp;

        uint256 nHash = mnp.GetHash();

        pfrom->setAskFor.erase(nHash);

        if(!goldminenodeSync.IsBlockchainSynced()) return;

        LogPrint("goldminenode", "MNPING -- Goldminenode ping, goldminenode=%s\n", mnp.goldminenodeOutpoint.ToStringShort());

        // Need LOCK2 here to ensure consistent locking order because the CheckAndUpdate call below locks cs_main
        LOCK2(cs_main, cs);

        if(mapSeenGoldminenodePing.count(nHash)) return; //seen
        mapSeenGoldminenodePing.insert(std::make_pair(nHash, mnp));

        LogPrint("goldminenode", "MNPING -- Goldminenode ping, goldminenode=%s new\n", mnp.goldminenodeOutpoint.ToStringShort());

        // see if we have this Goldminenode
        CGoldminenode* pmn = Find(mnp.goldminenodeOutpoint);

        if(pmn && mnp.fSentinelIsCurrent)
            UpdateLastSentinelPingTime();

        // too late, new MNANNOUNCE is required
        if(pmn && pmn->IsNewStartRequired()) return;

        int nDos = 0;
        if(mnp.CheckAndUpdate(pmn, false, nDos, connman)) return;

        if(nDos > 0) {
            // if anything significant failed, mark that node
            Misbehaving(pfrom->GetId(), nDos);
        } else if(pmn != NULL) {
            // nothing significant failed, mn is a known one too
            return;
        }

        // something significant is broken or mn is unknown,
        // we might have to ask for a goldminenode entry once
        AskForMN(pfrom, mnp.goldminenodeOutpoint, connman);

    } else if (strCommand == NetMsgType::DSEG) { //Get Goldminenode list or specific entry
        // Ignore such requests until we are fully synced.
        // We could start processing this after goldminenode list is synced
        // but this is a heavy one so it's better to finish sync first.
        if (!goldminenodeSync.IsSynced()) return;

        COutPoint goldminenodeOutpoint;

      //  if (pfrom->nVersion == 70209) {
            CTxIn vin;
            vRecv >> vin;
            goldminenodeOutpoint = vin.prevout;
     //   } else {
        //    vRecv >> goldminenodeOutpoint;
     //   }

        LogPrint("goldminenode", "DSEG -- Goldminenode list, goldminenode=%s\n", goldminenodeOutpoint.ToStringShort());

        if(goldminenodeOutpoint.IsNull()) {
            SyncAll(pfrom, connman);
        } else {
            SyncSingle(pfrom, goldminenodeOutpoint, connman);
        }

    } else if (strCommand == NetMsgType::MNVERIFY) { // Goldminenode Verify

        // Need LOCK2 here to ensure consistent locking order because all functions below call GetBlockHash which locks cs_main
        LOCK2(cs_main, cs);

        CGoldminenodeVerification mnv;
        vRecv >> mnv;

        pfrom->setAskFor.erase(mnv.GetHash());

        if(!goldminenodeSync.IsGoldminenodeListSynced()) return;

        if(mnv.vchSig1.empty()) {
            // CASE 1: someone asked me to verify myself /IP we are using/
            SendVerifyReply(pfrom, mnv, connman);
        } else if (mnv.vchSig2.empty()) {
            // CASE 2: we _probably_ got verification we requested from some goldminenode
            ProcessVerifyReply(pfrom, mnv);
        } else {
            // CASE 3: we _probably_ got verification broadcast signed by some goldminenode which verified another one
            ProcessVerifyBroadcast(pfrom, mnv);
        }
    }
}

void CGoldminenodeMan::SyncSingle(CNode* pnode, const COutPoint& outpoint, CConnman& connman)
{
    // do not provide any data until our node is synced
    if (!goldminenodeSync.IsSynced()) return;

    LOCK(cs);

    auto it = mapGoldminenodes.find(outpoint);

    if(it != mapGoldminenodes.end()) {
        if (it->second.addr.IsRFC1918() || it->second.addr.IsLocal()) return; // do not send local network goldminenode
        // NOTE: send goldminenode regardless of its current state, the other node will need it to verify old votes.
        LogPrint("goldminenode", "CGoldminenodeMan::%s -- Sending Goldminenode entry: goldminenode=%s  addr=%s\n", __func__, outpoint.ToStringShort(), it->second.addr.ToString());
        PushDsegInvs(pnode, it->second);
        LogPrintf("CGoldminenodeMan::%s -- Sent 1 Goldminenode inv to peer=%d\n", __func__, pnode->id);
    }
}

void CGoldminenodeMan::SyncAll(CNode* pnode, CConnman& connman)
{
    // do not provide any data until our node is synced
    if (!goldminenodeSync.IsSynced()) return;

    // local network
    bool isLocal = (pnode->addr.IsRFC1918() || pnode->addr.IsLocal());

    CService addrSquashed = Params().AllowMultiplePorts() ? (CService)pnode->addr : CService(pnode->addr, 0);
    // should only ask for this once
    if(!isLocal && Params().NetworkIDString() == CBaseChainParams::MAIN) {
        LOCK2(cs_main, cs);
        auto it = mAskedUsForGoldminenodeList.find(addrSquashed);
        if (it != mAskedUsForGoldminenodeList.end() && it->second > GetTime()) {
            Misbehaving(pnode->GetId(), 34);
            LogPrintf("CGoldminenodeMan::%s -- peer already asked me for the list, peer=%d\n", __func__, pnode->id);
            return;
        }
        int64_t askAgain = GetTime() + DSEG_UPDATE_SECONDS;
        mAskedUsForGoldminenodeList[addrSquashed] = askAgain;
    }

    int nInvCount = 0;

    LOCK(cs);

    for (const auto& mnpair : mapGoldminenodes) {
        if (mnpair.second.addr.IsRFC1918() || mnpair.second.addr.IsLocal()) continue; // do not send local network goldminenode
        // NOTE: send goldminenode regardless of its current state, the other node will need it to verify old votes.
        LogPrint("goldminenode", "CGoldminenodeMan::%s -- Sending Goldminenode entry: goldminenode=%s  addr=%s\n", __func__, mnpair.first.ToStringShort(), mnpair.second.addr.ToString());
        PushDsegInvs(pnode, mnpair.second);
        nInvCount++;
    }

    connman.PushMessage(pnode, CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::SYNCSTATUSCOUNT, GOLDMINENODE_SYNC_LIST, nInvCount));
    LogPrintf("CGoldminenodeMan::%s -- Sent %d Goldminenode invs to peer=%d\n", __func__, nInvCount, pnode->id);
}

void CGoldminenodeMan::PushDsegInvs(CNode* pnode, const CGoldminenode& mn)
{
    AssertLockHeld(cs);

    CGoldminenodeBroadcast mnb(mn);
    CGoldminenodePing mnp = mnb.lastPing;
    uint256 hashMNB = mnb.GetHash();
    uint256 hashMNP = mnp.GetHash();
    pnode->PushInventory(CInv(MSG_GOLDMINENODE_ANNOUNCE, hashMNB));
    pnode->PushInventory(CInv(MSG_GOLDMINENODE_PING, hashMNP));
    mapSeenGoldminenodeBroadcast.insert(std::make_pair(hashMNB, std::make_pair(GetTime(), mnb)));
    mapSeenGoldminenodePing.insert(std::make_pair(hashMNP, mnp));
}

// Verification of goldminenodes via unique direct requests.

void CGoldminenodeMan::DoFullVerificationStep(CConnman& connman)
{
    if(activeGoldminenode.outpoint.IsNull()) return;
    if(!goldminenodeSync.IsSynced()) return;

    rank_pair_vec_t vecGoldminenodeRanks;
    GetGoldminenodeRanks(vecGoldminenodeRanks, nCachedBlockHeight - 1, MIN_POSE_PROTO_VERSION);

    LOCK(cs);

    int nCount = 0;

    int nMyRank = -1;
    int nRanksTotal = (int)vecGoldminenodeRanks.size();

    // send verify requests only if we are in top MAX_POSE_RANK
    rank_pair_vec_t::iterator it = vecGoldminenodeRanks.begin();
    while(it != vecGoldminenodeRanks.end()) {
        if(it->first > MAX_POSE_RANK) {
            LogPrint("goldminenode", "CGoldminenodeMan::DoFullVerificationStep -- Must be in top %d to send verify request\n",
                        (int)MAX_POSE_RANK);
            return;
        }
        if(it->second.outpoint == activeGoldminenode.outpoint) {
            nMyRank = it->first;
            LogPrint("goldminenode", "CGoldminenodeMan::DoFullVerificationStep -- Found self at rank %d/%d, verifying up to %d goldminenodes\n",
                        nMyRank, nRanksTotal, (int)MAX_POSE_CONNECTIONS);
            break;
        }
        ++it;
    }

    // edge case: list is too short and this goldminenode is not enabled
    if(nMyRank == -1) return;

    // send verify requests to up to MAX_POSE_CONNECTIONS goldminenodes
    // starting from MAX_POSE_RANK + nMyRank and using MAX_POSE_CONNECTIONS as a step
    int nOffset = MAX_POSE_RANK + nMyRank - 1;
    if(nOffset >= (int)vecGoldminenodeRanks.size()) return;

    std::vector<const CGoldminenode*> vSortedByAddr;
    for (const auto& mnpair : mapGoldminenodes) {
        vSortedByAddr.push_back(&mnpair.second);
    }

    sort(vSortedByAddr.begin(), vSortedByAddr.end(), CompareByAddr());

    it = vecGoldminenodeRanks.begin() + nOffset;
    while(it != vecGoldminenodeRanks.end()) {
        if(it->second.IsPoSeVerified() || it->second.IsPoSeBanned()) {
            LogPrint("goldminenode", "CGoldminenodeMan::DoFullVerificationStep -- Already %s%s%s goldminenode %s address %s, skipping...\n",
                        it->second.IsPoSeVerified() ? "verified" : "",
                        it->second.IsPoSeVerified() && it->second.IsPoSeBanned() ? " and " : "",
                        it->second.IsPoSeBanned() ? "banned" : "",
                        it->second.outpoint.ToStringShort(), it->second.addr.ToString());
            nOffset += MAX_POSE_CONNECTIONS;
            if(nOffset >= (int)vecGoldminenodeRanks.size()) break;
            it += MAX_POSE_CONNECTIONS;
            continue;
        }
        LogPrint("goldminenode", "CGoldminenodeMan::DoFullVerificationStep -- Verifying goldminenode %s rank %d/%d address %s\n",
                    it->second.outpoint.ToStringShort(), it->first, nRanksTotal, it->second.addr.ToString());
        if(SendVerifyRequest(CAddress(it->second.addr, NODE_NETWORK), vSortedByAddr, connman)) {
            nCount++;
            if(nCount >= MAX_POSE_CONNECTIONS) break;
        }
        nOffset += MAX_POSE_CONNECTIONS;
        if(nOffset >= (int)vecGoldminenodeRanks.size()) break;
        it += MAX_POSE_CONNECTIONS;
    }

    LogPrint("goldminenode", "CGoldminenodeMan::DoFullVerificationStep -- Sent verification requests to %d goldminenodes\n", nCount);
}

// This function tries to find goldminenodes with the same addr,
// find a verified one and ban all the other. If there are many nodes
// with the same addr but none of them is verified yet, then none of them are banned.
// It could take many times to run this before most of the duplicate nodes are banned.

void CGoldminenodeMan::CheckSameAddr()
{
    if(!goldminenodeSync.IsSynced() || mapGoldminenodes.empty()) return;

    std::vector<CGoldminenode*> vBan;
    std::vector<CGoldminenode*> vSortedByAddr;

    {
        LOCK(cs);

        CGoldminenode* pprevGoldminenode = NULL;
        CGoldminenode* pverifiedGoldminenode = NULL;

        for (auto& mnpair : mapGoldminenodes) {
            vSortedByAddr.push_back(&mnpair.second);
        }

        sort(vSortedByAddr.begin(), vSortedByAddr.end(), CompareByAddr());

        for (const auto& pmn : vSortedByAddr) {
            // check only (pre)enabled goldminenodes
            if(!pmn->IsEnabled() && !pmn->IsPreEnabled()) continue;
            // initial step
            if(!pprevGoldminenode) {
                pprevGoldminenode = pmn;
                pverifiedGoldminenode = pmn->IsPoSeVerified() ? pmn : NULL;
                continue;
            }
            // second+ step
            if(pmn->addr == pprevGoldminenode->addr) {
                if(pverifiedGoldminenode) {
                    // another goldminenode with the same ip is verified, ban this one
                    vBan.push_back(pmn);
                } else if(pmn->IsPoSeVerified()) {
                    // this goldminenode with the same ip is verified, ban previous one
                    vBan.push_back(pprevGoldminenode);
                    // and keep a reference to be able to ban following goldminenodes with the same ip
                    pverifiedGoldminenode = pmn;
                }
            } else {
                pverifiedGoldminenode = pmn->IsPoSeVerified() ? pmn : NULL;
            }
            pprevGoldminenode = pmn;
        }
    }

    // ban duplicates
    for (auto& pmn : vBan) {
        LogPrintf("CGoldminenodeMan::CheckSameAddr -- increasing PoSe ban score for goldminenode %s\n", pmn->outpoint.ToStringShort());
        pmn->IncreasePoSeBanScore();
    }
}

bool CGoldminenodeMan::SendVerifyRequest(const CAddress& addr, const std::vector<const CGoldminenode*>& vSortedByAddr, CConnman& connman)
{
    if(netfulfilledman.HasFulfilledRequest(addr, strprintf("%s", NetMsgType::MNVERIFY)+"-request")) {
        // we already asked for verification, not a good idea to do this too often, skip it
        LogPrint("goldminenode", "CGoldminenodeMan::SendVerifyRequest -- too many requests, skipping... addr=%s\n", addr.ToString());
        return false;
    }

    if (connman.IsGoldminenodeOrDisconnectRequested(addr)) return false;

    connman.AddPendingGoldminenode(addr);
    // use random nonce, store it and require node to reply with correct one later
    CGoldminenodeVerification mnv(addr, GetRandInt(999999), nCachedBlockHeight - 1);
    LOCK(cs_mapPendingMNV);
    mapPendingMNV.insert(std::make_pair(addr, std::make_pair(GetTime(), mnv)));
    LogPrintf("CGoldminenodeMan::SendVerifyRequest -- verifying node using nonce %d addr=%s\n", mnv.nonce, addr.ToString());
    return true;
}

void CGoldminenodeMan::ProcessPendingMnvRequests(CConnman& connman)
{
    LOCK(cs_mapPendingMNV);

    std::map<CService, std::pair<int64_t, CGoldminenodeVerification> >::iterator itPendingMNV = mapPendingMNV.begin();

    while (itPendingMNV != mapPendingMNV.end()) {
        bool fDone = connman.ForNode(itPendingMNV->first, [&](CNode* pnode) {
            netfulfilledman.AddFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-request");
            // use random nonce, store it and require node to reply with correct one later
            mWeAskedForVerification[pnode->addr] = itPendingMNV->second.second;
            LogPrint("goldminenode", "-- verifying node using nonce %d addr=%s\n", itPendingMNV->second.second.nonce, pnode->addr.ToString());
            CNetMsgMaker msgMaker(pnode->GetSendVersion()); // TODO this gives a warning about version not being set (we should wait for VERSION exchange)
            connman.PushMessage(pnode, msgMaker.Make(NetMsgType::MNVERIFY, itPendingMNV->second.second));
            return true;
        });

        int64_t nTimeAdded = itPendingMNV->second.first;
        if (fDone || (GetTime() - nTimeAdded > 15)) {
            if (!fDone) {
                LogPrint("goldminenode", "CGoldminenodeMan::%s -- failed to connect to %s\n", __func__, itPendingMNV->first.ToString());
            }
            mapPendingMNV.erase(itPendingMNV++);
        } else {
            ++itPendingMNV;
        }
    }
    LogPrint("goldminenode", "%s -- mapPendingMNV size: %d\n", __func__, mapPendingMNV.size());
}

void CGoldminenodeMan::SendVerifyReply(CNode* pnode, CGoldminenodeVerification& mnv, CConnman& connman)
{
    AssertLockHeld(cs_main);

    // only goldminenodes can sign this, why would someone ask regular node?
    if(!fGoldminenodeMode) {
        // do not ban, malicious node might be using my IP
        // and trying to confuse the node which tries to verify it
        return;
    }

    if(netfulfilledman.HasFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-reply")) {
        // peer should not ask us that often
        LogPrintf("GoldminenodeMan::SendVerifyReply -- ERROR: peer already asked me recently, peer=%d\n", pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    uint256 blockHash;
    if(!GetBlockHash(blockHash, mnv.nBlockHeight)) {
        LogPrintf("GoldminenodeMan::SendVerifyReply -- can't get block hash for unknown block height %d, peer=%d\n", mnv.nBlockHeight, pnode->id);
        return;
    }

    std::string strError;

    if (sporkManager.IsSporkActive(SPORK_6_NEW_SIGS)) {
        uint256 hash = mnv.GetSignatureHash1(blockHash);

        if(!CHashSigner::SignHash(hash, activeGoldminenode.keyGoldminenode, mnv.vchSig1)) {
            LogPrintf("CGoldminenodeMan::SendVerifyReply -- SignHash() failed\n");
            return;
        }

        if (!CHashSigner::VerifyHash(hash, activeGoldminenode.pubKeyGoldminenode, mnv.vchSig1, strError)) {
            LogPrintf("CGoldminenodeMan::SendVerifyReply -- VerifyHash() failed, error: %s\n", strError);
            return;
        }
    } else {
        std::string strMessage = strprintf("%s%d%s", activeGoldminenode.service.ToString(false), mnv.nonce, blockHash.ToString());

        if(!CMessageSigner::SignMessage(strMessage, mnv.vchSig1, activeGoldminenode.keyGoldminenode)) {
            LogPrintf("GoldminenodeMan::SendVerifyReply -- SignMessage() failed\n");
            return;
        }

        if(!CMessageSigner::VerifyMessage(activeGoldminenode.pubKeyGoldminenode, mnv.vchSig1, strMessage, strError)) {
            LogPrintf("GoldminenodeMan::SendVerifyReply -- VerifyMessage() failed, error: %s\n", strError);
            return;
        }
    }

    CNetMsgMaker msgMaker(pnode->GetSendVersion());
    connman.PushMessage(pnode, msgMaker.Make(NetMsgType::MNVERIFY, mnv));
    netfulfilledman.AddFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-reply");
}

void CGoldminenodeMan::ProcessVerifyReply(CNode* pnode, CGoldminenodeVerification& mnv)
{
    AssertLockHeld(cs_main);

    std::string strError;

    // did we even ask for it? if that's the case we should have matching fulfilled request
    if(!netfulfilledman.HasFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-request")) {
        LogPrintf("CGoldminenodeMan::ProcessVerifyReply -- ERROR: we didn't ask for verification of %s, peer=%d\n", pnode->addr.ToString(), pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    // Received nonce for a known address must match the one we sent
    if(mWeAskedForVerification[pnode->addr].nonce != mnv.nonce) {
        LogPrintf("CGoldminenodeMan::ProcessVerifyReply -- ERROR: wrong nounce: requested=%d, received=%d, peer=%d\n",
                    mWeAskedForVerification[pnode->addr].nonce, mnv.nonce, pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    // Received nBlockHeight for a known address must match the one we sent
    if(mWeAskedForVerification[pnode->addr].nBlockHeight != mnv.nBlockHeight) {
        LogPrintf("CGoldminenodeMan::ProcessVerifyReply -- ERROR: wrong nBlockHeight: requested=%d, received=%d, peer=%d\n",
                    mWeAskedForVerification[pnode->addr].nBlockHeight, mnv.nBlockHeight, pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    uint256 blockHash;
    if(!GetBlockHash(blockHash, mnv.nBlockHeight)) {
        // this shouldn't happen...
        LogPrintf("GoldminenodeMan::ProcessVerifyReply -- can't get block hash for unknown block height %d, peer=%d\n", mnv.nBlockHeight, pnode->id);
        return;
    }

    // we already verified this address, why node is spamming?
    if(netfulfilledman.HasFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-done")) {
        LogPrintf("CGoldminenodeMan::ProcessVerifyReply -- ERROR: already verified %s recently\n", pnode->addr.ToString());
        Misbehaving(pnode->id, 20);
        return;
    }

    {
        LOCK(cs);

        CGoldminenode* prealGoldminenode = NULL;
        std::vector<CGoldminenode*> vpGoldminenodesToBan;

        uint256 hash1 = mnv.GetSignatureHash1(blockHash);
        std::string strMessage1 = strprintf("%s%d%s", pnode->addr.ToString(false), mnv.nonce, blockHash.ToString());

        for (auto& mnpair : mapGoldminenodes) {
            if(CAddress(mnpair.second.addr, NODE_NETWORK) == pnode->addr) {
                bool fFound = false;
                if (sporkManager.IsSporkActive(SPORK_6_NEW_SIGS)) {
                    fFound = CHashSigner::VerifyHash(hash1, mnpair.second.pubKeyGoldminenode, mnv.vchSig1, strError);
                    // we don't care about mnv with signature in old format
                } else {
                    fFound = CMessageSigner::VerifyMessage(mnpair.second.pubKeyGoldminenode, mnv.vchSig1, strMessage1, strError);
                }
                if (fFound) {
                    // found it!
                    prealGoldminenode = &mnpair.second;
                    if(!mnpair.second.IsPoSeVerified()) {
                        mnpair.second.DecreasePoSeBanScore();
                    }
                    netfulfilledman.AddFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-done");

                    // we can only broadcast it if we are an activated goldminenode
                    if(activeGoldminenode.outpoint.IsNull()) continue;
                    // update ...
                    mnv.addr = mnpair.second.addr;
                    mnv.goldminenodeOutpoint1 = mnpair.second.outpoint;
                    mnv.goldminenodeOutpoint2 = activeGoldminenode.outpoint;
                    // ... and sign it
                    std::string strError;

                    if (sporkManager.IsSporkActive(SPORK_6_NEW_SIGS)) {
                        uint256 hash2 = mnv.GetSignatureHash2(blockHash);

                        if(!CHashSigner::SignHash(hash2, activeGoldminenode.keyGoldminenode, mnv.vchSig2)) {
                            LogPrintf("GoldminenodeMan::ProcessVerifyReply -- SignHash() failed\n");
                            return;
                        }

                        if(!CHashSigner::VerifyHash(hash2, activeGoldminenode.pubKeyGoldminenode, mnv.vchSig2, strError)) {
                            LogPrintf("GoldminenodeMan::ProcessVerifyReply -- VerifyHash() failed, error: %s\n", strError);
                            return;
                        }
                    } else {
                        std::string strMessage2 = strprintf("%s%d%s%s%s", mnv.addr.ToString(false), mnv.nonce, blockHash.ToString(),
                                                mnv.goldminenodeOutpoint1.ToStringShort(), mnv.goldminenodeOutpoint2.ToStringShort());

                        if(!CMessageSigner::SignMessage(strMessage2, mnv.vchSig2, activeGoldminenode.keyGoldminenode)) {
                            LogPrintf("GoldminenodeMan::ProcessVerifyReply -- SignMessage() failed\n");
                            return;
                        }

                        if(!CMessageSigner::VerifyMessage(activeGoldminenode.pubKeyGoldminenode, mnv.vchSig2, strMessage2, strError)) {
                            LogPrintf("GoldminenodeMan::ProcessVerifyReply -- VerifyMessage() failed, error: %s\n", strError);
                            return;
                        }
                    }

                    mWeAskedForVerification[pnode->addr] = mnv;
                    mapSeenGoldminenodeVerification.insert(std::make_pair(mnv.GetHash(), mnv));
                    mnv.Relay();

                } else {
                    vpGoldminenodesToBan.push_back(&mnpair.second);
                }
            }
        }
        // no real goldminenode found?...
        if(!prealGoldminenode) {
            // this should never be the case normally,
            // only if someone is trying to game the system in some way or smth like that
            LogPrintf("CGoldminenodeMan::ProcessVerifyReply -- ERROR: no real goldminenode found for addr %s\n", pnode->addr.ToString());
            Misbehaving(pnode->id, 20);
            return;
        }
        LogPrintf("CGoldminenodeMan::ProcessVerifyReply -- verified real goldminenode %s for addr %s\n",
                    prealGoldminenode->outpoint.ToStringShort(), pnode->addr.ToString());
        // increase ban score for everyone else
        for (const auto& pmn : vpGoldminenodesToBan) {
            pmn->IncreasePoSeBanScore();
            LogPrint("goldminenode", "CGoldminenodeMan::ProcessVerifyReply -- increased PoSe ban score for %s addr %s, new score %d\n",
                        prealGoldminenode->outpoint.ToStringShort(), pnode->addr.ToString(), pmn->nPoSeBanScore);
        }
        if(!vpGoldminenodesToBan.empty())
            LogPrintf("CGoldminenodeMan::ProcessVerifyReply -- PoSe score increased for %d fake goldminenodes, addr %s\n",
                        (int)vpGoldminenodesToBan.size(), pnode->addr.ToString());
    }
}

void CGoldminenodeMan::ProcessVerifyBroadcast(CNode* pnode, const CGoldminenodeVerification& mnv)
{
    AssertLockHeld(cs_main);

    std::string strError;

    if(mapSeenGoldminenodeVerification.find(mnv.GetHash()) != mapSeenGoldminenodeVerification.end()) {
        // we already have one
        return;
    }
    mapSeenGoldminenodeVerification[mnv.GetHash()] = mnv;

    // we don't care about history
    if(mnv.nBlockHeight < nCachedBlockHeight - MAX_POSE_BLOCKS) {
        LogPrint("goldminenode", "CGoldminenodeMan::ProcessVerifyBroadcast -- Outdated: current block %d, verification block %d, peer=%d\n",
                    nCachedBlockHeight, mnv.nBlockHeight, pnode->id);
        return;
    }

    if(mnv.goldminenodeOutpoint1 == mnv.goldminenodeOutpoint2) {
        LogPrint("goldminenode", "CGoldminenodeMan::ProcessVerifyBroadcast -- ERROR: same outpoints %s, peer=%d\n",
                    mnv.goldminenodeOutpoint1.ToStringShort(), pnode->id);
        // that was NOT a good idea to cheat and verify itself,
        // ban the node we received such message from
        Misbehaving(pnode->id, 100);
        return;
    }

    uint256 blockHash;
    if(!GetBlockHash(blockHash, mnv.nBlockHeight)) {
        // this shouldn't happen...
        LogPrintf("CGoldminenodeMan::ProcessVerifyBroadcast -- Can't get block hash for unknown block height %d, peer=%d\n", mnv.nBlockHeight, pnode->id);
        return;
    }

    int nRank;

    if (!GetGoldminenodeRank(mnv.goldminenodeOutpoint2, nRank, mnv.nBlockHeight, MIN_POSE_PROTO_VERSION)) {
        LogPrint("goldminenode", "CGoldminenodeMan::ProcessVerifyBroadcast -- Can't calculate rank for goldminenode %s\n",
                    mnv.goldminenodeOutpoint2.ToStringShort());
        return;
    }

    if(nRank > MAX_POSE_RANK) {
        LogPrint("goldminenode", "CGoldminenodeMan::ProcessVerifyBroadcast -- Goldminenode %s is not in top %d, current rank %d, peer=%d\n",
                    mnv.goldminenodeOutpoint2.ToStringShort(), (int)MAX_POSE_RANK, nRank, pnode->id);
        return;
    }

    {
        LOCK(cs);

        CGoldminenode* pmn1 = Find(mnv.goldminenodeOutpoint1);
        if(!pmn1) {
            LogPrintf("CGoldminenodeMan::ProcessVerifyBroadcast -- can't find goldminenode1 %s\n", mnv.goldminenodeOutpoint1.ToStringShort());
            return;
        }

        CGoldminenode* pmn2 = Find(mnv.goldminenodeOutpoint2);
        if(!pmn2) {
            LogPrintf("CGoldminenodeMan::ProcessVerifyBroadcast -- can't find goldminenode2 %s\n", mnv.goldminenodeOutpoint2.ToStringShort());
            return;
        }

        if(pmn1->addr != mnv.addr) {
            LogPrintf("CGoldminenodeMan::ProcessVerifyBroadcast -- addr %s does not match %s\n", mnv.addr.ToString(), pmn1->addr.ToString());
            return;
        }

        if (sporkManager.IsSporkActive(SPORK_6_NEW_SIGS)) {
            uint256 hash1 = mnv.GetSignatureHash1(blockHash);
            uint256 hash2 = mnv.GetSignatureHash2(blockHash);

            if(!CHashSigner::VerifyHash(hash1, pmn1->pubKeyGoldminenode, mnv.vchSig1, strError)) {
                LogPrintf("GoldminenodeMan::ProcessVerifyBroadcast -- VerifyHash() failed, error: %s\n", strError);
                return;
            }

            if(!CHashSigner::VerifyHash(hash2, pmn2->pubKeyGoldminenode, mnv.vchSig2, strError)) {
                LogPrintf("GoldminenodeMan::ProcessVerifyBroadcast -- VerifyHash() failed, error: %s\n", strError);
                return;
            }
        } else {
            std::string strMessage1 = strprintf("%s%d%s", mnv.addr.ToString(false), mnv.nonce, blockHash.ToString());
            std::string strMessage2 = strprintf("%s%d%s%s%s", mnv.addr.ToString(false), mnv.nonce, blockHash.ToString(),
                                    mnv.goldminenodeOutpoint1.ToStringShort(), mnv.goldminenodeOutpoint2.ToStringShort());

            if(!CMessageSigner::VerifyMessage(pmn1->pubKeyGoldminenode, mnv.vchSig1, strMessage1, strError)) {
                LogPrintf("CGoldminenodeMan::ProcessVerifyBroadcast -- VerifyMessage() for goldminenode1 failed, error: %s\n", strError);
                return;
            }

            if(!CMessageSigner::VerifyMessage(pmn2->pubKeyGoldminenode, mnv.vchSig2, strMessage2, strError)) {
                LogPrintf("CGoldminenodeMan::ProcessVerifyBroadcast -- VerifyMessage() for goldminenode2 failed, error: %s\n", strError);
                return;
            }
        }

        if(!pmn1->IsPoSeVerified()) {
            pmn1->DecreasePoSeBanScore();
        }
        mnv.Relay();

        LogPrintf("CGoldminenodeMan::ProcessVerifyBroadcast -- verified goldminenode %s for addr %s\n",
                    pmn1->outpoint.ToStringShort(), pmn1->addr.ToString());

        // increase ban score for everyone else with the same addr
        int nCount = 0;
        for (auto& mnpair : mapGoldminenodes) {
            if(mnpair.second.addr != mnv.addr || mnpair.first == mnv.goldminenodeOutpoint1) continue;
            mnpair.second.IncreasePoSeBanScore();
            nCount++;
            LogPrint("goldminenode", "CGoldminenodeMan::ProcessVerifyBroadcast -- increased PoSe ban score for %s addr %s, new score %d\n",
                        mnpair.first.ToStringShort(), mnpair.second.addr.ToString(), mnpair.second.nPoSeBanScore);
        }
        if(nCount)
            LogPrintf("CGoldminenodeMan::ProcessVerifyBroadcast -- PoSe score increased for %d fake goldminenodes, addr %s\n",
                        nCount, pmn1->addr.ToString());
    }
}

std::string CGoldminenodeMan::ToString() const
{
    std::ostringstream info;

    info << "Goldminenodes: " << (int)mapGoldminenodes.size() <<
            ", peers who asked us for Goldminenode list: " << (int)mAskedUsForGoldminenodeList.size() <<
            ", peers we asked for Goldminenode list: " << (int)mWeAskedForGoldminenodeList.size() <<
            ", entries in Goldminenode list we asked for: " << (int)mWeAskedForGoldminenodeListEntry.size() <<
            ", nDsqCount: " << (int)nDsqCount;

    return info.str();
}

bool CGoldminenodeMan::CheckMnbAndUpdateGoldminenodeList(CNode* pfrom, CGoldminenodeBroadcast mnb, int& nDos, CConnman& connman)
{
    // Need to lock cs_main here to ensure consistent locking order because the SimpleCheck call below locks cs_main
    LOCK(cs_main);

    {
        LOCK(cs);
        nDos = 0;
        LogPrint("goldminenode", "CGoldminenodeMan::CheckMnbAndUpdateGoldminenodeList -- goldminenode=%s\n", mnb.outpoint.ToStringShort());

        uint256 hash = mnb.GetHash();
        if(mapSeenGoldminenodeBroadcast.count(hash) && !mnb.fRecovery) { //seen
            LogPrint("goldminenode", "CGoldminenodeMan::CheckMnbAndUpdateGoldminenodeList -- goldminenode=%s seen\n", mnb.outpoint.ToStringShort());
            // less then 2 pings left before this MN goes into non-recoverable state, bump sync timeout
            if(GetTime() - mapSeenGoldminenodeBroadcast[hash].first > GOLDMINENODE_NEW_START_REQUIRED_SECONDS - GOLDMINENODE_MIN_MNP_SECONDS * 2) {
                LogPrint("goldminenode", "CGoldminenodeMan::CheckMnbAndUpdateGoldminenodeList -- goldminenode=%s seen update\n", mnb.outpoint.ToStringShort());
                mapSeenGoldminenodeBroadcast[hash].first = GetTime();
                goldminenodeSync.BumpAssetLastTime("CGoldminenodeMan::CheckMnbAndUpdateGoldminenodeList - seen");
            }
            // did we ask this node for it?
            if(pfrom && IsMnbRecoveryRequested(hash) && GetTime() < mMnbRecoveryRequests[hash].first) {
                LogPrint("goldminenode", "CGoldminenodeMan::CheckMnbAndUpdateGoldminenodeList -- mnb=%s seen request\n", hash.ToString());
                if(mMnbRecoveryRequests[hash].second.count(pfrom->addr)) {
                    LogPrint("goldminenode", "CGoldminenodeMan::CheckMnbAndUpdateGoldminenodeList -- mnb=%s seen request, addr=%s\n", hash.ToString(), pfrom->addr.ToString());
                    // do not allow node to send same mnb multiple times in recovery mode
                    mMnbRecoveryRequests[hash].second.erase(pfrom->addr);
                    // does it have newer lastPing?
                    if(mnb.lastPing.sigTime > mapSeenGoldminenodeBroadcast[hash].second.lastPing.sigTime) {
                        // simulate Check
                        CGoldminenode mnTemp = CGoldminenode(mnb);
                        mnTemp.Check();
                        LogPrint("goldminenode", "CGoldminenodeMan::CheckMnbAndUpdateGoldminenodeList -- mnb=%s seen request, addr=%s, better lastPing: %d min ago, projected mn state: %s\n", hash.ToString(), pfrom->addr.ToString(), (GetAdjustedTime() - mnb.lastPing.sigTime)/60, mnTemp.GetStateString());
                        if(mnTemp.IsValidStateForAutoStart(mnTemp.nActiveState)) {
                            // this node thinks it's a good one
                            LogPrint("goldminenode", "CGoldminenodeMan::CheckMnbAndUpdateGoldminenodeList -- goldminenode=%s seen good\n", mnb.outpoint.ToStringShort());
                            mMnbRecoveryGoodReplies[hash].push_back(mnb);
                        }
                    }
                }
            }
            return true;
        }
        mapSeenGoldminenodeBroadcast.insert(std::make_pair(hash, std::make_pair(GetTime(), mnb)));

        LogPrint("goldminenode", "CGoldminenodeMan::CheckMnbAndUpdateGoldminenodeList -- goldminenode=%s new\n", mnb.outpoint.ToStringShort());

        if(!mnb.SimpleCheck(nDos)) {
            LogPrint("goldminenode", "CGoldminenodeMan::CheckMnbAndUpdateGoldminenodeList -- SimpleCheck() failed, goldminenode=%s\n", mnb.outpoint.ToStringShort());
            return false;
        }

        // search Goldminenode list
        CGoldminenode* pmn = Find(mnb.outpoint);
        if(pmn) {
            CGoldminenodeBroadcast mnbOld = mapSeenGoldminenodeBroadcast[CGoldminenodeBroadcast(*pmn).GetHash()].second;
            if(!mnb.Update(pmn, nDos, connman)) {
                LogPrint("goldminenode", "CGoldminenodeMan::CheckMnbAndUpdateGoldminenodeList -- Update() failed, goldminenode=%s\n", mnb.outpoint.ToStringShort());
                return false;
            }
            if(hash != mnbOld.GetHash()) {
                mapSeenGoldminenodeBroadcast.erase(mnbOld.GetHash());
            }
            return true;
        }
    }

    if(mnb.CheckOutpoint(nDos)) {
        Add(mnb);
        goldminenodeSync.BumpAssetLastTime("CGoldminenodeMan::CheckMnbAndUpdateGoldminenodeList - new");
        // if it matches our Goldminenode privkey...
        if(fGoldminenodeMode && mnb.pubKeyGoldminenode == activeGoldminenode.pubKeyGoldminenode) {
            mnb.nPoSeBanScore = -GOLDMINENODE_POSE_BAN_MAX_SCORE;
            if(mnb.nProtocolVersion == PROTOCOL_VERSION) {
                // ... and PROTOCOL_VERSION, then we've been remotely activated ...
                LogPrintf("CGoldminenodeMan::CheckMnbAndUpdateGoldminenodeList -- Got NEW Goldminenode entry: goldminenode=%s  sigTime=%lld  addr=%s\n",
                            mnb.outpoint.ToStringShort(), mnb.sigTime, mnb.addr.ToString());
                activeGoldminenode.ManageState(connman);
            } else {
                // ... otherwise we need to reactivate our node, do not add it to the list and do not relay
                // but also do not ban the node we get this message from
                LogPrintf("CGoldminenodeMan::CheckMnbAndUpdateGoldminenodeList -- wrong PROTOCOL_VERSION, re-activate your MN: message nProtocolVersion=%d  PROTOCOL_VERSION=%d\n", mnb.nProtocolVersion, PROTOCOL_VERSION);
                return false;
            }
        }
        mnb.Relay(connman);
    } else {
        LogPrintf("CGoldminenodeMan::CheckMnbAndUpdateGoldminenodeList -- Rejected Goldminenode entry: %s  addr=%s\n", mnb.outpoint.ToStringShort(), mnb.addr.ToString());
        return false;
    }

    return true;
}

void CGoldminenodeMan::UpdateLastPaid(const CBlockIndex* pindex)
{
    LOCK(cs);

    if(fLiteMode || !goldminenodeSync.IsWinnersListSynced() || mapGoldminenodes.empty()) return;

    static int nLastRunBlockHeight = 0;
    // Scan at least LAST_PAID_SCAN_BLOCKS but no more than mnpayments.GetStorageLimit()
    int nMaxBlocksToScanBack = std::max(LAST_PAID_SCAN_BLOCKS, nCachedBlockHeight - nLastRunBlockHeight);
    nMaxBlocksToScanBack = std::min(nMaxBlocksToScanBack, mnpayments.GetStorageLimit());

    LogPrint("goldminenode", "CGoldminenodeMan::UpdateLastPaid -- nCachedBlockHeight=%d, nLastRunBlockHeight=%d, nMaxBlocksToScanBack=%d\n",
                            nCachedBlockHeight, nLastRunBlockHeight, nMaxBlocksToScanBack);

    for (auto& mnpair : mapGoldminenodes) {
        mnpair.second.UpdateLastPaid(pindex, nMaxBlocksToScanBack);
    }

    nLastRunBlockHeight = nCachedBlockHeight;
}

void CGoldminenodeMan::UpdateLastSentinelPingTime()
{
    LOCK(cs);
    nLastSentinelPingTime = GetTime();
}

bool CGoldminenodeMan::IsSentinelPingActive()
{
    LOCK(cs);
    // Check if any goldminenodes have voted recently, otherwise return false
    return (GetTime() - nLastSentinelPingTime) <= GOLDMINENODE_SENTINEL_PING_MAX_SECONDS;
}

void CGoldminenodeMan::CheckGoldminenode(const CPubKey& pubKeyGoldminenode, bool fForce)
{
    LOCK2(cs_main, cs);
    for (auto& mnpair : mapGoldminenodes) {
        if (mnpair.second.pubKeyGoldminenode == pubKeyGoldminenode) {
            mnpair.second.Check(fForce);
            return;
        }
    }
}

bool CGoldminenodeMan::IsGoldminenodePingedWithin(const COutPoint& outpoint, int nSeconds, int64_t nTimeToCheckAt)
{
    LOCK(cs);
    CGoldminenode* pmn = Find(outpoint);
    return pmn ? pmn->IsPingedWithin(nSeconds, nTimeToCheckAt) : false;
}

void CGoldminenodeMan::SetGoldminenodeLastPing(const COutPoint& outpoint, const CGoldminenodePing& mnp)
{
    LOCK(cs);
    CGoldminenode* pmn = Find(outpoint);
    if(!pmn) {
        return;
    }
    pmn->lastPing = mnp;
    if(mnp.fSentinelIsCurrent) {
        UpdateLastSentinelPingTime();
    }
    mapSeenGoldminenodePing.insert(std::make_pair(mnp.GetHash(), mnp));

    CGoldminenodeBroadcast mnb(*pmn);
    uint256 hash = mnb.GetHash();
    if(mapSeenGoldminenodeBroadcast.count(hash)) {
        mapSeenGoldminenodeBroadcast[hash].second.lastPing = mnp;
    }
}

void CGoldminenodeMan::UpdatedBlockTip(const CBlockIndex *pindex)
{
    nCachedBlockHeight = pindex->nHeight;
    LogPrint("goldminenode", "CGoldminenodeMan::UpdatedBlockTip -- nCachedBlockHeight=%d\n", nCachedBlockHeight);

    CheckSameAddr();

    if(fGoldminenodeMode) {
        // normal wallet does not need to update this every block, doing update on rpc call should be enough
        UpdateLastPaid(pindex);
    }
}

void CGoldminenodeMan::WarnGoldminenodeDaemonUpdates()
{
    LOCK(cs);

    static bool fWarned = false;

    if (fWarned || !size() || !goldminenodeSync.IsGoldminenodeListSynced())
        return;

    int nUpdatedGoldminenodes{0};

    for (const auto& mnpair : mapGoldminenodes) {
        if (mnpair.second.lastPing.nDaemonVersion > CLIENT_VERSION) {
            ++nUpdatedGoldminenodes;
        }
    }

    // Warn only when at least half of known goldminenodes already updated
    if (nUpdatedGoldminenodes < size() / 2)
        return;

    std::string strWarning;
    if (nUpdatedGoldminenodes != size()) {
        strWarning = strprintf(_("Warning: At least %d of %d goldminenodes are running on a newer software version. Please check latest releases, you might need to update too."),
                    nUpdatedGoldminenodes, size());
    } else {
        // someone was postponing this update for way too long probably
        strWarning = strprintf(_("Warning: Every goldminenode (out of %d known ones) is running on a newer software version. Please check latest releases, it's very likely that you missed a major/critical update."),
                    size());
    }

    // notify GetWarnings(), called by Qt and the JSON-RPC code to warn the user
    SetMiscWarning(strWarning);
    // trigger GUI update
    uiInterface.NotifyAlertChanged(SerializeHash(strWarning), CT_NEW);
    // trigger cmd-line notification
    CAlert::Notify(strWarning);

    fWarned = true;
}

void CGoldminenodeMan::NotifyGoldminenodeUpdates(CConnman& connman)
{
}
