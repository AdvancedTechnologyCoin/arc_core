// Copyright (c) 2015-2017 The Arctic Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activegoldminenode.h"
#include "goldminenode.h"
#include "goldminenode-sync.h"
#include "goldminenodeman.h"
#include "protocol.h"

extern CWallet* pwalletMain;

// Keep track of the active Goldminenode
CActiveGoldminenode activeGoldminenode;

void CActiveGoldminenode::ManageState()
{
    LogPrint("goldminenode", "CActiveGoldminenode::ManageState -- Start\n");
    if(!fGoldmineNode) {
        LogPrint("goldminenode", "CActiveGoldminenode::ManageState -- Not a goldminenode, returning\n");
        return;
    }

    if(Params().NetworkIDString() != CBaseChainParams::REGTEST && !goldminenodeSync.IsBlockchainSynced()) {
        nState = ACTIVE_GOLDMINENODE_SYNC_IN_PROCESS;
        LogPrintf("CActiveGoldminenode::ManageState -- %s: %s\n", GetStateString(), GetStatus());
        return;
    }

    if(nState == ACTIVE_GOLDMINENODE_SYNC_IN_PROCESS) {
        nState = ACTIVE_GOLDMINENODE_INITIAL;
    }

    LogPrint("goldminenode", "CActiveGoldminenode::ManageState -- status = %s, type = %s, pinger enabled = %d\n", GetStatus(), GetTypeString(), fPingerEnabled);

    if(eType == GOLDMINENODE_UNKNOWN) {
        ManageStateInitial();
    }

    if(eType == GOLDMINENODE_REMOTE) {
        ManageStateRemote();
    } else if(eType == GOLDMINENODE_LOCAL) {
        // Try Remote Start first so the started local goldminenode can be restarted without recreate goldminenode broadcast.
        ManageStateRemote();
        if(nState != ACTIVE_GOLDMINENODE_STARTED)
            ManageStateLocal();
    }

    SendGoldminenodePing();
}

std::string CActiveGoldminenode::GetStateString() const
{
    switch (nState) {
        case ACTIVE_GOLDMINENODE_INITIAL:         return "INITIAL";
        case ACTIVE_GOLDMINENODE_SYNC_IN_PROCESS: return "SYNC_IN_PROCESS";
        case ACTIVE_GOLDMINENODE_INPUT_TOO_NEW:   return "INPUT_TOO_NEW";
        case ACTIVE_GOLDMINENODE_NOT_CAPABLE:     return "NOT_CAPABLE";
        case ACTIVE_GOLDMINENODE_STARTED:         return "STARTED";
        default:                                return "UNKNOWN";
    }
}

std::string CActiveGoldminenode::GetStatus() const
{
    switch (nState) {
        case ACTIVE_GOLDMINENODE_INITIAL:         return "Node just started, not yet activated";
        case ACTIVE_GOLDMINENODE_SYNC_IN_PROCESS: return "Sync in progress. Must wait until sync is complete to start Goldminenode";
        case ACTIVE_GOLDMINENODE_INPUT_TOO_NEW:   return strprintf("Goldminenode input must have at least %d confirmations", Params().GetConsensus().nGoldminenodeMinimumConfirmations);
        case ACTIVE_GOLDMINENODE_NOT_CAPABLE:     return "Not capable goldminenode: " + strNotCapableReason;
        case ACTIVE_GOLDMINENODE_STARTED:         return "Goldminenode successfully started";
        default:                                return "Unknown";
    }
}

std::string CActiveGoldminenode::GetTypeString() const
{
    std::string strType;
    switch(eType) {
    case GOLDMINENODE_UNKNOWN:
        strType = "UNKNOWN";
        break;
    case GOLDMINENODE_REMOTE:
        strType = "REMOTE";
        break;
    case GOLDMINENODE_LOCAL:
        strType = "LOCAL";
        break;
    default:
        strType = "UNKNOWN";
        break;
    }
    return strType;
}

