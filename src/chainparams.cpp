// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2015-2017 The Arctic Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "consensus/merkle.h"

#include "tinyformat.h"
#include "util.h"
#include "utilstrencodings.h"

#include <assert.h>

#include <boost/assign/list_of.hpp>

#include "chainparamsseeds.h"

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(txNew);
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
     const char* pszTimestamp = "euronews.com 23/July/2015 Nasa announces the discovery of an earth-like planet";
 	const CScript genesisOutputScript = CScript() << ParseHex("0464db1776e49cde9d666f79ea3a99f2e9019b1ead31df97ea33fa3a34486a1676ad098e01d48c02d73157ff608e983c2f5474ad4750041b25be4d6b94def99932") << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward);
}


/**
 * Main network
 */
/**
 * What makes a good checkpoint block?
 * + Is surrounded by blocks with reasonable timestamps
 *   (no blocks before with a timestamp after, none after with
 *    timestamp before)
 * + Contains no strange transactions
 */


class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = "main";
        consensus.nSubsidyHalvingInterval = 210240; // Note: actual number of blocks per calendar year with DGW v3 is ~200700 (for example 449750 - 249050)
        consensus.nGoldminenodePaymentsStartBlock = 1; // not true, but it's ok as long as it's less then nGoldminenodePaymentsIncreaseBlock
        consensus.nInstantSendKeepLock = 24;
        consensus.nEvolutionPaymentsStartBlock = 105000; // actual historical value
        consensus.nEvolutionPaymentsCycleBlocks = 16616; // ~(60*24*30)/2.6, actual number of blocks per month is 200700 / 12 = 16725
        consensus.nEvolutionPaymentsWindowBlocks = 100;
        consensus.nEvolutionProposalEstablishingTime = 60*60*24;
        consensus.nSuperblockStartBlock = 105001; // The block at which 12.1 goes live (end of final 12.0 budget cycle)
        consensus.nSuperblockCycle = 16616; // ~(60*24*30)/2.6, actual number of blocks per month is 200700 / 12 = 16725
        consensus.nGovernanceMinQuorum = 10;
        consensus.nGovernanceFilterElements = 20000;
        consensus.nGoldminenodeMinimumConfirmations = 15;
        consensus.nMajorityEnforceBlockUpgrade = 750;
        consensus.nMajorityRejectBlockOutdated = 950;
        consensus.nMajorityWindow = 1000;
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256S("0x00000f39e7fdff2d6025511f525bf1dce2f705c15d098d7f31c824a1785a254a");
        consensus.powLimit = uint256S("00000fffff000000000000000000000000000000000000000000000000000000");
        consensus.nPowTargetTimespan = 24 * 60 * 60; // Arctic: 1 day
        consensus.nPowTargetSpacing = 2.5 * 60; // Arctic: 2.5 minutes
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1916; // 95% of 2016
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1406156401; //  24/07/2014
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1437696001; // 24/07/2015

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1486252800; // Feb 5th, 2017
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1517788800; // Feb 5th, 2018

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0x3d;
        pchMessageStart[1] = 0xd2;
        pchMessageStart[2] = 0x28;
        pchMessageStart[3] = 0x61;
        vAlertPubKey = ParseHex("043e15412cd9aaef5c2ca59b943b44100a5406878ab43cf2df79ed578687b05f8f96cc207fc5c611358a34e694eb6d139683024c999016bfb358e6673aa18fb954");
        nDefaultPort = 7209;
        nMaxTipAge = 6 * 60 * 60; // ~144 blocks behind -> 2 x fork detection time, was 24 * 60 * 60 in bitcoin
        nPruneAfterHeight = 100000;

        genesis = CreateGenesisBlock(1437685200, 1179352, 0x1e0ffff0, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x00000f39e7fdff2d6025511f525bf1dce2f705c15d098d7f31c824a1785a254a"));
        assert(genesis.hashMerkleRoot == uint256S("0xfc6237933e11360eedbb4d3993575cf7a65e257b7c265b115cad37c8d183dc3d"));


       		vSeeds.push_back(CDNSSeedData("advtech.group", "dnsseed.advtech.group"));
   		vSeeds.push_back(CDNSSeedData("5.9.41.6", "5.9.41.6"));
		vSeeds.push_back(CDNSSeedData("5.9.176.36", "5.9.176.36"));
		vSeeds.push_back(CDNSSeedData("88.99.173.16", "88.99.173.16"));
		vSeeds.push_back(CDNSSeedData("85.10.206.182", "85.10.206.182"));

        // ArcticCoin addresses start with 'A'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,23);
        // ArcticCoin script addresses start with '4'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,8);
        // ArcticCoin private keys start with '7' or 'X'
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,176);
        // ArcticCoin BIP32 pubkeys start with 'xpub' (Bitcoin defaults)
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x07)(0xE8)(0xF8)(0x9C).convert_to_container<std::vector<unsigned char> >();
        // ArcticCoin BIP32 prvkeys start with 'xprv' (Bitcoin defaults)
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x07)(0x74)(0xA1)(0x37).convert_to_container<std::vector<unsigned char> >();
        // ArcticCoin BIP44 coin type is '5'
        base58Prefixes[EXT_COIN_TYPE]  = boost::assign::list_of(0x05)(0x00)(0x00)(0x80).convert_to_container<std::vector<unsigned char> >();

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = false;

        nPoolMaxTransactions = 3;
        nFulfilledRequestExpireTime = 60*60; // fulfilled requests expire in 1 hour
        strSporkPubKey = "040d2e49efb5881619c17f419e1e7355ec7e8065a69055dc0acff4054908823d5d2a0910f206369b5ccbff34373d371e8b0ce46fd0d766e44160bbc09cded59801";
        strGoldminenodePaymentsPubKey = "040d2e49efb5881619c17f419e1e7355ec7e8065a69055dc0acff4054908823d5d2a0910f206369b5ccbff34373d371e8b0ce46fd0d766e44160bbc09cded59801";

        checkpointData = (CCheckpointData) {
            boost::assign::map_list_of
            (    0, uint256S("0x00000f39e7fdff2d6025511f525bf1dce2f705c15d098d7f31c824a1785a254a"))
			(  975, uint256S("0x00000100549369f77cf5e01c60c483b1508aeaa53719b971ef19e80d8abd89d0"))
			( 3715, uint256S("0x0000024f111e8b996c038a6371d98d33f92b8583dc3c30dce7a6f44011f54db2"))
			( 6145, uint256S("0x0000000004817ef0faf6dd02bbcb74d133df139212075947802067316676d61e"))
			(12863, uint256S("0x00000000072e62711296c94e7687afda1cc8c7033550603bc3ae7c05bb11e887"))
			(17971, uint256S("0x000000000a9ae7a3540e9d1ed4434b6ffa5045b4a3d835144fc8e709ae040d59"))
			(20438, uint256S("0x0000000001b202c01a581ad8eed84c24da9b1e9a7a1ce99f585d0560016878ec"))
			(27194, uint256S("0x000000000df23b42705c16276be267e784a678586134c1910db86c8735f8192f"))		
			(27790, uint256S("0x0000000014a380900941cdf4d79c53b82dd5d557041dc219abe404b022859385"))
			(28472, uint256S("0x00000000183f960efd87642792d29d86fd15296bd1cf3d5ab23ce6eefe999fb6"))
			(39408, uint256S("0x0000000001ed0fa1f07e9013d073889b6ddb45989e7a90216bd7825c7bf250bd"))
			(97961, uint256S("0x000000000885f7d2150e39c2229572d0d33634c076bdd8d49a4cbf7d76fb1002")),
            1486720819, // * UNIX timestamp of last checkpoint block
            107745,    // * total number of transactions between genesis and last checkpoint
                        //   (the tx=... number in the SetBestChain debug.log lines)
            1800        // * estimated number of transactions per day after checkpoint
        };
    }
};
static CMainParams mainParams;

