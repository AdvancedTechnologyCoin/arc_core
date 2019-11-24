// Copyright (c) 2019 The Advanced Technology Coin and Eternity Group
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

static const int MNPAYMENTS_SIGNATURES_REQUIRED         = 1;
static const int MNPAYMENTS_SIGNATURES_TOTAL            = 10;

//! minimum peer version that can receive and send goldminenode payment messages,
//  vote for goldminenode and be elected as a payment winner
// V1 - Last protocol version before update
// V2 - Newest protocol version
static const int MIN_GOLDMINENODE_PAYMENT_PROTO_VERSION_1 = 70208;
static const int MIN_GOLDMINENODE_PAYMENT_PROTO_VERSION_2 = 70209;

extern CCriticalSection cs_vecPayees;
extern CCriticalSection cs_mapGoldminenodeBlocks;
extern CCriticalSection cs_mapGoldminenodePayeeVotes;

extern CGoldminenodePayments mnpayments;

/// TODO: all 4 functions do not belong here really, they should be refactored/moved somewhere (main.cpp ?)
bool IsBlockValueValid(const CBlock& block, int nBlockHeight, CAmount blockReward, std::string &strErrorRet);
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
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(*(CScriptBase*)(&scriptPubKey));
        READWRITE(vecVoteHashes);
    }

    CScript GetPayee() { return scriptPubKey; }

    void AddVoteHash(uint256 hashIn) { vecVoteHashes.push_back(hashIn); }
    std::vector<uint256> GetVoteHashes() { return vecVoteHashes; }
    int GetVoteCount() { return vecVoteHashes.size(); }
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
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(nBlockHeight);
        READWRITE(vecPayees);
    }

    void AddPayee(const CGoldminenodePaymentVote& vote);
    bool GetBestPayee(CScript& payeeRet);
    bool HasPayeeWithVotes(const CScript& payeeIn, int nVotesReq);

    bool IsTransactionValid(const CTransaction& txNew);

    std::string GetRequiredPaymentsString();
};

// vote for the winning payment
class CGoldminenodePaymentVote
{
public:
    CTxIn vinGoldminenode;

    int nBlockHeight;
    CScript payee;
    std::vector<unsigned char> vchSig;

    CGoldminenodePaymentVote() :
        vinGoldminenode(),
        nBlockHeight(0),
        payee(),
        vchSig()
        {}

    CGoldminenodePaymentVote(COutPoint outpointGoldminenode, int nBlockHeight, CScript payee) :
        vinGoldminenode(outpointGoldminenode),
        nBlockHeight(nBlockHeight),
        payee(payee),
        vchSig()
        {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(vinGoldminenode);
        READWRITE(nBlockHeight);
        READWRITE(*(CScriptBase*)(&payee));
        READWRITE(vchSig);
    }

    uint256 GetHash() const {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << *(CScriptBase*)(&payee);
        ss << nBlockHeight;
        ss << vinGoldminenode.prevout;
        return ss.GetHash();
    }

    bool Sign();
    bool CheckSignature(const CPubKey& pubKeyGoldminenode, int nValidationHeight, int &nDos);

    bool IsValid(CNode* pnode, int nValidationHeight, std::string& strError, CConnman& connman);
    void Relay(CConnman& connman);

    bool IsVerified() { return !vchSig.empty(); }
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

    CGoldminenodePayments() : nStorageCoeff(1.25), nMinBlocksToStore(5000) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(mapGoldminenodePaymentVotes);
        READWRITE(mapGoldminenodeBlocks);
    }

    void Clear();

    bool AddPaymentVote(const CGoldminenodePaymentVote& vote);
    bool HasVerifiedPaymentVote(uint256 hashIn);
    bool ProcessBlock(int nBlockHeight, CConnman& connman);
    void CheckPreviousBlockVotes(int nPrevBlockHeight);

    void Sync(CNode* node, CConnman& connman);
    void RequestLowDataPaymentBlocks(CNode* pnode, CConnman& connman);
    void CheckAndRemove();

    bool GetBlockPayee(int nBlockHeight, CScript& payee);
    bool IsTransactionValid(const CTransaction& txNew, int nBlockHeight);
    bool IsScheduled(CGoldminenode& mn, int nNotBlockHeight);

    bool CanVote(COutPoint outGoldminenode, int nBlockHeight);

    int GetMinGoldminenodePaymentsProto();
    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv, CConnman& connman);
    std::string GetRequiredPaymentsString(int nBlockHeight);
    void FillBlockPayee(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CTxOut& txoutGoldminenodeRet);
    std::string ToString() const;

    int GetBlockCount() { return mapGoldminenodeBlocks.size(); }
    int GetVoteCount() { return mapGoldminenodePaymentVotes.size(); }

    bool IsEnoughData();
    int GetStorageLimit();
	
	static void CreateEvolution(CMutableTransaction& txNewRet, int nBlockHeight, CAmount blockEvolution, std::vector<CTxOut>& voutSuperblockRet);	
	
    void UpdatedBlockTip(const CBlockIndex *pindex, CConnman& connman);
};

#endif
