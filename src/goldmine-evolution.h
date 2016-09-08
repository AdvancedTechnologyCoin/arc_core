// Copyright (c) 2015-2016 The Arctic developers

// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef GOLDMINE_EVOLUTION_H
#define GOLDMINE_EVOLUTION_H

#include "main.h"
#include "sync.h"
#include "net.h"
#include "key.h"
#include "util.h"
#include "base58.h"
#include "goldmine.h"
#include <boost/lexical_cast.hpp>
#include "init.h"

using namespace std;

extern CCriticalSection cs_evolution;

class CEvolutionManager;
class CFinalizedEvolutionBroadcast;
class CFinalizedEvolution;
class CFinalizedEvolutionVote;
class CEvolutionProposal;
class CEvolutionProposalBroadcast;
class CEvolutionVote;
class CTxEvolutionPayment;

#define VOTE_ABSTAIN  0
#define VOTE_YES      1
#define VOTE_NO       2

static const CAmount EVOLUTION_FEE_TX = (5*COIN);
static const int64_t EVOLUTION_FEE_CONFIRMATIONS = 6;
static const int64_t EVOLUTION_VOTE_UPDATE_MIN = 60*60;

extern std::vector<CEvolutionProposalBroadcast> vecImmatureEvolutionProposals;
extern std::vector<CFinalizedEvolutionBroadcast> vecImmatureFinalizedEvolutions;

extern CEvolutionManager evolution;
void DumpEvolutions();

// Define amount of blocks in evolution payment cycle
int GetEvolutionPaymentCycleBlocks();

//Check the collateral transaction for the evolution proposal/finalized evolution
bool IsEvolutionCollateralValid(uint256 nTxCollateralHash, uint256 nExpectedHash, std::string& strError, int64_t& nTime, int& nConf);

/** Save Evolution Manager (evolution.dat)
 */
class CEvolutionDB
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

    CEvolutionDB();
    bool Write(const CEvolutionManager &objToSave);
    ReadResult Read(CEvolutionManager& objToLoad, bool fDryRun = false);
};


//
// Evolution Manager : Contains all proposals for the evolution
//
class CEvolutionManager
{
private:

    //hold txes until they mature enough to use
    map<uint256, uint256> mapCollateralTxids;

public:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;
    
    // keep track of the scanning errors I've seen
    map<uint256, CEvolutionProposal> mapProposals;
    map<uint256, CFinalizedEvolution> mapFinalizedEvolutions;

    std::map<uint256, CEvolutionProposalBroadcast> mapSeenGoldmineEvolutionProposals;
    std::map<uint256, CEvolutionVote> mapSeenGoldmineEvolutionVotes;
    std::map<uint256, CEvolutionVote> mapOrphanGoldmineEvolutionVotes;
    std::map<uint256, CFinalizedEvolutionBroadcast> mapSeenFinalizedEvolutions;
    std::map<uint256, CFinalizedEvolutionVote> mapSeenFinalizedEvolutionVotes;
    std::map<uint256, CFinalizedEvolutionVote> mapOrphanFinalizedEvolutionVotes;

    CEvolutionManager() {
        mapProposals.clear();
        mapFinalizedEvolutions.clear();
    }

    void ClearSeen() {
        mapSeenGoldmineEvolutionProposals.clear();
        mapSeenGoldmineEvolutionVotes.clear();
        mapSeenFinalizedEvolutions.clear();
        mapSeenFinalizedEvolutionVotes.clear();
    }

    int sizeFinalized() {return (int)mapFinalizedEvolutions.size();}
    int sizeProposals() {return (int)mapProposals.size();}

    void ResetSync();
    void MarkSynced();
    void Sync(CNode* node, uint256 nProp, bool fPartial=false);

    void Calculate();
    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    void NewBlock();
    CEvolutionProposal *FindProposal(const std::string &strProposalName);
    CEvolutionProposal *FindProposal(uint256 nHash);
    CFinalizedEvolution *FindFinalizedEvolution(uint256 nHash);
    std::pair<std::string, std::string> GetVotes(std::string strProposalName);

    CAmount GetTotalEvolution(int nHeight);
    std::vector<CEvolutionProposal*> GetEvolution();
    std::vector<CEvolutionProposal*> GetAllProposals();
    std::vector<CFinalizedEvolution*> GetFinalizedEvolutions();
    bool IsEvolutionPaymentBlock(int nBlockHeight);
    bool AddProposal(CEvolutionProposal& evolutionProposal);
    bool AddFinalizedEvolution(CFinalizedEvolution& finalizedEvolution);
    void SubmitFinalEvolution();
    bool HasNextFinalizedEvolution();

