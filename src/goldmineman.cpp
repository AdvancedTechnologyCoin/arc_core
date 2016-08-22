// Copyright (c) 2015-2016 The Arctic developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "goldmineman.h"
#include "goldmine.h"
#include "activegoldmine.h"
#include "spysend.h"
#include "util.h"
#include "addrman.h"
#include "spork.h"
#include <boost/lexical_cast.hpp>
#include <boost/filesystem.hpp>

/** Goldmine manager */
CGoldmineMan gmineman;

struct CompareLastPaid
{
    bool operator()(const pair<int64_t, CTxIn>& t1,
                    const pair<int64_t, CTxIn>& t2) const
    {
        return t1.first < t2.first;
    }
};

struct CompareScoreTxIn
{
    bool operator()(const pair<int64_t, CTxIn>& t1,
                    const pair<int64_t, CTxIn>& t2) const
    {
        return t1.first < t2.first;
    }
};

struct CompareScoreMN
{
    bool operator()(const pair<int64_t, CGoldmine>& t1,
                    const pair<int64_t, CGoldmine>& t2) const
    {
        return t1.first < t2.first;
    }
};

//
// CGoldmineDB
//

CGoldmineDB::CGoldmineDB()
{
    pathMN = GetDataDir() / "mncache.dat";
    strMagicMessage = "GoldmineCache";
}

