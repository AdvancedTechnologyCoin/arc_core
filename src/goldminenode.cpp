// Copyright (c) 2015-2017 The Arctic Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activegoldminenode.h"
#include "consensus/validation.h"
#include "spysend.h"
#include "init.h"
#include "governance.h"
#include "goldminenode.h"
#include "goldminenode-payments.h"
#include "goldminenode-sync.h"
#include "goldminenodeman.h"
#include "util.h"

#include <boost/lexical_cast.hpp>


CGoldminenode::CGoldminenode() :
    vin(),
    addr(),
    pubKeyCollateralAddress(),
    pubKeyGoldminenode(),
    lastPing(),
    vchSig(),
    sigTime(GetAdjustedTime()),
    nLastDsq(0),
    nTimeLastChecked(0),
    nTimeLastPaid(0),
    nTimeLastWatchdogVote(0),
    nActiveState(GOLDMINENODE_ENABLED),
    nCacheCollateralBlock(0),
    nBlockLastPaid(0),
    nProtocolVersion(PROTOCOL_VERSION),
    nPoSeBanScore(0),
    nPoSeBanHeight(0),
    fAllowMixingTx(true),
    fUnitTest(false)
{}

CGoldminenode::CGoldminenode(CService addrNew, CTxIn vinNew, CPubKey pubKeyCollateralAddressNew, CPubKey pubKeyGoldminenodeNew, int nProtocolVersionIn) :
    vin(vinNew),
    addr(addrNew),
    pubKeyCollateralAddress(pubKeyCollateralAddressNew),
    pubKeyGoldminenode(pubKeyGoldminenodeNew),
    lastPing(),
    vchSig(),
    sigTime(GetAdjustedTime()),
    nLastDsq(0),
    nTimeLastChecked(0),
    nTimeLastPaid(0),
    nTimeLastWatchdogVote(0),
    nActiveState(GOLDMINENODE_ENABLED),
    nCacheCollateralBlock(0),
    nBlockLastPaid(0),
    nProtocolVersion(nProtocolVersionIn),
    nPoSeBanScore(0),
    nPoSeBanHeight(0),
    fAllowMixingTx(true),
    fUnitTest(false)
{}

CGoldminenode::CGoldminenode(const CGoldminenode& other) :
    vin(other.vin),
    addr(other.addr),
    pubKeyCollateralAddress(other.pubKeyCollateralAddress),
    pubKeyGoldminenode(other.pubKeyGoldminenode),
    lastPing(other.lastPing),
    vchSig(other.vchSig),
    sigTime(other.sigTime),
    nLastDsq(other.nLastDsq),
    nTimeLastChecked(other.nTimeLastChecked),
    nTimeLastPaid(other.nTimeLastPaid),
    nTimeLastWatchdogVote(other.nTimeLastWatchdogVote),
    nActiveState(other.nActiveState),
    nCacheCollateralBlock(other.nCacheCollateralBlock),
    nBlockLastPaid(other.nBlockLastPaid),
    nProtocolVersion(other.nProtocolVersion),
    nPoSeBanScore(other.nPoSeBanScore),
    nPoSeBanHeight(other.nPoSeBanHeight),
    fAllowMixingTx(other.fAllowMixingTx),
    fUnitTest(other.fUnitTest)
{}

CGoldminenode::CGoldminenode(const CGoldminenodeBroadcast& mnb) :
    vin(mnb.vin),
    addr(mnb.addr),
    pubKeyCollateralAddress(mnb.pubKeyCollateralAddress),
    pubKeyGoldminenode(mnb.pubKeyGoldminenode),
    lastPing(mnb.lastPing),
    vchSig(mnb.vchSig),
    sigTime(mnb.sigTime),
    nLastDsq(0),
    nTimeLastChecked(0),
    nTimeLastPaid(0),
    nTimeLastWatchdogVote(mnb.sigTime),
    nActiveState(mnb.nActiveState),
    nCacheCollateralBlock(0),
    nBlockLastPaid(0),
    nProtocolVersion(mnb.nProtocolVersion),
    nPoSeBanScore(0),
    nPoSeBanHeight(0),
    fAllowMixingTx(true),
    fUnitTest(false)
{}

//
// When a new goldminenode broadcast is sent, update our information
//
bool CGoldminenode::UpdateFromNewBroadcast(CGoldminenodeBroadcast& mnb)
{
    if(mnb.sigTime <= sigTime && !mnb.fRecovery) return false;

    pubKeyGoldminenode = mnb.pubKeyGoldminenode;
    sigTime = mnb.sigTime;
    vchSig = mnb.vchSig;
    nProtocolVersion = mnb.nProtocolVersion;
    addr = mnb.addr;
    nPoSeBanScore = 0;
    nPoSeBanHeight = 0;
    nTimeLastChecked = 0;
    int nDos = 0;
    if(mnb.lastPing == CGoldminenodePing() || (mnb.lastPing != CGoldminenodePing() && mnb.lastPing.CheckAndUpdate(this, true, nDos))) {
        lastPing = mnb.lastPing;
        mnodeman.mapSeenGoldminenodePing.insert(std::make_pair(lastPing.GetHash(), lastPing));
    }
    // if it matches our Goldminenode privkey...
    if(fGoldmineNode && pubKeyGoldminenode == activeGoldminenode.pubKeyGoldminenode) {
        nPoSeBanScore = -GOLDMINENODE_POSE_BAN_MAX_SCORE;
        if(nProtocolVersion == PROTOCOL_VERSION) {
            // ... and PROTOCOL_VERSION, then we've been remotely activated ...
            activeGoldminenode.ManageState();
        } else {
            // ... otherwise we need to reactivate our node, do not add it to the list and do not relay
            // but also do not ban the node we get this message from
            LogPrintf("CGoldminenode::UpdateFromNewBroadcast -- wrong PROTOCOL_VERSION, re-activate your MN: message nProtocolVersion=%d  PROTOCOL_VERSION=%d\n", nProtocolVersion, PROTOCOL_VERSION);
            return false;
        }
    }
    return true;
}

