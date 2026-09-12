#include "RuntimeOperations.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#endif

namespace
{
    struct JsonValue
    {
        enum class Type { Null, Boolean, Number, String, Array, Object } type = Type::Null;
        bool boolean = false;
        double number = 0;
        std::string string;
        std::vector<JsonValue> array;
        std::map<std::string, JsonValue> object;
    };

    class JsonParser
    {
    public:
        explicit JsonParser(const std::string& input) : input_(input) {}

        bool Parse(JsonValue& value, std::string& error)
        {
            SkipWhitespace();
            if (!ParseValue(value, error)) return false;
            SkipWhitespace();
            if (position_ != input_.size()) return Fail(error, "unexpected trailing JSON content");
            return true;
        }

    private:
        bool ParseValue(JsonValue& value, std::string& error)
        {
            SkipWhitespace();
            if (position_ == input_.size()) return Fail(error, "unexpected end of JSON");
            const char current = input_[position_];
            if (current == '{') return ParseObject(value, error);
            if (current == '[') return ParseArray(value, error);
            if (current == '"') { value.type = JsonValue::Type::String; return ParseString(value.string, error); }
            if (current == 't' && Consume("true")) { value.type = JsonValue::Type::Boolean; value.boolean = true; return true; }
            if (current == 'f' && Consume("false")) { value.type = JsonValue::Type::Boolean; return true; }
            if (current == 'n' && Consume("null")) return true;
            return ParseNumber(value, error);
        }

        bool ParseObject(JsonValue& value, std::string& error)
        {
            value.type = JsonValue::Type::Object;
            ++position_;
            SkipWhitespace();
            if (ConsumeCharacter('}')) return true;
            while (true)
            {
                if (position_ == input_.size() || input_[position_] != '"') return Fail(error, "object key must be a string");
                std::string key;
                if (!ParseString(key, error)) return false;
                SkipWhitespace();
                if (!ConsumeCharacter(':')) return Fail(error, "expected ':' after object key");
                JsonValue member;
                if (!ParseValue(member, error)) return false;
                if (!value.object.emplace(std::move(key), std::move(member)).second) return Fail(error, "duplicate object key");
                SkipWhitespace();
                if (ConsumeCharacter('}')) return true;
                if (!ConsumeCharacter(',')) return Fail(error, "expected ',' or '}' in object");
                SkipWhitespace();
            }
        }

        bool ParseArray(JsonValue& value, std::string& error)
        {
            value.type = JsonValue::Type::Array;
            ++position_;
            SkipWhitespace();
            if (ConsumeCharacter(']')) return true;
            while (true)
            {
                JsonValue member;
                if (!ParseValue(member, error)) return false;
                value.array.push_back(std::move(member));
                SkipWhitespace();
                if (ConsumeCharacter(']')) return true;
                if (!ConsumeCharacter(',')) return Fail(error, "expected ',' or ']' in array");
                SkipWhitespace();
            }
        }

        bool ParseString(std::string& value, std::string& error)
        {
            ++position_;
            while (position_ < input_.size())
            {
                const char current = input_[position_++];
                if (current == '"') return true;
                if (static_cast<unsigned char>(current) < 0x20) return Fail(error, "control character in string");
                if (current != '\\') { value.push_back(current); continue; }
                if (position_ == input_.size()) return Fail(error, "unterminated escape sequence");
                const char escaped = input_[position_++];
                switch (escaped)
                {
                case '"': value.push_back('"'); break; case '\\': value.push_back('\\'); break; case '/': value.push_back('/'); break;
                case 'b': value.push_back('\b'); break; case 'f': value.push_back('\f'); break; case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break; case 't': value.push_back('\t'); break;
                default: return Fail(error, "unsupported JSON escape sequence");
                }
            }
            return Fail(error, "unterminated string");
        }

