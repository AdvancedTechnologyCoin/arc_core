// Copyright (c) 2014-2015 The Arctic Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "db.h"
#include "init.h"
#include "activegoldmine.h"
#include "goldmineman.h"
#include "goldmine-payments.h"
#include "goldmine-evolution.h"
#include "goldmineconfig.h"
#include "rpcserver.h"
#include "utilmoneystr.h"

#include <fstream>
using namespace json_spirit;
using namespace std;

Value gmevolution(const Array& params, bool fHelp)
{
    string strCommand;
    if (params.size() >= 1)
        strCommand = params[0].get_str();

    if (fHelp  ||
        (strCommand != "vote-many" && strCommand != "prepare" && strCommand != "submit" && strCommand != "vote" && strCommand != "getvotes" && strCommand != "getinfo" && strCommand != "show" && strCommand != "projection" && strCommand != "check" && strCommand != "nextblock"))
        throw runtime_error(
                "gmevolution \"command\"... ( \"passphrase\" )\n"
                "Vote or show current evolutions\n"
                "\nAvailable commands:\n"
                "  prepare            - Prepare proposal for network by signing and creating tx\n"
                "  submit             - Submit proposal for network\n"
                "  vote-many          - Vote on a Arctic initiative\n"
                "  vote-alias         - Vote on a Arctic initiative\n"
                "  vote               - Vote on a Arctic initiative/evolution\n"
                "  getvotes           - Show current goldmine evolutions\n"
                "  getinfo            - Show current goldmine evolutions\n"
                "  show               - Show all evolutions\n"
                "  projection         - Show the projection of which proposals will be paid the next cycle\n"
                "  check              - Scan proposals and remove invalid\n"
                "  nextblock          - Get next superblock for evolution system\n"
                );

    if(strCommand == "nextblock")
    {
        CBlockIndex* pindexPrev = chainActive.Tip();
        if(!pindexPrev) return "unknown";

        int nNext = pindexPrev->nHeight - pindexPrev->nHeight % GetEvolutionPaymentCycleBlocks() + GetEvolutionPaymentCycleBlocks();
        return nNext;
    }

    if(strCommand == "prepare")
    {
        int nBlockMin = 0;
        CBlockIndex* pindexPrev = chainActive.Tip();

        std::vector<CGoldmineConfig::CGoldmineEntry> mnEntries;
        mnEntries = goldmineConfig.getEntries();

        if (params.size() != 7)
            throw runtime_error("Correct usage is 'gmevolution prepare proposal-name url payment_count block_start arcticcoin_address monthly_payment_arcticcoin'");

        std::string strProposalName = params[1].get_str();
        if(strProposalName.size() > 20)
            return "Invalid proposal name, limit of 20 characters.";

        std::string strURL = params[2].get_str();
        if(strURL.size() > 64)
            return "Invalid url, limit of 64 characters.";

        int nPaymentCount = params[3].get_int();
        if(nPaymentCount < 1)
            return "Invalid payment count, must be more than zero.";

        //set block min
        if(pindexPrev != NULL) nBlockMin = pindexPrev->nHeight - GetEvolutionPaymentCycleBlocks() * (nPaymentCount + 1);

        int nBlockStart = params[4].get_int();
        if(nBlockStart % GetEvolutionPaymentCycleBlocks() != 0){
            int nNext = pindexPrev->nHeight - pindexPrev->nHeight % GetEvolutionPaymentCycleBlocks() + GetEvolutionPaymentCycleBlocks();
            return strprintf("Invalid block start - must be a evolution cycle block. Next valid block: %d", nNext);
        }

        int nBlockEnd = nBlockStart + GetEvolutionPaymentCycleBlocks() * nPaymentCount;

        if(nBlockStart < nBlockMin)
            return "Invalid block start, must be more than current height.";

        if(nBlockEnd < pindexPrev->nHeight)
            return "Invalid ending block, starting block + (payment_cycle*payments) must be more than current height.";

        CBitcoinAddress address(params[5].get_str());
        if (!address.IsValid())
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Arctic address");

        // Parse Arctic address
        CScript scriptPubKey = GetScriptForDestination(address.Get());
        CAmount nAmount = AmountFromValue(params[6]);

        //*************************************************************************

        // create transaction 15 minutes into the future, to allow for confirmation time
        CEvolutionProposalBroadcast evolutionProposalBroadcast(strProposalName, strURL, nPaymentCount, scriptPubKey, nAmount, nBlockStart, 0);

        std::string strError = "";
        if(!evolutionProposalBroadcast.IsValid(strError, false))
            return "Proposal is not valid - " + evolutionProposalBroadcast.GetHash().ToString() + " - " + strError;

        bool useIX = false; //true;
        // if (params.size() > 7) {
        //     if(params[7].get_str() != "false" && params[7].get_str() != "true")
        //         return "Invalid use_ix, must be true or false";
        //     useIX = params[7].get_str() == "true" ? true : false;
        // }

        CWalletTx wtx;
        if(!pwalletMain->GetEvolutionSystemCollateralTX(wtx, evolutionProposalBroadcast.GetHash(), useIX)){
            return "Error making collateral transaction for proposal. Please check your wallet balance and make sure your wallet is unlocked.";
        }

        // make our change address
        CReserveKey reservekey(pwalletMain);
        //send the tx to the network
        pwalletMain->CommitTransaction(wtx, reservekey, useIX ? "ix" : "tx");

        return wtx.GetHash().ToString();
    }

    if(strCommand == "submit")
    {
        int nBlockMin = 0;
        CBlockIndex* pindexPrev = chainActive.Tip();

        std::vector<CGoldmineConfig::CGoldmineEntry> mnEntries;
        mnEntries = goldmineConfig.getEntries();

        if (params.size() != 8)
            throw runtime_error("Correct usage is 'gmevolution submit proposal-name url payment_count block_start arcticcoin_address monthly_payment_arcticcoin fee_tx'");

        // Check these inputs the same way we check the vote commands:
        // **********************************************************

        std::string strProposalName = params[1].get_str();
        if(strProposalName.size() > 20)
            return "Invalid proposal name, limit of 20 characters.";

        std::string strURL = params[2].get_str();
        if(strURL.size() > 64)
            return "Invalid url, limit of 64 characters.";

        int nPaymentCount = params[3].get_int();
        if(nPaymentCount < 1)
            return "Invalid payment count, must be more than zero.";

        //set block min
        if(pindexPrev != NULL) nBlockMin = pindexPrev->nHeight - GetEvolutionPaymentCycleBlocks() * (nPaymentCount + 1);

        int nBlockStart = params[4].get_int();
        if(nBlockStart % GetEvolutionPaymentCycleBlocks() != 0){
            int nNext = pindexPrev->nHeight - pindexPrev->nHeight % GetEvolutionPaymentCycleBlocks() + GetEvolutionPaymentCycleBlocks();
            return strprintf("Invalid block start - must be a evolution cycle block. Next valid block: %d", nNext);
        }

        int nBlockEnd = nBlockStart + (GetEvolutionPaymentCycleBlocks()*nPaymentCount);

        if(nBlockStart < nBlockMin)
            return "Invalid payment count, must be more than current height.";

        if(nBlockEnd < pindexPrev->nHeight)
            return "Invalid ending block, starting block + (payment_cycle*payments) must be more than current height.";

        CBitcoinAddress address(params[5].get_str());
        if (!address.IsValid())
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Arctic address");

        // Parse Arctic address
        CScript scriptPubKey = GetScriptForDestination(address.Get());
        CAmount nAmount = AmountFromValue(params[6]);
        uint256 hash = ParseHashV(params[7], "parameter 1");

        //create the proposal incase we're the first to make it
        CEvolutionProposalBroadcast evolutionProposalBroadcast(strProposalName, strURL, nPaymentCount, scriptPubKey, nAmount, nBlockStart, hash);

        std::string strError = "";
        int nConf = 0;
        if(!IsEvolutionCollateralValid(hash, evolutionProposalBroadcast.GetHash(), strError, evolutionProposalBroadcast.nTime, nConf)){
            return "Proposal FeeTX is not valid - " + hash.ToString() + " - " + strError;
        }

        if(!goldmineSync.IsBlockchainSynced()){
            return "Must wait for client to sync with goldmine network. Try again in a minute or so.";            
        }

        // if(!evolutionProposalBroadcast.IsValid(strError)){
        //     return "Proposal is not valid - " + evolutionProposalBroadcast.GetHash().ToString() + " - " + strError;
        // }

        evolution.mapSeenGoldmineEvolutionProposals.insert(make_pair(evolutionProposalBroadcast.GetHash(), evolutionProposalBroadcast));
        evolutionProposalBroadcast.Relay();
        evolution.AddProposal(evolutionProposalBroadcast);

        return evolutionProposalBroadcast.GetHash().ToString();

    }

    if(strCommand == "vote-many")
    {
        std::vector<CGoldmineConfig::CGoldmineEntry> mnEntries;
        mnEntries = goldmineConfig.getEntries();

        if (params.size() != 3)
            throw runtime_error("Correct usage is 'gmevolution vote-many proposal-hash yes|no'");

        uint256 hash = ParseHashV(params[1], "parameter 1");
        std::string strVote = params[2].get_str();

        if(strVote != "yes" && strVote != "no") return "You can only vote 'yes' or 'no'";
        int nVote = VOTE_ABSTAIN;
        if(strVote == "yes") nVote = VOTE_YES;
        if(strVote == "no") nVote = VOTE_NO;

        int success = 0;
        int failed = 0;

        Object resultsObj;

        BOOST_FOREACH(CGoldmineConfig::CGoldmineEntry mne, goldmineConfig.getEntries()) {
            std::string errorMessage;
            std::vector<unsigned char> vchGoldMineSignature;
            std::string strGoldMineSignMessage;

            CPubKey pubKeyCollateralAddress;
            CKey keyCollateralAddress;
            CPubKey pubKeyGoldmine;
            CKey keyGoldmine;

            Object statusObj;

            if(!spySendSigner.SetKey(mne.getPrivKey(), errorMessage, keyGoldmine, pubKeyGoldmine)){
                failed++;
                statusObj.push_back(Pair("result", "failed"));
                statusObj.push_back(Pair("errorMessage", "Goldmine signing error, could not set key correctly: " + errorMessage));
                resultsObj.push_back(Pair(mne.getAlias(), statusObj));
                continue;
            }

            CGoldmine* pmn = gmineman.Find(pubKeyGoldmine);
            if(pmn == NULL)
            {
                failed++;
                statusObj.push_back(Pair("result", "failed"));
                statusObj.push_back(Pair("errorMessage", "Can't find goldmine by pubkey"));
                resultsObj.push_back(Pair(mne.getAlias(), statusObj));
                continue;
            }

            CEvolutionVote vote(pmn->vin, hash, nVote);
            if(!vote.Sign(keyGoldmine, pubKeyGoldmine)){
                failed++;
                statusObj.push_back(Pair("result", "failed"));
                statusObj.push_back(Pair("errorMessage", "Failure to sign."));
                resultsObj.push_back(Pair(mne.getAlias(), statusObj));
                continue;
            }


            std::string strError = "";
            if(evolution.UpdateProposal(vote, NULL, strError)) {
                evolution.mapSeenGoldmineEvolutionVotes.insert(make_pair(vote.GetHash(), vote));
                vote.Relay();
                success++;
                statusObj.push_back(Pair("result", "success"));
            } else {
                failed++;
                statusObj.push_back(Pair("result", strError.c_str()));
            }

            resultsObj.push_back(Pair(mne.getAlias(), statusObj));
        }

        Object returnObj;
        returnObj.push_back(Pair("overall", strprintf("Voted successfully %d time(s) and failed %d time(s).", success, failed)));
        returnObj.push_back(Pair("detail", resultsObj));

        return returnObj;
    }

    if(strCommand == "vote")
    {
        std::vector<CGoldmineConfig::CGoldmineEntry> mnEntries;
        mnEntries = goldmineConfig.getEntries();

        if (params.size() != 3)
            throw runtime_error("Correct usage is 'gmevolution vote proposal-hash yes|no'");

        uint256 hash = ParseHashV(params[1], "parameter 1");
        std::string strVote = params[2].get_str();

        if(strVote != "yes" && strVote != "no") return "You can only vote 'yes' or 'no'";
        int nVote = VOTE_ABSTAIN;
        if(strVote == "yes") nVote = VOTE_YES;
        if(strVote == "no") nVote = VOTE_NO;

        CPubKey pubKeyGoldmine;
        CKey keyGoldmine;
        std::string errorMessage;

        if(!spySendSigner.SetKey(strGoldMinePrivKey, errorMessage, keyGoldmine, pubKeyGoldmine))
            return "Error upon calling SetKey";

        CGoldmine* pmn = gmineman.Find(activeGoldmine.vin);
        if(pmn == NULL)
        {
            return "Failure to find goldmine in list : " + activeGoldmine.vin.ToString();
        }

        CEvolutionVote vote(activeGoldmine.vin, hash, nVote);
        if(!vote.Sign(keyGoldmine, pubKeyGoldmine)){
            return "Failure to sign.";
        }

        std::string strError = "";
        if(evolution.UpdateProposal(vote, NULL, strError)){
            evolution.mapSeenGoldmineEvolutionVotes.insert(make_pair(vote.GetHash(), vote));
            vote.Relay();
            return "Voted successfully";
        } else {
            return "Error voting : " + strError;
        }
    }

    if(strCommand == "projection")
    {
        Object resultObj;
        CAmount nTotalAllotted = 0;

        std::vector<CEvolutionProposal*> winningProps = evolution.GetEvolution();
        BOOST_FOREACH(CEvolutionProposal* pevolutionProposal, winningProps)
        {
            nTotalAllotted += pevolutionProposal->GetAllotted();

            CTxDestination address1;
            ExtractDestination(pevolutionProposal->GetPayee(), address1);
            CBitcoinAddress address2(address1);

            Object bObj;
            bObj.push_back(Pair("URL",  pevolutionProposal->GetURL()));
            bObj.push_back(Pair("Hash",  pevolutionProposal->GetHash().ToString()));
            bObj.push_back(Pair("BlockStart",  (int64_t)pevolutionProposal->GetBlockStart()));
            bObj.push_back(Pair("BlockEnd",    (int64_t)pevolutionProposal->GetBlockEnd()));
            bObj.push_back(Pair("TotalPaymentCount",  (int64_t)pevolutionProposal->GetTotalPaymentCount()));
            bObj.push_back(Pair("RemainingPaymentCount",  (int64_t)pevolutionProposal->GetRemainingPaymentCount()));
            bObj.push_back(Pair("PaymentAddress",   address2.ToString()));
            bObj.push_back(Pair("Ratio",  pevolutionProposal->GetRatio()));
            bObj.push_back(Pair("Yeas",  (int64_t)pevolutionProposal->GetYeas()));
            bObj.push_back(Pair("Nays",  (int64_t)pevolutionProposal->GetNays()));
            bObj.push_back(Pair("Abstains",  (int64_t)pevolutionProposal->GetAbstains()));
            bObj.push_back(Pair("TotalPayment",  ValueFromAmount(pevolutionProposal->GetAmount()*pevolutionProposal->GetTotalPaymentCount())));
            bObj.push_back(Pair("MonthlyPayment",  ValueFromAmount(pevolutionProposal->GetAmount())));
            bObj.push_back(Pair("Alloted",  ValueFromAmount(pevolutionProposal->GetAllotted())));
            bObj.push_back(Pair("TotalEvolutionAlloted",  ValueFromAmount(nTotalAllotted)));

            std::string strError = "";
            bObj.push_back(Pair("IsValid",  pevolutionProposal->IsValid(strError)));
            bObj.push_back(Pair("IsValidReason",  strError.c_str()));
            bObj.push_back(Pair("fValid",  pevolutionProposal->fValid));

            resultObj.push_back(Pair(pevolutionProposal->GetName(), bObj));
        }

        return resultObj;
    }

    if(strCommand == "show")
    {
        std::string strShow = "valid";
        if (params.size() == 2)
            std::string strProposalName = params[1].get_str();

        Object resultObj;
        int64_t nTotalAllotted = 0;

        std::vector<CEvolutionProposal*> winningProps = evolution.GetAllProposals();
        BOOST_FOREACH(CEvolutionProposal* pevolutionProposal, winningProps)
        {
            if(strShow == "valid" && !pevolutionProposal->fValid) continue;

            nTotalAllotted += pevolutionProposal->GetAllotted();

            CTxDestination address1;
            ExtractDestination(pevolutionProposal->GetPayee(), address1);
            CBitcoinAddress address2(address1);

            Object bObj;
            bObj.push_back(Pair("Name",  pevolutionProposal->GetName()));
            bObj.push_back(Pair("URL",  pevolutionProposal->GetURL()));
            bObj.push_back(Pair("Hash",  pevolutionProposal->GetHash().ToString()));
            bObj.push_back(Pair("FeeHash",  pevolutionProposal->nFeeTXHash.ToString()));
            bObj.push_back(Pair("BlockStart",  (int64_t)pevolutionProposal->GetBlockStart()));
            bObj.push_back(Pair("BlockEnd",    (int64_t)pevolutionProposal->GetBlockEnd()));
            bObj.push_back(Pair("TotalPaymentCount",  (int64_t)pevolutionProposal->GetTotalPaymentCount()));
            bObj.push_back(Pair("RemainingPaymentCount",  (int64_t)pevolutionProposal->GetRemainingPaymentCount()));
            bObj.push_back(Pair("PaymentAddress",   address2.ToString()));
            bObj.push_back(Pair("Ratio",  pevolutionProposal->GetRatio()));
            bObj.push_back(Pair("Yeas",  (int64_t)pevolutionProposal->GetYeas()));
            bObj.push_back(Pair("Nays",  (int64_t)pevolutionProposal->GetNays()));
            bObj.push_back(Pair("Abstains",  (int64_t)pevolutionProposal->GetAbstains()));
            bObj.push_back(Pair("TotalPayment",  ValueFromAmount(pevolutionProposal->GetAmount()*pevolutionProposal->GetTotalPaymentCount())));
            bObj.push_back(Pair("MonthlyPayment",  ValueFromAmount(pevolutionProposal->GetAmount())));

            bObj.push_back(Pair("IsEstablished",  pevolutionProposal->IsEstablished()));

            std::string strError = "";
            bObj.push_back(Pair("IsValid",  pevolutionProposal->IsValid(strError)));
            bObj.push_back(Pair("IsValidReason",  strError.c_str()));
            bObj.push_back(Pair("fValid",  pevolutionProposal->fValid));

            resultObj.push_back(Pair(pevolutionProposal->GetName(), bObj));
        }

        return resultObj;
    }

    if(strCommand == "getinfo")
    {
        if (params.size() != 2)
            throw runtime_error("Correct usage is 'gmevolution getinfo profilename'");

        std::string strProposalName = params[1].get_str();

        CEvolutionProposal* pevolutionProposal = evolution.FindProposal(strProposalName);

        if(pevolutionProposal == NULL) return "Unknown proposal name";

        CTxDestination address1;
        ExtractDestination(pevolutionProposal->GetPayee(), address1);
        CBitcoinAddress address2(address1);

        Object obj;
        obj.push_back(Pair("Name",  pevolutionProposal->GetName()));
        obj.push_back(Pair("Hash",  pevolutionProposal->GetHash().ToString()));
        obj.push_back(Pair("FeeHash",  pevolutionProposal->nFeeTXHash.ToString()));
        obj.push_back(Pair("URL",  pevolutionProposal->GetURL()));
        obj.push_back(Pair("BlockStart",  (int64_t)pevolutionProposal->GetBlockStart()));
        obj.push_back(Pair("BlockEnd",    (int64_t)pevolutionProposal->GetBlockEnd()));
        obj.push_back(Pair("TotalPaymentCount",  (int64_t)pevolutionProposal->GetTotalPaymentCount()));
        obj.push_back(Pair("RemainingPaymentCount",  (int64_t)pevolutionProposal->GetRemainingPaymentCount()));
        obj.push_back(Pair("PaymentAddress",   address2.ToString()));
        obj.push_back(Pair("Ratio",  pevolutionProposal->GetRatio()));
        obj.push_back(Pair("Yeas",  (int64_t)pevolutionProposal->GetYeas()));
        obj.push_back(Pair("Nays",  (int64_t)pevolutionProposal->GetNays()));
        obj.push_back(Pair("Abstains",  (int64_t)pevolutionProposal->GetAbstains()));
        obj.push_back(Pair("TotalPayment",  ValueFromAmount(pevolutionProposal->GetAmount()*pevolutionProposal->GetTotalPaymentCount())));
        obj.push_back(Pair("MonthlyPayment",  ValueFromAmount(pevolutionProposal->GetAmount())));
        
        obj.push_back(Pair("IsEstablished",  pevolutionProposal->IsEstablished()));

        std::string strError = "";
        obj.push_back(Pair("IsValid",  pevolutionProposal->IsValid(strError)));
        obj.push_back(Pair("fValid",  pevolutionProposal->fValid));

        return obj;
    }

    if(strCommand == "getvotes")
    {
        if (params.size() != 2)
            throw runtime_error("Correct usage is 'gmevolution getvotes profilename'");

        std::string strProposalName = params[1].get_str();

        Object obj;

        CEvolutionProposal* pevolutionProposal = evolution.FindProposal(strProposalName);

        if(pevolutionProposal == NULL) return "Unknown proposal name";

        std::map<uint256, CEvolutionVote>::iterator it = pevolutionProposal->mapVotes.begin();
        while(it != pevolutionProposal->mapVotes.end()){

            Object bObj;
            bObj.push_back(Pair("nHash",  (*it).first.ToString().c_str()));
            bObj.push_back(Pair("Vote",  (*it).second.GetVoteString()));
            bObj.push_back(Pair("nTime",  (int64_t)(*it).second.nTime));
            bObj.push_back(Pair("fValid",  (*it).second.fValid));

            obj.push_back(Pair((*it).second.vin.prevout.ToStringShort(), bObj));

            it++;
        }


        return obj;
    }

    if(strCommand == "check")
    {
        evolution.CheckAndRemove();

        return "Success";
    }

    return Value::null;
}

