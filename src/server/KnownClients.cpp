#include "KnownClients.h"

#include <fstream>

KnownClients::KnownClients() : log(createNamedLogger("KnownClients")) {}

// TODO: Some functions are duplicated from HostList.cpp
//       It might be good to deduplicate them (or get a proper serialization library)
bool KnownClients::loadFile(const char* filename) {
    try {
        auto root = toml::parse(filename);
        if (root["clients"].is_array())
            load_(root["clients"].as_array());
        else if (!root["clients"].is_uninitialized())  // Warn if not empty; An empty file IS valid
            log->warn("`clients` is not an array. Is it a valid file? --> {}", filename);
    } catch (std::runtime_error err) {
        log->warn("Failed to deserialize from file: {}", err.what());
        return false;
    } catch (toml::syntax_error err) {
        log->warn("Failed to deserialize from file: {}", err.what());
        return false;
    }

    return true;
}

bool KnownClients::saveFile(const char* filename) {
    std::ofstream fout(filename, std::ios_base::binary | std::ios_base::trunc);
    if (!fout.good())
        return false;

    bool good = save_(fout);
    fout.close();

    return good;
}

void KnownClients::load_(toml::array& arr) {
    clients.clear();

    for (toml::value& now : arr) {
        if (now["fingerprint"].is_string()) {
            CertHash hash = CertHash::fromRepr(now["fingerprint"].as_string());
            if (hash.isValid())
                clients.push_back(std::move(hash));
        }
    }
}

bool KnownClients::save_(std::ostream& out) {
    for (const auto& now : clients) {
        out << std::setw(0) << "[[clients]]\n";
        out << std::setw(80) << toml::value(toml::table({{"fingerprint", now.getRepr()}})) << '\n';
        out << std::setw(0) << '\n';
    }

    return true;
}