bool CGoldmineDB::Write(const CGoldmineMan& gminemanToSave)
{
    int64_t nStart = GetTimeMillis();

    // serialize, checksum data up to that point, then append checksum
    CDataStream ssGoldmines(SER_DISK, CLIENT_VERSION);
    ssGoldmines << strMagicMessage; // goldmine cache file specific magic message
    ssGoldmines << FLATDATA(Params().MessageStart()); // network specific magic number
    ssGoldmines << gminemanToSave;
    uint256 hash = Hash(ssGoldmines.begin(), ssGoldmines.end());
    ssGoldmines << hash;

    // open output file, and associate with CAutoFile
    FILE *file = fopen(pathMN.string().c_str(), "wb");
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s : Failed to open file %s", __func__, pathMN.string());

    // Write and commit header, data
    try {
        fileout << ssGoldmines;
    }
    catch (std::exception &e) {
        return error("%s : Serialize or I/O error - %s", __func__, e.what());
    }
//    FileCommit(fileout);
    fileout.fclose();

    LogPrintf("Written info to mncache.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrintf("  %s\n", gminemanToSave.ToString());

    return true;
}

CGoldmineDB::ReadResult CGoldmineDB::Read(CGoldmineMan& gminemanToLoad, bool fDryRun)
{
    int64_t nStart = GetTimeMillis();
    // open input file, and associate with CAutoFile
    FILE *file = fopen(pathMN.string().c_str(), "rb");
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
    if (filein.IsNull())
    {
        error("%s : Failed to open file %s", __func__, pathMN.string());
        return FileError;
    }

    // use file size to size memory buffer
    int fileSize = boost::filesystem::file_size(pathMN);
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

    CDataStream ssGoldmines(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssGoldmines.begin(), ssGoldmines.end());
    if (hashIn != hashTmp)
    {
        error("%s : Checksum mismatch, data corrupted", __func__);
        return IncorrectHash;
    }

    unsigned char pchMsgTmp[4];
    std::string strMagicMessageTmp;
    try {
        // de-serialize file header (goldmine cache file specific magic message) and ..

        ssGoldmines >> strMagicMessageTmp;

        // ... verify the message matches predefined one
        if (strMagicMessage != strMagicMessageTmp)
        {
            error("%s : Invalid goldmine cache magic message", __func__);
            return IncorrectMagicMessage;
        }

        // de-serialize file header (network specific magic number) and ..
        ssGoldmines >> FLATDATA(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp)))
        {
            error("%s : Invalid network magic number", __func__);
            return IncorrectMagicNumber;
        }
        // de-serialize data into CGoldmineMan object
        ssGoldmines >> gminemanToLoad;
    }
    catch (std::exception &e) {
        gminemanToLoad.Clear();
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    LogPrintf("Loaded info from mncache.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrintf("  %s\n", gminemanToLoad.ToString());
    if(!fDryRun) {
        LogPrintf("Goldmine manager - cleaning....\n");
        gminemanToLoad.CheckAndRemove(true);
        LogPrintf("Goldmine manager - result:\n");
        LogPrintf("  %s\n", gminemanToLoad.ToString());
    }

    return Ok;
}

void DumpGoldmines()
{
    int64_t nStart = GetTimeMillis();

    CGoldmineDB mndb;
    CGoldmineMan tempMnodeman;

    LogPrintf("Verifying mncache.dat format...\n");
    CGoldmineDB::ReadResult readResult = mndb.Read(tempMnodeman, true);
    // there was an error and it was not an error on file opening => do not proceed
    if (readResult == CGoldmineDB::FileError)
        LogPrintf("Missing goldmine cache file - mncache.dat, will try to recreate\n");
    else if (readResult != CGoldmineDB::Ok)
    {
        LogPrintf("Error reading mncache.dat: ");
        if(readResult == CGoldmineDB::IncorrectFormat)
            LogPrintf("magic is ok but data has invalid format, will try to recreate\n");
        else
        {
            LogPrintf("file format is unknown or invalid, please fix it manually\n");
            return;
        }
    }
    LogPrintf("Writting info to mncache.dat...\n");
    mndb.Write(gmineman);

    LogPrintf("Goldmine dump finished  %dms\n", GetTimeMillis() - nStart);
}

CGoldmineMan::CGoldmineMan() {
    nDsqCount = 0;
}

bool CGoldmineMan::Add(CGoldmine &gm)
{
    LOCK(cs);

    if (!gm.IsEnabled())
        return false;

    CGoldmine *pmn = Find(gm.vin);
    if (pmn == NULL)
    {
        LogPrint("goldmine", "CGoldmineMan: Adding new Goldmine %s - %i now\n", gm.addr.ToString(), size() + 1);
        vGoldmines.push_back(gm);
        return true;
    }

    return false;
}

void CGoldmineMan::AskForMN(CNode* pnode, CTxIn &vin)
{
    std::map<COutPoint, int64_t>::iterator i = mWeAskedForGoldmineListEntry.find(vin.prevout);
    if (i != mWeAskedForGoldmineListEntry.end())
    {
        int64_t t = (*i).second;
        if (GetTime() < t) return; // we've asked recently
    }

    // ask for the mnb info once from the node that sent mnp

    LogPrintf("CGoldmineMan::AskForMN - Asking node for missing entry, vin: %s\n", vin.ToString());
    pnode->PushMessage("dseg", vin);
    int64_t askAgain = GetTime() + GOLDMINE_MIN_MNP_SECONDS;
    mWeAskedForGoldmineListEntry[vin.prevout] = askAgain;
}

void CGoldmineMan::Check()
{
    LOCK(cs);

    BOOST_FOREACH(CGoldmine& gm, vGoldmines) {
        gm.Check();
    }
}

void CGoldmineMan::CheckAndRemove(bool forceExpiredRemoval)
{
    Check();

    LOCK(cs);

    //remove inactive and outdated
    vector<CGoldmine>::iterator it = vGoldmines.begin();
    while(it != vGoldmines.end()){
        if((*it).activeState == CGoldmine::GOLDMINE_REMOVE ||
                (*it).activeState == CGoldmine::GOLDMINE_VIN_SPENT ||
                (forceExpiredRemoval && (*it).activeState == CGoldmine::GOLDMINE_EXPIRED) ||
                (*it).protocolVersion < goldminePayments.GetMinGoldminePaymentsProto()) {
            LogPrint("goldmine", "CGoldmineMan: Removing inactive Goldmine %s - %i now\n", (*it).addr.ToString(), size() - 1);

            //erase all of the broadcasts we've seen from this vin
            // -- if we missed a few pings and the node was removed, this will allow is to get it back without them 
            //    sending a brand new mnb
            map<uint256, CGoldmineBroadcast>::iterator it3 = mapSeenGoldmineBroadcast.begin();
            while(it3 != mapSeenGoldmineBroadcast.end()){
                if((*it3).second.vin == (*it).vin){
                    goldmineSync.mapSeenSyncMNB.erase((*it3).first);
                    mapSeenGoldmineBroadcast.erase(it3++);
                } else {
                    ++it3;
                }
            }

            // allow us to ask for this goldmine again if we see another ping
            map<COutPoint, int64_t>::iterator it2 = mWeAskedForGoldmineListEntry.begin();
            while(it2 != mWeAskedForGoldmineListEntry.end()){
                if((*it2).first == (*it).vin.prevout){
                    mWeAskedForGoldmineListEntry.erase(it2++);
                } else {
                    ++it2;
                }
            }

            it = vGoldmines.erase(it);
        } else {
            ++it;
        }
    }

    // check who's asked for the Goldmine list
    map<CNetAddr, int64_t>::iterator it1 = mAskedUsForGoldmineList.begin();
    while(it1 != mAskedUsForGoldmineList.end()){
        if((*it1).second < GetTime()) {
            mAskedUsForGoldmineList.erase(it1++);
        } else {
            ++it1;
        }
    }

    // check who we asked for the Goldmine list
    it1 = mWeAskedForGoldmineList.begin();
    while(it1 != mWeAskedForGoldmineList.end()){
        if((*it1).second < GetTime()){
            mWeAskedForGoldmineList.erase(it1++);
        } else {
            ++it1;
        }
    }

    // check which Goldmines we've asked for
    map<COutPoint, int64_t>::iterator it2 = mWeAskedForGoldmineListEntry.begin();
    while(it2 != mWeAskedForGoldmineListEntry.end()){
        if((*it2).second < GetTime()){
            mWeAskedForGoldmineListEntry.erase(it2++);
        } else {
            ++it2;
        }
    }

    // remove expired mapSeenGoldmineBroadcast
    map<uint256, CGoldmineBroadcast>::iterator it3 = mapSeenGoldmineBroadcast.begin();
    while(it3 != mapSeenGoldmineBroadcast.end()){
        if((*it3).second.lastPing.sigTime < GetTime() - GOLDMINE_REMOVAL_SECONDS*2){
            LogPrint("goldmine", "CGoldmineMan::CheckAndRemove - Removing expired Goldmine broadcast %s\n", (*it3).second.GetHash().ToString());
            goldmineSync.mapSeenSyncMNB.erase((*it3).second.GetHash());
            mapSeenGoldmineBroadcast.erase(it3++);
        } else {
            ++it3;
        }
    }

    // remove expired mapSeenGoldminePing
    map<uint256, CGoldminePing>::iterator it4 = mapSeenGoldminePing.begin();
    while(it4 != mapSeenGoldminePing.end()){
        if((*it4).second.sigTime < GetTime()-(GOLDMINE_REMOVAL_SECONDS*2)){
            mapSeenGoldminePing.erase(it4++);
        } else {
            ++it4;
        }
    }

}

void CGoldmineMan::Clear()
{
    LOCK(cs);
    vGoldmines.clear();
    mAskedUsForGoldmineList.clear();
    mWeAskedForGoldmineList.clear();
    mWeAskedForGoldmineListEntry.clear();
    mapSeenGoldmineBroadcast.clear();
    mapSeenGoldminePing.clear();
    nDsqCount = 0;
}

int CGoldmineMan::CountEnabled(int protocolVersion)
{
    int i = 0;
    protocolVersion = protocolVersion == -1 ? goldminePayments.GetMinGoldminePaymentsProto() : protocolVersion;

    BOOST_FOREACH(CGoldmine& gm, vGoldmines) {
        gm.Check();
        if(gm.protocolVersion < protocolVersion || !gm.IsEnabled()) continue;
        i++;
    }

    return i;
}

void CGoldmineMan::DsegUpdate(CNode* pnode)
{
    LOCK(cs);

    if(Params().NetworkID() == CBaseChainParams::MAIN) {
        if(!(pnode->addr.IsRFC1918() || pnode->addr.IsLocal())){
            std::map<CNetAddr, int64_t>::iterator it = mWeAskedForGoldmineList.find(pnode->addr);
            if (it != mWeAskedForGoldmineList.end())
            {
                if (GetTime() < (*it).second) {
                    LogPrintf("dseg - we already asked %s for the list; skipping...\n", pnode->addr.ToString());
                    return;
                }
            }
        }
    }
    
    pnode->PushMessage("dseg", CTxIn());
    int64_t askAgain = GetTime() + GOLDMINES_DSEG_SECONDS;
    mWeAskedForGoldmineList[pnode->addr] = askAgain;
}

CGoldmine *CGoldmineMan::Find(const CScript &payee)
{
    LOCK(cs);
    CScript payee2;

    BOOST_FOREACH(CGoldmine& gm, vGoldmines)
    {
        payee2 = GetScriptForDestination(gm.pubkey.GetID());
        if(payee2 == payee)
            return &gm;
    }
    return NULL;
}

CGoldmine *CGoldmineMan::Find(const CTxIn &vin)
{
    LOCK(cs);

    BOOST_FOREACH(CGoldmine& gm, vGoldmines)
    {
        if(gm.vin.prevout == vin.prevout)
            return &gm;
    }
    return NULL;
}


CGoldmine *CGoldmineMan::Find(const CPubKey &pubKeyGoldmine)
{
    LOCK(cs);

    BOOST_FOREACH(CGoldmine& gm, vGoldmines)
    {
        if(gm.pubkey2 == pubKeyGoldmine)
            return &gm;
    }
    return NULL;
}

// 
// Deterministically select the oldest/best goldmine to pay on the network
//
CGoldmine* CGoldmineMan::GetNextGoldmineInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount)
{
    LOCK(cs);

    CGoldmine *pBestGoldmine = NULL;
    std::vector<pair<int64_t, CTxIn> > vecGoldmineLastPaid;

    /*
        Make a vector with all of the last paid times
    */

    int nMnCount = CountEnabled();
    BOOST_FOREACH(CGoldmine &gm, vGoldmines)
    {
        gm.Check();
        if(!gm.IsEnabled()) continue;

        // //check protocol version
        if(gm.protocolVersion < goldminePayments.GetMinGoldminePaymentsProto()) continue;

        //it's in the list (up to 8 entries ahead of current block to allow propagation) -- so let's skip it
        if(goldminePayments.IsScheduled(gm, nBlockHeight)) continue;

        //it's too new, wait for a cycle
        if(fFilterSigTime && gm.sigTime + (nMnCount*2.6*60) > GetAdjustedTime()) continue;

        //make sure it has as many confirmations as there are goldmines
        if(gm.GetGoldmineInputAge() < nMnCount) continue;

        vecGoldmineLastPaid.push_back(make_pair(gm.SecondsSincePayment(), gm.vin));
    }

    nCount = (int)vecGoldmineLastPaid.size();

    //when the network is in the process of upgrading, don't penalize nodes that recently restarted
    if(fFilterSigTime && nCount < nMnCount/3) return GetNextGoldmineInQueueForPayment(nBlockHeight, false, nCount);

    // Sort them high to low
    sort(vecGoldmineLastPaid.rbegin(), vecGoldmineLastPaid.rend(), CompareLastPaid());

    // Look at 1/10 of the oldest nodes (by last payment), calculate their scores and pay the best one
    //  -- This doesn't look at who is being paid in the +8-10 blocks, allowing for double payments very rarely
    //  -- 1/100 payments should be a double payment on mainnet - (1/(3000/10))*2
    //  -- (chance per block * chances before IsScheduled will fire)
    int nTenthNetwork = CountEnabled()/10;
    int nCountTenth = 0; 
    uint256 nHigh = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CTxIn)& s, vecGoldmineLastPaid){
        CGoldmine* pmn = Find(s.second);
        if(!pmn) break;

        uint256 n = pmn->CalculateScore(1, nBlockHeight-100);
        if(n > nHigh){
            nHigh = n;
            pBestGoldmine = pmn;
        }
        nCountTenth++;
        if(nCountTenth >= nTenthNetwork) break;
    }
    return pBestGoldmine;
}

