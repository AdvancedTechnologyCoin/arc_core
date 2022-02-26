// Copyright (c) 2017-2022 The Advanced Technology Coin
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activegoldminenode.h"
#include "base58.h"
#include "clientversion.h"
#include "init.h"
#include "netbase.h"
#include "goldminenode.h"
#include "goldminenode-payments.h"
#include "goldminenode-sync.h"
#include "goldminenodeman.h"
#include "messagesigner.h"
#include "script/standard.h"
#include "util.h"
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif // ENABLE_WALLET

#include <boost/lexical_cast.hpp>


CGoldminenode::CGoldminenode() :
    goldminenode_info_t{ GOLDMINENODE_ENABLED, PROTOCOL_VERSION, GetAdjustedTime()},
    fAllowMixingTx(true)
{}

CGoldminenode::CGoldminenode(CService addr, COutPoint outpoint, CPubKey pubKeyCollateralAddress, CPubKey pubKeyGoldminenode, int nProtocolVersionIn) :
    goldminenode_info_t{ GOLDMINENODE_ENABLED, nProtocolVersionIn, GetAdjustedTime(),
                       outpoint, addr, pubKeyCollateralAddress, pubKeyGoldminenode},
    fAllowMixingTx(true)
{}

CGoldminenode::CGoldminenode(const CGoldminenode& other) :
    goldminenode_info_t{other},
    lastPing(other.lastPing),
    vchSig(other.vchSig),
    nCollateralMinConfBlockHash(other.nCollateralMinConfBlockHash),
    nBlockLastPaid(other.nBlockLastPaid),
    nPoSeBanScore(other.nPoSeBanScore),
    nPoSeBanHeight(other.nPoSeBanHeight),
    fAllowMixingTx(other.fAllowMixingTx),
    fUnitTest(other.fUnitTest)
{}

CGoldminenode::CGoldminenode(const CGoldminenodeBroadcast& mnb) :
    goldminenode_info_t{ mnb.nActiveState, mnb.nProtocolVersion, mnb.sigTime,
                       mnb.outpoint, mnb.addr, mnb.pubKeyCollateralAddress, mnb.pubKeyGoldminenode},
    lastPing(mnb.lastPing),
    vchSig(mnb.vchSig),
    fAllowMixingTx(true)
{
	
	enableTime = mnb.enableTime;
}

