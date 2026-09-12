#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace SoulMod {

bool IsSha256(std::string_view value);
bool IsSafeRelativePath(std::string_view value);
bool VerifySha256File(const std::filesystem::path& path, std::string_view expected, std::string& error);

}
