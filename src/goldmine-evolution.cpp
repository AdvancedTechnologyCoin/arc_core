// Copyright (c) 2015-2016 The Arctic developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "init.h"

#include "goldmine-evolution.h"
#include "goldmine.h"
#include "spysend.h"
#include "goldmineman.h"
#include "goldmine-sync.h"
#include "util.h"
#include "addrman.h"
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>

CEvolutionManager evolution;
CCriticalSection cs_evolution;

std::map<uint256, int64_t> askedForSourceProposalOrEvolution;
std::vector<CEvolutionProposalBroadcast> vecImmatureEvolutionProposals;
std::vector<CFinalizedEvolutionBroadcast> vecImmatureFinalizedEvolutions;

int GetEvolutionPaymentCycleBlocks(){
    // Amount of blocks in a months period of time (using 2.6 minutes per) = (60*24*30)/2.6
    if(Params().NetworkID() == CBaseChainParams::MAIN) return 16616;
    //for testing purposes

    return 50; //ten times per day
}

bool IsEvolutionCollateralValid(uint256 nTxCollateralHash, uint256 nExpectedHash, std::string& strError, int64_t& nTime, int& nConf)
{
    CTransaction txCollateral;
    uint256 nBlockHash;
    if(!GetTransaction(nTxCollateralHash, txCollateral, nBlockHash, true)){
        strError = strprintf("Can't find collateral tx %s", txCollateral.ToString());
        LogPrintf ("CEvolutionProposalBroadcast::IsEvolutionCollateralValid - %s\n", strError);
        return false;
    }

    if(txCollateral.vout.size() < 1) return false;
    if(txCollateral.nLockTime != 0) return false;

    CScript findScript;
    findScript << OP_RETURN << ToByteVector(nExpectedHash);

    bool foundOpReturn = false;
    BOOST_FOREACH(const CTxOut o, txCollateral.vout){
        if(!o.scriptPubKey.IsNormalPaymentScript() && !o.scriptPubKey.IsUnspendable()){
            strError = strprintf("Invalid Script %s", txCollateral.ToString());
            LogPrintf ("CEvolutionProposalBroadcast::IsEvolutionCollateralValid - %s\n", strError);
            return false;
        }
        if(o.scriptPubKey == findScript && o.nValue >= EVOLUTION_FEE_TX) foundOpReturn = true;

    }
    if(!foundOpReturn){
        strError = strprintf("Couldn't find opReturn %s in %s", nExpectedHash.ToString(), txCollateral.ToString());
        LogPrintf ("CEvolutionProposalBroadcast::IsEvolutionCollateralValid - %s\n", strError);
        return false;
    }

    // RETRIEVE CONFIRMATIONS AND NTIME
    /*
        - nTime starts as zero and is passed-by-reference out of this function and stored in the external proposal
        - nTime is never validated via the hashing mechanism and comes from a full-validated source (the blockchain)
    */

    int conf = GetIXConfirmations(nTxCollateralHash);
    if (nBlockHash != uint256(0)) {
        BlockMap::iterator mi = mapBlockIndex.find(nBlockHash);
        if (mi != mapBlockIndex.end() && (*mi).second) {
            CBlockIndex* pindex = (*mi).second;
            if (chainActive.Contains(pindex)) {
                conf += chainActive.Height() - pindex->nHeight + 1;
                nTime = pindex->nTime;
            }
        }
    }

    nConf = conf;

    //if we're syncing we won't have instantX information, so accept 1 confirmation 
    if(conf >= EVOLUTION_FEE_CONFIRMATIONS){
        return true;
    } else {
        strError = strprintf("Collateral requires at least %d confirmations - %d confirmations", EVOLUTION_FEE_CONFIRMATIONS, conf);
        LogPrintf ("CEvolutionProposalBroadcast::IsEvolutionCollateralValid - %s - %d confirmations\n", strError, conf);
        return false;
    }
}

void CEvolutionManager::CheckOrphanVotes()
{
    LOCK(cs);


    std::string strError = "";
    std::map<uint256, CEvolutionVote>::iterator it1 = mapOrphanGoldmineEvolutionVotes.begin();
    while(it1 != mapOrphanGoldmineEvolutionVotes.end()){
        if(evolution.UpdateProposal(((*it1).second), NULL, strError)){
            LogPrintf("CEvolutionManager::CheckOrphanVotes - Proposal/Evolution is known, activating and removing orphan vote\n");
            mapOrphanGoldmineEvolutionVotes.erase(it1++);
        } else {
            ++it1;
        }
    }
    std::map<uint256, CFinalizedEvolutionVote>::iterator it2 = mapOrphanFinalizedEvolutionVotes.begin();
    while(it2 != mapOrphanFinalizedEvolutionVotes.end()){
        if(evolution.UpdateFinalizedEvolution(((*it2).second),NULL, strError)){
            LogPrintf("CEvolutionManager::CheckOrphanVotes - Proposal/Evolution is known, activating and removing orphan vote\n");
            mapOrphanFinalizedEvolutionVotes.erase(it2++);
        } else {
            ++it2;
        }
    }
}

void CEvolutionManager::SubmitFinalEvolution()
{
    static int nSubmittedHeight = 0; // height at which final evolution was submitted last time
    int nCurrentHeight;
	{
		TRY_LOCK(cs_main, locked);
		if(!locked) return;
		if(!chainActive.Tip()) return;
		nCurrentHeight = chainActive.Height();
	}
  
	int nBlockStart = nCurrentHeight - nCurrentHeight % GetEvolutionPaymentCycleBlocks() + GetEvolutionPaymentCycleBlocks();
	if(nSubmittedHeight >= nBlockStart) return;
	if(nBlockStart - nCurrentHeight > 576*2) return; // allow submitting final evolution only when 2 days left before payments

	
    std::vector<CEvolutionProposal*> vEvolutionProposals = evolution.GetEvolution();
    std::string strEvolutionName = "main";
    std::vector<CTxEvolutionPayment> vecTxEvolutionPayments;

    for(unsigned int i = 0; i < vEvolutionProposals.size(); i++){
        CTxEvolutionPayment txEvolutionPayment;
        txEvolutionPayment.nProposalHash = vEvolutionProposals[i]->GetHash();
        txEvolutionPayment.payee = vEvolutionProposals[i]->GetPayee();
        txEvolutionPayment.nAmount = vEvolutionProposals[i]->GetAllotted();
        vecTxEvolutionPayments.push_back(txEvolutionPayment);
    }

    if(vecTxEvolutionPayments.size() < 1) {
        LogPrintf("CEvolutionManager::SubmitFinalEvolution - Found No Proposals For Period\n");
        return;
    }

    CFinalizedEvolutionBroadcast tempEvolution(strEvolutionName, nBlockStart, vecTxEvolutionPayments, 0);
    if(mapSeenFinalizedEvolutions.count(tempEvolution.GetHash())) {
        LogPrintf("CEvolutionManager::SubmitFinalEvolution - Evolution already exists - %s\n", tempEvolution.GetHash().ToString());    
        nSubmittedHeight = nCurrentHeight;
        return; //already exists
    }

    //create fee tx
    CTransaction tx;
    uint256 txidCollateral;
	
    if(!mapCollateralTxids.count(tempEvolution.GetHash())){
        CWalletTx wtx;
        if(!pwalletMain->GetEvolutionSystemCollateralTX(wtx, tempEvolution.GetHash(), false)){
            LogPrintf("CEvolutionManager::SubmitFinalEvolution - Can't make collateral transaction\n");
            return;
        }
        
        // make our change address
        CReserveKey reservekey(pwalletMain);
        //send the tx to the network
        pwalletMain->CommitTransaction(wtx, reservekey, "ix");
        tx = (CTransaction)wtx;
		txidCollateral = tx.GetHash();
		mapCollateralTxids.insert(make_pair(tempEvolution.GetHash(), txidCollateral));
    } else {
        txidCollateral = mapCollateralTxids[tempEvolution.GetHash()];
    }

	int conf = GetIXConfirmations(tx.GetHash());
	CTransaction txCollateral;
	uint256 nBlockHash;
	if(!GetTransaction(txidCollateral, txCollateral, nBlockHash, true)) {
		LogPrintf ("CBudgetManager::SubmitFinalBudget - Can't find collateral tx %s", txidCollateral.ToString());
		return;
	}
	
	if (nBlockHash != uint256(0)) {
		BlockMap::iterator mi = mapBlockIndex.find(nBlockHash);
		if (mi != mapBlockIndex.end() && (*mi).second) {
			CBlockIndex* pindex = (*mi).second;
			if (chainActive.Contains(pindex)) {
				conf += chainActive.Height() - pindex->nHeight + 1;
			}
		}
	}

	
    /*
        Wait will we have 1 extra confirmation, otherwise some clients might reject this feeTX
        -- This function is tied to NewBlock, so we will propagate this evolution while the block is also propagating
    */
    if(conf < EVOLUTION_FEE_CONFIRMATIONS+1){
        LogPrintf ("CEvolutionManager::SubmitFinalEvolution - Collateral requires at least %d confirmations - %s - %d confirmations\n", EVOLUTION_FEE_CONFIRMATIONS+1, txidCollateral.ToString(), conf);
		return;
    }

    //create the proposal incase we're the first to make it
    CFinalizedEvolutionBroadcast finalizedEvolutionBroadcast(strEvolutionName, nBlockStart, vecTxEvolutionPayments, txidCollateral);

    std::string strError = "";
    if(!finalizedEvolutionBroadcast.IsValid(strError)){
        LogPrintf("CEvolutionManager::SubmitFinalEvolution - Invalid finalized evolution - %s \n", strError);
        return;
    }

    LOCK(cs);
    mapSeenFinalizedEvolutions.insert(make_pair(finalizedEvolutionBroadcast.GetHash(), finalizedEvolutionBroadcast));
    finalizedEvolutionBroadcast.Relay();
    evolution.AddFinalizedEvolution(finalizedEvolutionBroadcast);
	nSubmittedHeight = nCurrentHeight;
	LogPrintf("CEvolutionManager::SubmitFinalEvolution - Done! %s\n", finalizedEvolutionBroadcast.GetHash().ToString());
}

