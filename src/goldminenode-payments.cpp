// Copyright (c) 2017-2022 The Advanced Technology Coin
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activegoldminenode.h"
#include "consensus/validation.h"
#include "goldminenode-payments.h"
#include "goldminenode-sync.h"
#include "goldminenodeman.h"
#include "messagesigner.h"
#include "netfulfilledman.h"
#include "netmessagemaker.h"
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

bool IsBlockValueValid(const CBlock& block, int nBlockHeight, CAmount blockReward, std::string& strErrorRet)
{
    strErrorRet = "";

    bool isBlockRewardValueMet = (block.vtx[0]->GetValueOut() <= blockReward);
    if(fDebug) LogPrintf("block.vtx[0]->GetValueOut() %lld <= blockReward %lld\n", block.vtx[0]->GetValueOut(), blockReward);

    // we are still using budgets, but we have no data about them anymore,
    // all we know is predefined budget cycle and window

    const Consensus::Params& consensusParams = Params().GetConsensus();

    if(nBlockHeight < consensusParams.nSuperblockStartBlock) {
        int nOffset = nBlockHeight % consensusParams.nBudgetPaymentsCycleBlocks;
        if(nBlockHeight >= consensusParams.nBudgetPaymentsStartBlock &&
            nOffset < consensusParams.nBudgetPaymentsWindowBlocks) {
            // NOTE: old budget system is disabled since 12.1
            if(goldminenodeSync.IsSynced()) {
                // no old budget blocks should be accepted here on mainnet,
                // testnet/devnet/regtest should produce regular blocks only
                LogPrint("gobject", "IsBlockValueValid -- WARNING: Client synced but old budget system is disabled, checking block value against block reward\n");
                if(!isBlockRewardValueMet) {
                    strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, old budgets are disabled",
                                            nBlockHeight, block.vtx[0]->GetValueOut(), blockReward);
                }
                return isBlockRewardValueMet;
            }
            // when not synced, rely on online nodes (all networks)
            LogPrint("gobject", "IsBlockValueValid -- WARNING: Skipping old budget block value checks, accepting block\n");
            return true;
        }
        // LogPrint("gobject", "IsBlockValueValid -- Block is not in budget cycle window, checking block value against block reward\n");
        if(!isBlockRewardValueMet) {
            strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, block is not in old budget cycle window",
                                    nBlockHeight, block.vtx[0]->GetValueOut(), blockReward);
        }
        return isBlockRewardValueMet;
    }

    // superblocks started

    

    if(!goldminenodeSync.IsSynced() || fLiteMode) {
        // not enough data but at least it must NOT exceed superblock max value
        if(!isBlockRewardValueMet) {
            strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, only regular blocks are allowed at this height",
                                    nBlockHeight, block.vtx[0]->GetValueOut(), blockReward);
        }
        // it MUST be a regular block otherwise
        return isBlockRewardValueMet;
    }

    // we are synced, let's try to check as much data as we can

    if(sporkManager.IsSporkActive(SPORK_9_SUPERBLOCKS_ENABLED)) {
        LogPrint("gobject", "IsBlockValueValid -- No triggered superblock detected at height %d\n", nBlockHeight);
        if(!isBlockRewardValueMet) {
            strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, no triggered superblock detected",
                                    nBlockHeight, block.vtx[0]->GetValueOut(), blockReward);
        }
    } else {
        // should NOT allow superblocks at all, when superblocks are disabled
        LogPrint("gobject", "IsBlockValueValid -- Superblocks are disabled, no superblocks allowed\n");
        if(!isBlockRewardValueMet) {
            strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, superblocks are disabled",
                                    nBlockHeight, block.vtx[0]->GetValueOut(), blockReward);
        }
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

    // we are still using budgets, but we have no data about them anymore,
    // we can only check goldminenode payments

    const Consensus::Params& consensusParams = Params().GetConsensus();

    if(nBlockHeight < consensusParams.nSuperblockStartBlock) {
        // NOTE: old budget system is disabled since 12.1 and we should never enter this branch
        // anymore when sync is finished (on mainnet). We have no old budget data but these blocks
        // have tons of confirmations and can be safely accepted without payee verification
        LogPrint("gobject", "IsBlockPayeeValid -- WARNING: Client synced but old budget system is disabled, accepting any payee\n");
        return true;
    }

    // superblocks started
    // SEE IF THIS IS A VALID SUPERBLOCK

    if(sporkManager.IsSporkActive(SPORK_9_SUPERBLOCKS_ENABLED)) {
        LogPrint("gobject", "IsBlockPayeeValid -- No triggered superblock detected at height %d\n", nBlockHeight);
    } else {
        // should NOT allow superblocks at all, when superblocks are disabled
        LogPrint("gobject", "IsBlockPayeeValid -- Superblocks are disabled, no superblocks allowed\n");
    }

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
	if(sporkManager.IsSporkActive(SPORK_18_EVOLUTION_PAYMENTS)){
        LogPrintf("SPORK_18 create evolution %f blockRet before %d\n",blockEvolution,voutSuperblockRet.size());	
		CGoldminenodePayments::CreateEvolution(  txNew, nBlockHeight, blockEvolution, voutSuperblockRet  );
        LogPrintf("SPORK_18 create evolution blockRet after %d\n",voutSuperblockRet.size());	
    }		
	
    // FILL BLOCK PAYEE WITH GOLDMINENODE PAYMENT OTHERWISE
    mnpayments.FillBlockPayee(txNew, nBlockHeight, blockReward, txoutGoldminenodeRet);
    LogPrint("mnpayments", "FillBlockPayments -- nBlockHeight %d blockReward %lld txoutGoldminenodeRet %s txNew %s",
                            nBlockHeight, blockReward, txoutGoldminenodeRet.ToString(), txNew.ToString());
}