bool CActiveGoldminenode::SendGoldminenodePing()
{
    if(!fPingerEnabled) {
        LogPrint("goldminenode", "CActiveGoldminenode::SendGoldminenodePing -- %s: goldminenode ping service is disabled, skipping...\n", GetStateString());
        return false;
    }

    if(!mnodeman.Has(vin)) {
        strNotCapableReason = "Goldminenode not in goldminenode list";
        nState = ACTIVE_GOLDMINENODE_NOT_CAPABLE;
        LogPrintf("CActiveGoldminenode::SendGoldminenodePing -- %s: %s\n", GetStateString(), strNotCapableReason);
        return false;
    }

    CGoldminenodePing mnp(vin);
    if(!mnp.Sign(keyGoldminenode, pubKeyGoldminenode)) {
        LogPrintf("CActiveGoldminenode::SendGoldminenodePing -- ERROR: Couldn't sign Goldminenode Ping\n");
        return false;
    }

    // Update lastPing for our goldminenode in Goldminenode list
    if(mnodeman.IsGoldminenodePingedWithin(vin, GOLDMINENODE_MIN_MNP_SECONDS, mnp.sigTime)) {
        LogPrintf("CActiveGoldminenode::SendGoldminenodePing -- Too early to send Goldminenode Ping\n");
        return false;
    }

    mnodeman.SetGoldminenodeLastPing(vin, mnp);

    LogPrintf("CActiveGoldminenode::SendGoldminenodePing -- Relaying ping, collateral=%s\n", vin.ToString());
    mnp.Relay();

    return true;
}