//
// CEvolutionDB
//

CEvolutionDB::CEvolutionDB()
{
    pathDB = GetDataDir() / "evolution.dat";
    strMagicMessage = "GoldmineEvolution";
}

bool CEvolutionDB::Write(const CEvolutionManager& objToSave)
{
    LOCK(objToSave.cs);

    int64_t nStart = GetTimeMillis();

    // serialize, checksum data up to that point, then append checksum
    CDataStream ssObj(SER_DISK, CLIENT_VERSION);
    ssObj << strMagicMessage; // goldmine cache file specific magic message
    ssObj << FLATDATA(Params().MessageStart()); // network specific magic number
    ssObj << objToSave;
    uint256 hash = Hash(ssObj.begin(), ssObj.end());
    ssObj << hash;

    // open output file, and associate with CAutoFile
    FILE *file = fopen(pathDB.string().c_str(), "wb");
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s : Failed to open file %s", __func__, pathDB.string());

    // Write and commit header, data
    try {
        fileout << ssObj;
    }
    catch (std::exception &e) {
        return error("%s : Serialize or I/O error - %s", __func__, e.what());
    }
    fileout.fclose();

    LogPrintf("Written info to evolution.dat  %dms\n", GetTimeMillis() - nStart);

    return true;
}

CEvolutionDB::ReadResult CEvolutionDB::Read(CEvolutionManager& objToLoad, bool fDryRun)
{
    LOCK(objToLoad.cs);

    int64_t nStart = GetTimeMillis();
    // open input file, and associate with CAutoFile
    FILE *file = fopen(pathDB.string().c_str(), "rb");
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
    if (filein.IsNull())
    {
        error("%s : Failed to open file %s", __func__, pathDB.string());
        return FileError;
    }

    // use file size to size memory buffer
    int fileSize = boost::filesystem::file_size(pathDB);
    int dataSize = fileSize - sizeof(uint256);
    // Don't try to resize to a negative number if file is small
    if (dataSize < 0)
        dataSize = 0;
    vector<unsigned char> vchData;
    vchData.resize(dataSize);
    uint256 hashIn;

    // read data and checksum from file
    try {
        filein.read((char *)&vchData[0], dataSize);
        filein >> hashIn;
    }
    catch (std::exception &e) {
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return HashReadError;
    }
    filein.fclose();

    CDataStream ssObj(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssObj.begin(), ssObj.end());
    if (hashIn != hashTmp)
    {
        error("%s : Checksum mismatch, data corrupted", __func__);
        return IncorrectHash;
    }


    unsigned char pchMsgTmp[4];
    std::string strMagicMessageTmp;
    try {
        // de-serialize file header (goldmine cache file specific magic message) and ..
        ssObj >> strMagicMessageTmp;

        // ... verify the message matches predefined one
        if (strMagicMessage != strMagicMessageTmp)
        {
            error("%s : Invalid goldmine cache magic message", __func__);
            return IncorrectMagicMessage;
        }


        // de-serialize file header (network specific magic number) and ..
        ssObj >> FLATDATA(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp)))
        {
            error("%s : Invalid network magic number", __func__);
            return IncorrectMagicNumber;
        }

        // de-serialize data into CEvolutionManager object
        ssObj >> objToLoad;
    }
    catch (std::exception &e) {
        objToLoad.Clear();
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    LogPrintf("Loaded info from evolution.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrintf("  %s\n", objToLoad.ToString());
    if(!fDryRun) {
        LogPrintf("Evolution manager - cleaning....\n");
        objToLoad.CheckAndRemove();
        LogPrintf("Evolution manager - result:\n");
        LogPrintf("  %s\n", objToLoad.ToString());
    }

    return Ok;
}

void DumpEvolutions()
{
    int64_t nStart = GetTimeMillis();

    CEvolutionDB evolutiondb;
    CEvolutionManager tempEvolution;

    LogPrintf("Verifying evolution.dat format...\n");
    CEvolutionDB::ReadResult readResult = evolutiondb.Read(tempEvolution, true);
    // there was an error and it was not an error on file opening => do not proceed
    if (readResult == CEvolutionDB::FileError)
        LogPrintf("Missing evolutions file - evolution.dat, will try to recreate\n");
    else if (readResult != CEvolutionDB::Ok)
    {
        LogPrintf("Error reading evolution.dat: ");
        if(readResult == CEvolutionDB::IncorrectFormat)
            LogPrintf("magic is ok but data has invalid format, will try to recreate\n");
        else
        {
            LogPrintf("file format is unknown or invalid, please fix it manually\n");
            return;
        }
    }
    LogPrintf("Writting info to evolution.dat...\n");
    evolutiondb.Write(evolution);

    LogPrintf("Evolution dump finished  %dms\n", GetTimeMillis() - nStart);
}

bool CEvolutionManager::AddFinalizedEvolution(CFinalizedEvolution& finalizedEvolution)
{
    std::string strError = "";
    if(!finalizedEvolution.IsValid(strError)) return false;

    if(mapFinalizedEvolutions.count(finalizedEvolution.GetHash())) {
        return false;
    }

    mapFinalizedEvolutions.insert(make_pair(finalizedEvolution.GetHash(), finalizedEvolution));
    return true;
}

bool CEvolutionManager::AddProposal(CEvolutionProposal& evolutionProposal)
{
    LOCK(cs);
    std::string strError = "";
    if(!evolutionProposal.IsValid(strError)) {
        LogPrintf("CEvolutionManager::AddProposal - invalid evolution proposal - %s\n", strError);
        return false;
    }

    if(mapProposals.count(evolutionProposal.GetHash())) {
        return false;
    }

    mapProposals.insert(make_pair(evolutionProposal.GetHash(), evolutionProposal));
    return true;
}

void CEvolutionManager::CheckAndRemove()
{
    LogPrintf("CEvolutionManager::CheckAndRemove\n");

    std::string strError = "";

    LogPrintf("CEvolutionManager::CheckAndRemove - mapFinalizedEvolutions cleanup - size: %d\n", mapFinalizedEvolutions.size());
    std::map<uint256, CFinalizedEvolution>::iterator it = mapFinalizedEvolutions.begin();
    while(it != mapFinalizedEvolutions.end())
    {
        CFinalizedEvolution* pfinalizedEvolution = &((*it).second);

        pfinalizedEvolution->fValid = pfinalizedEvolution->IsValid(strError);
        LogPrintf("CEvolutionManager::CheckAndRemove - pfinalizedEvolution->IsValid - strError: %s\n", strError);
        if(pfinalizedEvolution->fValid) {
            pfinalizedEvolution->AutoCheck();
        }

        ++it;
    }

    LogPrintf("CEvolutionManager::CheckAndRemove - mapProposals cleanup - size: %d\n", mapProposals.size());
    std::map<uint256, CEvolutionProposal>::iterator it2 = mapProposals.begin();
    while(it2 != mapProposals.end())
    {
        CEvolutionProposal* pevolutionProposal = &((*it2).second);
        pevolutionProposal->fValid = pevolutionProposal->IsValid(strError);
        ++it2;
    }

    LogPrintf("CEvolutionManager::CheckAndRemove - PASSED\n");
}

void CEvolutionManager::FillBlockPayee(CMutableTransaction& txNew, CAmount nFees)
{
    LOCK(cs);

    CBlockIndex* pindexPrev = chainActive.Tip();
    if(!pindexPrev) return;

    int nHighestCount = 0;
    CScript payee;
    CAmount nAmount = 0;

    // ------- Grab The Highest Count

    std::map<uint256, CFinalizedEvolution>::iterator it = mapFinalizedEvolutions.begin();
    while(it != mapFinalizedEvolutions.end())
    {
        CFinalizedEvolution* pfinalizedEvolution = &((*it).second);
        if(pfinalizedEvolution->GetVoteCount() > nHighestCount &&
                pindexPrev->nHeight + 1 >= pfinalizedEvolution->GetBlockStart() &&
                pindexPrev->nHeight + 1 <= pfinalizedEvolution->GetBlockEnd() &&
                pfinalizedEvolution->GetPayeeAndAmount(pindexPrev->nHeight + 1, payee, nAmount)){
                    nHighestCount = pfinalizedEvolution->GetVoteCount();
        }

        ++it;
    }

    CAmount blockValue = GetBlockValue(pindexPrev->nBits, pindexPrev->nHeight, nFees);

    //miners get the full amount on these blocks
    txNew.vout[0].nValue = blockValue;

    if(nHighestCount > 0){
        txNew.vout.resize(2);

        //these are super blocks, so their value can be much larger than normal
        txNew.vout[1].scriptPubKey = payee;
        txNew.vout[1].nValue = nAmount;

        CTxDestination address1;
        ExtractDestination(payee, address1);
        CBitcoinAddress address2(address1);

        LogPrintf("CEvolutionManager::FillBlockPayee - Evolution payment to %s for %lld\n", address2.ToString(), nAmount);
    }

}

CFinalizedEvolution *CEvolutionManager::FindFinalizedEvolution(uint256 nHash)
{
    if(mapFinalizedEvolutions.count(nHash))
        return &mapFinalizedEvolutions[nHash];

    return NULL;
}

CEvolutionProposal *CEvolutionManager::FindProposal(const std::string &strProposalName)
{
    //find the prop with the highest yes count

    int nYesCount = -99999;
    CEvolutionProposal* pevolutionProposal = NULL;

    std::map<uint256, CEvolutionProposal>::iterator it = mapProposals.begin();
    while(it != mapProposals.end()){
        if((*it).second.strProposalName == strProposalName && (*it).second.GetYeas() > nYesCount){
            pevolutionProposal = &((*it).second);
            nYesCount = pevolutionProposal->GetYeas();
        }
        ++it;
    }

    if(nYesCount == -99999) return NULL;

    return pevolutionProposal;
}

CEvolutionProposal *CEvolutionManager::FindProposal(uint256 nHash)
{
    LOCK(cs);

    if(mapProposals.count(nHash))
        return &mapProposals[nHash];

    return NULL;
}

bool CEvolutionManager::IsEvolutionPaymentBlock(int nBlockHeight)
{
    int nHighestCount = -1;

    std::map<uint256, CFinalizedEvolution>::iterator it = mapFinalizedEvolutions.begin();
    while(it != mapFinalizedEvolutions.end())
    {
        CFinalizedEvolution* pfinalizedEvolution = &((*it).second);
        if(pfinalizedEvolution->GetVoteCount() > nHighestCount && 
            nBlockHeight >= pfinalizedEvolution->GetBlockStart() && 
            nBlockHeight <= pfinalizedEvolution->GetBlockEnd()){
            nHighestCount = pfinalizedEvolution->GetVoteCount();
        }

        ++it;
    }

    /*
        If evolution doesn't have 5% of the network votes, then we should pay a goldmine instead
    */
    if(nHighestCount > gmineman.CountEnabled(MIN_EVOLUTION_PEER_PROTO_VERSION)/20) return true;

    return false;
}

bool CEvolutionManager::HasNextFinalizedEvolution()
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if(!pindexPrev) return false;

    if(goldmineSync.IsEvolutionFinEmpty()) return true;

    int nBlockStart = pindexPrev->nHeight - pindexPrev->nHeight % GetEvolutionPaymentCycleBlocks() + GetEvolutionPaymentCycleBlocks();
    if(nBlockStart - pindexPrev->nHeight > 576*2) return true; //we wouldn't have the evolution yet

    if(evolution.IsEvolutionPaymentBlock(nBlockStart)) return true;

    LogPrintf("CEvolutionManager::HasNextFinalizedEvolution() - Client is missing evolution - %lli\n", nBlockStart);

    return false;
}