CGoldmine *CGoldmineMan::FindRandomNotInVec(std::vector<CTxIn> &vecToExclude, int protocolVersion)
{
    LOCK(cs);

    protocolVersion = protocolVersion == -1 ? goldminePayments.GetMinGoldminePaymentsProto() : protocolVersion;

    int nCountEnabled = CountEnabled(protocolVersion);
    LogPrintf("CGoldmineMan::FindRandomNotInVec - nCountEnabled - vecToExclude.size() %d\n", nCountEnabled - vecToExclude.size());
    if(nCountEnabled - vecToExclude.size() < 1) return NULL;

    int rand = GetRandInt(nCountEnabled - vecToExclude.size());
    LogPrintf("CGoldmineMan::FindRandomNotInVec - rand %d\n", rand);
    bool found;

    BOOST_FOREACH(CGoldmine &gm, vGoldmines) {
        if(gm.protocolVersion < protocolVersion || !gm.IsEnabled()) continue;
        found = false;
        BOOST_FOREACH(CTxIn &usedVin, vecToExclude) {
            if(gm.vin.prevout == usedVin.prevout) {
                found = true;
                break;
            }
        }
        if(found) continue;
        if(--rand < 1) {
            return &gm;
        }
    }

    return NULL;
}

CGoldmine* CGoldmineMan::GetCurrentGoldMine(int mod, int64_t nBlockHeight, int minProtocol)
{
    int64_t score = 0;
    CGoldmine* winner = NULL;

    // scan for winner
    BOOST_FOREACH(CGoldmine& gm, vGoldmines) {
        gm.Check();
        if(gm.protocolVersion < minProtocol || !gm.IsEnabled()) continue;

        // calculate the score for each Goldmine
        uint256 n = gm.CalculateScore(mod, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        // determine the winner
        if(n2 > score){
            score = n2;
            winner = &gm;
        }
    }

    return winner;
}

int CGoldmineMan::GetGoldmineRank(const CTxIn& vin, int64_t nBlockHeight, int minProtocol, bool fOnlyActive)
{
    std::vector<pair<int64_t, CTxIn> > vecGoldmineScores;

    //make sure we know about this block
    uint256 hash = 0;
    if(!GetBlockHash(hash, nBlockHeight)) return -1;

    // scan for winner
    BOOST_FOREACH(CGoldmine& gm, vGoldmines) {
        if(gm.protocolVersion < minProtocol) continue;
        if(fOnlyActive) {
            gm.Check();
            if(!gm.IsEnabled()) continue;
        }
        uint256 n = gm.CalculateScore(1, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        vecGoldmineScores.push_back(make_pair(n2, gm.vin));
    }

    sort(vecGoldmineScores.rbegin(), vecGoldmineScores.rend(), CompareScoreTxIn());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CTxIn)& s, vecGoldmineScores){
        rank++;
        if(s.second.prevout == vin.prevout) {
            return rank;
        }
    }

    return -1;
}

