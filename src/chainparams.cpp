// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2015-2016 The Arctic developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"

#include "random.h"
#include "util.h"
#include "utilstrencodings.h"

#include <assert.h>

#include <boost/assign/list_of.hpp>

using namespace std;
using namespace boost::assign;

struct SeedSpec6 {
    uint8_t addr[16];
    uint16_t port;
};

#include "chainparamsseeds.h"

/**
 * Main network
 */

//! Convert the pnSeeds6 array into usable address objects.
static void convertSeed6(std::vector<CAddress> &vSeedsOut, const SeedSpec6 *data, unsigned int count)
{
    // It'll only connect to one or two seed nodes because once it connects,
    // it'll get a pile of addresses with newer timestamps.
    // Seed nodes are given a random 'last seen time' of between one and two
    // weeks ago.
    const int64_t nOneWeek = 7*24*60*60;
    for (unsigned int i = 0; i < count; i++)
    {
        struct in6_addr ip;
        memcpy(&ip, data[i].addr, sizeof(ip));
        CAddress addr(CService(ip, data[i].port));
        addr.nTime = GetTime() - GetRand(nOneWeek) - nOneWeek;
        vSeedsOut.push_back(addr);
    }
}

/**
 * What makes a good checkpoint block?
 * + Is surrounded by blocks with reasonable timestamps
 *   (no blocks before with a timestamp after, none after with
 *    timestamp before)
 * + Contains no strange transactions
 */

static Checkpoints::MapCheckpoints mapCheckpoints =
        boost::assign::map_list_of
        (    0, uint256("0x00000f39e7fdff2d6025511f525bf1dce2f705c15d098d7f31c824a1785a254a"))
		(  975, uint256("0x00000100549369f77cf5e01c60c483b1508aeaa53719b971ef19e80d8abd89d0"))
		( 3715, uint256("0x0000024f111e8b996c038a6371d98d33f92b8583dc3c30dce7a6f44011f54db2"))
		( 6145, uint256("0x0000000004817ef0faf6dd02bbcb74d133df139212075947802067316676d61e"))
		(12863, uint256("0x00000000072e62711296c94e7687afda1cc8c7033550603bc3ae7c05bb11e887"))
		(17971, uint256("0x000000000a9ae7a3540e9d1ed4434b6ffa5045b4a3d835144fc8e709ae040d59"))
		(20438, uint256("0x0000000001b202c01a581ad8eed84c24da9b1e9a7a1ce99f585d0560016878ec"))
		(27194, uint256("0x000000000df23b42705c16276be267e784a678586134c1910db86c8735f8192f"))		
		(27790, uint256("0x0000000014a380900941cdf4d79c53b82dd5d557041dc219abe404b022859385"))
		(28472, uint256("0x00000000183f960efd87642792d29d86fd15296bd1cf3d5ab23ce6eefe999fb6"))
		(39408, uint256("0x0000000001ed0fa1f07e9013d073889b6ddb45989e7a90216bd7825c7bf250bd"))
        ;
static const Checkpoints::CCheckpointData data = {
        &mapCheckpoints,
        1476787425, // * UNIX timestamp of last checkpoint block
        41533,      // * total number of transactions between genesis and last checkpoint
                    //   (the tx=... number in the SetBestChain debug.log lines)
        2800        // * estimated number of transactions per day after checkpoint
    };

static Checkpoints::MapCheckpoints mapCheckpointsTestnet =
        boost::assign::map_list_of
        ( 0, uint256("0x000007ddc82c93642da51870d1d549c6b91c44d290aad2b9d95e90f17ec2fc2b"))
        ;
static const Checkpoints::CCheckpointData dataTestnet = {
        &mapCheckpointsTestnet,
        1437685201,
        0,
        0
    };

static Checkpoints::MapCheckpoints mapCheckpointsRegtest =
        boost::assign::map_list_of
        ( 0, uint256("0x62890b2e5ba197afc37d17fea3ffb3e207034a8290080cd3100976c36d14ff1a"))
        ;
