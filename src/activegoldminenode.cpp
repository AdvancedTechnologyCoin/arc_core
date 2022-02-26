// Copyright (c) 2017-2022 The Advanced Technology Coin
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activegoldminenode.h"
#include "goldminenode.h"
#include "goldminenode-sync.h"
#include "goldminenodeman.h"
#include "netbase.h"
#include "protocol.h"

// Keep track of the active Goldminenode
CActiveGoldminenode activeGoldminenode;

void CActiveGoldminenode::ManageState(CConnman& connman)
{
    LogPrint("goldminenode", "CActiveGoldminenode::ManageState -- Start\n");
    if(!fGoldminenodeMode) {
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
        ManageStateInitial(connman);
    }

    if(eType == GOLDMINENODE_REMOTE) {
        ManageStateRemote();
    }

    SendGoldminenodePing(connman);
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
    case GOLDMINENODE_REMOTE:
        strType = "REMOTE";
        break;
    default:
        strType = "UNKNOWN";
        break;
    }
    return strType;
}

bool CActiveGoldminenode::SendGoldminenodePing(CConnman& connman)
{
    if(!fPingerEnabled) {
        LogPrint("goldminenode", "CActiveGoldminenode::SendGoldminenodePing -- %s: goldminenode ping service is disabled, skipping...\n", GetStateString());
        return false;
    }

    if(!mnodeman.Has(outpoint)) {
        strNotCapableReason = "Goldminenode not in goldminenode list";
        nState = ACTIVE_GOLDMINENODE_NOT_CAPABLE;
        LogPrintf("CActiveGoldminenode::SendGoldminenodePing -- %s: %s\n", GetStateString(), strNotCapableReason);
        return false;
    }

    CGoldminenodePing mnp(outpoint);
    mnp.nSentinelVersion = nSentinelVersion;
    mnp.fSentinelIsCurrent =
            (abs(GetAdjustedTime() - nSentinelPingTime) < GOLDMINENODE_SENTINEL_PING_MAX_SECONDS);
    if(!mnp.Sign(keyGoldminenode, pubKeyGoldminenode)) {
        LogPrintf("CActiveGoldminenode::SendGoldminenodePing -- ERROR: Couldn't sign Goldminenode Ping\n");
        return false;
    }

    // Update lastPing for our goldminenode in Goldminenode list
    if(mnodeman.IsGoldminenodePingedWithin(outpoint, GOLDMINENODE_MIN_MNP_SECONDS, mnp.sigTime)) {
        LogPrintf("CActiveGoldminenode::SendGoldminenodePing -- Too early to send Goldminenode Ping\n");
        return false;
    }

    mnodeman.SetGoldminenodeLastPing(outpoint, mnp);

    LogPrintf("CActiveGoldminenode::SendGoldminenodePing -- Relaying ping, collateral=%s\n", outpoint.ToStringShort());
    mnp.Relay(connman);

    return true;
}

bool CActiveGoldminenode::UpdateSentinelPing(int version)
{
    nSentinelVersion = version;
    nSentinelPingTime = GetAdjustedTime();

    return true;
}

void CActiveGoldminenode::ManageStateInitial(CConnman& connman)
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

    // First try to find whatever local address is specified by externalip option
    bool fFoundLocal = GetLocal(service) && CGoldminenode::IsValidNetAddr(service);
    if(!fFoundLocal) {
        bool empty = true;
        // If we have some peers, let's try to find our local address from one of them
        connman.ForEachNodeContinueIf(CConnman::AllNodes, [&fFoundLocal, &empty, this](CNode* pnode) {
            empty = false;
            if (pnode->addr.IsIPv4())
                fFoundLocal = GetLocal(service, &pnode->addr) && CGoldminenode::IsValidNetAddr(service);
            return !fFoundLocal;
        });
        // nothing and no live connections, can't do anything for now
        if (empty) {
            nState = ACTIVE_GOLDMINENODE_NOT_CAPABLE;
            strNotCapableReason = "Can't detect valid external address. Will retry when there are some connections available.";
            LogPrintf("CActiveGoldminenode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
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

    // Check socket connectivity
    LogPrintf("CActiveGoldminenode::ManageStateInitial -- Checking inbound connection to '%s'\n", service.ToString());
    SOCKET hSocket;
    bool fConnected = ConnectSocket(service, hSocket, nConnectTimeout) && IsSelectableSocket(hSocket);
    CloseSocket(hSocket);

    if (!fConnected) {
        nState = ACTIVE_GOLDMINENODE_NOT_CAPABLE;
        strNotCapableReason = "Could not connect to " + service.ToString();
        LogPrintf("CActiveGoldminenode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    // Default to REMOTE
    eType = GOLDMINENODE_REMOTE;

    LogPrint("goldminenode", "CActiveGoldminenode::ManageStateInitial -- End status = %s, type = %s, pinger enabled = %d\n", GetStatus(), GetTypeString(), fPingerEnabled);
}

void CActiveGoldminenode::ManageStateRemote()
{
    LogPrint("goldminenode", "CActiveGoldminenode::ManageStateRemote -- Start status = %s, type = %s, pinger enabled = %d, pubKeyGoldminenode.GetID() = %s\n", 
             GetStatus(), GetTypeString(), fPingerEnabled, pubKeyGoldminenode.GetID().ToString());

    mnodeman.CheckGoldminenode(pubKeyGoldminenode, true);
    goldminenode_info_t infoMn;
    if(mnodeman.GetGoldminenodeInfo(pubKeyGoldminenode, infoMn)) {
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
            outpoint = infoMn.outpoint;
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
