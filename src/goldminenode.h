// Copyright (c) 2015-2017 The Arctic Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef GOLDMINENODE_H
#define GOLDMINENODE_H

#include "key.h"
#include "main.h"
#include "net.h"
#include "spork.h"
#include "timedata.h"

class CGoldminenode;
class CGoldminenodeBroadcast;
class CGoldminenodePing;

static const int GOLDMINENODE_CHECK_SECONDS               =   5;
static const int GOLDMINENODE_MIN_MNB_SECONDS             =   5 * 60;
static const int GOLDMINENODE_MIN_MNP_SECONDS             =  10 * 60;
static const int GOLDMINENODE_EXPIRATION_SECONDS          =  65 * 60;
static const int GOLDMINENODE_WATCHDOG_MAX_SECONDS        = 120 * 60;
static const int GOLDMINENODE_NEW_START_REQUIRED_SECONDS  = 180 * 60;

static const int GOLDMINENODE_POSE_BAN_MAX_SCORE          = 5;
//
// The Goldminenode Ping Class : Contains a different serialize method for sending pings from goldminenodes throughout the network
//

class CGoldminenodePing
{
public:
    CTxIn vin;
    uint256 blockHash;
    int64_t sigTime; //mnb message times
    std::vector<unsigned char> vchSig;
    //removed stop

    CGoldminenodePing() :
        vin(),
        blockHash(),
        sigTime(0),
        vchSig()
        {}

    CGoldminenodePing(CTxIn& vinNew);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(vin);
        READWRITE(blockHash);
        READWRITE(sigTime);
        READWRITE(vchSig);
    }

    void swap(CGoldminenodePing& first, CGoldminenodePing& second) // nothrow
    {
        // enable ADL (not necessary in our case, but good practice)
        using std::swap;

        // by swapping the members of two classes,
        // the two classes are effectively swapped
        swap(first.vin, second.vin);
        swap(first.blockHash, second.blockHash);
        swap(first.sigTime, second.sigTime);
        swap(first.vchSig, second.vchSig);
    }

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vin;
        ss << sigTime;
        return ss.GetHash();
    }

    bool IsExpired() { return GetTime() - sigTime > GOLDMINENODE_NEW_START_REQUIRED_SECONDS; }

    bool Sign(CKey& keyGoldminenode, CPubKey& pubKeyGoldminenode);
    bool CheckSignature(CPubKey& pubKeyGoldminenode, int &nDos);
    bool SimpleCheck(int& nDos);
    bool CheckAndUpdate(CGoldminenode* pmn, bool fFromNewBroadcast, int& nDos);
    void Relay();

    CGoldminenodePing& operator=(CGoldminenodePing from)
    {
        swap(*this, from);
        return *this;
    }
    friend bool operator==(const CGoldminenodePing& a, const CGoldminenodePing& b)
    {
        return a.vin == b.vin && a.blockHash == b.blockHash;
    }
    friend bool operator!=(const CGoldminenodePing& a, const CGoldminenodePing& b)
    {
        return !(a == b);
    }

};

struct goldminenode_info_t
{
    goldminenode_info_t()
        : vin(),
          addr(),
          pubKeyCollateralAddress(),
          pubKeyGoldminenode(),
          sigTime(0),
          nLastDsq(0),
          nTimeLastChecked(0),
          nTimeLastPaid(0),
          nTimeLastWatchdogVote(0),
          nTimeLastPing(0),
          nActiveState(0),
          nProtocolVersion(0),
          fInfoValid(false)
        {}

    CTxIn vin;
    CService addr;
    CPubKey pubKeyCollateralAddress;
    CPubKey pubKeyGoldminenode;
    int64_t sigTime; //mnb message time
    int64_t nLastDsq; //the dsq count from the last dsq broadcast of this node
    int64_t nTimeLastChecked;
    int64_t nTimeLastPaid;
    int64_t nTimeLastWatchdogVote;
    int64_t nTimeLastPing;
    int nActiveState;
    int nProtocolVersion;
    bool fInfoValid;
};

