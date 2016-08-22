

// Copyright (c) 2015-2016 The Arctic developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef GOLDMINE_PAYMENTS_H
#define GOLDMINE_PAYMENTS_H

#include "key.h"
#include "main.h"
#include "goldmine.h"
#include <boost/lexical_cast.hpp>

using namespace std;

extern CCriticalSection cs_vecPayments;
extern CCriticalSection cs_mapGoldmineBlocks;
extern CCriticalSection cs_mapGoldminePayeeVotes;

class CGoldminePayments;
class CGoldminePaymentWinner;
class CGoldmineBlockPayees;

extern CGoldminePayments goldminePayments;

#define MNPAYMENTS_SIGNATURES_REQUIRED           6
#define MNPAYMENTS_SIGNATURES_TOTAL              10

void ProcessMessageGoldminePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
bool IsReferenceNode(CTxIn& vin);
bool IsBlockPayeeValid(const CTransaction& txNew, int nBlockHeight);
std::string GetRequiredPaymentsString(int nBlockHeight);
bool IsBlockValueValid(const CBlock& block, int64_t nExpectedValue);
void FillBlockPayee(CMutableTransaction& txNew, int64_t nFees);

void DumpGoldminePayments();

/** Save Goldmine Payment Data (mnpayments.dat)
 */
class CGoldminePaymentDB
{
private:
    boost::filesystem::path pathDB;
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

    CGoldminePaymentDB();
    bool Write(const CGoldminePayments &objToSave);
    ReadResult Read(CGoldminePayments& objToLoad, bool fDryRun = false);
};

class CGoldminePayee
{
public:
    CScript scriptPubKey;
    int nVotes;

    CGoldminePayee() {
        scriptPubKey = CScript();
        nVotes = 0;
    }

    CGoldminePayee(CScript payee, int nVotesIn) {
        scriptPubKey = payee;
        nVotes = nVotesIn;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(scriptPubKey);
        READWRITE(nVotes);
     }
};

// Keep track of votes for payees from goldmines
class CGoldmineBlockPayees
{
public:
    int nBlockHeight;
    std::vector<CGoldminePayee> vecPayments;

    CGoldmineBlockPayees(){
        nBlockHeight = 0;
        vecPayments.clear();
    }
    CGoldmineBlockPayees(int nBlockHeightIn) {
        nBlockHeight = nBlockHeightIn;
        vecPayments.clear();
    }

    void AddPayee(CScript payeeIn, int nIncrement){
        LOCK(cs_vecPayments);

        BOOST_FOREACH(CGoldminePayee& payee, vecPayments){
            if(payee.scriptPubKey == payeeIn) {
                payee.nVotes += nIncrement;
                return;
            }
        }

        CGoldminePayee c(payeeIn, nIncrement);
        vecPayments.push_back(c);
    }

    bool GetPayee(CScript& payee)
    {
        LOCK(cs_vecPayments);

        int nVotes = -1;
        BOOST_FOREACH(CGoldminePayee& p, vecPayments){
            if(p.nVotes > nVotes){
                payee = p.scriptPubKey;
                nVotes = p.nVotes;
            }
        }

        return (nVotes > -1);
    }

    bool HasPayeeWithVotes(CScript payee, int nVotesReq)
    {
        LOCK(cs_vecPayments);

        BOOST_FOREACH(CGoldminePayee& p, vecPayments){
            if(p.nVotes >= nVotesReq && p.scriptPubKey == payee) return true;
        }

        return false;
    }

    bool IsTransactionValid(const CTransaction& txNew);
    std::string GetRequiredPaymentsString();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(nBlockHeight);
        READWRITE(vecPayments);
     }
};

// for storing the winning payments
class CGoldminePaymentWinner
{
public:
    CTxIn vinGoldmine;

    int nBlockHeight;
    CScript payee;
    std::vector<unsigned char> vchSig;

    CGoldminePaymentWinner() {
        nBlockHeight = 0;
        vinGoldmine = CTxIn();
        payee = CScript();
    }

    CGoldminePaymentWinner(CTxIn vinIn) {
        nBlockHeight = 0;
        vinGoldmine = vinIn;
        payee = CScript();
    }

    uint256 GetHash(){
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << payee;
        ss << nBlockHeight;
        ss << vinGoldmine.prevout;

        return ss.GetHash();
    }

    bool Sign(CKey& keyGoldmine, CPubKey& pubKeyGoldmine);
    bool IsValid(CNode* pnode, std::string& strError);
    bool SignatureValid();
    void Relay();

    void AddPayee(CScript payeeIn){
        payee = payeeIn;
    }


    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(vinGoldmine);
        READWRITE(nBlockHeight);
        READWRITE(payee);
        READWRITE(vchSig);
    }

    std::string ToString()
    {
        std::string ret = "";
        ret += vinGoldmine.ToString();
        ret += ", " + boost::lexical_cast<std::string>(nBlockHeight);
        ret += ", " + payee.ToString();
        ret += ", " + boost::lexical_cast<std::string>((int)vchSig.size());
        return ret;
    }
};

//
// Goldmine Payments Class
// Keeps track of who should get paid for which blocks
//

class CGoldminePayments
{
private:
    int nSyncedFromPeer;
    int nLastBlockHeight;

public:
    std::map<uint256, CGoldminePaymentWinner> mapGoldminePayeeVotes;
    std::map<int, CGoldmineBlockPayees> mapGoldmineBlocks;
    std::map<uint256, int> mapGoldmineLastVote; //prevout.hash + prevout.n, nBlockHeight

    CGoldminePayments() {
        nSyncedFromPeer = 0;
        nLastBlockHeight = 0;
    }

    void Clear() {
        LOCK2(cs_mapGoldmineBlocks, cs_mapGoldminePayeeVotes);
        mapGoldmineBlocks.clear();
        mapGoldminePayeeVotes.clear();
    }

    bool AddWinningGoldmine(CGoldminePaymentWinner& winner);
    bool ProcessBlock(int nBlockHeight);

    void Sync(CNode* node, int nCountNeeded);
    void CleanPaymentList();
    int LastPayment(CGoldmine& gm);

    bool GetBlockPayee(int nBlockHeight, CScript& payee);
    bool IsTransactionValid(const CTransaction& txNew, int nBlockHeight);
    bool IsScheduled(CGoldmine& gm, int nNotBlockHeight);

    bool CanVote(COutPoint outGoldmine, int nBlockHeight) {
        LOCK(cs_mapGoldminePayeeVotes);

        if(mapGoldmineLastVote.count(outGoldmine.hash + outGoldmine.n)) {
            if(mapGoldmineLastVote[outGoldmine.hash + outGoldmine.n] == nBlockHeight) {
                return false;
            }
        }

        //record this goldmine voted
        mapGoldmineLastVote[outGoldmine.hash + outGoldmine.n] = nBlockHeight;
        return true;
    }

    int GetMinGoldminePaymentsProto();
    void ProcessMessageGoldminePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    std::string GetRequiredPaymentsString(int nBlockHeight);
    void FillBlockPayee(CMutableTransaction& txNew, int64_t nFees);
    std::string ToString() const;
    int GetOldestBlock();
    int GetNewestBlock();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(mapGoldminePayeeVotes);
        READWRITE(mapGoldmineBlocks);
    }
};



#endif
