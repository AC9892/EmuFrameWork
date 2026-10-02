#pragma once
#include <emuframe/emuframe.h>
#include <filesystem>
#include <string>
namespace ef {
struct Rom { EF_GameInfo info{}; std::filesystem::path path; };
EF_Result inspect_rom(const std::filesystem::path& path, Rom& out, std::string& error);
std::string sha256_file(const std::filesystem::path& path);
}