//
// Deterministically calculate a given "score" for a Goldminenode depending on how close it's hash is to
// the proof of work for that block. The further away they are the better, the furthest will win the election
// and get paid this block
//
arith_uint256 CGoldminenode::CalculateScore(const uint256& blockHash)
{
    uint256 aux = ArithToUint256(UintToArith256(vin.prevout.hash) + vin.prevout.n);

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << blockHash;
    arith_uint256 hash2 = UintToArith256(ss.GetHash());

    CHashWriter ss2(SER_GETHASH, PROTOCOL_VERSION);
    ss2 << blockHash;
    ss2 << aux;
    arith_uint256 hash3 = UintToArith256(ss2.GetHash());

    return (hash3 > hash2 ? hash3 - hash2 : hash2 - hash3);
}

void CGoldminenode::Check(bool fForce)
{
    LOCK(cs);

    if(ShutdownRequested()) return;

    if(!fForce && (GetTime() - nTimeLastChecked < GOLDMINENODE_CHECK_SECONDS)) return;
    nTimeLastChecked = GetTime();

    LogPrint("goldminenode", "CGoldminenode::Check -- Goldminenode %s is in %s state\n", vin.prevout.ToStringShort(), GetStateString());

    //once spent, stop doing the checks
    if(IsOutpointSpent()) return;

    int nHeight = 0;
    if(!fUnitTest) {
        TRY_LOCK(cs_main, lockMain);
        if(!lockMain) return;

        CCoins coins;
        if(!pcoinsTip->GetCoins(vin.prevout.hash, coins) ||
           (unsigned int)vin.prevout.n>=coins.vout.size() ||
           coins.vout[vin.prevout.n].IsNull()) {
            nActiveState = GOLDMINENODE_OUTPOINT_SPENT;
            LogPrint("goldminenode", "CGoldminenode::Check -- Failed to find Goldminenode UTXO, goldminenode=%s\n", vin.prevout.ToStringShort());
            return;
        }

        nHeight = chainActive.Height();
    }

    if(IsPoSeBanned()) {
        if(nHeight < nPoSeBanHeight) return; // too early?
        // Otherwise give it a chance to proceed further to do all the usual checks and to change its state.
        // Goldminenode still will be on the edge and can be banned back easily if it keeps ignoring mnverify
        // or connect attempts. Will require few mnverify messages to strengthen its position in mn list.
        LogPrintf("CGoldminenode::Check -- Goldminenode %s is unbanned and back in list now\n", vin.prevout.ToStringShort());
        DecreasePoSeBanScore();
    } else if(nPoSeBanScore >= GOLDMINENODE_POSE_BAN_MAX_SCORE) {
        nActiveState = GOLDMINENODE_POSE_BAN;
        // ban for the whole payment cycle
        nPoSeBanHeight = nHeight + mnodeman.size();
        LogPrintf("CGoldminenode::Check -- Goldminenode %s is banned till block %d now\n", vin.prevout.ToStringShort(), nPoSeBanHeight);
        return;
    }

    int nActiveStatePrev = nActiveState;
    bool fOurGoldminenode = fGoldmineNode && activeGoldminenode.pubKeyGoldminenode == pubKeyGoldminenode;

                   // goldminenode doesn't meet payment protocol requirements ...
    bool fRequireUpdate = nProtocolVersion < mnpayments.GetMinGoldminenodePaymentsProto() ||
                   // or it's our own node and we just updated it to the new protocol but we are still waiting for activation ...
                   (fOurGoldminenode && nProtocolVersion < PROTOCOL_VERSION);

    if(fRequireUpdate) {
        nActiveState = GOLDMINENODE_UPDATE_REQUIRED;
        if(nActiveStatePrev != nActiveState) {
            LogPrint("goldminenode", "CGoldminenode::Check -- Goldminenode %s is in %s state now\n", vin.prevout.ToStringShort(), GetStateString());
        }
        return;
    }

    // keep old goldminenodes on start, give them a chance to receive updates...
    bool fWaitForPing = !goldminenodeSync.IsGoldminenodeListSynced() && !IsPingedWithin(GOLDMINENODE_MIN_MNP_SECONDS);

    //
    // REMOVE AFTER MIGRATION TO 12.1
    //
    // Old nodes don't send pings on dseg, so they could switch to one of the expired states
    // if we were offline for too long even if they are actually enabled for the rest
    // of the network. Postpone their check for GOLDMINENODE_MIN_MNP_SECONDS seconds.
    // This could be usefull for 12.1 migration, can be removed after it's done.
    static int64_t nTimeStart = GetTime();
    if(nProtocolVersion < 70204) {
        if(!goldminenodeSync.IsGoldminenodeListSynced()) nTimeStart = GetTime();
        fWaitForPing = GetTime() - nTimeStart < GOLDMINENODE_MIN_MNP_SECONDS;
    }
    //
    // END REMOVE
    //

    if(fWaitForPing && !fOurGoldminenode) {
        // ...but if it was already expired before the initial check - return right away
        if(IsExpired() || IsWatchdogExpired() || IsNewStartRequired()) {
            LogPrint("goldminenode", "CGoldminenode::Check -- Goldminenode %s is in %s state, waiting for ping\n", vin.prevout.ToStringShort(), GetStateString());
            return;
        }
    }

    // don't expire if we are still in "waiting for ping" mode unless it's our own goldminenode
    if(!fWaitForPing || fOurGoldminenode) {

        if(!IsPingedWithin(GOLDMINENODE_NEW_START_REQUIRED_SECONDS)) {
            nActiveState = GOLDMINENODE_NEW_START_REQUIRED;
            if(nActiveStatePrev != nActiveState) {
                LogPrint("goldminenode", "CGoldminenode::Check -- Goldminenode %s is in %s state now\n", vin.prevout.ToStringShort(), GetStateString());
            }
            return;
        }

        bool fWatchdogActive = goldminenodeSync.IsSynced() && mnodeman.IsWatchdogActive();
        bool fWatchdogExpired = (fWatchdogActive && ((GetTime() - nTimeLastWatchdogVote) > GOLDMINENODE_WATCHDOG_MAX_SECONDS));

        LogPrint("goldminenode", "CGoldminenode::Check -- outpoint=%s, nTimeLastWatchdogVote=%d, GetTime()=%d, fWatchdogExpired=%d\n",
                vin.prevout.ToStringShort(), nTimeLastWatchdogVote, GetTime(), fWatchdogExpired);

        if(fWatchdogExpired) {
            nActiveState = GOLDMINENODE_WATCHDOG_EXPIRED;
            if(nActiveStatePrev != nActiveState) {
                LogPrint("goldminenode", "CGoldminenode::Check -- Goldminenode %s is in %s state now\n", vin.prevout.ToStringShort(), GetStateString());
            }
            return;
        }

        if(!IsPingedWithin(GOLDMINENODE_EXPIRATION_SECONDS)) {
            nActiveState = GOLDMINENODE_EXPIRED;
            if(nActiveStatePrev != nActiveState) {
                LogPrint("goldminenode", "CGoldminenode::Check -- Goldminenode %s is in %s state now\n", vin.prevout.ToStringShort(), GetStateString());
            }
            return;
        }
    }

    if(lastPing.sigTime - sigTime < GOLDMINENODE_MIN_MNP_SECONDS) {
        nActiveState = GOLDMINENODE_PRE_ENABLED;
        if(nActiveStatePrev != nActiveState) {
            LogPrint("goldminenode", "CGoldminenode::Check -- Goldminenode %s is in %s state now\n", vin.prevout.ToStringShort(), GetStateString());
        }
        return;
    }

    nActiveState = GOLDMINENODE_ENABLED; // OK
    if(nActiveStatePrev != nActiveState) {
        LogPrint("goldminenode", "CGoldminenode::Check -- Goldminenode %s is in %s state now\n", vin.prevout.ToStringShort(), GetStateString());
    }
}

