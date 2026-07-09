// svstests.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <concepts>

using std::cout;

extern void fss_test();
extern void fss_test_2();

template <typename T>
constexpr std::string_view get_type_name() {
#if defined(__clang__)
    std::string_view name = __PRETTY_FUNCTION__;
    std::string_view prefix = "get_type_name() [T = ";
    std::string_view suffix = "]";
#elif defined(__GNUC__)
    std::string_view name = __PRETTY_FUNCTION__;
    std::string_view prefix = "get_type_name() [with T = ";
    std::string_view suffix = "]";
#elif defined(_MSC_VER)
    std::string_view name = __FUNCSIG__;
    std::string_view prefix = "get_type_name<";
    std::string_view suffix = ">(void)";
#endif

    size_t start = name.find(prefix) + prefix.size();
    size_t end = name.rfind(suffix);

    return name.substr(start, end - start);
}

template <std::invocable T>
void run_test(T&& f, const char* testname)
{
    clock_t c = clock();
    int sec = c / CLOCKS_PER_SEC;
    printf("[TEST] starting %s at %02d:%02d:%02d\n", testname, sec / 3600, (sec / 60) % 60, sec % 60);
    try
    {
        f();
    }
    catch (const std::runtime_error& re)
    {
        printf("[ERR] test failed with exception \"%s\" (runtime_error)\n", re.what());
        printf("[INFO] internal exception name: %s\n", typeid(re).name());
        printf("[INFO] waiting for debugger...\n");
        system("pause");
    }
    catch (const std::exception& ex)
    {
        printf("[ERR] test failed with generic C++ exception \"%s\"\n", ex.what());
        printf("[INFO] internal exception name: %s\n", typeid(ex).name());
        printf("[INFO] waiting for debugger...\n");
        system("pause");
}
    catch (...)
    {
        printf("[FTL] test failed with unknown C++ exception\n");
        printf("[INFO] waiting for debugger...\n");
        system("pause");
        printf("[INFO] unrecoverable exception; rethrowing...\n");
        throw;
    }
    clock_t c2 = clock();
    sec = c2 / CLOCKS_PER_SEC;
    int diff = c2 - c;
    printf("[TEST] finished %s at %02d:%02d:%02d\n", testname, sec / 3600, (sec / 60) % 60, sec % 60);
    printf("[INFO] elapsed %d ticks\n\n", diff);
}

int main()
{
    // run_test(fss_test, "file subsystem test");
    run_test(fss_test_2, "data checking test");
}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
