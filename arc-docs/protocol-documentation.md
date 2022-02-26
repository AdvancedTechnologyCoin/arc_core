Protocol Documentation - 0.12.1
=====================================

This document describes the protocol extensions for all additional functionality build into the Arc protocol. This doesn't include any of the Bitcoin protocol, which has been left intact in the Arc project. For more information about the core protocol, please see https://en.bitcoin.it/w/index.php?title#Protocol_documentation&action#edit

## Common Structures

### Simple types

uint256  => char[32]

CScript => uchar[]

### COutPoint

Bitcoin Outpoint https://bitcoin.org/en/glossary/outpoint

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | ---------- |
| 32 | hash | uint256 | Hash of transactional output which is being referenced
| 4 | n | uint32_t | Index of transaction which is being referenced


### CTxIn

Bitcoin Input https://bitcoin.org/en/glossary/input

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | ---------- |
| 36 | prevout | [COutPoint](#coutpoint) | The previous output from an existing transaction, in the form of an unspent output
| 1+ | script length | var_int | The length of the signature script
| ? | script | CScript | The script which is validated for this input to be spent
| 4 | nSequence | uint_32t | Transaction version as defined by the sender. Intended for "replacement" of transactions when information is updated before inclusion into a block.

### CTxOut

Bitcoin Output https://bitcoin.org/en/glossary/output

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | ---------- |
| 8 | nValue | int64_t | Transfered value
| ? | scriptPubKey | CScript | The script for indicating what conditions must be fulfilled for this output to be further spent

### CTransaction

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | ---------- |
| 4 | nVersion | int32_t | Transaction data format version
| 1+ | tx_in count | var_int | Number of Transaction inputs
| 41+ | vin | [CTxIn](#ctxin)[] | A list of 1 or more transaction inputs
| 1+ | tx_out count | var_int | Number of Transaction outputs
| 9+ | vout | [CTxOut](#ctxout)[] | A list of 1 or more transaction outputs
| 4 | nLockTime | uint32_t | The block number or timestamp at which this transaction is unlocked

### CPubKey

Bitcoin Public Key https://bitcoin.org/en/glossary/public-key

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | ---------- |
| 33-65 | vch | char[] | The public portion of a keypair which can be used to verify signatures made with the private portion of the keypair.

### CService

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | ---------- |
| 16 | IP | CNetAddr | IP Address
| 2 | Port | uint16 | IP Port

## Message Types

### MNANNOUNCE - "mnb"

CGoldminenodeBroadcast

Whenever a goldminenode comes online or a client is syncing, they will send this message which describes the goldminenode entry and how to validate messages from it.

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | ---------- |
| 36 | outpoint | [COutPoint](#coutpoint) | The unspent output which is holding 1000 ARC
| # | addr | [CService](#cservice) | IPv4 address of the goldminenode
| 33-65 | pubKeyCollateralAddress | [CPubKey](#cpubkey) | CPubKey of the main 1000 ARC unspent output
| 33-65 | pubKeyGoldminenode | [CPubKey](#cpubkey) | CPubKey of the secondary signing key (For all other messaging other than announce message)
| 71-73 | sig | char[] | Signature of this message (verifiable via pubKeyCollateralAddress)
| 8 | sigTime | int64_t | Time which the signature was created
| 4 | nProtocolVersion | int | The protocol version of the goldminenode
| # | lastPing | [CGoldminenodePing](#mnping---mnp) | The last known ping of the goldminenode

### MNPING - "mnp"

CGoldminenodePing

Every few minutes, goldminenodes ping the network with a message that propagates the whole network.

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | --------- |
| 36 | goldminenodeOutpoint | [COutPoint](#coutpoint) | The unspent output of the goldminenode which is signing the message
| 32 | blockHash | uint256 | Current chaintip blockhash minus 12
| 8 | sigTime | int64_t | Signature time for this ping
| 71-73 | vchSig | char[] | Signature of this message by goldminenode (verifiable via pubKeyGoldminenode)
| 1 | fSentinelIsCurrent | bool | true if last sentinel ping was current
| 4 | nSentinelVersion | uint32_t | The version of Sentinel running on the goldminenode which is signing the message
| 4 | nDaemonVersion | uint32_t | The version of arcd of the goldminenode which is signing the message (i.e. CLIENT_VERSION)

### GOLDMINENODEPAYMENTVOTE - "mnw"

CGoldminenodePaymentVote

When a new block is found on the network, a goldminenode quorum will be determined and those 10 selected goldminenodes will issue a goldminenode payment vote message to pick the next winning node.

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | ---------- |
| 36 | goldminenodeOutpoint | [COutPoint](#coutpoint) | The unspent output of the goldminenode which is signing the message
| 4 | nBlockHeight | int | The blockheight which the payee should be paid
| ? | payeeAddress | CScript | The address to pay to
| 71-73 | sig | char[] | Signature of the goldminenode which is signing the message

### DSTX - "dstx"

CDarksendBroadcastTx

Goldminenodes can broadcast subsidised transactions without fees for the sake of security in mixing. This is done via the DSTX message.

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | ---------- |
| # | tx | [CTransaction](#ctransaction) | The transaction
| 36 | goldminenodeOutpoint | [COutPoint](#coutpoint) | The unspent output of the goldminenode which is signing the message
| 71-73 | vchSig | char[] | Signature of this message by goldminenode (verifiable via pubKeyGoldminenode)
| 8 | sigTime | int64_t | Time this message was signed

### DSSTATUSUPDATE - "dssu"

Mixing pool status update

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | ---------- |
| 4 | nMsgSessionID | int | Session ID
| 4 | nMsgState | int | Current state of mixing process
| 4 | nMsgEntriesCount | int | Number of entries in the mixing pool
| 4 | nMsgStatusUpdate | int | Update state and/or signal if entry was accepted or not
| 4 | nMsgMessageID | int | ID of the typical goldminenode reply message

### DSQUEUE - "dsq"

CDarksendQueue

Asks users to sign final mixing tx message.

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | ---------- |
| 4 | nDenom | int | Which denomination is allowed in this mixing session
| 36 | goldminenodeOutpoint | [COutPoint](#coutpoint) | The unspent output of the goldminenode which is hosting this session
| 8 | nTime | int64_t | the time this DSQ was created
| 1 | fReady | bool | if the mixing pool is ready to be executed
| 66 | vchSig | char[] | Signature of this message by goldminenode (verifiable via pubKeyGoldminenode)

### DSACCEPT - "dsa"

Response to DSQ message which allows the user to join a mixing pool

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | ---------- |
| 4 | nDenom | int | denomination that will be exclusively used when submitting inputs into the pool
| 216+ | txCollateral | [CTransaction](#ctransaction) | collateral tx that will be charged if this client acts maliciously

### DSVIN - "dsi"

CDarkSendEntry

When queue is ready user is expected to send his entry to start actual mixing

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | ---------- |
| ? | vecTxDSIn | CTxDSIn[] | vector of users inputs (CTxDSIn serialization is equal to [CTxIn](#ctxin) serialization)
| 216+ | txCollateral | [CTransaction](#ctransaction) | Collateral transaction which is used to prevent misbehavior and also to charge fees randomly
| ? | vecTxOut | [CTxOut](#ctxout)[] | vector of user outputs

### DSSIGNFINALTX - "dss"

User's signed inputs for a group transaction in a mixing session

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | ---------- |
| # | inputs | [CTxIn](#ctxin)[] | signed inputs for mixing session


### TXLOCKREQUEST - "ix"

CTxLockRequest

Transaction Lock Request, serialization is the same as for [CTransaction](#ctransaction).

### TXLOCKVOTE - "txlvote"

CTxLockVote

Transaction Lock Vote

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | ---------- |
| 32 | txHash | uint256 | txid of the transaction to lock
| 36 | outpoint | [COutPoint](#coutpoint) | The utxo to lock in this transaction
| 36 | outpointGoldminenode | [COutPoint](#coutpoint) | The utxo of the goldminenode which is signing the vote
| 71-73 | vchGoldminenodeSignature | char[] | Signature of this message by goldminenode (verifiable via pubKeyGoldminenode)

### MNGOVERNANCEOBJECT - "govobj"

Governance Object

A proposal, contract or setting.

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | ---------- |
| 32 | nHashParent | uint256 | Parent object, 0 is root
| 4 | nRevision | int | Object revision in the system
| 8 | nTime | int64_t | Time which this object was created
| 32 | nCollateralHash | uint256 | Hash of the collateral fee transaction
| 0-16384 | strData | string | Data field - can be used for anything
| 4 | nObjectType | int | ????
| 36 | goldminenodeOutpoint | [COutPoint](#coutpoint) | The unspent output of the goldminenode which is signing this object
| 66* | vchSig | char[] | Signature of the goldminenode (unclear if 66 is the correct size, but this is what it appears to be in most cases)

### MNGOVERNANCEOBJECTVOTE - "govobjvote"

Governance Vote

Goldminenodes use governance voting in response to new proposals, contracts, settings or finalized budgets.

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | ---------- |
| 36 | goldminenodeOutpoint | [COutPoint](#coutpoint) | The unspent output of the goldminenode which is voting
| 32 | nParentHash | uint256 | Object which we're voting on (proposal, contract, setting or final budget)
| 4 | nVoteOutcome | int | ???
| 4 | nVoteSignal | int | ???
| 8 | nTime | int64_t | Time which the vote was created
| 66* | vchSig | char[] | Signature of the goldminenode (unclear if 66 is the correct size, but this is what it appears to be in most cases)

### SPORK - "spork"

Spork

Spork

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | ---------- |
| 4 | nSporkID | int | |
| 8 | nValue | int64_t | |
| 8 | nTimeSigned | int64_t | |
| 66* | vchSig | char[] | Unclear if 66 is the correct size, but this is what it appears to be in most cases |

#### Defined Sporks (per src/sporks.h)
 
| Spork ID | Number | Name | Description | 
| ---------- | ---------- | ----------- | ----------- |
| 10001 | 2 | INSTANTSEND_ENABLED | Turns on and off InstantSend network wide
| 10002 | 3 | INSTANTSEND_BLOCK_FILTERING | Turns on and off InstantSend block filtering
| 10004 | 5 | INSTANTSEND_MAX_VALUE | Controls the max value for an InstantSend transaction (currently 2000 arc)
| 10005 | 6 | NEW_SIGS | Turns on and off new signature format for Arc-specific messages
| 10007 | 8 | GOLDMINENODE_PAYMENT_ENFORCEMENT | Requires goldminenodes to be paid by miners when blocks are processed
| 10008 | 9 | SUPERBLOCKS_ENABLED | Superblocks are enabled (the 10% comes to fund the arc treasury)
| 10009 | 10 | GOLDMINENODE_PAY_UPDATED_NODES | Only current protocol version goldminenode's will be paid (not older nodes)
| 10011 | 12 | RECONSIDER_BLOCKS | |
| 10012 | 13 | OLD_SUPERBLOCK_FLAG | |
| 10013 | 14 | REQUIRE_SENTINEL_FLAG | Only goldminenode's running sentinel will be paid 

## Undocumented messages

### GOLDMINENODEPAYMENTBLOCK - "mnwb"

Goldminenode Payment Block

*NOTE: Per src/protocol.cpp, there is no message for this (only inventory)*

### MNVERIFY - "mnv"

Goldminenode Verify

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | ---------- |
| 36 | goldminenodeOutpoint1 | [COutPoint](#coutpoint) | The unspent output which is holding 1000 ARC for goldminenode 1
| 36 | goldminenodeOutpoint2 | [COutPoint](#coutpoint) | The unspent output which is holding 1000 ARC for goldminenode 2
| # | addr | [CService](#cservice) | IPv4 address / port of the goldminenode
| 4 | nonce | int | Nonce
| 4 | nBlockHeight | int | The blockheight
| 66* | vchSig1 | char[] | Signature of by goldminenode 1 (unclear if 66 is the correct size, but this is what it appears to be in most cases)
| 66* | vchSig2 | char[] | Signature of by goldminenode 2 (unclear if 66 is the correct size, but this is what it appears to be in most cases)

### DSFINALTX - "dsf"

Darksend Final Transaction

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | ---------- |
| 4 | nSessionID | int | |
| # | txFinal | [CTransaction](#ctransaction) | Final mixing transaction

### DSCOMPLETE - "dsc"

Darksend Complete

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | ---------- |
| 4 | nSessionID | int | |
| 4 | nMessageID | int | |

### MNGOVERNANCESYNC - "govsync"

Governance Sync

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | ---------- |
| 32 | nHash | uint256 | |
| # | filter | CBloomFilter | |

### DSEG - "dseg"

Goldminenode List/Entry Sync

Get Goldminenode list or specific entry

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | ---------- |
| 36 | goldminenodeOutpoint | [COutPoint](#coutpoint) | The unspent output which is holding 1000 ARC

### SYNCSTATUSCOUNT - "ssc"

Sync Status Count

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | ---------- |
| 4 | nItemID | int | Goldminenode Sync Item ID
| 4 | nCount | int | Goldminenode Sync Count

#### Defined Sync Item IDs (per src/goldminenode-sync.h)

| Item ID | Name | Description |
| ---------- | ---------- | ----------- |
| 2 | GOLDMINENODE_SYNC_LIST | |
| 3 | GOLDMINENODE_SYNC_MNW | |
| 4 | GOLDMINENODE_SYNC_GOVERNANCE | |
| 10 | GOLDMINENODE_SYNC_GOVOBJ | |
| 11 | GOLDMINENODE_SYNC_GOVOBJ_VOTE | |

### GOLDMINENODEPAYMENTSYNC - "mnget"

Goldminenode Payment Sync

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | ---------- |
| 4 | nMnCount | int | | (DEPRECATED)

*NOTE: There are no fields in this mesasge starting from protocol 70209*