Value gmevolutionvoteraw(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 6)
        throw runtime_error(
                "gmevolutionvoteraw <goldmine-tx-hash> <goldmine-tx-index> <proposal-hash> <yes|no> <time> <vote-sig>\n"
                "Compile and relay a proposal vote with provided external signature instead of signing vote internally\n"
                );

    uint256 hashMnTx = ParseHashV(params[0], "gm tx hash");
    int nMnTxIndex = params[1].get_int();
    CTxIn vin = CTxIn(hashMnTx, nMnTxIndex);

    uint256 hashProposal = ParseHashV(params[2], "Proposal hash");
    std::string strVote = params[3].get_str();

    if(strVote != "yes" && strVote != "no") return "You can only vote 'yes' or 'no'";
    int nVote = VOTE_ABSTAIN;
    if(strVote == "yes") nVote = VOTE_YES;
    if(strVote == "no") nVote = VOTE_NO;

    int64_t nTime = params[4].get_int64();
    std::string strSig = params[5].get_str();
    bool fInvalid = false;
    vector<unsigned char> vchSig = DecodeBase64(strSig.c_str(), &fInvalid);

    if (fInvalid)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Malformed base64 encoding");

    CGoldmine* pmn = gmineman.Find(vin);
    if(pmn == NULL)
    {
        return "Failure to find goldmine in list : " + vin.ToString();
    }

    CEvolutionVote vote(vin, hashProposal, nVote);
    vote.nTime = nTime;
    vote.vchSig = vchSig;

    if(!vote.SignatureValid(true)){
        return "Failure to verify signature.";
    }

    std::string strError = "";
    if(evolution.UpdateProposal(vote, NULL, strError)){
        evolution.mapSeenGoldmineEvolutionVotes.insert(make_pair(vote.GetHash(), vote));
        vote.Relay();
        return "Voted successfully";
    } else {
        return "Error voting : " + strError;
    }
}

