#include <algorithm>
#include <chrono>
#include <codecvt>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <locale>
#include <random>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static int failures;
#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::printf("FAIL line %d: %s\n", __LINE__, #condition);           \
            failures++;                                                        \
        }                                                                      \
    } while (0)

int main() {
    std::ostringstream out;
    out << 42 << ' ' << std::hex << 255 << ' ' << std::dec << std::boolalpha << true << ' '
        << std::fixed << std::setprecision(3) << 3.14159 << ' ' << std::setw(6) << std::setfill('*') << 7;
    CHECK(out.str() == "42 ff true 3.142 *****7");

    std::istringstream in("17 2.5 word\nsecond line\n");
    int number = 0;
    double real = 0;
    std::string word, line;
    in >> number >> real >> word;
    std::getline(in, line);
    std::getline(in, line);
    CHECK(number == 17 && real == 2.5 && word == "word" && line == "second line");

    std::wstring wide = L"Привет, мир";
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    std::string utf8 = converter.to_bytes(wide);
    CHECK(utf8.size() == 21 && converter.from_bytes(utf8) == wide);
    std::wostringstream wout;
    wout << wide << L' ' << 12345;
    CHECK(wout.str() == L"Привет, мир 12345");

    std::locale classic = std::locale::classic();
    CHECK(std::use_facet<std::ctype<char>>(classic).is(std::ctype_base::alpha, 'q'));
    CHECK(std::use_facet<std::ctype<wchar_t>>(classic).toupper(L'я') == L'Я');
    CHECK(std::use_facet<std::numpunct<char>>(classic).decimal_point() == '.');
    std::locale named("en_US.UTF-8");
    std::ostringstream localized;
    localized.imbue(named);
    localized << 1234567;
    CHECK(!localized.str().empty());
    bool bad_locale = false;
    try { std::locale missing("xx_NOWHERE.nonsense"); } catch (const std::runtime_error &) { bad_locale = true; }
    CHECK(bad_locale);

    fs::path base = fs::temp_directory_path() / "cxx_stdlib_test";
    std::error_code ignore;
    fs::remove_all(base, ignore);
    CHECK(fs::create_directories(base / "a" / "b"));
    {
        std::ofstream file(base / "a" / "data.txt");
        file << "line one\nline two\nline three\n";
    }
    CHECK(fs::exists(base / "a" / "data.txt"));
    CHECK(fs::is_directory(base / "a" / "b"));
    CHECK(fs::file_size(base / "a" / "data.txt") == 29);
    std::ifstream reader(base / "a" / "data.txt");
    std::vector<std::string> lines;
    for (std::string text; std::getline(reader, text);) lines.push_back(text);
    CHECK(lines.size() == 3 && lines[1] == "line two");
    std::size_t entries = 0;
    for (const auto &entry : fs::directory_iterator(base / "a")) { (void)entry; entries++; }
    CHECK(entries == 2);
    fs::rename(base / "a" / "data.txt", base / "a" / "moved.txt");
    CHECK(!fs::exists(base / "a" / "data.txt") && fs::exists(base / "a" / "moved.txt"));
    fs::path sample = "/usr/share/dir/archive.tar.gz";
    CHECK(sample.filename() == "archive.tar.gz" && sample.extension() == ".gz" &&
          sample.stem() == "archive.tar" && sample.parent_path() == "/usr/share/dir");
    CHECK(fs::path("a/./b/../c").lexically_normal() == "a/c");
    CHECK(fs::remove_all(base) >= 3);
    CHECK(!fs::exists(base));

    std::mt19937 engine(42);
    CHECK(engine() == 1608637542u);
    std::uniform_int_distribution<int> dice(1, 6);
    bool in_range = true;
    for (int i = 0; i < 1000; i++) { int value = dice(engine); if (value < 1 || value > 6) in_range = false; }
    CHECK(in_range);
    std::random_device device;
    unsigned first = device();
    unsigned second = device();
    unsigned third = device();
    CHECK(!(first == second && second == third));
    std::vector<int> items{1, 2, 3, 4, 5, 6, 7, 8};
    std::shuffle(items.begin(), items.end(), engine);
    std::sort(items.begin(), items.end());
    CHECK(items.front() == 1 && items.back() == 8);

    auto before = std::chrono::steady_clock::now();
    auto wall = std::chrono::system_clock::now().time_since_epoch();
    CHECK(std::chrono::duration_cast<std::chrono::seconds>(wall).count() > 1700000000);
    CHECK(std::chrono::steady_clock::now() >= before);
    std::time_t stamp = 86400 * 365;
    std::tm broken = *std::gmtime(&stamp);
    std::ostringstream formatted;
    formatted << std::put_time(&broken, "%Y-%m-%d");
    CHECK(formatted.str() == "1971-01-01");

    std::regex pattern("([a-z]+)([0-9]+)");
    std::smatch match;
    std::string subject = "abc123 def45 ghi6";
    CHECK(std::regex_search(subject, match, pattern) && match[1] == "abc" && match[2] == "123");
    CHECK(std::regex_replace(subject, pattern, "$2$1") == "123abc 45def 6ghi");
    std::ptrdiff_t matches = std::distance(std::sregex_iterator(subject.begin(), subject.end(), pattern),
                                           std::sregex_iterator());
    CHECK(matches == 3);

    CHECK(std::format("{} {:>5} {:.2f} {:x}", 1, "ab", 2.5, 255) == "1    ab 2.50 ff");
    CHECK(std::to_string(3.5) == "3.500000" && std::stod("2.5e1") == 25.0 && std::stoi("  -73") == -73);

    std::cout << "cxx stdlib via cout: " << 7 * 6 << std::endl;
    std::cerr << "cxx stdlib via cerr" << std::endl;
    std::printf("cxx stdlib done failures=%d\n", failures);
    return failures ? 1 : 0;
}
