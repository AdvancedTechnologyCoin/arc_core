// Copyright (c) 2015-2016 The Arctic developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "goldmine-payments.h"
#include "goldmine-evolution.h"
#include "goldmine-sync.h"
#include "goldmineman.h"
#include "spysend.h"
#include "util.h"
#include "sync.h"
#include "spork.h"
#include "addrman.h"
#include <boost/lexical_cast.hpp>
#include <boost/filesystem.hpp>

/** Object for who's going to get paid on which blocks */
CGoldminePayments goldminePayments;

CCriticalSection cs_vecPayments;
CCriticalSection cs_mapGoldmineBlocks;
CCriticalSection cs_mapGoldminePayeeVotes;

//
// CGoldminePaymentDB
//

CGoldminePaymentDB::CGoldminePaymentDB()
{
    pathDB = GetDataDir() / "gmpayments.dat";
    strMagicMessage = "GoldminePayments";
}

bool CGoldminePaymentDB::Write(const CGoldminePayments& objToSave)
{
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

    LogPrintf("Written info to gmpayments.dat  %dms\n", GetTimeMillis() - nStart);

    return true;
}

CGoldminePaymentDB::ReadResult CGoldminePaymentDB::Read(CGoldminePayments& objToLoad, bool fDryRun)
{

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
            error("%s : Invalid goldmine payement cache magic message", __func__);
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

        // de-serialize data into CGoldminePayments object
        ssObj >> objToLoad;
    }
    catch (std::exception &e) {
        objToLoad.Clear();
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    LogPrintf("Loaded info from gmpayments.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrintf("  %s\n", objToLoad.ToString());
    if(!fDryRun) {
        LogPrintf("Goldmine payments manager - cleaning....\n");
        objToLoad.CleanPaymentList();
        LogPrintf("Goldmine payments manager - result:\n");
        LogPrintf("  %s\n", objToLoad.ToString());
    }

    return Ok;
}

void DumpGoldminePayments()
{
    int64_t nStart = GetTimeMillis();

    CGoldminePaymentDB paymentdb;
    CGoldminePayments tempPayments;

    LogPrintf("Verifying gmpayments.dat format...\n");
    CGoldminePaymentDB::ReadResult readResult = paymentdb.Read(tempPayments, true);
    // there was an error and it was not an error on file opening => do not proceed
    if (readResult == CGoldminePaymentDB::FileError)
        LogPrintf("Missing evolutions file - gmpayments.dat, will try to recreate\n");
    else if (readResult != CGoldminePaymentDB::Ok)
    {
        LogPrintf("Error reading gmpayments.dat: ");
        if(readResult == CGoldminePaymentDB::IncorrectFormat)
            LogPrintf("magic is ok but data has invalid format, will try to recreate\n");
        else
        {
            LogPrintf("file format is unknown or invalid, please fix it manually\n");
            return;
        }
    }
    LogPrintf("Writting info to gmpayments.dat...\n");
    paymentdb.Write(goldminePayments);

    LogPrintf("Evolution dump finished  %dms\n", GetTimeMillis() - nStart);
}

bool IsBlockValueValid(const CBlock& block, int64_t nExpectedValue){
    CBlockIndex* pindexPrev = chainActive.Tip();
    if(pindexPrev == NULL) return true;

    int nHeight = 0;
    if(pindexPrev->GetBlockHash() == block.hashPrevBlock)
    {
        nHeight = pindexPrev->nHeight+1;
    } else { //out of order
        BlockMap::iterator mi = mapBlockIndex.find(block.hashPrevBlock);
        if (mi != mapBlockIndex.end() && (*mi).second)
            nHeight = (*mi).second->nHeight+1;
    }

    if(nHeight == 0){
        LogPrintf("IsBlockValueValid() : WARNING: Couldn't find previous block");
    }

    if(!goldmineSync.IsSynced()) { //there is no evolution data to use to check anything
        //super blocks will always be on these blocks, max 100 per evolutioning
        if(nHeight % GetEvolutionPaymentCycleBlocks() < 100){
            return true;
        } else {
            if(block.vtx[0].GetValueOut() > nExpectedValue) return false;
        }
    } else { // we're synced and have data so check the evolution schedule

        //are these blocks even enabled
        if(!IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS)){
            return block.vtx[0].GetValueOut() <= nExpectedValue;
        }
        
        if(evolution.IsEvolutionPaymentBlock(nHeight)){
            //the value of the block is evaluated in CheckBlock
            return true;
        } else {
            if(block.vtx[0].GetValueOut() > nExpectedValue) return false;
        }
    }

    return true;
}