std::vector<pair<int, CGoldmine> > CGoldmineMan::GetGoldmineRanks(int64_t nBlockHeight, int minProtocol)
{
    std::vector<pair<int64_t, CGoldmine> > vecGoldmineScores;
    std::vector<pair<int, CGoldmine> > vecGoldmineRanks;

    //make sure we know about this block
    uint256 hash = 0;
    if(!GetBlockHash(hash, nBlockHeight)) return vecGoldmineRanks;

    // scan for winner
    BOOST_FOREACH(CGoldmine& gm, vGoldmines) {

        gm.Check();

        if(gm.protocolVersion < minProtocol) continue;
        if(!gm.IsEnabled()) {
            continue;
        }

        uint256 n = gm.CalculateScore(1, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        vecGoldmineScores.push_back(make_pair(n2, gm));
    }

    sort(vecGoldmineScores.rbegin(), vecGoldmineScores.rend(), CompareScoreMN());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CGoldmine)& s, vecGoldmineScores){
        rank++;
        vecGoldmineRanks.push_back(make_pair(rank, s.second));
    }

    return vecGoldmineRanks;
}

CGoldmine* CGoldmineMan::GetGoldmineByRank(int nRank, int64_t nBlockHeight, int minProtocol, bool fOnlyActive)
{
    std::vector<pair<int64_t, CTxIn> > vecGoldmineScores;

    // scan for winner
    BOOST_FOREACH(CGoldmine& gm, vGoldmines) {

        if(gm.protocolVersion < minProtocol) continue;
        if(fOnlyActive) {
            gm.Check();
            if(!gm.IsEnabled()) continue;
        }

        uint256 n = gm.CalculateScore(1, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        vecGoldmineScores.push_back(make_pair(n2, gm.vin));
    }

    sort(vecGoldmineScores.rbegin(), vecGoldmineScores.rend(), CompareScoreTxIn());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CTxIn)& s, vecGoldmineScores){
        rank++;
        if(rank == nRank) {
            return Find(s.second);
        }
    }

    return NULL;
}

