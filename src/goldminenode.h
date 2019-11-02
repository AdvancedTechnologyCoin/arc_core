// Copyright (c) 2015-2017 The ARC developers
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
static const int GOLDMINENODE_EXPIRATION_SECONDS          =  65 * 60;
static const int GOLDMINENODE_WATCHDOG_MAX_SECONDS        = 120 * 60;
static const int GOLDMINENODE_NEW_START_REQUIRED_SECONDS  = 180 * 60;

static const int GOLDMINENODE_POSE_BAN_MAX_SCORE          = 5;

//
// The Goldminenode Ping Class : Contains a different serialize method for sending pings from goldminenodes throughout the network
//

// sentinel version before sentinel ping implementation
#define DEFAULT_SENTINEL_VERSION 0x010001

class CGoldminenodePing
{
public:
    CTxIn vin{};
    uint256 blockHash{};
    int64_t sigTime{}; //mnb message times
    std::vector<unsigned char> vchSig{};
    bool fSentinelIsCurrent = false; // true if last sentinel ping was actual
    // MSB is always 0, other 3 bits corresponds to x.x.x version scheme
    uint32_t nSentinelVersion{DEFAULT_SENTINEL_VERSION};

    CGoldminenodePing() = default;

    CGoldminenodePing(const COutPoint& outpoint);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(vin);
        READWRITE(blockHash);
        READWRITE(sigTime);
        READWRITE(vchSig);
		
		if(sporkManager.IsSporkActive(SPORK_10_GOLDMINENODE_PAY_UPDATED_NODES))
		{
            if(ser_action.ForRead() && (s.size() == 0))
            {
                fSentinelIsCurrent = false;
                nSentinelVersion = DEFAULT_SENTINEL_VERSION;
                return;
            }
            READWRITE(fSentinelIsCurrent);
            READWRITE(nSentinelVersion);
        }
    }

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vin;
        ss << sigTime;
        return ss.GetHash();
    }

    bool IsExpired() const { return GetAdjustedTime() - sigTime > GOLDMINENODE_NEW_START_REQUIRED_SECONDS; }

    bool Sign(const CKey& keyGoldminenode, const CPubKey& pubKeyGoldminenode);
    bool CheckSignature(CPubKey& pubKeyGoldminenode, int &nDos);
    bool SimpleCheck(int& nDos);
    bool CheckAndUpdate(CGoldminenode* pmn, bool fFromNewBroadcast, int& nDos, CConnman& connman);
    void Relay(CConnman& connman);
};

