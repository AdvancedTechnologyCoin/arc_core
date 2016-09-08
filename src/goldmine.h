
// Copyright (c) 2015-2016 The Arctic developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef GOLDMINE_H
#define GOLDMINE_H

#include "sync.h"
#include "net.h"
#include "key.h"
#include "util.h"
#include "base58.h"
#include "main.h"
#include "timedata.h"

#define GOLDMINE_MIN_CONFIRMATIONS           15
#define GOLDMINE_MIN_MNP_SECONDS             (10*60)
#define GOLDMINE_MIN_MNB_SECONDS             (5*60)
#define GOLDMINE_PING_SECONDS                (5*60)
#define GOLDMINE_EXPIRATION_SECONDS          (65*60)
#define GOLDMINE_REMOVAL_SECONDS             (75*60)
#define GOLDMINE_CHECK_SECONDS               5

using namespace std;

class CGoldmine;
class CGoldmineBroadcast;
class CGoldminePing;
extern map<int64_t, uint256> mapCacheBlockHashes;

bool GetBlockHash(uint256& hash, int nBlockHeight);


//
// The Goldmine Ping Class : Contains a different serialize method for sending pings from goldmines throughout the network
//

class CGoldminePing
{
public:

    CTxIn vin;
    uint256 blockHash;
    int64_t sigTime; //gmb message times
    std::vector<unsigned char> vchSig;
    //removed stop

    CGoldminePing();
    CGoldminePing(CTxIn& newVin);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(vin);
        READWRITE(blockHash);
        READWRITE(sigTime);
        READWRITE(vchSig);
    }

    bool CheckAndUpdate(int& nDos, bool fRequireEnabled = true, bool fCheckSigTimeOnly = false);
    bool Sign(CKey& keyGoldmine, CPubKey& pubKeyGoldmine);
    bool VerifySignature(CPubKey& pubKeyGoldmine, int &nDos);
    void Relay();

    uint256 GetHash(){
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vin;
        ss << sigTime;
        return ss.GetHash();
    }

    void swap(CGoldminePing& first, CGoldminePing& second) // nothrow
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

    CGoldminePing& operator=(CGoldminePing from)
    {
        swap(*this, from);
        return *this;
    }
    friend bool operator==(const CGoldminePing& a, const CGoldminePing& b)
    {
        return a.vin == b.vin && a.blockHash == b.blockHash;
    }
    friend bool operator!=(const CGoldminePing& a, const CGoldminePing& b)
    {
        return !(a == b);
    }

};


//
// The Goldmine Class. For managing the Spysend process. It contains the input of the 1000ARC, signature to prove
// it's the one who own that ip address and code for calculating the payment election.
//
class CGoldmine
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;
    int64_t lastTimeChecked;