bool CGoldminenode::IsValidNetAddr()
{
    return IsValidNetAddr(addr);
}

bool CGoldminenode::IsValidNetAddr(CService addrIn)
{
    // TODO: regtest is fine with any addresses for now,
    // should probably be a bit smarter if one day we start to implement tests for this
    return Params().NetworkIDString() == CBaseChainParams::REGTEST ||
            (addrIn.IsIPv4() && IsReachable(addrIn) && addrIn.IsRoutable());
}

goldminenode_info_t CGoldminenode::GetInfo()
{
    goldminenode_info_t info;
    info.vin = vin;
    info.addr = addr;
    info.pubKeyCollateralAddress = pubKeyCollateralAddress;
    info.pubKeyGoldminenode = pubKeyGoldminenode;
    info.sigTime = sigTime;
    info.nLastDsq = nLastDsq;
    info.nTimeLastChecked = nTimeLastChecked;
    info.nTimeLastPaid = nTimeLastPaid;
    info.nTimeLastWatchdogVote = nTimeLastWatchdogVote;
    info.nTimeLastPing = lastPing.sigTime;
    info.nActiveState = nActiveState;
    info.nProtocolVersion = nProtocolVersion;
    info.fInfoValid = true;
    return info;
}

std::string CGoldminenode::StateToString(int nStateIn)
{
    switch(nStateIn) {
        case GOLDMINENODE_PRE_ENABLED:            return "PRE_ENABLED";
        case GOLDMINENODE_ENABLED:                return "ENABLED";
        case GOLDMINENODE_EXPIRED:                return "EXPIRED";
        case GOLDMINENODE_OUTPOINT_SPENT:         return "OUTPOINT_SPENT";
        case GOLDMINENODE_UPDATE_REQUIRED:        return "UPDATE_REQUIRED";
        case GOLDMINENODE_WATCHDOG_EXPIRED:       return "WATCHDOG_EXPIRED";
        case GOLDMINENODE_NEW_START_REQUIRED:     return "NEW_START_REQUIRED";
        case GOLDMINENODE_POSE_BAN:               return "POSE_BAN";
        default:                                return "UNKNOWN";
    }
}

std::string CGoldminenode::GetStateString() const
{
    return StateToString(nActiveState);
}

std::string CGoldminenode::GetStatus() const
{
    // TODO: return smth a bit more human readable here
    return GetStateString();
}

int CGoldminenode::GetCollateralAge()
{
    int nHeight;
    {
        TRY_LOCK(cs_main, lockMain);
        if(!lockMain || !chainActive.Tip()) return -1;
        nHeight = chainActive.Height();
    }

    if (nCacheCollateralBlock == 0) {
        int nInputAge = GetInputAge(vin);
        if(nInputAge > 0) {
            nCacheCollateralBlock = nHeight - nInputAge;
        } else {
            return nInputAge;
        }
    }

    return nHeight - nCacheCollateralBlock;
}

