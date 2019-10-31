#start-many Setup Guide

## Two Options for Setting up your Wallet
There are many ways to setup a wallet to support start-many. This guide will walk through two of them.

1. [Importing an existing wallet (recommended if you are consolidating wallets).](#option1)
2. [Sending 1000 ARC to new wallet addresses.](#option2)

## <a name="option1"></a>Option 1. Importing an existing wallet

This is the way to go if you are consolidating multiple wallets into one that supports start-many. 

### From your single-instance Goldminenode Wallet

Open your QT Wallet and go to console (from the menu select `Tools` => `Debug Console`)

Dump the private key from your GoldmineNode's pulic key.

```
walletpassphrase [your_wallet_passphrase] 600
dumpprivkey [mn_public_key]
```

Copy the resulting priviate key. You'll use it in the next step.

### From your multi-instance Goldminenode Wallet

Open your QT Wallet and go to console (from the menu select `Tools` => `Debug Console`)

Import the private key from the step above.

```
walletpassphrase [your_wallet_passphrase] 600
importprivkey [single_instance_private_key]
```

The wallet will re-scan and you will see your available balance increase by the amount that was in the imported wallet.

[Skip Option 2. and go to Create goldminenode.conf file](#goldminenodeconf)

## <a name="option2"></a>Option 2. Starting with a new wallet

[If you used Option 1 above, then you can skip down to Create goldminenode.conf file.](#goldminenodeconf)

### Create New Wallet Addresses

1. Open the QT Wallet.
2. Click the Receive tab.
3. Fill in the form to request a payment.
    * Label: mn01
    * Amount: 1000 (optional)
    * Click *Request payment* button
5. Click the *Copy Address* button

Create a new wallet address for each Goldminenode.

Close your QT Wallet.

### Send 1000 ARC to New Addresses

Just like setting up a standard MN. Send exactly 1000 ARC to each new address created above.

### Create New Goldminenode Private Keys

Open your QT Wallet and go to console (from the menu select `Tools` => `Debug Console`)

Issue the following:

```goldminenode genkey```

*Note: A goldminenode private key will need to be created for each Goldminenode you run. You should not use the same goldminenode private key for multiple Goldminenodes.*

Close your QT Wallet.

## <a name="goldminenodeconf"></a>Create goldminenode.conf file

Remember... this is local. Make sure your QT is not running.

Create the `goldminenode.conf` file in the same directory as your `wallet.dat`.

Copy the goldminenode private key and correspondig collateral output transaction that holds the 1000 ARC.

The goldminenode private key may be an existing key from [Option 1](#option1), or a newly generated key from [Option 2](#option2). 

*Note: The goldminenode priviate key is **not** the same as a wallet private key. **Never** put your wallet private key in the goldminenode.conf file. That is almost equivalent to putting your 1000 ARC on the remote server and defeats the purpose of a hot/cold setup.*

### Get the collateral output

Open your QT Wallet and go to console (from the menu select `Tools` => `Debug Console`)

Issue the following:

```goldminenode outputs```

Make note of the hash (which is your collateral_output) and index.

### Enter your Goldminenode details into your goldminenode.conf file
[From the arc github repo](https://github.com/ArcticCore/arc/blob/goldmine/doc/goldminenode_conf.md)

`goldminenode.conf` format is a space seperated text file. Each line consisting of an alias, IP address followed by port, goldminenode private key, collateral output transaction id and collateral output index.

```
alias ipaddress:port goldminenode_private_key collateral_output collateral_output_index
```

Example:

```
gm01 127.0.0.1:7209 93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg 2bcd3c84c84f87eaa86e4e56834c92927a07f9e18718810b92e0d0324456a67c 0
gm02 127.0.0.2:7209 93WaAb3htPJEV8E9aQcN23Jt97bPex7YvWfgMDTUdWJvzmrMqey aa9f1034d973377a5e733272c3d0eced1de22555ad45d6b24abadff8087948d4 0
```

## What about the arc.conf file?

If you are using a `goldminenode.conf` file you no longer need the `arc.conf` file. The exception is if you need custom settings (_thanks oblox_). In that case you **must** remove `goldminenode=1` from local `arc.conf` file. This option should be used only to start local Hot goldminenode now.

## Update arc.conf on server

If you generated a new goldminenode private key, you will need to update the remote `arc.conf` files.

Shut down the daemon and then edit the file.

```nano .arc/arc.conf```

### Edit the goldminenodeprivkey
If you generated a new goldminenode private key, you will need to update the `goldminenodeprivkey` value in your remote `arc.conf` file.

## Start your Goldminenodes

### Remote

If your remote server is not running, start your remote daemon as you normally would. 

You can confirm that remote server is on the correct block by issuing

```arc-cli getinfo```

and comparing with the official explorer at https://explorer.arc.org/

### Local

Finally... time to start from local.

#### Open up your QT Wallet

From the menu select `Tools` => `Debug Console`

If you want to review your `goldminenode.conf` setting before starting Goldminenodes, issue the following in the Debug Console:

```goldminenode list-conf```

Give it the eye-ball test. If satisfied, you can start your Goldminenodes one of two ways.

1. `goldminenode start-alias [alias_from_goldminenode.conf]`  
Example ```goldminenode start-alias mn01```
2. `goldminenode start-many`

## Verify that Goldminenodes actually started

### Remote

Issue command `goldminenode status`
It should return you something like that:
```
arc-cli goldminenode status
{
    "vin" : "CTxIn(COutPoint(<collateral_output>, <collateral_output_index>), scriptSig=)",
    "service" : "<ipaddress>:<port>",
    "pubkey" : "<1000 ARC address>",
    "status" : "Goldminenode successfully started"
}
```
Command output should have "_Goldminenode successfully started_" in its `status` field now. If it says "_not capable_" instead, you should check your config again.

### Local

Search your Goldminenodes on https://arc.org/goldminenodes.html

_Hint: Bookmark it, you definitely will be using this site a lot._
