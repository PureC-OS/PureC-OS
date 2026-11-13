// Smoke test for the PureC OS C++ standard library port (libc++, no exceptions, no locale).
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

static int constructed_before_main = 0;

struct Global {
    Global() { constructed_before_main = 42; }
    ~Global() { std::printf("global destructor ran\n"); }
};
static Global global_object;

struct Shape {
    virtual ~Shape() = default;
    virtual int sides() const = 0;
};
struct Triangle : Shape { int sides() const override { return 3; } };
struct Square : Shape { int sides() const override { return 4; } };

static int failures = 0;
#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::printf("FAIL line %d: %s\n", __LINE__, #condition);           \
            failures++;                                                        \
        }                                                                      \
    } while (0)

int main()
{
    CHECK(constructed_before_main == 42);

    std::string text = "PureC";
    text += " OS";
    text.append(" with libc++");
    CHECK(text == "PureC OS with libc++");
    CHECK(text.find("OS") == 6);
    std::string long_text(200, 'x');
    CHECK(long_text.size() == 200 && long_text[199] == 'x');

    std::vector<int> values;
    for (int i = 0; i < 1000; i++)
        values.push_back((i * 7919) % 1009);
    std::sort(values.begin(), values.end());
    CHECK(std::is_sorted(values.begin(), values.end()));
    CHECK(std::accumulate(values.begin(), values.end(), 0LL) > 0);

    std::map<std::string, int> counts;
    for (const char *word : {"a", "b", "a", "c", "a", "b"})
        counts[word]++;
    CHECK(counts["a"] == 3 && counts["b"] == 2 && counts["c"] == 1);

    std::unique_ptr<Shape> shapes[2] = {std::make_unique<Triangle>(), std::make_unique<Square>()};
    CHECK(shapes[0]->sides() + shapes[1]->sides() == 7);

    auto shared = std::make_shared<std::vector<int>>(10, 5);
    auto copy = shared;
    CHECK(shared.use_count() == 2 && (*copy)[9] == 5);

    std::function<int(int)> square = [](int x) { return x * x; };
    CHECK(square(12) == 144);

    std::optional<int> maybe;
    CHECK(!maybe.has_value());
    maybe = 9;
    CHECK(*maybe == 9);

    std::variant<int, std::string> variant = std::string("held");
    CHECK(std::holds_alternative<std::string>(variant));

    std::array<int, 4> fixed = {4, 3, 2, 1};
    std::sort(fixed.begin(), fixed.end());
    CHECK(fixed[0] == 1 && fixed[3] == 4);

    std::string_view view = "string_view";
    CHECK(view.substr(0, 6) == "string");

    alignas(64) static char aligned_block[64];
    CHECK((reinterpret_cast<unsigned long>(aligned_block) & 63) == 0);
    struct alignas(64) Wide { char data[64]; };
    auto *wide = new Wide;
    CHECK((reinterpret_cast<unsigned long>(wide) & 63) == 0);
    delete wide;

    std::printf("%s\n", failures ? "cxx hello: FAILED" : "cxx hello: ok");
    return failures ? 1 : 0;
}
