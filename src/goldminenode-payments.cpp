// Copyright (c) 2019 The Advanced Technology Coin and Eternity Group
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activegoldminenode.h"
#include "goldminenode-payments.h"
#include "goldminenode-sync.h"
#include "goldminenodeman.h"
#include "messagesigner.h"
#include "netfulfilledman.h"
#include "spork.h"
#include "util.h"
#include "script/standard.h"
#include "base58.h"

#include <boost/lexical_cast.hpp>

/** Object for who's going to get paid on which blocks */
CGoldminenodePayments mnpayments;

CCriticalSection cs_vecPayees;
CCriticalSection cs_mapGoldminenodeBlocks;
CCriticalSection cs_mapGoldminenodePaymentVotes;

/**
* IsBlockValueValid
*
*   Determine if coinbase outgoing created money is the correct value
*
*   Why is this needed?
*   - In Arc some blocks are superblocks, which output much higher amounts of coins
*   - Otherblocks are 10% lower in outgoing value, so in total, no extra coins are created
*   - When non-superblocks are detected, the normal schedule should be maintained
*/

bool IsBlockValueValid(const CBlock& block, int nBlockHeight, CAmount blockReward, std::string &strErrorRet)
{
    strErrorRet = "";

    bool isBlockRewardValueMet = (block.vtx[0].GetValueOut() <= blockReward);
    if(fDebug) LogPrintf("block.vtx[0].GetValueOut() %lld <= blockReward %lld\n", block.vtx[0].GetValueOut(), blockReward);


    if(!goldminenodeSync.IsSynced()) {
        if(!isBlockRewardValueMet) {
            strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, only regular blocks are allowed at this height",
                                    nBlockHeight, block.vtx[0].GetValueOut(), blockReward);
        }
        // it MUST be a regular block otherwise
        return isBlockRewardValueMet;
    }
	
    // we are synced, let's try to check as much data as we can
	
	// should NOT allow superblocks at all, when superblocks are disabled
	LogPrint("gobject", "IsBlockValueValid -- Superblocks are disabled, no superblocks allowed\n");
	if(!isBlockRewardValueMet) {
		strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, superblocks are disabled",
								nBlockHeight, block.vtx[0].GetValueOut(), blockReward);
	}
	
    // it MUST be a regular block
    return isBlockRewardValueMet;
}

bool IsBlockPayeeValid(const CTransaction& txNew, int nBlockHeight, CAmount blockReward)
{
    if(!goldminenodeSync.IsSynced()) {
        //there is no budget data to use to check anything, let's just accept the longest chain
        if(fDebug) LogPrintf("IsBlockPayeeValid -- WARNING: Client not synced, skipping block payee checks\n");
        return true;
    }
	
	// should NOT allow superblocks at all, when superblocks are disabled
	LogPrint("gobject", "IsBlockPayeeValid -- Superblocks are disabled, no superblocks allowed\n");
    
    // IF THIS ISN'T A SUPERBLOCK OR SUPERBLOCK IS INVALID, IT SHOULD PAY A GOLDMINENODE DIRECTLY
    if(mnpayments.IsTransactionValid(txNew, nBlockHeight)) {
        LogPrint("mnpayments", "IsBlockPayeeValid -- Valid goldminenode payment at height %d: %s", nBlockHeight, txNew.ToString());
        return true;
    }

    if(sporkManager.IsSporkActive(SPORK_8_GOLDMINENODE_PAYMENT_ENFORCEMENT)) {
        LogPrintf("IsBlockPayeeValid -- ERROR: Invalid goldminenode payment detected at height %d: %s", nBlockHeight, txNew.ToString());
        return false;
    }

    LogPrintf("IsBlockPayeeValid -- WARNING: Goldminenode payment enforcement is disabled, accepting any payee\n");
    return true;
}

void FillBlockPayments(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CAmount blockEvolution, CTxOut& txoutGoldminenodeRet, std::vector<CTxOut>& voutSuperblockRet)
{
    // only create superblocks if spork is enabled 
	if(  sporkManager.IsSporkWorkActive(SPORK_18_EVOLUTION_PAYMENTS) ){	
		CGoldminenodePayments::CreateEvolution(  txNew, nBlockHeight, blockEvolution, voutSuperblockRet  );
    }		
	
    // FILL BLOCK PAYEE WITH GOLDMINENODE PAYMENT OTHERWISE
    mnpayments.FillBlockPayee(txNew, nBlockHeight, blockReward, txoutGoldminenodeRet);
    LogPrint("mnpayments", "FillBlockPayments -- nBlockHeight %d blockReward %lld txoutGoldminenodeRet %s txNew %s",
                            nBlockHeight, blockReward, txoutGoldminenodeRet.ToString(), txNew.ToString());
}

std::string GetRequiredPaymentsString(int nBlockHeight)
{
    // OTHERWISE, PAY GOLDMINENODE
    return mnpayments.GetRequiredPaymentsString(nBlockHeight);
}