//
// The Goldminenode Class. For managing the SpySend process. It contains the input of the 1000DRK, signature to prove
// it's the one who own that ip address and code for calculating the payment election.
//
class CGoldminenode
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

public:
    enum state {
        GOLDMINENODE_PRE_ENABLED,
        GOLDMINENODE_ENABLED,
        GOLDMINENODE_EXPIRED,
        GOLDMINENODE_OUTPOINT_SPENT,
        GOLDMINENODE_UPDATE_REQUIRED,
        GOLDMINENODE_WATCHDOG_EXPIRED,
        GOLDMINENODE_NEW_START_REQUIRED,
        GOLDMINENODE_POSE_BAN
    };

    CTxIn vin;
    CService addr;
    CPubKey pubKeyCollateralAddress;
    CPubKey pubKeyGoldminenode;
    CGoldminenodePing lastPing;
    std::vector<unsigned char> vchSig;
    int64_t sigTime; //mnb message time
    int64_t nLastDsq; //the dsq count from the last dsq broadcast of this node
    int64_t nTimeLastChecked;
    int64_t nTimeLastPaid;
    int64_t nTimeLastWatchdogVote;
    int nActiveState;
    int nCacheCollateralBlock;
    int nBlockLastPaid;
    int nProtocolVersion;
    int nPoSeBanScore;
    int nPoSeBanHeight;
    bool fAllowMixingTx;
    bool fUnitTest;

    // KEEP TRACK OF GOVERNANCE ITEMS EACH GOLDMINENODE HAS VOTE UPON FOR RECALCULATION
    std::map<uint256, int> mapGovernanceObjectsVotedOn;

    CGoldminenode();
    CGoldminenode(const CGoldminenode& other);
    CGoldminenode(const CGoldminenodeBroadcast& mnb);
    CGoldminenode(CService addrNew, CTxIn vinNew, CPubKey pubKeyCollateralAddressNew, CPubKey pubKeyGoldminenodeNew, int nProtocolVersionIn);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        LOCK(cs);
        READWRITE(vin);
        READWRITE(addr);
        READWRITE(pubKeyCollateralAddress);
        READWRITE(pubKeyGoldminenode);
        READWRITE(lastPing);
        READWRITE(vchSig);
        READWRITE(sigTime);
        READWRITE(nLastDsq);
        READWRITE(nTimeLastChecked);
        READWRITE(nTimeLastPaid);
        READWRITE(nTimeLastWatchdogVote);
        READWRITE(nActiveState);
        READWRITE(nCacheCollateralBlock);
        READWRITE(nBlockLastPaid);
        READWRITE(nProtocolVersion);
        READWRITE(nPoSeBanScore);
        READWRITE(nPoSeBanHeight);
        READWRITE(fAllowMixingTx);
        READWRITE(fUnitTest);
        READWRITE(mapGovernanceObjectsVotedOn);
    }

    void swap(CGoldminenode& first, CGoldminenode& second) // nothrow
    {
        // enable ADL (not necessary in our case, but good practice)
        using std::swap;

        // by swapping the members of two classes,
        // the two classes are effectively swapped
        swap(first.vin, second.vin);
        swap(first.addr, second.addr);
        swap(first.pubKeyCollateralAddress, second.pubKeyCollateralAddress);
        swap(first.pubKeyGoldminenode, second.pubKeyGoldminenode);
        swap(first.lastPing, second.lastPing);
        swap(first.vchSig, second.vchSig);
        swap(first.sigTime, second.sigTime);
        swap(first.nLastDsq, second.nLastDsq);
        swap(first.nTimeLastChecked, second.nTimeLastChecked);
        swap(first.nTimeLastPaid, second.nTimeLastPaid);
        swap(first.nTimeLastWatchdogVote, second.nTimeLastWatchdogVote);
        swap(first.nActiveState, second.nActiveState);
        swap(first.nCacheCollateralBlock, second.nCacheCollateralBlock);
        swap(first.nBlockLastPaid, second.nBlockLastPaid);
        swap(first.nProtocolVersion, second.nProtocolVersion);
        swap(first.nPoSeBanScore, second.nPoSeBanScore);
        swap(first.nPoSeBanHeight, second.nPoSeBanHeight);
        swap(first.fAllowMixingTx, second.fAllowMixingTx);
        swap(first.fUnitTest, second.fUnitTest);
        swap(first.mapGovernanceObjectsVotedOn, second.mapGovernanceObjectsVotedOn);
    }

    // CALCULATE A RANK AGAINST OF GIVEN BLOCK
    arith_uint256 CalculateScore(const uint256& blockHash);

    bool UpdateFromNewBroadcast(CGoldminenodeBroadcast& mnb);

    void Check(bool fForce = false);

    bool IsBroadcastedWithin(int nSeconds) { return GetAdjustedTime() - sigTime < nSeconds; }

    bool IsPingedWithin(int nSeconds, int64_t nTimeToCheckAt = -1)
    {
        if(lastPing == CGoldminenodePing()) return false;

        if(nTimeToCheckAt == -1) {
            nTimeToCheckAt = GetAdjustedTime();
        }
        return nTimeToCheckAt - lastPing.sigTime < nSeconds;
    }

    bool IsEnabled() { return nActiveState == GOLDMINENODE_ENABLED; }
    bool IsPreEnabled() { return nActiveState == GOLDMINENODE_PRE_ENABLED; }
    bool IsPoSeBanned() { return nActiveState == GOLDMINENODE_POSE_BAN; }
    // NOTE: this one relies on nPoSeBanScore, not on nActiveState as everything else here
    bool IsPoSeVerified() { return nPoSeBanScore <= -GOLDMINENODE_POSE_BAN_MAX_SCORE; }
    bool IsExpired() { return nActiveState == GOLDMINENODE_EXPIRED; }
    bool IsOutpointSpent() { return nActiveState == GOLDMINENODE_OUTPOINT_SPENT; }
    bool IsUpdateRequired() { return nActiveState == GOLDMINENODE_UPDATE_REQUIRED; }
    bool IsWatchdogExpired() { return nActiveState == GOLDMINENODE_WATCHDOG_EXPIRED; }
    bool IsNewStartRequired() { return nActiveState == GOLDMINENODE_NEW_START_REQUIRED; }

    static bool IsValidStateForAutoStart(int nActiveStateIn)
    {
        return  nActiveStateIn == GOLDMINENODE_ENABLED ||
                nActiveStateIn == GOLDMINENODE_PRE_ENABLED ||
                nActiveStateIn == GOLDMINENODE_EXPIRED ||
                nActiveStateIn == GOLDMINENODE_WATCHDOG_EXPIRED;
    }

    bool IsValidForPayment()
    {
        if(nActiveState == GOLDMINENODE_ENABLED) {
            return true;
        }
        if(!sporkManager.IsSporkActive(SPORK_14_REQUIRE_SENTINEL_FLAG) &&
           (nActiveState == GOLDMINENODE_WATCHDOG_EXPIRED)) {
            return true;
        }

        return false;
    }

    bool IsValidNetAddr();
    static bool IsValidNetAddr(CService addrIn);

    void IncreasePoSeBanScore() { if(nPoSeBanScore < GOLDMINENODE_POSE_BAN_MAX_SCORE) nPoSeBanScore++; }
    void DecreasePoSeBanScore() { if(nPoSeBanScore > -GOLDMINENODE_POSE_BAN_MAX_SCORE) nPoSeBanScore--; }

    goldminenode_info_t GetInfo();

    static std::string StateToString(int nStateIn);
    std::string GetStateString() const;
    std::string GetStatus() const;

    int GetCollateralAge();

    int GetLastPaidTime() { return nTimeLastPaid; }
    int GetLastPaidBlock() { return nBlockLastPaid; }
    void UpdateLastPaid(const CBlockIndex *pindex, int nMaxBlocksToScanBack);

    // KEEP TRACK OF EACH GOVERNANCE ITEM INCASE THIS NODE GOES OFFLINE, SO WE CAN RECALC THEIR STATUS
    void AddGovernanceVote(uint256 nGovernanceObjectHash);
    // RECALCULATE CACHED STATUS FLAGS FOR ALL AFFECTED OBJECTS
    void FlagGovernanceItemsAsDirty();

    void RemoveGovernanceObject(uint256 nGovernanceObjectHash);

    void UpdateWatchdogVoteTime();

    CGoldminenode& operator=(CGoldminenode from)
    {
        swap(*this, from);
        return *this;
    }
    friend bool operator==(const CGoldminenode& a, const CGoldminenode& b)
    {
        return a.vin == b.vin;
    }
    friend bool operator!=(const CGoldminenode& a, const CGoldminenode& b)
    {
        return !(a.vin == b.vin);
    }

};