bool CEvolutionManager::IsTransactionValid(const CTransaction& txNew, int nBlockHeight)
{
    LOCK(cs);

    int nHighestCount = 0;
    std::vector<CFinalizedEvolution*> ret;

    // ------- Grab The Highest Count

    std::map<uint256, CFinalizedEvolution>::iterator it = mapFinalizedEvolutions.begin();
    while(it != mapFinalizedEvolutions.end())
    {
        CFinalizedEvolution* pfinalizedEvolution = &((*it).second);
        if(pfinalizedEvolution->GetVoteCount() > nHighestCount &&
                nBlockHeight >= pfinalizedEvolution->GetBlockStart() &&
                nBlockHeight <= pfinalizedEvolution->GetBlockEnd()){
                    nHighestCount = pfinalizedEvolution->GetVoteCount();
        }

        ++it;
    }

    /*
        If evolution doesn't have 5% of the network votes, then we should pay a goldmine instead
    */
    if(nHighestCount < gmineman.CountEnabled(MIN_EVOLUTION_PEER_PROTO_VERSION)/20) return false;

    // check the highest finalized evolutions (+/- 10% to assist in consensus)

    it = mapFinalizedEvolutions.begin();
    while(it != mapFinalizedEvolutions.end())
    {
        CFinalizedEvolution* pfinalizedEvolution = &((*it).second);

        if(pfinalizedEvolution->GetVoteCount() > nHighestCount - gmineman.CountEnabled(MIN_EVOLUTION_PEER_PROTO_VERSION)/10){
            if(nBlockHeight >= pfinalizedEvolution->GetBlockStart() && nBlockHeight <= pfinalizedEvolution->GetBlockEnd()){
                if(pfinalizedEvolution->IsTransactionValid(txNew, nBlockHeight)){
                    return true;
                }
            }
        }

        ++it;
    }

    //we looked through all of the known evolutions
    return false;
}

std::vector<CEvolutionProposal*> CEvolutionManager::GetAllProposals()
{
    LOCK(cs);

    std::vector<CEvolutionProposal*> vEvolutionProposalRet;

    std::map<uint256, CEvolutionProposal>::iterator it = mapProposals.begin();
    while(it != mapProposals.end())
    {
        (*it).second.CleanAndRemove(false);

        CEvolutionProposal* pevolutionProposal = &((*it).second);
        vEvolutionProposalRet.push_back(pevolutionProposal);

        ++it;
    }

    return vEvolutionProposalRet;
}

//
// Sort by votes, if there's a tie sort by their feeHash TX
//
struct sortProposalsByVotes {
    bool operator()(const std::pair<CEvolutionProposal*, int> &left, const std::pair<CEvolutionProposal*, int> &right) {
      if( left.second != right.second)
        return (left.second > right.second);
      return (left.first->nFeeTXHash > right.first->nFeeTXHash);
    }
};

//Need to review this function
std::vector<CEvolutionProposal*> CEvolutionManager::GetEvolution()
{
    LOCK(cs);

    // ------- Sort evolutions by Yes Count

    std::vector<std::pair<CEvolutionProposal*, int> > vEvolutionPorposalsSort;

    std::map<uint256, CEvolutionProposal>::iterator it = mapProposals.begin();
    while(it != mapProposals.end()){
        (*it).second.CleanAndRemove(false);
        vEvolutionPorposalsSort.push_back(make_pair(&((*it).second), (*it).second.GetYeas()-(*it).second.GetNays()));
        ++it;
    }

    std::sort(vEvolutionPorposalsSort.begin(), vEvolutionPorposalsSort.end(), sortProposalsByVotes());

    // ------- Grab The Evolutions In Order

    std::vector<CEvolutionProposal*> vEvolutionProposalsRet;

    CAmount nEvolutionAllocated = 0;
    CBlockIndex* pindexPrev = chainActive.Tip();
    if(pindexPrev == NULL) return vEvolutionProposalsRet;

    int nBlockStart = pindexPrev->nHeight - pindexPrev->nHeight % GetEvolutionPaymentCycleBlocks() + GetEvolutionPaymentCycleBlocks();
    int nBlockEnd  =  nBlockStart + GetEvolutionPaymentCycleBlocks() - 1;
    CAmount nTotalEvolution = GetTotalEvolution(nBlockStart);


    std::vector<std::pair<CEvolutionProposal*, int> >::iterator it2 = vEvolutionPorposalsSort.begin();
    while(it2 != vEvolutionPorposalsSort.end())
    {
        CEvolutionProposal* pevolutionProposal = (*it2).first;

        //prop start/end should be inside this period
        if(pevolutionProposal->fValid && pevolutionProposal->nBlockStart <= nBlockStart &&
                pevolutionProposal->nBlockEnd >= nBlockEnd &&
                pevolutionProposal->GetYeas() - pevolutionProposal->GetNays() > gmineman.CountEnabled(MIN_EVOLUTION_PEER_PROTO_VERSION)/10 && 
                pevolutionProposal->IsEstablished())
        {
            if(pevolutionProposal->GetAmount() + nEvolutionAllocated <= nTotalEvolution) {
                pevolutionProposal->SetAllotted(pevolutionProposal->GetAmount());
                nEvolutionAllocated += pevolutionProposal->GetAmount();
                vEvolutionProposalsRet.push_back(pevolutionProposal);
            } else {
                pevolutionProposal->SetAllotted(0);
            }
        }

        ++it2;
    }

    return vEvolutionProposalsRet;
}

struct sortFinalizedEvolutionsByVotes {
    bool operator()(const std::pair<CFinalizedEvolution*, int> &left, const std::pair<CFinalizedEvolution*, int> &right) {
        return left.second > right.second;
    }
};

std::vector<CFinalizedEvolution*> CEvolutionManager::GetFinalizedEvolutions()
{
    LOCK(cs);

    std::vector<CFinalizedEvolution*> vFinalizedEvolutionsRet;
    std::vector<std::pair<CFinalizedEvolution*, int> > vFinalizedEvolutionsSort;

    // ------- Grab The Evolutions In Order

    std::map<uint256, CFinalizedEvolution>::iterator it = mapFinalizedEvolutions.begin();
    while(it != mapFinalizedEvolutions.end())
    {
        CFinalizedEvolution* pfinalizedEvolution = &((*it).second);

        vFinalizedEvolutionsSort.push_back(make_pair(pfinalizedEvolution, pfinalizedEvolution->GetVoteCount()));
        ++it;
    }
    std::sort(vFinalizedEvolutionsSort.begin(), vFinalizedEvolutionsSort.end(), sortFinalizedEvolutionsByVotes());

    std::vector<std::pair<CFinalizedEvolution*, int> >::iterator it2 = vFinalizedEvolutionsSort.begin();
    while(it2 != vFinalizedEvolutionsSort.end())
    {
        vFinalizedEvolutionsRet.push_back((*it2).first);
        ++it2;
    }

    return vFinalizedEvolutionsRet;
}

std::string CEvolutionManager::GetRequiredPaymentsString(int nBlockHeight)
{
    LOCK(cs);

    std::string ret = "unknown-evolution";

    std::map<uint256, CFinalizedEvolution>::iterator it = mapFinalizedEvolutions.begin();
    while(it != mapFinalizedEvolutions.end())
    {
        CFinalizedEvolution* pfinalizedEvolution = &((*it).second);
        if(nBlockHeight >= pfinalizedEvolution->GetBlockStart() && nBlockHeight <= pfinalizedEvolution->GetBlockEnd()){
            CTxEvolutionPayment payment;
            if(pfinalizedEvolution->GetEvolutionPaymentByBlock(nBlockHeight, payment)){
                if(ret == "unknown-evolution"){
                    ret = payment.nProposalHash.ToString();
                } else {
                    ret += ",";
                    ret += payment.nProposalHash.ToString();
                }
            } else {
                LogPrintf("CEvolutionManager::GetRequiredPaymentsString - Couldn't find evolution payment for block %d\n", nBlockHeight);
            }
        }

        ++it;
    }

    return ret;
}