Value gmfinalevolution(const Array& params, bool fHelp)
{
    string strCommand;
    if (params.size() >= 1)
        strCommand = params[0].get_str();

    if (fHelp  ||
        (strCommand != "suggest" && strCommand != "vote-many" && strCommand != "vote" && strCommand != "show" && strCommand != "getvotes"))
        throw runtime_error(
                "gmfinalevolution \"command\"... ( \"passphrase\" )\n"
                "Vote or show current evolutions\n"
                "\nAvailable commands:\n"
                "  vote-many   - Vote on a finalized evolution\n"
                "  vote        - Vote on a finalized evolution\n"
                "  show        - Show existing finalized evolutions\n"
                "  getvotes     - Get vote information for each finalized evolution\n"
                );

    if(strCommand == "vote-many")
    {
        std::vector<CGoldmineConfig::CGoldmineEntry> mnEntries;
        mnEntries = goldmineConfig.getEntries();

        if (params.size() != 2)
            throw runtime_error("Correct usage is 'gmfinalevolution vote-many EVOLUTION_HASH'");

        std::string strHash = params[1].get_str();
        uint256 hash(strHash);

        int success = 0;
        int failed = 0;

        Object resultsObj;

        BOOST_FOREACH(CGoldmineConfig::CGoldmineEntry mne, goldmineConfig.getEntries()) {
            std::string errorMessage;
            std::vector<unsigned char> vchGoldMineSignature;
            std::string strGoldMineSignMessage;

            CPubKey pubKeyCollateralAddress;
            CKey keyCollateralAddress;
            CPubKey pubKeyGoldmine;
            CKey keyGoldmine;

            Object statusObj;

            if(!spySendSigner.SetKey(mne.getPrivKey(), errorMessage, keyGoldmine, pubKeyGoldmine)){
                failed++;
                statusObj.push_back(Pair("result", "failed"));
                statusObj.push_back(Pair("errorMessage", "Goldmine signing error, could not set key correctly: " + errorMessage));
                resultsObj.push_back(Pair(mne.getAlias(), statusObj));
                continue;
            }

            CGoldmine* pmn = gmineman.Find(pubKeyGoldmine);
            if(pmn == NULL)
            {
                failed++;
                statusObj.push_back(Pair("result", "failed"));
                statusObj.push_back(Pair("errorMessage", "Can't find goldmine by pubkey"));
                resultsObj.push_back(Pair(mne.getAlias(), statusObj));
                continue;
            }


            CFinalizedEvolutionVote vote(pmn->vin, hash);
            if(!vote.Sign(keyGoldmine, pubKeyGoldmine)){
                failed++;
                statusObj.push_back(Pair("result", "failed"));
                statusObj.push_back(Pair("errorMessage", "Failure to sign."));
                resultsObj.push_back(Pair(mne.getAlias(), statusObj));
                continue;
            }

            std::string strError = "";
            if(evolution.UpdateFinalizedEvolution(vote, NULL, strError)){
                evolution.mapSeenFinalizedEvolutionVotes.insert(make_pair(vote.GetHash(), vote));
                vote.Relay();
                success++;
                statusObj.push_back(Pair("result", "success"));
            } else {
                failed++;
                statusObj.push_back(Pair("result", strError.c_str()));
            }

            resultsObj.push_back(Pair(mne.getAlias(), statusObj));
        }

        Object returnObj;
        returnObj.push_back(Pair("overall", strprintf("Voted successfully %d time(s) and failed %d time(s).", success, failed)));
        returnObj.push_back(Pair("detail", resultsObj));

        return returnObj;
    }

    if(strCommand == "vote")
    {
        std::vector<CGoldmineConfig::CGoldmineEntry> mnEntries;
        mnEntries = goldmineConfig.getEntries();

        if (params.size() != 2)
            throw runtime_error("Correct usage is 'gmfinalevolution vote EVOLUTION_HASH'");

        std::string strHash = params[1].get_str();
        uint256 hash(strHash);

        CPubKey pubKeyGoldmine;
        CKey keyGoldmine;
        std::string errorMessage;

        if(!spySendSigner.SetKey(strGoldMinePrivKey, errorMessage, keyGoldmine, pubKeyGoldmine))
            return "Error upon calling SetKey";

        CGoldmine* pmn = gmineman.Find(activeGoldmine.vin);
        if(pmn == NULL)
        {
            return "Failure to find goldmine in list : " + activeGoldmine.vin.ToString();
        }

        CFinalizedEvolutionVote vote(activeGoldmine.vin, hash);
        if(!vote.Sign(keyGoldmine, pubKeyGoldmine)){
            return "Failure to sign.";
        }

        std::string strError = "";
        if(evolution.UpdateFinalizedEvolution(vote, NULL, strError)){
            evolution.mapSeenFinalizedEvolutionVotes.insert(make_pair(vote.GetHash(), vote));
            vote.Relay();
            return "success";
        } else {
            return "Error voting : " + strError;
        }

    }

    if(strCommand == "show")
    {
        Object resultObj;

        std::vector<CFinalizedEvolution*> winningFbs = evolution.GetFinalizedEvolutions();
        BOOST_FOREACH(CFinalizedEvolution* finalizedEvolution, winningFbs)
        {
            Object bObj;
            bObj.push_back(Pair("FeeTX",  finalizedEvolution->nFeeTXHash.ToString()));
            bObj.push_back(Pair("Hash",  finalizedEvolution->GetHash().ToString()));
            bObj.push_back(Pair("BlockStart",  (int64_t)finalizedEvolution->GetBlockStart()));
            bObj.push_back(Pair("BlockEnd",    (int64_t)finalizedEvolution->GetBlockEnd()));
            bObj.push_back(Pair("Proposals",  finalizedEvolution->GetProposals()));
            bObj.push_back(Pair("VoteCount",  (int64_t)finalizedEvolution->GetVoteCount()));
            bObj.push_back(Pair("Status",  finalizedEvolution->GetStatus()));

            std::string strError = "";
            bObj.push_back(Pair("IsValid",  finalizedEvolution->IsValid(strError)));
            bObj.push_back(Pair("IsValidReason",  strError.c_str()));

            resultObj.push_back(Pair(finalizedEvolution->GetName(), bObj));
        }

        return resultObj;

    }

    if(strCommand == "getvotes")
    {
        if (params.size() != 2)
            throw runtime_error("Correct usage is 'gmevolution getvotes evolution-hash'");

        std::string strHash = params[1].get_str();
        uint256 hash(strHash);

        Object obj;

        CFinalizedEvolution* pfinalEvolution = evolution.FindFinalizedEvolution(hash);

        if(pfinalEvolution == NULL) return "Unknown evolution hash";

        std::map<uint256, CFinalizedEvolutionVote>::iterator it = pfinalEvolution->mapVotes.begin();
        while(it != pfinalEvolution->mapVotes.end()){

            Object bObj;
            bObj.push_back(Pair("nHash",  (*it).first.ToString().c_str()));
            bObj.push_back(Pair("nTime",  (int64_t)(*it).second.nTime));
            bObj.push_back(Pair("fValid",  (*it).second.fValid));

            obj.push_back(Pair((*it).second.vin.prevout.ToStringShort(), bObj));

            it++;
        }


        return obj;
    }


    return Value::null;
}
