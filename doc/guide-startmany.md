# start-many Setup Guide

## Setting up your Wallet

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

Send exactly 1000 ARC to each new address created above.

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

*Note: The goldminenode priviate key is **not** the same as a wallet private key. **Never** put your wallet private key in the goldminenode.conf file. That is almost equivalent to putting your 1000 ARC on the remote server and defeats the purpose of a hot/cold setup.*

### Get the collateral output

Open your QT Wallet and go to console (from the menu select `Tools` => `Debug Console`)

Issue the following:

```goldminenode outputs```

Make note of the hash (which is your collateral_output) and index.

### Enter your Goldminenode details into your goldminenode.conf file
[From the arc github repo](https://github.com/AdvancedTechnologyCoin/arc_core/blob/master/doc/goldminenode_conf.md)

`goldminenode.conf` format is a space seperated text file. Each line consisting of an alias, IP address followed by port, goldminenode private key, collateral output transaction id and collateral output index.

```
alias ipaddress:port goldminenode_private_key collateral_output collateral_output_index
```

Example:

```
mn01 127.0.0.1:7209 93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg 2bcd3c84c84f87eaa86e4e56834c92927a07f9e18718810b92e0d0324456a67c 0
mn02 127.0.0.2:7209 93WaAb3htPJEV8E9aQcN23Jt97bPex7YvWfgMDTUdWJvzmrMqey aa9f1034d973377a5e733272c3d0eced1de22555ad45d6b24abadff8087948d4 0
```

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

and comparing with the official explorer at https://explorer.arc.org/chain/Arc

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
    "outpoint" : "<collateral_output>-<collateral_output_index>",
    "service" : "<ipaddress>:<port>",
    "pubkey" : "<1000 ARC address>",
    "status" : "Goldminenode successfully started"
}
```
Command output should have "_Goldminenode successfully started_" in its `status` field now. If it says "_not capable_" instead, you should check your config again.

### Local

Search your Goldminenodes on https://arcninja.pl/goldminenodes.html

_Hint: Bookmark it, you definitely will be using this site a lot._
