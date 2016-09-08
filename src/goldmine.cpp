// Copyright (c) 2015-2016 The Arctic developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "goldmine.h"
#include "goldmineman.h"
#include "spysend.h"
#include "util.h"
#include "sync.h"
#include "addrman.h"
#include <boost/lexical_cast.hpp>

// keep track of the scanning errors I've seen
map<uint256, int> mapSeenGoldmineScanningErrors;
// cache block hashes as we calculate them
std::map<int64_t, uint256> mapCacheBlockHashes;

//Get the last hash that matches the modulus given. Processed in reverse order
bool GetBlockHash(uint256& hash, int nBlockHeight)
{
    if (chainActive.Tip() == NULL) return false;

    if(nBlockHeight == 0)
        nBlockHeight = chainActive.Tip()->nHeight;

    if(mapCacheBlockHashes.count(nBlockHeight)){
        hash = mapCacheBlockHashes[nBlockHeight];
        return true;
    }

    const CBlockIndex *BlockLastSolved = chainActive.Tip();
    const CBlockIndex *BlockReading = chainActive.Tip();

    if (BlockLastSolved == NULL || BlockLastSolved->nHeight == 0 || chainActive.Tip()->nHeight+1 < nBlockHeight) return false;

    int nBlocksAgo = 0;
    if(nBlockHeight > 0) nBlocksAgo = (chainActive.Tip()->nHeight+1)-nBlockHeight;
    assert(nBlocksAgo >= 0);

    int n = 0;
    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if(n >= nBlocksAgo){
            hash = BlockReading->GetBlockHash();
            mapCacheBlockHashes[nBlockHeight] = hash;
            return true;
        }
        n++;

        if (BlockReading->pprev == NULL) { assert(BlockReading); break; }
        BlockReading = BlockReading->pprev;
    }

    return false;
}

CGoldmine::CGoldmine()
{
    LOCK(cs);
    vin = CTxIn();
    addr = CService();
    pubkey = CPubKey();
    pubkey2 = CPubKey();
    sig = std::vector<unsigned char>();
    activeState = GOLDMINE_ENABLED;
    sigTime = GetAdjustedTime();
    lastPing = CGoldminePing();
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    protocolVersion = PROTOCOL_VERSION;
    nLastDsq = 0;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
    lastTimeChecked = 0;
}

CGoldmine::CGoldmine(const CGoldmine& other)
{
    LOCK(cs);
    vin = other.vin;
    addr = other.addr;
    pubkey = other.pubkey;
    pubkey2 = other.pubkey2;
    sig = other.sig;
    activeState = other.activeState;
    sigTime = other.sigTime;
    lastPing = other.lastPing;
    cacheInputAge = other.cacheInputAge;
    cacheInputAgeBlock = other.cacheInputAgeBlock;
    unitTest = other.unitTest;
    allowFreeTx = other.allowFreeTx;
    protocolVersion = other.protocolVersion;
    nLastDsq = other.nLastDsq;
    nScanningErrorCount = other.nScanningErrorCount;
    nLastScanningErrorBlockHeight = other.nLastScanningErrorBlockHeight;
    lastTimeChecked = 0;
}

CGoldmine::CGoldmine(const CGoldmineBroadcast& gmb)
{
    LOCK(cs);
    vin = gmb.vin;
    addr = gmb.addr;
    pubkey = gmb.pubkey;
    pubkey2 = gmb.pubkey2;
    sig = gmb.sig;
    activeState = GOLDMINE_ENABLED;
    sigTime = gmb.sigTime;
    lastPing = gmb.lastPing;
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    protocolVersion = gmb.protocolVersion;
    nLastDsq = gmb.nLastDsq;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
    lastTimeChecked = 0;
}