void CGoldminenodePayments::Clear()
{
    LOCK2(cs_mapGoldminenodeBlocks, cs_mapGoldminenodePaymentVotes);
    mapGoldminenodeBlocks.clear();
    mapGoldminenodePaymentVotes.clear();
}

bool CGoldminenodePayments::CanVote(COutPoint outGoldminenode, int nBlockHeight)
{
    LOCK(cs_mapGoldminenodePaymentVotes);

    if (mapGoldminenodesLastVote.count(outGoldminenode) && mapGoldminenodesLastVote[outGoldminenode] == nBlockHeight) {
        return false;
    }

    //record this goldminenode voted
    mapGoldminenodesLastVote[outGoldminenode] = nBlockHeight;
    return true;
}

/**
*   FillBlockPayee
*
*   Fill Goldminenode ONLY payment block
*/

void CGoldminenodePayments::FillBlockPayee(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CTxOut& txoutGoldminenodeRet)
{
    // make sure it's not filled yet
    txoutGoldminenodeRet = CTxOut();

    CScript payee;
    LogPrintf("CGoldminenodePayments::FillBlockPayee -- prepare \n");

    if(!mnpayments.GetBlockPayee(nBlockHeight, payee)) {
        // no goldminenode detected...
        if(sporkManager.IsSporkActive(SPORK_23_GOLDMINENODE_UPDATE_PROTO2))
		{
	        int nCount = 0;
	        goldminenode_info_t mnInfo;
	        if(!mnodeman.GetNextGoldminenodeInQueueForTmp(nBlockHeight, true, nCount, mnInfo)) {
	            // ...and we can't calculate it on our own
	            LogPrintf("CGoldminenodePayments::FillBlockPayee -- Failed to detect goldminenode to pay\n");
	            return;
	        }
	        // fill payee with locally calculated winner and hope for the best
	        payee = GetScriptForDestination(mnInfo.pubKeyCollateralAddress.GetID());
    	}
    	else
    	{
    		// no goldminenode detected...and we can't calculate it on our own
			LogPrintf("CGoldminenodePayments::FillBlockPayee -- Failed to detect goldminenode to pay\n");
			return;
    	}
    }

    // GET GOLDMINENODE PAYMENT VARIABLES SETUP
    CAmount goldminenodePayment = GetGoldminenodePayment(nBlockHeight, blockReward);

    // split reward between miner ...
    txNew.vout[0].nValue -= goldminenodePayment;
    // ... and goldminenode
    txoutGoldminenodeRet = CTxOut(goldminenodePayment, payee);
    txNew.vout.push_back(txoutGoldminenodeRet);

    CTxDestination address1;
    ExtractDestination(payee, address1);
    CBitcoinAddress address2(address1);

    LogPrintf("CGoldminenodePayments::FillBlockPayee -- Goldminenode payment %lld to %s\n", goldminenodePayment, address2.ToString());
}

int CGoldminenodePayments::GetMinGoldminenodePaymentsProto() {
    return sporkManager.IsSporkActive(SPORK_10_GOLDMINENODE_PAY_UPDATED_NODES)
            ? MIN_GOLDMINENODE_PAYMENT_PROTO_VERSION_2
            : MIN_GOLDMINENODE_PAYMENT_PROTO_VERSION_1;	
}