static const Checkpoints::CCheckpointData dataRegtest = {
        &mapCheckpointsRegtest,
        1437685202,
        0,
        0
    };

class CMainParams : public CChainParams {
public:
    CMainParams() {
        networkID = CBaseChainParams::MAIN;
        strNetworkID = "main";
        /** 
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 4-byte int at any alignment.
         */
        pchMessageStart[0] = 0x3d;
        pchMessageStart[1] = 0xd2;
        pchMessageStart[2] = 0x28;
		pchMessageStart[3] = 0x61;
        
		vAlertPubKey = ParseHex("043e15412cd9aaef5c2ca59b943b44100a5406878ab43cf2df79ed578687b05f8f96cc207fc5c611358a34e694eb6d139683024c999016bfb358e6673aa18fb954");
        nDefaultPort = 7209;
        bnProofOfWorkLimit = ~uint256(0) >> 20;  // Arctic starting difficulty is 1 / 2^12
        nSubsidyHalvingInterval = 210000;
        nEnforceBlockUpgradeMajority = 750;
        nRejectBlockOutdatedMajority = 950;
        nToCheckBlockUpgradeMajority = 1000;
        nMinerThreads = 0;
        nTargetTimespan = 24 * 60 * 60; // Arctic: 1 day
        nTargetSpacing = 2.5 * 60; // Arctic: 2.5 minutes

       
        const char* pszTimestamp = "euronews.com 23/July/2015 Nasa announces the discovery of an earth-like planet";
        CMutableTransaction txNew;
        txNew.vin.resize(1);
        txNew.vout.resize(1);
        txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
        txNew.vout[0].nValue = 50 * COIN;
        txNew.vout[0].scriptPubKey = CScript() << ParseHex("0464db1776e49cde9d666f79ea3a99f2e9019b1ead31df97ea33fa3a34486a1676ad098e01d48c02d73157ff608e983c2f5474ad4750041b25be4d6b94def99932") << OP_CHECKSIG;
        genesis.vtx.push_back(txNew);
        genesis.hashPrevBlock = 0;
        genesis.hashMerkleRoot = genesis.BuildMerkleTree();
        genesis.nVersion = 1;
        genesis.nTime    = 1437685200;
        genesis.nBits    = 0x1e0ffff0;
        genesis.nNonce   = 1179352;

        hashGenesisBlock = genesis.GetHash();
        assert(hashGenesisBlock == uint256("0x00000f39e7fdff2d6025511f525bf1dce2f705c15d098d7f31c824a1785a254a"));
        assert(genesis.hashMerkleRoot == uint256("0xfc6237933e11360eedbb4d3993575cf7a65e257b7c265b115cad37c8d183dc3d"));

        vSeeds.push_back(CDNSSeedData("5.9.65.168", "5.9.65.168"));
		vSeeds.push_back(CDNSSeedData("5.9.55.201", "5.9.55.201"));
		vSeeds.push_back(CDNSSeedData("78.46.75.49", "78.46.75.49"));
		vSeeds.push_back(CDNSSeedData("78.47.238.36", "78.47.238.36"));

		#if __cplusplus > 199711L
			base58Prefixes[PUBKEY_ADDRESS] = {23};                    // Arctic addresses start with 'A'
			base58Prefixes[SCRIPT_ADDRESS] = {8};                    // Arctic script addresses start with '4'
			base58Prefixes[SECRET_KEY] =     {176};                    // Arctic private keys start with '7' or 'X'
			base58Prefixes[EXT_PUBLIC_KEY] = {0x07,0xE8,0xF8,0x9C}; // Arctic BIP32 pubkeys
			base58Prefixes[EXT_SECRET_KEY] = {0x07,0x74,0xA1,0x37}; // Arctic BIP32 prvkeys
			base58Prefixes[EXT_COIN_TYPE]  = {0x05,0x00,0x00,0x80};             // Arctic BIP44 coin type is '5'
		#else
			base58Prefixes[PUBKEY_ADDRESS] = list_of( 23);                    // Arctic addresses start with 'A'
			base58Prefixes[SCRIPT_ADDRESS] = list_of(  8);                    // Arctic script addresses start with '4'
			base58Prefixes[SECRET_KEY] =     list_of(176);                    // Arctic private keys start with '7' or 'X'
			base58Prefixes[EXT_PUBLIC_KEY] = list_of(0x07)(0xE8)(0xF8)(0x9C); // Arctic BIP32 pubkeys
			base58Prefixes[EXT_SECRET_KEY] = list_of(0x07)(0x74)(0xA1)(0x37); // Arctic BIP32 prvkeys
			base58Prefixes[EXT_COIN_TYPE]  = list_of(0x80000005);             // Arctic BIP44 coin type is '5'
        #endif
		
		convertSeed6(vFixedSeeds, pnSeed6_main, ARRAYLEN(pnSeed6_main));

        fRequireRPCPassword = true;
        fMiningRequiresPeers = true;
        fAllowMinDifficultyBlocks = false;
        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        fSkipProofOfWorkCheck = false;
        fTestnetToBeDeprecatedFieldRPC = false;

        nPoolMaxTransactions = 3;
        strSporkKey = "040d2e49efb5881619c17f419e1e7355ec7e8065a69055dc0acff4054908823d5d2a0910f206369b5ccbff34373d371e8b0ce46fd0d766e44160bbc09cded59801";
        strMasternodePaymentsPubKey = "040d2e49efb5881619c17f419e1e7355ec7e8065a69055dc0acff4054908823d5d2a0910f206369b5ccbff34373d371e8b0ce46fd0d766e44160bbc09cded59801";
        strDarksendPoolDummyAddress = "AbJNRzf2fu1tUi2cPWMan5d5hZ3o8swAT3";
        nStartMasternodePayments = 1470603601; //08.08.2016 @ 00:00:01 MSK (GTM +03:00)
    }