void CGoldmineMan::ProcessGoldmineConnections()
{
    //we don't care about this for regtest
    if(Params().NetworkID() == CBaseChainParams::REGTEST) return;

    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes) {
        if(pnode->fSpySendMaster){
            if(spySendPool.pSubmittedToGoldmine != NULL && pnode->addr == spySendPool.pSubmittedToGoldmine->addr) continue;
            LogPrintf("Closing Goldmine connection %s \n", pnode->addr.ToString());
            pnode->fSpySendMaster = false;
            pnode->Release();
        }
    }
}

void CGoldmineMan::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{

    if(fLiteMode) return; //disable all Spysend/Goldmine related functionality
    if(!goldmineSync.IsBlockchainSynced()) return;

    LOCK(cs_process_message);

    if (strCommand == "mnb") { //Goldmine Broadcast
        CGoldmineBroadcast mnb;
        vRecv >> mnb;

        int nDoS = 0;
        if (CheckMnbAndUpdateGoldmineList(mnb, nDoS)) {
            // use announced Goldmine as a peer
             addrman.Add(CAddress(mnb.addr), pfrom->addr, 2*60*60);
        } else {
            if(nDoS > 0) Misbehaving(pfrom->GetId(), nDoS);
        }
    }

    else if (strCommand == "mnp") { //Goldmine Ping
        CGoldminePing mnp;
        vRecv >> mnp;

        LogPrint("goldmine", "mnp - Goldmine ping, vin: %s\n", mnp.vin.ToString());

        if(mapSeenGoldminePing.count(mnp.GetHash())) return; //seen
        mapSeenGoldminePing.insert(make_pair(mnp.GetHash(), mnp));

        int nDoS = 0;
        if(mnp.CheckAndUpdate(nDoS)) return;

        if(nDoS > 0) {
            // if anything significant failed, mark that node
            Misbehaving(pfrom->GetId(), nDoS);
        } else {
            // if nothing significant failed, search existing Goldmine list
            CGoldmine* pmn = Find(mnp.vin);
            // if it's known, don't ask for the mnb, just return
            if(pmn != NULL) return;
        }

        // something significant is broken or gm is unknown,
        // we might have to ask for a goldmine entry once
        AskForMN(pfrom, mnp.vin);

    } else if (strCommand == "dseg") { //Get Goldmine list or specific entry

        CTxIn vin;
        vRecv >> vin;

        if(vin == CTxIn()) { //only should ask for this once
            //local network
            bool isLocal = (pfrom->addr.IsRFC1918() || pfrom->addr.IsLocal());

            if(!isLocal && Params().NetworkID() == CBaseChainParams::MAIN) {
                std::map<CNetAddr, int64_t>::iterator i = mAskedUsForGoldmineList.find(pfrom->addr);
                if (i != mAskedUsForGoldmineList.end()){
                    int64_t t = (*i).second;
                    if (GetTime() < t) {
                        Misbehaving(pfrom->GetId(), 34);
                        LogPrintf("dseg - peer already asked me for the list\n");
                        return;
                    }
                }
                int64_t askAgain = GetTime() + GOLDMINES_DSEG_SECONDS;
                mAskedUsForGoldmineList[pfrom->addr] = askAgain;
            }
        } //else, asking for a specific node which is ok


        int nInvCount = 0;

        BOOST_FOREACH(CGoldmine& gm, vGoldmines) {
            if(gm.addr.IsRFC1918()) continue; //local network

            if(gm.IsEnabled()) {
                LogPrint("goldmine", "dseg - Sending Goldmine entry - %s \n", gm.addr.ToString());
                if(vin == CTxIn() || vin == gm.vin){
                    CGoldmineBroadcast mnb = CGoldmineBroadcast(gm);
                    uint256 hash = mnb.GetHash();
                    pfrom->PushInventory(CInv(MSG_GOLDMINE_ANNOUNCE, hash));
                    nInvCount++;

                    if(!mapSeenGoldmineBroadcast.count(hash)) mapSeenGoldmineBroadcast.insert(make_pair(hash, mnb));

                    if(vin == gm.vin) {
                        LogPrintf("dseg - Sent 1 Goldmine entries to %s\n", pfrom->addr.ToString());
                        return;
                    }
                }
            }
        }

        if(vin == CTxIn()) {
            pfrom->PushMessage("ssc", GOLDMINE_SYNC_LIST, nInvCount);
            LogPrintf("dseg - Sent %d Goldmine entries to %s\n", nInvCount, pfrom->addr.ToString());
        }
    }

}