bool IsBlockPayeeValid(const CTransaction& txNew, int nBlockHeight)
{
    if(!goldmineSync.IsSynced()) { //there is no evolution data to use to check anything -- find the longest chain
        LogPrint("gmpayments", "Client not synced, skipping block payee checks\n");
        return true;
    }

    //check if it's a evolution block
    if(IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS)){
        if(evolution.IsEvolutionPaymentBlock(nBlockHeight)){
            if(evolution.IsTransactionValid(txNew, nBlockHeight)){
                return true;
            } else {
                LogPrintf("Invalid evolution payment detected %s\n", txNew.ToString().c_str());
                if(IsSporkActive(SPORK_9_GOLDMINE_EVOLUTION_ENFORCEMENT)){
                    return false;
                } else {
                    LogPrintf("Evolution enforcement is disabled, accepting block\n");
                    return true;
                }
            }
        }
    }

    //check for goldmine payee
    if(goldminePayments.IsTransactionValid(txNew, nBlockHeight))
    {
        return true;
    } else {
        LogPrintf("Invalid gm payment detected %s\n", txNew.ToString().c_str());
        if(IsSporkActive(SPORK_8_GOLDMINE_PAYMENT_ENFORCEMENT)){
            return false;
        } else {
            LogPrintf("Goldmine payment enforcement is disabled, accepting block\n");
            return true;
        }
    }

    return false;
}


void FillBlockPayee(CMutableTransaction& txNew, int64_t nFees)
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if(!pindexPrev) return;

    if(IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS) && evolution.IsEvolutionPaymentBlock(pindexPrev->nHeight+1)){
        evolution.FillBlockPayee(txNew, nFees);
    } else {
        goldminePayments.FillBlockPayee(txNew, nFees);
    }
}

std::string GetRequiredPaymentsString(int nBlockHeight)
{
    if(IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS) && evolution.IsEvolutionPaymentBlock(nBlockHeight)){
        return evolution.GetRequiredPaymentsString(nBlockHeight);
    } else {
        return goldminePayments.GetRequiredPaymentsString(nBlockHeight);
    }
}

void CGoldminePayments::FillBlockPayee(CMutableTransaction& txNew, int64_t nFees)
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if(!pindexPrev) return;

    bool hasPayment = true;
    CScript payee;

    //spork
    if(!goldminePayments.GetBlockPayee(pindexPrev->nHeight+1, payee)){
        //no goldmine detected
        CGoldmine* winningNode = gmineman.GetCurrentGoldMine(1);
        if(winningNode){
            payee = GetScriptForDestination(winningNode->pubkey.GetID());
        } else {
            LogPrintf("CreateNewBlock: Failed to detect goldmine to pay\n");
            hasPayment = false;
        }
    }

    CAmount blockValue = GetBlockValue(pindexPrev->nBits, pindexPrev->nHeight, nFees);
    CAmount goldminePayment = GetGoldminePayment(pindexPrev->nHeight+1, blockValue);

    txNew.vout[0].nValue = blockValue;

    if(hasPayment && pindexPrev->nHeight+1>1){
        txNew.vout.resize(2);

        txNew.vout[1].scriptPubKey = payee;
        txNew.vout[1].nValue = goldminePayment;

        txNew.vout[0].nValue -= goldminePayment;

        CTxDestination address1;
        ExtractDestination(payee, address1);
        CBitcoinAddress address2(address1);

        LogPrintf("Goldmine payment to %s\n", address2.ToString().c_str());
    }
}