    const Checkpoints::CCheckpointData& Checkpoints() const 
    {
        return data;
    }
};
static CMainParams mainParams;

/**
 * Testnet (v3)
 */
class CTestNetParams : public CMainParams {
public:
    CTestNetParams() {
        networkID = CBaseChainParams::TESTNET;
        strNetworkID = "test";
        pchMessageStart[0] = 0x2a;
        pchMessageStart[1] = 0x2c;
        pchMessageStart[2] = 0x2c;
        pchMessageStart[3] = 0x2d;
        vAlertPubKey = ParseHex("04a0185346717caa7955096af0e2932dc4cfda459792617c26e5c620ef9eb50a843aedb6d58f05360ed871b974c2ae71b4d68c6456ec86ae39a581cb4704e61de5");
        nDefaultPort = 17209;
        nEnforceBlockUpgradeMajority = 51;
        nRejectBlockOutdatedMajority = 75;
        nToCheckBlockUpgradeMajority = 100;
        nMinerThreads = 0;
        nTargetTimespan = 24 * 60 * 60; // Arctic: 1 day
        nTargetSpacing = 2.5 * 60; // Arctic: 2.5 minutes

        //! Modify the testnet genesis block so the timestamp is valid for a later start.
        genesis.nTime = 1437685201;
        genesis.nNonce = 747171;

        hashGenesisBlock = genesis.GetHash();
        assert(hashGenesisBlock == uint256("0x000007ddc82c93642da51870d1d549c6b91c44d290aad2b9d95e90f17ec2fc2b"));

        vFixedSeeds.clear();
        vSeeds.clear();
		
		#if __cplusplus > 199711L
			base58Prefixes[PUBKEY_ADDRESS] = {83};                    // Testnet arcticcoin addresses start with 'a' 
			base58Prefixes[SCRIPT_ADDRESS] = {9};                    // Testnet arcticcoin script addresses start with '4' or '5'
			base58Prefixes[SECRET_KEY]     = {239};                    // Testnet private keys start with '9' or 'c' (Bitcoin defaults)
			base58Prefixes[EXT_PUBLIC_KEY] = {0x09,0x72,0x98,0xBF}; // Testnet arcticcoin BIP32 pubkeys 
			base58Prefixes[EXT_SECRET_KEY] = {0x09,0x62,0x3A,0x6F}; // Testnet arcticcoin BIP32 prvkeys
			base58Prefixes[EXT_COIN_TYPE]  = {0x01,0x00,0x00,0x80};             // Testnet arcticcoin BIP44 coin type is '5' (All coin's testnet default)        
		#else
			base58Prefixes[PUBKEY_ADDRESS] = list_of(83);                    // Testnet arcticcoin addresses start with 'a' 
			base58Prefixes[SCRIPT_ADDRESS] = list_of( 9);                    // Testnet arcticcoin script addresses start with '4' or '5'
			base58Prefixes[SECRET_KEY]     = list_of(239);                    // Testnet private keys start with '9' or 'c' (Bitcoin defaults)
			base58Prefixes[EXT_PUBLIC_KEY] = list_of(0x09)(0x72)(0x98)(0xBF); // Testnet arcticcoin BIP32 pubkeys 
			base58Prefixes[EXT_SECRET_KEY] = list_of(0x09)(0x62)(0x3A)(0x6F); // Testnet arcticcoin BIP32 prvkeys
			base58Prefixes[EXT_COIN_TYPE]  = list_of(0x80000001);             // Testnet arcticcoin BIP44 coin type is '5' (All coin's testnet default)
		#endif
			
        convertSeed6(vFixedSeeds, pnSeed6_test, ARRAYLEN(pnSeed6_test));

        fRequireRPCPassword = true;
        fMiningRequiresPeers = true;
        fAllowMinDifficultyBlocks = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = true;

        nPoolMaxTransactions = 2;
        strSporkKey = "04d65cdbb409be1be32829855bd43a150666f731f422850ca1b101e56f4f1dcd3109402fd7b9068fc39d77666d4338982d9f30c4997ce948c4e0c770f99b7fc95b";
        strMasternodePaymentsPubKey = "04d65cdbb409be1be32829855bd43a150666f731f422850ca1b101e56f4f1dcd3109402fd7b9068fc39d77666d4338982d9f30c4997ce948c4e0c770f99b7fc95b";
        strDarksendPoolDummyAddress = "aeZBmkcHw3DyBJvfGn3TQ8kYVoPRRzaT4f";
        nStartMasternodePayments = 1471208401; //15.08.2016 @ 00:00:01 MSK (GTM +03:00)
    }
    const Checkpoints::CCheckpointData& Checkpoints() const 
    {
        return dataTestnet;
    }
};
static CTestNetParams testNetParams;