void CGoldminenodePayments::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv, CConnman& connman)
{
    if(fLiteMode) return; // disable all Arc specific functionality

    if (strCommand == NetMsgType::GOLDMINENODEPAYMENTSYNC) { //Goldminenode Payments Request Sync

        // Ignore such requests until we are fully synced.
        // We could start processing this after goldminenode list is synced
        // but this is a heavy one so it's better to finish sync first.
        if (!goldminenodeSync.IsSynced()) return;

        int nCountNeeded;
        vRecv >> nCountNeeded;

        if(netfulfilledman.HasFulfilledRequest(pfrom->addr, NetMsgType::GOLDMINENODEPAYMENTSYNC)) {
            // Asking for the payments list multiple times in a short period of time is no good
            LogPrintf("GOLDMINENODEPAYMENTSYNC -- peer already asked me for the list, peer=%d\n", pfrom->id);
            Misbehaving(pfrom->GetId(), 20);
            return;
        }
        netfulfilledman.AddFulfilledRequest(pfrom->addr, NetMsgType::GOLDMINENODEPAYMENTSYNC);

        Sync(pfrom, connman);
        LogPrintf("GOLDMINENODEPAYMENTSYNC -- Sent Goldminenode payment votes to peer %d\n", pfrom->id);

    } else if (strCommand == NetMsgType::GOLDMINENODEPAYMENTVOTE) { // Goldminenode Payments Vote for the Winner

        CGoldminenodePaymentVote vote;
        vRecv >> vote;

        if(pfrom->nVersion < GetMinGoldminenodePaymentsProto()) return;

        uint256 nHash = vote.GetHash();

        pfrom->setAskFor.erase(nHash);

        // TODO: clear setAskFor for MSG_GOLDMINENODE_PAYMENT_BLOCK too

        // Ignore any payments messages until goldminenode list is synced
        if(!goldminenodeSync.IsGoldminenodeListSynced()) return;

        {
            LOCK(cs_mapGoldminenodePaymentVotes);
            if(mapGoldminenodePaymentVotes.count(nHash)) {
                LogPrint("mnpayments", "GOLDMINENODEPAYMENTVOTE -- hash=%s, nHeight=%d seen\n", nHash.ToString(), nCachedBlockHeight);
                return;
            }

            // Avoid processing same vote multiple times
            mapGoldminenodePaymentVotes[nHash] = vote;
            // but first mark vote as non-verified,
            // AddPaymentVote() below should take care of it if vote is actually ok
            mapGoldminenodePaymentVotes[nHash].MarkAsNotVerified();
        }

        int nFirstBlock = nCachedBlockHeight - GetStorageLimit();
        if(vote.nBlockHeight < nFirstBlock || vote.nBlockHeight > nCachedBlockHeight+20) {
            LogPrint("mnpayments", "GOLDMINENODEPAYMENTVOTE -- vote out of range: nFirstBlock=%d, nBlockHeight=%d, nHeight=%d\n", nFirstBlock, vote.nBlockHeight, nCachedBlockHeight);
            return;
        }

        std::string strError = "";
        if(!vote.IsValid(pfrom, nCachedBlockHeight, strError, connman)) {
            LogPrint("mnpayments", "GOLDMINENODEPAYMENTVOTE -- invalid message, error: %s\n", strError);
            return;
        }

        if(!CanVote(vote.vinGoldminenode.prevout, vote.nBlockHeight)) {
            LogPrintf("GOLDMINENODEPAYMENTVOTE -- goldminenode already voted, goldminenode=%s\n", vote.vinGoldminenode.prevout.ToStringShort());
            return;
        }

        goldminenode_info_t mnInfo;
        if(!mnodeman.GetGoldminenodeInfo(vote.vinGoldminenode.prevout, mnInfo)) {
            // mn was not found, so we can't check vote, some info is probably missing
            LogPrintf("GOLDMINENODEPAYMENTVOTE -- goldminenode is missing %s\n", vote.vinGoldminenode.prevout.ToStringShort());
            mnodeman.AskForMN(pfrom, vote.vinGoldminenode.prevout, connman);
            return;
        }

        int nDos = 0;
        if(!vote.CheckSignature(mnInfo.pubKeyGoldminenode, nCachedBlockHeight, nDos)) {
            if(nDos) {
                LogPrintf("GOLDMINENODEPAYMENTVOTE -- ERROR: invalid signature\n");
                Misbehaving(pfrom->GetId(), nDos);
            } else {
                // only warn about anything non-critical (i.e. nDos == 0) in debug mode
                LogPrint("mnpayments", "GOLDMINENODEPAYMENTVOTE -- WARNING: invalid signature\n");
            }
            // Either our info or vote info could be outdated.
            // In case our info is outdated, ask for an update,
            mnodeman.AskForMN(pfrom, vote.vinGoldminenode.prevout, connman);
            // but there is nothing we can do if vote info itself is outdated
            // (i.e. it was signed by a mn which changed its key),
            // so just quit here.
            return;
        }
        if(!sporkManager.IsSporkActive(SPORK_23_GOLDMINENODE_UPDATE_PROTO2))
		{
			int nCount = 0;
	        //masternode_info_t mnInfo;
			if (!mnodeman.GetNextGoldminenodeInQueueForPayment(vote.nBlockHeight, true, nCount, mnInfo)) {
				LogPrintf("CGoldminenodePayments::ProcessBlock -- ERROR: Failed to find masternode to pay\n");
	            return;
			}
			LogPrintf("CGoldminenodePayments::ProcessBlock -- Masternode found : %s\n", mnInfo.vin.prevout.ToStringShort());
	
			CScript payee = GetScriptForDestination(mnInfo.pubKeyCollateralAddress.GetID());
			CGoldminenodePaymentVote voteNew(vote.vinGoldminenode.prevout, vote.nBlockHeight, payee);
	       

	        CTxDestination address1;
	        ExtractDestination(voteNew.payee, address1);
	        CBitcoinAddress address2(address1);
			
	
	        LogPrint("mnpayments"," MASTERNODEPAYMENTVOTE -- vote: address=%s, nBlockHeight=%d, nHeight=%d, prevout=%s, hash=%s new\n",
	                    address2.ToString(), voteNew.nBlockHeight, nCachedBlockHeight, voteNew.vinGoldminenode.prevout.ToStringShort(), nHash.ToString());
			
	        if(AddPaymentVote(voteNew)){
	            voteNew.Relay(connman);
	            goldminenodeSync.BumpAssetLastTime("MASTERNODEPAYMENTVOTE");
	        }
		}
		else
		{
	        CTxDestination address1;
	        ExtractDestination(vote.payee, address1);
	        CBitcoinAddress address2(address1);
	
	        LogPrint("mnpayments", "GOLDMINENODEPAYMENTVOTE -- vote: address=%s, nBlockHeight=%d, nHeight=%d, prevout=%s, hash=%s new\n",
	                    address2.ToString(), vote.nBlockHeight, nCachedBlockHeight, vote.vinGoldminenode.prevout.ToStringShort(), nHash.ToString());
	
	        if(AddPaymentVote(vote)){
	            vote.Relay(connman);
	            goldminenodeSync.BumpAssetLastTime("GOLDMINENODEPAYMENTVOTE");
	        }
    	}
    }
}