/**
 * Testnet (v3)
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = "test";
        consensus.nSubsidyHalvingInterval = 210240;
        consensus.nGoldminenodePaymentsStartBlock = 10000; // not true, but it's ok as long as it's less then nGoldminenodePaymentsIncreaseBlock
        consensus.nGoldminenodePaymentsIncreaseBlock = 46000;
        consensus.nGoldminenodePaymentsIncreasePeriod = 576;
        consensus.nInstantSendKeepLock = 6;
        consensus.nEvolutionPaymentsStartBlock = 100;
        consensus.nEvolutionPaymentsCycleBlocks = 50;
        consensus.nEvolutionPaymentsWindowBlocks = 10;
        consensus.nEvolutionProposalEstablishingTime = 60*20;
        consensus.nSuperblockStartBlock = 61000; // NOTE: Should satisfy nSuperblockStartBlock > nEvolutionPeymentsStartBlock
        consensus.nSuperblockCycle = 24; // Superblocks can be issued hourly on testnet
        consensus.nGovernanceMinQuorum = 1;
        consensus.nGovernanceFilterElements = 500;
        consensus.nGoldminenodeMinimumConfirmations = 1;
        consensus.nMajorityEnforceBlockUpgrade = 51;
        consensus.nMajorityRejectBlockOutdated = 75;
        consensus.nMajorityWindow = 100;
        consensus.BIP34Height = 21111;
        consensus.BIP34Hash = uint256S("0x000007ddc82c93642da51870d1d549c6b91c44d290aad2b9d95e90f17ec2fc2b");
        consensus.powLimit = uint256S("00000fffff000000000000000000000000000000000000000000000000000000");
        consensus.nPowTargetTimespan = 24 * 60 * 60; // Arctic: 1 day
        consensus.nPowTargetSpacing = 2.5 * 60; // Arctic: 2.5 minutes
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1512; // 75% for testchains
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1456790400; // March 1st, 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1493596800; // May 1st, 2017

        pchMessageStart[0] = 0x2a;
        pchMessageStart[1] = 0x2c;
        pchMessageStart[2] = 0x2c;
        pchMessageStart[3] = 0x2d;
        vAlertPubKey = ParseHex("04a0185346717caa7955096af0e2932dc4cfda459792617c26e5c620ef9eb50a843aedb6d58f05360ed871b974c2ae71b4d68c6456ec86ae39a581cb4704e61de5");
        nDefaultPort = 17209;
        nMaxTipAge = 0x7fffffff; // allow mining on top of old blocks for testnet
        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlock(1437685201UL, 747171UL, 0x1e0ffff0, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x000007ddc82c93642da51870d1d549c6b91c44d290aad2b9d95e90f17ec2fc2b"));
        assert(genesis.hashMerkleRoot == uint256S("0xfc6237933e11360eedbb4d3993575cf7a65e257b7c265b115cad37c8d183dc3d"));

        vFixedSeeds.clear();
        vSeeds.clear();
		vSeeds.push_back(CDNSSeedData("advtech.group", "dnsseed.advtech.group"));
    		vSeeds.push_back(CDNSSeedData("5.9.41.6", "5.9.41.6"));
		vSeeds.push_back(CDNSSeedData("5.9.176.36", "5.9.176.36"));
		vSeeds.push_back(CDNSSeedData("88.99.173.16", "88.99.173.16"));
		vSeeds.push_back(CDNSSeedData("85.10.206.182", "85.10.206.182"));

        // Testnet ArcticCoin addresses start with 'y'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,83);
        // Testnet ArcticCoin script addresses start with '8' or '9'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,9);
        // Testnet private keys start with '9' or 'c' (Bitcoin defaults)
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        // Testnet ArcticCoin BIP32 pubkeys start with 'tpub' (Bitcoin defaults)
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x09)(0x72)(0x98)(0xBF).convert_to_container<std::vector<unsigned char> >();
        // Testnet ArcticCoin BIP32 prvkeys start with 'tprv' (Bitcoin defaults)
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x09)(0x62)(0x3A)(0x6F).convert_to_container<std::vector<unsigned char> >();
        // Testnet ArcticCoin BIP44 coin type is '1' (All coin's testnet default)
        base58Prefixes[EXT_COIN_TYPE]  = boost::assign::list_of(0x01)(0x00)(0x00)(0x80).convert_to_container<std::vector<unsigned char> >();

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test, pnSeed6_test + ARRAYLEN(pnSeed6_test));

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = true;

		nPoolMaxTransactions = 3;
        nFulfilledRequestExpireTime = 5*60; // fulfilled requests expire in 5 minutes
        strSporkPubKey = "04d65cdbb409be1be32829855bd43a150666f731f422850ca1b101e56f4f1dcd3109402fd7b9068fc39d77666d4338982d9f30c4997ce948c4e0c770f99b7fc95b";
        strGoldminenodePaymentsPubKey = "04d65cdbb409be1be32829855bd43a150666f731f422850ca1b101e56f4f1dcd3109402fd7b9068fc39d77666d4338982d9f30c4997ce948c4e0c770f99b7fc95b";

        checkpointData = (CCheckpointData) {
            boost::assign::map_list_of
            ( 0, uint256S("0x000007ddc82c93642da51870d1d549c6b91c44d290aad2b9d95e90f17ec2fc2b")),
            1437685201, // * UNIX timestamp of last checkpoint block
            0,     // * total number of transactions between genesis and last checkpoint
                        //   (the tx=... number in the SetBestChain debug.log lines)
            0         // * estimated number of transactions per day after checkpoint
        };

    }
};
static CTestNetParams testNetParams;

/**
 * Regression test
 */
