#include "spysend.h"
#include "spysend-relay.h"


CSpySendRelay::CSpySendRelay()
{
    vinGoldminenode = CTxIn();
    nBlockHeight = 0;
    nRelayType = 0;
    in = CTxIn();
    out = CTxOut();
}

CSpySendRelay::CSpySendRelay(CTxIn& vinGoldminenodeIn, vector<unsigned char>& vchSigIn, int nBlockHeightIn, int nRelayTypeIn, CTxIn& in2, CTxOut& out2)
{
    vinGoldminenode = vinGoldminenodeIn;
    vchSig = vchSigIn;
    nBlockHeight = nBlockHeightIn;
    nRelayType = nRelayTypeIn;
    in = in2;
    out = out2;
}

std::string CSpySendRelay::ToString()
{
    std::ostringstream info;

    info << "vin: " << vinGoldminenode.ToString() <<
        " nBlockHeight: " << (int)nBlockHeight <<
        " nRelayType: "  << (int)nRelayType <<
        " in " << in.ToString() <<
        " out " << out.ToString();
        
    return info.str();   
}

bool CSpySendRelay::Sign(std::string strSharedKey)
{
    std::string strError = "";
    std::string strMessage = in.ToString() + out.ToString();

    CKey key2;
    CPubKey pubkey2;

    if(!spySendSigner.GetKeysFromSecret(strSharedKey, key2, pubkey2)) {
        LogPrintf("CSpySendRelay::Sign -- GetKeysFromSecret() failed, invalid shared key %s\n", strSharedKey);
        return false;
    }

    if(!spySendSigner.SignMessage(strMessage, vchSig2, key2)) {
        LogPrintf("CSpySendRelay::Sign -- SignMessage() failed\n");
        return false;
    }

    if(!spySendSigner.VerifyMessage(pubkey2, vchSig2, strMessage, strError)) {
        LogPrintf("CSpySendRelay::Sign -- VerifyMessage() failed, error: %s\n", strError);
        return false;
    }

    return true;
}

bool CSpySendRelay::VerifyMessage(std::string strSharedKey)
{
    std::string strError = "";
    std::string strMessage = in.ToString() + out.ToString();

    CKey key2;
    CPubKey pubkey2;

    if(!spySendSigner.GetKeysFromSecret(strSharedKey, key2, pubkey2)) {
        LogPrintf("CSpySendRelay::VerifyMessage -- GetKeysFromSecret() failed, invalid shared key %s\n", strSharedKey);
        return false;
    }

    if(!spySendSigner.VerifyMessage(pubkey2, vchSig2, strMessage, strError)) {
        LogPrintf("CSpySendRelay::VerifyMessage -- VerifyMessage() failed, error: %s\n", strError);
        return false;
    }

    return true;
}

void CSpySendRelay::Relay()
{
    int nCount = std::min(mnodeman.CountEnabled(MIN_PRIVATESEND_PEER_PROTO_VERSION), 20);
    int nRank1 = (rand() % nCount)+1; 
    int nRank2 = (rand() % nCount)+1; 

    //keep picking another second number till we get one that doesn't match
    while(nRank1 == nRank2) nRank2 = (rand() % nCount)+1;

    //printf("rank 1 - rank2 %d %d \n", nRank1, nRank2);

    //relay this message through 2 separate nodes for redundancy
    RelayThroughNode(nRank1);
    RelayThroughNode(nRank2);
}

void CSpySendRelay::RelayThroughNode(int nRank)
{
    CGoldminenode* pmn = mnodeman.GetGoldminenodeByRank(nRank, nBlockHeight, MIN_PRIVATESEND_PEER_PROTO_VERSION);

    if(pmn != NULL){
        //printf("RelayThroughNode %s\n", pmn->addr.ToString().c_str());
        CNode* pnode = ConnectNode((CAddress)pmn->addr, NULL);
        if(pnode) {
            //printf("Connected\n");
            pnode->PushMessage("dsr", (*this));
            return;
        }
    } else {
        //printf("RelayThroughNode NULL\n");
    }
}