    bool UpdateProposal(CEvolutionVote& vote, CNode* pfrom, std::string& strError);
    bool UpdateFinalizedEvolution(CFinalizedEvolutionVote& vote, CNode* pfrom, std::string& strError);
    bool PropExists(uint256 nHash);
    bool IsTransactionValid(const CTransaction& txNew, int nBlockHeight);
    std::string GetRequiredPaymentsString(int nBlockHeight);
    void FillBlockPayee(CMutableTransaction& txNew, CAmount nFees);

    void CheckOrphanVotes();
    void Clear(){
        LOCK(cs);

        LogPrintf("Evolution object cleared\n");
        mapProposals.clear();
        mapFinalizedEvolutions.clear();
        mapSeenGoldmineEvolutionProposals.clear();
        mapSeenGoldmineEvolutionVotes.clear();
        mapSeenFinalizedEvolutions.clear();
        mapSeenFinalizedEvolutionVotes.clear();
        mapOrphanGoldmineEvolutionVotes.clear();
        mapOrphanFinalizedEvolutionVotes.clear();
    }
    void CheckAndRemove();
    std::string ToString() const;


    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(mapSeenGoldmineEvolutionProposals);
        READWRITE(mapSeenGoldmineEvolutionVotes);
        READWRITE(mapSeenFinalizedEvolutions);
        READWRITE(mapSeenFinalizedEvolutionVotes);
        READWRITE(mapOrphanGoldmineEvolutionVotes);
        READWRITE(mapOrphanFinalizedEvolutionVotes);

        READWRITE(mapProposals);
        READWRITE(mapFinalizedEvolutions);
    }
};


class CTxEvolutionPayment
{
public:
    uint256 nProposalHash;
    CScript payee;
    CAmount nAmount;

    CTxEvolutionPayment() {
        payee = CScript();
        nAmount = 0;
        nProposalHash = 0;
    }

    ADD_SERIALIZE_METHODS;

    //for saving to the serialized db
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(payee);
        READWRITE(nAmount);
        READWRITE(nProposalHash);
    }
};

//
// Finalized Evolution : Contains the suggested proposals to pay on a given block
//

class CFinalizedEvolution
{

private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;
    bool fAutoChecked; //If it matches what we see, we'll auto vote for it (goldmine only)

public:
    bool fValid;
    std::string strEvolutionName;
    int nBlockStart;
    std::vector<CTxEvolutionPayment> vecEvolutionPayments;
    map<uint256, CFinalizedEvolutionVote> mapVotes;
    uint256 nFeeTXHash;
    int64_t nTime;

    CFinalizedEvolution();
    CFinalizedEvolution(const CFinalizedEvolution& other);

    void CleanAndRemove(bool fSignatureCheck);
    bool AddOrUpdateVote(CFinalizedEvolutionVote& vote, std::string& strError);
    double GetScore();
    bool HasMinimumRequiredSupport();

    bool IsValid(std::string& strError, bool fCheckCollateral=true);

    std::string GetName() {return strEvolutionName; }
    std::string GetProposals();
    int GetBlockStart() {return nBlockStart;}
    int GetBlockEnd() {return nBlockStart + (int)(vecEvolutionPayments.size() - 1);}
    int GetVoteCount() {return (int)mapVotes.size();}
    bool IsTransactionValid(const CTransaction& txNew, int nBlockHeight);
    bool GetEvolutionPaymentByBlock(int64_t nBlockHeight, CTxEvolutionPayment& payment)
    {
        LOCK(cs);

        int i = nBlockHeight - GetBlockStart();
        if(i < 0) return false;
        if(i > (int)vecEvolutionPayments.size() - 1) return false;
        payment = vecEvolutionPayments[i];
        return true;
    }
    bool GetPayeeAndAmount(int64_t nBlockHeight, CScript& payee, CAmount& nAmount)
    {
        LOCK(cs);

        int i = nBlockHeight - GetBlockStart();
        if(i < 0) return false;
        if(i > (int)vecEvolutionPayments.size() - 1) return false;
        payee = vecEvolutionPayments[i].payee;
        nAmount = vecEvolutionPayments[i].nAmount;
        return true;
    }

    //check to see if we should vote on this
    void AutoCheck();
    //total arcticcoin paid out by this evolution
    CAmount GetTotalPayout();
    //vote on this finalized evolution as a goldmine
    void SubmitVote();

    //checks the hashes to make sure we know about them
    string GetStatus();

    uint256 GetHash(){
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << strEvolutionName;
        ss << nBlockStart;
        ss << vecEvolutionPayments;

        uint256 h1 = ss.GetHash();
        return h1;
    }