CAmount CEvolutionManager::GetTotalEvolution(int nHeight)
{
    if(chainActive.Tip() == NULL) return 0;

    //get min block value and calculate from that
    CAmount nSubsidy = 5 * COIN;

    if(Params().NetworkID() == CBaseChainParams::TESTNET){
        for(int i = 210240; i <= nHeight; i += 210240) nSubsidy -= nSubsidy/14;
    } else {
        // yearly decline of production by 7.1% per year, projected 21.3M coins max by year 2050.
        for(int i = 210240; i <= nHeight; i += 210240) nSubsidy -= nSubsidy/14;
    }

    // Amount of blocks in a months period of time (using 2.6 minutes per) = (60*24*30)/2.6
    if(Params().NetworkID() == CBaseChainParams::MAIN) return ((nSubsidy/100)*10)*576*30;

    //for testing purposes
    return ((nSubsidy/100)*10)*50;
}

void CEvolutionManager::NewBlock()
{
    TRY_LOCK(cs, fEvolutionNewBlock);
    if(!fEvolutionNewBlock) return;

    if (goldmineSync.RequestedGoldmineAssets <= GOLDMINE_SYNC_EVOLUTION) return;

    if (strEvolutionMode == "suggest") { //suggest the evolution we see
        SubmitFinalEvolution();
    }

    //this function should be called 1/6 blocks, allowing up to 100 votes per day on all proposals
    if(chainActive.Height() % 6 != 0) return;

    // incremental sync with our peers
    if(goldmineSync.IsSynced()){
        LogPrintf("CEvolutionManager::NewBlock - incremental sync started\n");
        if(chainActive.Height() % 600 == rand() % 600) {
            ClearSeen();
            ResetSync();
        }

        LOCK(cs_vNodes);
        BOOST_FOREACH(CNode* pnode, vNodes)
            if(pnode->nVersion >= MIN_EVOLUTION_PEER_PROTO_VERSION) 
                Sync(pnode, 0, true);
        
        MarkSynced();
    }
     

    CheckAndRemove();

    //remove invalid votes once in a while (we have to check the signatures and validity of every vote, somewhat CPU intensive)

    LogPrintf("CEvolutionManager::NewBlock - askedForSourceProposalOrEvolution cleanup - size: %d\n", askedForSourceProposalOrEvolution.size());
    std::map<uint256, int64_t>::iterator it = askedForSourceProposalOrEvolution.begin();
    while(it != askedForSourceProposalOrEvolution.end()){
        if((*it).second > GetTime() - (60*60*24)){
            ++it;
        } else {
            askedForSourceProposalOrEvolution.erase(it++);
        }
    }

    LogPrintf("CEvolutionManager::NewBlock - mapProposals cleanup - size: %d\n", mapProposals.size());
    std::map<uint256, CEvolutionProposal>::iterator it2 = mapProposals.begin();
    while(it2 != mapProposals.end()){
        (*it2).second.CleanAndRemove(false);
        ++it2;
    }

    LogPrintf("CEvolutionManager::NewBlock - mapFinalizedEvolutions cleanup - size: %d\n", mapFinalizedEvolutions.size());
    std::map<uint256, CFinalizedEvolution>::iterator it3 = mapFinalizedEvolutions.begin();
    while(it3 != mapFinalizedEvolutions.end()){
        (*it3).second.CleanAndRemove(false);
        ++it3;
    }

    LogPrintf("CEvolutionManager::NewBlock - vecImmatureEvolutionProposals cleanup - size: %d\n", vecImmatureEvolutionProposals.size());
    std::vector<CEvolutionProposalBroadcast>::iterator it4 = vecImmatureEvolutionProposals.begin();
    while(it4 != vecImmatureEvolutionProposals.end())
    {
        std::string strError = "";
        int nConf = 0;
        if(!IsEvolutionCollateralValid((*it4).nFeeTXHash, (*it4).GetHash(), strError, (*it4).nTime, nConf)){
            ++it4;
            continue;
        }

        if(!(*it4).IsValid(strError)) {
            LogPrintf("mprop (immature) - invalid evolution proposal - %s\n", strError);
            it4 = vecImmatureEvolutionProposals.erase(it4); 
            continue;
        }

        CEvolutionProposal evolutionProposal((*it4));
        if(AddProposal(evolutionProposal)) {(*it4).Relay();}

        LogPrintf("mprop (immature) - new evolution - %s\n", (*it4).GetHash().ToString());
        it4 = vecImmatureEvolutionProposals.erase(it4); 
    }

    LogPrintf("CEvolutionManager::NewBlock - vecImmatureFinalizedEvolutions cleanup - size: %d\n", vecImmatureFinalizedEvolutions.size());
    std::vector<CFinalizedEvolutionBroadcast>::iterator it5 = vecImmatureFinalizedEvolutions.begin();
    while(it5 != vecImmatureFinalizedEvolutions.end())
    {
        std::string strError = "";
        int nConf = 0;
        if(!IsEvolutionCollateralValid((*it5).nFeeTXHash, (*it5).GetHash(), strError, (*it5).nTime, nConf)){
            ++it5;
            continue;
        }

        if(!(*it5).IsValid(strError)) {
            LogPrintf("fbs (immature) - invalid finalized evolution - %s\n", strError);
            it5 = vecImmatureFinalizedEvolutions.erase(it5); 
            continue;
        }

        LogPrintf("fbs (immature) - new finalized evolution - %s\n", (*it5).GetHash().ToString());

        CFinalizedEvolution finalizedEvolution((*it5));
        if(AddFinalizedEvolution(finalizedEvolution)) {(*it5).Relay();}

        it5 = vecImmatureFinalizedEvolutions.erase(it5); 
    }
    LogPrintf("CEvolutionManager::NewBlock - PASSED\n");
}

void CEvolutionManager::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    // lite mode is not supported
    if(fLiteMode) return;
    if(!goldmineSync.IsBlockchainSynced()) return;

    LOCK(cs_evolution);

    if (strCommand == "gmvs") { //Goldmine vote sync
        uint256 nProp;
        vRecv >> nProp;

        if(Params().NetworkID() == CBaseChainParams::MAIN){
            if(nProp == 0) {
                if(pfrom->HasFulfilledRequest("gmvs")) {
                    LogPrintf("gmvs - peer already asked me for the list\n");
                    Misbehaving(pfrom->GetId(), 20);
                    return;
                }
                pfrom->FulfilledRequest("gmvs");
            }
        }

        Sync(pfrom, nProp);
        LogPrintf("gmvs - Sent Goldmine votes to %s\n", pfrom->addr.ToString());
    }

    if (strCommand == "mprop") { //Goldmine Proposal
        CEvolutionProposalBroadcast evolutionProposalBroadcast;
        vRecv >> evolutionProposalBroadcast;

        if(mapSeenGoldmineEvolutionProposals.count(evolutionProposalBroadcast.GetHash())){
            goldmineSync.AddedEvolutionItem(evolutionProposalBroadcast.GetHash());
            return;
        }

        std::string strError = "";
        int nConf = 0;
        if(!IsEvolutionCollateralValid(evolutionProposalBroadcast.nFeeTXHash, evolutionProposalBroadcast.GetHash(), strError, evolutionProposalBroadcast.nTime, nConf)){
            LogPrintf("Proposal FeeTX is not valid - %s - %s\n", evolutionProposalBroadcast.nFeeTXHash.ToString(), strError);
            if(nConf >= 1) vecImmatureEvolutionProposals.push_back(evolutionProposalBroadcast);
            return;
        }

        mapSeenGoldmineEvolutionProposals.insert(make_pair(evolutionProposalBroadcast.GetHash(), evolutionProposalBroadcast));

        if(!evolutionProposalBroadcast.IsValid(strError)) {
            LogPrintf("mprop - invalid evolution proposal - %s\n", strError);
            return;
        }

        CEvolutionProposal evolutionProposal(evolutionProposalBroadcast);
        if(AddProposal(evolutionProposal)) {evolutionProposalBroadcast.Relay();}
        goldmineSync.AddedEvolutionItem(evolutionProposalBroadcast.GetHash());

        LogPrintf("mprop - new evolution - %s\n", evolutionProposalBroadcast.GetHash().ToString());

        //We might have active votes for this proposal that are valid now
        CheckOrphanVotes();
    }

    if (strCommand == "mvote") { //Goldmine Vote
        CEvolutionVote vote;
        vRecv >> vote;
        vote.fValid = true;

        if(mapSeenGoldmineEvolutionVotes.count(vote.GetHash())){
            goldmineSync.AddedEvolutionItem(vote.GetHash());
            return;
        }

        CGoldmine* pgm = gmineman.Find(vote.vin);
        if(pgm == NULL) {
            LogPrint("gmevolution", "mvote - unknown goldmine - vin: %s\n", vote.vin.ToString());
            gmineman.AskForGM(pfrom, vote.vin);
            return;
        }


        mapSeenGoldmineEvolutionVotes.insert(make_pair(vote.GetHash(), vote));
        if(!vote.SignatureValid(true)){
            LogPrintf("mvote - signature invalid\n");
            if(goldmineSync.IsSynced()) Misbehaving(pfrom->GetId(), 20);
            // it could just be a non-synced goldmine
            gmineman.AskForGM(pfrom, vote.vin);
            return;
        }
        
        std::string strError = "";
        if(UpdateProposal(vote, pfrom, strError)) {
            vote.Relay();
            goldmineSync.AddedEvolutionItem(vote.GetHash());
        }

        LogPrintf("mvote - new evolution vote - %s\n", vote.GetHash().ToString());
    }

    if (strCommand == "fbs") { //Finalized Evolution Suggestion
        CFinalizedEvolutionBroadcast finalizedEvolutionBroadcast;
        vRecv >> finalizedEvolutionBroadcast;

        if(mapSeenFinalizedEvolutions.count(finalizedEvolutionBroadcast.GetHash())){
            goldmineSync.AddedEvolutionItem(finalizedEvolutionBroadcast.GetHash());
            return;
        }

        std::string strError = "";
        int nConf = 0;
        if(!IsEvolutionCollateralValid(finalizedEvolutionBroadcast.nFeeTXHash, finalizedEvolutionBroadcast.GetHash(), strError, finalizedEvolutionBroadcast.nTime, nConf)){
            LogPrintf("Finalized Evolution FeeTX is not valid - %s - %s\n", finalizedEvolutionBroadcast.nFeeTXHash.ToString(), strError);

            if(nConf >= 1) vecImmatureFinalizedEvolutions.push_back(finalizedEvolutionBroadcast);
            return;
        }

        mapSeenFinalizedEvolutions.insert(make_pair(finalizedEvolutionBroadcast.GetHash(), finalizedEvolutionBroadcast));

        if(!finalizedEvolutionBroadcast.IsValid(strError)) {
            LogPrintf("fbs - invalid finalized evolution - %s\n", strError);
            return;
        }

        LogPrintf("fbs - new finalized evolution - %s\n", finalizedEvolutionBroadcast.GetHash().ToString());

        CFinalizedEvolution finalizedEvolution(finalizedEvolutionBroadcast);
        if(AddFinalizedEvolution(finalizedEvolution)) {finalizedEvolutionBroadcast.Relay();}
        goldmineSync.AddedEvolutionItem(finalizedEvolutionBroadcast.GetHash());

        //we might have active votes for this evolution that are now valid
        CheckOrphanVotes();
    }

    if (strCommand == "fbvote") { //Finalized Evolution Vote
        CFinalizedEvolutionVote vote;
        vRecv >> vote;
        vote.fValid = true;

        if(mapSeenFinalizedEvolutionVotes.count(vote.GetHash())){
            goldmineSync.AddedEvolutionItem(vote.GetHash());
            return;
        }

        CGoldmine* pgm = gmineman.Find(vote.vin);
        if(pgm == NULL) {
            LogPrint("gmevolution", "fbvote - unknown goldmine - vin: %s\n", vote.vin.ToString());
            gmineman.AskForGM(pfrom, vote.vin);
            return;
        }

        mapSeenFinalizedEvolutionVotes.insert(make_pair(vote.GetHash(), vote));
        if(!vote.SignatureValid(true)){
            LogPrintf("fbvote - signature invalid\n");
            if(goldmineSync.IsSynced()) Misbehaving(pfrom->GetId(), 20);
            // it could just be a non-synced goldmine
            gmineman.AskForGM(pfrom, vote.vin);
            return;
        }

        std::string strError = "";
        if(UpdateFinalizedEvolution(vote, pfrom, strError)) {
            vote.Relay();
            goldmineSync.AddedEvolutionItem(vote.GetHash());

            LogPrintf("fbvote - new finalized evolution vote - %s\n", vote.GetHash().ToString());
        } else {
            LogPrintf("fbvote - rejected finalized evolution vote - %s - %s\n", vote.GetHash().ToString(), strError);
        }
    }
}

