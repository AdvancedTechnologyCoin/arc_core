// Copyright (c) 2015-2016 The Arctic developers

// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef GOLDMINE_SYNC_H
#define GOLDMINE_SYNC_H

#define GOLDMINE_SYNC_INITIAL           0
#define GOLDMINE_SYNC_SPORKS            1
#define GOLDMINE_SYNC_LIST              2
#define GOLDMINE_SYNC_GMW               3
#define GOLDMINE_SYNC_EVOLUTION            4
#define GOLDMINE_SYNC_EVOLUTION_PROP       10
#define GOLDMINE_SYNC_EVOLUTION_FIN        11
#define GOLDMINE_SYNC_FAILED            998
#define GOLDMINE_SYNC_FINISHED          999

#define GOLDMINE_SYNC_TIMEOUT           5
#define GOLDMINE_SYNC_THRESHOLD         2

class CGoldmineSync;
extern CGoldmineSync goldmineSync;

//
// CGoldmineSync : Sync goldmine assets in stages
//

class CGoldmineSync
{
public:
    std::map<uint256, int> mapSeenSyncMNB;
    std::map<uint256, int> mapSeenSyncGMW;
    std::map<uint256, int> mapSeenSyncEvolution;

    int64_t lastGoldmineList;
    int64_t lastGoldmineWinner;
    int64_t lastEvolutionItem;
    int64_t lastFailure;
    int nCountFailures;

    // sum of all counts
    int sumGoldmineList;
    int sumGoldmineWinner;
    int sumEvolutionItemProp;
    int sumEvolutionItemFin;
    // peers that reported counts
    int countGoldmineList;
    int countGoldmineWinner;
    int countEvolutionItemProp;
    int countEvolutionItemFin;

    // Count peers we've requested the list from
    int RequestedGoldmineAssets;
    int RequestedGoldmineAttempt;

    // Time when current goldmine asset sync started
    int64_t nAssetSyncStarted;

    CGoldmineSync();

    void AddedGoldmineList(uint256 hash);
    void AddedGoldmineWinner(uint256 hash);
    void AddedEvolutionItem(uint256 hash);
    void GetNextAsset();
    std::string GetSyncStatus();
    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    bool IsEvolutionFinEmpty();
    bool IsEvolutionPropEmpty();

    void Reset();
    void Process();
    bool IsSynced();
    bool IsBlockchainSynced();
    void ClearFulfilledRequest();
};

#endif