std::string GetRequiredPaymentsString(int nBlockHeight)
{
    // IF WE HAVE A ACTIVATED TRIGGER FOR THIS HEIGHT - IT IS A SUPERBLOCK, GET THE REQUIRED PAYEES
 
    // OTHERWISE, PAY GOLDMINENODE
    return mnpayments.GetRequiredPaymentsString(nBlockHeight);
}

void CGoldminenodePayments::Clear()
{
    LOCK2(cs_mapGoldminenodeBlocks, cs_mapGoldminenodePaymentVotes);
    mapGoldminenodeBlocks.clear();
    mapGoldminenodePaymentVotes.clear();
}

bool CGoldminenodePayments::UpdateLastVote(const CGoldminenodePaymentVote& vote)
{
    LOCK(cs_mapGoldminenodePaymentVotes);

    const auto it = mapGoldminenodesLastVote.find(vote.goldminenodeOutpoint);
    if (it != mapGoldminenodesLastVote.end()) {
        if (it->second == vote.nBlockHeight)
            return false;
        it->second = vote.nBlockHeight;
        return true;
    }

    //record this goldminenode voted
    mapGoldminenodesLastVote.emplace(vote.goldminenodeOutpoint, vote.nBlockHeight);
    return true;
}

/**
*   FillBlockPayee
*
*   Fill Goldminenode ONLY payment block
*/

