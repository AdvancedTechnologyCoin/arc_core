// Copyright (c) 2015-2017 The Arctic Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activegoldminenode.h"
#include "spysend.h"
#include "governance-classes.h"
#include "goldminenode-payments.h"
#include "goldminenode-sync.h"
#include "goldminenodeman.h"
#include "netfulfilledman.h"
#include "spork.h"
#include "util.h"

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
*   - In Arctic some blocks are superblocks, which output much higher amounts of coins
*   - Otherblocks are 10% lower in outgoing value, so in total, no extra coins are created
*   - When non-superblocks are detected, the normal schedule should be maintained
*/

bool IsBlockValueValid(const CBlock& block, int nBlockHeight, CAmount blockReward, std::string &strErrorRet)
{
    strErrorRet = "";

    bool isBlockRewardValueMet = (block.vtx[0].GetValueOut() <= blockReward);
    if(fDebug) LogPrintf("block.vtx[0].GetValueOut() %lld <= blockReward %lld\n", block.vtx[0].GetValueOut(), blockReward);

    // we are still using budgets, but we have no data about them anymore,
    // all we know is predefined budget cycle and window

    const Consensus::Params& consensusParams = Params().GetConsensus();

    if(nBlockHeight < consensusParams.nSuperblockStartBlock) {
        int nOffset = nBlockHeight % consensusParams.nEvolutionPaymentsCycleBlocks;
        if(nBlockHeight >= consensusParams.nEvolutionPaymentsStartBlock &&
            nOffset < consensusParams.nEvolutionPaymentsWindowBlocks) {
            // NOTE: make sure SPORK_13_OLD_SUPERBLOCK_FLAG is disabled when 12.1 starts to go live
            if(goldminenodeSync.IsSynced() && !sporkManager.IsSporkActive(SPORK_13_OLD_SUPERBLOCK_FLAG)) {
                // no budget blocks should be accepted here, if SPORK_13_OLD_SUPERBLOCK_FLAG is disabled
                LogPrint("gobject", "IsBlockValueValid -- Client synced but budget spork is disabled, checking block value against block reward\n");
                if(!isBlockRewardValueMet) {
                    strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, budgets are disabled",
                                            nBlockHeight, block.vtx[0].GetValueOut(), blockReward);
                }
                return isBlockRewardValueMet;
            }
            LogPrint("gobject", "IsBlockValueValid -- WARNING: Skipping budget block value checks, accepting block\n");
            // TODO: reprocess blocks to make sure they are legit?
            return true;
        }
        // LogPrint("gobject", "IsBlockValueValid -- Block is not in budget cycle window, checking block value against block reward\n");
        if(!isBlockRewardValueMet) {
            strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, block is not in budget cycle window",
                                    nBlockHeight, block.vtx[0].GetValueOut(), blockReward);
        }
        return isBlockRewardValueMet;
    }

    // superblocks started

    CAmount nSuperblockMaxValue =  blockReward + CSuperblock::GetPaymentsLimit(nBlockHeight);
    bool isSuperblockMaxValueMet = (block.vtx[0].GetValueOut() <= nSuperblockMaxValue);

    LogPrint("gobject", "block.vtx[0].GetValueOut() %lld <= nSuperblockMaxValue %lld\n", block.vtx[0].GetValueOut(), nSuperblockMaxValue);

    if(!goldminenodeSync.IsSynced()) {
        // not enough data but at least it must NOT exceed superblock max value
        if(CSuperblock::IsValidBlockHeight(nBlockHeight)) {
            if(fDebug) LogPrintf("IsBlockPayeeValid -- WARNING: Client not synced, checking superblock max bounds only\n");
            if(!isSuperblockMaxValueMet) {
                strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded superblock max value",
                                        nBlockHeight, block.vtx[0].GetValueOut(), nSuperblockMaxValue);
            }
            return isSuperblockMaxValueMet;
        }
        if(!isBlockRewardValueMet) {
            strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, only regular blocks are allowed at this height",
                                    nBlockHeight, block.vtx[0].GetValueOut(), blockReward);
        }
        // it MUST be a regular block otherwise
        return isBlockRewardValueMet;
    }

    // we are synced, let's try to check as much data as we can

    if(sporkManager.IsSporkActive(SPORK_9_SUPERBLOCKS_ENABLED)) {
        if(CSuperblockManager::IsSuperblockTriggered(nBlockHeight)) {
            if(CSuperblockManager::IsValid(block.vtx[0], nBlockHeight, blockReward)) {
                LogPrint("gobject", "IsBlockValueValid -- Valid superblock at height %d: %s", nBlockHeight, block.vtx[0].ToString());
                // all checks are done in CSuperblock::IsValid, nothing to do here
                return true;
            }

            // triggered but invalid? that's weird
            LogPrintf("IsBlockValueValid -- ERROR: Invalid superblock detected at height %d: %s", nBlockHeight, block.vtx[0].ToString());
            // should NOT allow invalid superblocks, when superblocks are enabled
            strErrorRet = strprintf("invalid superblock detected at height %d", nBlockHeight);
            return false;
        }
        LogPrint("gobject", "IsBlockValueValid -- No triggered superblock detected at height %d\n", nBlockHeight);
        if(!isBlockRewardValueMet) {
            strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, no triggered superblock detected",
                                    nBlockHeight, block.vtx[0].GetValueOut(), blockReward);
        }
    } else {
        // should NOT allow superblocks at all, when superblocks are disabled
        LogPrint("gobject", "IsBlockValueValid -- Superblocks are disabled, no superblocks allowed\n");
        if(!isBlockRewardValueMet) {
            strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, superblocks are disabled",
                                    nBlockHeight, block.vtx[0].GetValueOut(), blockReward);
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
        if(mnpayments.IsTransactionValid(txNew, nBlockHeight)) {
            LogPrint("mnpayments", "IsBlockPayeeValid -- Valid goldminenode payment at height %d: %s", nBlockHeight, txNew.ToString());
            return true;
        }

        int nOffset = nBlockHeight % consensusParams.nEvolutionPaymentsCycleBlocks;
        if(nBlockHeight >= consensusParams.nEvolutionPaymentsStartBlock &&
            nOffset < consensusParams.nEvolutionPaymentsWindowBlocks) {
            if(!sporkManager.IsSporkActive(SPORK_13_OLD_SUPERBLOCK_FLAG)) {
                // no budget blocks should be accepted here, if SPORK_13_OLD_SUPERBLOCK_FLAG is disabled
                LogPrint("gobject", "IsBlockPayeeValid -- ERROR: Client synced but budget spork is disabled and goldminenode payment is invalid\n");
                return false;
            }
            // NOTE: this should never happen in real, SPORK_13_OLD_SUPERBLOCK_FLAG MUST be disabled when 12.1 starts to go live
            LogPrint("gobject", "IsBlockPayeeValid -- WARNING: Probably valid budget block, have no data, accepting\n");
            // TODO: reprocess blocks to make sure they are legit?
            return true;
        }

        if(sporkManager.IsSporkActive(SPORK_8_GOLDMINENODE_PAYMENT_ENFORCEMENT)) {
            LogPrintf("IsBlockPayeeValid -- ERROR: Invalid goldminenode payment detected at height %d: %s", nBlockHeight, txNew.ToString());
            return false;
        }

        LogPrintf("IsBlockPayeeValid -- WARNING: Goldminenode payment enforcement is disabled, accepting any payee\n");
        return true;
    }

    // superblocks started
    // SEE IF THIS IS A VALID SUPERBLOCK

    if(sporkManager.IsSporkActive(SPORK_9_SUPERBLOCKS_ENABLED)) {
        if(CSuperblockManager::IsSuperblockTriggered(nBlockHeight)) {
            if(CSuperblockManager::IsValid(txNew, nBlockHeight, blockReward)) {
                LogPrint("gobject", "IsBlockPayeeValid -- Valid superblock at height %d: %s", nBlockHeight, txNew.ToString());
                return true;
            }

            LogPrintf("IsBlockPayeeValid -- ERROR: Invalid superblock detected at height %d: %s", nBlockHeight, txNew.ToString());
            // should NOT allow such superblocks, when superblocks are enabled
            return false;
        }
        // continue validation, should pay MN
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

void FillBlockPayments(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CTxOut& txoutGoldminenodeRet, std::vector<CTxOut>& voutSuperblockRet)
{
    // only create superblocks if spork is enabled AND if superblock is actually triggered
    // (height should be validated inside)
    if(sporkManager.IsSporkActive(SPORK_9_SUPERBLOCKS_ENABLED) &&
        CSuperblockManager::IsSuperblockTriggered(nBlockHeight)) {
            LogPrint("gobject", "FillBlockPayments -- triggered superblock creation at height %d\n", nBlockHeight);
            CSuperblockManager::CreateSuperblock(txNew, nBlockHeight, voutSuperblockRet);
            return;
    }

    // FILL BLOCK PAYEE WITH GOLDMINENODE PAYMENT OTHERWISE
    mnpayments.FillBlockPayee(txNew, nBlockHeight, blockReward, txoutGoldminenodeRet);
    LogPrint("mnpayments", "FillBlockPayments -- nBlockHeight %d blockReward %lld txoutGoldminenodeRet %s txNew %s",
                            nBlockHeight, blockReward, txoutGoldminenodeRet.ToString(), txNew.ToString());
}

std::string GetRequiredPaymentsString(int nBlockHeight)
{
    // IF WE HAVE A ACTIVATED TRIGGER FOR THIS HEIGHT - IT IS A SUPERBLOCK, GET THE REQUIRED PAYEES
    if(CSuperblockManager::IsSuperblockTriggered(nBlockHeight)) {
        return CSuperblockManager::GetRequiredPaymentsString(nBlockHeight);
    }

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

    if(!mnpayments.GetBlockPayee(nBlockHeight, payee)) {
        // no goldminenode detected...
        int nCount = 0;
        CGoldminenode *winningNode = mnodeman.GetNextGoldminenodeInQueueForPayment(nBlockHeight, true, nCount);
        if(!winningNode) {
            // ...and we can't calculate it on our own
            LogPrintf("CGoldminenodePayments::FillBlockPayee -- Failed to detect goldminenode to pay\n");
            return;
        }
        // fill payee with locally calculated winner and hope for the best
        payee = GetScriptForDestination(winningNode->pubKeyCollateralAddress.GetID());
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

void CGoldminenodePayments::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    // Ignore any payments messages until goldminenode list is synced
    if(!goldminenodeSync.IsGoldminenodeListSynced()) return;

    if(fLiteMode) return; // disable all Arctic specific functionality

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

        Sync(pfrom, nCountNeeded);
        LogPrintf("GOLDMINENODEPAYMENTSYNC -- Sent Goldminenode payment votes to peer %d\n", pfrom->id);

    } else if (strCommand == NetMsgType::GOLDMINENODEPAYMENTVOTE) { // Goldminenode Payments Vote for the Winner

        CGoldminenodePaymentVote vote;
        vRecv >> vote;

        if(pfrom->nVersion < GetMinGoldminenodePaymentsProto()) return;

        if(!pCurrentBlockIndex) return;

        uint256 nHash = vote.GetHash();

        pfrom->setAskFor.erase(nHash);

        {
            LOCK(cs_mapGoldminenodePaymentVotes);
            if(mapGoldminenodePaymentVotes.count(nHash)) {
                LogPrint("mnpayments", "GOLDMINENODEPAYMENTVOTE -- hash=%s, nHeight=%d seen\n", nHash.ToString(), pCurrentBlockIndex->nHeight);
                return;
            }

            // Avoid processing same vote multiple times
            mapGoldminenodePaymentVotes[nHash] = vote;
            // but first mark vote as non-verified,
            // AddPaymentVote() below should take care of it if vote is actually ok
            mapGoldminenodePaymentVotes[nHash].MarkAsNotVerified();
        }

        int nFirstBlock = pCurrentBlockIndex->nHeight - GetStorageLimit();
        if(vote.nBlockHeight < nFirstBlock || vote.nBlockHeight > pCurrentBlockIndex->nHeight+20) {
            LogPrint("mnpayments", "GOLDMINENODEPAYMENTVOTE -- vote out of range: nFirstBlock=%d, nBlockHeight=%d, nHeight=%d\n", nFirstBlock, vote.nBlockHeight, pCurrentBlockIndex->nHeight);
            return;
        }

        std::string strError = "";
        if(!vote.IsValid(pfrom, pCurrentBlockIndex->nHeight, strError)) {
            LogPrint("mnpayments", "GOLDMINENODEPAYMENTVOTE -- invalid message, error: %s\n", strError);
            return;
        }

        if(!CanVote(vote.vinGoldminenode.prevout, vote.nBlockHeight)) {
            LogPrintf("GOLDMINENODEPAYMENTVOTE -- goldminenode already voted, goldminenode=%s\n", vote.vinGoldminenode.prevout.ToStringShort());
            return;
        }

        goldminenode_info_t mnInfo = mnodeman.GetGoldminenodeInfo(vote.vinGoldminenode);
        if(!mnInfo.fInfoValid) {
            // mn was not found, so we can't check vote, some info is probably missing
            LogPrintf("GOLDMINENODEPAYMENTVOTE -- goldminenode is missing %s\n", vote.vinGoldminenode.prevout.ToStringShort());
            mnodeman.AskForMN(pfrom, vote.vinGoldminenode);
            return;
        }

        int nDos = 0;
        if(!vote.CheckSignature(mnInfo.pubKeyGoldminenode, pCurrentBlockIndex->nHeight, nDos)) {
            if(nDos) {
                LogPrintf("GOLDMINENODEPAYMENTVOTE -- ERROR: invalid signature\n");
                Misbehaving(pfrom->GetId(), nDos);
            } else {
                // only warn about anything non-critical (i.e. nDos == 0) in debug mode
                LogPrint("mnpayments", "GOLDMINENODEPAYMENTVOTE -- WARNING: invalid signature\n");
            }
            // Either our info or vote info could be outdated.
            // In case our info is outdated, ask for an update,
            mnodeman.AskForMN(pfrom, vote.vinGoldminenode);
            // but there is nothing we can do if vote info itself is outdated
            // (i.e. it was signed by a mn which changed its key),
            // so just quit here.
            return;
        }

        CTxDestination address1;
        ExtractDestination(vote.payee, address1);
        CBitcoinAddress address2(address1);

        LogPrint("mnpayments", "GOLDMINENODEPAYMENTVOTE -- vote: address=%s, nBlockHeight=%d, nHeight=%d, prevout=%s\n", address2.ToString(), vote.nBlockHeight, pCurrentBlockIndex->nHeight, vote.vinGoldminenode.prevout.ToStringShort());

        if(AddPaymentVote(vote)){
            vote.Relay();
            goldminenodeSync.AddedPaymentVote();
        }
    }
}

bool CGoldminenodePaymentVote::Sign()
{
    std::string strError;
    std::string strMessage = vinGoldminenode.prevout.ToStringShort() +
                boost::lexical_cast<std::string>(nBlockHeight) +
                ScriptToAsmStr(payee);

    if(!darkSendSigner.SignMessage(strMessage, vchSig, activeGoldminenode.keyGoldminenode)) {
        LogPrintf("CGoldminenodePaymentVote::Sign -- SignMessage() failed\n");
        return false;
    }

    if(!darkSendSigner.VerifyMessage(activeGoldminenode.pubKeyGoldminenode, vchSig, strMessage, strError)) {
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

    if(!pCurrentBlockIndex) return false;

    CScript mnpayee;
    mnpayee = GetScriptForDestination(mn.pubKeyCollateralAddress.GetID());

    CScript payee;
    for(int64_t h = pCurrentBlockIndex->nHeight; h <= pCurrentBlockIndex->nHeight + 8; h++){
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
    BOOST_FOREACH(CGoldminenodePayee& payee, vecPayees) {
        if (payee.GetVoteCount() > nVotes) {
            payeeRet = payee.GetPayee();
            nVotes = payee.GetVoteCount();
        }
    }

    return (nVotes > -1);
}

bool CGoldminenodeBlockPayees::HasPayeeWithVotes(CScript payeeIn, int nVotesReq)
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

    CAmount nGoldminenodePayment = GetGoldminenodePayment(nBlockHeight, txNew.GetValueOut());

    //require at least MNPAYMENTS_SIGNATURES_REQUIRED signatures

    BOOST_FOREACH(CGoldminenodePayee& payee, vecPayees) {
        if (payee.GetVoteCount() >= nMaxSignatures) {
            nMaxSignatures = payee.GetVoteCount();
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
    if(!pCurrentBlockIndex) return;

    LOCK2(cs_mapGoldminenodeBlocks, cs_mapGoldminenodePaymentVotes);

    int nLimit = GetStorageLimit();

    std::map<uint256, CGoldminenodePaymentVote>::iterator it = mapGoldminenodePaymentVotes.begin();
    while(it != mapGoldminenodePaymentVotes.end()) {
        CGoldminenodePaymentVote vote = (*it).second;

        if(pCurrentBlockIndex->nHeight - vote.nBlockHeight > nLimit) {
            LogPrint("mnpayments", "CGoldminenodePayments::CheckAndRemove -- Removing old Goldminenode payment: nBlockHeight=%d\n", vote.nBlockHeight);
            mapGoldminenodePaymentVotes.erase(it++);
            mapGoldminenodeBlocks.erase(vote.nBlockHeight);
        } else {
            ++it;
        }
    }
    LogPrintf("CGoldminenodePayments::CheckAndRemove -- %s\n", ToString());
}

bool CGoldminenodePaymentVote::IsValid(CNode* pnode, int nValidationHeight, std::string& strError)
{
    CGoldminenode* pmn = mnodeman.Find(vinGoldminenode);

    if(!pmn) {
        strError = strprintf("Unknown Goldminenode: prevout=%s", vinGoldminenode.prevout.ToStringShort());
        // Only ask if we are already synced and still have no idea about that Goldminenode
        if(goldminenodeSync.IsGoldminenodeListSynced()) {
            mnodeman.AskForMN(pnode, vinGoldminenode);
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

    if(pmn->nProtocolVersion < nMinRequiredProtocol) {
        strError = strprintf("Goldminenode protocol is too old: nProtocolVersion=%d, nMinRequiredProtocol=%d", pmn->nProtocolVersion, nMinRequiredProtocol);
        return false;
    }

    // Only goldminenodes should try to check goldminenode rank for old votes - they need to pick the right winner for future blocks.
    // Regular clients (miners included) need to verify goldminenode rank for future block votes only.
    if(!fGoldmineNode && nBlockHeight < nValidationHeight) return true;

    int nRank = mnodeman.GetGoldminenodeRank(vinGoldminenode, nBlockHeight - 101, nMinRequiredProtocol, false);

    if(nRank > MNPAYMENTS_SIGNATURES_TOTAL) {
        // It's common to have goldminenodes mistakenly think they are in the top 10
        // We don't want to print all of these messages in normal mode, debug mode should print though
        strError = strprintf("Goldminenode is not in the top %d (%d)", MNPAYMENTS_SIGNATURES_TOTAL, nRank);
        // Only ban for new mnw which is out of bounds, for old mnw MN list itself might be way too much off
        if(nRank > MNPAYMENTS_SIGNATURES_TOTAL*2 && nBlockHeight > nValidationHeight) {
            strError = strprintf("Goldminenode is not in the top %d (%d)", MNPAYMENTS_SIGNATURES_TOTAL*2, nRank);
            LogPrintf("CGoldminenodePaymentVote::IsValid -- Error: %s\n", strError);
            Misbehaving(pnode->GetId(), 20);
        }
        // Still invalid however
        return false;
    }

    return true;
}

bool CGoldminenodePayments::ProcessBlock(int nBlockHeight)
{
    // DETERMINE IF WE SHOULD BE VOTING FOR THE NEXT PAYEE

    if(fLiteMode || !fGoldmineNode) return false;

    // We have little chances to pick the right winner if winners list is out of sync
    // but we have no choice, so we'll try. However it doesn't make sense to even try to do so
    // if we have not enough data about goldminenodes.
    if(!goldminenodeSync.IsGoldminenodeListSynced()) return false;

    int nRank = mnodeman.GetGoldminenodeRank(activeGoldminenode.vin, nBlockHeight - 101, GetMinGoldminenodePaymentsProto(), false);

    if (nRank == -1) {
        LogPrint("mnpayments", "CGoldminenodePayments::ProcessBlock -- Unknown Goldminenode\n");
        return false;
    }

    if (nRank > MNPAYMENTS_SIGNATURES_TOTAL) {
        LogPrint("mnpayments", "CGoldminenodePayments::ProcessBlock -- Goldminenode not in the top %d (%d)\n", MNPAYMENTS_SIGNATURES_TOTAL, nRank);
        return false;
    }


    // LOCATE THE NEXT GOLDMINENODE WHICH SHOULD BE PAID

    LogPrintf("CGoldminenodePayments::ProcessBlock -- Start: nBlockHeight=%d, goldminenode=%s\n", nBlockHeight, activeGoldminenode.vin.prevout.ToStringShort());

    // pay to the oldest MN that still had no payment but its input is old enough and it was active long enough
    int nCount = 0;
    CGoldminenode *pmn = mnodeman.GetNextGoldminenodeInQueueForPayment(nBlockHeight, true, nCount);

    if (pmn == NULL) {
        LogPrintf("CGoldminenodePayments::ProcessBlock -- ERROR: Failed to find goldminenode to pay\n");
        return false;
    }

    LogPrintf("CGoldminenodePayments::ProcessBlock -- Goldminenode found by GetNextGoldminenodeInQueueForPayment(): %s\n", pmn->vin.prevout.ToStringShort());


    CScript payee = GetScriptForDestination(pmn->pubKeyCollateralAddress.GetID());

    CGoldminenodePaymentVote voteNew(activeGoldminenode.vin, nBlockHeight, payee);

    CTxDestination address1;
    ExtractDestination(payee, address1);
    CBitcoinAddress address2(address1);

    LogPrintf("CGoldminenodePayments::ProcessBlock -- vote: payee=%s, nBlockHeight=%d\n", address2.ToString(), nBlockHeight);

    // SIGN MESSAGE TO NETWORK WITH OUR GOLDMINENODE KEYS

    LogPrintf("CGoldminenodePayments::ProcessBlock -- Signing vote\n");
    if (voteNew.Sign()) {
        LogPrintf("CGoldminenodePayments::ProcessBlock -- AddPaymentVote()\n");

        if (AddPaymentVote(voteNew)) {
            voteNew.Relay();
            return true;
        }
    }

    return false;
}

void CGoldminenodePaymentVote::Relay()
{
    // do not relay until synced
    if (!goldminenodeSync.IsWinnersListSynced()) return;
    CInv inv(MSG_GOLDMINENODE_PAYMENT_VOTE, GetHash());
    RelayInv(inv);
}

bool CGoldminenodePaymentVote::CheckSignature(const CPubKey& pubKeyGoldminenode, int nValidationHeight, int &nDos)
{
    // do not ban by default
    nDos = 0;

    std::string strMessage = vinGoldminenode.prevout.ToStringShort() +
                boost::lexical_cast<std::string>(nBlockHeight) +
                ScriptToAsmStr(payee);

    std::string strError = "";
    if (!darkSendSigner.VerifyMessage(pubKeyGoldminenode, vchSig, strMessage, strError)) {
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

// Send all votes up to nCountNeeded blocks (but not more than GetStorageLimit)
void CGoldminenodePayments::Sync(CNode* pnode, int nCountNeeded)
{
    LOCK(cs_mapGoldminenodeBlocks);

    if(!pCurrentBlockIndex) return;

    if(pnode->nVersion < 70202) {
        // Old nodes can only sync via heavy method
        int nLimit = GetStorageLimit();
        if(nCountNeeded > nLimit) nCountNeeded = nLimit;
    } else {
        // New nodes request missing payment blocks themselves, push only votes for future blocks to them
        nCountNeeded = 0;
    }

    int nInvCount = 0;

    for(int h = pCurrentBlockIndex->nHeight - nCountNeeded; h < pCurrentBlockIndex->nHeight + 20; h++) {
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
    pnode->PushMessage(NetMsgType::SYNCSTATUSCOUNT, GOLDMINENODE_SYNC_MNW, nInvCount);
}

// Request low data/unknown payment blocks in batches directly from some node instead of/after preliminary Sync.
void CGoldminenodePayments::RequestLowDataPaymentBlocks(CNode* pnode)
{
    // Old nodes can't process this
    if(pnode->nVersion < 70202) return;
    if(!pCurrentBlockIndex) return;

    LOCK2(cs_main, cs_mapGoldminenodeBlocks);

    std::vector<CInv> vToFetch;
    int nLimit = GetStorageLimit();

    const CBlockIndex *pindex = pCurrentBlockIndex;

    while(pCurrentBlockIndex->nHeight - pindex->nHeight < nLimit) {
        if(!mapGoldminenodeBlocks.count(pindex->nHeight)) {
            // We have no idea about this block height, let's ask
            vToFetch.push_back(CInv(MSG_GOLDMINENODE_PAYMENT_BLOCK, pindex->GetBlockHash()));
            // We should not violate GETDATA rules
            if(vToFetch.size() == MAX_INV_SZ) {
                LogPrintf("CGoldminenodePayments::SyncLowDataPaymentBlocks -- asking peer %d for %d blocks\n", pnode->id, MAX_INV_SZ);
                pnode->PushMessage(NetMsgType::GETDATA, vToFetch);
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
            pnode->PushMessage(NetMsgType::GETDATA, vToFetch);
            // Start filling new batch
            vToFetch.clear();
        }
        ++it;
    }
    // Ask for the rest of it
    if(!vToFetch.empty()) {
        LogPrintf("CGoldminenodePayments::SyncLowDataPaymentBlocks -- asking peer %d for %d payment blocks\n", pnode->id, vToFetch.size());
        pnode->PushMessage(NetMsgType::GETDATA, vToFetch);
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

void CGoldminenodePayments::UpdatedBlockTip(const CBlockIndex *pindex)
{
    pCurrentBlockIndex = pindex;
    LogPrint("mnpayments", "CGoldminenodePayments::UpdatedBlockTip -- pCurrentBlockIndex->nHeight=%d\n", pCurrentBlockIndex->nHeight);

    ProcessBlock(pindex->nHeight + 10);
}