        bool ParseNumber(JsonValue& value, std::string& error)
        {
            const size_t start = position_;
            if (position_ < input_.size() && input_[position_] == '-') ++position_;
            if (position_ == input_.size() || !std::isdigit(static_cast<unsigned char>(input_[position_]))) return Fail(error, "invalid JSON value");
            if (input_[position_] == '0') ++position_; else while (position_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[position_]))) ++position_;
            if (position_ < input_.size() && input_[position_] == '.') { ++position_; while (position_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[position_]))) ++position_; }
            if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) { ++position_; if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-')) ++position_; while (position_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[position_]))) ++position_; }
            try { value.number = std::stod(input_.substr(start, position_ - start)); }
            catch (const std::exception&) { return Fail(error, "invalid JSON number"); }
            value.type = JsonValue::Type::Number;
            return true;
        }

        bool Consume(const char* literal)
        {
            const size_t length = std::char_traits<char>::length(literal);
            if (input_.compare(position_, length, literal) != 0) return false;
            position_ += length;
            return true;
        }

        bool ConsumeCharacter(char expected)
        {
            if (position_ == input_.size() || input_[position_] != expected) return false;
            ++position_;
            return true;
        }

        void SkipWhitespace() { while (position_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[position_]))) ++position_; }
        bool Fail(std::string& error, const char* message) { error = std::string(message) + " at byte " + std::to_string(position_); return false; }

        const std::string& input_;
        size_t position_ = 0;
    };

    const JsonValue* Member(const JsonValue& object, const char* name)
    {
        const auto found = object.object.find(name);
        return found == object.object.end() ? nullptr : &found->second;
    }

    bool RequireString(const JsonValue& object, const char* name, std::string& value, std::string& error)
    {
        const JsonValue* member = Member(object, name);
        if (!member || member->type != JsonValue::Type::String || member->string.empty()) { error = std::string("missing required string '") + name + "'"; return false; }
        value = member->string;
        return true;
    }

    bool RequireInteger(const JsonValue& object, const char* name, int& value, std::string& error)
    {
        const JsonValue* member = Member(object, name);
        if (!member || member->type != JsonValue::Type::Number || member->number != static_cast<int>(member->number)) { error = std::string("missing required integer '") + name + "'"; return false; }
        value = static_cast<int>(member->number);
        return true;
    }

    bool IsSafeRelativePath(const std::string& path)
    {
        const std::filesystem::path parsed(path);
        return !path.empty() && !parsed.is_absolute() && std::none_of(parsed.begin(), parsed.end(), [](const auto& part) { return part == ".."; });
    }

    std::string NormalizePath(const std::string& path)
    {
        std::string result;
        result.reserve(path.size());
        for (char character : path) result.push_back(character == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
        while (result.size() > 1 && result.back() == '/') result.pop_back();
        return result;
    }

    bool IsContainedPath(const std::filesystem::path& root, const std::filesystem::path& candidate)
    {
        const auto normalizedRoot = std::filesystem::weakly_canonical(root);
        const auto normalizedCandidate = std::filesystem::weakly_canonical(candidate);
        auto rootPart = normalizedRoot.begin();
        auto candidatePart = normalizedCandidate.begin();
        for (; rootPart != normalizedRoot.end(); ++rootPart, ++candidatePart)
            if (candidatePart == normalizedCandidate.end() || *rootPart != *candidatePart) return false;
        return true;
    }

    bool VerifySha256Bytes(const std::string& bytes, const std::string& expected, std::string& error)
    {
        if (expected.size() != 64 || !std::all_of(expected.begin(), expected.end(), [](unsigned char value) { return std::isxdigit(value) != 0; })) { error = "assetSha256 must be a 64-character hexadecimal SHA-256"; return false; }
#ifdef _WIN32
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        DWORD hashLength = 0, resultLength = 0;
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 || BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength), &resultLength, 0) < 0) { error = "BCrypt SHA-256 initialization failed"; if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); return false; }
        std::vector<unsigned char> hashObject;
        DWORD objectLength = 0;
        if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &resultLength, 0) < 0) { error = "BCrypt SHA-256 object query failed"; BCryptCloseAlgorithmProvider(algorithm, 0); return false; }
        hashObject.resize(objectLength);
        if (BCryptCreateHash(algorithm, &hash, hashObject.data(), objectLength, nullptr, 0, 0) < 0) { error = "BCrypt SHA-256 creation failed"; BCryptCloseAlgorithmProvider(algorithm, 0); return false; }
        if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())), static_cast<ULONG>(bytes.size()), 0) < 0) { error = "BCrypt SHA-256 update failed"; BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm, 0); return false; }
        std::vector<unsigned char> digest(hashLength);
        const bool finished = BCryptFinishHash(hash, digest.data(), hashLength, 0) >= 0;
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        if (!finished) { error = "BCrypt SHA-256 finish failed"; return false; }
        static constexpr char hex[] = "0123456789abcdef";
        std::string actual;
        actual.reserve(digest.size() * 2);
        for (unsigned char byte : digest) { actual.push_back(hex[byte >> 4]); actual.push_back(hex[byte & 0x0f]); }
        if (actual != NormalizePath(expected)) { error = "asset SHA-256 mismatch"; return false; }
        return true;