bool CGoldminenodePaymentVote::Sign()
{
    std::string strError;
    std::string strMessage = vinGoldminenode.prevout.ToStringShort() +
                boost::lexical_cast<std::string>(nBlockHeight) +
                ScriptToAsmStr(payee);

    if(!CMessageSigner::SignMessage(strMessage, vchSig, activeGoldminenode.keyGoldminenode)) {
        LogPrintf("CGoldminenodePaymentVote::Sign -- SignMessage() failed\n");
        return false;
    }

    if(!CMessageSigner::VerifyMessage(activeGoldminenode.pubKeyGoldminenode, vchSig, strMessage, strError)) {
        LogPrintf("CGoldminenodePaymentVote::Sign -- VerifyMessage() failed, error: %s\n", strError);
        return false;
    }

    return true;
}

bool CGoldminenodePayments::GetBlockPayee(int nBlockHeight, CScript& payee)
{
    if(mapGoldminenodeBlocks.count(nBlockHeight)){
        return mapGoldminenodeBlocks[nBlockHeight].GetBestPayee(payee);
    }

    return false;
}

// Is this goldminenode scheduled to get paid soon?
// -- Only look ahead up to 8 blocks to allow for propagation of the latest 2 blocks of votes
bool CGoldminenodePayments::IsScheduled(CGoldminenode& mn, int nNotBlockHeight)
{
    LOCK(cs_mapGoldminenodeBlocks);

    if(!goldminenodeSync.IsGoldminenodeListSynced()) return false;

    CScript mnpayee;
    mnpayee = GetScriptForDestination(mn.pubKeyCollateralAddress.GetID());

    CScript payee;
    for(int64_t h = nCachedBlockHeight; h <= nCachedBlockHeight + 8; h++){
        if(h == nNotBlockHeight) continue;
        if(mapGoldminenodeBlocks.count(h) && mapGoldminenodeBlocks[h].GetBestPayee(payee) && mnpayee == payee) {
            return true;
        }
    }

    return false;
}

bool CGoldminenodePayments::AddPaymentVote(const CGoldminenodePaymentVote& vote)
{
    uint256 blockHash = uint256();
    if(!GetBlockHash(blockHash, vote.nBlockHeight - 101)) return false;

    if(HasVerifiedPaymentVote(vote.GetHash())) return false;

    LOCK2(cs_mapGoldminenodeBlocks, cs_mapGoldminenodePaymentVotes);

	if(sporkManager.IsSporkActive(SPORK_21_GOLDMINENODE_ORDER_ENABLE))
	{
		auto it2 = mapGoldminenodeBlocks.find(vote.nBlockHeight);
		if(it2!=mapGoldminenodeBlocks.end())
			return false;
	}
	
	CTxDestination address1;
	ExtractDestination(vote.payee, address1);
	CBitcoinAddress address2(address1);
	

    mapGoldminenodePaymentVotes[vote.GetHash()] = vote;

    if(!mapGoldminenodeBlocks.count(vote.nBlockHeight)) {
       CGoldminenodeBlockPayees blockPayees(vote.nBlockHeight);
       mapGoldminenodeBlocks[vote.nBlockHeight] = blockPayees;
    }

    mapGoldminenodeBlocks[vote.nBlockHeight].AddPayee(vote);

    return true;
}

bool CGoldminenodePayments::HasVerifiedPaymentVote(uint256 hashIn)
{
    LOCK(cs_mapGoldminenodePaymentVotes);
    std::map<uint256, CGoldminenodePaymentVote>::iterator it = mapGoldminenodePaymentVotes.find(hashIn);
    return it != mapGoldminenodePaymentVotes.end() && it->second.IsVerified();
}

void CGoldminenodeBlockPayees::AddPayee(const CGoldminenodePaymentVote& vote)
{
    LOCK(cs_vecPayees);

    BOOST_FOREACH(CGoldminenodePayee& payee, vecPayees) {
        if (payee.GetPayee() == vote.payee) {
            payee.AddVoteHash(vote.GetHash());
            return;
        }
    }
    CGoldminenodePayee payeeNew(vote.payee, vote.GetHash());
    vecPayees.push_back(payeeNew);
}