    ADD_SERIALIZE_METHODS;

    //for saving to the serialized db
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(LIMITED_STRING(strEvolutionName, 20));
        READWRITE(nFeeTXHash);
        READWRITE(nTime);
        READWRITE(nBlockStart);
        READWRITE(vecEvolutionPayments);
        READWRITE(fAutoChecked);

        READWRITE(mapVotes);
    }
};

// FinalizedEvolution are cast then sent to peers with this object, which leaves the votes out
class CFinalizedEvolutionBroadcast : public CFinalizedEvolution
{
private:
    std::vector<unsigned char> vchSig;

public:
    CFinalizedEvolutionBroadcast();
    CFinalizedEvolutionBroadcast(const CFinalizedEvolution& other);
    CFinalizedEvolutionBroadcast(std::string strEvolutionNameIn, int nBlockStartIn, std::vector<CTxEvolutionPayment> vecEvolutionPaymentsIn, uint256 nFeeTXHashIn);

    void swap(CFinalizedEvolutionBroadcast& first, CFinalizedEvolutionBroadcast& second) // nothrow
    {
        // enable ADL (not necessary in our case, but good practice)
        using std::swap;

        // by swapping the members of two classes,
        // the two classes are effectively swapped
        swap(first.strEvolutionName, second.strEvolutionName);
        swap(first.nBlockStart, second.nBlockStart);
        first.mapVotes.swap(second.mapVotes);
        first.vecEvolutionPayments.swap(second.vecEvolutionPayments);
        swap(first.nFeeTXHash, second.nFeeTXHash);
        swap(first.nTime, second.nTime);
    }

    CFinalizedEvolutionBroadcast& operator=(CFinalizedEvolutionBroadcast from)
    {
        swap(*this, from);
        return *this;
    }

    void Relay();

    ADD_SERIALIZE_METHODS;

    //for propagating messages
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        //for syncing with other clients
        READWRITE(LIMITED_STRING(strEvolutionName, 20));
        READWRITE(nBlockStart);
        READWRITE(vecEvolutionPayments);
        READWRITE(nFeeTXHash);
    }
};

//
// CFinalizedEvolutionVote - Allow a goldmine node to vote and broadcast throughout the network
//

class CFinalizedEvolutionVote
{
public:
    bool fValid; //if the vote is currently valid / counted
    bool fSynced; //if we've sent this to our peers
    CTxIn vin;
    uint256 nEvolutionHash;
    int64_t nTime;
    std::vector<unsigned char> vchSig;

    CFinalizedEvolutionVote();
    CFinalizedEvolutionVote(CTxIn vinIn, uint256 nEvolutionHashIn);

    bool Sign(CKey& keyGoldmine, CPubKey& pubKeyGoldmine);
    bool SignatureValid(bool fSignatureCheck);
    void Relay();

    uint256 GetHash(){
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vin;
        ss << nEvolutionHash;
        ss << nTime;
        return ss.GetHash();
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(vin);
        READWRITE(nEvolutionHash);
        READWRITE(nTime);
        READWRITE(vchSig);
    }

};

//
// Evolution Proposal : Contains the goldmine votes for each evolution
//

class CEvolutionProposal
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;
    CAmount nAlloted;