void CGoldmineMan::Remove(CTxIn vin)
{
    LOCK(cs);

    vector<CGoldmine>::iterator it = vGoldmines.begin();
    while(it != vGoldmines.end()){
        if((*it).vin == vin){
            LogPrint("goldmine", "CGoldmineMan: Removing Goldmine %s - %i now\n", (*it).addr.ToString(), size() - 1);
            vGoldmines.erase(it);
            break;
        }
        ++it;
    }
}

std::string CGoldmineMan::ToString() const
{
    std::ostringstream info;

    info << "Goldmines: " << (int)vGoldmines.size() <<
            ", peers who asked us for Goldmine list: " << (int)mAskedUsForGoldmineList.size() <<
            ", peers we asked for Goldmine list: " << (int)mWeAskedForGoldmineList.size() <<
            ", entries in Goldmine list we asked for: " << (int)mWeAskedForGoldmineListEntry.size() <<
            ", nDsqCount: " << (int)nDsqCount;

    return info.str();
}

void CGoldmineMan::UpdateGoldmineList(CGoldmineBroadcast mnb) {
    mapSeenGoldminePing.insert(make_pair(mnb.lastPing.GetHash(), mnb.lastPing));
    mapSeenGoldmineBroadcast.insert(make_pair(mnb.GetHash(), mnb));
    goldmineSync.AddedGoldmineList(mnb.GetHash());

    LogPrintf("CGoldmineMan::UpdateGoldmineList() - addr: %s\n    vin: %s\n", mnb.addr.ToString(), mnb.vin.ToString());

    CGoldmine* pmn = Find(mnb.vin);
    if(pmn == NULL)
    {
        CGoldmine gm(mnb);
        Add(gm);
    } else {
        pmn->UpdateFromNewBroadcast(mnb);
    }
}