bool CEvolutionManager::PropExists(uint256 nHash)
{
    if(mapProposals.count(nHash)) return true;
    return false;
}

//mark that a full sync is needed
void CEvolutionManager::ResetSync()
{
    LOCK(cs);


    std::map<uint256, CEvolutionProposalBroadcast>::iterator it1 = mapSeenGoldmineEvolutionProposals.begin();
    while(it1 != mapSeenGoldmineEvolutionProposals.end()){
        CEvolutionProposal* pevolutionProposal = FindProposal((*it1).first);
        if(pevolutionProposal && pevolutionProposal->fValid){
        
            //mark votes
            std::map<uint256, CEvolutionVote>::iterator it2 = pevolutionProposal->mapVotes.begin();
            while(it2 != pevolutionProposal->mapVotes.end()){
                (*it2).second.fSynced = false;
                ++it2;
            }
        }
        ++it1;
    }

    std::map<uint256, CFinalizedEvolutionBroadcast>::iterator it3 = mapSeenFinalizedEvolutions.begin();
    while(it3 != mapSeenFinalizedEvolutions.end()){
        CFinalizedEvolution* pfinalizedEvolution = FindFinalizedEvolution((*it3).first);
        if(pfinalizedEvolution && pfinalizedEvolution->fValid){

            //send votes
            std::map<uint256, CFinalizedEvolutionVote>::iterator it4 = pfinalizedEvolution->mapVotes.begin();
            while(it4 != pfinalizedEvolution->mapVotes.end()){
                (*it4).second.fSynced = false;
                ++it4;
            }
        }
        ++it3;
    }
}

void CEvolutionManager::MarkSynced()
{
    LOCK(cs);

    /*
        Mark that we've sent all valid items
    */

    std::map<uint256, CEvolutionProposalBroadcast>::iterator it1 = mapSeenGoldmineEvolutionProposals.begin();
    while(it1 != mapSeenGoldmineEvolutionProposals.end()){
        CEvolutionProposal* pevolutionProposal = FindProposal((*it1).first);
        if(pevolutionProposal && pevolutionProposal->fValid){
        
            //mark votes
            std::map<uint256, CEvolutionVote>::iterator it2 = pevolutionProposal->mapVotes.begin();
            while(it2 != pevolutionProposal->mapVotes.end()){
                if((*it2).second.fValid)
                    (*it2).second.fSynced = true;
                ++it2;
            }
        }
        ++it1;
    }

    std::map<uint256, CFinalizedEvolutionBroadcast>::iterator it3 = mapSeenFinalizedEvolutions.begin();
    while(it3 != mapSeenFinalizedEvolutions.end()){
        CFinalizedEvolution* pfinalizedEvolution = FindFinalizedEvolution((*it3).first);
        if(pfinalizedEvolution && pfinalizedEvolution->fValid){

            //mark votes
            std::map<uint256, CFinalizedEvolutionVote>::iterator it4 = pfinalizedEvolution->mapVotes.begin();
            while(it4 != pfinalizedEvolution->mapVotes.end()){
                if((*it4).second.fValid)
                    (*it4).second.fSynced = true;
                ++it4;
            }
        }
        ++it3;
    }

}


void CEvolutionManager::Sync(CNode* pfrom, uint256 nProp, bool fPartial)
{
    LOCK(cs);

    /*
        Sync with a client on the network

        --

        This code checks each of the hash maps for all known evolution proposals and finalized evolution proposals, then checks them against the
        evolution object to see if they're OK. If all checks pass, we'll send it to the peer.

    */

    int nInvCount = 0;

    std::map<uint256, CEvolutionProposalBroadcast>::iterator it1 = mapSeenGoldmineEvolutionProposals.begin();
    while(it1 != mapSeenGoldmineEvolutionProposals.end()){
        CEvolutionProposal* pevolutionProposal = FindProposal((*it1).first);
        if(pevolutionProposal && pevolutionProposal->fValid && (nProp == 0 || (*it1).first == nProp)){
            pfrom->PushInventory(CInv(MSG_EVOLUTION_PROPOSAL, (*it1).second.GetHash()));
            nInvCount++;
        
            //send votes
            std::map<uint256, CEvolutionVote>::iterator it2 = pevolutionProposal->mapVotes.begin();
            while(it2 != pevolutionProposal->mapVotes.end()){
                if((*it2).second.fValid){
                    if((fPartial && !(*it2).second.fSynced) || !fPartial) {
                        pfrom->PushInventory(CInv(MSG_EVOLUTION_VOTE, (*it2).second.GetHash()));
                        nInvCount++;
                    }
                }
                ++it2;
            }
        }
        ++it1;
    }

    pfrom->PushMessage("ssc", GOLDMINE_SYNC_EVOLUTION_PROP, nInvCount);

    LogPrintf("CEvolutionManager::Sync - sent %d items\n", nInvCount);

    nInvCount = 0;

    std::map<uint256, CFinalizedEvolutionBroadcast>::iterator it3 = mapSeenFinalizedEvolutions.begin();
    while(it3 != mapSeenFinalizedEvolutions.end()){
        CFinalizedEvolution* pfinalizedEvolution = FindFinalizedEvolution((*it3).first);
        if(pfinalizedEvolution && pfinalizedEvolution->fValid && (nProp == 0 || (*it3).first == nProp)){
            pfrom->PushInventory(CInv(MSG_EVOLUTION_FINALIZED, (*it3).second.GetHash()));
            nInvCount++;

            //send votes
            std::map<uint256, CFinalizedEvolutionVote>::iterator it4 = pfinalizedEvolution->mapVotes.begin();
            while(it4 != pfinalizedEvolution->mapVotes.end()){
                if((*it4).second.fValid) {
                    if((fPartial && !(*it4).second.fSynced) || !fPartial) {
                        pfrom->PushInventory(CInv(MSG_EVOLUTION_FINALIZED_VOTE, (*it4).second.GetHash()));
                        nInvCount++;
                    }
                }
                ++it4;
            }
        }
        ++it3;
    }

    pfrom->PushMessage("ssc", GOLDMINE_SYNC_EVOLUTION_FIN, nInvCount);
    LogPrintf("CEvolutionManager::Sync - sent %d items\n", nInvCount);

}

bool CEvolutionManager::UpdateProposal(CEvolutionVote& vote, CNode* pfrom, std::string& strError)
{
    LOCK(cs);

    if(!mapProposals.count(vote.nProposalHash)){
        if(pfrom){
            // only ask for missing items after our syncing process is complete -- 
            //   otherwise we'll think a full sync succeeded when they return a result
            if(!goldmineSync.IsSynced()) return false;

            LogPrintf("CEvolutionManager::UpdateProposal - Unknown proposal %d, asking for source proposal\n", vote.nProposalHash.ToString());
            mapOrphanGoldmineEvolutionVotes[vote.nProposalHash] = vote;

            if(!askedForSourceProposalOrEvolution.count(vote.nProposalHash)){
                pfrom->PushMessage("gmvs", vote.nProposalHash);
                askedForSourceProposalOrEvolution[vote.nProposalHash] = GetTime();
            }
        }

        strError = "Proposal not found!";
        return false;
    }


    return mapProposals[vote.nProposalHash].AddOrUpdateVote(vote, strError);
}