public:
    bool fValid;
    std::string strProposalName;

    /*
        json object with name, short-description, long-description, pdf-url and any other info
        This allows the proposal website to stay 100% decentralized
    */
    std::string strURL;
    int nBlockStart;
    int nBlockEnd;
    CAmount nAmount;
    CScript address;
    int64_t nTime;
    uint256 nFeeTXHash;

    map<uint256, CEvolutionVote> mapVotes;
    //cache object

    CEvolutionProposal();
    CEvolutionProposal(const CEvolutionProposal& other);
    CEvolutionProposal(std::string strProposalNameIn, std::string strURLIn, int nBlockStartIn, int nBlockEndIn, CScript addressIn, CAmount nAmountIn, uint256 nFeeTXHashIn);

    void Calculate();
    bool AddOrUpdateVote(CEvolutionVote& vote, std::string& strError);
    bool HasMinimumRequiredSupport();
    std::pair<std::string, std::string> GetVotes();

    bool IsValid(std::string& strError, bool fCheckCollateral=true);

    bool IsEstablished() {
        //Proposals must be at least a day old to make it into a evolution
        if(Params().NetworkID() == CBaseChainParams::MAIN) return (nTime < GetTime() - (60*60*24));

        //for testing purposes - 4 hours
        return (nTime < GetTime() - (60*20));
    }

    std::string GetName() {return strProposalName; }
    std::string GetURL() {return strURL; }
    int GetBlockStart() {return nBlockStart;}
    int GetBlockEnd() {return nBlockEnd;}
    CScript GetPayee() {return address;}
    int GetTotalPaymentCount();
    int GetRemainingPaymentCount();
    int GetBlockStartCycle();
    int GetBlockCurrentCycle();
    int GetBlockEndCycle();
    double GetRatio();
    int GetYeas();
    int GetNays();
    int GetAbstains();
    CAmount GetAmount() {return nAmount;}
    void SetAllotted(CAmount nAllotedIn) {nAlloted = nAllotedIn;}
    CAmount GetAllotted() {return nAlloted;}

    void CleanAndRemove(bool fSignatureCheck);

    uint256 GetHash(){
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << strProposalName;
        ss << strURL;
        ss << nBlockStart;
        ss << nBlockEnd;
        ss << nAmount;
        ss << address;
        uint256 h1 = ss.GetHash();

        return h1;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        //for syncing with other clients
        READWRITE(LIMITED_STRING(strProposalName, 20));
        READWRITE(LIMITED_STRING(strURL, 64));
        READWRITE(nTime);
        READWRITE(nBlockStart);
        READWRITE(nBlockEnd);
        READWRITE(nAmount);
        READWRITE(address);
        READWRITE(nTime);
        READWRITE(nFeeTXHash);

        //for saving to the serialized db
        READWRITE(mapVotes);
    }
};

// Proposals are cast then sent to peers with this object, which leaves the votes out
class CEvolutionProposalBroadcast : public CEvolutionProposal
{
public:
    CEvolutionProposalBroadcast() : CEvolutionProposal(){}
    CEvolutionProposalBroadcast(const CEvolutionProposal& other) : CEvolutionProposal(other){}
    CEvolutionProposalBroadcast(const CEvolutionProposalBroadcast& other) : CEvolutionProposal(other){}
    CEvolutionProposalBroadcast(std::string strProposalNameIn, std::string strURLIn, int nPaymentCount, CScript addressIn, CAmount nAmountIn, int nBlockStartIn, uint256 nFeeTXHashIn);

    void swap(CEvolutionProposalBroadcast& first, CEvolutionProposalBroadcast& second) // nothrow
    {
        // enable ADL (not necessary in our case, but good practice)
        using std::swap;

        // by swapping the members of two classes,
        // the two classes are effectively swapped
        swap(first.strProposalName, second.strProposalName);
        swap(first.nBlockStart, second.nBlockStart);
        swap(first.strURL, second.strURL);
        swap(first.nBlockEnd, second.nBlockEnd);
        swap(first.nAmount, second.nAmount);
        swap(first.address, second.address);
        swap(first.nTime, second.nTime);
        swap(first.nFeeTXHash, second.nFeeTXHash);        
        first.mapVotes.swap(second.mapVotes);
    }

    CEvolutionProposalBroadcast& operator=(CEvolutionProposalBroadcast from)
    {
        swap(*this, from);
        return *this;
    }

    void Relay();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        //for syncing with other clients

        READWRITE(LIMITED_STRING(strProposalName, 20));
        READWRITE(LIMITED_STRING(strURL, 64));
        READWRITE(nTime);
        READWRITE(nBlockStart);
        READWRITE(nBlockEnd);
        READWRITE(nAmount);
        READWRITE(address);
        READWRITE(nFeeTXHash);
    }
};

//
// CEvolutionVote - Allow a goldmine node to vote and broadcast throughout the network
//

class CEvolutionVote
{
public:
    bool fValid; //if the vote is currently valid / counted
    bool fSynced; //if we've sent this to our peers
    CTxIn vin;
    uint256 nProposalHash;
    int nVote;
    int64_t nTime;
    std::vector<unsigned char> vchSig;

    CEvolutionVote();
    CEvolutionVote(CTxIn vin, uint256 nProposalHash, int nVoteIn);

    bool Sign(CKey& keyGoldmine, CPubKey& pubKeyGoldmine);
    bool SignatureValid(bool fSignatureCheck);
    void Relay();

    std::string GetVoteString() {
        std::string ret = "ABSTAIN";
        if(nVote == VOTE_YES) ret = "YES";
        if(nVote == VOTE_NO) ret = "NO";
        return ret;
    }

    uint256 GetHash(){
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vin;
        ss << nProposalHash;
        ss << nVote;
        ss << nTime;
        return ss.GetHash();
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(vin);
        READWRITE(nProposalHash);
        READWRITE(nVote);
        READWRITE(nTime);
        READWRITE(vchSig);
    }



};

#endif