int CGoldminePayments::GetMinGoldminePaymentsProto() {
    return IsSporkActive(SPORK_10_GOLDMINE_PAY_UPDATED_NODES)
            ? MIN_GOLDMINE_PAYMENT_PROTO_VERSION_2
            : MIN_GOLDMINE_PAYMENT_PROTO_VERSION_1;
}

void CGoldminePayments::ProcessMessageGoldminePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if(!goldmineSync.IsBlockchainSynced()) return;

    if(fLiteMode) return; //disable all Spysend/Goldmine related functionality


    if (strCommand == "gmget") { //Goldmine Payments Request Sync
        if(fLiteMode) return; //disable all Spysend/Goldmine related functionality

        int nCountNeeded;
        vRecv >> nCountNeeded;

        if(Params().NetworkID() == CBaseChainParams::MAIN){
            if(pfrom->HasFulfilledRequest("gmget")) {
                LogPrintf("gmget - peer already asked me for the list\n");
                Misbehaving(pfrom->GetId(), 20);
                return;
            }
        }

        pfrom->FulfilledRequest("gmget");
        goldminePayments.Sync(pfrom, nCountNeeded);
        LogPrintf("gmget - Sent Goldmine winners to %s\n", pfrom->addr.ToString().c_str());
    }
    else if (strCommand == "mnw") { //Goldmine Payments Declare Winner
        //this is required in litemodef
        CGoldminePaymentWinner winner;
        vRecv >> winner;

        if(pfrom->nVersion < MIN_GMW_PEER_PROTO_VERSION) return;

        int nHeight;
		{
			TRY_LOCK(cs_main, locked);
			if(!locked || chainActive.Tip() == NULL) return;
			nHeight = chainActive.Tip()->nHeight;
		}

        if(goldminePayments.mapGoldminePayeeVotes.count(winner.GetHash())){
            LogPrint("gmpayments", "gmw - Already seen - %s bestHeight %d\n", winner.GetHash().ToString().c_str(), nHeight);
            goldmineSync.AddedGoldmineWinner(winner.GetHash());
            return;
        }

        int nFirstBlock = nHeight - (gmineman.CountEnabled()*1.25);
         if(winner.nBlockHeight < nFirstBlock || winner.nBlockHeight > nHeight+20){
            LogPrint("gmpayments", "gmw - winner out of range - FirstBlock %d Height %d bestHeight %d\n", nFirstBlock, winner.nBlockHeight, nHeight);
            return;
        }

        std::string strError = "";
        if(!winner.IsValid(pfrom, strError)){
            if(strError != "") LogPrintf("gmw - invalid message - %s\n", strError);
            return;
        }

        if(!goldminePayments.CanVote(winner.vinGoldmine.prevout, winner.nBlockHeight)){
            LogPrintf("gmw - goldmine already voted - %s\n", winner.vinGoldmine.prevout.ToStringShort());
            return;
        }

        if(!winner.SignatureValid()){
            LogPrintf("gmw - invalid signature\n");
            if(goldmineSync.IsSynced()) Misbehaving(pfrom->GetId(), 20);
            // it could just be a non-synced goldmine
            gmineman.AskForGM(pfrom, winner.vinGoldmine);
            return;
        }

        CTxDestination address1;
        ExtractDestination(winner.payee, address1);
        CBitcoinAddress address2(address1);

        LogPrint("gmpayments", "gmw - winning vote - Addr %s Height %d bestHeight %d - %s\n", address2.ToString().c_str(), winner.nBlockHeight, nHeight, winner.vinGoldmine.prevout.ToStringShort());

        if(goldminePayments.AddWinningGoldmine(winner)){
            winner.Relay();
            goldmineSync.AddedGoldmineWinner(winner.GetHash());
        }
    }
}