//
// When a new goldmine broadcast is sent, update our information
//
bool CGoldmine::UpdateFromNewBroadcast(CGoldmineBroadcast& gmb)
{
    if(gmb.sigTime > sigTime) {    
        pubkey2 = gmb.pubkey2;
        sigTime = gmb.sigTime;
        sig = gmb.sig;
        protocolVersion = gmb.protocolVersion;
        addr = gmb.addr;
        lastTimeChecked = 0;
        int nDoS = 0;
        if(gmb.lastPing == CGoldminePing() || (gmb.lastPing != CGoldminePing() && gmb.lastPing.CheckAndUpdate(nDoS, false))) {
            lastPing = gmb.lastPing;
            gmineman.mapSeenGoldminePing.insert(make_pair(lastPing.GetHash(), lastPing));
        }
        return true;
    }
    return false;
}

//
// Deterministically calculate a given "score" for a Goldmine depending on how close it's hash is to
// the proof of work for that block. The further away they are the better, the furthest will win the election
// and get paid this block
//
uint256 CGoldmine::CalculateScore(int mod, int64_t nBlockHeight)
{
    if(chainActive.Tip() == NULL) return 0;

    uint256 hash = 0;
    uint256 aux = vin.prevout.hash + vin.prevout.n;

    if(!GetBlockHash(hash, nBlockHeight)) {
        LogPrintf("CalculateScore ERROR - nHeight %d - Returned 0\n", nBlockHeight);
        return 0;
    }

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << hash;
    uint256 hash2 = ss.GetHash();

    CHashWriter ss2(SER_GETHASH, PROTOCOL_VERSION);
    ss2 << hash;
    ss2 << aux;
    uint256 hash3 = ss2.GetHash();

    uint256 r = (hash3 > hash2 ? hash3 - hash2 : hash2 - hash3);

    return r;
}

void CGoldmine::Check(bool forceCheck)
{
    if(ShutdownRequested()) return;

    if(!forceCheck && (GetTime() - lastTimeChecked < GOLDMINE_CHECK_SECONDS)) return;
    lastTimeChecked = GetTime();


    //once spent, stop doing the checks
    if(activeState == GOLDMINE_VIN_SPENT) return;


    if(!IsPingedWithin(GOLDMINE_REMOVAL_SECONDS)){
        activeState = GOLDMINE_REMOVE;
        return;
    }

    if(!IsPingedWithin(GOLDMINE_EXPIRATION_SECONDS)){
        activeState = GOLDMINE_EXPIRED;
        return;
    }

    if(!unitTest){
        CValidationState state;
        CMutableTransaction tx = CMutableTransaction();
        CTxOut vout = CTxOut(999.99*COIN, spySendPool.collateralPubKey);
        tx.vin.push_back(vin);
        tx.vout.push_back(vout);

        {
            TRY_LOCK(cs_main, lockMain);
            if(!lockMain) return;

            if(!AcceptableInputs(mempool, state, CTransaction(tx), false, NULL)){
                activeState = GOLDMINE_VIN_SPENT;
                return;

            }
        }
    }

    activeState = GOLDMINE_ENABLED; // OK
}

int64_t CGoldmine::SecondsSincePayment() {
    CScript pubkeyScript;
    pubkeyScript = GetScriptForDestination(pubkey.GetID());

    int64_t sec = (GetAdjustedTime() - GetLastPaid());
    int64_t month = 60*60*24*30;
    if(sec < month) return sec; //if it's less than 30 days, give seconds

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << vin;
    ss << sigTime;
    uint256 hash =  ss.GetHash();

    // return some deterministic value for unknown/unpaid but force it to be more than 30 days old
    return month + hash.GetCompact(false);
}

