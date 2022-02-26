// Copyright (c) 2017-2022 The Advanced Technology Coin
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef GOLDMINENODE_H
#define GOLDMINENODE_H

#include "key.h"
#include "validation.h"
#include "spork.h"

class CGoldminenode;
class CGoldminenodeBroadcast;
class CConnman;

static const int GOLDMINENODE_CHECK_SECONDS               =   5;
static const int GOLDMINENODE_MIN_MNB_SECONDS             =   5 * 60;
static const int GOLDMINENODE_MIN_MNP_SECONDS             =  10 * 60;
static const int GOLDMINENODE_SENTINEL_PING_MAX_SECONDS   =  60 * 60;
static const int GOLDMINENODE_EXPIRATION_SECONDS          = 120 * 60;
static const int GOLDMINENODE_NEW_START_REQUIRED_SECONDS  = 180 * 60;

static const int GOLDMINENODE_POSE_BAN_MAX_SCORE          = 5;

//
// The Goldminenode Ping Class : Contains a different serialize method for sending pings from goldminenodes throughout the network
//

// sentinel version before implementation of nSentinelVersion in CGoldminenodePing
#define DEFAULT_SENTINEL_VERSION 0x010001
// daemon version before implementation of nDaemonVersion in CGoldminenodePing
#define DEFAULT_DAEMON_VERSION 120200

class CGoldminenodePing
{
public:
    COutPoint goldminenodeOutpoint{};
    uint256 blockHash{};
    int64_t sigTime{}; //mnb message times
    std::vector<unsigned char> vchSig{};
    bool fSentinelIsCurrent = false; // true if last sentinel ping was current
    // MSB is always 0, other 3 bits corresponds to x.x.x version scheme
    uint32_t nSentinelVersion{DEFAULT_SENTINEL_VERSION};
    uint32_t nDaemonVersion{DEFAULT_DAEMON_VERSION};

    CGoldminenodePing() = default;

    CGoldminenodePing(const COutPoint& outpoint);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        int nVersion = s.GetVersion();
        if (nVersion == 70209 && (s.GetType() & SER_NETWORK)) {
            // converting from/to old format
            CTxIn txin{};
            if (ser_action.ForRead()) {
                READWRITE(txin);
                goldminenodeOutpoint = txin.prevout;
            } else {
                txin = CTxIn(goldminenodeOutpoint);
                READWRITE(txin);
            }
        } else {
            // using new format directly
            READWRITE(goldminenodeOutpoint);
        }
        READWRITE(blockHash);
        READWRITE(sigTime);
        if (!(s.GetType() & SER_GETHASH)) {
            READWRITE(vchSig);
        }
        if(ser_action.ForRead() && s.size() == 0) {
            // TODO: drop this after migration to 70209
            fSentinelIsCurrent = false;
            nSentinelVersion = DEFAULT_SENTINEL_VERSION;
            nDaemonVersion = DEFAULT_DAEMON_VERSION;
            return;
        }
        READWRITE(fSentinelIsCurrent);
        READWRITE(nSentinelVersion);
        if(ser_action.ForRead() && s.size() == 0) {
            // TODO: drop this after migration to 70209
            nDaemonVersion = DEFAULT_DAEMON_VERSION;
            return;
        }
        if (!(nVersion == 70209 && (s.GetType() & SER_NETWORK))) {
            READWRITE(nDaemonVersion);
        }
    }

    uint256 GetHash() const;
    uint256 GetSignatureHash() const;

    bool IsExpired() const { return GetAdjustedTime() - sigTime > GOLDMINENODE_NEW_START_REQUIRED_SECONDS; }

    bool Sign(const CKey& keyGoldminenode, const CPubKey& pubKeyGoldminenode);
    bool CheckSignature(const CPubKey& pubKeyGoldminenode, int &nDos) const;
    bool SimpleCheck(int& nDos);
    bool CheckAndUpdate(CGoldminenode* pmn, bool fFromNewBroadcast, int& nDos, CConnman& connman);
    void Relay(CConnman& connman);

    explicit operator bool() const;
};

inline bool operator==(const CGoldminenodePing& a, const CGoldminenodePing& b)
{
    return a.goldminenodeOutpoint == b.goldminenodeOutpoint && a.blockHash == b.blockHash;
}
inline bool operator!=(const CGoldminenodePing& a, const CGoldminenodePing& b)
{
    return !(a == b);
}
inline CGoldminenodePing::operator bool() const
{
    return *this != CGoldminenodePing();
}

struct goldminenode_info_t
{
    // Note: all these constructors can be removed once C++14 is enabled.
    // (in C++11 the member initializers wrongly disqualify this as an aggregate)
    goldminenode_info_t() = default;
    goldminenode_info_t(goldminenode_info_t const&) = default;