bool CEvolutionManager::UpdateFinalizedEvolution(CFinalizedEvolutionVote& vote, CNode* pfrom, std::string& strError)
{
    LOCK(cs);

    if(!mapFinalizedEvolutions.count(vote.nEvolutionHash)){
        if(pfrom){
            // only ask for missing items after our syncing process is complete -- 
            //   otherwise we'll think a full sync succeeded when they return a result
            if(!goldmineSync.IsSynced()) return false;

            LogPrintf("CEvolutionManager::UpdateFinalizedEvolution - Unknown Finalized Proposal %s, asking for source evolution\n", vote.nEvolutionHash.ToString());
            mapOrphanFinalizedEvolutionVotes[vote.nEvolutionHash] = vote;

            if(!askedForSourceProposalOrEvolution.count(vote.nEvolutionHash)){
                pfrom->PushMessage("gmvs", vote.nEvolutionHash);
                askedForSourceProposalOrEvolution[vote.nEvolutionHash] = GetTime();
            }

        }

        strError = "Finalized Evolution not found!";
        return false;
    }

    return mapFinalizedEvolutions[vote.nEvolutionHash].AddOrUpdateVote(vote, strError);
}

CEvolutionProposal::CEvolutionProposal()
{
    strProposalName = "unknown";
    nBlockStart = 0;
    nBlockEnd = 0;
    nAmount = 0;
    nTime = 0;
    fValid = true;
}

CEvolutionProposal::CEvolutionProposal(std::string strProposalNameIn, std::string strURLIn, int nBlockStartIn, int nBlockEndIn, CScript addressIn, CAmount nAmountIn, uint256 nFeeTXHashIn)
{
    strProposalName = strProposalNameIn;
    strURL = strURLIn;
    nBlockStart = nBlockStartIn;
    nBlockEnd = nBlockEndIn;
    address = addressIn;
    nAmount = nAmountIn;
    nFeeTXHash = nFeeTXHashIn;
    fValid = true;
}

CEvolutionProposal::CEvolutionProposal(const CEvolutionProposal& other)
{
    strProposalName = other.strProposalName;
    strURL = other.strURL;
    nBlockStart = other.nBlockStart;
    nBlockEnd = other.nBlockEnd;
    address = other.address;
    nAmount = other.nAmount;
    nTime = other.nTime;
    nFeeTXHash = other.nFeeTXHash;
    mapVotes = other.mapVotes;
    fValid = true;
}

bool CEvolutionProposal::IsValid(std::string& strError, bool fCheckCollateral)
{
    if(GetNays() - GetYeas() > gmineman.CountEnabled(MIN_EVOLUTION_PEER_PROTO_VERSION)/10){
         strError = "Active removal";
         return false;
    }

    if(nBlockStart < 0) {
        strError = "Invalid Proposal";
        return false;
    }

    if(nBlockEnd < nBlockStart) {
        strError = "Invalid nBlockEnd";
        return false;
    }

    if(nAmount < 1*COIN) {
        strError = "Invalid nAmount";
        return false;
    }

    if(address == CScript()) {
        strError = "Invalid Payment Address";
        return false;
    }

    if(fCheckCollateral){
        int nConf = 0;
        if(!IsEvolutionCollateralValid(nFeeTXHash, GetHash(), strError, nTime, nConf)){
            return false;
        }
    }

    /*
        TODO: There might be an issue with multisig in the coinbase on mainnet, we will add support for it in a future release.
    */
    if(address.IsPayToScriptHash()) {
        strError = "Multisig is not currently supported.";
        return false;
    }

    //if proposal doesn't gain traction within 2 weeks, remove it
    // nTime not being saved correctly
    // -- TODO: We should keep track of the last time the proposal was valid, if it's invalid for 2 weeks, erase it
    // if(nTime + (60*60*24*2) < GetAdjustedTime()) {
    //     if(GetYeas()-GetNays() < (gmineman.CountEnabled(MIN_EVOLUTION_PEER_PROTO_VERSION)/10)) {
    //         strError = "Not enough support";
    //         return false;
    //     }
    // }

    //can only pay out 10% of the possible coins (min value of coins)
    if(nAmount > evolution.GetTotalEvolution(nBlockStart)) {
        strError = "Payment more than max";
        return false;
    }

    CBlockIndex* pindexPrev = chainActive.Tip();
    if(pindexPrev == NULL) {strError = "Tip is NULL"; return true;}

    if(GetBlockEnd() < pindexPrev->nHeight - GetEvolutionPaymentCycleBlocks()/2 ) return false;


    return true;
}

bool CEvolutionProposal::AddOrUpdateVote(CEvolutionVote& vote, std::string& strError)
{
    LOCK(cs);

    uint256 hash = vote.vin.prevout.GetHash();

    if(mapVotes.count(hash)){
        if(mapVotes[hash].nTime > vote.nTime){
            strError = strprintf("new vote older than existing vote - %s\n", vote.GetHash().ToString());
            LogPrint("gmevolution", "CEvolutionProposal::AddOrUpdateVote - %s\n", strError);
            return false;
        }
        if(vote.nTime - mapVotes[hash].nTime < EVOLUTION_VOTE_UPDATE_MIN){
            strError = strprintf("time between votes is too soon - %s - %lli\n", vote.GetHash().ToString(), vote.nTime - mapVotes[hash].nTime);
            LogPrint("gmevolution", "CEvolutionProposal::AddOrUpdateVote - %s\n", strError);
            return false;
        }
    }

    if(vote.nTime > GetTime() + (60*60)){
        strError = strprintf("new vote is too far ahead of current time - %s - nTime %lli - Max Time %lli\n", vote.GetHash().ToString(), vote.nTime, GetTime() + (60*60));
        LogPrint("gmevolution", "CEvolutionProposal::AddOrUpdateVote - %s\n", strError);
        return false;
    }        

    mapVotes[hash] = vote;
    return true;
}

// If goldmine voted for a proposal, but is now invalid -- remove the vote
void CEvolutionProposal::CleanAndRemove(bool fSignatureCheck)
{
    std::map<uint256, CEvolutionVote>::iterator it = mapVotes.begin();

    while(it != mapVotes.end()) {
        (*it).second.fValid = (*it).second.SignatureValid(fSignatureCheck);
        ++it;
    }
}

double CEvolutionProposal::GetRatio()
{
    int yeas = 0;
    int nays = 0;

    std::map<uint256, CEvolutionVote>::iterator it = mapVotes.begin();

    while(it != mapVotes.end()) {
        if ((*it).second.nVote == VOTE_YES) yeas++;
        if ((*it).second.nVote == VOTE_NO) nays++;
        ++it;
    }

    if(yeas+nays == 0) return 0.0f;

    return ((double)(yeas) / (double)(yeas+nays));
}

int CEvolutionProposal::GetYeas()
{
    int ret = 0;

    std::map<uint256, CEvolutionVote>::iterator it = mapVotes.begin();
    while(it != mapVotes.end()){
        if ((*it).second.nVote == VOTE_YES && (*it).second.fValid) ret++;
        ++it;
    }

    return ret;
}

int CEvolutionProposal::GetNays()
{
    int ret = 0;

    std::map<uint256, CEvolutionVote>::iterator it = mapVotes.begin();
    while(it != mapVotes.end()){
        if ((*it).second.nVote == VOTE_NO && (*it).second.fValid) ret++;
        ++it;
    }

    return ret;
}

int CEvolutionProposal::GetAbstains()
{
    int ret = 0;

    std::map<uint256, CEvolutionVote>::iterator it = mapVotes.begin();
    while(it != mapVotes.end()){
        if ((*it).second.nVote == VOTE_ABSTAIN && (*it).second.fValid) ret++;
        ++it;
    }

    return ret;
}

int CEvolutionProposal::GetBlockStartCycle()
{
    //end block is half way through the next cycle (so the proposal will be removed much after the payment is sent)

    return nBlockStart - nBlockStart % GetEvolutionPaymentCycleBlocks();
}

int CEvolutionProposal::GetBlockCurrentCycle()
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if(pindexPrev == NULL) return -1;

    if(pindexPrev->nHeight >= GetBlockEndCycle()) return -1;

    return pindexPrev->nHeight - pindexPrev->nHeight % GetEvolutionPaymentCycleBlocks();
}

int CEvolutionProposal::GetBlockEndCycle()
{
    //end block is half way through the next cycle (so the proposal will be removed much after the payment is sent)

    return nBlockEnd - GetEvolutionPaymentCycleBlocks()/2;
}

int CEvolutionProposal::GetTotalPaymentCount()
{
    return (GetBlockEndCycle() - GetBlockStartCycle()) / GetEvolutionPaymentCycleBlocks();
}

int CEvolutionProposal::GetRemainingPaymentCount()
{
    // If this evolution starts in the future, this value will be wrong
    int nPayments = (GetBlockEndCycle() - GetBlockCurrentCycle()) / GetEvolutionPaymentCycleBlocks() - 1;
    // Take the lowest value
    return std::min(nPayments, GetTotalPaymentCount());
}

CEvolutionProposalBroadcast::CEvolutionProposalBroadcast(std::string strProposalNameIn, std::string strURLIn, int nPaymentCount, CScript addressIn, CAmount nAmountIn, int nBlockStartIn, uint256 nFeeTXHashIn)
{
    strProposalName = strProposalNameIn;
    strURL = strURLIn;

    nBlockStart = nBlockStartIn;

    int nCycleStart = nBlockStart - nBlockStart % GetEvolutionPaymentCycleBlocks();
    //calculate the end of the cycle for this vote, add half a cycle (vote will be deleted after that block)
    nBlockEnd = nCycleStart + GetEvolutionPaymentCycleBlocks() * nPaymentCount + GetEvolutionPaymentCycleBlocks()/2;

    address = addressIn;
    nAmount = nAmountIn;

    nFeeTXHash = nFeeTXHashIn;
}

void CEvolutionProposalBroadcast::Relay()
{
    CInv inv(MSG_EVOLUTION_PROPOSAL, GetHash());
    RelayInv(inv, MIN_EVOLUTION_PEER_PROTO_VERSION);
}

CEvolutionVote::CEvolutionVote()
{
    vin = CTxIn();
    nProposalHash = 0;
    nVote = VOTE_ABSTAIN;
    nTime = 0;
    fValid = true;
    fSynced = false;
}