//
// When a new goldminenode broadcast is sent, update our information
//
bool CGoldminenode::UpdateFromNewBroadcast(CGoldminenodeBroadcast& mnb, CConnman& connman)
{
    if(mnb.sigTime <= sigTime && !mnb.fRecovery) return false;

    pubKeyGoldminenode = mnb.pubKeyGoldminenode;
    sigTime = mnb.sigTime;
	enableTime = mnb.enableTime;
    vchSig = mnb.vchSig;
    nProtocolVersion = mnb.nProtocolVersion;
    addr = mnb.addr;
    nPoSeBanScore = 0;
    nPoSeBanHeight = 0;
    nTimeLastChecked = 0;
    int nDos = 0;
    if(!mnb.lastPing || (mnb.lastPing && mnb.lastPing.CheckAndUpdate(this, true, nDos, connman))) {
        lastPing = mnb.lastPing;
        mnodeman.mapSeenGoldminenodePing.insert(std::make_pair(lastPing.GetHash(), lastPing));
    }
    // if it matches our Goldminenode privkey...
    if(fGoldminenodeMode && pubKeyGoldminenode == activeGoldminenode.pubKeyGoldminenode) {
        nPoSeBanScore = -GOLDMINENODE_POSE_BAN_MAX_SCORE;
        if(nProtocolVersion == PROTOCOL_VERSION) {
            // ... and PROTOCOL_VERSION, then we've been remotely activated ...
            activeGoldminenode.ManageState(connman);
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
arith_uint256 CGoldminenode::CalculateScore(const uint256& blockHash) const
{
    // Deterministically calculate a "score" for a Goldminenode based on any given (block)hash
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << outpoint << nCollateralMinConfBlockHash << blockHash;
    return UintToArith256(ss.GetHash());
}

CGoldminenode::CollateralStatus CGoldminenode::CheckCollateral(const COutPoint& outpoint, const CPubKey& pubkey)
{
    int nHeight;
    return CheckCollateral(outpoint, pubkey, nHeight);
}

CGoldminenode::CollateralStatus CGoldminenode::CheckCollateral(const COutPoint& outpoint, const CPubKey& pubkey, int& nHeightRet)
{
    AssertLockHeld(cs_main);

    Coin coin;
    if(!GetUTXOCoin(outpoint, coin)) {
        return COLLATERAL_UTXO_NOT_FOUND;
    }

    if(coin.out.nValue != 10000 * COIN) {

        return COLLATERAL_INVALID_AMOUNT;
    }
    
    if(pubkey == CPubKey() || coin.out.scriptPubKey != GetScriptForDestination(pubkey.GetID())) {
        return COLLATERAL_INVALID_PUBKEY;
    }
    
    nHeightRet = coin.nHeight;
    return COLLATERAL_OK;
}

void CGoldminenode::Check(bool fForce)
{
    AssertLockHeld(cs_main);
    LOCK(cs);

    if(ShutdownRequested()) return;

    if(!fForce && (GetTime() - nTimeLastChecked < GOLDMINENODE_CHECK_SECONDS)) return;
    nTimeLastChecked = GetTime();

    LogPrint("goldminenode", "CGoldminenode::Check -- Goldminenode %s is in %s state\n", outpoint.ToStringShort(), GetStateString());

    //once spent, stop doing the checks
    if(IsOutpointSpent()) return;

    int nHeight = 0;
    if(!fUnitTest) {
        Coin coin;
        if(!GetUTXOCoin(outpoint, coin)) {
            nActiveState = GOLDMINENODE_OUTPOINT_SPENT;
            LogPrint("goldminenode", "CGoldminenode::Check -- Failed to find Goldminenode UTXO, goldminenode=%s\n", outpoint.ToStringShort());
            return;
        }

        nHeight = chainActive.Height();
    }

    if(IsPoSeBanned()) {
        if(nHeight < nPoSeBanHeight) return; // too early?
        // Otherwise give it a chance to proceed further to do all the usual checks and to change its state.
        // Goldminenode still will be on the edge and can be banned back easily if it keeps ignoring mnverify
        // or connect attempts. Will require few mnverify messages to strengthen its position in mn list.
        LogPrintf("CGoldminenode::Check -- Goldminenode %s is unbanned and back in list now\n", outpoint.ToStringShort());
        DecreasePoSeBanScore();
    } else if(nPoSeBanScore >= GOLDMINENODE_POSE_BAN_MAX_SCORE) {
        nActiveState = GOLDMINENODE_POSE_BAN;
        // ban for the whole payment cycle
        nPoSeBanHeight = nHeight + mnodeman.size();
        LogPrintf("CGoldminenode::Check -- Goldminenode %s is banned till block %d now\n", outpoint.ToStringShort(), nPoSeBanHeight);
        return;
    }

    int nActiveStatePrev = nActiveState;
    bool fOurGoldminenode = fGoldminenodeMode && activeGoldminenode.pubKeyGoldminenode == pubKeyGoldminenode;

                   // goldminenode doesn't meet payment protocol requirements ...
    bool fRequireUpdate = nProtocolVersion < mnpayments.GetMinGoldminenodePaymentsProto() ||
                   // or it's our own node and we just updated it to the new protocol but we are still waiting for activation ...
                   (fOurGoldminenode && nProtocolVersion < PROTOCOL_VERSION);

    if(fRequireUpdate) {
        nActiveState = GOLDMINENODE_UPDATE_REQUIRED;
        if(nActiveStatePrev != nActiveState) {
            LogPrint("goldminenode", "CGoldminenode::Check -- Goldminenode %s is in %s state now\n", outpoint.ToStringShort(), GetStateString());
        }
        return;
    }

    // keep old goldminenodes on start, give them a chance to receive updates...
    bool fWaitForPing = !goldminenodeSync.IsGoldminenodeListSynced() && !IsPingedWithin(GOLDMINENODE_MIN_MNP_SECONDS);

    if(fWaitForPing && !fOurGoldminenode) {
        // ...but if it was already expired before the initial check - return right away
        if(IsExpired() || IsSentinelPingExpired() || IsNewStartRequired()) {
            LogPrint("goldminenode", "CGoldminenode::Check -- Goldminenode %s is in %s state, waiting for ping\n", outpoint.ToStringShort(), GetStateString());
            return;
        }
    }

    // don't expire if we are still in "waiting for ping" mode unless it's our own goldminenode
    if(!fWaitForPing || fOurGoldminenode) {

        if(!IsPingedWithin(GOLDMINENODE_NEW_START_REQUIRED_SECONDS)) {
            nActiveState = GOLDMINENODE_NEW_START_REQUIRED;
            if(nActiveStatePrev != nActiveState) {
                LogPrint("goldminenode", "CGoldminenode::Check -- Goldminenode %s is in %s state now\n", outpoint.ToStringShort(), GetStateString());
            }
            return;
        }

        if(!IsPingedWithin(GOLDMINENODE_EXPIRATION_SECONDS)) {
            nActiveState = GOLDMINENODE_EXPIRED;
            if(nActiveStatePrev != nActiveState) {
                LogPrint("goldminenode", "CGoldminenode::Check -- Goldminenode %s is in %s state now\n", outpoint.ToStringShort(), GetStateString());
            }
            return;
        }

        // part 1: expire based on arcd ping
        bool fSentinelPingActive = goldminenodeSync.IsSynced() && mnodeman.IsSentinelPingActive();
        bool fSentinelPingExpired = fSentinelPingActive && !IsPingedWithin(GOLDMINENODE_SENTINEL_PING_MAX_SECONDS);
        LogPrint("goldminenode", "CGoldminenode::Check -- outpoint=%s, GetAdjustedTime()=%d, fSentinelPingExpired=%d\n",
                outpoint.ToStringShort(), GetAdjustedTime(), fSentinelPingExpired);

        if(fSentinelPingExpired) {
            nActiveState = GOLDMINENODE_SENTINEL_PING_EXPIRED;
            if(nActiveStatePrev != nActiveState) {
                LogPrint("goldminenode", "CGoldminenode::Check -- Goldminenode %s is in %s state now\n", outpoint.ToStringShort(), GetStateString());
            }
            return;
        }
    }

    // We require MNs to be in PRE_ENABLED until they either start to expire or receive a ping and go into ENABLED state
    // Works on mainnet/testnet only and not the case on regtest/devnet.
    if (Params().NetworkIDString() != CBaseChainParams::REGTEST && Params().NetworkIDString() != CBaseChainParams::DEVNET) {
        if (lastPing.sigTime - sigTime < GOLDMINENODE_MIN_MNP_SECONDS) {
            nActiveState = GOLDMINENODE_PRE_ENABLED;
            if (nActiveStatePrev != nActiveState) {
                LogPrint("goldminenode", "CGoldminenode::Check -- Goldminenode %s is in %s state now\n", outpoint.ToStringShort(), GetStateString());
            }
            return;
        }
    }

    if(!fWaitForPing || fOurGoldminenode) {
        // part 2: expire based on sentinel info
        bool fSentinelPingActive = goldminenodeSync.IsSynced() && mnodeman.IsSentinelPingActive();
        bool fSentinelPingExpired = fSentinelPingActive && !lastPing.fSentinelIsCurrent;

        LogPrint("goldminenode", "CGoldminenode::Check -- outpoint=%s, GetAdjustedTime()=%d, fSentinelPingExpired=%d\n",
                outpoint.ToStringShort(), GetAdjustedTime(), fSentinelPingExpired);

        if(fSentinelPingExpired) {
            nActiveState = GOLDMINENODE_SENTINEL_PING_EXPIRED;
            if(nActiveStatePrev != nActiveState) {
                LogPrint("goldminenode", "CGoldminenode::Check -- Goldminenode %s is in %s state now\n", outpoint.ToStringShort(), GetStateString());
            }
            return;
        }
    }

    nActiveState = GOLDMINENODE_ENABLED; // OK
    if(nActiveStatePrev != nActiveState) {
        LogPrint("goldminenode", "CGoldminenode::Check -- Goldminenode %s is in %s state now\n", outpoint.ToStringShort(), GetStateString());
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

goldminenode_info_t CGoldminenode::GetInfo() const
{
    goldminenode_info_t info{*this};
    info.nTimeLastPing = lastPing.sigTime;
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
        case GOLDMINENODE_SENTINEL_PING_EXPIRED:  return "SENTINEL_PING_EXPIRED";
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

void CGoldminenode::UpdateLastPaid(const CBlockIndex *pindex, int nMaxBlocksToScanBack)
{
    if(!pindex) return;

    const CBlockIndex *BlockReading = pindex;

    CScript mnpayee = GetScriptForDestination(pubKeyCollateralAddress.GetID());
    // LogPrint("mnpayments", "CGoldminenode::UpdateLastPaidBlock -- searching for block with payment to %s\n", outpoint.ToStringShort());

    LOCK(cs_mapGoldminenodeBlocks);

    for (int i = 0; BlockReading && BlockReading->nHeight > nBlockLastPaid && i < nMaxBlocksToScanBack; i++) {
        if(mnpayments.mapGoldminenodeBlocks.count(BlockReading->nHeight) 
		//	&& mnpayments.mapGoldminenodeBlocks[BlockReading->nHeight].HasPayeeWithVotes(mnpayee, 2)
		)
        {
            CBlock block;
            if(!ReadBlockFromDisk(block, BlockReading, Params().GetConsensus()))
                continue; // shouldn't really happen

            CAmount nGoldminenodePayment = GetGoldminenodePayment(BlockReading->nHeight, block.vtx[0]->GetValueOut());

            for (const auto& txout : block.vtx[0]->vout)
                if(mnpayee == txout.scriptPubKey && nGoldminenodePayment == txout.nValue) {
                    nBlockLastPaid = BlockReading->nHeight;
                    nTimeLastPaid = BlockReading->nTime;
                    LogPrint("mnpayments", "CGoldminenode::UpdateLastPaidBlock -- searching for block with payment to %s -- found new %d\n", outpoint.ToStringShort(), nBlockLastPaid);
                    return;
                }
        }

        if (BlockReading->pprev == NULL) { assert(BlockReading); break; }
        BlockReading = BlockReading->pprev;
    }

    // Last payment for this goldminenode wasn't found in latest mnpayments blocks
    // or it was found in mnpayments blocks but wasn't found in the blockchain.
    // LogPrint("mnpayments", "CGoldminenode::UpdateLastPaidBlock -- searching for block with payment to %s -- keeping old %d\n", outpoint.ToStringShort(), nBlockLastPaid);
}

#ifdef ENABLE_WALLET
bool CGoldminenodeBroadcast::Create(const std::string& strService, const std::string& strKeyGoldminenode, const std::string& strTxHash, const std::string& strOutputIndex, std::string& strErrorRet, CGoldminenodeBroadcast &mnbRet, bool fOffline)
{
    COutPoint outpoint;
    CPubKey pubKeyCollateralAddressNew;
    CKey keyCollateralAddressNew;
    CPubKey pubKeyGoldminenodeNew;
    CKey keyGoldminenodeNew;

    auto Log = [&strErrorRet](std::string sErr)->bool
    {
        strErrorRet = sErr;
        LogPrintf("CGoldminenodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    };

    // Wait for sync to finish because mnb simply won't be relayed otherwise
    if (!fOffline && !goldminenodeSync.IsSynced())
        return Log("Sync in progress. Must wait until sync is complete to start Goldminenode");

    if (!CMessageSigner::GetKeysFromSecret(strKeyGoldminenode, keyGoldminenodeNew, pubKeyGoldminenodeNew))
        return Log(strprintf("Invalid goldminenode key %s", strKeyGoldminenode));

    if (!pwalletMain->GetGoldminenodeOutpointAndKeys(outpoint, pubKeyCollateralAddressNew, keyCollateralAddressNew, strTxHash, strOutputIndex))
        return Log(strprintf("Could not allocate outpoint %s:%s for goldminenode %s", strTxHash, strOutputIndex, strService));

    CService service;
    if (!Lookup(strService.c_str(), service, 0, false))
        return Log(strprintf("Invalid address %s for goldminenode.", strService));
    int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
    if (Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if (service.GetPort() != mainnetDefaultPort)
            return Log(strprintf("Invalid port %u for goldminenode %s, only %d is supported on mainnet.", service.GetPort(), strService, mainnetDefaultPort));
    } else if (service.GetPort() == mainnetDefaultPort)
        return Log(strprintf("Invalid port %u for goldminenode %s, %d is the only supported on mainnet.", service.GetPort(), strService, mainnetDefaultPort));

    return Create(outpoint, service, keyCollateralAddressNew, pubKeyCollateralAddressNew, keyGoldminenodeNew, pubKeyGoldminenodeNew, strErrorRet, mnbRet);
}

bool CGoldminenodeBroadcast::Create(const COutPoint& outpoint, const CService& service, const CKey& keyCollateralAddressNew, const CPubKey& pubKeyCollateralAddressNew, const CKey& keyGoldminenodeNew, const CPubKey& pubKeyGoldminenodeNew, std::string &strErrorRet, CGoldminenodeBroadcast &mnbRet)
{
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    LogPrint("goldminenode", "CGoldminenodeBroadcast::Create -- pubKeyCollateralAddressNew = %s, pubKeyGoldminenodeNew.GetID() = %s\n",
             CBitcoinAddress(pubKeyCollateralAddressNew.GetID()).ToString(),
             pubKeyGoldminenodeNew.GetID().ToString());

    auto Log = [&strErrorRet,&mnbRet](std::string sErr)->bool
    {
        strErrorRet = sErr;
        LogPrintf("CGoldminenodeBroadcast::Create -- %s\n", strErrorRet);
        mnbRet = CGoldminenodeBroadcast();
        return false;
    };

    CGoldminenodePing mnp(outpoint);
    if (!mnp.Sign(keyGoldminenodeNew, pubKeyGoldminenodeNew))
        return Log(strprintf("Failed to sign ping, goldminenode=%s", outpoint.ToStringShort()));

    mnbRet = CGoldminenodeBroadcast(service, outpoint, pubKeyCollateralAddressNew, pubKeyGoldminenodeNew, PROTOCOL_VERSION);

    if (!mnbRet.IsValidNetAddr())
        return Log(strprintf("Invalid IP address, goldminenode=%s", outpoint.ToStringShort()));

    mnbRet.lastPing = mnp;
    if (!mnbRet.Sign(keyCollateralAddressNew))
        return Log(strprintf("Failed to sign broadcast, goldminenode=%s", outpoint.ToStringShort()));

    return true;
}
#endif // ENABLE_WALLET

bool CGoldminenodeBroadcast::SimpleCheck(int& nDos)
{
    nDos = 0;

    AssertLockHeld(cs_main);

    // make sure addr is valid
    if(!IsValidNetAddr()) {
        LogPrintf("CGoldminenodeBroadcast::SimpleCheck -- Invalid addr, rejected: goldminenode=%s  addr=%s\n",
                    outpoint.ToStringShort(), addr.ToString());
        return false;
    }

    // make sure signature isn't in the future (past is OK)
    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrintf("CGoldminenodeBroadcast::SimpleCheck -- Signature rejected, too far into the future: goldminenode=%s\n", outpoint.ToStringShort());
        nDos = 1;
        return false;
    }

    // empty ping or incorrect sigTime/unknown blockhash
    if(!lastPing || !lastPing.SimpleCheck(nDos)) {
        // one of us is probably forked or smth, just mark it as expired and check the rest of the rules
        nActiveState = GOLDMINENODE_EXPIRED;
    }

    if(nProtocolVersion < mnpayments.GetMinGoldminenodePaymentsProto()) {
        LogPrintf("CGoldminenodeBroadcast::SimpleCheck -- outdated Goldminenode: goldminenode=%s  nProtocolVersion=%d\n", outpoint.ToStringShort(), nProtocolVersion);
        nActiveState = GOLDMINENODE_UPDATE_REQUIRED;
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

    int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
    if(Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if(addr.GetPort() != mainnetDefaultPort) return false;
    } else if(addr.GetPort() == mainnetDefaultPort) return false;

    return true;
}

bool CGoldminenodeBroadcast::Update(CGoldminenode* pmn, int& nDos, CConnman& connman)
{
    nDos = 0;

    AssertLockHeld(cs_main);

    if(pmn->sigTime == sigTime && !fRecovery) {
        // mapSeenGoldminenodeBroadcast in CGoldminenodeMan::CheckMnbAndUpdateGoldminenodeList should filter legit duplicates
        // but this still can happen if we just started, which is ok, just do nothing here.
        return false;
    }

    // this broadcast is older than the one that we already have - it's bad and should never happen
    // unless someone is doing something fishy
    if(pmn->sigTime > sigTime) {
        LogPrintf("CGoldminenodeBroadcast::Update -- Bad sigTime %d (existing broadcast is at %d) for Goldminenode %s %s\n",
                      sigTime, pmn->sigTime, outpoint.ToStringShort(), addr.ToString());
        return false;
    }

    pmn->Check();

    // goldminenode is banned by PoSe
    if(pmn->IsPoSeBanned()) {
        LogPrintf("CGoldminenodeBroadcast::Update -- Banned by PoSe, goldminenode=%s\n", outpoint.ToStringShort());
        return false;
    }

    // IsVnAssociatedWithPubkey is validated once in CheckOutpoint, after that they just need to match
    if(pmn->pubKeyCollateralAddress != pubKeyCollateralAddress) {
        LogPrintf("CGoldminenodeBroadcast::Update -- Got mismatched pubKeyCollateralAddress and outpoint\n");
        nDos = 33;
        return false;
    }

    if (!CheckSignature(nDos)) {
        LogPrintf("CGoldminenodeBroadcast::Update -- CheckSignature() failed, goldminenode=%s\n", outpoint.ToStringShort());
        return false;
    }

    // if ther was no goldminenode broadcast recently or if it matches our Goldminenode privkey...
    if(!pmn->IsBroadcastedWithin(GOLDMINENODE_MIN_MNB_SECONDS) || (fGoldminenodeMode && pubKeyGoldminenode == activeGoldminenode.pubKeyGoldminenode)) {
        // take the newest entry
        LogPrintf("CGoldminenodeBroadcast::Update -- Got UPDATED Goldminenode entry: addr=%s\n", addr.ToString());
        if(pmn->UpdateFromNewBroadcast(*this, connman)) {
            pmn->Check();
            Relay(connman);
        }
        goldminenodeSync.BumpAssetLastTime("CGoldminenodeBroadcast::Update");
    }

    return true;
}

bool CGoldminenodeBroadcast::CheckOutpoint(int& nDos)
{
    // we are a goldminenode with the same outpoint (i.e. already activated) and this mnb is ours (matches our Goldminenode privkey)
    // so nothing to do here for us
    if(fGoldminenodeMode && outpoint == activeGoldminenode.outpoint && pubKeyGoldminenode == activeGoldminenode.pubKeyGoldminenode) {
        return false;
    }

    AssertLockHeld(cs_main);

    int nHeight;
    CollateralStatus err = CheckCollateral(outpoint, pubKeyCollateralAddress, nHeight);
    if (err == COLLATERAL_UTXO_NOT_FOUND) {
        LogPrint("goldminenode", "CGoldminenodeBroadcast::CheckOutpoint -- Failed to find Goldminenode UTXO, goldminenode=%s\n", outpoint.ToStringShort());
        return false;
    }

    if (err == COLLATERAL_INVALID_AMOUNT) {
        LogPrint("goldminenode", "CGoldminenodeBroadcast::CheckOutpoint -- Goldminenode UTXO should have 1000 ARC, goldminenode=%s\n", outpoint.ToStringShort());
        nDos = 33;
        return false;
    }

    if(err == COLLATERAL_INVALID_PUBKEY) {
        LogPrint("goldminenode", "CGoldminenodeBroadcast::CheckOutpoint -- Goldminenode UTXO should match pubKeyCollateralAddress, goldminenode=%s\n", outpoint.ToStringShort());
        nDos = 33;
        return false;
    }

    if(chainActive.Height() - nHeight + 1 < Params().GetConsensus().nGoldminenodeMinimumConfirmations) {
        LogPrintf("CGoldminenodeBroadcast::CheckOutpoint -- Goldminenode UTXO must have at least %d confirmations, goldminenode=%s\n",
                Params().GetConsensus().nGoldminenodeMinimumConfirmations, outpoint.ToStringShort());
        // UTXO is legit but has not enough confirmations.
        // Maybe we miss few blocks, let this mnb be checked again later.
        mnodeman.mapSeenGoldminenodeBroadcast.erase(GetHash());
        return false;
    }

    LogPrint("goldminenode", "CGoldminenodeBroadcast::CheckOutpoint -- Goldminenode UTXO verified\n");

    // Verify that sig time is legit, should be at least not earlier than the timestamp of the block
    // at which collateral became nGoldminenodeMinimumConfirmations blocks deep.
    // NOTE: this is not accurate because block timestamp is NOT guaranteed to be 100% correct one.
    CBlockIndex* pRequiredConfIndex = chainActive[nHeight + Params().GetConsensus().nGoldminenodeMinimumConfirmations - 1]; // block where tx got nGoldminenodeMinimumConfirmations
    if(pRequiredConfIndex->GetBlockTime() > sigTime) {
        LogPrintf("CGoldminenodeBroadcast::CheckOutpoint -- Bad sigTime %d (%d conf block is at %d) for Goldminenode %s %s\n",
                  sigTime, Params().GetConsensus().nGoldminenodeMinimumConfirmations, pRequiredConfIndex->GetBlockTime(), outpoint.ToStringShort(), addr.ToString());
        return false;
    }

    if (!CheckSignature(nDos)) {
        LogPrintf("CGoldminenodeBroadcast::CheckOutpoint -- CheckSignature() failed, goldminenode=%s\n", outpoint.ToStringShort());
        return false;
    }

    // remember the block hash when collateral for this goldminenode had minimum required confirmations
    nCollateralMinConfBlockHash = pRequiredConfIndex->GetBlockHash();

    return true;
}

uint256 CGoldminenodeBroadcast::GetHash() const
{
    // Note: doesn't match serialization

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << outpoint << uint8_t{} << 0xffffffff; // adding dummy values here to match old hashing format
    ss << pubKeyCollateralAddress;
    ss << sigTime;
    ss << enableTime;
    return ss.GetHash();
}

uint256 CGoldminenodeBroadcast::GetSignatureHash() const
{
    // TODO: replace with "return SerializeHash(*this);" after migration to 70209
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << outpoint;
    ss << addr;
    ss << pubKeyCollateralAddress;
    ss << pubKeyGoldminenode;
    ss << sigTime;
    ss << nProtocolVersion;
    return ss.GetHash();
}

bool CGoldminenodeBroadcast::Sign(const CKey& keyCollateralAddress)
{
    std::string strError;

    sigTime = GetAdjustedTime();
    enableTime = GetAdjustedTime();

    if (sporkManager.IsSporkActive(SPORK_6_NEW_SIGS)) {
        uint256 hash = GetSignatureHash();

        if (!CHashSigner::SignHash(hash, keyCollateralAddress, vchSig)) {
            LogPrintf("CGoldminenodeBroadcast::Sign -- SignHash() failed\n");
            return false;
        }

        if (!CHashSigner::VerifyHash(hash, pubKeyCollateralAddress, vchSig, strError)) {
            LogPrintf("CGoldminenodeBroadcast::Sign -- VerifyMessage() failed, error: %s\n", strError);
            return false;
        }
    } else {
        std::string strMessage = addr.ToString(false) + boost::lexical_cast<std::string>(sigTime) +
                        pubKeyCollateralAddress.GetID().ToString() + pubKeyGoldminenode.GetID().ToString() +
                        boost::lexical_cast<std::string>(nProtocolVersion);

        if (!CMessageSigner::SignMessage(strMessage, vchSig, keyCollateralAddress)) {
            LogPrintf("CGoldminenodeBroadcast::Sign -- SignMessage() failed\n");
            return false;
        }

        if (!CMessageSigner::VerifyMessage(pubKeyCollateralAddress, vchSig, strMessage, strError)) {
            LogPrintf("CGoldminenodeBroadcast::Sign -- VerifyMessage() failed, error: %s\n", strError);
            return false;
        }
    }

    return true;
}

bool CGoldminenodeBroadcast::CheckSignature(int& nDos) const
{
    std::string strError = "";
    nDos = 0;

    if (sporkManager.IsSporkActive(SPORK_6_NEW_SIGS)) {
        uint256 hash = GetSignatureHash();

        if (!CHashSigner::VerifyHash(hash, pubKeyCollateralAddress, vchSig, strError)) {
            // maybe it's in old format
            std::string strMessage = addr.ToString(false) + boost::lexical_cast<std::string>(sigTime) +
                            pubKeyCollateralAddress.GetID().ToString() + pubKeyGoldminenode.GetID().ToString() +
                            boost::lexical_cast<std::string>(nProtocolVersion);

            if (!CMessageSigner::VerifyMessage(pubKeyCollateralAddress, vchSig, strMessage, strError)){
                // nope, not in old format either
                LogPrintf("CGoldminenodeBroadcast::CheckSignature -- Got bad Goldminenode announce signature, error: %s\n", strError);
                nDos = 100;
                return false;
            }
        }
    } else {
        std::string strMessage = addr.ToString(false) + boost::lexical_cast<std::string>(sigTime) +
                        pubKeyCollateralAddress.GetID().ToString() + pubKeyGoldminenode.GetID().ToString() +
                        boost::lexical_cast<std::string>(nProtocolVersion);

        if (!CMessageSigner::VerifyMessage(pubKeyCollateralAddress, vchSig, strMessage, strError)){
            LogPrintf("CGoldminenodeBroadcast::CheckSignature -- Got bad Goldminenode announce signature, error: %s\n", strError);
            nDos = 100;
            return false;
        }
    }

    return true;
}

void CGoldminenodeBroadcast::Relay(CConnman& connman) const
{
    // Do not relay until fully synced
    if(!goldminenodeSync.IsSynced()) {
        LogPrint("goldminenode", "CGoldminenodeBroadcast::Relay -- won't relay until fully synced\n");
        return;
    }

    CInv inv(MSG_GOLDMINENODE_ANNOUNCE, GetHash());
    connman.RelayInv(inv);
}

uint256 CGoldminenodePing::GetHash() const
{
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    if (sporkManager.IsSporkActive(SPORK_6_NEW_SIGS)) {
        // TODO: replace with "return SerializeHash(*this);" after migration to 70209
        ss << goldminenodeOutpoint;
        ss << blockHash;
        ss << sigTime;
        ss << fSentinelIsCurrent;
        ss << nSentinelVersion;
        ss << nDaemonVersion;
    } else {
        // Note: doesn't match serialization

        ss << goldminenodeOutpoint << uint8_t{} << 0xffffffff; // adding dummy values here to match old hashing format
        ss << sigTime;
    }
    return ss.GetHash();
}

uint256 CGoldminenodePing::GetSignatureHash() const
{
    return GetHash();
}

CGoldminenodePing::CGoldminenodePing(const COutPoint& outpoint)
{
    LOCK(cs_main);
    if (!chainActive.Tip() || chainActive.Height() < 12) return;

    goldminenodeOutpoint = outpoint;
    blockHash = chainActive[chainActive.Height() - 12]->GetBlockHash();
    sigTime = GetAdjustedTime();
    nDaemonVersion = CLIENT_VERSION;
}

bool CGoldminenodePing::Sign(const CKey& keyGoldminenode, const CPubKey& pubKeyGoldminenode)
{
    std::string strError;

    sigTime = GetAdjustedTime();

    if (sporkManager.IsSporkActive(SPORK_6_NEW_SIGS)) {
        uint256 hash = GetSignatureHash();

        if (!CHashSigner::SignHash(hash, keyGoldminenode, vchSig)) {
            LogPrintf("CGoldminenodePing::Sign -- SignHash() failed\n");
            return false;
        }

        if (!CHashSigner::VerifyHash(hash, pubKeyGoldminenode, vchSig, strError)) {
            LogPrintf("CGoldminenodePing::Sign -- VerifyHash() failed, error: %s\n", strError);
            return false;
        }
    } else {
        std::string strMessage = CTxIn(goldminenodeOutpoint).ToString() + blockHash.ToString() +
                    boost::lexical_cast<std::string>(sigTime);

        if (!CMessageSigner::SignMessage(strMessage, vchSig, keyGoldminenode)) {
            LogPrintf("CGoldminenodePing::Sign -- SignMessage() failed\n");
            return false;
        }

        if (!CMessageSigner::VerifyMessage(pubKeyGoldminenode, vchSig, strMessage, strError)) {
            LogPrintf("CGoldminenodePing::Sign -- VerifyMessage() failed, error: %s\n", strError);
            return false;
        }
    }

    return true;
}

bool CGoldminenodePing::CheckSignature(const CPubKey& pubKeyGoldminenode, int &nDos) const
{
    std::string strError = "";
    nDos = 0;

    if (sporkManager.IsSporkActive(SPORK_6_NEW_SIGS)) {
        uint256 hash = GetSignatureHash();

        if (!CHashSigner::VerifyHash(hash, pubKeyGoldminenode, vchSig, strError)) {
            std::string strMessage = CTxIn(goldminenodeOutpoint).ToString() + blockHash.ToString() +
                        boost::lexical_cast<std::string>(sigTime);

            if (!CMessageSigner::VerifyMessage(pubKeyGoldminenode, vchSig, strMessage, strError)) {
                LogPrintf("CGoldminenodePing::CheckSignature -- Got bad Goldminenode ping signature, goldminenode=%s, error: %s\n", goldminenodeOutpoint.ToStringShort(), strError);
                nDos = 33;
                return false;
            }
        }
    } else {
        std::string strMessage = CTxIn(goldminenodeOutpoint).ToString() + blockHash.ToString() +
                    boost::lexical_cast<std::string>(sigTime);

        if (!CMessageSigner::VerifyMessage(pubKeyGoldminenode, vchSig, strMessage, strError)) {
            LogPrintf("CGoldminenodePing::CheckSignature -- Got bad Goldminenode ping signature, goldminenode=%s, error: %s\n", goldminenodeOutpoint.ToStringShort(), strError);
            nDos = 33;
            return false;
        }
    }

    return true;
}

bool CGoldminenodePing::SimpleCheck(int& nDos)
{
    // don't ban by default
    nDos = 0;

    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrintf("CGoldminenodePing::SimpleCheck -- Signature rejected, too far into the future, goldminenode=%s\n", goldminenodeOutpoint.ToStringShort());
        nDos = 1;
        return false;
    }

    {
        AssertLockHeld(cs_main);
        BlockMap::iterator mi = mapBlockIndex.find(blockHash);
        if (mi == mapBlockIndex.end()) {
            LogPrint("goldminenode", "CGoldminenodePing::SimpleCheck -- Goldminenode ping is invalid, unknown block hash: goldminenode=%s blockHash=%s\n", goldminenodeOutpoint.ToStringShort(), blockHash.ToString());
            // maybe we stuck or forked so we shouldn't ban this node, just fail to accept this ping
            // TODO: or should we also request this block?
            return false;
        }
    }

    LogPrint("goldminenode", "CGoldminenodePing::SimpleCheck -- Goldminenode ping verified: goldminenode=%s  blockHash=%s  sigTime=%d\n", goldminenodeOutpoint.ToStringShort(), blockHash.ToString(), sigTime);
    return true;
}

bool CGoldminenodePing::CheckAndUpdate(CGoldminenode* pmn, bool fFromNewBroadcast, int& nDos, CConnman& connman)
{
    AssertLockHeld(cs_main);

    // don't ban by default
    nDos = 0;

    if (!SimpleCheck(nDos)) {
        return false;
    }

    if (pmn == NULL) {
        LogPrint("goldminenode", "CGoldminenodePing::CheckAndUpdate -- Couldn't find Goldminenode entry, goldminenode=%s\n", goldminenodeOutpoint.ToStringShort());
        return false;
    }

    if(!fFromNewBroadcast) {
        if (pmn->IsUpdateRequired()) {
            LogPrint("goldminenode", "CGoldminenodePing::CheckAndUpdate -- goldminenode protocol is outdated, goldminenode=%s\n", goldminenodeOutpoint.ToStringShort());
            return false;
        }

        if (pmn->IsNewStartRequired()) {
            LogPrint("goldminenode", "CGoldminenodePing::CheckAndUpdate -- goldminenode is completely expired, new start is required, goldminenode=%s\n", goldminenodeOutpoint.ToStringShort());
            return false;
        }
    }

    {
        BlockMap::iterator mi = mapBlockIndex.find(blockHash);
        if ((*mi).second && (*mi).second->nHeight < chainActive.Height() - 24) {
            LogPrintf("CGoldminenodePing::CheckAndUpdate -- Goldminenode ping is invalid, block hash is too old: goldminenode=%s  blockHash=%s\n", goldminenodeOutpoint.ToStringShort(), blockHash.ToString());
            // nDos = 1;
            return false;
        }
    }

    LogPrint("goldminenode", "CGoldminenodePing::CheckAndUpdate -- New ping: goldminenode=%s  blockHash=%s  sigTime=%d\n", goldminenodeOutpoint.ToStringShort(), blockHash.ToString(), sigTime);

    // LogPrintf("mnping - Found corresponding mn for outpoint: %s\n", goldminenodeOutpoint.ToStringShort());
    // update only if there is no known ping for this goldminenode or
    // last ping was more then GOLDMINENODE_MIN_MNP_SECONDS-60 ago comparing to this one
    if (pmn->IsPingedWithin(GOLDMINENODE_MIN_MNP_SECONDS - 60, sigTime)) {
        LogPrint("goldminenode", "CGoldminenodePing::CheckAndUpdate -- Goldminenode ping arrived too early, goldminenode=%s\n", goldminenodeOutpoint.ToStringShort());
        //nDos = 1; //disable, this is happening frequently and causing banned peers
        return false;
    }

    if (!CheckSignature(pmn->pubKeyGoldminenode, nDos)) return false;

    // so, ping seems to be ok

    // if we are still syncing and there was no known ping for this mn for quite a while
    // (NOTE: assuming that GOLDMINENODE_EXPIRATION_SECONDS/2 should be enough to finish mn list sync)
    if(!goldminenodeSync.IsGoldminenodeListSynced() && !pmn->IsPingedWithin(GOLDMINENODE_EXPIRATION_SECONDS/2)) {
        // let's bump sync timeout
        LogPrint("goldminenode", "CGoldminenodePing::CheckAndUpdate -- bumping sync timeout, goldminenode=%s\n", goldminenodeOutpoint.ToStringShort());
        goldminenodeSync.BumpAssetLastTime("CGoldminenodePing::CheckAndUpdate");
    }

    // let's store this ping as the last one
    LogPrint("goldminenode", "CGoldminenodePing::CheckAndUpdate -- Goldminenode ping accepted, goldminenode=%s\n", goldminenodeOutpoint.ToStringShort());
    pmn->lastPing = *this;

    // and update mnodeman.mapSeenGoldminenodeBroadcast.lastPing which is probably outdated
    CGoldminenodeBroadcast mnb(*pmn);
    uint256 hash = mnb.GetHash();
    if (mnodeman.mapSeenGoldminenodeBroadcast.count(hash)) {
        mnodeman.mapSeenGoldminenodeBroadcast[hash].second.lastPing = *this;
    }

    // force update, ignoring cache
    pmn->Check(true);
    // relay ping for nodes in ENABLED/EXPIRED/SENTINEL_PING_EXPIRED state only, skip everyone else
    if (!pmn->IsEnabled() && !pmn->IsExpired() && !pmn->IsSentinelPingExpired()) return false;

    LogPrint("goldminenode", "CGoldminenodePing::CheckAndUpdate -- Goldminenode ping acceepted and relayed, goldminenode=%s\n", goldminenodeOutpoint.ToStringShort());
    Relay(connman);

    return true;
}

void CGoldminenodePing::Relay(CConnman& connman)
{
    // Do not relay until fully synced
    if(!goldminenodeSync.IsSynced()) {
        LogPrint("goldminenode", "CGoldminenodePing::Relay -- won't relay until fully synced\n");
        return;
    }

    CInv inv(MSG_GOLDMINENODE_PING, GetHash());
    connman.RelayInv(inv);
}
void CGoldminenode::UpdateWatchdogVoteTime(uint64_t nVoteTime)
{
    LOCK(cs);
    nTimeLastWatchdogVote = (nVoteTime == 0) ? GetAdjustedTime() : nVoteTime;
}