bool CGoldmineMan::CheckMnbAndUpdateGoldmineList(CGoldmineBroadcast mnb, int& nDos) {
    nDos = 0;
    LogPrint("goldmine", "CGoldmineMan::CheckMnbAndUpdateGoldmineList - Goldmine broadcast, vin: %s\n", mnb.vin.ToString());

    if(mapSeenGoldmineBroadcast.count(mnb.GetHash())) { //seen
        goldmineSync.AddedGoldmineList(mnb.GetHash());
        return true;
    }
    mapSeenGoldmineBroadcast.insert(make_pair(mnb.GetHash(), mnb));

    LogPrint("goldmine", "CGoldmineMan::CheckMnbAndUpdateGoldmineList - Goldmine broadcast, vin: %s new\n", mnb.vin.ToString());

    if(!mnb.CheckAndUpdate(nDos)){
        LogPrint("goldmine", "CGoldmineMan::CheckMnbAndUpdateGoldmineList - Goldmine broadcast, vin: %s CheckAndUpdate failed\n", mnb.vin.ToString());
        return false;
    }

    // make sure the vout that was signed is related to the transaction that spawned the Goldmine
    //  - this is expensive, so it's only done once per Goldmine
    if(!spySendSigner.IsVinAssociatedWithPubkey(mnb.vin, mnb.pubkey)) {
        LogPrintf("CGoldmineMan::CheckMnbAndUpdateGoldmineList - Got mismatched pubkey and vin\n");
        nDos = 33;
        return false;
    }

    // make sure it's still unspent
    //  - this is checked later by .check() in many places and by ThreadCheckSpySendPool()
    if(mnb.CheckInputsAndAdd(nDos)) {
        goldmineSync.AddedGoldmineList(mnb.GetHash());
    } else {
        LogPrintf("CGoldmineMan::CheckMnbAndUpdateGoldmineList - Rejected Goldmine entry %s\n", mnb.addr.ToString());
        return false;
    }

    return true;
}