bool CGoldminenodeBlockPayees::GetBestPayee(CScript& payeeRet)
{
    LOCK(cs_vecPayees);

    if(!vecPayees.size()) {
        LogPrint("mnpayments", "CGoldminenodeBlockPayees::GetBestPayee -- ERROR: couldn't find any payee\n");
        return false;
    }
		int nVotes = -1;
	if(sporkManager.IsSporkActive(SPORK_21_GOLDMINENODE_ORDER_ENABLE)) 
	{
	    BOOST_FOREACH(CGoldminenodePayee& payee, vecPayees) {
			payeeRet = payee.GetPayee();
	    }
    	return true;
	}
	else
	{
		BOOST_FOREACH(CGoldminenodePayee& payee, vecPayees) {
			if (payee.GetVoteCount() > nVotes) {
				payeeRet = payee.GetPayee();
				nVotes = payee.GetVoteCount();
			}
		}
	}

    return (nVotes > -1);
}

bool CGoldminenodeBlockPayees::HasPayeeWithVotes(const CScript& payeeIn, int nVotesReq)
{
    LOCK(cs_vecPayees);

    BOOST_FOREACH(CGoldminenodePayee& payee, vecPayees) {
        if (payee.GetVoteCount() >= nVotesReq && payee.GetPayee() == payeeIn) {
            return true;
        }
    }

    LogPrint("mnpayments", "CGoldminenodeBlockPayees::HasPayeeWithVotes -- ERROR: couldn't find any payee with %d+ votes\n", nVotesReq);
    return false;
}

bool CGoldminenodeBlockPayees::IsTransactionValid(const CTransaction& txNew)
{	
    LOCK(cs_vecPayees);
	
    int nMaxSignatures = 0;
	std::string strPayeesPossible = "";
	
	CAmount nGoldminenodePayment;
	if( sporkManager.IsSporkWorkActive(SPORK_18_EVOLUTION_PAYMENTS) ){
		CScript payeeEvo;
		CBitcoinAddress address( evolutionManager.getEvolution(nBlockHeight) );
		payeeEvo = GetScriptForDestination( address.Get() );
		nGoldminenodePayment = GetGoldminenodePayment(nBlockHeight, txNew.GetValueOutWOEvol(payeeEvo));
	}else{
		nGoldminenodePayment = GetGoldminenodePayment(nBlockHeight, txNew.GetValueOut());
	}
		
    //require at least MNPAYMENTS_SIGNATURES_REQUIRED signatures
	
   
	if(!sporkManager.IsSporkActive(SPORK_21_GOLDMINENODE_ORDER_ENABLE)) 
	{
		BOOST_FOREACH(CGoldminenodePayee& payee, vecPayees) {
	        if (payee.GetVoteCount() >= nMaxSignatures) {
	            nMaxSignatures = payee.GetVoteCount();
	        }
		}
	}
    // if we don't have at least MNPAYMENTS_SIGNATURES_REQUIRED signatures on a payee, approve whichever is the longest chain
    if(nMaxSignatures < MNPAYMENTS_SIGNATURES_REQUIRED) return true;

    BOOST_FOREACH(CGoldminenodePayee& payee, vecPayees) {
        if (payee.GetVoteCount() >= MNPAYMENTS_SIGNATURES_REQUIRED) {
            BOOST_FOREACH(CTxOut txout, txNew.vout) {
                if (payee.GetPayee() == txout.scriptPubKey && nGoldminenodePayment == txout.nValue) {
                    LogPrint("mnpayments", "CGoldminenodeBlockPayees::IsTransactionValid -- Found required payment\n");
                    return true;
                }
            }

            CTxDestination address1;
            ExtractDestination(payee.GetPayee(), address1);
            CBitcoinAddress address2(address1);

            if(strPayeesPossible == "") {
                strPayeesPossible = address2.ToString();
            } else {
                strPayeesPossible += "," + address2.ToString();
            }
        }
    }

    LogPrintf("CGoldminenodeBlockPayees::IsTransactionValid -- ERROR: Missing required payment, possible payees: '%s', amount: %f ARC\n", strPayeesPossible, (float)nGoldminenodePayment/COIN);
    return false;
}

std::string CGoldminenodeBlockPayees::GetRequiredPaymentsString()
{
    LOCK(cs_vecPayees);

    std::string strRequiredPayments = "Unknown";

    BOOST_FOREACH(CGoldminenodePayee& payee, vecPayees)
    {
        CTxDestination address1;
        ExtractDestination(payee.GetPayee(), address1);
        CBitcoinAddress address2(address1);

        if (strRequiredPayments != "Unknown") {
            strRequiredPayments += ", " + address2.ToString() + ":" + boost::lexical_cast<std::string>(payee.GetVoteCount());
        } else {
            strRequiredPayments = address2.ToString() + ":" + boost::lexical_cast<std::string>(payee.GetVoteCount());
        }
    }

    return strRequiredPayments;
}

std::string CGoldminenodePayments::GetRequiredPaymentsString(int nBlockHeight)
{
    LOCK(cs_mapGoldminenodeBlocks);

    if(mapGoldminenodeBlocks.count(nBlockHeight)){
        return mapGoldminenodeBlocks[nBlockHeight].GetRequiredPaymentsString();
    }

    return "Unknown";
}