bool CGoldminePaymentWinner::Sign(CKey& keyGoldmine, CPubKey& pubKeyGoldmine)
{
    std::string errorMessage;
    std::string strGoldMineSignMessage;

    std::string strMessage =  vinGoldmine.prevout.ToStringShort() +
                boost::lexical_cast<std::string>(nBlockHeight) +
                payee.ToString();

    if(!spySendSigner.SignMessage(strMessage, errorMessage, vchSig, keyGoldmine)) {
        LogPrintf("CGoldminePing::Sign() - Error: %s\n", errorMessage.c_str());
        return false;
    }

    if(!spySendSigner.VerifyMessage(pubKeyGoldmine, vchSig, strMessage, errorMessage)) {
        LogPrintf("CGoldminePing::Sign() - Error: %s\n", errorMessage.c_str());
        return false;
    }

    return true;
}

bool CGoldminePayments::GetBlockPayee(int nBlockHeight, CScript& payee)
{
    if(mapGoldmineBlocks.count(nBlockHeight)){
        return mapGoldmineBlocks[nBlockHeight].GetPayee(payee);
    }

    return false;
}

// Is this goldmine scheduled to get paid soon? 
// -- Only look ahead up to 8 blocks to allow for propagation of the latest 2 winners
bool CGoldminePayments::IsScheduled(CGoldmine& gm, int nNotBlockHeight)
{
    LOCK(cs_mapGoldmineBlocks);

    int nHeight;
	{
		TRY_LOCK(cs_main, locked);
		if(!locked || chainActive.Tip() == NULL) return false;
		nHeight = chainActive.Tip()->nHeight;
	}
    
    CScript gmpayee;
    gmpayee = GetScriptForDestination(gm.pubkey.GetID());

    CScript payee;
    for(int64_t h = nHeight; h <= nHeight+8; h++){
        if(h == nNotBlockHeight) continue;
        if(mapGoldmineBlocks.count(h)){
            if(mapGoldmineBlocks[h].GetPayee(payee)){
                if(gmpayee == payee) {
                    return true;
                }
            }
        }
    }

    return false;
}

bool CGoldminePayments::AddWinningGoldmine(CGoldminePaymentWinner& winnerIn)
{
    uint256 blockHash = 0;
    if(!GetBlockHash(blockHash, winnerIn.nBlockHeight-100)) {
        return false;
    }

    {
        LOCK2(cs_mapGoldminePayeeVotes, cs_mapGoldmineBlocks);
    
        if(mapGoldminePayeeVotes.count(winnerIn.GetHash())){
           return false;
        }

        mapGoldminePayeeVotes[winnerIn.GetHash()] = winnerIn;

        if(!mapGoldmineBlocks.count(winnerIn.nBlockHeight)){
           CGoldmineBlockPayees blockPayees(winnerIn.nBlockHeight);
           mapGoldmineBlocks[winnerIn.nBlockHeight] = blockPayees;
        }
    }

    int n = 1;
    if(IsReferenceNode(winnerIn.vinGoldmine)) n = 100;
    mapGoldmineBlocks[winnerIn.nBlockHeight].AddPayee(winnerIn.payee, n);

    return true;
}

bool CGoldmineBlockPayees::IsTransactionValid(const CTransaction& txNew)
{
    LOCK(cs_vecPayments);

    int nMaxSignatures = 0;
    std::string strPayeesPossible = "";

    CAmount goldminePayment = GetGoldminePayment(nBlockHeight, txNew.GetValueOut());

    //require at least 6 signatures

    BOOST_FOREACH(CGoldminePayee& payee, vecPayments)
        if(payee.nVotes >= nMaxSignatures && payee.nVotes >= GMPAYMENTS_SIGNATURES_REQUIRED)
            nMaxSignatures = payee.nVotes;

    // if we don't have at least 6 signatures on a payee, approve whichever is the longest chain
    if(nMaxSignatures < GMPAYMENTS_SIGNATURES_REQUIRED) return true;

    BOOST_FOREACH(CGoldminePayee& payee, vecPayments)
    {

		if (payee.nVotes >= GMPAYMENTS_SIGNATURES_REQUIRED) {
	
			BOOST_FOREACH(CTxOut out, txNew.vout){
				if(payee.scriptPubKey == out.scriptPubKey && goldminePayment == out.nValue){
					LogPrint("gmpayments", "CGoldmineBlockPayees::IsTransactionValid -- Found required payment\n");
					return true;
				}
			}

            CTxDestination address1;
            ExtractDestination(payee.scriptPubKey, address1);
            CBitcoinAddress address2(address1);

            if(strPayeesPossible == ""){
                strPayeesPossible = address2.ToString();
            } else {
                strPayeesPossible += "," + address2.ToString();
            }
        }
    }

	LogPrintf("CGoldmineBlockPayees::IsTransactionValid -- ERROR: Missing required payment, possible payees: '%s', amount: %f ARC\n", strPayeesPossible, (float)goldminePayment/COIN);
    return false;
}