    goldminenode_info_t(int activeState, int protoVer, int64_t sTime) :
        nActiveState{activeState}, nProtocolVersion{protoVer}, sigTime{sTime} {}

    goldminenode_info_t(int activeState, int protoVer, int64_t sTime,
                      COutPoint const& outpnt, CService const& addr,
                      CPubKey const& pkCollAddr, CPubKey const& pkMN) :
        nActiveState{activeState}, nProtocolVersion{protoVer}, sigTime{sTime},
        outpoint{outpnt}, addr{addr},
        pubKeyCollateralAddress{pkCollAddr}, pubKeyGoldminenode{pkMN} {}

    int nActiveState = 0;
    int nProtocolVersion = 0;
    int64_t sigTime = 0; //mnb message time
    int64_t enableTime = 0; //mnb message time

    COutPoint outpoint{};
    CService addr{};
    CPubKey pubKeyCollateralAddress{};
    CPubKey pubKeyGoldminenode{};
    int64_t nTimeLastWatchdogVote = 0;

    int64_t nLastDsq = 0; //the dsq count from the last dsq broadcast of this node
    int64_t nTimeLastChecked = 0;
    int64_t nTimeLastPaid = 0;
    int64_t nTimeLastPing = 0; //* not in CMN
    bool fInfoValid = false; //* not in CMN
};