#else
        error = "SHA-256 validation requires Windows BCrypt";
        return false;
#endif
    }

    bool ReadFileBytes(const std::filesystem::path& path, std::string& bytes, std::string& error)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input) { error = "cannot open asset"; return false; }
        std::ostringstream content;
        content << input.rdbuf();
        if (input.bad()) { error = "cannot read asset"; return false; }
        bytes = content.str();
        return true;
    }

    bool VerifySha256(const std::filesystem::path& path, const std::string& expected, std::string& error)
    {
        std::string bytes;
        return ReadFileBytes(path, bytes, error) && VerifySha256Bytes(bytes, expected, error);
    }

    bool IsUnqualifiedGlobalName(const std::string& name)
    {
        if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name.front())) || name.front() == '_')) return false;
        return std::all_of(name.begin() + 1, name.end(), [](unsigned char character) { return std::isalnum(character) || character == '_'; });
    }

    bool ReadOptionalStringArray(const JsonValue& object, const char* name, std::vector<std::string>& values, std::string& error)
    {
        const JsonValue* member = Member(object, name);
        values.clear();
        if (!member) return true;
        if (member->type != JsonValue::Type::Array) { error = std::string("invalid string array '") + name + "'"; return false; }
        for (const JsonValue& value : member->array)
        {
            if (value.type != JsonValue::Type::String || value.string.empty()) { error = std::string("invalid string array '") + name + "'"; return false; }
            values.push_back(value.string);
        }
        return true;
    }

    bool ReadOptionalGlobalJsonLoadCondition(const JsonValue& selector, std::optional<soulmod::GlobalJsonLoadCondition>& condition, std::string& error)
    {
        condition.reset();
        const JsonValue* when = Member(selector, "when");
        if (!when) return true;
        if (when->type != JsonValue::Type::Object) { error = "global.json.load when must be an object"; return false; }
        for (const auto& [name, value] : when->object)
            if (name != "global" && name != "equals") { error = "global.json.load when contains an unknown member"; return false; }
        std::string global;
        if (!RequireString(*when, "global", global, error) || !IsUnqualifiedGlobalName(global)) { error = "global.json.load when global must be an unqualified global name"; return false; }
        const JsonValue* equals = Member(*when, "equals");
        if (!equals || equals->type != JsonValue::Type::String) { error = "global.json.load when equals must be a string"; return false; }
        condition = soulmod::GlobalJsonLoadCondition{ std::move(global), equals->string };
        return true;
    }

    bool ParseOperations(const JsonValue& document, const std::filesystem::path& root, const std::string& packageId, soulmod::PackageOperations& operations, std::string& error)
    {
        if (document.type != JsonValue::Type::Array) { error = "operations root must be an array"; return false; }
        std::unordered_map<std::string, std::string> globalOwners;
        for (const JsonValue& entry : document.array)
        {
            if (entry.type != JsonValue::Type::Object) { error = "operation must be an object"; return false; }
            std::string id, modId, operation;
            if (!RequireString(entry, "id", id, error) || !RequireString(entry, "modId", modId, error) || !RequireString(entry, "operation", operation, error)) return false;
            if (modId != packageId) { error = "operation modId does not match manifest ID"; return false; }
            const JsonValue* target = Member(entry, "target");
            if (!target || target->type != JsonValue::Type::Object) { error = "operation target must be an object"; return false; }
            std::string targetType, targetName;
            if (!RequireString(*target, "type", targetType, error) || !RequireString(*target, "name", targetName, error)) return false;
            if (operation == "resource.redirect")
            {
                std::string assetPath, assetHash;
                if (targetType != "file" || !IsSafeRelativePath(targetName) || !RequireString(entry, "assetPath", assetPath, error) || !RequireString(entry, "assetSha256", assetHash, error) || !IsSafeRelativePath(assetPath)) { error = "invalid resource.redirect operation"; return false; }
                const auto asset = root / assetPath;
                if (!std::filesystem::is_regular_file(asset) || !IsContainedPath(root, asset) || !VerifySha256(asset, assetHash, error)) return false;
                if (!operations.fileRedirects.emplace(NormalizePath(targetName), asset).second) { error = "duplicate file redirect"; return false; }
            }
            else if (operation == "global.json.load")
            {
                const JsonValue* selector = Member(entry, "selector");
                soulmod::GlobalJsonLoad load;
                std::string assetPath;
                if (targetType != "global" || !IsUnqualifiedGlobalName(targetName) || !selector || selector->type != JsonValue::Type::Object || !RequireString(*selector, "applyAt", load.applyAt, error) || load.applyAt != "first.draw_set_font" || !ReadOptionalStringArray(*selector, "mirrorGlobals", load.mirrorGlobals, error) || !ReadOptionalGlobalJsonLoadCondition(*selector, load.condition, error) || !RequireString(entry, "assetPath", assetPath, error) || !RequireString(entry, "assetSha256", load.assetSha256, error) || !IsSafeRelativePath(assetPath)) { error = "invalid global.json.load operation"; return false; }
                load.operationId = id;
                load.targetGlobal = targetName;
                std::unordered_map<std::string, bool> names;
                names.emplace(load.targetGlobal, true);
                for (const std::string& mirror : load.mirrorGlobals)
                {
                    if (!IsUnqualifiedGlobalName(mirror) || !names.emplace(mirror, true).second) { error = "global.json.load mirrors must be distinct unqualified global names"; return false; }
                }
                const auto asset = root / assetPath;
                load.assetPath = std::filesystem::weakly_canonical(asset);
                std::string verifiedBytes;
                if (!std::filesystem::is_regular_file(load.assetPath) || !IsContainedPath(root, load.assetPath) || !soulmod::ReadVerifiedAsset(load, verifiedBytes, error)) return false;
                for (const auto& [globalName, unused] : names)
                {
                    if (globalOwners.contains(globalName)) { error = "global is owned by more than one global.json.load operation"; return false; }
                }
                for (const auto& [globalName, unused] : names) globalOwners.emplace(globalName, id);
                operations.globalJsonLoads.push_back(std::move(load));
            }
            else if (operation == "font.register")
            {
                const JsonValue* selector = Member(entry, "selector");
                std::string assetPath, assetHash;
                if (targetType != "font" || !selector || selector->type != JsonValue::Type::Object || !RequireString(entry, "assetPath", assetPath, error) || !RequireString(entry, "assetSha256", assetHash, error) || !IsSafeRelativePath(assetPath)) { error = "invalid font.register operation"; return false; }
                soulmod::FontRegistration font{ targetName, root / assetPath };
                const JsonValue* bold = Member(*selector, "bold"); const JsonValue* italic = Member(*selector, "italic");
                if (!RequireInteger(*selector, "size", font.size, error) || !RequireInteger(*selector, "first", font.first, error) || !RequireInteger(*selector, "last", font.last, error) || !bold || bold->type != JsonValue::Type::Boolean || !italic || italic->type != JsonValue::Type::Boolean || font.size <= 0 || font.first < 0 || font.last < font.first) { error = "invalid font.register selector"; return false; }
                font.bold = bold->boolean; font.italic = italic->boolean;
                if (!std::filesystem::is_regular_file(font.assetPath) || !IsContainedPath(root, font.assetPath) || !VerifySha256(font.assetPath, assetHash, error)) return false;
                if (!operations.registeredFonts.emplace(font.name, std::move(font)).second) { error = "duplicate font registration"; return false; }
            }
            else if (operation == "font.redirect")
            {
                const JsonValue* selector = Member(entry, "selector");
                std::string value;
                if (targetType != "font" || selector || !RequireString(entry, "value", value, error)) { error = "invalid font.redirect operation"; return false; }
                if (!operations.fontRedirects.emplace(targetName, value).second) { error = "duplicate font redirect"; return false; }
            }
            else { error = "unsupported operation in runtime proof: " + operation; return false; }
        }
        for (const auto& [sourceName, targetName] : operations.fontRedirects)
            if (!operations.registeredFonts.contains(targetName)) { error = "font redirect references an unregistered font"; return false; }
        return true;
    }
}

