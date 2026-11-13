#include <cstdio>
#include <string>
#include <thread>
#include <vector>
int main() {
    std::vector<std::string> v{"cmake", "built", "this"};
    int total = 0;
    std::thread t([&] { for (auto &s : v) total += (int)s.size(); });
    t.join();
    try { throw 7; } catch (int e) { total += e; }
    std::printf("cmake-ok %d\n", total);
    return 0;
}