bool CGoldminenodePayments::IsTransactionValid(const CTransaction& txNew, int nBlockHeight)
{
    LOCK(cs_mapGoldminenodeBlocks);

    if(mapGoldminenodeBlocks.count(nBlockHeight)){
        return mapGoldminenodeBlocks[nBlockHeight].IsTransactionValid(txNew);
    }

    return true;
}

void CGoldminenodePayments::CheckAndRemove()
{
    if(!goldminenodeSync.IsBlockchainSynced()) return;

    LOCK2(cs_mapGoldminenodeBlocks, cs_mapGoldminenodePaymentVotes);

    int nLimit = GetStorageLimit();

    std::map<uint256, CGoldminenodePaymentVote>::iterator it = mapGoldminenodePaymentVotes.begin();
    while(it != mapGoldminenodePaymentVotes.end()) {
        CGoldminenodePaymentVote vote = (*it).second;

        if(nCachedBlockHeight - vote.nBlockHeight > nLimit) {
            LogPrint("mnpayments", "CGoldminenodePayments::CheckAndRemove -- Removing old Goldminenode payment: nBlockHeight=%d\n", vote.nBlockHeight);
            mapGoldminenodePaymentVotes.erase(it++);
            mapGoldminenodeBlocks.erase(vote.nBlockHeight);
        } else {
            ++it;
        }
    }
    LogPrintf("CGoldminenodePayments::CheckAndRemove -- %s\n", ToString());
}

bool CGoldminenodePaymentVote::IsValid(CNode* pnode, int nValidationHeight, std::string& strError, CConnman& connman)
{
    goldminenode_info_t mnInfo;

    if(!mnodeman.GetGoldminenodeInfo(vinGoldminenode.prevout, mnInfo)) {
        strError = strprintf("Unknown Goldminenode: prevout=%s", vinGoldminenode.prevout.ToStringShort());
        // Only ask if we are already synced and still have no idea about that Goldminenode
        if(goldminenodeSync.IsGoldminenodeListSynced()) {
            mnodeman.AskForMN(pnode, vinGoldminenode.prevout, connman);
        }

        return false;
    }

    int nMinRequiredProtocol;
    if(nBlockHeight >= nValidationHeight) {
        // new votes must comply SPORK_10_GOLDMINENODE_PAY_UPDATED_NODES rules
        nMinRequiredProtocol = mnpayments.GetMinGoldminenodePaymentsProto();
    } else {
        // allow non-updated goldminenodes for old blocks
        nMinRequiredProtocol = MIN_GOLDMINENODE_PAYMENT_PROTO_VERSION_1;
    }

    if(mnInfo.nProtocolVersion < nMinRequiredProtocol) {
        strError = strprintf("Goldminenode protocol is too old: nProtocolVersion=%d, nMinRequiredProtocol=%d", mnInfo.nProtocolVersion, nMinRequiredProtocol);
        return false;
    }

    // Only goldminenodes should try to check goldminenode rank for old votes - they need to pick the right winner for future blocks.
    // Regular clients (miners included) need to verify goldminenode rank for future block votes only.
    if(!fGoldmineNode && nBlockHeight < nValidationHeight) return true;

    return true;
}

bool CGoldminenodePayments::ProcessBlock(int nBlockHeight, CConnman& connman)
{
    // DETERMINE IF WE SHOULD BE VOTING FOR THE NEXT PAYEE

    if(fLiteMode || !fGoldmineNode) return false;

    // We have little chances to pick the right winner if winners list is out of sync
    // but we have no choice, so we'll try. However it doesn't make sense to even try to do so
    // if we have not enough data about goldminenodes.
    if(!goldminenodeSync.IsGoldminenodeListSynced()) return false;

    // LOCATE THE NEXT GOLDMINENODE WHICH SHOULD BE PAID

    LogPrintf("CGoldminenodePayments::ProcessBlock -- Start: nBlockHeight=%d, goldminenode=%s\n", nBlockHeight, activeGoldminenode.outpoint.ToStringShort());

    // pay to the oldest MN that still had no payment but its input is old enough and it was active long enough
    int nCount = 0;
    goldminenode_info_t mnInfo;

    if (!mnodeman.GetNextGoldminenodeInQueueForPayment(nBlockHeight, true, nCount, mnInfo)) {
        LogPrintf("CGoldminenodePayments::ProcessBlock -- ERROR: Failed to find goldminenode to pay\n");
        return false;
    }

    LogPrintf("CGoldminenodePayments::ProcessBlock -- Goldminenode found: %s\n", mnInfo.vin.prevout.ToStringShort());


    CScript payee = GetScriptForDestination(mnInfo.pubKeyCollateralAddress.GetID());

    CGoldminenodePaymentVote voteNew(activeGoldminenode.outpoint, nBlockHeight, payee);

    CTxDestination address1;
    ExtractDestination(payee, address1);
    CBitcoinAddress address2(address1);

    LogPrintf("CGoldminenodePayments::ProcessBlock -- winner: payee=%s, nBlockHeight=%d\n", address2.ToString(), nBlockHeight);

    // SIGN MESSAGE TO NETWORK WITH OUR GOLDMINENODE KEYS

    LogPrintf("CGoldminenodePayments::ProcessBlock -- Signing winner\n");
    if (voteNew.Sign()) {
        LogPrintf("CGoldminenodePayments::ProcessBlock -- AddPaymentWinner()\n");

        if (AddPaymentVote(voteNew)) {
            voteNew.Relay(connman);
            return true;
        }
    }

    return false;
}