int64_t CGoldmine::GetLastPaid() {
    CBlockIndex* pindexPrev = chainActive.Tip();
    if(pindexPrev == NULL) return false;

    CScript gmpayee;
    gmpayee = GetScriptForDestination(pubkey.GetID());

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << vin;
    ss << sigTime;
    uint256 hash =  ss.GetHash();

    // use a deterministic offset to break a tie -- 2.5 minutes
    int64_t nOffset = hash.GetCompact(false) % 150; 

    if (chainActive.Tip() == NULL) return false;

    const CBlockIndex *BlockReading = chainActive.Tip();

    int nMnCount = gmineman.CountEnabled()*1.25;
    int n = 0;
    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if(n >= nMnCount){
            return 0;
        }
        n++;

        if(goldminePayments.mapGoldmineBlocks.count(BlockReading->nHeight)){
            /*
                Search for this payee, with at least 2 votes. This will aid in consensus allowing the network 
                to converge on the same payees quickly, then keep the same schedule.
            */
            if(goldminePayments.mapGoldmineBlocks[BlockReading->nHeight].HasPayeeWithVotes(gmpayee, 2)){
                return BlockReading->nTime + nOffset;
            }
        }

        if (BlockReading->pprev == NULL) { assert(BlockReading); break; }
        BlockReading = BlockReading->pprev;
    }

    return 0;
}

CGoldmineBroadcast::CGoldmineBroadcast()
{
    vin = CTxIn();
    addr = CService();
    pubkey = CPubKey();
    pubkey2 = CPubKey();
    sig = std::vector<unsigned char>();
    activeState = GOLDMINE_ENABLED;
    sigTime = GetAdjustedTime();
    lastPing = CGoldminePing();
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    protocolVersion = PROTOCOL_VERSION;
    nLastDsq = 0;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
}

CGoldmineBroadcast::CGoldmineBroadcast(CService newAddr, CTxIn newVin, CPubKey newPubkey, CPubKey newPubkey2, int protocolVersionIn)
{
    vin = newVin;
    addr = newAddr;
    pubkey = newPubkey;
    pubkey2 = newPubkey2;
    sig = std::vector<unsigned char>();
    activeState = GOLDMINE_ENABLED;
    sigTime = GetAdjustedTime();
    lastPing = CGoldminePing();
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    protocolVersion = protocolVersionIn;
    nLastDsq = 0;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
}

CGoldmineBroadcast::CGoldmineBroadcast(const CGoldmine& gm)
{
    vin = gm.vin;
    addr = gm.addr;
    pubkey = gm.pubkey;
    pubkey2 = gm.pubkey2;
    sig = gm.sig;
    activeState = gm.activeState;
    sigTime = gm.sigTime;
    lastPing = gm.lastPing;
    cacheInputAge = gm.cacheInputAge;
    cacheInputAgeBlock = gm.cacheInputAgeBlock;
    unitTest = gm.unitTest;
    allowFreeTx = gm.allowFreeTx;
    protocolVersion = gm.protocolVersion;
    nLastDsq = gm.nLastDsq;
    nScanningErrorCount = gm.nScanningErrorCount;
    nLastScanningErrorBlockHeight = gm.nLastScanningErrorBlockHeight;
}