CEvolutionVote::CEvolutionVote(CTxIn vinIn, uint256 nProposalHashIn, int nVoteIn)
{
    vin = vinIn;
    nProposalHash = nProposalHashIn;
    nVote = nVoteIn;
    nTime = GetAdjustedTime();
    fValid = true;
    fSynced = false;
}

void CEvolutionVote::Relay()
{
    CInv inv(MSG_EVOLUTION_VOTE, GetHash());
    RelayInv(inv, MIN_EVOLUTION_PEER_PROTO_VERSION);
}

bool CEvolutionVote::Sign(CKey& keyGoldmine, CPubKey& pubKeyGoldmine)
{
    // Choose coins to use
    CPubKey pubKeyCollateralAddress;
    CKey keyCollateralAddress;

    std::string errorMessage;
    std::string strMessage = vin.prevout.ToStringShort() + nProposalHash.ToString() + boost::lexical_cast<std::string>(nVote) + boost::lexical_cast<std::string>(nTime);

    if(!spySendSigner.SignMessage(strMessage, errorMessage, vchSig, keyGoldmine)) {
        LogPrintf("CEvolutionVote::Sign - Error upon calling SignMessage");
        return false;
    }

    if(!spySendSigner.VerifyMessage(pubKeyGoldmine, vchSig, strMessage, errorMessage)) {
        LogPrintf("CEvolutionVote::Sign - Error upon calling VerifyMessage");
        return false;
    }

    return true;
}

bool CEvolutionVote::SignatureValid(bool fSignatureCheck)
{
    std::string errorMessage;
    std::string strMessage = vin.prevout.ToStringShort() + nProposalHash.ToString() + boost::lexical_cast<std::string>(nVote) + boost::lexical_cast<std::string>(nTime);

    CGoldmine* pgm = gmineman.Find(vin);

    if(pgm == NULL)
    {
        LogPrint("gmevolution", "CEvolutionVote::SignatureValid() - Unknown Goldmine - %s\n", vin.ToString());
        return false;
    }

    if(!fSignatureCheck) return true;

    if(!spySendSigner.VerifyMessage(pgm->pubkey2, vchSig, strMessage, errorMessage)) {
        LogPrintf("CEvolutionVote::SignatureValid() - Verify message failed\n");
        return false;
    }

    return true;
}

CFinalizedEvolution::CFinalizedEvolution()
{
    strEvolutionName = "";
    nBlockStart = 0;
    vecEvolutionPayments.clear();
    mapVotes.clear();
    nFeeTXHash = 0;
    nTime = 0;
    fValid = true;
    fAutoChecked = false;
}

CFinalizedEvolution::CFinalizedEvolution(const CFinalizedEvolution& other)
{
    strEvolutionName = other.strEvolutionName;
    nBlockStart = other.nBlockStart;
    vecEvolutionPayments = other.vecEvolutionPayments;
    mapVotes = other.mapVotes;
    nFeeTXHash = other.nFeeTXHash;
    nTime = other.nTime;
    fValid = true;
    fAutoChecked = false;
}

bool CFinalizedEvolution::AddOrUpdateVote(CFinalizedEvolutionVote& vote, std::string& strError)
{
    LOCK(cs);

    uint256 hash = vote.vin.prevout.GetHash();
    if(mapVotes.count(hash)){
        if(mapVotes[hash].nTime > vote.nTime){
            strError = strprintf("new vote older than existing vote - %s\n", vote.GetHash().ToString());
            LogPrint("gmevolution", "CFinalizedEvolution::AddOrUpdateVote - %s\n", strError);
            return false;
        }
        if(vote.nTime - mapVotes[hash].nTime < EVOLUTION_VOTE_UPDATE_MIN){
            strError = strprintf("time between votes is too soon - %s - %lli\n", vote.GetHash().ToString(), vote.nTime - mapVotes[hash].nTime);
            LogPrint("gmevolution", "CFinalizedEvolution::AddOrUpdateVote - %s\n", strError);
            return false;
        }
    }

    if(vote.nTime > GetTime() + (60*60)){
        strError = strprintf("new vote is too far ahead of current time - %s - nTime %lli - Max Time %lli\n", vote.GetHash().ToString(), vote.nTime, GetTime() + (60*60));
        LogPrint("gmevolution", "CFinalizedEvolution::AddOrUpdateVote - %s\n", strError);
        return false;
    }

    mapVotes[hash] = vote;
    return true;
}

//evaluate if we should vote for this. Goldmine only
void CFinalizedEvolution::AutoCheck()
{
    LOCK(cs);

    CBlockIndex* pindexPrev = chainActive.Tip();
    if(!pindexPrev) return;

    LogPrintf("CFinalizedEvolution::AutoCheck - %lli - %d\n", pindexPrev->nHeight, fAutoChecked);

    if(!fGoldMine || fAutoChecked) return;

    //do this 1 in 4 blocks -- spread out the voting activity on mainnet
    // -- this function is only called every sixth block, so this is really 1 in 24 blocks
    if(Params().NetworkID() == CBaseChainParams::MAIN && rand() % 4 != 0) {
        LogPrintf("CFinalizedEvolution::AutoCheck - waiting\n");
        return;
    }

    fAutoChecked = true; //we only need to check this once


    if(strEvolutionMode == "auto") //only vote for exact matches
    {
        std::vector<CEvolutionProposal*> vEvolutionProposals = evolution.GetEvolution();


        for(unsigned int i = 0; i < vecEvolutionPayments.size(); i++){
            LogPrintf("CFinalizedEvolution::AutoCheck - nProp %d %s\n", i, vecEvolutionPayments[i].nProposalHash.ToString());
            LogPrintf("CFinalizedEvolution::AutoCheck - Payee %d %s\n", i, vecEvolutionPayments[i].payee.ToString());
            LogPrintf("CFinalizedEvolution::AutoCheck - nAmount %d %lli\n", i, vecEvolutionPayments[i].nAmount);
        }

        for(unsigned int i = 0; i < vEvolutionProposals.size(); i++){
            LogPrintf("CFinalizedEvolution::AutoCheck - nProp %d %s\n", i, vEvolutionProposals[i]->GetHash().ToString());
            LogPrintf("CFinalizedEvolution::AutoCheck - Payee %d %s\n", i, vEvolutionProposals[i]->GetPayee().ToString());
            LogPrintf("CFinalizedEvolution::AutoCheck - nAmount %d %lli\n", i, vEvolutionProposals[i]->GetAmount());
        }

        if(vEvolutionProposals.size() == 0) {
            LogPrintf("CFinalizedEvolution::AutoCheck - Can't get Evolution, aborting\n");
            return;
        }

        if(vEvolutionProposals.size() != vecEvolutionPayments.size()) {
            LogPrintf("CFinalizedEvolution::AutoCheck - Evolution length doesn't match\n");
            return;
        }


        for(unsigned int i = 0; i < vecEvolutionPayments.size(); i++){
            if(i > vEvolutionProposals.size() - 1) {
                LogPrintf("CFinalizedEvolution::AutoCheck - Vector size mismatch, aborting\n");
                return;
            }

            if(vecEvolutionPayments[i].nProposalHash != vEvolutionProposals[i]->GetHash()){
                LogPrintf("CFinalizedEvolution::AutoCheck - item #%d doesn't match %s %s\n", i, vecEvolutionPayments[i].nProposalHash.ToString(), vEvolutionProposals[i]->GetHash().ToString());
                return;
            }

            // if(vecEvolutionPayments[i].payee != vEvolutionProposals[i]->GetPayee()){ -- triggered with false positive 
            if(vecEvolutionPayments[i].payee.ToString() != vEvolutionProposals[i]->GetPayee().ToString()){
                LogPrintf("CFinalizedEvolution::AutoCheck - item #%d payee doesn't match %s %s\n", i, vecEvolutionPayments[i].payee.ToString(), vEvolutionProposals[i]->GetPayee().ToString());
                return;
            }

            if(vecEvolutionPayments[i].nAmount != vEvolutionProposals[i]->GetAmount()){
                LogPrintf("CFinalizedEvolution::AutoCheck - item #%d payee doesn't match %lli %lli\n", i, vecEvolutionPayments[i].nAmount, vEvolutionProposals[i]->GetAmount());
                return;
            }
        }

        LogPrintf("CFinalizedEvolution::AutoCheck - Finalized Evolution Matches! Submitting Vote.\n");
        SubmitVote();

    }
}
// If goldmine voted for a proposal, but is now invalid -- remove the vote
void CFinalizedEvolution::CleanAndRemove(bool fSignatureCheck)
{
    std::map<uint256, CFinalizedEvolutionVote>::iterator it = mapVotes.begin();

    while(it != mapVotes.end()) {
        (*it).second.fValid = (*it).second.SignatureValid(fSignatureCheck);
        ++it;
    }
}


CAmount CFinalizedEvolution::GetTotalPayout()
{
    CAmount ret = 0;

    for(unsigned int i = 0; i < vecEvolutionPayments.size(); i++){
        ret += vecEvolutionPayments[i].nAmount;
    }

    return ret;
}

std::string CFinalizedEvolution::GetProposals() 
{
    LOCK(cs);
    std::string ret = "";

    BOOST_FOREACH(CTxEvolutionPayment& evolutionPayment, vecEvolutionPayments){
        CEvolutionProposal* pevolutionProposal = evolution.FindProposal(evolutionPayment.nProposalHash);

        std::string token = evolutionPayment.nProposalHash.ToString();

        if(pevolutionProposal) token = pevolutionProposal->GetName();
        if(ret == "") {ret = token;}
        else {ret += "," + token;}
    }
    return ret;
}