namespace soulmod
{
    bool LoadPackageOperations(const std::filesystem::path& packageRoot, PackageOperations& operations, std::string& error)
    {
        operations = {};
        const auto manifestPath = packageRoot / "manifest.json";
        if (!std::filesystem::is_regular_file(manifestPath)) { error = "manifest.json not found"; return false; }
        std::ifstream manifestInput(manifestPath);
        std::stringstream manifestContent; manifestContent << manifestInput.rdbuf();
        JsonValue manifest;
        if (!JsonParser(manifestContent.str()).Parse(manifest, error) || manifest.type != JsonValue::Type::Object) { if (error.empty()) error = "manifest root must be an object"; return false; }
        int formatVersion = 0;
        std::string packageId, operationsFile;
        if (!RequireInteger(manifest, "formatVersion", formatVersion, error) || formatVersion != 1 || !RequireString(manifest, "id", packageId, error) || !RequireString(manifest, "operationsFile", operationsFile, error) || !IsSafeRelativePath(operationsFile)) { if (error.empty()) error = "invalid manifest"; return false; }
        const auto operationsPath = packageRoot / operationsFile;
        if (!std::filesystem::is_regular_file(operationsPath) || !IsContainedPath(packageRoot, operationsPath)) { error = "operations file not found or outside package root"; return false; }
        std::ifstream operationsInput(operationsPath);
        std::stringstream operationsContent; operationsContent << operationsInput.rdbuf();
        JsonValue document;
        if (!JsonParser(operationsContent.str()).Parse(document, error)) return false;
        operations.packageId = packageId;
        operations.root = std::filesystem::weakly_canonical(packageRoot);
        return ParseOperations(document, operations.root, packageId, operations, error);
    }