//
// The Goldminenode Class. For managing the Darksend process. It contains the input of the 1000DRK, signature to prove
// it's the one who own that ip address and code for calculating the payment election.
//
class CGoldminenode : public goldminenode_info_t
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
        GOLDMINENODE_SENTINEL_PING_EXPIRED,
        GOLDMINENODE_NEW_START_REQUIRED,
        GOLDMINENODE_POSE_BAN
    };

    enum CollateralStatus {
        COLLATERAL_OK,
        COLLATERAL_UTXO_NOT_FOUND,
        COLLATERAL_INVALID_AMOUNT,
        COLLATERAL_INVALID_PUBKEY
    };


    CGoldminenodePing lastPing{};
    std::vector<unsigned char> vchSig{};

    uint256 nCollateralMinConfBlockHash{};
    int nBlockLastPaid{};
    int nPoSeBanScore{};
    int nPoSeBanHeight{};
    bool fAllowMixingTx{};
    bool fUnitTest = false;

    // KEEP TRACK OF GOVERNANCE ITEMS EACH GOLDMINENODE HAS VOTE UPON FOR RECALCULATION
    std::map<uint256, int> mapGovernanceObjectsVotedOn;

    CGoldminenode();
    CGoldminenode(const CGoldminenode& other);
    CGoldminenode(const CGoldminenodeBroadcast& mnb);
    CGoldminenode(CService addrNew, COutPoint outpointNew, CPubKey pubKeyCollateralAddressNew, CPubKey pubKeyGoldminenodeNew, int nProtocolVersionIn);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        LOCK(cs);
        int nVersion = s.GetVersion();
        if (nVersion == 70209 && (s.GetType() & SER_NETWORK)) {
            // converting from/to old format
            CTxIn txin{};
            if (ser_action.ForRead()) {
                READWRITE(txin);
                outpoint = txin.prevout;
            } else {
                txin = CTxIn(outpoint);
                READWRITE(txin);
            }
        } else {
            // using new format directly
            READWRITE(outpoint);
        }
        READWRITE(addr);
        READWRITE(pubKeyCollateralAddress);
        READWRITE(pubKeyGoldminenode);
        READWRITE(lastPing);
        READWRITE(vchSig);
        READWRITE(sigTime);
        READWRITE(enableTime);
        READWRITE(nLastDsq);
        READWRITE(nTimeLastChecked);
        READWRITE(nTimeLastPaid);
        READWRITE(nTimeLastWatchdogVote);
        READWRITE(nActiveState);
        READWRITE(nCollateralMinConfBlockHash);
        READWRITE(nBlockLastPaid);
        READWRITE(nProtocolVersion);
        READWRITE(nPoSeBanScore);
        READWRITE(nPoSeBanHeight);
        READWRITE(fAllowMixingTx);
        READWRITE(fUnitTest);
        READWRITE(mapGovernanceObjectsVotedOn);
    }

    // CALCULATE A RANK AGAINST OF GIVEN BLOCK
    arith_uint256 CalculateScore(const uint256& blockHash) const;

    bool UpdateFromNewBroadcast(CGoldminenodeBroadcast& mnb, CConnman& connman);

    static CollateralStatus CheckCollateral(const COutPoint& outpoint, const CPubKey& pubkey);
    static CollateralStatus CheckCollateral(const COutPoint& outpoint, const CPubKey& pubkey, int& nHeightRet);
    void Check(bool fForce = false);

    bool IsBroadcastedWithin(int nSeconds) { return GetAdjustedTime() - sigTime < nSeconds; }

    bool IsPingedWithin(int nSeconds, int64_t nTimeToCheckAt = -1)
    {
        if(!lastPing) return false;

        if(nTimeToCheckAt == -1) {
            nTimeToCheckAt = GetAdjustedTime();
        }
        return nTimeToCheckAt - lastPing.sigTime < nSeconds;
    }

    bool IsEnabled() const { return nActiveState == GOLDMINENODE_ENABLED; }
    bool IsPreEnabled() const { return nActiveState == GOLDMINENODE_PRE_ENABLED; }
    bool IsPoSeBanned() const { return nActiveState == GOLDMINENODE_POSE_BAN; }
    // NOTE: this one relies on nPoSeBanScore, not on nActiveState as everything else here
    bool IsPoSeVerified() const { return nPoSeBanScore <= -GOLDMINENODE_POSE_BAN_MAX_SCORE; }
    bool IsExpired() const { return nActiveState == GOLDMINENODE_EXPIRED; }
    bool IsOutpointSpent() const { return nActiveState == GOLDMINENODE_OUTPOINT_SPENT; }
    bool IsUpdateRequired() const { return nActiveState == GOLDMINENODE_UPDATE_REQUIRED; }
    bool IsSentinelPingExpired() const { return nActiveState == GOLDMINENODE_SENTINEL_PING_EXPIRED; }
    bool IsNewStartRequired() const { return nActiveState == GOLDMINENODE_NEW_START_REQUIRED; }

    static bool IsValidStateForAutoStart(int nActiveStateIn)
    {
        return  nActiveStateIn == GOLDMINENODE_ENABLED ||
                nActiveStateIn == GOLDMINENODE_PRE_ENABLED ||
                nActiveStateIn == GOLDMINENODE_EXPIRED ||
                nActiveStateIn == GOLDMINENODE_SENTINEL_PING_EXPIRED;
    }

    bool IsValidForPayment() const
    {
        if(nActiveState == GOLDMINENODE_ENABLED) {
            return true;
        }
        if(!sporkManager.IsSporkActive(SPORK_14_REQUIRE_SENTINEL_FLAG) &&
           (nActiveState == GOLDMINENODE_SENTINEL_PING_EXPIRED)) {
            return true;
        }

        return false;
    }

    bool IsValidNetAddr();
    static bool IsValidNetAddr(CService addrIn);

    void IncreasePoSeBanScore() { if(nPoSeBanScore < GOLDMINENODE_POSE_BAN_MAX_SCORE) nPoSeBanScore++; }
    void DecreasePoSeBanScore() { if(nPoSeBanScore > -GOLDMINENODE_POSE_BAN_MAX_SCORE) nPoSeBanScore--; }
    void PoSeBan() { nPoSeBanScore = GOLDMINENODE_POSE_BAN_MAX_SCORE; }

    goldminenode_info_t GetInfo() const;

    static std::string StateToString(int nStateIn);
    std::string GetStateString() const;
    std::string GetStatus() const;

    int GetLastPaidTime() const { return nTimeLastPaid; }
    int GetLastPaidBlock() const { return nBlockLastPaid; }
    void UpdateLastPaid(const CBlockIndex *pindex, int nMaxBlocksToScanBack);

    void UpdateWatchdogVoteTime(uint64_t nVoteTime = 0);

    CGoldminenode& operator=(CGoldminenode const& from)
    {
        static_cast<goldminenode_info_t&>(*this)=from;
        lastPing = from.lastPing;
        vchSig = from.vchSig;
        nCollateralMinConfBlockHash = from.nCollateralMinConfBlockHash;
        nBlockLastPaid = from.nBlockLastPaid;
        nPoSeBanScore = from.nPoSeBanScore;
        nPoSeBanHeight = from.nPoSeBanHeight;
        fAllowMixingTx = from.fAllowMixingTx;
        fUnitTest = from.fUnitTest;
        mapGovernanceObjectsVotedOn = from.mapGovernanceObjectsVotedOn;
        return *this;
    }
};

inline bool operator==(const CGoldminenode& a, const CGoldminenode& b)
{
    return a.outpoint == b.outpoint;
}
inline bool operator!=(const CGoldminenode& a, const CGoldminenode& b)
{
    return !(a.outpoint == b.outpoint);
}


//
// The Goldminenode Broadcast Class : Contains a different serialize method for sending goldminenodes through the network
//

class CGoldminenodeBroadcast : public CGoldminenode
{
public:

    bool fRecovery;

