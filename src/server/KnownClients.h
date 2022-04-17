#ifndef TWILIGHT_SERVER_KNOWNCLIENTS_H
#define TWILIGHT_SERVER_KNOWNCLIENTS_H

#include "common/CertHash.h"
#include "common/log.h"

#include <toml11/toml.hpp>

#include <ostream>
#include <vector>

class KnownClients {
public:
    KnownClients();
    ~KnownClients() = default;

    bool loadFile(const char* filename);
    bool saveFile(const char* filename);

    void add(const CertHash& hash) { clients.push_back(hash); }

    const std::vector<CertHash>& list() const { return clients; }

private:
    void load_(toml::array& arr);
    bool save_(std::ostream& out);

    static NamedLogger log;

    std::vector<CertHash> clients;
};

#endif
