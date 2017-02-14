// Copyright (c) 2015-2017 The Arctic Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef GOLDMINENODEMAN_H
#define GOLDMINENODEMAN_H

#include "goldminenode.h"
#include "sync.h"

using namespace std;

class CGoldminenodeMan;

extern CGoldminenodeMan mnodeman;

/**
 * Provides a forward and reverse index between MN vin's and integers.
 *
 * This mapping is normally add-only and is expected to be permanent
 * It is only rebuilt if the size of the index exceeds the expected maximum number
 * of MN's and the current number of known MN's.
 *
 * The external interface to this index is provided via delegation by CGoldminenodeMan
 */
class CGoldminenodeIndex
{
public: // Types
    typedef std::map<CTxIn,int> index_m_t;

    typedef index_m_t::iterator index_m_it;

    typedef index_m_t::const_iterator index_m_cit;

    typedef std::map<int,CTxIn> rindex_m_t;

    typedef rindex_m_t::iterator rindex_m_it;

    typedef rindex_m_t::const_iterator rindex_m_cit;

private:
    int                  nSize;

    index_m_t            mapIndex;

    rindex_m_t           mapReverseIndex;

public:
    CGoldminenodeIndex();

    int GetSize() const {
        return nSize;
    }

    /// Retrieve goldminenode vin by index
    bool Get(int nIndex, CTxIn& vinGoldminenode) const;

    /// Get index of a goldminenode vin
    int GetGoldminenodeIndex(const CTxIn& vinGoldminenode) const;

    void AddGoldminenodeVIN(const CTxIn& vinGoldminenode);

    void Clear();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(mapIndex);
        if(ser_action.ForRead()) {
            RebuildIndex();
        }
    }

private:
    void RebuildIndex();

};

class CGoldminenodeMan
{
public:
    typedef std::map<CTxIn,int> index_m_t;

    typedef index_m_t::iterator index_m_it;

    typedef index_m_t::const_iterator index_m_cit;

private:
    static const int MAX_EXPECTED_INDEX_SIZE = 30000;

    /// Only allow 1 index rebuild per hour
    static const int64_t MIN_INDEX_REBUILD_TIME = 3600;

    static const std::string SERIALIZATION_VERSION_STRING;

    static const int DSEG_UPDATE_SECONDS        = 3 * 60 * 60;

    static const int LAST_PAID_SCAN_BLOCKS      = 100;

    static const int MIN_POSE_PROTO_VERSION     = 70203;
    static const int MAX_POSE_RANK              = 10;
    static const int MAX_POSE_BLOCKS            = 10;

    static const int MNB_RECOVERY_QUORUM_TOTAL      = 10;
    static const int MNB_RECOVERY_QUORUM_REQUIRED   = 6;
    static const int MNB_RECOVERY_MAX_ASK_ENTRIES   = 10;
    static const int MNB_RECOVERY_WAIT_SECONDS      = 60;
    static const int MNB_RECOVERY_RETRY_SECONDS     = 3 * 60 * 60;


    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    // Keep track of current block index
    const CBlockIndex *pCurrentBlockIndex;

    // map to hold all MNs
    std::vector<CGoldminenode> vGoldminenodes;
    // who's asked for the Goldminenode list and the last time
    std::map<CNetAddr, int64_t> mAskedUsForGoldminenodeList;
    // who we asked for the Goldminenode list and the last time
    std::map<CNetAddr, int64_t> mWeAskedForGoldminenodeList;
    // which Goldminenodes we've asked for
    std::map<COutPoint, std::map<CNetAddr, int64_t> > mWeAskedForGoldminenodeListEntry;
    // who we asked for the goldminenode verification
    std::map<CNetAddr, CGoldminenodeVerification> mWeAskedForVerification;

    // these maps are used for goldminenode recovery from GOLDMINENODE_NEW_START_REQUIRED state
    std::map<uint256, std::pair< int64_t, std::set<CNetAddr> > > mMnbRecoveryRequests;
    std::map<uint256, std::vector<CGoldminenodeBroadcast> > mMnbRecoveryGoodReplies;
    std::list< std::pair<CService, uint256> > listScheduledMnbRequestConnections;

    int64_t nLastIndexRebuildTime;

    CGoldminenodeIndex indexGoldminenodes;

    CGoldminenodeIndex indexGoldminenodesOld;

    /// Set when index has been rebuilt, clear when read
    bool fIndexRebuilt;

    /// Set when goldminenodes are added, cleared when CGovernanceManager is notified
    bool fGoldminenodesAdded;

    /// Set when goldminenodes are removed, cleared when CGovernanceManager is notified
    bool fGoldminenodesRemoved;

