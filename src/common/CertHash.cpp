#include "CertHash.h"

#include "common/util.h"

#include <mbedtls/sha256.h>

#include <mutex>
#include <unordered_map>

static ByteBuffer computeHash(CertHash::HashType type, const ByteBuffer& content) {
    ByteBuffer ret;
    int status;

    switch (type) {
    case CertHash::HashType::RAW:
        return content.clone();

    case CertHash::HashType::SHA256:
        ret.resize(32);
        status = mbedtls_sha256_ret(content.data(), content.size(), ret.data(), 0);
        if (status != 0)
            abort();  // FIXME: Use of abort
        break;

    default:
        abort();  // FIXME: Use of abort
    }

    return ret;
}

static ByteBuffer decodeBase64(std::string_view content) {
    ByteBuffer ret;
    const uint8_t* src = reinterpret_cast<const uint8_t*>(content.data());
    size_t olen = 0;
    int err;

    err = mbedtls_base64_decode(nullptr, 0, &olen, src, content.size());

    ret.resize(olen);
    err = mbedtls_base64_decode(ret.data(), olen, &olen, src, content.size());
    if (err != 0)
        NamedLogger("::decodeBase64").error_quit("Failed to call mbedtls_base64_decode");

    return ret;
}

CertHash& CertHash::operator=(const CertHash& copy) {
    type = copy.type;
    hash = copy.hash.clone();
    return *this;
}

CertHash CertHash::digest(HashType type, const ByteBuffer& pubkey) {
    return CertHash(type, computeHash(type, pubkey));
}

CertHash CertHash::fromRepr(const std::string& repr) {
    CertHash ret;
    long long cnt = 0;

    stringSplit(repr, ':', [&](std::string_view seg) {
        if (cnt > 0 && ret.type == HashType::INVALID)
            return;

        switch (cnt++) {
        case 0:
            if (seg == "raw")
                ret.type = HashType::RAW;
            else if (seg == "sha256")
                ret.type = HashType::SHA256;
            else
                ret.type = HashType::INVALID;
            break;
        case 1:
            ret.hash = decodeBase64(seg);
            break;
        default:
            ret.type = HashType::INVALID;
        }
    });

    return ret;
}

std::string CertHash::getRepr() const {
    static constexpr char* PREFIX_TABLE[] = {"raw:", "sha256:"};
    static constexpr int PREFIX_TABLE_LEN = sizeof(PREFIX_TABLE) / sizeof(char*);

    int hashType = static_cast<int>(type);
    if (hashType < 0 || PREFIX_TABLE_LEN <= hashType)
        return "invalid:";

    std::string ret(PREFIX_TABLE[hashType]);
    ret += hash.intoBase64String();

    return ret;
}

bool CertHash::compare(const ByteBuffer& cert) const {
    ByteBuffer otherHash = computeHash(type, cert);
    if (hash.size() != otherHash.size())
        return false;

    return secureMemcmp(hash.data(), otherHash.data(), hash.size());
}
