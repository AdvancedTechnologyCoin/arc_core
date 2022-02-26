// Copyright (c) 2017-2022 The Advanced Technology Coin
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef GOLDMINENODE_PAYMENTS_H
#define GOLDMINENODE_PAYMENTS_H

#include "util.h"
#include "core_io.h"
#include "key.h"
#include "goldminenode.h"
#include "net_processing.h"
#include "utilstrencodings.h"

class CGoldminenodePayments;
class CGoldminenodePaymentVote;
class CGoldminenodeBlockPayees;

static const int MNPAYMENTS_SIGNATURES_REQUIRED         = 6;
static const int MNPAYMENTS_SIGNATURES_TOTAL            = 10;

//! minimum peer version that can receive and send goldminenode payment messages,
//  vote for goldminenode and be elected as a payment winner
// V1 - Last protocol version before update
// V2 - Newest protocol version
static const int MIN_GOLDMINENODE_PAYMENT_PROTO_VERSION_1 = 70209;
static const int MIN_GOLDMINENODE_PAYMENT_PROTO_VERSION_2 = 70210;

extern CCriticalSection cs_vecPayees;
extern CCriticalSection cs_mapGoldminenodeBlocks;
extern CCriticalSection cs_mapGoldminenodePayeeVotes;

extern CGoldminenodePayments mnpayments;

/// TODO: all 4 functions do not belong here really, they should be refactored/moved somewhere (main.cpp ?)
bool IsBlockValueValid(const CBlock& block, int nBlockHeight, CAmount blockReward, std::string& strErrorRet);
bool IsBlockPayeeValid(const CTransaction& txNew, int nBlockHeight, CAmount blockReward);
void FillBlockPayments(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CAmount blockEvolution, CTxOut& txoutGoldminenodeRet, std::vector<CTxOut>& voutSuperblockRet);
std::string GetRequiredPaymentsString(int nBlockHeight);

class CGoldminenodePayee
{
private:
    CScript scriptPubKey;
    std::vector<uint256> vecVoteHashes;

public:
    CGoldminenodePayee() :
        scriptPubKey(),
        vecVoteHashes()
        {}

    CGoldminenodePayee(CScript payee, uint256 hashIn) :
        scriptPubKey(payee),
        vecVoteHashes()
    {
        vecVoteHashes.push_back(hashIn);
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(*(CScriptBase*)(&scriptPubKey));
        READWRITE(vecVoteHashes);
    }

    CScript GetPayee() const { return scriptPubKey; }

    void AddVoteHash(uint256 hashIn) { vecVoteHashes.push_back(hashIn); }
    std::vector<uint256> GetVoteHashes() const { return vecVoteHashes; }
    int GetVoteCount() const { return vecVoteHashes.size(); }
};

// Keep track of votes for payees from goldminenodes
class CGoldminenodeBlockPayees
{
public:
    int nBlockHeight;
    std::vector<CGoldminenodePayee> vecPayees;

    CGoldminenodeBlockPayees() :
        nBlockHeight(0),
        vecPayees()
        {}
    CGoldminenodeBlockPayees(int nBlockHeightIn) :
        nBlockHeight(nBlockHeightIn),
        vecPayees()
        {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(nBlockHeight);
        READWRITE(vecPayees);
    }

    void AddPayee(const CGoldminenodePaymentVote& vote);
    bool GetBestPayee(CScript& payeeRet) const;
    bool HasPayeeWithVotes(const CScript& payeeIn, int nVotesReq) const;

    bool IsTransactionValid(const CTransaction& txNew) const;

    std::string GetRequiredPaymentsString() const;
};

// vote for the winning payment
class CGoldminenodePaymentVote
{
public:
    COutPoint goldminenodeOutpoint;

    int nBlockHeight;
    CScript payee;
    std::vector<unsigned char> vchSig;

    CGoldminenodePaymentVote() :
        goldminenodeOutpoint(),
        nBlockHeight(0),
        payee(),
        vchSig()
        {}

    CGoldminenodePaymentVote(COutPoint outpoint, int nBlockHeight, CScript payee) :
        goldminenodeOutpoint(outpoint),
        nBlockHeight(nBlockHeight),
        payee(payee),
        vchSig()
        {}

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
        READWRITE(nBlockHeight);
        READWRITE(*(CScriptBase*)(&payee));
        if (!(s.GetType() & SER_GETHASH)) {
            READWRITE(vchSig);
        }
    }

    uint256 GetHash() const;
    uint256 GetSignatureHash() const;

    bool Sign();
    bool CheckSignature(const CPubKey& pubKeyGoldminenode, int nValidationHeight, int &nDos) const;

    bool IsValid(CNode* pnode, int nValidationHeight, std::string& strError, CConnman& connman) const;
    void Relay(CConnman& connman) const;

    bool IsVerified() const { return !vchSig.empty(); }
    void MarkAsNotVerified() { vchSig.clear(); }

    std::string ToString() const;
};

//
// Goldminenode Payments Class
// Keeps track of who should get paid for which blocks
//

class CGoldminenodePayments
{
private:
    // goldminenode count times nStorageCoeff payments blocks should be stored ...
    const float nStorageCoeff;
    // ... but at least nMinBlocksToStore (payments blocks)
    const int nMinBlocksToStore;

    // Keep track of current block height
    int nCachedBlockHeight;

public:
    std::map<uint256, CGoldminenodePaymentVote> mapGoldminenodePaymentVotes;
    std::map<int, CGoldminenodeBlockPayees> mapGoldminenodeBlocks;
    std::map<COutPoint, int> mapGoldminenodesLastVote;
    std::map<COutPoint, int> mapGoldminenodesDidNotVote;

    CGoldminenodePayments() : nStorageCoeff(1.25), nMinBlocksToStore(6000) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(mapGoldminenodePaymentVotes);
        READWRITE(mapGoldminenodeBlocks);
    }

    void Clear();

    bool AddOrUpdatePaymentVote(const CGoldminenodePaymentVote& vote);
    bool HasVerifiedPaymentVote(const uint256& hashIn) const;
    bool ProcessBlock(int nBlockHeight, CConnman& connman);
    void CheckBlockVotes(int nBlockHeight);

    void Sync(CNode* node, CConnman& connman) const;
    void RequestLowDataPaymentBlocks(CNode* pnode, CConnman& connman) const;
    void CheckAndRemove();

    bool GetBlockPayee(int nBlockHeight, CScript& payeeRet) const;
    bool IsTransactionValid(const CTransaction& txNew, int nBlockHeight) const;
    bool IsScheduled(const goldminenode_info_t& mnInfo, int nNotBlockHeight) const;

    bool UpdateLastVote(const CGoldminenodePaymentVote& vote);

    int GetMinGoldminenodePaymentsProto() const;
    void ProcessMessage(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv, CConnman& connman);
    std::string GetRequiredPaymentsString(int nBlockHeight) const;
    void FillBlockPayee(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CTxOut& txoutGoldminenodeRet) const;
    std::string ToString() const;

    int GetBlockCount() const { return mapGoldminenodeBlocks.size(); }
    int GetVoteCount() const { return mapGoldminenodePaymentVotes.size(); }

    bool IsEnoughData() const;
    int GetStorageLimit() const;
	static void CreateEvolution(CMutableTransaction& txNewRet, int nBlockHeight, CAmount blockEvolution, std::vector<CTxOut>& voutSuperblockRet);	
	
    void UpdatedBlockTip(const CBlockIndex *pindex, CConnman& connman);
};

#endif
