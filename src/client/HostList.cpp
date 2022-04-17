#include "HostList.h"

#include <fstream>

TWILIGHT_DEFINE_LOGGER(HostList);

HostList::Entry::Entry() : lastConnected(std::chrono::system_clock::from_time_t(0)) {}

HostList::Entry::~Entry() {}

bool HostList::Entry::hasConnected() const {
    return lastConnected.time_since_epoch().count() != 0;
}

void HostList::Entry::updateLastConnected() {
    lastConnected = std::chrono::system_clock::now();
}

HostList::HostList() {}

bool HostList::save(std::ostream &out) {
    for (const std::shared_ptr<Entry> &now : hosts) {
        out << std::setw(0) << "[[hosts]]\n";
        out << std::setw(80) << toml::value(toml::table({{"nickname", now->nickname}}));
        out << std::setw(80) << toml::value(toml::table({{"addr", now->addr}}));
        if (now->hasConnected())
            out << std::setw(0) << "last_connected = " << toml::value(now->lastConnected) << '\n';
        if (now->certHash.isValid())
            out << std::setw(0) << "cert = " << toml::value(now->certHash.getRepr()) << '\n';

        out << std::setw(0) << '\n';
    }

    return true;
}

bool HostList::saveToFile(const char *filename) {
    std::ofstream fout(filename, std::ios_base::binary | std::ios_base::trunc);
    if (!fout.good())
        return false;

    bool good = save(fout);
    fout.close();

    return good;
}

bool HostList::load(toml::array arr) {
    bool warned = false;
    bool warnNotTable = false;
    bool warnNoAddress = false;
    bool warnInvalidCert = false;

    hosts.reserve(arr.size());

    for (toml::value &now : arr) {
        if (!now.is_table()) {
            warned = warnNotTable = true;
            continue;
        }

        HostListEntry entry = std::make_shared<HostList::Entry>();

        auto &dir = now.as_table();
        if (dir["nickname"].is_string())
            entry->nickname = dir["nickname"].as_string();

        if (dir["addr"].is_array()) {
            for (auto &addr : dir["addr"].as_array()) {
                if (addr.is_string())
                    entry->addr.push_back(addr.as_string());
            }
        }

        if (dir["last_connected"].is_offset_datetime())
            entry->lastConnected = dir["last_connected"].as_offset_datetime();

        if (dir["cert"].is_string()) {
            std::string &repr = dir["cert"].as_string();
            entry->certHash = CertHash::fromRepr(repr);

            if (!entry->certHash.isValid())
                warned = warnInvalidCert = true;
        }

        if (!entry->addr.empty())
            hosts.push_back(std::move(entry));
        else
            warned = warnNoAddress = true;
    }

    if (warnNotTable)
        log.warn("Error while parsing: An entry in `hosts` is not a table.");

    if (warnNoAddress)
        log.warn("Error while parsing: An entry in `hosts` was dropped because it had no address.");

    if (warnInvalidCert)
        log.warn("Error while parsing: An entry in `hosts` had invalid certificate.");

    return !warned;
}

bool HostList::loadFromFile(const char *filename) {
    try {
        auto root = toml::parse(filename);
        if (root["hosts"].is_array())
            load(root["hosts"].as_array());
        else if (!root["hosts"].is_uninitialized())  // Warn if not empty; An empty file IS valid
            log.warn("`hosts` is not an array. Is it a valid file? --> {}", filename);
    } catch (std::runtime_error err) {
        log.warn("Failed to deserialize from file: {}", err.what());
        return false;
    } catch (toml::syntax_error err) {
        log.warn("Failed to deserialize from file: {}", err.what());
        return false;
    }

    return true;
}