    bool ReadVerifiedAsset(const GlobalJsonLoad& operation, std::string& bytes, std::string& error)
    {
        return ReadFileBytes(operation.assetPath, bytes, error) && VerifySha256Bytes(bytes, operation.assetSha256, error);
    }

    std::optional<std::filesystem::path> SelectFileRedirect(const PackageOperations& operations, const std::string& requestedPath)
    {
        const std::string request = NormalizePath(requestedPath);
        for (const auto& [logicalPath, assetPath] : operations.fileRedirects)
            if (request == logicalPath || (request.size() > logicalPath.size() && request.ends_with('/' + logicalPath))) return assetPath;
        return std::nullopt;
    }

    std::optional<std::string> SelectFontRedirect(const PackageOperations& operations, std::string_view sourceFontName)
    {
        const auto redirect = operations.fontRedirects.find(std::string(sourceFontName));
        return redirect == operations.fontRedirects.end() ? std::nullopt : std::optional<std::string>(redirect->second);
    }

    std::optional<std::filesystem::path> DiscoverPackageRoot(const std::filesystem::path& modulePath, std::string& error)
    {
        std::filesystem::path current = modulePath.parent_path();
        while (!current.empty())
        {
            if (std::filesystem::is_regular_file(current / "manifest.json")) return std::filesystem::weakly_canonical(current);
            const auto mods = current / "mods";
            if (std::filesystem::is_directory(mods))
            {
                std::vector<std::filesystem::path> candidates;
                for (const auto& entry : std::filesystem::directory_iterator(mods))
                {
                    if (!entry.is_directory() || !std::filesystem::is_regular_file(entry.path() / "manifest.json")) continue;
                    PackageOperations candidateOperations;
                    std::string candidateError;
                    if (LoadPackageOperations(entry.path(), candidateOperations, candidateError)) candidates.push_back(entry.path());
                }
                std::sort(candidates.begin(), candidates.end());
                if (candidates.size() == 1) return std::filesystem::weakly_canonical(candidates.front());
                if (candidates.size() > 1) { error = "multiple valid operation packages found; place the plugin under a single package root"; return std::nullopt; }
            }
            const auto parent = current.parent_path();
            if (parent == current) break;
            current = parent;
        }
        error = "no package manifest found while walking module ancestors";
        return std::nullopt;
    }
}