    std::vector<uint256> vecDirtyGovernanceObjectHashes;

    int64_t nLastWatchdogVoteTime;

    friend class CGoldminenodeSync;

public:
    // Keep track of all broadcasts I've seen
    std::map<uint256, std::pair<int64_t, CGoldminenodeBroadcast> > mapSeenGoldminenodeBroadcast;
    // Keep track of all pings I've seen
    std::map<uint256, CGoldminenodePing> mapSeenGoldminenodePing;
    // Keep track of all verifications I've seen
    std::map<uint256, CGoldminenodeVerification> mapSeenGoldminenodeVerification;
    // keep track of dsq count to prevent goldminenodes from gaming spysend queue
    int64_t nDsqCount;


    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        LOCK(cs);
        std::string strVersion;
        if(ser_action.ForRead()) {
            READWRITE(strVersion);
        }
        else {
            strVersion = SERIALIZATION_VERSION_STRING; 
            READWRITE(strVersion);
        }

        READWRITE(vGoldminenodes);
        READWRITE(mAskedUsForGoldminenodeList);
        READWRITE(mWeAskedForGoldminenodeList);
        READWRITE(mWeAskedForGoldminenodeListEntry);
        READWRITE(mMnbRecoveryRequests);
        READWRITE(mMnbRecoveryGoodReplies);
        READWRITE(nLastWatchdogVoteTime);
        READWRITE(nDsqCount);