void CGoldminenode::UpdateLastPaid(const CBlockIndex *pindex, int nMaxBlocksToScanBack)
{
    if(!pindex) return;

    const CBlockIndex *BlockReading = pindex;

    CScript mnpayee = GetScriptForDestination(pubKeyCollateralAddress.GetID());
    // LogPrint("goldminenode", "CGoldminenode::UpdateLastPaidBlock -- searching for block with payment to %s\n", vin.prevout.ToStringShort());

    LOCK(cs_mapGoldminenodeBlocks);

    for (int i = 0; BlockReading && BlockReading->nHeight > nBlockLastPaid && i < nMaxBlocksToScanBack; i++) {
        if(mnpayments.mapGoldminenodeBlocks.count(BlockReading->nHeight) &&
            mnpayments.mapGoldminenodeBlocks[BlockReading->nHeight].HasPayeeWithVotes(mnpayee, 2))
        {
            CBlock block;
            if(!ReadBlockFromDisk(block, BlockReading, Params().GetConsensus())) // shouldn't really happen
                continue;

            CAmount nGoldminenodePayment = GetGoldminenodePayment(BlockReading->nHeight, block.vtx[0].GetValueOut());

            BOOST_FOREACH(CTxOut txout, block.vtx[0].vout)
                if(mnpayee == txout.scriptPubKey && nGoldminenodePayment == txout.nValue) {
                    nBlockLastPaid = BlockReading->nHeight;
                    nTimeLastPaid = BlockReading->nTime;
                    LogPrint("goldminenode", "CGoldminenode::UpdateLastPaidBlock -- searching for block with payment to %s -- found new %d\n", vin.prevout.ToStringShort(), nBlockLastPaid);
                    return;
                }
        }

        if (BlockReading->pprev == NULL) { assert(BlockReading); break; }
        BlockReading = BlockReading->pprev;
    }

    // Last payment for this goldminenode wasn't found in latest mnpayments blocks
    // or it was found in mnpayments blocks but wasn't found in the blockchain.
    // LogPrint("goldminenode", "CGoldminenode::UpdateLastPaidBlock -- searching for block with payment to %s -- keeping old %d\n", vin.prevout.ToStringShort(), nBlockLastPaid);
}