public:
    enum state {
        GOLDMINE_ENABLED = 1,
        GOLDMINE_EXPIRED = 2,
        GOLDMINE_VIN_SPENT = 3,
        GOLDMINE_REMOVE = 4,
        GOLDMINE_POS_ERROR = 5
    };

    CTxIn vin;
    CService addr;
    CPubKey pubkey;
    CPubKey pubkey2;
    std::vector<unsigned char> sig;
    int activeState;
    int64_t sigTime; //gmb message time
    int cacheInputAge;
    int cacheInputAgeBlock;
    bool unitTest;
    bool allowFreeTx;
    int protocolVersion;
    int64_t nLastDsq; //the dsq count from the last dsq broadcast of this node
    int nScanningErrorCount;
    int nLastScanningErrorBlockHeight;
    CGoldminePing lastPing;

    CGoldmine();
    CGoldmine(const CGoldmine& other);
    CGoldmine(const CGoldmineBroadcast& gmb);


    void swap(CGoldmine& first, CGoldmine& second) // nothrow
    {
        // enable ADL (not necessary in our case, but good practice)
        using std::swap;

        // by swapping the members of two classes,
        // the two classes are effectively swapped
        swap(first.vin, second.vin);
        swap(first.addr, second.addr);
        swap(first.pubkey, second.pubkey);
        swap(first.pubkey2, second.pubkey2);
        swap(first.sig, second.sig);
        swap(first.activeState, second.activeState);
        swap(first.sigTime, second.sigTime);
        swap(first.lastPing, second.lastPing);
        swap(first.cacheInputAge, second.cacheInputAge);
        swap(first.cacheInputAgeBlock, second.cacheInputAgeBlock);
        swap(first.unitTest, second.unitTest);
        swap(first.allowFreeTx, second.allowFreeTx);
        swap(first.protocolVersion, second.protocolVersion);
        swap(first.nLastDsq, second.nLastDsq);
        swap(first.nScanningErrorCount, second.nScanningErrorCount);
        swap(first.nLastScanningErrorBlockHeight, second.nLastScanningErrorBlockHeight);
    }

    CGoldmine& operator=(CGoldmine from)
    {
        swap(*this, from);
        return *this;
    }
    friend bool operator==(const CGoldmine& a, const CGoldmine& b)
    {
        return a.vin == b.vin;
    }
    friend bool operator!=(const CGoldmine& a, const CGoldmine& b)
    {
        return !(a.vin == b.vin);
    }

    uint256 CalculateScore(int mod=1, int64_t nBlockHeight=0);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
            LOCK(cs);

            READWRITE(vin);
            READWRITE(addr);
            READWRITE(pubkey);
            READWRITE(pubkey2);
            READWRITE(sig);
            READWRITE(sigTime);
            READWRITE(protocolVersion);
            READWRITE(activeState);
            READWRITE(lastPing);
            READWRITE(cacheInputAge);
            READWRITE(cacheInputAgeBlock);
            READWRITE(unitTest);
            READWRITE(allowFreeTx);
            READWRITE(nLastDsq);
            READWRITE(nScanningErrorCount);
            READWRITE(nLastScanningErrorBlockHeight);
    }

    int64_t SecondsSincePayment();

    bool UpdateFromNewBroadcast(CGoldmineBroadcast& gmb);

    inline uint64_t SliceHash(uint256& hash, int slice)
    {
        uint64_t n = 0;
        memcpy(&n, &hash+slice*64, 64);
        return n;
    }

    void Check(bool forceCheck = false);

    bool IsBroadcastedWithin(int seconds)
    {
        return (GetAdjustedTime() - sigTime) < seconds;
    }

    bool IsPingedWithin(int seconds, int64_t now = -1)
    {
        now == -1 ? now = GetAdjustedTime() : now;

        return (lastPing == CGoldminePing())
                ? false
                : now - lastPing.sigTime < seconds;
    }

    void Disable()
    {
        sigTime = 0;
        lastPing = CGoldminePing();
    }

    bool IsEnabled()
    {
        return activeState == GOLDMINE_ENABLED;
    }

    int GetGoldmineInputAge()
    {
        if(chainActive.Tip() == NULL) return 0;

        if(cacheInputAge == 0){
            cacheInputAge = GetInputAge(vin);
            cacheInputAgeBlock = chainActive.Tip()->nHeight;
        }

        return cacheInputAge+(chainActive.Tip()->nHeight-cacheInputAgeBlock);
    }

    std::string Status() {
        std::string strStatus = "ACTIVE";

        if(activeState == CGoldmine::GOLDMINE_ENABLED) strStatus   = "ENABLED";
        if(activeState == CGoldmine::GOLDMINE_EXPIRED) strStatus   = "EXPIRED";
        if(activeState == CGoldmine::GOLDMINE_VIN_SPENT) strStatus = "VIN_SPENT";
        if(activeState == CGoldmine::GOLDMINE_REMOVE) strStatus    = "REMOVE";
        if(activeState == CGoldmine::GOLDMINE_POS_ERROR) strStatus = "POS_ERROR";

        return strStatus;
    }

    int64_t GetLastPaid();

};


//
// The Goldmine Broadcast Class : Contains a different serialize method for sending goldmines through the network
//

class CGoldmineBroadcast : public CGoldmine
{
public:
    CGoldmineBroadcast();
    CGoldmineBroadcast(CService newAddr, CTxIn newVin, CPubKey newPubkey, CPubKey newPubkey2, int protocolVersionIn);
    CGoldmineBroadcast(const CGoldmine& gm);

    bool CheckAndUpdate(int& nDoS);
    bool CheckInputsAndAdd(int& nDos);
    bool Sign(CKey& keyCollateralAddress);
    bool VerifySignature();
    void Relay();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(vin);
        READWRITE(addr);
        READWRITE(pubkey);
        READWRITE(pubkey2);
        READWRITE(sig);
        READWRITE(sigTime);
        READWRITE(protocolVersion);
        READWRITE(lastPing);
        READWRITE(nLastDsq);
    }

    uint256 GetHash(){
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << sigTime;
        ss << pubkey;
        return ss.GetHash();
    }

};

#endif
