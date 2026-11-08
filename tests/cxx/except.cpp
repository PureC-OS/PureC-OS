#include <cstdio>
#include <exception>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <vector>

static int failures;
#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::printf("FAIL line %d: %s\n", __LINE__, #condition);           \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static int live_objects;
struct Tracked {
    Tracked() { live_objects++; }
    Tracked(const Tracked &) { live_objects++; }
    ~Tracked() { live_objects--; }
};

struct Base { virtual ~Base() = default; virtual const char *name() const { return "Base"; } };
struct Derived : Base { const char *name() const override { return "Derived"; } };
struct Other : Base {};

struct CustomError : std::runtime_error {
    int code;
    CustomError(const char *text, int value) : std::runtime_error(text), code(value) {}
};

static void level3(int mode) {
    Tracked guard;
    if (mode == 1) throw 42;
    if (mode == 2) throw std::runtime_error("runtime");
    if (mode == 3) throw CustomError("custom", 7);
    if (mode == 4) throw std::string("string");
}
static void level2(int mode) { Tracked guard; std::vector<Tracked> many(3); level3(mode); }
static void level1(int mode) { Tracked guard; level2(mode); }

struct ThrowsInConstructor {
    Tracked member;
    ThrowsInConstructor() { throw std::logic_error("ctor"); }
};

static int rethrow_chain() {
    try {
        try {
            throw CustomError("inner", 5);
        } catch (const CustomError &error) {
            throw;
        }
    } catch (const std::runtime_error &error) {
        return dynamic_cast<const CustomError *>(&error) ? 1 : 0;
    }
    return -1;
}

int main() {
    try { level1(1); CHECK(false); } catch (int value) { CHECK(value == 42); }
    CHECK(live_objects == 0);
    try { level1(2); CHECK(false); } catch (const std::exception &error) {
        CHECK(std::string(error.what()) == "runtime");
    }
    CHECK(live_objects == 0);
    try { level1(3); CHECK(false); } catch (const CustomError &error) {
        CHECK(error.code == 7 && std::string(error.what()) == "custom");
    }
    try { level1(4); CHECK(false); } catch (const std::string &text) { CHECK(text == "string"); }
    try { level1(1); } catch (...) { CHECK(true); }
    CHECK(live_objects == 0);

    CHECK(rethrow_chain() == 1);

    try { ThrowsInConstructor object; CHECK(false); } catch (const std::logic_error &) { CHECK(true); }
    CHECK(live_objects == 0);

    std::exception_ptr saved;
    try { level1(2); } catch (...) { saved = std::current_exception(); }
    CHECK(saved != nullptr);
    try { std::rethrow_exception(saved); } catch (const std::runtime_error &error) {
        CHECK(std::string(error.what()) == "runtime");
    }

    Base *base = new Derived;
    CHECK(dynamic_cast<Derived *>(base) != nullptr);
    CHECK(dynamic_cast<Other *>(base) == nullptr);
    CHECK(typeid(*base) == typeid(Derived));
    CHECK(typeid(*base) != typeid(Base));
    CHECK(std::string(typeid(Derived).name()).find("Derived") != std::string::npos);
    try { (void)dynamic_cast<Other &>(*base); CHECK(false); } catch (const std::bad_cast &) { CHECK(true); }
    delete base;

    try {
        std::vector<char> huge;
        huge.reserve(static_cast<std::size_t>(1) << 60);
        CHECK(false);
    } catch (const std::length_error &) { CHECK(true); } catch (const std::bad_alloc &) { CHECK(true); }

    try { std::vector<int> v(2); (void)v.at(5); CHECK(false); } catch (const std::out_of_range &) { CHECK(true); }

    int caught = 0;
    for (int i = 0; i < 20000; i++) {
        try { throw i; } catch (int value) { if (value == i) caught++; }
    }
    CHECK(caught == 20000);

    void *(*allocate)(std::size_t) = &::operator new;
    try { void *block = allocate(static_cast<std::size_t>(1) << 60); CHECK(block == nullptr); }
    catch (const std::bad_alloc &) { CHECK(true); }

    std::printf("cxx except done failures=%d\n", failures);
    return failures ? 1 : 0;
}