//
// The Goldminenode Broadcast Class : Contains a different serialize method for sending goldminenodes through the network
//

class CGoldminenodeBroadcast : public CGoldminenode
{
public:

    bool fRecovery;

    CGoldminenodeBroadcast() : CGoldminenode(), fRecovery(false) {}
    CGoldminenodeBroadcast(const CGoldminenode& mn) : CGoldminenode(mn), fRecovery(false) {}
    CGoldminenodeBroadcast(CService addrNew, CTxIn vinNew, CPubKey pubKeyCollateralAddressNew, CPubKey pubKeyGoldminenodeNew, int nProtocolVersionIn) :
        CGoldminenode(addrNew, vinNew, pubKeyCollateralAddressNew, pubKeyGoldminenodeNew, nProtocolVersionIn), fRecovery(false) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(vin);
        READWRITE(addr);
        READWRITE(pubKeyCollateralAddress);
        READWRITE(pubKeyGoldminenode);
        READWRITE(vchSig);
        READWRITE(sigTime);
        READWRITE(nProtocolVersion);
        READWRITE(lastPing);
    }

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        //
        // REMOVE AFTER MIGRATION TO 12.1
        //
        if(nProtocolVersion < 70201) {
            ss << sigTime;
            ss << pubKeyCollateralAddress;
        } else {
        //
        // END REMOVE
        //
            ss << vin;
            ss << pubKeyCollateralAddress;
            ss << sigTime;
        }
        return ss.GetHash();
    }

    /// Create Goldminenode broadcast, needs to be relayed manually after that
    static bool Create(CTxIn vin, CService service, CKey keyCollateralAddressNew, CPubKey pubKeyCollateralAddressNew, CKey keyGoldminenodeNew, CPubKey pubKeyGoldminenodeNew, std::string &strErrorRet, CGoldminenodeBroadcast &mnbRet);
    static bool Create(std::string strService, std::string strKey, std::string strTxHash, std::string strOutputIndex, std::string& strErrorRet, CGoldminenodeBroadcast &mnbRet, bool fOffline = false);

    bool SimpleCheck(int& nDos);
    bool Update(CGoldminenode* pmn, int& nDos);
    bool CheckOutpoint(int& nDos);

    bool Sign(CKey& keyCollateralAddress);
    bool CheckSignature(int& nDos);
    void Relay();
};

class CGoldminenodeVerification
{
public:
    CTxIn vin1;
    CTxIn vin2;
    CService addr;
    int nonce;
    int nBlockHeight;
    std::vector<unsigned char> vchSig1;
    std::vector<unsigned char> vchSig2;

    CGoldminenodeVerification() :
        vin1(),
        vin2(),
        addr(),
        nonce(0),
        nBlockHeight(0),
        vchSig1(),
        vchSig2()
        {}

    CGoldminenodeVerification(CService addr, int nonce, int nBlockHeight) :
        vin1(),
        vin2(),
        addr(addr),
        nonce(nonce),
        nBlockHeight(nBlockHeight),
        vchSig1(),
        vchSig2()
        {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(vin1);
        READWRITE(vin2);
        READWRITE(addr);
        READWRITE(nonce);
        READWRITE(nBlockHeight);
        READWRITE(vchSig1);
        READWRITE(vchSig2);
    }

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vin1;
        ss << vin2;
        ss << addr;
        ss << nonce;
        ss << nBlockHeight;
        return ss.GetHash();
    }

    void Relay() const
    {
        CInv inv(MSG_GOLDMINENODE_VERIFY, GetHash());
        RelayInv(inv);
    }
};

#endif