        READWRITE(mapSeenGoldminenodeBroadcast);
        READWRITE(mapSeenGoldminenodePing);
        READWRITE(indexGoldminenodes);
        if(ser_action.ForRead() && (strVersion != SERIALIZATION_VERSION_STRING)) {
            Clear();
        }
    }

    CGoldminenodeMan();

    /// Add an entry
    bool Add(CGoldminenode &mn);

    /// Ask (source) node for mnb
    void AskForMN(CNode *pnode, const CTxIn &vin);
    void AskForMnb(CNode *pnode, const uint256 &hash);

    /// Check all Goldminenodes
    void Check();

    /// Check all Goldminenodes and remove inactive
    void CheckAndRemove();

    /// Clear Goldminenode vector
    void Clear();

    /// Count Goldminenodes filtered by nProtocolVersion.
    /// Goldminenode nProtocolVersion should match or be above the one specified in param here.
    int CountGoldminenodes(int nProtocolVersion = -1);
    /// Count enabled Goldminenodes filtered by nProtocolVersion.
    /// Goldminenode nProtocolVersion should match or be above the one specified in param here.
    int CountEnabled(int nProtocolVersion = -1);

    /// Count Goldminenodes by network type - NET_IPV4, NET_IPV6, NET_TOR
    // int CountByIP(int nNetworkType);

    void DsegUpdate(CNode* pnode);

    /// Find an entry
    CGoldminenode* Find(const CScript &payee);
    CGoldminenode* Find(const CTxIn& vin);
    CGoldminenode* Find(const CPubKey& pubKeyGoldminenode);

    /// Versions of Find that are safe to use from outside the class
    bool Get(const CPubKey& pubKeyGoldminenode, CGoldminenode& goldminenode);
    bool Get(const CTxIn& vin, CGoldminenode& goldminenode);

    /// Retrieve goldminenode vin by index
    bool Get(int nIndex, CTxIn& vinGoldminenode, bool& fIndexRebuiltOut) {
        LOCK(cs);
        fIndexRebuiltOut = fIndexRebuilt;
        return indexGoldminenodes.Get(nIndex, vinGoldminenode);
    }

    bool GetIndexRebuiltFlag() {
        LOCK(cs);
        return fIndexRebuilt;
    }

    /// Get index of a goldminenode vin
    int GetGoldminenodeIndex(const CTxIn& vinGoldminenode) {
        LOCK(cs);
        return indexGoldminenodes.GetGoldminenodeIndex(vinGoldminenode);
    }

    /// Get old index of a goldminenode vin
    int GetGoldminenodeIndexOld(const CTxIn& vinGoldminenode) {
        LOCK(cs);
        return indexGoldminenodesOld.GetGoldminenodeIndex(vinGoldminenode);
    }

    /// Get goldminenode VIN for an old index value
    bool GetGoldminenodeVinForIndexOld(int nGoldminenodeIndex, CTxIn& vinGoldminenodeOut) {
        LOCK(cs);
        return indexGoldminenodesOld.Get(nGoldminenodeIndex, vinGoldminenodeOut);
    }

    /// Get index of a goldminenode vin, returning rebuild flag
    int GetGoldminenodeIndex(const CTxIn& vinGoldminenode, bool& fIndexRebuiltOut) {
        LOCK(cs);
        fIndexRebuiltOut = fIndexRebuilt;
        return indexGoldminenodes.GetGoldminenodeIndex(vinGoldminenode);
    }

    void ClearOldGoldminenodeIndex() {
        LOCK(cs);
        indexGoldminenodesOld.Clear();
        fIndexRebuilt = false;
    }

    bool Has(const CTxIn& vin);

    goldminenode_info_t GetGoldminenodeInfo(const CTxIn& vin);

    goldminenode_info_t GetGoldminenodeInfo(const CPubKey& pubKeyGoldminenode);

    /// Find an entry in the goldminenode list that is next to be paid
    CGoldminenode* GetNextGoldminenodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount);
    /// Same as above but use current block height
    CGoldminenode* GetNextGoldminenodeInQueueForPayment(bool fFilterSigTime, int& nCount);

    /// Find a random entry
    CGoldminenode* FindRandomNotInVec(const std::vector<CTxIn> &vecToExclude, int nProtocolVersion = -1);

    std::vector<CGoldminenode> GetFullGoldminenodeVector() { return vGoldminenodes; }

    std::vector<std::pair<int, CGoldminenode> > GetGoldminenodeRanks(int nBlockHeight = -1, int nMinProtocol=0);
    int GetGoldminenodeRank(const CTxIn &vin, int nBlockHeight, int nMinProtocol=0, bool fOnlyActive=true);
    CGoldminenode* GetGoldminenodeByRank(int nRank, int nBlockHeight, int nMinProtocol=0, bool fOnlyActive=true);

    void ProcessGoldminenodeConnections();
    std::pair<CService, std::set<uint256> > PopScheduledMnbRequestConnection();

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);

    void DoFullVerificationStep();
    void CheckSameAddr();
    bool SendVerifyRequest(const CAddress& addr, const std::vector<CGoldminenode*>& vSortedByAddr);
    void SendVerifyReply(CNode* pnode, CGoldminenodeVerification& mnv);
    void ProcessVerifyReply(CNode* pnode, CGoldminenodeVerification& mnv);
    void ProcessVerifyBroadcast(CNode* pnode, const CGoldminenodeVerification& mnv);

    /// Return the number of (unique) Goldminenodes
    int size() { return vGoldminenodes.size(); }

    std::string ToString() const;

    /// Update goldminenode list and maps using provided CGoldminenodeBroadcast
    void UpdateGoldminenodeList(CGoldminenodeBroadcast mnb);
    /// Perform complete check and only then update list and maps
    bool CheckMnbAndUpdateGoldminenodeList(CNode* pfrom, CGoldminenodeBroadcast mnb, int& nDos);
    bool IsMnbRecoveryRequested(const uint256& hash) { return mMnbRecoveryRequests.count(hash); }

    void UpdateLastPaid();

    void CheckAndRebuildGoldminenodeIndex();

    void AddDirtyGovernanceObjectHash(const uint256& nHash)
    {
        LOCK(cs);
        vecDirtyGovernanceObjectHashes.push_back(nHash);
    }

    std::vector<uint256> GetAndClearDirtyGovernanceObjectHashes()
    {
        LOCK(cs);
        std::vector<uint256> vecTmp = vecDirtyGovernanceObjectHashes;
        vecDirtyGovernanceObjectHashes.clear();
        return vecTmp;;
    }

    bool IsWatchdogActive();
    void UpdateWatchdogVoteTime(const CTxIn& vin);
    void AddGovernanceVote(const CTxIn& vin, uint256 nGovernanceObjectHash);
    void RemoveGovernanceObject(uint256 nGovernanceObjectHash);

    void CheckGoldminenode(const CTxIn& vin, bool fForce = false);
    void CheckGoldminenode(const CPubKey& pubKeyGoldminenode, bool fForce = false);

    int GetGoldminenodeState(const CTxIn& vin);
    int GetGoldminenodeState(const CPubKey& pubKeyGoldminenode);

    bool IsGoldminenodePingedWithin(const CTxIn& vin, int nSeconds, int64_t nTimeToCheckAt = -1);
    void SetGoldminenodeLastPing(const CTxIn& vin, const CGoldminenodePing& mnp);

    void UpdatedBlockTip(const CBlockIndex *pindex);

    /**
     * Called to notify CGovernanceManager that the goldminenode index has been updated.
     * Must be called while not holding the CGoldminenodeMan::cs mutex
     */
    void NotifyGoldminenodeUpdates();

};

#endif