void CGoldminenodePayments::CheckPreviousBlockVotes(int nPrevBlockHeight)
{
    if (!goldminenodeSync.IsWinnersListSynced()) return;
}

void CGoldminenodePaymentVote::Relay(CConnman& connman)
{
    // Do not relay until fully synced
    if(!goldminenodeSync.IsSynced()) {
        LogPrint("mnpayments", "CGoldminenodePayments::Relay -- won't relay until fully synced\n");
        return;
    }

    CInv inv(MSG_GOLDMINENODE_PAYMENT_VOTE, GetHash());
    connman.RelayInv(inv);
}

bool CGoldminenodePaymentVote::CheckSignature(const CPubKey& pubKeyGoldminenode, int nValidationHeight, int &nDos)
{
    // do not ban by default
    nDos = 0;

    std::string strMessage = vinGoldminenode.prevout.ToStringShort() +
                boost::lexical_cast<std::string>(nBlockHeight) +
                ScriptToAsmStr(payee);

    std::string strError = "";
    if (!CMessageSigner::VerifyMessage(pubKeyGoldminenode, vchSig, strMessage, strError)) {
        // Only ban for future block vote when we are already synced.
        // Otherwise it could be the case when MN which signed this vote is using another key now
        // and we have no idea about the old one.
        if(goldminenodeSync.IsGoldminenodeListSynced() && nBlockHeight > nValidationHeight) {
            nDos = 20;
        }
        return error("CGoldminenodePaymentVote::CheckSignature -- Got bad Goldminenode payment signature, goldminenode=%s, error: %s", vinGoldminenode.prevout.ToStringShort().c_str(), strError);
    }

    return true;
}

std::string CGoldminenodePaymentVote::ToString() const
{
    std::ostringstream info;

    info << vinGoldminenode.prevout.ToStringShort() <<
            ", " << nBlockHeight <<
            ", " << ScriptToAsmStr(payee) <<
            ", " << (int)vchSig.size();

    return info.str();
}

// Send only votes for future blocks, node should request every other missing payment block individually
void CGoldminenodePayments::Sync(CNode* pnode, CConnman& connman)
{
    LOCK(cs_mapGoldminenodeBlocks);

    if(!goldminenodeSync.IsWinnersListSynced()) return;

    int nInvCount = 0;

    for(int h = nCachedBlockHeight; h < nCachedBlockHeight + 20; h++) {
        if(mapGoldminenodeBlocks.count(h)) {
            BOOST_FOREACH(CGoldminenodePayee& payee, mapGoldminenodeBlocks[h].vecPayees) {
                std::vector<uint256> vecVoteHashes = payee.GetVoteHashes();
                BOOST_FOREACH(uint256& hash, vecVoteHashes) {
                    if(!HasVerifiedPaymentVote(hash)) continue;
                    pnode->PushInventory(CInv(MSG_GOLDMINENODE_PAYMENT_VOTE, hash));
                    nInvCount++;
                }
            }
        }
    }

    LogPrintf("CGoldminenodePayments::Sync -- Sent %d votes to peer %d\n", nInvCount, pnode->id);
    connman.PushMessage(pnode, NetMsgType::SYNCSTATUSCOUNT, GOLDMINENODE_SYNC_MNW, nInvCount);
}

