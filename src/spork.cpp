// Copyright (c) 2017-2022 The Advanced Technology Coin
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "spork.h"

#include "base58.h"
#include "chainparams.h"
#include "validation.h"
#include "messagesigner.h"
#include "net_processing.h"
#include "netmessagemaker.h"

#include <boost/lexical_cast.hpp>

CSporkManager sporkManager;

std::map<uint256, CSporkMessage> mapSporks;
std::map<int, int64_t> mapSporkDefaults = {
    {SPORK_2_INSTANTSEND_ENABLED,            0},             // ON
    {SPORK_3_INSTANTSEND_BLOCK_FILTERING,    0},             // ON
    {SPORK_5_INSTANTSEND_MAX_VALUE,          1000},          // 1000 Arc
    {SPORK_6_NEW_SIGS,                       4070908800ULL}, // OFF
    {SPORK_8_GOLDMINENODE_PAYMENT_ENFORCEMENT, 4070908800ULL}, // OFF
    {SPORK_9_SUPERBLOCKS_ENABLED,            4070908800ULL}, // OFF
    {SPORK_10_GOLDMINENODE_PAY_UPDATED_NODES,  4070908800ULL}, // OFF
    {SPORK_12_RECONSIDER_BLOCKS,             0},             // 0 BLOCKS
    {SPORK_13_OLD_SUPERBLOCK_FLAG,             4070908800ULL},             // 0 BLOCKS
    {SPORK_14_REQUIRE_SENTINEL_FLAG,             4070908800ULL},             // 0 BLOCKS
    {SPORK_18_EVOLUTION_PAYMENTS,         4070908800ULL}, // OFF
    {SPORK_19_EVOLUTION_PAYMENTS_ENFORCEMENT,         4070908800ULL}, // OFF
    {SPORK_21_GOLDMINENODE_ORDER_ENABLE,         4070908800ULL}, // OFF
    {SPORK_22_GOLDMINENODE_UPDATE_PROTO,         4070908800ULL}, // OFF
    {SPORK_23_GOLDMINENODE_UPDATE_PROTO2,         4070908800ULL}, // OFF
    {SPORK_24_UPGRADE,         4070908800ULL}, // OFF
};
CEvolutionManager evolutionManager;

CCriticalSection cs_mapEvolution;
CCriticalSection cs_mapActive;