class CRegTestParams : public CChainParams {
public:
    CRegTestParams() {
        strNetworkID = "regtest";
        consensus.nSubsidyHalvingInterval = 150;
        consensus.nGoldminenodePaymentsStartBlock = 240;
        consensus.nGoldminenodePaymentsIncreaseBlock = 350;
        consensus.nGoldminenodePaymentsIncreasePeriod = 10;
        consensus.nInstantSendKeepLock = 6;
        consensus.nEvolutionPaymentsStartBlock = 1000;
        consensus.nEvolutionPaymentsCycleBlocks = 50;
        consensus.nEvolutionPaymentsWindowBlocks = 10;
        consensus.nEvolutionProposalEstablishingTime = 60*20;
        consensus.nSuperblockStartBlock = 1500;
        consensus.nSuperblockCycle = 10;
        consensus.nGovernanceMinQuorum = 1;
        consensus.nGovernanceFilterElements = 100;
        consensus.nGoldminenodeMinimumConfirmations = 1;
        consensus.nMajorityEnforceBlockUpgrade = 750;
        consensus.nMajorityRejectBlockOutdated = 950;
        consensus.nMajorityWindow = 1000;
        consensus.BIP34Height = -1; // BIP34 has not necessarily activated on regtest
        consensus.BIP34Hash = uint256();
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 24 * 60 * 60; // Arctic: 1 day
        consensus.nPowTargetSpacing = 2.5 * 60; // Arctic: 2.5 minutes
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 2016)
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 999999999999ULL;

