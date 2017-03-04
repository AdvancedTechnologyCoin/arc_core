
// Copyright (c) 2015-2017 The Arctic Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SRC_GOLDMINENODECONFIG_H_
#define SRC_GOLDMINENODECONFIG_H_

class CGoldminenodeConfig;
extern CGoldminenodeConfig goldminenodeConfig;

class CGoldminenodeConfig
{

public:

    class CGoldminenodeEntry {

    private:
        std::string alias;
        std::string ip;
        std::string privKey;
        std::string txHash;
        std::string outputIndex;
    public:

        CGoldminenodeEntry(std::string alias, std::string ip, std::string privKey, std::string txHash, std::string outputIndex) {
            this->alias = alias;
            this->ip = ip;
            this->privKey = privKey;
            this->txHash = txHash;
            this->outputIndex = outputIndex;
        }

        const std::string& getAlias() const {
            return alias;
        }

        void setAlias(const std::string& alias) {
            this->alias = alias;
        }

        const std::string& getOutputIndex() const {
            return outputIndex;
        }

        void setOutputIndex(const std::string& outputIndex) {
            this->outputIndex = outputIndex;
        }

        const std::string& getPrivKey() const {
            return privKey;
        }

        void setPrivKey(const std::string& privKey) {
            this->privKey = privKey;
        }

        const std::string& getTxHash() const {
            return txHash;
        }

        void setTxHash(const std::string& txHash) {
            this->txHash = txHash;
        }

        const std::string& getIp() const {
            return ip;
        }

        void setIp(const std::string& ip) {
            this->ip = ip;
        }
    };

    CGoldminenodeConfig() {
        entries = std::vector<CGoldminenodeEntry>();
    }

    void clear();
    bool read(std::string& strErr);
    void add(std::string alias, std::string ip, std::string privKey, std::string txHash, std::string outputIndex);

    std::vector<CGoldminenodeEntry>& getEntries() {
        return entries;
    }

    int getCount() {
        return (int)entries.size();
    }

private:
    std::vector<CGoldminenodeEntry> entries;


};


#endif /* SRC_GOLDMINENODECONFIG_H_ */