bool CGoldmineBroadcast::CheckAndUpdate(int& nDos)
{
    nDos = 0;

    // make sure signature isn't in the future (past is OK)
    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrintf("gmb - Signature rejected, too far into the future %s\n", vin.ToString());
        nDos = 1;
        return false;
    }

    if(protocolVersion < goldminePayments.GetMinGoldminePaymentsProto()) {
        LogPrintf("gmb - ignoring outdated Goldmine %s protocol version %d\n", vin.ToString(), protocolVersion);
        return false;
    }

    CScript pubkeyScript;
    pubkeyScript = GetScriptForDestination(pubkey.GetID());

    if(pubkeyScript.size() != 25) {
        LogPrintf("gmb - pubkey the wrong size\n");
        nDos = 100;
        return false;
    }

    CScript pubkeyScript2;
    pubkeyScript2 = GetScriptForDestination(pubkey2.GetID());

    if(pubkeyScript2.size() != 25) {
        LogPrintf("gmb - pubkey2 the wrong size\n");
        nDos = 100;
        return false;
    }

    if(!vin.scriptSig.empty()) {
        LogPrintf("gmb - Ignore Not Empty ScriptSig %s\n",vin.ToString());
        return false;
    }

    // incorrect ping or its sigTime
    if(lastPing == CGoldminePing() || !lastPing.CheckAndUpdate(nDos, false, true))
        return false;

    std::string strMessage;
    std::string errorMessage = "";

    if(protocolVersion < 70201) {
        std::string vchPubKey(pubkey.begin(), pubkey.end());
        std::string vchPubKey2(pubkey2.begin(), pubkey2.end());
        strMessage = addr.ToString(false) + boost::lexical_cast<std::string>(sigTime) +
                        vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(protocolVersion);

        LogPrint("goldmine", "gmb - sanitized strMessage: %s, pubkey address: %s, sig: %s\n",
            SanitizeString(strMessage), CBitcoinAddress(pubkey.GetID()).ToString(),
            EncodeBase64(&sig[0], sig.size()));

        if(!spySendSigner.VerifyMessage(pubkey, sig, strMessage, errorMessage)){
            if (addr.ToString() != addr.ToString(false))
            {
                // maybe it's wrong format, try again with the old one
                strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) +
                                vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(protocolVersion);

                LogPrint("goldmine", "gmb - sanitized strMessage: %s, pubkey address: %s, sig: %s\n",
                    SanitizeString(strMessage), CBitcoinAddress(pubkey.GetID()).ToString(),
                    EncodeBase64(&sig[0], sig.size()));

                if(!spySendSigner.VerifyMessage(pubkey, sig, strMessage, errorMessage)){
                    // didn't work either
                    LogPrintf("gmb - Got bad Goldmine address signature, sanitized error: %s\n", SanitizeString(errorMessage));
                    // there is a bug in old MN signatures, ignore such MN but do not ban the peer we got this from
                    return false;
                }
            } else {
                // nope, sig is actually wrong
                LogPrintf("gmb - Got bad Goldmine address signature, sanitized error: %s\n", SanitizeString(errorMessage));
                // there is a bug in old MN signatures, ignore such MN but do not ban the peer we got this from
                return false;
            }
        }
    } else {
        strMessage = addr.ToString(false) + boost::lexical_cast<std::string>(sigTime) +
                        pubkey.GetID().ToString() + pubkey2.GetID().ToString() +
                        boost::lexical_cast<std::string>(protocolVersion);

        LogPrint("goldmine", "gmb - strMessage: %s, pubkey address: %s, sig: %s\n",
            strMessage, CBitcoinAddress(pubkey.GetID()).ToString(), EncodeBase64(&sig[0], sig.size()));

        if(!spySendSigner.VerifyMessage(pubkey, sig, strMessage, errorMessage)){
            LogPrintf("gmb - Got bad Goldmine address signature, error: %s\n", errorMessage);
            nDos = 100;
            return false;
        }
    }

    if(Params().NetworkID() == CBaseChainParams::MAIN) {
        if(addr.GetPort() != 7209) return false;
    } else if(addr.GetPort() == 7209) return false;

    //search existing Goldmine list, this is where we update existing Goldmines with new gmb broadcasts
    CGoldmine* pgm = gmineman.Find(vin);

    // no such goldmine, nothing to update
    if(pgm == NULL) return true;

    // this broadcast is older or equal than the one that we already have - it's bad and should never happen
    // unless someone is doing something fishy
    // (mapSeenGoldmineBroadcast in CGoldmineMan::ProcessMessage should filter legit duplicates)
    if(pgm->sigTime >= sigTime) {
        LogPrintf("CGoldmineBroadcast::CheckAndUpdate - Bad sigTime %d for Goldmine %20s %105s (existing broadcast is at %d)\n",
                      sigTime, addr.ToString(), vin.ToString(), pgm->sigTime);
        return false;
    }

    // goldmine is not enabled yet/already, nothing to update
    if(!pgm->IsEnabled()) return true;

    // gm.pubkey = pubkey, IsVinAssociatedWithPubkey is validated once below,
    //   after that they just need to match
    if(pgm->pubkey == pubkey && !pgm->IsBroadcastedWithin(GOLDMINE_MIN_MNB_SECONDS)) {
        //take the newest entry
        LogPrintf("gmb - Got updated entry for %s\n", addr.ToString());
        if(pgm->UpdateFromNewBroadcast((*this))){
            pgm->Check();
            if(pgm->IsEnabled()) Relay();
        }
        goldmineSync.AddedGoldmineList(GetHash());
    }

    return true;
}