/**
 * Regression test
 */
class CRegTestParams : public CTestNetParams {
public:
    CRegTestParams() {
        networkID = CBaseChainParams::REGTEST;
        strNetworkID = "regtest";
        pchMessageStart[0] = 0x3a;
        pchMessageStart[1] = 0x3c;
        pchMessageStart[2] = 0x3c;
        pchMessageStart[3] = 0x3d;
        nSubsidyHalvingInterval = 150;
        nEnforceBlockUpgradeMajority = 750;
        nRejectBlockOutdatedMajority = 950;
        nToCheckBlockUpgradeMajority = 1000;
        nMinerThreads = 1;
        nTargetTimespan = 24 * 60 * 60; // Arctic: 1 day
        nTargetSpacing = 2.5 * 60; // Arctic: 2.5 minutes
        bnProofOfWorkLimit = ~uint256(0) >> 1;
        genesis.nTime = 1437685202;
        genesis.nBits = 0x207fffff;
        genesis.nNonce = 0;
        hashGenesisBlock = genesis.GetHash();
        nDefaultPort = 17203;
        assert(hashGenesisBlock == uint256("0x62890b2e5ba197afc37d17fea3ffb3e207034a8290080cd3100976c36d14ff1a"));

        vFixedSeeds.clear(); //! Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();  //! Regtest mode doesn't have any DNS seeds.

        fRequireRPCPassword = false;
        fMiningRequiresPeers = false;
        fAllowMinDifficultyBlocks = true;
        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;
        fTestnetToBeDeprecatedFieldRPC = false;
    }
    const Checkpoints::CCheckpointData& Checkpoints() const 
    {
        return dataRegtest;
    }
};
static CRegTestParams regTestParams;