std::string CFinalizedEvolution::GetStatus()
{
    std::string retBadHashes = "";
    std::string retBadPayeeOrAmount = "";

    for(int nBlockHeight = GetBlockStart(); nBlockHeight <= GetBlockEnd(); nBlockHeight++)
    {
        CTxEvolutionPayment evolutionPayment;
        if(!GetEvolutionPaymentByBlock(nBlockHeight, evolutionPayment)){
            LogPrintf("CFinalizedEvolution::GetStatus - Couldn't find evolution payment for block %lld\n", nBlockHeight);
            continue;
        }

        CEvolutionProposal* pevolutionProposal =  evolution.FindProposal(evolutionPayment.nProposalHash);
        if(!pevolutionProposal){
            if(retBadHashes == ""){
                retBadHashes = "Unknown proposal hash! Check this proposal before voting" + evolutionPayment.nProposalHash.ToString();
            } else {
                retBadHashes += "," + evolutionPayment.nProposalHash.ToString();
            }
        } else {
            if(pevolutionProposal->GetPayee() != evolutionPayment.payee || pevolutionProposal->GetAmount() != evolutionPayment.nAmount)
            {
                if(retBadPayeeOrAmount == ""){
                    retBadPayeeOrAmount = "Evolution payee/nAmount doesn't match our proposal! " + evolutionPayment.nProposalHash.ToString();
                } else {
                    retBadPayeeOrAmount += "," + evolutionPayment.nProposalHash.ToString();
                }
            }
        }
    }

    if(retBadHashes == "" && retBadPayeeOrAmount == "") return "OK";

    return retBadHashes + retBadPayeeOrAmount;
}

bool CFinalizedEvolution::IsValid(std::string& strError, bool fCheckCollateral)
{
    //must be the correct block for payment to happen (once a month)
    if(nBlockStart % GetEvolutionPaymentCycleBlocks() != 0) {strError = "Invalid BlockStart"; return false;}
    if(GetBlockEnd() - nBlockStart > 100) {strError = "Invalid BlockEnd"; return false;}
    if((int)vecEvolutionPayments.size() > 100) {strError = "Invalid evolution payments count (too many)"; return false;}
    if(strEvolutionName == "") {strError = "Invalid Evolution Name"; return false;}
    if(nBlockStart == 0) {strError = "Invalid BlockStart == 0"; return false;}
    if(nFeeTXHash == 0) {strError = "Invalid FeeTx == 0"; return false;}

    //can only pay out 10% of the possible coins (min value of coins)
    if(GetTotalPayout() > evolution.GetTotalEvolution(nBlockStart)) {strError = "Invalid Payout (more than max)"; return false;}

    std::string strError2 = "";
    if(fCheckCollateral){
        int nConf = 0;
        if(!IsEvolutionCollateralValid(nFeeTXHash, GetHash(), strError2, nTime, nConf)){
            {strError = "Invalid Collateral : " + strError2; return false;}
        }
    }

    //TODO: if N cycles old, invalid, invalid

    CBlockIndex* pindexPrev = chainActive.Tip();
    if(pindexPrev == NULL) return true;

    if(nBlockStart < pindexPrev->nHeight-100) {strError = "Older than current blockHeight"; return false;}

    return true;
}

bool CFinalizedEvolution::IsTransactionValid(const CTransaction& txNew, int nBlockHeight)
{
    int nCurrentEvolutionPayment = nBlockHeight - GetBlockStart();
    if(nCurrentEvolutionPayment < 0) {
        LogPrintf("CFinalizedEvolution::IsTransactionValid - Invalid block - height: %d start: %d\n", nBlockHeight, GetBlockStart());
        return false;
    }

    if(nCurrentEvolutionPayment > (int)vecEvolutionPayments.size() - 1) {
        LogPrintf("CFinalizedEvolution::IsTransactionValid - Invalid block - current evolution payment: %d of %d\n", nCurrentEvolutionPayment + 1, (int)vecEvolutionPayments.size());
        return false;
    }

    bool found = false;
    BOOST_FOREACH(CTxOut out, txNew.vout)
    {
        if(vecEvolutionPayments[nCurrentEvolutionPayment].payee == out.scriptPubKey && vecEvolutionPayments[nCurrentEvolutionPayment].nAmount == out.nValue)
            found = true;
    }
    if(!found) {
        CTxDestination address1;
        ExtractDestination(vecEvolutionPayments[nCurrentEvolutionPayment].payee, address1);
        CBitcoinAddress address2(address1);

        LogPrintf("CFinalizedEvolution::IsTransactionValid - Missing required payment - %s: %d\n", address2.ToString(), vecEvolutionPayments[nCurrentEvolutionPayment].nAmount);
    }
    
    return found;
}

void CFinalizedEvolution::SubmitVote()
{
    CPubKey pubKeyGoldmine;
    CKey keyGoldmine;
    std::string errorMessage;

    if(!spySendSigner.SetKey(strGoldMinePrivKey, errorMessage, keyGoldmine, pubKeyGoldmine)){
        LogPrintf("CFinalizedEvolution::SubmitVote - Error upon calling SetKey\n");
        return;
    }

    CFinalizedEvolutionVote vote(activeGoldmine.vin, GetHash());
    if(!vote.Sign(keyGoldmine, pubKeyGoldmine)){
        LogPrintf("CFinalizedEvolution::SubmitVote - Failure to sign.");
        return;
    }

    std::string strError = "";
    if(evolution.UpdateFinalizedEvolution(vote, NULL, strError)){
        LogPrintf("CFinalizedEvolution::SubmitVote  - new finalized evolution vote - %s\n", vote.GetHash().ToString());

        evolution.mapSeenFinalizedEvolutionVotes.insert(make_pair(vote.GetHash(), vote));
        vote.Relay();
    } else {
        LogPrintf("CFinalizedEvolution::SubmitVote : Error submitting vote - %s\n", strError);
    }
}

CFinalizedEvolutionBroadcast::CFinalizedEvolutionBroadcast()
{
    strEvolutionName = "";
    nBlockStart = 0;
    vecEvolutionPayments.clear();
    mapVotes.clear();
    vchSig.clear();
    nFeeTXHash = 0;
}

CFinalizedEvolutionBroadcast::CFinalizedEvolutionBroadcast(const CFinalizedEvolution& other)
{
    strEvolutionName = other.strEvolutionName;
    nBlockStart = other.nBlockStart;
    BOOST_FOREACH(CTxEvolutionPayment out, other.vecEvolutionPayments) vecEvolutionPayments.push_back(out);
    mapVotes = other.mapVotes;
    nFeeTXHash = other.nFeeTXHash;
}

CFinalizedEvolutionBroadcast::CFinalizedEvolutionBroadcast(std::string strEvolutionNameIn, int nBlockStartIn, std::vector<CTxEvolutionPayment> vecEvolutionPaymentsIn, uint256 nFeeTXHashIn)
{
    strEvolutionName = strEvolutionNameIn;
    nBlockStart = nBlockStartIn;
    BOOST_FOREACH(CTxEvolutionPayment out, vecEvolutionPaymentsIn) vecEvolutionPayments.push_back(out);
    mapVotes.clear();
    nFeeTXHash = nFeeTXHashIn;
}

void CFinalizedEvolutionBroadcast::Relay()
{
    CInv inv(MSG_EVOLUTION_FINALIZED, GetHash());
    RelayInv(inv, MIN_EVOLUTION_PEER_PROTO_VERSION);
}

CFinalizedEvolutionVote::CFinalizedEvolutionVote()
{
    vin = CTxIn();
    nEvolutionHash = 0;
    nTime = 0;
    vchSig.clear();
    fValid = true;
    fSynced = false;
}

CFinalizedEvolutionVote::CFinalizedEvolutionVote(CTxIn vinIn, uint256 nEvolutionHashIn)
{
    vin = vinIn;
    nEvolutionHash = nEvolutionHashIn;
    nTime = GetAdjustedTime();
    vchSig.clear();
    fValid = true;
    fSynced = false;
}

void CFinalizedEvolutionVote::Relay()
{
    CInv inv(MSG_EVOLUTION_FINALIZED_VOTE, GetHash());
    RelayInv(inv, MIN_EVOLUTION_PEER_PROTO_VERSION);
}

bool CFinalizedEvolutionVote::Sign(CKey& keyGoldmine, CPubKey& pubKeyGoldmine)
{
    // Choose coins to use
    CPubKey pubKeyCollateralAddress;
    CKey keyCollateralAddress;

    std::string errorMessage;
    std::string strMessage = vin.prevout.ToStringShort() + nEvolutionHash.ToString() + boost::lexical_cast<std::string>(nTime);

    if(!spySendSigner.SignMessage(strMessage, errorMessage, vchSig, keyGoldmine)) {
        LogPrintf("CFinalizedEvolutionVote::Sign - Error upon calling SignMessage");
        return false;
    }

    if(!spySendSigner.VerifyMessage(pubKeyGoldmine, vchSig, strMessage, errorMessage)) {
        LogPrintf("CFinalizedEvolutionVote::Sign - Error upon calling VerifyMessage");
        return false;
    }

    return true;
}

bool CFinalizedEvolutionVote::SignatureValid(bool fSignatureCheck)
{
    std::string errorMessage;

    std::string strMessage = vin.prevout.ToStringShort() + nEvolutionHash.ToString() + boost::lexical_cast<std::string>(nTime);

    CGoldmine* pgm = gmineman.Find(vin);

    if(pgm == NULL)
    {
        LogPrint("gmevolution", "CFinalizedEvolutionVote::SignatureValid() - Unknown Goldmine\n");
        return false;
    }

    if(!fSignatureCheck) return true;

    if(!spySendSigner.VerifyMessage(pgm->pubkey2, vchSig, strMessage, errorMessage)) {
        LogPrintf("CFinalizedEvolutionVote::SignatureValid() - Verify message failed\n");
        return false;
    }

    return true;
}

std::string CEvolutionManager::ToString() const
{
    std::ostringstream info;

    info << "Proposals: " << (int)mapProposals.size() <<
            ", Evolutions: " << (int)mapFinalizedEvolutions.size() <<
            ", Seen Evolutions: " << (int)mapSeenGoldmineEvolutionProposals.size() <<
            ", Seen Evolution Votes: " << (int)mapSeenGoldmineEvolutionVotes.size() <<
            ", Seen Final Evolutions: " << (int)mapSeenFinalizedEvolutions.size() <<
            ", Seen Final Evolution Votes: " << (int)mapSeenFinalizedEvolutionVotes.size();

    return info.str();
}


