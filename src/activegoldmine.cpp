
#include "addrman.h"
#include "protocol.h"
#include "activegoldmine.h"
#include "goldmineman.h"
#include "goldmine.h"
#include "goldmineconfig.h"
#include "spork.h"

//
// Bootup the Goldmine, look for a 1000ARC input and register on the network
//
void CActiveGoldmine::ManageStatus()
{    
    std::string errorMessage;

    if(!fGoldMine) return;

    if (fDebug) LogPrintf("CActiveGoldmine::ManageStatus() - Begin\n");

    //need correct blocks to send ping
    if(Params().NetworkID() != CBaseChainParams::REGTEST && !goldmineSync.IsBlockchainSynced()) {
        status = ACTIVE_GOLDMINE_SYNC_IN_PROCESS;
        LogPrintf("CActiveGoldmine::ManageStatus() - %s\n", GetStatus());
        return;
    }

    if(status == ACTIVE_GOLDMINE_SYNC_IN_PROCESS) status = ACTIVE_GOLDMINE_INITIAL;

    if(status == ACTIVE_GOLDMINE_INITIAL) {
        CGoldmine *pmn;
        pmn = gmineman.Find(pubKeyGoldmine);
        if(pmn != NULL) {
            pmn->Check();
            if(pmn->IsEnabled() && pmn->protocolVersion == PROTOCOL_VERSION) EnableHotColdGoldMine(pmn->vin, pmn->addr);
        }
    }

    if(status != ACTIVE_GOLDMINE_STARTED) {

        // Set defaults
        status = ACTIVE_GOLDMINE_NOT_CAPABLE;
        notCapableReason = "";

        if(pwalletMain->IsLocked()){
            notCapableReason = "Wallet is locked.";
            LogPrintf("CActiveGoldmine::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        if(pwalletMain->GetBalance() == 0){
            notCapableReason = "Hot node, waiting for remote activation.";
            LogPrintf("CActiveGoldmine::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        if(strGoldMineAddr.empty()) {
            if(!GetLocal(service)) {
                notCapableReason = "Can't detect external address. Please use the goldmineaddr configuration option.";
                LogPrintf("CActiveGoldmine::ManageStatus() - not capable: %s\n", notCapableReason);
                return;
            }
        } else {
            service = CService(strGoldMineAddr);
        }

        if(Params().NetworkID() == CBaseChainParams::MAIN) {
            if(service.GetPort() != 7209) {
                notCapableReason = strprintf("Invalid port: %u - only 7209 is supported on mainnet.", service.GetPort());
                LogPrintf("CActiveGoldmine::ManageStatus() - not capable: %s\n", notCapableReason);
                return;
            }
        } else if(service.GetPort() == 7209) {
            notCapableReason = strprintf("Invalid port: %u - 7209 is only supported on mainnet.", service.GetPort());
            LogPrintf("CActiveGoldmine::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        LogPrintf("CActiveGoldmine::ManageStatus() - Checking inbound connection to '%s'\n", service.ToString());

        CNode *pnode = ConnectNode((CAddress)service, NULL, false);
        if(!pnode){
            notCapableReason = "Could not connect to " + service.ToString();
            LogPrintf("CActiveGoldmine::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }
        pnode->Release();

        // Choose coins to use
        CPubKey pubKeyCollateralAddress;
        CKey keyCollateralAddress;

        if(GetGoldMineVin(vin, pubKeyCollateralAddress, keyCollateralAddress)) {

            if(GetInputAge(vin) < GOLDMINE_MIN_CONFIRMATIONS){
                status = ACTIVE_GOLDMINE_INPUT_TOO_NEW;
                notCapableReason = strprintf("%s - %d confirmations", GetStatus(), GetInputAge(vin));
                LogPrintf("CActiveGoldmine::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            LOCK(pwalletMain->cs_wallet);
            pwalletMain->LockCoin(vin.prevout);

            // send to all nodes
            CPubKey pubKeyGoldmine;
            CKey keyGoldmine;

            if(!spySendSigner.SetKey(strGoldMinePrivKey, errorMessage, keyGoldmine, pubKeyGoldmine))
            {
                notCapableReason = "Error upon calling SetKey: " + errorMessage;
                LogPrintf("Register::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            CGoldmineBroadcast mnb;
            if(!CreateBroadcast(vin, service, keyCollateralAddress, pubKeyCollateralAddress, keyGoldmine, pubKeyGoldmine, errorMessage, mnb)) {
                notCapableReason = "Error on CreateBroadcast: " + errorMessage;
                LogPrintf("Register::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            //send to all peers
            LogPrintf("CActiveGoldmine::ManageStatus() - Relay broadcast vin = %s\n", vin.ToString());
            mnb.Relay();

            LogPrintf("CActiveGoldmine::ManageStatus() - Is capable master node!\n");
            status = ACTIVE_GOLDMINE_STARTED;

            return;
        } else {
            notCapableReason = "Could not find suitable coins!";
            LogPrintf("CActiveGoldmine::ManageStatus() - %s\n", notCapableReason);
            return;
        }
    }

    //send to all peers
    if(!SendGoldminePing(errorMessage)) {
        LogPrintf("CActiveGoldmine::ManageStatus() - Error on Ping: %s\n", errorMessage);
    }
}

std::string CActiveGoldmine::GetStatus() {
    switch (status) {
    case ACTIVE_GOLDMINE_INITIAL: return "Node just started, not yet activated";
    case ACTIVE_GOLDMINE_SYNC_IN_PROCESS: return "Sync in progress. Must wait until sync is complete to start Goldmine";
    case ACTIVE_GOLDMINE_INPUT_TOO_NEW: return strprintf("Goldmine input must have at least %d confirmations", GOLDMINE_MIN_CONFIRMATIONS);
    case ACTIVE_GOLDMINE_NOT_CAPABLE: return "Not capable goldmine: " + notCapableReason;
    case ACTIVE_GOLDMINE_STARTED: return "Goldmine successfully started";
    default: return "unknown";
    }
}

bool CActiveGoldmine::SendGoldminePing(std::string& errorMessage) {
    if(status != ACTIVE_GOLDMINE_STARTED) {
        errorMessage = "Goldmine is not in a running status";
        return false;
    }

    CPubKey pubKeyGoldmine;
    CKey keyGoldmine;

    if(!spySendSigner.SetKey(strGoldMinePrivKey, errorMessage, keyGoldmine, pubKeyGoldmine))
    {
        errorMessage = strprintf("Error upon calling SetKey: %s\n", errorMessage);
        return false;
    }

    LogPrintf("CActiveGoldmine::SendGoldminePing() - Relay Goldmine Ping vin = %s\n", vin.ToString());
    
    CGoldminePing mnp(vin);
    if(!mnp.Sign(keyGoldmine, pubKeyGoldmine))
    {
        errorMessage = "Couldn't sign Goldmine Ping";
        return false;
    }

    // Update lastPing for our goldmine in Goldmine list
    CGoldmine* pmn = gmineman.Find(vin);
    if(pmn != NULL)
    {
        if(pmn->IsPingedWithin(GOLDMINE_PING_SECONDS, mnp.sigTime)){
            errorMessage = "Too early to send Goldmine Ping";
            return false;
        }

        pmn->lastPing = mnp;
        gmineman.mapSeenGoldminePing.insert(make_pair(mnp.GetHash(), mnp));

        //gmineman.mapSeenGoldmineBroadcast.lastPing is probably outdated, so we'll update it
        CGoldmineBroadcast mnb(*pmn);
        uint256 hash = mnb.GetHash();
        if(gmineman.mapSeenGoldmineBroadcast.count(hash)) gmineman.mapSeenGoldmineBroadcast[hash].lastPing = mnp;

        mnp.Relay();

        return true;
    }
    else
    {
        // Seems like we are trying to send a ping while the Goldmine is not registered in the network
        errorMessage = "Spysend Goldmine List doesn't include our Goldmine, shutting down Goldmine pinging service! " + vin.ToString();
        status = ACTIVE_GOLDMINE_NOT_CAPABLE;
        notCapableReason = errorMessage;
        return false;
    }

}

bool CActiveGoldmine::CreateBroadcast(std::string strService, std::string strKeyGoldmine, std::string strTxHash, std::string strOutputIndex, std::string& errorMessage, CGoldmineBroadcast &mnb, bool fOffline) {
    CTxIn vin;
    CPubKey pubKeyCollateralAddress;
    CKey keyCollateralAddress;
    CPubKey pubKeyGoldmine;
    CKey keyGoldmine;

    //need correct blocks to send ping
    if(!fOffline && !goldmineSync.IsBlockchainSynced()) {
        errorMessage = "Sync in progress. Must wait until sync is complete to start Goldmine";
        LogPrintf("CActiveGoldmine::CreateBroadcast() - %s\n", errorMessage);
        return false;
    }

    if(!spySendSigner.SetKey(strKeyGoldmine, errorMessage, keyGoldmine, pubKeyGoldmine))
    {
        errorMessage = strprintf("Can't find keys for goldmine %s - %s", strService, errorMessage);
        LogPrintf("CActiveGoldmine::CreateBroadcast() - %s\n", errorMessage);
        return false;
    }

    if(!GetGoldMineVin(vin, pubKeyCollateralAddress, keyCollateralAddress, strTxHash, strOutputIndex)) {
        errorMessage = strprintf("Could not allocate vin %s:%s for goldmine %s", strTxHash, strOutputIndex, strService);
        LogPrintf("CActiveGoldmine::CreateBroadcast() - %s\n", errorMessage);
        return false;
    }

    CService service = CService(strService);
    if(Params().NetworkID() == CBaseChainParams::MAIN) {
        if(service.GetPort() != 7209) {
            errorMessage = strprintf("Invalid port %u for goldmine %s - only 7209 is supported on mainnet.", service.GetPort(), strService);
            LogPrintf("CActiveGoldmine::CreateBroadcast() - %s\n", errorMessage);
            return false;
        }
    } else if(service.GetPort() == 7209) {
        errorMessage = strprintf("Invalid port %u for goldmine %s - 7209 is only supported on mainnet.", service.GetPort(), strService);
        LogPrintf("CActiveGoldmine::CreateBroadcast() - %s\n", errorMessage);
        return false;
    }

    addrman.Add(CAddress(service), CNetAddr("127.0.0.1"), 2*60*60);

    return CreateBroadcast(vin, CService(strService), keyCollateralAddress, pubKeyCollateralAddress, keyGoldmine, pubKeyGoldmine, errorMessage, mnb);
}

bool CActiveGoldmine::CreateBroadcast(CTxIn vin, CService service, CKey keyCollateralAddress, CPubKey pubKeyCollateralAddress, CKey keyGoldmine, CPubKey pubKeyGoldmine, std::string &errorMessage, CGoldmineBroadcast &mnb) {
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    CGoldminePing mnp(vin);
    if(!mnp.Sign(keyGoldmine, pubKeyGoldmine)){
        errorMessage = strprintf("Failed to sign ping, vin: %s", vin.ToString());
        LogPrintf("CActiveGoldmine::CreateBroadcast() -  %s\n", errorMessage);
        mnb = CGoldmineBroadcast();
        return false;
    }

    mnb = CGoldmineBroadcast(service, vin, pubKeyCollateralAddress, pubKeyGoldmine, PROTOCOL_VERSION);
    mnb.lastPing = mnp;
    if(!mnb.Sign(keyCollateralAddress)){
        errorMessage = strprintf("Failed to sign broadcast, vin: %s", vin.ToString());
        LogPrintf("CActiveGoldmine::CreateBroadcast() - %s\n", errorMessage);
        mnb = CGoldmineBroadcast();
        return false;
    }

    return true;
}

bool CActiveGoldmine::GetGoldMineVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey) {
    return GetGoldMineVin(vin, pubkey, secretKey, "", "");
}

bool CActiveGoldmine::GetGoldMineVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash, std::string strOutputIndex) {
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    // Find possible candidates
    TRY_LOCK(pwalletMain->cs_wallet, fWallet);
    if(!fWallet) return false;

    vector<COutput> possibleCoins = SelectCoinsGoldmine();
    COutput *selectedOutput;

    // Find the vin
    if(!strTxHash.empty()) {
        // Let's find it
        uint256 txHash(strTxHash);
        int outputIndex = atoi(strOutputIndex.c_str());
        bool found = false;
        BOOST_FOREACH(COutput& out, possibleCoins) {
            if(out.tx->GetHash() == txHash && out.i == outputIndex)
            {
                selectedOutput = &out;
                found = true;
                break;
            }
        }
        if(!found) {
            LogPrintf("CActiveGoldmine::GetGoldMineVin - Could not locate valid vin\n");
            return false;
        }
    } else {
        // No output specified,  Select the first one
        if(possibleCoins.size() > 0) {
            selectedOutput = &possibleCoins[0];
        } else {
            LogPrintf("CActiveGoldmine::GetGoldMineVin - Could not locate specified vin from possible list\n");
            return false;
        }
    }

    // At this point we have a selected output, retrieve the associated info
    return GetVinFromOutput(*selectedOutput, vin, pubkey, secretKey);
}


// Extract Goldmine vin information from output
bool CActiveGoldmine::GetVinFromOutput(COutput out, CTxIn& vin, CPubKey& pubkey, CKey& secretKey) {
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    CScript pubScript;

    vin = CTxIn(out.tx->GetHash(),out.i);
    pubScript = out.tx->vout[out.i].scriptPubKey; // the inputs PubKey

    CTxDestination address1;
    ExtractDestination(pubScript, address1);
    CBitcoinAddress address2(address1);

    CKeyID keyID;
    if (!address2.GetKeyID(keyID)) {
        LogPrintf("CActiveGoldmine::GetGoldMineVin - Address does not refer to a key\n");
        return false;
    }

    if (!pwalletMain->GetKey(keyID, secretKey)) {
        LogPrintf ("CActiveGoldmine::GetGoldMineVin - Private key for address is not known\n");
        return false;
    }

    pubkey = secretKey.GetPubKey();
    return true;
}

// get all possible outputs for running Goldmine
vector<COutput> CActiveGoldmine::SelectCoinsGoldmine()
{
    vector<COutput> vCoins;
    vector<COutput> filteredCoins;
    vector<COutPoint> confLockedCoins;

    // Temporary unlock MN coins from goldmine.conf
    if(GetBoolArg("-mnconflock", true)) {
        uint256 mnTxHash;
        BOOST_FOREACH(CGoldmineConfig::CGoldmineEntry mne, goldmineConfig.getEntries()) {
            mnTxHash.SetHex(mne.getTxHash());
            COutPoint outpoint = COutPoint(mnTxHash, atoi(mne.getOutputIndex().c_str()));
            confLockedCoins.push_back(outpoint);
            pwalletMain->UnlockCoin(outpoint);
        }
    }

    // Retrieve all possible outputs
    pwalletMain->AvailableCoins(vCoins);

    // Lock MN coins from goldmine.conf back if they where temporary unlocked
    if(!confLockedCoins.empty()) {
        BOOST_FOREACH(COutPoint outpoint, confLockedCoins)
            pwalletMain->LockCoin(outpoint);
    }

    // Filter
    BOOST_FOREACH(const COutput& out, vCoins)
    {
        if(out.tx->vout[out.i].nValue == 1000*COIN) { //exactly
            filteredCoins.push_back(out);
        }
    }
    return filteredCoins;
}

// when starting a Goldmine, this can enable to run as a hot wallet with no funds
bool CActiveGoldmine::EnableHotColdGoldMine(CTxIn& newVin, CService& newService)
{
    if(!fGoldMine) return false;

    status = ACTIVE_GOLDMINE_STARTED;

    //The values below are needed for signing mnping messages going forward
    vin = newVin;
    service = newService;

    LogPrintf("CActiveGoldmine::EnableHotColdGoldMine() - Enabled! You may shut down the cold daemon.\n");

    return true;
}