void CGoldminenodePayments::FillBlockPayee(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CTxOut& txoutGoldminenodeRet) const
{
    // make sure it's not filled yet
    txoutGoldminenodeRet = CTxOut();

    CScript payee;

    if(!GetBlockPayee(nBlockHeight, payee)) {
        // no goldminenode detected...
        int nCount = 0;
        goldminenode_info_t mnInfo;
        if(!mnodeman.GetNextGoldminenodeInQueueForPayment(nBlockHeight, true, nCount, mnInfo)) {
            // ...and we can't calculate it on our own
            LogPrintf("CGoldminenodePayments::FillBlockPayee -- Failed to detect goldminenode to pay\n");
            return;
        }
        // fill payee with locally calculated winner and hope for the best
        payee = GetScriptForDestination(mnInfo.pubKeyCollateralAddress.GetID());
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

int CGoldminenodePayments::GetMinGoldminenodePaymentsProto() const {
    return sporkManager.IsSporkActive(SPORK_22_GOLDMINENODE_UPDATE_PROTO)
            ? MIN_GOLDMINENODE_PAYMENT_PROTO_VERSION_2
            : MIN_GOLDMINENODE_PAYMENT_PROTO_VERSION_1;
}

void CGoldminenodePayments::ProcessMessage(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv, CConnman& connman)
{
    if(fLiteMode) return; // disable all Arc specific functionality

    if (strCommand == NetMsgType::GOLDMINENODEPAYMENTSYNC) { //Goldminenode Payments Request Sync

        if(pfrom->nVersion < GetMinGoldminenodePaymentsProto()) {
            LogPrint("mnpayments", "GOLDMINENODEPAYMENTSYNC -- peer=%d using obsolete version %i\n", pfrom->id, pfrom->nVersion);
            connman.PushMessage(pfrom, CNetMsgMaker(pfrom->GetSendVersion()).Make(NetMsgType::REJECT, strCommand, REJECT_OBSOLETE,
                               strprintf("Version must be %d or greater", GetMinGoldminenodePaymentsProto())));
            return;
        }

        // Ignore such requests until we are fully synced.
        // We could start processing this after goldminenode list is synced
        // but this is a heavy one so it's better to finish sync first.
        if (!goldminenodeSync.IsSynced()) return;

        // DEPRECATED, should be removed on next protocol bump
        if(pfrom->nVersion == 70209) {
            int nCountNeeded;
            vRecv >> nCountNeeded;
        }

        if(netfulfilledman.HasFulfilledRequest(pfrom->addr, NetMsgType::GOLDMINENODEPAYMENTSYNC)) {
            LOCK(cs_main);
            // Asking for the payments list multiple times in a short period of time is no good
            LogPrintf("GOLDMINENODEPAYMENTSYNC -- peer already asked me for the list, peer=%d\n", pfrom->id);
            Misbehaving(pfrom->GetId(), 20);
            return;
        }
        netfulfilledman.AddFulfilledRequest(pfrom->addr, NetMsgType::GOLDMINENODEPAYMENTSYNC);

        Sync(pfrom, connman);
        LogPrintf("GOLDMINENODEPAYMENTSYNC -- Sent Goldminenode payment votes to peer=%d\n", pfrom->id);

    } else if (strCommand == NetMsgType::GOLDMINENODEPAYMENTVOTE) { // Goldminenode Payments Vote for the Winner

        CGoldminenodePaymentVote vote;
        vRecv >> vote;

        if(pfrom->nVersion < GetMinGoldminenodePaymentsProto()) {
            LogPrint("mnpayments", "GOLDMINENODEPAYMENTVOTE -- peer=%d using obsolete version %i\n", pfrom->id, pfrom->nVersion);
            connman.PushMessage(pfrom, CNetMsgMaker(pfrom->GetSendVersion()).Make(NetMsgType::REJECT, strCommand, REJECT_OBSOLETE,
                               strprintf("Version must be %d or greater", GetMinGoldminenodePaymentsProto())));
            return;
        }

        uint256 nHash = vote.GetHash();

        pfrom->setAskFor.erase(nHash);

        // TODO: clear setAskFor for MSG_GOLDMINENODE_PAYMENT_BLOCK too

        // Ignore any payments messages until goldminenode list is synced
        if(!goldminenodeSync.IsGoldminenodeListSynced()) return;

        {
            LOCK(cs_mapGoldminenodePaymentVotes);

            auto res = mapGoldminenodePaymentVotes.emplace(nHash, vote);

            // Avoid processing same vote multiple times if it was already verified earlier
            if(!res.second && res.first->second.IsVerified()) {
                LogPrint("mnpayments", "GOLDMINENODEPAYMENTVOTE -- hash=%s, nBlockHeight=%d/%d seen\n",
                            nHash.ToString(), vote.nBlockHeight, nCachedBlockHeight);
                return;
            }

            // Mark vote as non-verified when it's seen for the first time,
            // AddOrUpdatePaymentVote() below should take care of it if vote is actually ok
            res.first->second.MarkAsNotVerified();
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

        goldminenode_info_t mnInfo;
        if(!mnodeman.GetGoldminenodeInfo(vote.goldminenodeOutpoint, mnInfo)) {
            // mn was not found, so we can't check vote, some info is probably missing
            LogPrintf("GOLDMINENODEPAYMENTVOTE -- goldminenode is missing %s\n", vote.goldminenodeOutpoint.ToStringShort());
            mnodeman.AskForMN(pfrom, vote.goldminenodeOutpoint, connman);
            return;
        }

        int nDos = 0;
        if(!vote.CheckSignature(mnInfo.pubKeyGoldminenode, nCachedBlockHeight, nDos)) {
            if(nDos) {
                LOCK(cs_main);
                LogPrintf("GOLDMINENODEPAYMENTVOTE -- ERROR: invalid signature\n");
                Misbehaving(pfrom->GetId(), nDos);
            } else {
                // only warn about anything non-critical (i.e. nDos == 0) in debug mode
                LogPrint("mnpayments", "GOLDMINENODEPAYMENTVOTE -- WARNING: invalid signature\n");
            }
            // Either our info or vote info could be outdated.
            // In case our info is outdated, ask for an update,
            mnodeman.AskForMN(pfrom, vote.goldminenodeOutpoint, connman);
            // but there is nothing we can do if vote info itself is outdated
            // (i.e. it was signed by a mn which changed its key),
            // so just quit here.
            return;
        }

        if(!UpdateLastVote(vote)) {
            LogPrintf("GOLDMINENODEPAYMENTVOTE -- goldminenode already voted, goldminenode=%s\n", vote.goldminenodeOutpoint.ToStringShort());
            return;
        }

        CTxDestination address1;
        ExtractDestination(vote.payee, address1);
        CBitcoinAddress address2(address1);

        LogPrint("mnpayments", "GOLDMINENODEPAYMENTVOTE -- vote: address=%s, nBlockHeight=%d, nHeight=%d, prevout=%s, hash=%s new\n",
                    address2.ToString(), vote.nBlockHeight, nCachedBlockHeight, vote.goldminenodeOutpoint.ToStringShort(), nHash.ToString());

        if(AddOrUpdatePaymentVote(vote)){
            vote.Relay(connman);
            goldminenodeSync.BumpAssetLastTime("GOLDMINENODEPAYMENTVOTE");
        }
    }
}

uint256 CGoldminenodePaymentVote::GetHash() const
{
    // Note: doesn't match serialization

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << *(CScriptBase*)(&payee);
    ss << nBlockHeight;
    ss << goldminenodeOutpoint;
    return ss.GetHash();
}

uint256 CGoldminenodePaymentVote::GetSignatureHash() const
{
    return SerializeHash(*this);
}

bool CGoldminenodePaymentVote::Sign()
{
    std::string strError;

    if (sporkManager.IsSporkActive(SPORK_6_NEW_SIGS)) {
        uint256 hash = GetSignatureHash();

        if(!CHashSigner::SignHash(hash, activeGoldminenode.keyGoldminenode, vchSig)) {
            LogPrintf("CGoldminenodePaymentVote::Sign -- SignHash() failed\n");
            return false;
        }

        if (!CHashSigner::VerifyHash(hash, activeGoldminenode.pubKeyGoldminenode, vchSig, strError)) {
            LogPrintf("CGoldminenodePaymentVote::Sign -- VerifyHash() failed, error: %s\n", strError);
            return false;
        }
    } else {
        std::string strMessage = goldminenodeOutpoint.ToStringShort() +
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
    }

    return true;
}

bool CGoldminenodePayments::GetBlockPayee(int nBlockHeight, CScript& payeeRet) const
{
    LOCK(cs_mapGoldminenodeBlocks);

    auto it = mapGoldminenodeBlocks.find(nBlockHeight);
    return it != mapGoldminenodeBlocks.end() && it->second.GetBestPayee(payeeRet);
}

// Is this goldminenode scheduled to get paid soon?
// -- Only look ahead up to 8 blocks to allow for propagation of the latest 2 blocks of votes
bool CGoldminenodePayments::IsScheduled(const goldminenode_info_t& mnInfo, int nNotBlockHeight) const
{
    LOCK(cs_mapGoldminenodeBlocks);

    if(!goldminenodeSync.IsGoldminenodeListSynced()) return false;

    CScript mnpayee;
    mnpayee = GetScriptForDestination(mnInfo.pubKeyCollateralAddress.GetID());

    CScript payee;
    for(int64_t h = nCachedBlockHeight; h <= nCachedBlockHeight + 8; h++){
        if(h == nNotBlockHeight) continue;
        if(GetBlockPayee(h, payee) && mnpayee == payee) {
            return true;
        }
    }

    return false;
}

bool CGoldminenodePayments::AddOrUpdatePaymentVote(const CGoldminenodePaymentVote& vote)
{
    uint256 blockHash = uint256();
    if(!GetBlockHash(blockHash, vote.nBlockHeight - 101)) return false;

    uint256 nVoteHash = vote.GetHash();

    if(HasVerifiedPaymentVote(nVoteHash)) return false;

    LOCK2(cs_mapGoldminenodeBlocks, cs_mapGoldminenodePaymentVotes);

    mapGoldminenodePaymentVotes[nVoteHash] = vote;

    auto it = mapGoldminenodeBlocks.emplace(vote.nBlockHeight, CGoldminenodeBlockPayees(vote.nBlockHeight)).first;
    it->second.AddPayee(vote);

    LogPrint("mnpayments", "CGoldminenodePayments::AddOrUpdatePaymentVote -- added, hash=%s\n", nVoteHash.ToString());

    return true;
}

bool CGoldminenodePayments::HasVerifiedPaymentVote(const uint256& hashIn) const
{
    LOCK(cs_mapGoldminenodePaymentVotes);
    const auto it = mapGoldminenodePaymentVotes.find(hashIn);
    return it != mapGoldminenodePaymentVotes.end() && it->second.IsVerified();
}

void CGoldminenodeBlockPayees::AddPayee(const CGoldminenodePaymentVote& vote)
{
    LOCK(cs_vecPayees);

    uint256 nVoteHash = vote.GetHash();

    for (auto& payee : vecPayees) {
        if (payee.GetPayee() == vote.payee) {
            payee.AddVoteHash(nVoteHash);
            return;
        }
    }
    CGoldminenodePayee payeeNew(vote.payee, nVoteHash);
    vecPayees.push_back(payeeNew);
}

bool CGoldminenodeBlockPayees::GetBestPayee(CScript& payeeRet) const
{
    LOCK(cs_vecPayees);

    if(vecPayees.empty()) {
        LogPrint("mnpayments", "CGoldminenodeBlockPayees::GetBestPayee -- ERROR: couldn't find any payee\n");
        return false;
    }

    int nVotes = -1;
    for (const auto& payee : vecPayees) {
        if (payee.GetVoteCount() > nVotes) {
            payeeRet = payee.GetPayee();
            nVotes = payee.GetVoteCount();
        }
    }

    return (nVotes > -1);
}

bool CGoldminenodeBlockPayees::HasPayeeWithVotes(const CScript& payeeIn, int nVotesReq) const
{
    LOCK(cs_vecPayees);

    for (const auto& payee : vecPayees) {
        if (payee.GetVoteCount() >= nVotesReq && payee.GetPayee() == payeeIn) {
            return true;
        }
    }

    LogPrint("mnpayments", "CGoldminenodeBlockPayees::HasPayeeWithVotes -- ERROR: couldn't find any payee with %d+ votes\n", nVotesReq);
    return false;
}

bool CGoldminenodeBlockPayees::IsTransactionValid(const CTransaction& txNew) const
{
    LOCK(cs_vecPayees);

    int nMaxSignatures = 0;
	std::string strPayeesPossible = "";
	
	CAmount nGoldminenodePayment;
	if( sporkManager.IsSporkActive(SPORK_18_EVOLUTION_PAYMENTS) ){
		CScript payeeEvo;
        std::string addr = evolutionManager.getEvolution(nBlockHeight);
        if(!addr.empty())
        {
		    CBitcoinAddress address( evolutionManager.getEvolution(nBlockHeight) );
		    payeeEvo = GetScriptForDestination( address.Get() );
		    nGoldminenodePayment = GetGoldminenodePayment(nBlockHeight, txNew.GetValueOutWOEvol(payeeEvo));
        }
	}else{
		nGoldminenodePayment = GetGoldminenodePayment(nBlockHeight, txNew.GetValueOut());
	}
		
    //require at least MNPAYMENTS_SIGNATURES_REQUIRED signatures
	
   
	if(!sporkManager.IsSporkActive(SPORK_21_GOLDMINENODE_ORDER_ENABLE)) 
	{
		for (const auto& payee : vecPayees) 
		{
	        if (payee.GetVoteCount() >= nMaxSignatures)
	            nMaxSignatures = payee.GetVoteCount();
    	}
	}
    // if we don't have at least MNPAYMENTS_SIGNATURES_REQUIRED signatures on a payee, approve whichever is the longest chain
    if(nMaxSignatures < MNPAYMENTS_SIGNATURES_REQUIRED) return true;

    for (const auto& payee : vecPayees) {
        if (payee.GetVoteCount() >= MNPAYMENTS_SIGNATURES_REQUIRED) {
            for (const auto& txout : txNew.vout) {
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

std::string CGoldminenodeBlockPayees::GetRequiredPaymentsString() const
{
    LOCK(cs_vecPayees);

    std::string strRequiredPayments = "";

    for (const auto& payee : vecPayees)
    {
        CTxDestination address1;
        ExtractDestination(payee.GetPayee(), address1);
        CBitcoinAddress address2(address1);

        if (!strRequiredPayments.empty())
            strRequiredPayments += ", ";

        strRequiredPayments += strprintf("%s:%d", address2.ToString(), payee.GetVoteCount());
    }

    if (strRequiredPayments.empty())
        return "Unknown";

    return strRequiredPayments;
}

std::string CGoldminenodePayments::GetRequiredPaymentsString(int nBlockHeight) const
{
    LOCK(cs_mapGoldminenodeBlocks);

    const auto it = mapGoldminenodeBlocks.find(nBlockHeight);
    return it == mapGoldminenodeBlocks.end() ? "Unknown" : it->second.GetRequiredPaymentsString();
}

bool CGoldminenodePayments::IsTransactionValid(const CTransaction& txNew, int nBlockHeight) const
{
    LOCK(cs_mapGoldminenodeBlocks);

    const auto it = mapGoldminenodeBlocks.find(nBlockHeight);
    return it == mapGoldminenodeBlocks.end() ? true : it->second.IsTransactionValid(txNew);
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

bool CGoldminenodePaymentVote::IsValid(CNode* pnode, int nValidationHeight, std::string& strError, CConnman& connman) const
{
    goldminenode_info_t mnInfo;

    if(!mnodeman.GetGoldminenodeInfo(goldminenodeOutpoint, mnInfo)) {
        strError = strprintf("Unknown goldminenode=%s", goldminenodeOutpoint.ToStringShort());
        // Only ask if we are already synced and still have no idea about that Goldminenode
        if(goldminenodeSync.IsGoldminenodeListSynced()) {
            mnodeman.AskForMN(pnode, goldminenodeOutpoint, connman);
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
    if(!fGoldminenodeMode && nBlockHeight < nValidationHeight) return true;


    return true;
}

bool CGoldminenodePayments::ProcessBlock(int nBlockHeight, CConnman& connman)
{
    // DETERMINE IF WE SHOULD BE VOTING FOR THE NEXT PAYEE

    if(fLiteMode || !fGoldminenodeMode) return false;

    // We have little chances to pick the right winner if winners list is out of sync
    // but we have no choice, so we'll try. However it doesn't make sense to even try to do so
    // if we have not enough data about goldminenodes.
    if(!goldminenodeSync.IsGoldminenodeListSynced()) return false;

    int nRank;

    if (!mnodeman.GetGoldminenodeRank(activeGoldminenode.outpoint, nRank, nBlockHeight - 101, GetMinGoldminenodePaymentsProto())) {
        LogPrint("mnpayments", "CGoldminenodePayments::ProcessBlock -- Unknown Goldminenode\n");
        return false;
    }

    if (nRank > MNPAYMENTS_SIGNATURES_TOTAL) {
        LogPrint("mnpayments", "CGoldminenodePayments::ProcessBlock -- Goldminenode not in the top %d (%d)\n", MNPAYMENTS_SIGNATURES_TOTAL, nRank);
        return false;
    }


    // LOCATE THE NEXT GOLDMINENODE WHICH SHOULD BE PAID

    LogPrintf("CGoldminenodePayments::ProcessBlock -- Start: nBlockHeight=%d, goldminenode=%s\n", nBlockHeight, activeGoldminenode.outpoint.ToStringShort());

    // pay to the oldest MN that still had no payment but its input is old enough and it was active long enough
    int nCount = 0;
    goldminenode_info_t mnInfo;

    if (!mnodeman.GetNextGoldminenodeInQueueForPayment(nBlockHeight, true, nCount, mnInfo)) {
        LogPrintf("CGoldminenodePayments::ProcessBlock -- ERROR: Failed to find goldminenode to pay\n");
        return false;
    }

    LogPrintf("CGoldminenodePayments::ProcessBlock -- Goldminenode found by GetNextGoldminenodeInQueueForPayment(): %s\n", mnInfo.outpoint.ToStringShort());


    CScript payee = GetScriptForDestination(mnInfo.pubKeyCollateralAddress.GetID());

    CGoldminenodePaymentVote voteNew(activeGoldminenode.outpoint, nBlockHeight, payee);

    CTxDestination address1;
    ExtractDestination(payee, address1);
    CBitcoinAddress address2(address1);

    LogPrintf("CGoldminenodePayments::ProcessBlock -- vote: payee=%s, nBlockHeight=%d\n", address2.ToString(), nBlockHeight);

    // SIGN MESSAGE TO NETWORK WITH OUR GOLDMINENODE KEYS

    LogPrintf("CGoldminenodePayments::ProcessBlock -- Signing vote\n");
    if (voteNew.Sign()) {
        LogPrintf("CGoldminenodePayments::ProcessBlock -- AddOrUpdatePaymentVote()\n");

        if (AddOrUpdatePaymentVote(voteNew)) {
            voteNew.Relay(connman);
            return true;
        }
    }

    return false;
}

void CGoldminenodePayments::CheckBlockVotes(int nBlockHeight)
{
    if (!goldminenodeSync.IsWinnersListSynced()) return;

    CGoldminenodeMan::rank_pair_vec_t mns;
    if (!mnodeman.GetGoldminenodeRanks(mns, nBlockHeight - 101, GetMinGoldminenodePaymentsProto())) {
        LogPrintf("CGoldminenodePayments::CheckBlockVotes -- nBlockHeight=%d, GetGoldminenodeRanks failed\n", nBlockHeight);
        return;
    }

    std::string debugStr;

    debugStr += strprintf("CGoldminenodePayments::CheckBlockVotes -- nBlockHeight=%d,\n  Expected voting MNs:\n", nBlockHeight);

    LOCK2(cs_mapGoldminenodeBlocks, cs_mapGoldminenodePaymentVotes);

    int i{0};
    for (const auto& mn : mns) {
        CScript payee;
        bool found = false;

        const auto it = mapGoldminenodeBlocks.find(nBlockHeight);
        if (it != mapGoldminenodeBlocks.end()) {
            for (const auto& p : it->second.vecPayees) {
                for (const auto& voteHash : p.GetVoteHashes()) {
                    const auto itVote = mapGoldminenodePaymentVotes.find(voteHash);
                    if (itVote == mapGoldminenodePaymentVotes.end()) {
                        debugStr += strprintf("    - could not find vote %s\n",
                                              voteHash.ToString());
                        continue;
                    }
                    if (itVote->second.goldminenodeOutpoint == mn.second.outpoint) {
                        payee = itVote->second.payee;
                        found = true;
                        break;
                    }
                }
            }
        }

        if (found) {
            CTxDestination address1;
            ExtractDestination(payee, address1);
            CBitcoinAddress address2(address1);

            debugStr += strprintf("    - %s - voted for %s\n",
                                  mn.second.outpoint.ToStringShort(), address2.ToString());
        } else {
            mapGoldminenodesDidNotVote.emplace(mn.second.outpoint, 0).first->second++;

            debugStr += strprintf("    - %s - no vote received\n",
                                  mn.second.outpoint.ToStringShort());
        }

        if (++i >= MNPAYMENTS_SIGNATURES_TOTAL) break;
    }

    if (mapGoldminenodesDidNotVote.empty()) {
        LogPrint("mnpayments", "%s", debugStr);
        return;
    }

    debugStr += "  Goldminenodes which missed a vote in the past:\n";
    for (const auto& item : mapGoldminenodesDidNotVote) {
        debugStr += strprintf("    - %s: %d\n", item.first.ToStringShort(), item.second);
    }

    LogPrint("mnpayments", "%s", debugStr);
}

void CGoldminenodePaymentVote::Relay(CConnman& connman) const
{
    // Do not relay until fully synced
    if(!goldminenodeSync.IsSynced()) {
        LogPrint("mnpayments", "CGoldminenodePayments::Relay -- won't relay until fully synced\n");
        return;
    }

    CInv inv(MSG_GOLDMINENODE_PAYMENT_VOTE, GetHash());
    connman.RelayInv(inv);
}

bool CGoldminenodePaymentVote::CheckSignature(const CPubKey& pubKeyGoldminenode, int nValidationHeight, int &nDos) const
{
    // do not ban by default
    nDos = 0;
    std::string strError = "";

    if (sporkManager.IsSporkActive(SPORK_6_NEW_SIGS)) {
        uint256 hash = GetSignatureHash();

        if (!CHashSigner::VerifyHash(hash, pubKeyGoldminenode, vchSig, strError)) {
            // could be a signature in old format
            std::string strMessage = goldminenodeOutpoint.ToStringShort() +
                        boost::lexical_cast<std::string>(nBlockHeight) +
                        ScriptToAsmStr(payee);
            if(!CMessageSigner::VerifyMessage(pubKeyGoldminenode, vchSig, strMessage, strError)) {
                // nope, not in old format either
                // Only ban for future block vote when we are already synced.
                // Otherwise it could be the case when MN which signed this vote is using another key now
                // and we have no idea about the old one.
                if(goldminenodeSync.IsGoldminenodeListSynced() && nBlockHeight > nValidationHeight) {
                    nDos = 20;
                }
                return error("CGoldminenodePaymentVote::CheckSignature -- Got bad Goldminenode payment signature, goldminenode=%s, error: %s",
                            goldminenodeOutpoint.ToStringShort(), strError);
            }
        }
    } else {
        std::string strMessage = goldminenodeOutpoint.ToStringShort() +
                    boost::lexical_cast<std::string>(nBlockHeight) +
                    ScriptToAsmStr(payee);

        if (!CMessageSigner::VerifyMessage(pubKeyGoldminenode, vchSig, strMessage, strError)) {
            // Only ban for future block vote when we are already synced.
            // Otherwise it could be the case when MN which signed this vote is using another key now
            // and we have no idea about the old one.
            if(goldminenodeSync.IsGoldminenodeListSynced() && nBlockHeight > nValidationHeight) {
                nDos = 20;
            }
            return error("CGoldminenodePaymentVote::CheckSignature -- Got bad Goldminenode payment signature, goldminenode=%s, error: %s",
                        goldminenodeOutpoint.ToStringShort(), strError);
        }
    }

    return true;
}

std::string CGoldminenodePaymentVote::ToString() const
{
    std::ostringstream info;

    info << goldminenodeOutpoint.ToStringShort() <<
            ", " << nBlockHeight <<
            ", " << ScriptToAsmStr(payee) <<
            ", " << (int)vchSig.size();

    return info.str();
}

// Send only votes for future blocks, node should request every other missing payment block individually
void CGoldminenodePayments::Sync(CNode* pnode, CConnman& connman) const
{
    LOCK(cs_mapGoldminenodeBlocks);

    if(!goldminenodeSync.IsWinnersListSynced()) return;

    int nInvCount = 0;

    for(int h = nCachedBlockHeight; h < nCachedBlockHeight + 20; h++) {
        const auto it = mapGoldminenodeBlocks.find(h);
        if(it != mapGoldminenodeBlocks.end()) {
            for (const auto& payee : it->second.vecPayees) {
                std::vector<uint256> vecVoteHashes = payee.GetVoteHashes();
                for (const auto& hash : vecVoteHashes) {
                    if(!HasVerifiedPaymentVote(hash)) continue;
                    pnode->PushInventory(CInv(MSG_GOLDMINENODE_PAYMENT_VOTE, hash));
                    nInvCount++;
                }
            }
        }
    }

    LogPrintf("CGoldminenodePayments::Sync -- Sent %d votes to peer=%d\n", nInvCount, pnode->id);
    CNetMsgMaker msgMaker(pnode->GetSendVersion());
    connman.PushMessage(pnode, msgMaker.Make(NetMsgType::SYNCSTATUSCOUNT, GOLDMINENODE_SYNC_MNW, nInvCount));
}

// Request low data/unknown payment blocks in batches directly from some node instead of/after preliminary Sync.
void CGoldminenodePayments::RequestLowDataPaymentBlocks(CNode* pnode, CConnman& connman) const
{
    if(!goldminenodeSync.IsGoldminenodeListSynced()) return;

    CNetMsgMaker msgMaker(pnode->GetSendVersion());
    LOCK2(cs_main, cs_mapGoldminenodeBlocks);

    std::vector<CInv> vToFetch;
    int nLimit = GetStorageLimit();

    const CBlockIndex *pindex = chainActive.Tip();

    while(nCachedBlockHeight - pindex->nHeight < nLimit) {
        const auto it = mapGoldminenodeBlocks.find(pindex->nHeight);
        if(it == mapGoldminenodeBlocks.end()) {
            // We have no idea about this block height, let's ask
            vToFetch.push_back(CInv(MSG_GOLDMINENODE_PAYMENT_BLOCK, pindex->GetBlockHash()));
            // We should not violate GETDATA rules
            if(vToFetch.size() == MAX_INV_SZ) {
                LogPrintf("CGoldminenodePayments::RequestLowDataPaymentBlocks -- asking peer=%d for %d blocks\n", pnode->id, MAX_INV_SZ);
                connman.PushMessage(pnode, msgMaker.Make(NetMsgType::GETDATA, vToFetch));
                // Start filling new batch
                vToFetch.clear();
            }
        }
        if(!pindex->pprev) break;
        pindex = pindex->pprev;
    }

    auto it = mapGoldminenodeBlocks.begin();

    while(it != mapGoldminenodeBlocks.end()) {
        int nTotalVotes = 0;
        bool fFound = false;
        for (const auto& payee : it->second.vecPayees) {
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
            for (const auto& payee : it->second.vecPayees) {
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
            LogPrintf("CGoldminenodePayments::RequestLowDataPaymentBlocks -- asking peer=%d for %d payment blocks\n", pnode->id, MAX_INV_SZ);
            connman.PushMessage(pnode, msgMaker.Make(NetMsgType::GETDATA, vToFetch));
            // Start filling new batch
            vToFetch.clear();
        }
        ++it;
    }
    // Ask for the rest of it
    if(!vToFetch.empty()) {
        LogPrintf("CGoldminenodePayments::RequestLowDataPaymentBlocks -- asking peer=%d for %d payment blocks\n", pnode->id, vToFetch.size());
        connman.PushMessage(pnode, msgMaker.Make(NetMsgType::GETDATA, vToFetch));
    }
}

std::string CGoldminenodePayments::ToString() const
{
    std::ostringstream info;

    info << "Votes: " << (int)mapGoldminenodePaymentVotes.size() <<
            ", Blocks: " << (int)mapGoldminenodeBlocks.size();

    return info.str();
}

bool CGoldminenodePayments::IsEnoughData() const
{
    float nAverageVotes = (MNPAYMENTS_SIGNATURES_TOTAL + MNPAYMENTS_SIGNATURES_REQUIRED) / 2;
    int nStorageLimit = GetStorageLimit();
    return GetBlockCount() > nStorageLimit && GetVoteCount() > nStorageLimit * nAverageVotes;
}

int CGoldminenodePayments::GetStorageLimit() const
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
	std::string addr = evolutionManager.getEvolution(nBlockHeight);
    LogPrintf("CreateEvolution for node %s\n",addr.c_str());
    if(addr.empty()) // if node is empty then going out
        return;
    
	CScript payeeEvo;
	CPubKey pb;

	CBitcoinAddress address(addr);

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

    CheckBlockVotes(nFutureBlock - 1);
    ProcessBlock(nFutureBlock, connman);
}
