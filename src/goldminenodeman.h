// Copyright (c) 2015-2017 The ARC developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef GOLDMINENODEMAN_H
#define GOLDMINENODEMAN_H

#include "goldminenode.h"
#include "sync.h"

using namespace std;

class CGoldminenodeMan;
class CConnman;

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
    typedef std::pair<arith_uint256, CGoldminenode*> score_pair_t;
    typedef std::vector<score_pair_t> score_pair_vec_t;
    typedef std::pair<int, CGoldminenode> rank_pair_t;
    typedef std::vector<rank_pair_t> rank_pair_vec_t;

private:
    static const std::string SERIALIZATION_VERSION_STRING;

    static const int DSEG_UPDATE_SECONDS        = 3 * 60 * 60;

    static const int LAST_PAID_SCAN_BLOCKS      = 100;

    static const int MIN_POSE_PROTO_VERSION     = 70203;
    static const int MAX_POSE_CONNECTIONS       = 10;
    static const int MAX_POSE_RANK              = 10;
    static const int MAX_POSE_BLOCKS            = 10;

    static const int MNB_RECOVERY_QUORUM_TOTAL      = 10;
    static const int MNB_RECOVERY_QUORUM_REQUIRED   = 6;
    static const int MNB_RECOVERY_MAX_ASK_ENTRIES   = 10;
    static const int MNB_RECOVERY_WAIT_SECONDS      = 60;
    static const int MNB_RECOVERY_RETRY_SECONDS     = 3 * 60 * 60;


    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    // Keep track of current block height
    int nCachedBlockHeight;

    // map to hold all MNs
    std::map<COutPoint, CGoldminenode> mapGoldminenodes;
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


    CGoldminenodeIndex indexGoldminenodes;
    /// Set when goldminenodes are added, cleared when CGovernanceManager is notified
    bool fGoldminenodesAdded;

    /// Set when goldminenodes are removed, cleared when CGovernanceManager is notified
    bool fGoldminenodesRemoved;

    int64_t nLastWatchdogVoteTime;

    friend class CGoldminenodeSync;
    /// Find an entry
    CGoldminenode* Find(const COutPoint& outpoint);

    bool GetGoldminenodeScores(const uint256& nBlockHash, score_pair_vec_t& vecGoldminenodeScoresRet, int nMinProtocol = 0);

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

        READWRITE(mapGoldminenodes);
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
    void AskForMN(CNode *pnode, const COutPoint& outpoint, CConnman& connman);
    void AskForMnb(CNode *pnode, const uint256 &hash);

    bool PoSeBan(const COutPoint &outpoint);
    bool AllowMixing(const COutPoint &outpoint);
    bool DisallowMixing(const COutPoint &outpoint);

    /// Check all Goldminenodes
    void Check();

    /// Check all Goldminenodes and remove inactive
    void CheckAndRemove(CConnman& connman);
    /// This is dummy overload to be used for dumping/loading mncache.dat
    void CheckAndRemove() {}

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

    void DsegUpdate(CNode* pnode, CConnman& connman);

    /// Versions of Find that are safe to use from outside the class
    bool Get(const COutPoint& outpoint, CGoldminenode& goldminenodeRet);
    bool Has(const COutPoint& outpoint);

    bool GetGoldminenodeInfo(const COutPoint& outpoint, goldminenode_info_t& mnInfoRet);
    bool GetGoldminenodeInfo(const CPubKey& pubKeyGoldminenode, goldminenode_info_t& mnInfoRet);
    bool GetGoldminenodeInfo(const CScript& payee, goldminenode_info_t& mnInfoRet);

    /// Find an entry in the goldminenode list that is next to be paid
    bool GetNextGoldminenodeInQueueForMasterPayment(int nBlockHeight, bool fFilterSigTime, int& nCountRet, goldminenode_info_t& mnInfoRet);
    /// Same as above but use current block height
    bool GetNextGoldminenodeInQueueForMasterPayment(bool fFilterSigTime, int& nCountRet, goldminenode_info_t& mnInfoRet);
	///
	bool GetNextGoldminenodeInQueueForTmp(int nBlockHeight, bool fFilterSigTime, int& nCountRet, goldminenode_info_t& mnInfoRet);
    /// Find a random entry
    goldminenode_info_t FindRandomNotInVec(const std::vector<COutPoint> &vecToExclude, int nProtocolVersion = -1);

    std::map<COutPoint, CGoldminenode> GetFullGoldminenodeMap() { return mapGoldminenodes; }

    bool GetGoldminenodeRanks(rank_pair_vec_t& vecGoldminenodeRanksRet, int nBlockHeight = -1, int nMinProtocol = 0);
    bool GetGoldminenodeRank(const COutPoint &outpoint, int& nRankRet, int nBlockHeight = -1, int nMinProtocol = 0);

    void ProcessGoldminenodeConnections(CConnman& connman);
    std::pair<CService, std::set<uint256> > PopScheduledMnbRequestConnection();

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv, CConnman& connman);

    void DoFullVerificationStep(CConnman& connman);
    void CheckSameAddr();
    bool SendVerifyRequest(const CAddress& addr, const std::vector<CGoldminenode*>& vSortedByAddr, CConnman& connman);
    void SendVerifyReply(CNode* pnode, CGoldminenodeVerification& mnv, CConnman& connman);
    void ProcessVerifyReply(CNode* pnode, CGoldminenodeVerification& mnv);
    void ProcessVerifyBroadcast(CNode* pnode, const CGoldminenodeVerification& mnv);

    /// Return the number of (unique) Goldminenodes
    int size() { return mapGoldminenodes.size(); }

    std::string ToString() const;

    /// Update goldminenode list and maps using provided CGoldminenodeBroadcast
    void UpdateGoldminenodeList(CGoldminenodeBroadcast mnb, CConnman& connman);
    /// Perform complete check and only then update list and maps
    bool CheckMnbAndUpdateGoldminenodeList(CNode* pfrom, CGoldminenodeBroadcast mnb, int& nDos, CConnman& connman);
    bool IsMnbRecoveryRequested(const uint256& hash) { return mMnbRecoveryRequests.count(hash); }

    void UpdateLastPaid(const CBlockIndex* pindex);

    bool IsWatchdogActive();
    void UpdateWatchdogVoteTime(const COutPoint& outpoint, uint64_t nVoteTime = 0);

    void CheckGoldminenode(const CPubKey& pubKeyGoldminenode, bool fForce);

    bool IsGoldminenodePingedWithin(const COutPoint& outpoint, int nSeconds, int64_t nTimeToCheckAt = -1);
    void SetGoldminenodeLastPing(const COutPoint& outpoint, const CGoldminenodePing& mnp);

    void UpdatedBlockTip(const CBlockIndex *pindex);

    /**
     * Called to notify CGovernanceManager that the goldminenode index has been updated.
     * Must be called while not holding the CGoldminenodeMan::cs mutex
     */
    void NotifyGoldminenodeUpdates(CConnman& connman);

};

#endif
