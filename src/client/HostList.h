#ifndef TWILIGHT_CLIENT_HOSTLIST_H
#define TWILIGHT_CLIENT_HOSTLIST_H

#include "common/CertHash.h"

#include <mbedtls/pk.h>

#include <chrono>
#include <memory>
#include <string>
#include <toml.hpp>
#include <vector>

#include "common/log.h"

class HostList {
public:
    struct Entry {
        std::string nickname;                                 //< Display name
        std::vector<std::string> addr;                        //< Addresses in preference order
        std::chrono::system_clock::time_point lastConnected;  //< Last connected time
        CertHash certHash;                                    //< Hash of server certificate

        Entry();
        Entry(const Entry &copy) = delete;
        Entry(Entry &&move) = delete;
        ~Entry();

        bool hasConnected() const;
        void updateLastConnected();
    };

    HostList();
    HostList(const HostList &copy) = delete;
    HostList(HostList &&move) = default;

    bool load(toml::array hosts);
    bool loadFromFile(const char *filename);

    bool save(std::ostream &out);
    bool saveToFile(const char *filename);

    std::vector<std::shared_ptr<Entry>> hosts;

private:
    LoggerPtr log;
};

using HostListEntry = std::shared_ptr<HostList::Entry>;

#endif