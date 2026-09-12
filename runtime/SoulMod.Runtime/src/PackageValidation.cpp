#include "SoulMod/PackageValidation.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <vector>

#include <windows.h>
#include <bcrypt.h>

namespace SoulMod {
namespace {

std::string HexDigest(const std::vector<unsigned char>& digest) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string value;
    value.reserve(digest.size() * 2);
    for (const unsigned char byte : digest) {
        value.push_back(hex[byte >> 4]);
        value.push_back(hex[byte & 0x0f]);
    }
    return value;
}

}

bool IsSha256(const std::string_view value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](const unsigned char character) {
        return std::isxdigit(character) != 0;
    });
}

bool IsSafeRelativePath(const std::string_view value) {
    const std::filesystem::path path{std::string(value)};
    return !value.empty() && !path.is_absolute() && std::none_of(path.begin(), path.end(), [](const auto& part) {
        return part == "..";
    });
}

bool VerifySha256File(const std::filesystem::path& path, const std::string_view expected, std::string& error) {
    if (!IsSha256(expected)) {
        error = "SHA-256 must be a 64-character hexadecimal value";
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "cannot open file for SHA-256 verification";
        return false;
    }
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLength = 0;
    DWORD hashLength = 0;
    DWORD resultLength = 0;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &resultLength, 0) < 0 ||
        BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength), &resultLength, 0) < 0) {
        error = "BCrypt SHA-256 initialization failed";
        if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }
    std::vector<unsigned char> object(objectLength);
    if (BCryptCreateHash(algorithm, &hash, object.data(), objectLength, nullptr, 0, 0) < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        error = "BCrypt SHA-256 creation failed";
        return false;
    }
    std::vector<char> buffer(8192);
    while (input.read(buffer.data(), buffer.size()) || input.gcount() != 0) {
        if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(input.gcount()), 0) < 0) {
            BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            error = "BCrypt SHA-256 update failed";
            return false;
        }
    }
    std::vector<unsigned char> digest(hashLength);
    const bool finished = BCryptFinishHash(hash, digest.data(), hashLength, 0) >= 0;
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!finished || input.bad()) {
        error = "SHA-256 read or finalization failed";
        return false;
    }
    std::string normalized(expected);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    if (HexDigest(digest) != normalized) {
        error = "SHA-256 mismatch";
        return false;
    }
    return true;
}

}