bool CGoldmineBroadcast::CheckInputsAndAdd(int& nDoS)
{
    // we are a goldmine with the same vin (i.e. already activated) and this gmb is ours (matches our Goldmine privkey)
    // so nothing to do here for us
    if(fGoldMine && vin.prevout == activeGoldmine.vin.prevout && pubkey2 == activeGoldmine.pubKeyGoldmine)
        return true;

    // incorrect ping or its sigTime
    if(lastPing == CGoldminePing() || !lastPing.CheckAndUpdate(nDoS, false, true))
        return false;

    // search existing Goldmine list
    CGoldmine* pgm = gmineman.Find(vin);

    if(pgm != NULL) {
        // nothing to do here if we already know about this goldmine and it's enabled
        if(pgm->IsEnabled()) return true;
        // if it's not enabled, remove old MN first and continue
        else gmineman.Remove(pgm->vin);
    }

    CValidationState state;
    CMutableTransaction tx = CMutableTransaction();
    CTxOut vout = CTxOut(999.99*COIN, spySendPool.collateralPubKey);
    tx.vin.push_back(vin);
    tx.vout.push_back(vout);

    {
        TRY_LOCK(cs_main, lockMain);
        if(!lockMain) {
            // not gmb fault, let it to be checked again later
            gmineman.mapSeenGoldmineBroadcast.erase(GetHash());
            goldmineSync.mapSeenSyncMNB.erase(GetHash());
            return false;
        }

        if(!AcceptableInputs(mempool, state, CTransaction(tx), false, NULL)) {
            //set nDos
            state.IsInvalid(nDoS);
            return false;
        }
    }

    LogPrint("goldmine", "gmb - Accepted Goldmine entry\n");

    if(GetInputAge(vin) < GOLDMINE_MIN_CONFIRMATIONS){
        LogPrintf("gmb - Input must have at least %d confirmations\n", GOLDMINE_MIN_CONFIRMATIONS);
        // maybe we miss few blocks, let this gmb to be checked again later
        gmineman.mapSeenGoldmineBroadcast.erase(GetHash());
        goldmineSync.mapSeenSyncMNB.erase(GetHash());
        return false;
    }

    // verify that sig time is legit in past
    // should be at least not earlier than block when 1000 ARC tx got GOLDMINE_MIN_CONFIRMATIONS
    uint256 hashBlock = 0;
    CTransaction tx2;
    GetTransaction(vin.prevout.hash, tx2, hashBlock, true);
    BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
    if (mi != mapBlockIndex.end() && (*mi).second)
    {
        CBlockIndex* pMNIndex = (*mi).second; // block for 1000 ARC tx -> 1 confirmation
        CBlockIndex* pConfIndex = chainActive[pMNIndex->nHeight + GOLDMINE_MIN_CONFIRMATIONS - 1]; // block where tx got GOLDMINE_MIN_CONFIRMATIONS
        if(pConfIndex->GetBlockTime() > sigTime)
        {
            LogPrintf("gmb - Bad sigTime %d for Goldmine %20s %105s (%i conf block is at %d)\n",
                      sigTime, addr.ToString(), vin.ToString(), GOLDMINE_MIN_CONFIRMATIONS, pConfIndex->GetBlockTime());
            return false;
        }
    }

    LogPrintf("gmb - Got NEW Goldmine entry - %s - %s - %s - %lli \n", GetHash().ToString(), addr.ToString(), vin.ToString(), sigTime);
    CGoldmine gm(*this);
    gmineman.Add(gm);

    // if it matches our Goldmine privkey, then we've been remotely activated
    if(pubkey2 == activeGoldmine.pubKeyGoldmine && protocolVersion == PROTOCOL_VERSION){
        activeGoldmine.EnableHotColdGoldMine(vin, addr);
    }

    bool isLocal = addr.IsRFC1918() || addr.IsLocal();
    if(Params().NetworkID() == CBaseChainParams::REGTEST) isLocal = false;

    if(!isLocal) Relay();

    return true;
}