/**
 * Unit test
 */
class CUnitTestParams : public CMainParams, public CModifiableParams {
public:
    CUnitTestParams() {
        networkID = CBaseChainParams::UNITTEST;
        strNetworkID = "unittest";
        nDefaultPort = 17334;
        vFixedSeeds.clear(); //! Unit test mode doesn't have any fixed seeds.
        vSeeds.clear();  //! Unit test mode doesn't have any DNS seeds.

        fRequireRPCPassword = false;
        fMiningRequiresPeers = false;
        fDefaultConsistencyChecks = true;
        fAllowMinDifficultyBlocks = false;
        fMineBlocksOnDemand = true;
    }

    const Checkpoints::CCheckpointData& Checkpoints() const 
    {
        // UnitTest share the same checkpoints as MAIN
        return data;
    }

    //! Published setters to allow changing values in unit test cases
    virtual void setSubsidyHalvingInterval(int anSubsidyHalvingInterval)  { nSubsidyHalvingInterval=anSubsidyHalvingInterval; }
    virtual void setEnforceBlockUpgradeMajority(int anEnforceBlockUpgradeMajority)  { nEnforceBlockUpgradeMajority=anEnforceBlockUpgradeMajority; }
    virtual void setRejectBlockOutdatedMajority(int anRejectBlockOutdatedMajority)  { nRejectBlockOutdatedMajority=anRejectBlockOutdatedMajority; }
    virtual void setToCheckBlockUpgradeMajority(int anToCheckBlockUpgradeMajority)  { nToCheckBlockUpgradeMajority=anToCheckBlockUpgradeMajority; }
    virtual void setDefaultConsistencyChecks(bool afDefaultConsistencyChecks)  { fDefaultConsistencyChecks=afDefaultConsistencyChecks; }
    virtual void setAllowMinDifficultyBlocks(bool afAllowMinDifficultyBlocks) {  fAllowMinDifficultyBlocks=afAllowMinDifficultyBlocks; }
    virtual void setSkipProofOfWorkCheck(bool afSkipProofOfWorkCheck) { fSkipProofOfWorkCheck = afSkipProofOfWorkCheck; }
};
static CUnitTestParams unitTestParams;


static CChainParams *pCurrentParams = 0;

CModifiableParams *ModifiableParams()
{
   assert(pCurrentParams);
   assert(pCurrentParams==&unitTestParams);
   return (CModifiableParams*)&unitTestParams;
}

const CChainParams &Params() {
    assert(pCurrentParams);
    return *pCurrentParams;
}

CChainParams &Params(CBaseChainParams::Network network) {
    switch (network) {
        case CBaseChainParams::MAIN:
            return mainParams;
        case CBaseChainParams::TESTNET:
            return testNetParams;
        case CBaseChainParams::REGTEST:
            return regTestParams;
        case CBaseChainParams::UNITTEST:
            return unitTestParams;
        default:
            assert(false && "Unimplemented network");
            return mainParams;
    }
}

void SelectParams(CBaseChainParams::Network network) {
    SelectBaseParams(network);
    pCurrentParams = &Params(network);
}

bool SelectParamsFromCommandLine()
{
    CBaseChainParams::Network network = NetworkIdFromCommandLine();
    if (network == CBaseChainParams::MAX_NETWORK_TYPES)
        return false;

    SelectParams(network);
    return true;
}