// Request low data/unknown payment blocks in batches directly from some node instead of/after preliminary Sync.
void CGoldminenodePayments::RequestLowDataPaymentBlocks(CNode* pnode, CConnman& connman)
{
    if(!goldminenodeSync.IsGoldminenodeListSynced()) return;

    LOCK2(cs_main, cs_mapGoldminenodeBlocks);

    std::vector<CInv> vToFetch;
    int nLimit = GetStorageLimit();

    const CBlockIndex *pindex = chainActive.Tip();

    while(nCachedBlockHeight - pindex->nHeight < nLimit) {
        if(!mapGoldminenodeBlocks.count(pindex->nHeight)) {
            // We have no idea about this block height, let's ask
            vToFetch.push_back(CInv(MSG_GOLDMINENODE_PAYMENT_BLOCK, pindex->GetBlockHash()));
            // We should not violate GETDATA rules
            if(vToFetch.size() == MAX_INV_SZ) {
                LogPrintf("CGoldminenodePayments::SyncLowDataPaymentBlocks -- asking peer %d for %d blocks\n", pnode->id, MAX_INV_SZ);
                connman.PushMessage(pnode, NetMsgType::GETDATA, vToFetch);
                // Start filling new batch
                vToFetch.clear();
            }
        }
        if(!pindex->pprev) break;
        pindex = pindex->pprev;
    }

    std::map<int, CGoldminenodeBlockPayees>::iterator it = mapGoldminenodeBlocks.begin();

    while(it != mapGoldminenodeBlocks.end()) {
        int nTotalVotes = 0;
        bool fFound = false;
        BOOST_FOREACH(CGoldminenodePayee& payee, it->second.vecPayees) {
            if(payee.GetVoteCount() >= MNPAYMENTS_SIGNATURES_REQUIRED) {
                fFound = true;
                break;
            }
            nTotalVotes += payee.GetVoteCount();
        }
        // A clear winner (MNPAYMENTS_SIGNATURES_REQUIRED+ votes) was found
        // or no clear winner was found but there are at least avg number of votes
        if(fFound || nTotalVotes >= (MNPAYMENTS_SIGNATURES_TOTAL + MNPAYMENTS_SIGNATURES_REQUIRED)/2) {
            // so just move to the next block
            ++it;
            continue;
        }
        // DEBUG
        DBG (
            // Let's see why this failed
            BOOST_FOREACH(CGoldminenodePayee& payee, it->second.vecPayees) {
                CTxDestination address1;
                ExtractDestination(payee.GetPayee(), address1);
                CBitcoinAddress address2(address1);
                printf("payee %s votes %d\n", address2.ToString().c_str(), payee.GetVoteCount());
            }
            printf("block %d votes total %d\n", it->first, nTotalVotes);
        )
        // END DEBUG
        // Low data block found, let's try to sync it
        uint256 hash;
        if(GetBlockHash(hash, it->first)) {
            vToFetch.push_back(CInv(MSG_GOLDMINENODE_PAYMENT_BLOCK, hash));
        }
        // We should not violate GETDATA rules
        if(vToFetch.size() == MAX_INV_SZ) {
            LogPrintf("CGoldminenodePayments::SyncLowDataPaymentBlocks -- asking peer %d for %d payment blocks\n", pnode->id, MAX_INV_SZ);
            connman.PushMessage(pnode, NetMsgType::GETDATA, vToFetch);
            // Start filling new batch
            vToFetch.clear();
        }
        ++it;
    }
    // Ask for the rest of it
    if(!vToFetch.empty()) {
        LogPrintf("CGoldminenodePayments::SyncLowDataPaymentBlocks -- asking peer %d for %d payment blocks\n", pnode->id, vToFetch.size());
        connman.PushMessage(pnode, NetMsgType::GETDATA, vToFetch);
    }
}

std::string CGoldminenodePayments::ToString() const
{
    std::ostringstream info;

    info << "Votes: " << (int)mapGoldminenodePaymentVotes.size() <<
            ", Blocks: " << (int)mapGoldminenodeBlocks.size();

    return info.str();
}

bool CGoldminenodePayments::IsEnoughData()
{
    float nAverageVotes = (MNPAYMENTS_SIGNATURES_TOTAL + MNPAYMENTS_SIGNATURES_REQUIRED) / 2;
    int nStorageLimit = GetStorageLimit();
    return GetBlockCount() > nStorageLimit && GetVoteCount() > nStorageLimit * nAverageVotes;
}

int CGoldminenodePayments::GetStorageLimit()
{
    return std::max(int(mnodeman.size() * nStorageCoeff), nMinBlocksToStore);
}

/**
*   Create Evolution Payments
*
*   - Create the correct payment structure for a given evolution
*/
void CGoldminenodePayments::CreateEvolution( CMutableTransaction& txNewRet, int nBlockHeight, CAmount blockEvolution, std::vector<CTxOut>& voutSuperblockRet )
{	
    // make sure it's empty, just in case
    voutSuperblockRet.clear();
			
	CScript payeeEvo;
	CPubKey pb;

	CBitcoinAddress address( evolutionManager.getEvolution(nBlockHeight)  );

	//--.
	payeeEvo = GetScriptForDestination( address.Get() );
	
	CTxOut txout = CTxOut(  blockEvolution, payeeEvo );
	
	txNewRet.vout.push_back( txout );
	
	voutSuperblockRet.push_back( txout );
}	

void CGoldminenodePayments::UpdatedBlockTip(const CBlockIndex *pindex, CConnman& connman)
{
    if(!pindex) return;

    nCachedBlockHeight = pindex->nHeight;
    LogPrint("mnpayments", "CGoldminenodePayments::UpdatedBlockTip -- nCachedBlockHeight=%d\n", nCachedBlockHeight);

    int nFutureBlock = nCachedBlockHeight + 10;

    CheckPreviousBlockVotes(nFutureBlock - 1);
    ProcessBlock(nFutureBlock, connman);
}