void CGoldmineBroadcast::Relay()
{
    CInv inv(MSG_GOLDMINE_ANNOUNCE, GetHash());
    RelayInv(inv);
}

bool CGoldmineBroadcast::Sign(CKey& keyCollateralAddress)
{
    std::string errorMessage;

    std::string vchPubKey(pubkey.begin(), pubkey.end());
    std::string vchPubKey2(pubkey2.begin(), pubkey2.end());

    sigTime = GetAdjustedTime();

    std::string strMessage = addr.ToString(false) + boost::lexical_cast<std::string>(sigTime) + vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(protocolVersion);

    if(!spySendSigner.SignMessage(strMessage, errorMessage, sig, keyCollateralAddress)) {
        LogPrintf("CGoldmineBroadcast::Sign() - Error: %s\n", errorMessage);
        return false;
    }

    return true;
}

bool CGoldmineBroadcast::VerifySignature()
{
    std::string errorMessage;

    std::string vchPubKey(pubkey.begin(), pubkey.end());
    std::string vchPubKey2(pubkey2.begin(), pubkey2.end());

    std::string strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) + vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(protocolVersion);

    if(!spySendSigner.VerifyMessage(pubkey, sig, strMessage, errorMessage)) {
        LogPrintf("CGoldmineBroadcast::VerifySignature() - Error: %s\n", errorMessage);
        return false;
    }

    return true;
}

CGoldminePing::CGoldminePing()
{
    vin = CTxIn();
    blockHash = uint256(0);
    sigTime = 0;
    vchSig = std::vector<unsigned char>();
}

CGoldminePing::CGoldminePing(CTxIn& newVin)
{
    vin = newVin;
    blockHash = chainActive[chainActive.Height() - 12]->GetBlockHash();
    sigTime = GetAdjustedTime();
    vchSig = std::vector<unsigned char>();
}


bool CGoldminePing::Sign(CKey& keyGoldmine, CPubKey& pubKeyGoldmine)
{
    std::string errorMessage;
    std::string strGoldMineSignMessage;

    sigTime = GetAdjustedTime();
    std::string strMessage = vin.ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);

    if(!spySendSigner.SignMessage(strMessage, errorMessage, vchSig, keyGoldmine)) {
        LogPrintf("CGoldminePing::Sign() - Error: %s\n", errorMessage);
        return false;
    }

    if(!spySendSigner.VerifyMessage(pubKeyGoldmine, vchSig, strMessage, errorMessage)) {
        LogPrintf("CGoldminePing::Sign() - Error: %s\n", errorMessage);
        return false;
    }

    return true;
}

bool CGoldminePing::VerifySignature(CPubKey& pubKeyGoldmine, int &nDos) {
    std::string strMessage = vin.ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);
    std::string errorMessage = "";

    if(!spySendSigner.VerifyMessage(pubKeyGoldmine, vchSig, strMessage, errorMessage))
    {
        LogPrintf("CGoldminePing::VerifySignature - Got bad Goldmine ping signature %s Error: %s\n", vin.ToString(), errorMessage);
        nDos = 33;
        return false;
    }
    return true;
}