std::string CGoldmineBlockPayees::GetRequiredPaymentsString()
{
    LOCK(cs_vecPayments);

    std::string ret = "Unknown";

    BOOST_FOREACH(CGoldminePayee& payee, vecPayments)
    {
        CTxDestination address1;
        ExtractDestination(payee.scriptPubKey, address1);
        CBitcoinAddress address2(address1);

        if(ret != "Unknown"){
            ret += ", " + address2.ToString() + ":" + boost::lexical_cast<std::string>(payee.nVotes);
        } else {
            ret = address2.ToString() + ":" + boost::lexical_cast<std::string>(payee.nVotes);
        }
    }

    return ret;
}

std::string CGoldminePayments::GetRequiredPaymentsString(int nBlockHeight)
{
    LOCK(cs_mapGoldmineBlocks);

    if(mapGoldmineBlocks.count(nBlockHeight)){
        return mapGoldmineBlocks[nBlockHeight].GetRequiredPaymentsString();
    }

    return "Unknown";
}

bool CGoldminePayments::IsTransactionValid(const CTransaction& txNew, int nBlockHeight)
{
    LOCK(cs_mapGoldmineBlocks);

    if(mapGoldmineBlocks.count(nBlockHeight)){
        return mapGoldmineBlocks[nBlockHeight].IsTransactionValid(txNew);
    }

    return true;
}

void CGoldminePayments::CleanPaymentList()
{
    LOCK2(cs_mapGoldminePayeeVotes, cs_mapGoldmineBlocks);

    int nHeight;
	{
		TRY_LOCK(cs_main, locked);
		if(!locked || chainActive.Tip() == NULL) return;
		nHeight = chainActive.Tip()->nHeight;
	}

    //keep up to five cycles for historical sake
    int nLimit = std::max(int(gmineman.size()*1.25), 1000);

    std::map<uint256, CGoldminePaymentWinner>::iterator it = mapGoldminePayeeVotes.begin();
    while(it != mapGoldminePayeeVotes.end()) {
        CGoldminePaymentWinner winner = (*it).second;

        if(nHeight - winner.nBlockHeight > nLimit){
            LogPrint("gmpayments", "CGoldminePayments::CleanPaymentList - Removing old Goldmine payment - block %d\n", winner.nBlockHeight);
            goldmineSync.mapSeenSyncGMW.erase((*it).first);
            mapGoldminePayeeVotes.erase(it++);
            mapGoldmineBlocks.erase(winner.nBlockHeight);
        } else {
            ++it;
        }
    }
}

bool IsReferenceNode(CTxIn& vin)
{
    //reference node - hybrid mode
    if(vin.prevout.ToStringShort() == "099c01bea63abd1692f60806bb646fa1d288e2d049281225f17e499024084e28-0") return true; // mainnet
    if(vin.prevout.ToStringShort() == "fbc16ae5229d6d99181802fd76a4feee5e7640164dcebc7f8feb04a7bea026f8-0") return true; // testnet
    if(vin.prevout.ToStringShort() == "e466f5d8beb4c2d22a314310dc58e0ea89505c95409754d0d68fb874952608cc-1") return true; // regtest

    return false;
}