void CActiveGoldminenode::ManageStateInitial()
{
    LogPrint("goldminenode", "CActiveGoldminenode::ManageStateInitial -- status = %s, type = %s, pinger enabled = %d\n", GetStatus(), GetTypeString(), fPingerEnabled);

    // Check that our local network configuration is correct
    if (!fListen) {
        // listen option is probably overwritten by smth else, no good
        nState = ACTIVE_GOLDMINENODE_NOT_CAPABLE;
        strNotCapableReason = "Goldminenode must accept connections from outside. Make sure listen configuration option is not overwritten by some another parameter.";
        LogPrintf("CActiveGoldminenode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    bool fFoundLocal = false;
    {
        LOCK(cs_vNodes);

        // First try to find whatever local address is specified by externalip option
        fFoundLocal = GetLocal(service) && CGoldminenode::IsValidNetAddr(service);
        if(!fFoundLocal) {
            // nothing and no live connections, can't do anything for now
            if (vNodes.empty()) {
                nState = ACTIVE_GOLDMINENODE_NOT_CAPABLE;
                strNotCapableReason = "Can't detect valid external address. Will retry when there are some connections available.";
                LogPrintf("CActiveGoldminenode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
                return;
            }
            // We have some peers, let's try to find our local address from one of them
            BOOST_FOREACH(CNode* pnode, vNodes) {
                if (pnode->fSuccessfullyConnected && pnode->addr.IsIPv4()) {
                    fFoundLocal = GetLocal(service, &pnode->addr) && CGoldminenode::IsValidNetAddr(service);
                    if(fFoundLocal) break;
                }
            }
        }
    }

    if(!fFoundLocal) {
        nState = ACTIVE_GOLDMINENODE_NOT_CAPABLE;
        strNotCapableReason = "Can't detect valid external address. Please consider using the externalip configuration option if problem persists. Make sure to use IPv4 address only.";
        LogPrintf("CActiveGoldminenode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
    if(Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if(service.GetPort() != mainnetDefaultPort) {
            nState = ACTIVE_GOLDMINENODE_NOT_CAPABLE;
            strNotCapableReason = strprintf("Invalid port: %u - only %d is supported on mainnet.", service.GetPort(), mainnetDefaultPort);
            LogPrintf("CActiveGoldminenode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
    } else if(service.GetPort() == mainnetDefaultPort) {
        nState = ACTIVE_GOLDMINENODE_NOT_CAPABLE;
        strNotCapableReason = strprintf("Invalid port: %u - %d is only supported on mainnet.", service.GetPort(), mainnetDefaultPort);
        LogPrintf("CActiveGoldminenode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    LogPrintf("CActiveGoldminenode::ManageStateInitial -- Checking inbound connection to '%s'\n", service.ToString());

    if(!ConnectNode((CAddress)service, NULL, true)) {
        nState = ACTIVE_GOLDMINENODE_NOT_CAPABLE;
        strNotCapableReason = "Could not connect to " + service.ToString();
        LogPrintf("CActiveGoldminenode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    // Default to REMOTE
    eType = GOLDMINENODE_REMOTE;

    // Check if wallet funds are available
    if(!pwalletMain) {
        LogPrintf("CActiveGoldminenode::ManageStateInitial -- %s: Wallet not available\n", GetStateString());
        return;
    }

    if(pwalletMain->IsLocked()) {
        LogPrintf("CActiveGoldminenode::ManageStateInitial -- %s: Wallet is locked\n", GetStateString());
        return;
    }

    if(pwalletMain->GetBalance() < 1000*COIN) {
        LogPrintf("CActiveGoldminenode::ManageStateInitial -- %s: Wallet balance is < 1000 ARC\n", GetStateString());
        return;
    }

    // Choose coins to use
    CPubKey pubKeyCollateral;
    CKey keyCollateral;

    // If collateral is found switch to LOCAL mode
    if(pwalletMain->GetGoldminenodeVinAndKeys(vin, pubKeyCollateral, keyCollateral)) {
        eType = GOLDMINENODE_LOCAL;
    }

    LogPrint("goldminenode", "CActiveGoldminenode::ManageStateInitial -- End status = %s, type = %s, pinger enabled = %d\n", GetStatus(), GetTypeString(), fPingerEnabled);
}

void CActiveGoldminenode::ManageStateRemote()
{
    LogPrint("goldminenode", "CActiveGoldminenode::ManageStateRemote -- Start status = %s, type = %s, pinger enabled = %d, pubKeyGoldminenode.GetID() = %s\n", 
             GetStatus(), fPingerEnabled, GetTypeString(), pubKeyGoldminenode.GetID().ToString());

    mnodeman.CheckGoldminenode(pubKeyGoldminenode);
    goldminenode_info_t infoMn = mnodeman.GetGoldminenodeInfo(pubKeyGoldminenode);
    if(infoMn.fInfoValid) {
        if(infoMn.nProtocolVersion != PROTOCOL_VERSION) {
            nState = ACTIVE_GOLDMINENODE_NOT_CAPABLE;
            strNotCapableReason = "Invalid protocol version";
            LogPrintf("CActiveGoldminenode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if(service != infoMn.addr) {
            nState = ACTIVE_GOLDMINENODE_NOT_CAPABLE;
            strNotCapableReason = "Broadcasted IP doesn't match our external address. Make sure you issued a new broadcast if IP of this goldminenode changed recently.";
            LogPrintf("CActiveGoldminenode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if(!CGoldminenode::IsValidStateForAutoStart(infoMn.nActiveState)) {
            nState = ACTIVE_GOLDMINENODE_NOT_CAPABLE;
            strNotCapableReason = strprintf("Goldminenode in %s state", CGoldminenode::StateToString(infoMn.nActiveState));
            LogPrintf("CActiveGoldminenode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if(nState != ACTIVE_GOLDMINENODE_STARTED) {
            LogPrintf("CActiveGoldminenode::ManageStateRemote -- STARTED!\n");
            vin = infoMn.vin;
            service = infoMn.addr;
            fPingerEnabled = true;
            nState = ACTIVE_GOLDMINENODE_STARTED;
        }
    }
    else {
        nState = ACTIVE_GOLDMINENODE_NOT_CAPABLE;
        strNotCapableReason = "Goldminenode not in goldminenode list";
        LogPrintf("CActiveGoldminenode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
    }
}

void CActiveGoldminenode::ManageStateLocal()
{
    LogPrint("goldminenode", "CActiveGoldminenode::ManageStateLocal -- status = %s, type = %s, pinger enabled = %d\n", GetStatus(), GetTypeString(), fPingerEnabled);
    if(nState == ACTIVE_GOLDMINENODE_STARTED) {
        return;
    }

    // Choose coins to use
    CPubKey pubKeyCollateral;
    CKey keyCollateral;

    if(pwalletMain->GetGoldminenodeVinAndKeys(vin, pubKeyCollateral, keyCollateral)) {
        int nInputAge = GetInputAge(vin);
        if(nInputAge < Params().GetConsensus().nGoldminenodeMinimumConfirmations){
            nState = ACTIVE_GOLDMINENODE_INPUT_TOO_NEW;
            strNotCapableReason = strprintf(_("%s - %d confirmations"), GetStatus(), nInputAge);
            LogPrintf("CActiveGoldminenode::ManageStateLocal -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }

        {
            LOCK(pwalletMain->cs_wallet);
            pwalletMain->LockCoin(vin.prevout);
        }

        CGoldminenodeBroadcast mnb;
        std::string strError;
        if(!CGoldminenodeBroadcast::Create(vin, service, keyCollateral, pubKeyCollateral, keyGoldminenode, pubKeyGoldminenode, strError, mnb)) {
            nState = ACTIVE_GOLDMINENODE_NOT_CAPABLE;
            strNotCapableReason = "Error creating mastenode broadcast: " + strError;
            LogPrintf("CActiveGoldminenode::ManageStateLocal -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }

        fPingerEnabled = true;
        nState = ACTIVE_GOLDMINENODE_STARTED;

        //update to goldminenode list
        LogPrintf("CActiveGoldminenode::ManageStateLocal -- Update Goldminenode List\n");
        mnodeman.UpdateGoldminenodeList(mnb);
        mnodeman.NotifyGoldminenodeUpdates();

        //send to all peers
        LogPrintf("CActiveGoldminenode::ManageStateLocal -- Relay broadcast, vin=%s\n", vin.ToString());
        mnb.Relay();
    }
}