inline bool operator==(const CGoldminenodePing& a, const CGoldminenodePing& b)
{
    return a.vin == b.vin && a.blockHash == b.blockHash;
}
inline bool operator!=(const CGoldminenodePing& a, const CGoldminenodePing& b)
{
    return !(a == b);
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
                      COutPoint const& outpoint, CService const& addr,
                      CPubKey const& pkCollAddr, CPubKey const& pkMN,
                      int64_t tWatchdogV = 0) :
        nActiveState{activeState}, nProtocolVersion{protoVer}, sigTime{sTime},
        vin{outpoint}, addr{addr},
        pubKeyCollateralAddress{pkCollAddr}, pubKeyGoldminenode{pkMN},
        nTimeLastWatchdogVote{tWatchdogV} {}

    int nActiveState = 0;
    int nProtocolVersion = 0;
    int64_t sigTime = 0; //mnb message time
    int64_t enableTime = 0; //mnb message time

    CTxIn vin{};
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
// The Goldminenode Class. For managing the SpySend process. It contains the input of the 1000DRK, signature to prove
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
        GOLDMINENODE_WATCHDOG_EXPIRED,
        GOLDMINENODE_NEW_START_REQUIRED,
        GOLDMINENODE_POSE_BAN
    };

    enum CollateralStatus {
        COLLATERAL_OK,
        COLLATERAL_UTXO_NOT_FOUND,
        COLLATERAL_INVALID_AMOUNT
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
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        LOCK(cs);
        READWRITE(vin);
        READWRITE(addr);
        READWRITE(pubKeyCollateralAddress);
        READWRITE(pubKeyGoldminenode);
        READWRITE(lastPing);
        READWRITE(vchSig);
        READWRITE(sigTime);
        if(nProtocolVersion == 70208) {
        READWRITE(enableTime);	
        }
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
    arith_uint256 CalculateScore(const uint256& blockHash);

    bool UpdateFromNewBroadcast(CGoldminenodeBroadcast& mnb, CConnman& connman);

    static CollateralStatus CheckCollateral(const COutPoint& outpoint);
    static CollateralStatus CheckCollateral(const COutPoint& outpoint, int& nHeightRet);
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

    /// Is the input associated with collateral public key? (and there is 1000 ARCTIC - checking if valid goldminenode)
    bool IsInputAssociatedWithPubkey();

    bool IsValidNetAddr();
    static bool IsValidNetAddr(CService addrIn);

    void IncreasePoSeBanScore() { if(nPoSeBanScore < GOLDMINENODE_POSE_BAN_MAX_SCORE) nPoSeBanScore++; }
    void DecreasePoSeBanScore() { if(nPoSeBanScore > -GOLDMINENODE_POSE_BAN_MAX_SCORE) nPoSeBanScore--; }
    void PoSeBan() { nPoSeBanScore = GOLDMINENODE_POSE_BAN_MAX_SCORE; }

    goldminenode_info_t GetInfo();

    static std::string StateToString(int nStateIn);
    std::string GetStateString() const;
    std::string GetStatus() const;

    int GetLastPaidTime() { return nTimeLastPaid; }
    int GetLastPaidBlock() { return nBlockLastPaid; }
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
    return a.vin == b.vin;
}
inline bool operator!=(const CGoldminenode& a, const CGoldminenode& b)
{
    return !(a.vin == b.vin);
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
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(vin);
        READWRITE(addr);
        READWRITE(pubKeyCollateralAddress);
        READWRITE(pubKeyGoldminenode);
        READWRITE(vchSig);
        READWRITE(sigTime);
		if(sporkManager.IsSporkActive(SPORK_10_GOLDMINENODE_PAY_UPDATED_NODES))
		{
			if(nProtocolVersion == 70208) {
				READWRITE(enableTime);
			}
        }
        READWRITE(nProtocolVersion);
        READWRITE(lastPing);
    }

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vin;
        ss << pubKeyCollateralAddress;
        ss << sigTime;
		if(sporkManager.IsSporkActive(SPORK_10_GOLDMINENODE_PAY_UPDATED_NODES))
		{
			if(nProtocolVersion == 70208) {
			ss << enableTime;
			}
		}
        return ss.GetHash();
    }

    /// Create Goldminenode broadcast, needs to be relayed manually after that
    static bool Create(const COutPoint& outpoint, const CService& service, const CKey& keyCollateralAddressNew, const CPubKey& pubKeyCollateralAddressNew, const CKey& keyGoldminenodeNew, const CPubKey& pubKeyGoldminenodeNew, std::string &strErrorRet, CGoldminenodeBroadcast &mnbRet);
    static bool Create(std::string strService, std::string strKey, std::string strTxHash, std::string strOutputIndex, std::string& strErrorRet, CGoldminenodeBroadcast &mnbRet, bool fOffline = false);

    bool SimpleCheck(int& nDos);
    bool Update(CGoldminenode* pmn, int& nDos, CConnman& connman);
    bool CheckOutpoint(int& nDos);

    bool Sign(const CKey& keyCollateralAddress);
    bool CheckSignature(int& nDos);
    void Relay(CConnman& connman);
};

class CGoldminenodeVerification
{
public:
    CTxIn vin1{};
    CTxIn vin2{};
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
        g_connman->RelayInv(inv);
    }
};

#endif