    CGoldminenodeBroadcast() : CGoldminenode(), fRecovery(false) {}
    CGoldminenodeBroadcast(const CGoldminenode& mn) : CGoldminenode(mn), fRecovery(false) {}
    CGoldminenodeBroadcast(CService addrNew, COutPoint outpointNew, CPubKey pubKeyCollateralAddressNew, CPubKey pubKeyGoldminenodeNew, int nProtocolVersionIn) :
        CGoldminenode(addrNew, outpointNew, pubKeyCollateralAddressNew, pubKeyGoldminenodeNew, nProtocolVersionIn), fRecovery(false) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        int nVersion = s.GetVersion();
        if (nVersion == 70209 && (s.GetType() & SER_NETWORK)) {
            // converting from/to old format
            CTxIn txin{};
            if (ser_action.ForRead()) {
                READWRITE(txin);
                outpoint = txin.prevout;
            } else {
                txin = CTxIn(outpoint);
                READWRITE(txin);
            }
        } else {
            // using new format directly
            READWRITE(outpoint);
        }
        READWRITE(addr);
        READWRITE(pubKeyCollateralAddress);
        READWRITE(pubKeyGoldminenode);
        if (!(s.GetType() & SER_GETHASH)) {
            READWRITE(vchSig);
        }
        READWRITE(sigTime);
        READWRITE(enableTime);
        READWRITE(nProtocolVersion);
        if (!(s.GetType() & SER_GETHASH)) {
            READWRITE(lastPing);
        }
    }

    uint256 GetHash() const;
    uint256 GetSignatureHash() const;

    /// Create Goldminenode broadcast, needs to be relayed manually after that
    static bool Create(const COutPoint& outpoint, const CService& service, const CKey& keyCollateralAddressNew, const CPubKey& pubKeyCollateralAddressNew, const CKey& keyGoldminenodeNew, const CPubKey& pubKeyGoldminenodeNew, std::string &strErrorRet, CGoldminenodeBroadcast &mnbRet);
    static bool Create(const std::string& strService, const std::string& strKey, const std::string& strTxHash, const std::string& strOutputIndex, std::string& strErrorRet, CGoldminenodeBroadcast &mnbRet, bool fOffline = false);

    bool SimpleCheck(int& nDos);
    bool Update(CGoldminenode* pmn, int& nDos, CConnman& connman);
    bool CheckOutpoint(int& nDos);

    bool Sign(const CKey& keyCollateralAddress);
    bool CheckSignature(int& nDos) const;
    void Relay(CConnman& connman) const;
};

class CGoldminenodeVerification
{
public:
    COutPoint goldminenodeOutpoint1{};
    COutPoint goldminenodeOutpoint2{};
    CService addr{};
    int nonce{};
    int nBlockHeight{};
    std::vector<unsigned char> vchSig1{};
    std::vector<unsigned char> vchSig2{};

    CGoldminenodeVerification() = default;

    CGoldminenodeVerification(CService addr, int nonce, int nBlockHeight) :
        addr(addr),
        nonce(nonce),
        nBlockHeight(nBlockHeight)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        int nVersion = s.GetVersion();
        if (nVersion == 70209 && (s.GetType() & SER_NETWORK)) {
            // converting from/to old format
            CTxIn txin1{};
            CTxIn txin2{};
            if (ser_action.ForRead()) {
                READWRITE(txin1);
                READWRITE(txin2);
                goldminenodeOutpoint1 = txin1.prevout;
                goldminenodeOutpoint2 = txin2.prevout;
            } else {
                txin1 = CTxIn(goldminenodeOutpoint1);
                txin2 = CTxIn(goldminenodeOutpoint2);
                READWRITE(txin1);
                READWRITE(txin2);
            }
        } else {
            // using new format directly
            READWRITE(goldminenodeOutpoint1);
            READWRITE(goldminenodeOutpoint2);
        }
        READWRITE(addr);
        READWRITE(nonce);
        READWRITE(nBlockHeight);
        READWRITE(vchSig1);
        READWRITE(vchSig2);
    }

    uint256 GetHash() const
    {
        // Note: doesn't match serialization

        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        // adding dummy values here to match old hashing format
        ss << goldminenodeOutpoint1 << uint8_t{} << 0xffffffff;
        ss << goldminenodeOutpoint2 << uint8_t{} << 0xffffffff;
        ss << addr;
        ss << nonce;
        ss << nBlockHeight;
        return ss.GetHash();
    }

    uint256 GetSignatureHash1(const uint256& blockHash) const
    {
        // Note: doesn't match serialization

        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << addr;
        ss << nonce;
        ss << blockHash;
        return ss.GetHash();
    }

    uint256 GetSignatureHash2(const uint256& blockHash) const
    {
        // Note: doesn't match serialization

        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << goldminenodeOutpoint1;
        ss << goldminenodeOutpoint2;
        ss << addr;
        ss << nonce;
        ss << blockHash;
        return ss.GetHash();
    }

    void Relay() const
    {
        CInv inv(MSG_GOLDMINENODE_VERIFY, GetHash());
        g_connman->RelayInv(inv);
    }
};

#endif
