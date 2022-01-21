#ifndef TWILIGHT_COMMON_CERTHASH_H
#define TWILIGHT_COMMON_CERTHASH_H

#include "common/ByteBuffer.h"

#include <string>

/*
 * @summary A container for storing a hash of a X.509 certificate
 *
 * This class is used in place of ByteBuffer to support various hash types
 */
class CertHash {
public:
    enum class HashType : int { INVALID = -1, RAW = 0, SHA256 };

    CertHash() : type(HashType::INVALID) {}
    CertHash(const CertHash& copy) : type(copy.type), hash(copy.hash.clone()) {}
    CertHash(CertHash&& move) = default;

    CertHash& operator=(const CertHash& copy);
    CertHash& operator=(CertHash&& move) = default;

    ~CertHash() = default;

    static CertHash digest(HashType type, const ByteBuffer& pubkey);
    static CertHash fromRepr(const std::string& repr);

    bool isValid() const { return type != HashType::INVALID; }

    std::string getRepr() const;

    bool compare(const ByteBuffer& cert) const;

private:
    CertHash(HashType type_, ByteBuffer&& hash_) : type(type_), hash(std::move(hash_)) {}

    HashType type;
    ByteBuffer hash;
};

#endif