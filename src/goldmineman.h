// Copyright (c) 2015-2016 The Arctic developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef GOLDMINEMAN_H
#define GOLDMINEMAN_H

#include "sync.h"
#include "net.h"
#include "key.h"
#include "util.h"
#include "base58.h"
#include "main.h"
#include "goldmine.h"

#define GOLDMINES_DUMP_SECONDS               (15*60)
#define GOLDMINES_DSEG_SECONDS               (3*60*60)

using namespace std;

class CGoldmineMan;

extern CGoldmineMan gmineman;
void DumpGoldmines();

/** Access to the MN database (gmcache.dat)
 */
class CGoldmineDB
{
private:
    boost::filesystem::path pathMN;
    std::string strMagicMessage;
public:
    enum ReadResult {
        Ok,
        FileError,
        HashReadError,
        IncorrectHash,
        IncorrectMagicMessage,
        IncorrectMagicNumber,
        IncorrectFormat
    };

    CGoldmineDB();
    bool Write(const CGoldmineMan &gminemanToSave);
    ReadResult Read(CGoldmineMan& gminemanToLoad, bool fDryRun = false);
};

class CGoldmineMan
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    // critical section to protect the inner data structures specifically on messaging
    mutable CCriticalSection cs_process_message;

    // map to hold all MNs
    std::vector<CGoldmine> vGoldmines;
    // who's asked for the Goldmine list and the last time
    std::map<CNetAddr, int64_t> mAskedUsForGoldmineList;
    // who we asked for the Goldmine list and the last time
    std::map<CNetAddr, int64_t> mWeAskedForGoldmineList;
    // which Goldmines we've asked for
    std::map<COutPoint, int64_t> mWeAskedForGoldmineListEntry;

public:
    // Keep track of all broadcasts I've seen
    map<uint256, CGoldmineBroadcast> mapSeenGoldmineBroadcast;
    // Keep track of all pings I've seen
    map<uint256, CGoldminePing> mapSeenGoldminePing;
    
    // keep track of dsq count to prevent goldmines from gaming spysend queue
    int64_t nDsqCount;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        LOCK(cs);
        READWRITE(vGoldmines);
        READWRITE(mAskedUsForGoldmineList);
        READWRITE(mWeAskedForGoldmineList);
        READWRITE(mWeAskedForGoldmineListEntry);
        READWRITE(nDsqCount);

        READWRITE(mapSeenGoldmineBroadcast);
        READWRITE(mapSeenGoldminePing);
    }

    CGoldmineMan();
    CGoldmineMan(CGoldmineMan& other);

    /// Add an entry
    bool Add(CGoldmine &gm);

    /// Ask (source) node for gmb
    void AskForMN(CNode *pnode, CTxIn &vin);

    /// Check all Goldmines
    void Check();

    /// Check all Goldmines and remove inactive
    void CheckAndRemove(bool forceExpiredRemoval = false);

    /// Clear Goldmine vector
    void Clear();

    int CountEnabled(int protocolVersion = -1);

    void DsegUpdate(CNode* pnode);

    /// Find an entry
    CGoldmine* Find(const CScript &payee);
    CGoldmine* Find(const CTxIn& vin);
    CGoldmine* Find(const CPubKey& pubKeyGoldmine);

    /// Find an entry in the goldmine list that is next to be paid
    CGoldmine* GetNextGoldmineInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount);

    /// Find a random entry
    CGoldmine* FindRandomNotInVec(std::vector<CTxIn> &vecToExclude, int protocolVersion = -1);

    /// Get the current winner for this block
    CGoldmine* GetCurrentGoldMine(int mod=1, int64_t nBlockHeight=0, int minProtocol=0);

    std::vector<CGoldmine> GetFullGoldmineVector() { Check(); return vGoldmines; }

    std::vector<pair<int, CGoldmine> > GetGoldmineRanks(int64_t nBlockHeight, int minProtocol=0);
    int GetGoldmineRank(const CTxIn &vin, int64_t nBlockHeight, int minProtocol=0, bool fOnlyActive=true);
    CGoldmine* GetGoldmineByRank(int nRank, int64_t nBlockHeight, int minProtocol=0, bool fOnlyActive=true);

    void ProcessGoldmineConnections();

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);

    /// Return the number of (unique) Goldmines
    int size() { return vGoldmines.size(); }

    std::string ToString() const;

    void Remove(CTxIn vin);

    /// Update goldmine list and maps using provided CGoldmineBroadcast
    void UpdateGoldmineList(CGoldmineBroadcast gmb);
    /// Perform complete check and only then update list and maps
    bool CheckMnbAndUpdateGoldmineList(CGoldmineBroadcast gmb, int& nDos);

};

#endif