        pchMessageStart[0] = 0x3a;
        pchMessageStart[1] = 0x3c;
        pchMessageStart[2] = 0x3c;
        pchMessageStart[3] = 0x3d;
        nMaxTipAge = 6 * 60 * 60; // ~144 blocks behind -> 2 x fork detection time, was 24 * 60 * 60 in bitcoin
        nDefaultPort = 17203;
        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlock(1437685202, 0, 0x207fffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x62890b2e5ba197afc37d17fea3ffb3e207034a8290080cd3100976c36d14ff1a"));
        assert(genesis.hashMerkleRoot == uint256S("0xfc6237933e11360eedbb4d3993575cf7a65e257b7c265b115cad37c8d183dc3d"));

        vFixedSeeds.clear(); //! Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();  //! Regtest mode doesn't have any DNS seeds.

        fMiningRequiresPeers = false;
        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;
        fTestnetToBeDeprecatedFieldRPC = false;

        nFulfilledRequestExpireTime = 5*60; // fulfilled requests expire in 5 minutes

        checkpointData = (CCheckpointData){
            boost::assign::map_list_of
            ( 0, uint256S("0x62890b2e5ba197afc37d17fea3ffb3e207034a8290080cd3100976c36d14ff1a")),
            1437685202,
            0,
            0
        };
        // Regtest ArcticCoin addresses start with 'y'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,140);
        // Regtest ArcticCoin script addresses start with '8' or '9'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,19);
        // Regtest private keys start with '9' or 'c' (Bitcoin defaults)
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        // Regtest ArcticCoin BIP32 pubkeys start with 'tpub' (Bitcoin defaults)
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x35)(0x87)(0xCF).convert_to_container<std::vector<unsigned char> >();
        // Regtest ArcticCoin BIP32 prvkeys start with 'tprv' (Bitcoin defaults)
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x35)(0x83)(0x94).convert_to_container<std::vector<unsigned char> >();
        // Regtest ArcticCoin BIP44 coin type is '1' (All coin's testnet default)
        base58Prefixes[EXT_COIN_TYPE]  = boost::assign::list_of(0x80)(0x00)(0x00)(0x01).convert_to_container<std::vector<unsigned char> >();
   }
};
static CRegTestParams regTestParams;

static CChainParams *pCurrentParams = 0;

const CChainParams &Params() {
    assert(pCurrentParams);
    return *pCurrentParams;
}

CChainParams& Params(const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN)
            return mainParams;
    else if (chain == CBaseChainParams::TESTNET)
            return testNetParams;
    else if (chain == CBaseChainParams::REGTEST)
            return regTestParams;
    else
        throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network)
{
    SelectBaseParams(network);
    pCurrentParams = &Params(network);
}