bool CGoldminePaymentWinner::IsValid(CNode* pnode, std::string& strError)
{
    if(IsReferenceNode(vinGoldmine)) return true;

    CGoldmine* pgm = gmineman.Find(vinGoldmine);

    if(!pgm)
    {
        strError = strprintf("Unknown Goldmine %s", vinGoldmine.prevout.ToStringShort());
        LogPrintf ("CGoldminePaymentWinner::IsValid - %s\n", strError);
        gmineman.AskForGM(pnode, vinGoldmine);
        return false;
    }

    if(pgm->protocolVersion < MIN_GMW_PEER_PROTO_VERSION)
    {
        strError = strprintf("Goldmine protocol too old %d - req %d", pgm->protocolVersion, MIN_GMW_PEER_PROTO_VERSION);
        LogPrintf ("CGoldminePaymentWinner::IsValid - %s\n", strError);
        return false;
    }

    int n = gmineman.GetGoldmineRank(vinGoldmine, nBlockHeight-100, MIN_GMW_PEER_PROTO_VERSION);

    if(n > GMPAYMENTS_SIGNATURES_TOTAL)
    {    
        //It's common to have goldmines mistakenly think they are in the top 10
        // We don't want to print all of these messages, or punish them unless they're way off
        if(n > GMPAYMENTS_SIGNATURES_TOTAL*2)
        {
            strError = strprintf("Goldmine not in the top %d (%d)", GMPAYMENTS_SIGNATURES_TOTAL, n);
            LogPrintf("CGoldminePaymentWinner::IsValid - %s\n", strError);
            if(goldmineSync.IsSynced()) Misbehaving(pnode->GetId(), 20);
        }
        return false;
    }

    return true;
}

bool CGoldminePayments::ProcessBlock(int nBlockHeight)
{
    if(!fGoldMine) return false;

    //reference node - hybrid mode

    if(!IsReferenceNode(activeGoldmine.vin)){
        int n = gmineman.GetGoldmineRank(activeGoldmine.vin, nBlockHeight-100, MIN_GMW_PEER_PROTO_VERSION);

        if(n == -1)
        {
            LogPrint("gmpayments", "CGoldminePayments::ProcessBlock - Unknown Goldmine\n");
            return false;
        }

        if(n > GMPAYMENTS_SIGNATURES_TOTAL)
        {
            LogPrint("gmpayments", "CGoldminePayments::ProcessBlock - Goldmine not in the top %d (%d)\n", GMPAYMENTS_SIGNATURES_TOTAL, n);
            return false;
        }
    }

    if(nBlockHeight <= nLastBlockHeight) return false;

    CGoldminePaymentWinner newWinner(activeGoldmine.vin);

    if(evolution.IsEvolutionPaymentBlock(nBlockHeight)){
        //is evolution payment block -- handled by the evolutioning software
    } else {
        LogPrintf("CGoldminePayments::ProcessBlock() Start nHeight %d - vin %s. \n", nBlockHeight, activeGoldmine.vin.ToString().c_str());

        // pay to the oldest GM that still had no payment but its input is old enough and it was active long enough
        int nCount = 0;
        CGoldmine *pgm = gmineman.GetNextGoldmineInQueueForPayment(nBlockHeight, true, nCount);
        
        if(pgm != NULL)
        {
            LogPrintf("CGoldminePayments::ProcessBlock() Found by FindOldestNotInVec \n");

            newWinner.nBlockHeight = nBlockHeight;

            CScript payee = GetScriptForDestination(pgm->pubkey.GetID());
            newWinner.AddPayee(payee);

            CTxDestination address1;
            ExtractDestination(payee, address1);
            CBitcoinAddress address2(address1);

            LogPrintf("CGoldminePayments::ProcessBlock() Winner payee %s nHeight %d. \n", address2.ToString().c_str(), newWinner.nBlockHeight);
        } else {
            LogPrintf("CGoldminePayments::ProcessBlock() Failed to find goldmine to pay\n");
        }

    }

    std::string errorMessage;
    CPubKey pubKeyGoldmine;
    CKey keyGoldmine;

    if(!spySendSigner.SetKey(strGoldMinePrivKey, errorMessage, keyGoldmine, pubKeyGoldmine))
    {
        LogPrintf("CGoldminePayments::ProcessBlock() - Error upon calling SetKey: %s\n", errorMessage.c_str());
        return false;
    }

    LogPrintf("CGoldminePayments::ProcessBlock() - Signing Winner\n");
    if(newWinner.Sign(keyGoldmine, pubKeyGoldmine))
    {
        LogPrintf("CGoldminePayments::ProcessBlock() - AddWinningGoldmine\n");

        if(AddWinningGoldmine(newWinner))
        {
            newWinner.Relay();
            nLastBlockHeight = nBlockHeight;
            return true;
        }
    }

    return false;
}

