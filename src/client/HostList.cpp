#include "HostList.h"

#include <mbedtls/pem.h>

#include <fstream>

HostList::Entry::Entry() : lastConnected(std::chrono::system_clock::from_time_t(0)) {
    mbedtls_x509_crt_init(&serverCert);
    mbedtls_x509_crt_init(&clientCert);
}

HostList::Entry::~Entry() {
    mbedtls_x509_crt_free(&serverCert);
    mbedtls_x509_crt_free(&clientCert);
}

bool HostList::Entry::hasConnected() const {
    return lastConnected.time_since_epoch().count() != 0;
}

bool HostList::Entry::hasServerCert() const {
    return serverCert.raw.p != nullptr;
}

bool HostList::Entry::hasClientCert() const {
    return clientCert.raw.p != nullptr;
}

void HostList::Entry::updateLastConnected() {
    lastConnected = std::chrono::system_clock::now();
}

HostList::HostList() : log(createNamedLogger("HostList")) {}

bool HostList::save(std::ostream &out) {
    for (const std::shared_ptr<Entry> &now : hosts) {
        out << std::setw(0) << "[[hosts]]\n";
        out << std::setw(80) << toml::value(toml::table({{"nickname", now->nickname}}));
        out << std::setw(80) << toml::value(toml::table({{"addr", now->addr}}));
        if (now->hasConnected())
            out << std::setw(0) << "last_connected = " << toml::value(now->lastConnected) << '\n';

        int ret;
        char buffer[8192];
        size_t olen;
        if (now->hasServerCert()) {
            ret = mbedtls_pem_write_buffer("-----BEGIN CERTIFICATE-----\n", "-----END CERTIFICATE-----\n",
                                           now->serverCert.raw.p, now->serverCert.raw.len, (unsigned char *)buffer,
                                           8191, &olen);
            if (ret == 0) {
                buffer[olen] = '\0';
                out << std::setw(0) << R"(server_cert = """)" << '\n' << buffer << R"(""")" << '\n';
            }
        }

        if (now->hasClientCert()) {
            ret = mbedtls_pem_write_buffer("-----BEGIN CERTIFICATE-----\n", "-----END CERTIFICATE-----\n",
                                           now->clientCert.raw.p, now->clientCert.raw.len, (unsigned char *)buffer,
                                           8191, &olen);
            if (ret == 0) {
                buffer[olen] = '\0';
                out << std::setw(0) << R"(client_cert = """)" << '\n' << buffer << R"(""")" << '\n';
            }
        }

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

        if (dir["server_cert"].is_string()) {
            std::string &pem = dir["server_cert"].as_string();
            int ret = mbedtls_x509_crt_parse(&entry->serverCert, reinterpret_cast<const unsigned char *>(pem.c_str()),
                                             pem.size() + 1);
            if (ret != 0)
                warned = warnInvalidCert = true;
        }

        if (dir["client_cert"].is_string()) {
            std::string &pem = dir["client_cert"].as_string();
            int ret = mbedtls_x509_crt_parse(&entry->clientCert, reinterpret_cast<const unsigned char *>(pem.c_str()),
                                             pem.size() + 1);
            if (ret != 0)
                warned = warnInvalidCert = true;
        }

        if (!entry->addr.empty())
            hosts.push_back(std::move(entry));
        else
            warned = warnNoAddress = true;
    }

    if (warnNotTable)
        log->warn("Error while parsing: An entry in `hosts` is not a table.");

    if (warnNoAddress)
        log->warn("Error while parsing: An entry in `hosts` was dropped because it had no address.");

    if (warnInvalidCert)
        log->warn("Error while parsing: An entry in `hosts` had invalid certificate.");

    return !warned;
}

bool HostList::loadFromFile(const char *filename) {
    try {
        auto root = toml::parse(filename);
        if (root["hosts"].is_array())
            load(root["hosts"].as_array());
        else if (!root["hosts"].is_uninitialized())  // Warn if not empty; An empty file IS valid
            log->warn("`hosts` is not an array. Is it a valid file? --> {}", filename);
    } catch (std::runtime_error err) {
        log->warn("Failed to deserialize from file: {}", err.what());
        return false;
    } catch (toml::syntax_error err) {
        log->warn("Failed to deserialize from file: {}", err.what());
        return false;
    }

    return true;
}