bool CGoldminenodeBroadcast::Create(std::string strService, std::string strKeyGoldminenode, std::string strTxHash, std::string strOutputIndex, std::string& strErrorRet, CGoldminenodeBroadcast &mnbRet, bool fOffline)
{
    CTxIn txin;
    CPubKey pubKeyCollateralAddressNew;
    CKey keyCollateralAddressNew;
    CPubKey pubKeyGoldminenodeNew;
    CKey keyGoldminenodeNew;

    //need correct blocks to send ping
    if(!fOffline && !goldminenodeSync.IsBlockchainSynced()) {
        strErrorRet = "Sync in progress. Must wait until sync is complete to start Goldminenode";
        LogPrintf("CGoldminenodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    if(!darkSendSigner.GetKeysFromSecret(strKeyGoldminenode, keyGoldminenodeNew, pubKeyGoldminenodeNew)) {
        strErrorRet = strprintf("Invalid goldminenode key %s", strKeyGoldminenode);
        LogPrintf("CGoldminenodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    if(!pwalletMain->GetGoldminenodeVinAndKeys(txin, pubKeyCollateralAddressNew, keyCollateralAddressNew, strTxHash, strOutputIndex)) {
        strErrorRet = strprintf("Could not allocate txin %s:%s for goldminenode %s", strTxHash, strOutputIndex, strService);
        LogPrintf("CGoldminenodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    CService service = CService(strService);
    int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
    if(Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if(service.GetPort() != mainnetDefaultPort) {
            strErrorRet = strprintf("Invalid port %u for goldminenode %s, only %d is supported on mainnet.", service.GetPort(), strService, mainnetDefaultPort);
            LogPrintf("CGoldminenodeBroadcast::Create -- %s\n", strErrorRet);
            return false;
        }
    } else if (service.GetPort() == mainnetDefaultPort) {
        strErrorRet = strprintf("Invalid port %u for goldminenode %s, %d is the only supported on mainnet.", service.GetPort(), strService, mainnetDefaultPort);
        LogPrintf("CGoldminenodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    return Create(txin, CService(strService), keyCollateralAddressNew, pubKeyCollateralAddressNew, keyGoldminenodeNew, pubKeyGoldminenodeNew, strErrorRet, mnbRet);
}

bool CGoldminenodeBroadcast::Create(CTxIn txin, CService service, CKey keyCollateralAddressNew, CPubKey pubKeyCollateralAddressNew, CKey keyGoldminenodeNew, CPubKey pubKeyGoldminenodeNew, std::string &strErrorRet, CGoldminenodeBroadcast &mnbRet)
{
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    LogPrint("goldminenode", "CGoldminenodeBroadcast::Create -- pubKeyCollateralAddressNew = %s, pubKeyGoldminenodeNew.GetID() = %s\n",
             CBitcoinAddress(pubKeyCollateralAddressNew.GetID()).ToString(),
             pubKeyGoldminenodeNew.GetID().ToString());


    CGoldminenodePing mnp(txin);
    if(!mnp.Sign(keyGoldminenodeNew, pubKeyGoldminenodeNew)) {
        strErrorRet = strprintf("Failed to sign ping, goldminenode=%s", txin.prevout.ToStringShort());
        LogPrintf("CGoldminenodeBroadcast::Create -- %s\n", strErrorRet);
        mnbRet = CGoldminenodeBroadcast();
        return false;
    }

    mnbRet = CGoldminenodeBroadcast(service, txin, pubKeyCollateralAddressNew, pubKeyGoldminenodeNew, PROTOCOL_VERSION);

    if(!mnbRet.IsValidNetAddr()) {
        strErrorRet = strprintf("Invalid IP address, goldminenode=%s", txin.prevout.ToStringShort());
        LogPrintf("CGoldminenodeBroadcast::Create -- %s\n", strErrorRet);
        mnbRet = CGoldminenodeBroadcast();
        return false;
    }

    mnbRet.lastPing = mnp;
    if(!mnbRet.Sign(keyCollateralAddressNew)) {
        strErrorRet = strprintf("Failed to sign broadcast, goldminenode=%s", txin.prevout.ToStringShort());
        LogPrintf("CGoldminenodeBroadcast::Create -- %s\n", strErrorRet);
        mnbRet = CGoldminenodeBroadcast();
        return false;
    }

    return true;
}

bool CGoldminenodeBroadcast::SimpleCheck(int& nDos)
{
    nDos = 0;

    // make sure addr is valid
    if(!IsValidNetAddr()) {
        LogPrintf("CGoldminenodeBroadcast::SimpleCheck -- Invalid addr, rejected: goldminenode=%s  addr=%s\n",
                    vin.prevout.ToStringShort(), addr.ToString());
        return false;
    }

    // make sure signature isn't in the future (past is OK)
    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrintf("CGoldminenodeBroadcast::SimpleCheck -- Signature rejected, too far into the future: goldminenode=%s\n", vin.prevout.ToStringShort());
        nDos = 1;
        return false;
    }

    // empty ping or incorrect sigTime/unknown blockhash
    if(lastPing == CGoldminenodePing() || !lastPing.SimpleCheck(nDos)) {
        // one of us is probably forked or smth, just mark it as expired and check the rest of the rules
        nActiveState = GOLDMINENODE_EXPIRED;
    }

    if(nProtocolVersion < mnpayments.GetMinGoldminenodePaymentsProto()) {
        LogPrintf("CGoldminenodeBroadcast::SimpleCheck -- ignoring outdated Goldminenode: goldminenode=%s  nProtocolVersion=%d\n", vin.prevout.ToStringShort(), nProtocolVersion);
        return false;
    }

    CScript pubkeyScript;
    pubkeyScript = GetScriptForDestination(pubKeyCollateralAddress.GetID());

    if(pubkeyScript.size() != 25) {
        LogPrintf("CGoldminenodeBroadcast::SimpleCheck -- pubKeyCollateralAddress has the wrong size\n");
        nDos = 100;
        return false;
    }

    CScript pubkeyScript2;
    pubkeyScript2 = GetScriptForDestination(pubKeyGoldminenode.GetID());

    if(pubkeyScript2.size() != 25) {
        LogPrintf("CGoldminenodeBroadcast::SimpleCheck -- pubKeyGoldminenode has the wrong size\n");
        nDos = 100;
        return false;
    }

    if(!vin.scriptSig.empty()) {
        LogPrintf("CGoldminenodeBroadcast::SimpleCheck -- Ignore Not Empty ScriptSig %s\n",vin.ToString());
        nDos = 100;
        return false;
    }

    int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
    if(Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if(addr.GetPort() != mainnetDefaultPort) return false;
    } else if(addr.GetPort() == mainnetDefaultPort) return false;

    return true;
}

bool CGoldminenodeBroadcast::Update(CGoldminenode* pmn, int& nDos)
{
    nDos = 0;

    if(pmn->sigTime == sigTime && !fRecovery) {
        // mapSeenGoldminenodeBroadcast in CGoldminenodeMan::CheckMnbAndUpdateGoldminenodeList should filter legit duplicates
        // but this still can happen if we just started, which is ok, just do nothing here.
        return false;
    }

    // this broadcast is older than the one that we already have - it's bad and should never happen
    // unless someone is doing something fishy
    if(pmn->sigTime > sigTime) {
        LogPrintf("CGoldminenodeBroadcast::Update -- Bad sigTime %d (existing broadcast is at %d) for Goldminenode %s %s\n",
                      sigTime, pmn->sigTime, vin.prevout.ToStringShort(), addr.ToString());
        return false;
    }

    pmn->Check();

    // goldminenode is banned by PoSe
    if(pmn->IsPoSeBanned()) {
        LogPrintf("CGoldminenodeBroadcast::Update -- Banned by PoSe, goldminenode=%s\n", vin.prevout.ToStringShort());
        return false;
    }

    // IsVnAssociatedWithPubkey is validated once in CheckOutpoint, after that they just need to match
    if(pmn->pubKeyCollateralAddress != pubKeyCollateralAddress) {
        LogPrintf("CGoldminenodeBroadcast::Update -- Got mismatched pubKeyCollateralAddress and vin\n");
        nDos = 33;
        return false;
    }

    if (!CheckSignature(nDos)) {
        LogPrintf("CGoldminenodeBroadcast::Update -- CheckSignature() failed, goldminenode=%s\n", vin.prevout.ToStringShort());
        return false;
    }

    // if ther was no goldminenode broadcast recently or if it matches our Goldminenode privkey...
    if(!pmn->IsBroadcastedWithin(GOLDMINENODE_MIN_MNB_SECONDS) || (fGoldmineNode && pubKeyGoldminenode == activeGoldminenode.pubKeyGoldminenode)) {
        // take the newest entry
        LogPrintf("CGoldminenodeBroadcast::Update -- Got UPDATED Goldminenode entry: addr=%s\n", addr.ToString());
        if(pmn->UpdateFromNewBroadcast((*this))) {
            pmn->Check();
            Relay();
        }
        goldminenodeSync.AddedGoldminenodeList();
    }

    return true;
}

bool CGoldminenodeBroadcast::CheckOutpoint(int& nDos)
{
    // we are a goldminenode with the same vin (i.e. already activated) and this mnb is ours (matches our Goldminenode privkey)
    // so nothing to do here for us
    if(fGoldmineNode && vin.prevout == activeGoldminenode.vin.prevout && pubKeyGoldminenode == activeGoldminenode.pubKeyGoldminenode) {
        return false;
    }

    if (!CheckSignature(nDos)) {
        LogPrintf("CGoldminenodeBroadcast::CheckOutpoint -- CheckSignature() failed, goldminenode=%s\n", vin.prevout.ToStringShort());
        return false;
    }

    {
        TRY_LOCK(cs_main, lockMain);
        if(!lockMain) {
            // not mnb fault, let it to be checked again later
            LogPrint("goldminenode", "CGoldminenodeBroadcast::CheckOutpoint -- Failed to aquire lock, addr=%s", addr.ToString());
            mnodeman.mapSeenGoldminenodeBroadcast.erase(GetHash());
            return false;
        }

        CCoins coins;
        if(!pcoinsTip->GetCoins(vin.prevout.hash, coins) ||
           (unsigned int)vin.prevout.n>=coins.vout.size() ||
           coins.vout[vin.prevout.n].IsNull()) {
            LogPrint("goldminenode", "CGoldminenodeBroadcast::CheckOutpoint -- Failed to find Goldminenode UTXO, goldminenode=%s\n", vin.prevout.ToStringShort());
            return false;
        }
        if(coins.vout[vin.prevout.n].nValue != 1000 * COIN) {
            LogPrint("goldminenode", "CGoldminenodeBroadcast::CheckOutpoint -- Goldminenode UTXO should have 1000 ARC, goldminenode=%s\n", vin.prevout.ToStringShort());
            return false;
        }
        if(chainActive.Height() - coins.nHeight + 1 < Params().GetConsensus().nGoldminenodeMinimumConfirmations) {
            LogPrintf("CGoldminenodeBroadcast::CheckOutpoint -- Goldminenode UTXO must have at least %d confirmations, goldminenode=%s\n",
                    Params().GetConsensus().nGoldminenodeMinimumConfirmations, vin.prevout.ToStringShort());
            // maybe we miss few blocks, let this mnb to be checked again later
            mnodeman.mapSeenGoldminenodeBroadcast.erase(GetHash());
            return false;
        }
    }

    LogPrint("goldminenode", "CGoldminenodeBroadcast::CheckOutpoint -- Goldminenode UTXO verified\n");

    // make sure the vout that was signed is related to the transaction that spawned the Goldminenode
    //  - this is expensive, so it's only done once per Goldminenode
    if(!darkSendSigner.IsVinAssociatedWithPubkey(vin, pubKeyCollateralAddress)) {
        LogPrintf("CGoldminenodeMan::CheckOutpoint -- Got mismatched pubKeyCollateralAddress and vin\n");
        nDos = 33;
        return false;
    }

    // verify that sig time is legit in past
    // should be at least not earlier than block when 1000 ARC tx got nGoldminenodeMinimumConfirmations
    uint256 hashBlock = uint256();
    CTransaction tx2;
    GetTransaction(vin.prevout.hash, tx2, Params().GetConsensus(), hashBlock, true);
    {
        LOCK(cs_main);
        BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
        if (mi != mapBlockIndex.end() && (*mi).second) {
            CBlockIndex* pMNIndex = (*mi).second; // block for 1000 ARC tx -> 1 confirmation
            CBlockIndex* pConfIndex = chainActive[pMNIndex->nHeight + Params().GetConsensus().nGoldminenodeMinimumConfirmations - 1]; // block where tx got nGoldminenodeMinimumConfirmations
            if(pConfIndex->GetBlockTime() > sigTime) {
                LogPrintf("CGoldminenodeBroadcast::CheckOutpoint -- Bad sigTime %d (%d conf block is at %d) for Goldminenode %s %s\n",
                          sigTime, Params().GetConsensus().nGoldminenodeMinimumConfirmations, pConfIndex->GetBlockTime(), vin.prevout.ToStringShort(), addr.ToString());
                return false;
            }
        }
    }

    return true;
}

bool CGoldminenodeBroadcast::Sign(CKey& keyCollateralAddress)
{
    std::string strError;
    std::string strMessage;

    sigTime = GetAdjustedTime();

    strMessage = addr.ToString(false) + boost::lexical_cast<std::string>(sigTime) +
                    pubKeyCollateralAddress.GetID().ToString() + pubKeyGoldminenode.GetID().ToString() +
                    boost::lexical_cast<std::string>(nProtocolVersion);

    if(!darkSendSigner.SignMessage(strMessage, vchSig, keyCollateralAddress)) {
        LogPrintf("CGoldminenodeBroadcast::Sign -- SignMessage() failed\n");
        return false;
    }

    if(!darkSendSigner.VerifyMessage(pubKeyCollateralAddress, vchSig, strMessage, strError)) {
        LogPrintf("CGoldminenodeBroadcast::Sign -- VerifyMessage() failed, error: %s\n", strError);
        return false;
    }

    return true;
}

bool CGoldminenodeBroadcast::CheckSignature(int& nDos)
{
    std::string strMessage;
    std::string strError = "";
    nDos = 0;

    //
    // REMOVE AFTER MIGRATION TO 12.1
    //
    if(nProtocolVersion < 70201) {
        std::string vchPubkeyCollateralAddress(pubKeyCollateralAddress.begin(), pubKeyCollateralAddress.end());
        std::string vchPubkeyGoldminenode(pubKeyGoldminenode.begin(), pubKeyGoldminenode.end());
        strMessage = addr.ToString(false) + boost::lexical_cast<std::string>(sigTime) +
                        vchPubkeyCollateralAddress + vchPubkeyGoldminenode + boost::lexical_cast<std::string>(nProtocolVersion);

        LogPrint("goldminenode", "CGoldminenodeBroadcast::CheckSignature -- sanitized strMessage: %s  pubKeyCollateralAddress address: %s  sig: %s\n",
            SanitizeString(strMessage), CBitcoinAddress(pubKeyCollateralAddress.GetID()).ToString(),
            EncodeBase64(&vchSig[0], vchSig.size()));

        if(!darkSendSigner.VerifyMessage(pubKeyCollateralAddress, vchSig, strMessage, strError)) {
            if(addr.ToString() != addr.ToString(false)) {
                // maybe it's wrong format, try again with the old one
                strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) +
                                vchPubkeyCollateralAddress + vchPubkeyGoldminenode + boost::lexical_cast<std::string>(nProtocolVersion);

                LogPrint("goldminenode", "CGoldminenodeBroadcast::CheckSignature -- second try, sanitized strMessage: %s  pubKeyCollateralAddress address: %s  sig: %s\n",
                    SanitizeString(strMessage), CBitcoinAddress(pubKeyCollateralAddress.GetID()).ToString(),
                    EncodeBase64(&vchSig[0], vchSig.size()));

                if(!darkSendSigner.VerifyMessage(pubKeyCollateralAddress, vchSig, strMessage, strError)) {
                    // didn't work either
                    LogPrintf("CGoldminenodeBroadcast::CheckSignature -- Got bad Goldminenode announce signature, second try, sanitized error: %s\n",
                        SanitizeString(strError));
                    // don't ban for old goldminenodes, their sigs could be broken because of the bug
                    return false;
                }
            } else {
                // nope, sig is actually wrong
                LogPrintf("CGoldminenodeBroadcast::CheckSignature -- Got bad Goldminenode announce signature, sanitized error: %s\n",
                    SanitizeString(strError));
                // don't ban for old goldminenodes, their sigs could be broken because of the bug
                return false;
            }
        }
    } else {
    //
    // END REMOVE
    //
        strMessage = addr.ToString(false) + boost::lexical_cast<std::string>(sigTime) +
                        pubKeyCollateralAddress.GetID().ToString() + pubKeyGoldminenode.GetID().ToString() +
                        boost::lexical_cast<std::string>(nProtocolVersion);

        LogPrint("goldminenode", "CGoldminenodeBroadcast::CheckSignature -- strMessage: %s  pubKeyCollateralAddress address: %s  sig: %s\n", strMessage, CBitcoinAddress(pubKeyCollateralAddress.GetID()).ToString(), EncodeBase64(&vchSig[0], vchSig.size()));

        if(!darkSendSigner.VerifyMessage(pubKeyCollateralAddress, vchSig, strMessage, strError)){
            LogPrintf("CGoldminenodeBroadcast::CheckSignature -- Got bad Goldminenode announce signature, error: %s\n", strError);
            nDos = 100;
            return false;
        }
    }

    return true;
}

void CGoldminenodeBroadcast::Relay()
{
    CInv inv(MSG_GOLDMINENODE_ANNOUNCE, GetHash());
    RelayInv(inv);
}

CGoldminenodePing::CGoldminenodePing(CTxIn& vinNew)
{
    LOCK(cs_main);
    if (!chainActive.Tip() || chainActive.Height() < 12) return;

    vin = vinNew;
    blockHash = chainActive[chainActive.Height() - 12]->GetBlockHash();
    sigTime = GetAdjustedTime();
    vchSig = std::vector<unsigned char>();
}

bool CGoldminenodePing::Sign(CKey& keyGoldminenode, CPubKey& pubKeyGoldminenode)
{
    std::string strError;
    std::string strGoldmineNodeSignMessage;

    sigTime = GetAdjustedTime();
    std::string strMessage = vin.ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);

    if(!darkSendSigner.SignMessage(strMessage, vchSig, keyGoldminenode)) {
        LogPrintf("CGoldminenodePing::Sign -- SignMessage() failed\n");
        return false;
    }

    if(!darkSendSigner.VerifyMessage(pubKeyGoldminenode, vchSig, strMessage, strError)) {
        LogPrintf("CGoldminenodePing::Sign -- VerifyMessage() failed, error: %s\n", strError);
        return false;
    }

    return true;
}

bool CGoldminenodePing::CheckSignature(CPubKey& pubKeyGoldminenode, int &nDos)
{
    std::string strMessage = vin.ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);
    std::string strError = "";
    nDos = 0;

    if(!darkSendSigner.VerifyMessage(pubKeyGoldminenode, vchSig, strMessage, strError)) {
        LogPrintf("CGoldminenodePing::CheckSignature -- Got bad Goldminenode ping signature, goldminenode=%s, error: %s\n", vin.prevout.ToStringShort(), strError);
        nDos = 33;
        return false;
    }
    return true;
}

bool CGoldminenodePing::SimpleCheck(int& nDos)
{
    // don't ban by default
    nDos = 0;

    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrintf("CGoldminenodePing::SimpleCheck -- Signature rejected, too far into the future, goldminenode=%s\n", vin.prevout.ToStringShort());
        nDos = 1;
        return false;
    }

    {
        LOCK(cs_main);
        BlockMap::iterator mi = mapBlockIndex.find(blockHash);
        if (mi == mapBlockIndex.end()) {
            LogPrint("goldminenode", "CGoldminenodePing::SimpleCheck -- Goldminenode ping is invalid, unknown block hash: goldminenode=%s blockHash=%s\n", vin.prevout.ToStringShort(), blockHash.ToString());
            // maybe we stuck or forked so we shouldn't ban this node, just fail to accept this ping
            // TODO: or should we also request this block?
            return false;
        }
    }
    LogPrint("goldminenode", "CGoldminenodePing::SimpleCheck -- Goldminenode ping verified: goldminenode=%s  blockHash=%s  sigTime=%d\n", vin.prevout.ToStringShort(), blockHash.ToString(), sigTime);
    return true;
}

bool CGoldminenodePing::CheckAndUpdate(CGoldminenode* pmn, bool fFromNewBroadcast, int& nDos)
{
    // don't ban by default
    nDos = 0;

    if (!SimpleCheck(nDos)) {
        return false;
    }

    if (pmn == NULL) {
        LogPrint("goldminenode", "CGoldminenodePing::CheckAndUpdate -- Couldn't find Goldminenode entry, goldminenode=%s\n", vin.prevout.ToStringShort());
        return false;
    }

    if(!fFromNewBroadcast) {
        if (pmn->IsUpdateRequired()) {
            LogPrint("goldminenode", "CGoldminenodePing::CheckAndUpdate -- goldminenode protocol is outdated, goldminenode=%s\n", vin.prevout.ToStringShort());
            return false;
        }

        if (pmn->IsNewStartRequired()) {
            LogPrint("goldminenode", "CGoldminenodePing::CheckAndUpdate -- goldminenode is completely expired, new start is required, goldminenode=%s\n", vin.prevout.ToStringShort());
            return false;
        }
    }

    {
        LOCK(cs_main);
        BlockMap::iterator mi = mapBlockIndex.find(blockHash);
        if ((*mi).second && (*mi).second->nHeight < chainActive.Height() - 24) {
            LogPrintf("CGoldminenodePing::CheckAndUpdate -- Goldminenode ping is invalid, block hash is too old: goldminenode=%s  blockHash=%s\n", vin.prevout.ToStringShort(), blockHash.ToString());
            // nDos = 1;
            return false;
        }
    }

    LogPrint("goldminenode", "CGoldminenodePing::CheckAndUpdate -- New ping: goldminenode=%s  blockHash=%s  sigTime=%d\n", vin.prevout.ToStringShort(), blockHash.ToString(), sigTime);

    // LogPrintf("mnping - Found corresponding mn for vin: %s\n", vin.prevout.ToStringShort());
    // update only if there is no known ping for this goldminenode or
    // last ping was more then GOLDMINENODE_MIN_MNP_SECONDS-60 ago comparing to this one
    if (pmn->IsPingedWithin(GOLDMINENODE_MIN_MNP_SECONDS - 60, sigTime)) {
        LogPrint("goldminenode", "CGoldminenodePing::CheckAndUpdate -- Goldminenode ping arrived too early, goldminenode=%s\n", vin.prevout.ToStringShort());
        //nDos = 1; //disable, this is happening frequently and causing banned peers
        return false;
    }

    if (!CheckSignature(pmn->pubKeyGoldminenode, nDos)) return false;

    // so, ping seems to be ok, let's store it
    LogPrint("goldminenode", "CGoldminenodePing::CheckAndUpdate -- Goldminenode ping accepted, goldminenode=%s\n", vin.prevout.ToStringShort());
    pmn->lastPing = *this;

    // and update mnodeman.mapSeenGoldminenodeBroadcast.lastPing which is probably outdated
    CGoldminenodeBroadcast mnb(*pmn);
    uint256 hash = mnb.GetHash();
    if (mnodeman.mapSeenGoldminenodeBroadcast.count(hash)) {
        mnodeman.mapSeenGoldminenodeBroadcast[hash].second.lastPing = *this;
    }

    pmn->Check(true); // force update, ignoring cache
    if (!pmn->IsEnabled()) return false;

    LogPrint("goldminenode", "CGoldminenodePing::CheckAndUpdate -- Goldminenode ping acceepted and relayed, goldminenode=%s\n", vin.prevout.ToStringShort());
    Relay();

    return true;
}

void CGoldminenodePing::Relay()
{
    CInv inv(MSG_GOLDMINENODE_PING, GetHash());
    RelayInv(inv);
}

void CGoldminenode::AddGovernanceVote(uint256 nGovernanceObjectHash)
{
    if(mapGovernanceObjectsVotedOn.count(nGovernanceObjectHash)) {
        mapGovernanceObjectsVotedOn[nGovernanceObjectHash]++;
    } else {
        mapGovernanceObjectsVotedOn.insert(std::make_pair(nGovernanceObjectHash, 1));
    }
}

void CGoldminenode::RemoveGovernanceObject(uint256 nGovernanceObjectHash)
{
    std::map<uint256, int>::iterator it = mapGovernanceObjectsVotedOn.find(nGovernanceObjectHash);
    if(it == mapGovernanceObjectsVotedOn.end()) {
        return;
    }
    mapGovernanceObjectsVotedOn.erase(it);
}

void CGoldminenode::UpdateWatchdogVoteTime()
{
    LOCK(cs);
    nTimeLastWatchdogVote = GetTime();
}

/**
*   FLAG GOVERNANCE ITEMS AS DIRTY
*
*   - When goldminenode come and go on the network, we must flag the items they voted on to recalc it's cached flags
*
*/
void CGoldminenode::FlagGovernanceItemsAsDirty()
{
    std::vector<uint256> vecDirty;
    {
        std::map<uint256, int>::iterator it = mapGovernanceObjectsVotedOn.begin();
        while(it != mapGovernanceObjectsVotedOn.end()) {
            vecDirty.push_back(it->first);
            ++it;
        }
    }
    for(size_t i = 0; i < vecDirty.size(); ++i) {
        mnodeman.AddDirtyGovernanceObjectHash(vecDirty[i]);
    }
}