void CGoldminePaymentWinner::Relay()
{
    CInv inv(MSG_GOLDMINE_WINNER, GetHash());
    RelayInv(inv);
}

bool CGoldminePaymentWinner::SignatureValid()
{

    CGoldmine* pgm = gmineman.Find(vinGoldmine);

    if(pgm != NULL)
    {
        std::string strMessage =  vinGoldmine.prevout.ToStringShort() +
                    boost::lexical_cast<std::string>(nBlockHeight) +
                    payee.ToString();

        std::string errorMessage = "";
        if(!spySendSigner.VerifyMessage(pgm->pubkey2, vchSig, strMessage, errorMessage)){
            return error("CGoldminePaymentWinner::SignatureValid() - Got bad Goldmine address signature %s \n", vinGoldmine.ToString().c_str());
        }

        return true;
    }

    return false;
}

void CGoldminePayments::Sync(CNode* node, int nCountNeeded)
{
    LOCK(cs_mapGoldminePayeeVotes);

    int nHeight;
	{
		TRY_LOCK(cs_main, locked);
		if(!locked || chainActive.Tip() == NULL) return;
		nHeight = chainActive.Tip()->nHeight;
	}

    int nCount = (gmineman.CountEnabled()*1.25);
    if(nCountNeeded > nCount) nCountNeeded = nCount;

    int nInvCount = 0;
    std::map<uint256, CGoldminePaymentWinner>::iterator it = mapGoldminePayeeVotes.begin();
    while(it != mapGoldminePayeeVotes.end()) {
        CGoldminePaymentWinner winner = (*it).second;
        if(winner.nBlockHeight >= nHeight-nCountNeeded && winner.nBlockHeight <= nHeight + 20) {
            node->PushInventory(CInv(MSG_GOLDMINE_WINNER, winner.GetHash()));
            nInvCount++;
        }
        ++it;
    }
    node->PushMessage("ssc", GOLDMINE_SYNC_GMW, nInvCount);
}

std::string CGoldminePayments::ToString() const
{
    std::ostringstream info;

    info << "Votes: " << (int)mapGoldminePayeeVotes.size() <<
            ", Blocks: " << (int)mapGoldmineBlocks.size();

    return info.str();
}



int CGoldminePayments::GetOldestBlock()
{
    LOCK(cs_mapGoldmineBlocks);

    int nOldestBlock = std::numeric_limits<int>::max();

    std::map<int, CGoldmineBlockPayees>::iterator it = mapGoldmineBlocks.begin();
    while(it != mapGoldmineBlocks.end()) {
        if((*it).first < nOldestBlock) {
            nOldestBlock = (*it).first;
        }
        it++;
    }

    return nOldestBlock;
}



int CGoldminePayments::GetNewestBlock()
{
    LOCK(cs_mapGoldmineBlocks);

    int nNewestBlock = 0;

    std::map<int, CGoldmineBlockPayees>::iterator it = mapGoldmineBlocks.begin();
    while(it != mapGoldmineBlocks.end()) {
        if((*it).first > nNewestBlock) {
            nNewestBlock = (*it).first;
        }
        it++;
    }

    return nNewestBlock;
}