bool CGoldminePing::CheckAndUpdate(int& nDos, bool fRequireEnabled, bool fCheckSigTimeOnly)
{
    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrintf("CGoldminePing::CheckAndUpdate - Signature rejected, too far into the future %s\n", vin.ToString());
        nDos = 1;
        return false;
    }

    if (sigTime <= GetAdjustedTime() - 60 * 60) {
        LogPrintf("CGoldminePing::CheckAndUpdate - Signature rejected, too far into the past %s - %d %d \n", vin.ToString(), sigTime, GetAdjustedTime());
        nDos = 1;
        return false;
    }

    if(fCheckSigTimeOnly) {
        CGoldmine* pgm = gmineman.Find(vin);
        if(pgm) return VerifySignature(pgm->pubkey2, nDos);
        return true;
    }

    LogPrint("goldmine", "CGoldminePing::CheckAndUpdate - New Ping - %s - %s - %lli\n", GetHash().ToString(), blockHash.ToString(), sigTime);

    // see if we have this Goldmine
    CGoldmine* pgm = gmineman.Find(vin);
    if(pgm != NULL && pgm->protocolVersion >= goldminePayments.GetMinGoldminePaymentsProto())
    {
        if (fRequireEnabled && !pgm->IsEnabled()) return false;

        // LogPrintf("mnping - Found corresponding gm for vin: %s\n", vin.ToString());
        // update only if there is no known ping for this goldmine or
        // last ping was more then GOLDMINE_MIN_MNP_SECONDS-60 ago comparing to this one
        if(!pgm->IsPingedWithin(GOLDMINE_MIN_MNP_SECONDS - 60, sigTime))
        {
            if(!VerifySignature(pgm->pubkey2, nDos))
                return false;

            BlockMap::iterator mi = mapBlockIndex.find(blockHash);
            if (mi != mapBlockIndex.end() && (*mi).second)
            {
                if((*mi).second->nHeight < chainActive.Height() - 24)
                {
                    LogPrintf("CGoldminePing::CheckAndUpdate - Goldmine %s block hash %s is too old\n", vin.ToString(), blockHash.ToString());
                    // Do nothing here (no Goldmine update, no mnping relay)
                    // Let this node to be visible but fail to accept mnping

                    return false;
                }
            } else {
                if (fDebug) LogPrintf("CGoldminePing::CheckAndUpdate - Goldmine %s block hash %s is unknown\n", vin.ToString(), blockHash.ToString());
                // maybe we stuck so we shouldn't ban this node, just fail to accept it
                // TODO: or should we also request this block?

                return false;
            }

            pgm->lastPing = *this;

            //gmineman.mapSeenGoldmineBroadcast.lastPing is probably outdated, so we'll update it
            CGoldmineBroadcast gmb(*pgm);
            uint256 hash = gmb.GetHash();
            if(gmineman.mapSeenGoldmineBroadcast.count(hash)) {
                gmineman.mapSeenGoldmineBroadcast[hash].lastPing = *this;
            }

            pgm->Check(true);
            if(!pgm->IsEnabled()) return false;

            LogPrint("goldmine", "CGoldminePing::CheckAndUpdate - Goldmine ping accepted, vin: %s\n", vin.ToString());

            Relay();
            return true;
        }
        LogPrint("goldmine", "CGoldminePing::CheckAndUpdate - Goldmine ping arrived too early, vin: %s\n", vin.ToString());
        //nDos = 1; //disable, this is happening frequently and causing banned peers
        return false;
    }
    LogPrint("goldmine", "CGoldminePing::CheckAndUpdate - Couldn't find compatible Goldmine entry, vin: %s\n", vin.ToString());

    return false;
}

void CGoldminePing::Relay()
{
    CInv inv(MSG_GOLDMINE_PING, GetHash());
    RelayInv(inv);
}