void CSporkManager::ProcessSpork(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv, CConnman& connman)
{
    if(fLiteMode) return; // disable all Arc specific functionality

    if (strCommand == NetMsgType::SPORK) {
        CSporkMessage spork;
        vRecv >> spork;

        uint256 hash = spork.GetHash();

        std::string strLogMsg;
        {
            LOCK(cs_main);
            pfrom->setAskFor.erase(hash);
            if(!chainActive.Tip()) return;
            strLogMsg = strprintf("SPORK -- hash: %s id: %d value: %10d bestHeight: %d peer=%d", hash.ToString(), spork.nSporkID, spork.nValue, chainActive.Height(), pfrom->id);
        }

        if(mapSporksActive.count(spork.nSporkID)) {
            if (mapSporksActive[spork.nSporkID].nTimeSigned >= spork.nTimeSigned) {
                LogPrint("spork", "%s seen\n", strLogMsg);
                return;
            } else {
                LogPrintf("%s updated\n", strLogMsg);
            }
        } else {
            LogPrintf("%s new\n", strLogMsg);
        }

        if(!spork.CheckSignature(sporkPubKeyID)) {
            LOCK(cs_main);
            LogPrintf("CSporkManager::ProcessSpork -- ERROR: invalid signature\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        mapSporks[hash] = spork;
        mapSporksActive[spork.nSporkID] = spork;
		if( spork.nSporkID == SPORK_18_EVOLUTION_PAYMENTS )
        {
			evolutionManager.setNewEvolutions( spork.sWEvolution );
		}
        spork.Relay(connman);

        //does a task if needed
        ExecuteSpork(spork.nSporkID, spork.nValue);

    } else if (strCommand == NetMsgType::GETSPORKS) {

        std::map<int, CSporkMessage>::iterator it = mapSporksActive.begin();

        while(it != mapSporksActive.end()) {
            connman.PushMessage(pfrom, CNetMsgMaker(pfrom->GetSendVersion()).Make(NetMsgType::SPORK, it->second));
            it++;
        }
    }

}

void CSporkManager::ExecuteSpork(int nSporkID, int nValue)
{
    //correct fork via spork technology
    if(nSporkID == SPORK_12_RECONSIDER_BLOCKS && nValue > 0) {
        // allow to reprocess 24h of blocks max, which should be enough to resolve any issues
        int64_t nMaxBlocks = 576;
        // this potentially can be a heavy operation, so only allow this to be executed once per 10 minutes
        int64_t nTimeout = 10 * 60;

        static int64_t nTimeExecuted = 0; // i.e. it was never executed before

        if(GetTime() - nTimeExecuted < nTimeout) {
            LogPrint("spork", "CSporkManager::ExecuteSpork -- ERROR: Trying to reconsider blocks, too soon - %d/%d\n", GetTime() - nTimeExecuted, nTimeout);
            return;
        }

        if(nValue > nMaxBlocks) {
            LogPrintf("CSporkManager::ExecuteSpork -- ERROR: Trying to reconsider too many blocks %d/%d\n", nValue, nMaxBlocks);
            return;
        }


        LogPrintf("CSporkManager::ExecuteSpork -- Reconsider Last %d Blocks\n", nValue);

        ReprocessBlocks(nValue);
        nTimeExecuted = GetTime();
    }
}

bool CSporkManager::UpdateSpork(int nSporkID, int64_t nValue, std::string sEvol, CConnman& connman)
{

    CSporkMessage spork = CSporkMessage(nSporkID, nValue,"", GetAdjustedTime());

    if(spork.Sign(sporkPrivKey)) {
        spork.Relay(connman);
        mapSporks[spork.GetHash()] = spork;
        mapSporksActive[nSporkID] = spork;
        if(nSporkID == SPORK_18_EVOLUTION_PAYMENTS){
			evolutionManager.setNewEvolutions( sEvol );
		}
        return true;
    }

    return false;
}

// grab the spork, otherwise say it's off
bool CSporkManager::IsSporkActive(int nSporkID)
{
    int64_t r = -1;

    if(mapSporksActive.count(nSporkID)){
        r = mapSporksActive[nSporkID].nValue;
    } else if (mapSporkDefaults.count(nSporkID)) {
        r = mapSporkDefaults[nSporkID];
    } else {
        LogPrint("spork", "CSporkManager::IsSporkActive -- Unknown Spork ID %d\n", nSporkID);
        r = 4070908800ULL; // 2099-1-1 i.e. off by default
    }
    if(nSporkID != SPORK_18_EVOLUTION_PAYMENTS)
        return r < GetAdjustedTime();
    else
         return !evolutionManager.getEvolution(chainActive.Height()).empty();
}

// grab the value of the spork on the network, or the default
int64_t CSporkManager::GetSporkValue(int nSporkID)
{
    if (mapSporksActive.count(nSporkID))
        return mapSporksActive[nSporkID].nValue;

    if (mapSporkDefaults.count(nSporkID)) {
        return mapSporkDefaults[nSporkID];
    }

    LogPrint("spork", "CSporkManager::GetSporkValue -- Unknown Spork ID %d\n", nSporkID);
    return -1;
}

int CSporkManager::GetSporkIDByName(const std::string& strName)
{
    if (strName == "SPORK_2_INSTANTSEND_ENABLED")               return SPORK_2_INSTANTSEND_ENABLED;
    if (strName == "SPORK_3_INSTANTSEND_BLOCK_FILTERING")       return SPORK_3_INSTANTSEND_BLOCK_FILTERING;
    if (strName == "SPORK_5_INSTANTSEND_MAX_VALUE")             return SPORK_5_INSTANTSEND_MAX_VALUE;
    if (strName == "SPORK_6_NEW_SIGS")                          return SPORK_6_NEW_SIGS;
    if (strName == "SPORK_8_GOLDMINENODE_PAYMENT_ENFORCEMENT")    return SPORK_8_GOLDMINENODE_PAYMENT_ENFORCEMENT;
    if (strName == "SPORK_9_SUPERBLOCKS_ENABLED")               return SPORK_9_SUPERBLOCKS_ENABLED;
    if (strName == "SPORK_10_GOLDMINENODE_PAY_UPDATED_NODES")     return SPORK_10_GOLDMINENODE_PAY_UPDATED_NODES;
    if (strName == "SPORK_12_RECONSIDER_BLOCKS")                return SPORK_12_RECONSIDER_BLOCKS;
    if (strName == "SPORK_13_OLD_SUPERBLOCK_FLAG")                return SPORK_13_OLD_SUPERBLOCK_FLAG;
    if (strName == "SPORK_14_REQUIRE_SENTINEL_FLAG")            return SPORK_14_REQUIRE_SENTINEL_FLAG;
	if (strName == "SPORK_18_EVOLUTION_PAYMENTS")				return SPORK_18_EVOLUTION_PAYMENTS;
    if (strName == "SPORK_19_EVOLUTION_PAYMENTS_ENFORCEMENT")   return SPORK_19_EVOLUTION_PAYMENTS_ENFORCEMENT;
    if (strName == "SPORK_21_GOLDMINENODE_ORDER_ENABLE")        return SPORK_21_GOLDMINENODE_ORDER_ENABLE;
    if (strName == "SPORK_22_GOLDMINENODE_UPDATE_PROTO")        return SPORK_22_GOLDMINENODE_UPDATE_PROTO;
    if (strName == "SPORK_23_GOLDMINENODE_UPDATE_PROTO2")       return SPORK_23_GOLDMINENODE_UPDATE_PROTO2;
    if (strName == "SPORK_24_UPGRADE")                          return SPORK_24_UPGRADE;

    LogPrint("spork", "CSporkManager::GetSporkIDByName -- Unknown Spork name '%s'\n", strName);
    return -1;
}

std::string CSporkManager::GetSporkNameByID(int nSporkID)
{
    switch (nSporkID) {
        case SPORK_2_INSTANTSEND_ENABLED:               return "SPORK_2_INSTANTSEND_ENABLED";
        case SPORK_3_INSTANTSEND_BLOCK_FILTERING:       return "SPORK_3_INSTANTSEND_BLOCK_FILTERING";
        case SPORK_5_INSTANTSEND_MAX_VALUE:             return "SPORK_5_INSTANTSEND_MAX_VALUE";
        case SPORK_6_NEW_SIGS:                          return "SPORK_6_NEW_SIGS";
        case SPORK_8_GOLDMINENODE_PAYMENT_ENFORCEMENT:    return "SPORK_8_GOLDMINENODE_PAYMENT_ENFORCEMENT";
        case SPORK_9_SUPERBLOCKS_ENABLED:               return "SPORK_9_SUPERBLOCKS_ENABLED";
        case SPORK_10_GOLDMINENODE_PAY_UPDATED_NODES:     return "SPORK_10_GOLDMINENODE_PAY_UPDATED_NODES";
        case SPORK_12_RECONSIDER_BLOCKS:                return "SPORK_12_RECONSIDER_BLOCKS";
        case SPORK_13_OLD_SUPERBLOCK_FLAG:              return "SPORK_13_OLD_SUPERBLOCK_FLAG";
        case SPORK_14_REQUIRE_SENTINEL_FLAG:            return "SPORK_14_REQUIRE_SENTINEL_FLAG";
		case SPORK_18_EVOLUTION_PAYMENTS:				return "SPORK_18_EVOLUTION_PAYMENTS";
		case SPORK_19_EVOLUTION_PAYMENTS_ENFORCEMENT:   return "SPORK_19_EVOLUTION_PAYMENTS_ENFORCEMENT";    
		case SPORK_21_GOLDMINENODE_ORDER_ENABLE:        return "SPORK_21_GOLDMINENODE_ORDER_ENABLE";   
		case SPORK_22_GOLDMINENODE_UPDATE_PROTO:        return "SPORK_22_GOLDMINENODE_UPDATE_PROTO";     
		case SPORK_23_GOLDMINENODE_UPDATE_PROTO2:       return "SPORK_23_GOLDMINENODE_UPDATE_PROTO2";     
		case SPORK_24_UPGRADE:                          return "SPORK_24_UPGRADE";   
        default:
            LogPrint("spork", "CSporkManager::GetSporkNameByID -- Unknown Spork ID %d\n", nSporkID);
            return "Unknown";
    }
}

bool CSporkManager::SetSporkAddress(const std::string& strAddress) {
    CBitcoinAddress address(strAddress);
    if (!address.IsValid() || !address.GetKeyID(sporkPubKeyID)) {
        LogPrintf("CSporkManager::SetSporkAddress -- Failed to parse spork address\n");
        return false;
    }
    return true;
}

bool CSporkManager::SetPrivKey(const std::string& strPrivKey)
{
    CKey key;
    CPubKey pubKey;
    if(!CMessageSigner::GetKeysFromSecret(strPrivKey, key, pubKey)) {
        LogPrintf("CSporkManager::SetPrivKey -- Failed to parse private key\n");
        return false;
    }

    if (pubKey.GetID() != sporkPubKeyID) {
        LogPrintf("CSporkManager::SetPrivKey -- New private key does not belong to spork address\n");
        return false;
    }

    CSporkMessage spork;
    if (spork.Sign(key)) {
        // Test signing successful, proceed
        LogPrintf("CSporkManager::SetPrivKey -- Successfully initialized as spork signer\n");

        sporkPrivKey = key;
        return true;
    } else {
        LogPrintf("CSporkManager::SetPrivKey -- Test signing failed\n");
        return false;
    }
}

uint256 CSporkMessage::GetHash() const
{
    return SerializeHash(*this);
}

uint256 CSporkMessage::GetSignatureHash() const
{
    return GetHash();
}

bool CSporkMessage::Sign(const CKey& key)
{
    if (!key.IsValid()) {
        LogPrintf("CSporkMessage::Sign -- signing key is not valid\n");
        return false;
    }

    CKeyID pubKeyId = key.GetPubKey().GetID();
    std::string strError = "";

    if (sporkManager.IsSporkActive(SPORK_6_NEW_SIGS)) {
        uint256 hash = GetSignatureHash();

        if(!CHashSigner::SignHash(hash, key, vchSig)) {
            LogPrintf("CSporkMessage::Sign -- SignHash() failed\n");
            return false;
        }

        if (!CHashSigner::VerifyHash(hash, pubKeyId, vchSig, strError)) {
            LogPrintf("CSporkMessage::Sign -- VerifyHash() failed, error: %s\n", strError);
            return false;
        }
    } else {
        std::string strMessage = boost::lexical_cast<std::string>(nSporkID) + boost::lexical_cast<std::string>(nValue) + boost::lexical_cast<std::string>(nTimeSigned);

        if(!CMessageSigner::SignMessage(strMessage, vchSig, key)) {
            LogPrintf("CSporkMessage::Sign -- SignMessage() failed\n");
            return false;
        }

        if(!CMessageSigner::VerifyMessage(pubKeyId, vchSig, strMessage, strError)) {
            LogPrintf("CSporkMessage::Sign -- VerifyMessage() failed, error: %s\n", strError);
            return false;
        }
    }

    return true;
}

bool CSporkMessage::CheckSignature(const CKeyID& pubKeyId) const
{
    std::string strError = "";
	    //note: need to investigate why this is failing
    
    if (!sporkManager.IsSporkActive(SPORK_22_GOLDMINENODE_UPDATE_PROTO)) 
    {
	    std::string strMessage = boost::lexical_cast<std::string>(nSporkID) + boost::lexical_cast<std::string>(nValue) + boost::lexical_cast<std::string>(nTimeSigned);
	    CPubKey pubkey(ParseHex(Params().SporkPubKey()));
	
	    if(!CMessageSigner::VerifyMessage(pubkey, vchSig, strMessage, strError)) 
	    {
	        LogPrintf("CSporkMessage::CheckSignature -- VerifyMessage() failed, error: %s\n", strError);
	        return false;
	    }
	        
	}
	else
	{        
	    if (sporkManager.IsSporkActive(SPORK_6_NEW_SIGS)) 
	    {
	        uint256 hash = GetSignatureHash();
	
	        if (!CHashSigner::VerifyHash(hash, pubKeyId, vchSig, strError)) 
	        {
	            // Note: unlike for many other messages when SPORK_6_NEW_SIGS is ON sporks with sigs in old format
	            // and newer timestamps should not be accepted, so if we failed here - that's it
	            LogPrintf("CSporkMessage::CheckSignature -- VerifyHash() failed, error: %s\n", strError);
	            return false;
	        }
	    } 
	    else 
	    {
	        std::string strMessage = boost::lexical_cast<std::string>(nSporkID) + boost::lexical_cast<std::string>(nValue) + boost::lexical_cast<std::string>(nTimeSigned);
	
	        if (!CMessageSigner::VerifyMessage(pubKeyId, vchSig, strMessage, strError))
	        {
	            // Note: unlike for other messages we have to check for new format even with SPORK_6_NEW_SIGS
	            // inactive because SPORK_6_NEW_SIGS default is OFF and it is not the first spork to sync
	            // (and even if it would, spork order can't be guaranteed anyway).
	            uint256 hash = GetSignatureHash();
	            if (!CHashSigner::VerifyHash(hash, pubKeyId, vchSig, strError)) 
	            {
	                LogPrintf("CSporkMessage::CheckSignature -- VerifyHash() failed, error: %s\n", strError);
	                return false;
	            }
	        }
	    }
	}
    return true;
}

void CSporkMessage::Relay(CConnman& connman)
{
    CInv inv(MSG_SPORK, GetHash());
    connman.RelayInv(inv);
}

void CEvolutionManager::setNewEvolutions( const std::string &sEvol )
{
	LOCK( cs_mapEvolution );
	
	unsigned long bComplete = 0;
	unsigned long uCountEvolution = 0;
	unsigned long uStart = 0;
	
	mapEvolution.clear();
    if(sEvol.size()<10)
    {
    	mapEvolution.clear();
    	return;
    }
    if(sporkManager.IsSporkActive(SPORK_18_EVOLUTION_PAYMENTS)) 
    {
        LogPrintf("SPORK_18_EVOLUTION_PAYMENTS is active\n");
    }
    else
        LogPrintf("SPORK_18_EVOLUTION_PAYMENTS NOT ACTIVE\n");
	for( unsigned int i = 0; i < sEvol.size() ; i++ )
	{
		if( (sEvol.c_str()[i] == '[') && (bComplete == 0) ){
			bComplete = 1;
			uStart = i + 1;
		}

		if( (sEvol.c_str()[i] == ',') && (bComplete == 1)  ){
			mapEvolution.insert(  make_pair( uCountEvolution, std::string(sEvol.c_str() + uStart, i-uStart))  );
			uCountEvolution++;
			uStart = i + 1;
		}

		if(  (sEvol.c_str()[i] == ']') && (bComplete == 1)  ){
			bComplete = 2;
			mapEvolution.insert(  make_pair( uCountEvolution, std::string(sEvol.c_str() + uStart, i - uStart))  );
			uCountEvolution++;
		}
	}
}

std::string CEvolutionManager::getEvolution( int nBlockHeight )
{	
	LOCK( cs_mapEvolution );
	if(mapEvolution.size()==0)
		return "";
	else
		return mapEvolution[ nBlockHeight%mapEvolution.size() ];
}	

bool CEvolutionManager::IsTransactionValid( const CTransaction& txNew, int nBlockHeight, CAmount blockCurEvolution  )
{	
	LOCK( cs_mapEvolution );
	
	if( mapEvolution.size() < 1 ){ 
		return true;
	}
	
	CTxDestination address1;

	for( unsigned int i = 0; i < txNew.vout.size(); i++ )
	{	
		ExtractDestination(txNew.vout[i].scriptPubKey, address1);
		CBitcoinAddress address2(address1);

		if(  ( mapEvolution[nBlockHeight%mapEvolution.size()] == address2.ToString() ) && (blockCurEvolution == txNew.vout[i].nValue)  ){
			return true;
		}
	}	

	return false;
}
	
bool CEvolutionManager::checkEvolutionString( const std::string &sEvol )
{	
	unsigned long bComplete = 0;

	for (unsigned int i = 0; i < sEvol.size(); i++)
	{
		if (sEvol.c_str()[i] == '[')
		{
			bComplete = 1;
		}

		if ((sEvol.c_str()[i] == ']') && (bComplete == 1))
		{
			bComplete = 2;
		}
	}

	if( bComplete == 2 ){
		return true;
	}

	return false;
}
