#include "SoulMod/ModLoader.hpp"
#include "SoulMod/FontManager.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

namespace {

int failures = 0;
constexpr char TargetHash[] = "34a04005bcaf206eec990bd9637d9fdb6725e0a0c0d4aebf003f17f4c956eb5c";
constexpr char AssetHash[] = "d59386e0ae435e292fbe0ebcdb954b75ed5fb3922091277cb19f798fc5d50718";

void Require(const bool condition, const char* message) { if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; } }

class Package final {
public:
    Package() : root_(fs::temp_directory_path() / ("soulmod-parity-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
        fs::create_directories(root_ / "assets");
        Write(root_ / "game.win", "target");
        Write(root_ / "assets" / "asset.bin", "asset");
    }
    ~Package() { std::error_code error; fs::remove_all(root_, error); }
    void Manifest(const std::string& targetHash = TargetHash, const std::string& version = "1.0.0") const {
        Write(root_ / "manifest.json", "{\"formatVersion\":1,\"id\":\"parity.mod\",\"name\":\"Parity\",\"version\":\"" + version + "\",\"targetFile\":\"game.win\",\"targetDataSha256\":\"" + targetHash + "\",\"operationsFile\":\"operations.json\"}");
    }
    void Operations(const std::string& modId = "parity.mod", const std::string& assetHash = AssetHash) const {
        Write(root_ / "operations.json", "[{\"id\":\"asset\",\"modId\":\"" + modId + "\",\"operation\":\"resource.redirect\",\"target\":{\"type\":\"file\",\"name\":\"data.bin\"},\"assetPath\":\"assets/asset.bin\",\"assetSha256\":\"" + assetHash + "\"}]");
    }
    void UnknownOperation() const { Write(root_ / "operations.json", "[{\"id\":\"unknown\",\"modId\":\"parity.mod\",\"operation\":\"unknown.operation\",\"target\":{\"type\":\"file\",\"name\":\"data.bin\"}}]"); }
    const fs::path& Root() const { return root_; }
private:
    static void Write(const fs::path& path, const std::string& value) { std::ofstream output(path, std::ios::binary); output << value; }
    fs::path root_;
};

bool Load(const Package& package, SoulMod::LoadedModPackage& loaded) {
    SoulMod::ModLoader::Instance().SetRuntimeTargetDirectory(package.Root());
    return SoulMod::ModLoader::Instance().LoadSinglePackageFromDir(package.Root().string(), loaded);
}

void Test_hash_and_identity_validation() {
    Package package;
    package.Manifest(); package.Operations();
    SoulMod::LoadedModPackage loaded;
    Require(Load(package, loaded), "valid scratch-style package with target and asset hashes loads");
    package.Manifest(std::string(64, '0')); package.Operations();
    Require(!Load(package, loaded) && loaded.operations.empty(), "wrong target hash fails before package publication");
    package.Manifest(); package.Operations("parity.mod", std::string(64, '0'));
    Require(!Load(package, loaded) && loaded.operations.empty(), "wrong asset hash fails before package publication");
    package.Operations("other.mod");
    Require(!Load(package, loaded) && loaded.operations.empty(), "operation modId mismatch fails before package publication");
    package.Manifest(TargetHash, "version-one"); package.Operations();
    Require(!Load(package, loaded) && loaded.operations.empty(), "malformed version fails before package publication");
    package.Manifest();
    package.UnknownOperation();
    Require(!Load(package, loaded) && loaded.operations.empty(), "unknown operation fails before package publication");
}

void Test_font_binding_is_atomic() {
    SoulMod::FontManager& fonts = SoulMod::FontManager::Instance();
    fonts.Clear();
    fonts.RegisterFontRedirect("fnt_ja_main", "soulmod.galmuri11", "parity.mod");
    Require(!fonts.BindFontRedirects([](const std::string&) { return std::optional<int32_t>{}; }),
        "missing asset_get_index binding fails before a redirect is published");
    Require(!fonts.GetRedirectedFontName(1).has_value(), "failed font binding leaves no redirect side effect");
    Require(fonts.BindFontRedirects([](const std::string&) { return std::optional<int32_t>{ 1 }; }),
        "declared source font resolves through asset_get_index");
    const auto redirect = fonts.GetRedirectedFontName(1);
    Require(redirect.has_value() && *redirect == "soulmod.galmuri11", "resolved font ID retains declared target");
    fonts.Clear();
}

}

int main() {
    Test_hash_and_identity_validation();
    Test_font_binding_is_atomic();
    if (failures != 0) return 1;
    std::cout << "RESULT: PASS (runtime parity package validation)\n";
    return 0;
}
