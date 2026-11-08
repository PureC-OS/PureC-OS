#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

static void step(const char *name, const std::error_code &code) {
    std::printf("fsdebug %-34s %s\n", name, code ? code.message().c_str() : "ok");
}

int main() {
    std::error_code ec;
    fs::path base = fs::temp_directory_path(ec) / "fsdebug_dir";
    step("temp_directory_path", ec);
    fs::remove_all(base, ec);
    step("remove_all (initial cleanup)", ec);
    fs::create_directories(base / "a" / "b", ec);
    step("create_directories", ec);
    { std::ofstream file(base / "a" / "data.txt"); file << "x\n"; }
    step("ofstream write", std::error_code());
    fs::rename(base / "a" / "data.txt", base / "a" / "moved.txt", ec);
    step("rename file", ec);
    step("exists moved.txt", fs::exists(base / "a" / "moved.txt") ? std::error_code() : std::make_error_code(std::errc::no_such_file_or_directory));
    fs::remove(base / "a" / "moved.txt", ec);
    step("remove file", ec);
    fs::remove(base / "a" / "b", ec);
    step("remove empty dir b", ec);
    fs::remove(base / "a", ec);
    step("remove empty dir a", ec);
    fs::create_directories(base / "a" / "b", ec);
    { std::ofstream file(base / "a" / "f.txt"); file << "y\n"; }
    auto removed = fs::remove_all(base, ec);
    step("remove_all (tree)", ec);
    std::printf("fsdebug removed=%ju exists_after=%d\n", (std::uintmax_t)removed, (int)fs::exists(base));
    return 0;
}
