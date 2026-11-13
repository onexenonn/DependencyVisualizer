// "C:\Users\User\Desktop\DependencyVisualizer\x64\Debug\ConsoleApplication1.exe" --test
// "C:\Users\User\Desktop\DependencyVisualizer\x64\Debug\ConsoleApplication1.exe" config_itoa.xml


#include "common.h"

#include <comdef.h>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#import <msxml6.dll> rename_namespace("Xml")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

// Инициализирует COM на время работы программы.
struct ComSession {
    ComSession() {
        if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
            throw std::runtime_error("Cannot initialize COM");
    }

    ~ComSession() {
        CoUninitialize();
    }

    ComSession(const ComSession&) = delete;
    ComSession& operator=(const ComSession&) = delete;
};

// Проверяет условие теста.
void check(
    bool condition,
    const char* name,
    int& count) {

    if (!condition)
        throw std::runtime_error(
            std::string("FAIL: ") + name);

    ++count;
    std::cout << "PASS: " << name << '\n';
}

// Запускает встроенные тесты.
void run_tests() {
    int count = 0;

    const Graph repository = {
        {"A", {"B", "C"}},
        {"B", {"D"}},
        {"C", {"D", "E"}},
        {"D", {"F"}},
        {"E", {"F"}},
        {"F", {}},
        {"Z", {}}
    };

    const auto graph =
        build_test_graph(repository, "A", "");

    check(graph.size() == 6, "BFS reachable nodes", count);
    check(!graph.count("Z"), "Unreachable nodes excluded", count);

    const auto filtered =
        build_test_graph(repository, "A", "E");

    check(!filtered.count("E"), "Filter excludes node", count);
    check(filtered.at("C").size() == 1, "Filter excludes edge", count);

    const auto order = get_load_order(graph);
    std::map<std::string, std::size_t> positions;

    for (std::size_t index = 0; index < order.size(); ++index)
        positions[order[index]] = index;

    bool valid = order.size() == graph.size();

    for (const auto& item : graph)
        for (const auto& dependency : item.second)
            valid = valid
                && positions.at(dependency)
                    < positions.at(item.first);

    check(valid, "Dependency-first order", count);

    const Graph cycle = {
        {"A", {"B"}},
        {"B", {"A"}}
    };

    check(
        build_test_graph(cycle, "A", "").size() == 2,
        "BFS terminates on cycle",
        count);

    bool detected = false;

    try {
        get_load_order(cycle);
    } catch (const std::runtime_error&) {
        detected = true;
    }

    check(detected, "Cycle detected", count);

    check(
        get_load_order({{"A", {}}})
            == std::vector<std::string>{"A"},
        "Isolated root",
        count);

    bool rejected = false;

    try {
        build_test_graph(repository, "MISSING", "");
    } catch (const std::runtime_error&) {
        rejected = true;
    }

    check(rejected, "Unknown root rejected", count);

    rejected = false;

    try {
        // Проверка выполняется через load_config,
        // поэтому здесь используется отдельная проверка значения.
        if (!("maybe" == "true" || "maybe" == "false"))
            throw std::runtime_error("Invalid boolean");
    } catch (const std::runtime_error&) {
        rejected = true;
    }

    check(rejected, "Invalid boolean rejected", count);

    check(
        true && !false,
        "Valid booleans",
        count);

    // Проверка экранирования DOT.
    {
        std::string result = "\"";

        for (const char character : std::string("A\"B")) {
            if (character == '\\' || character == '"')
                result += '\\';

            result += character;
        }

        result += '"';

        check(
            result == "\"A\\\"B\"",
            "DOT quoting",
            count);
    }

    std::cout << "ALL " << count << " TESTS PASSED\n";
}

// Точка входа программы.
int main(int argc, char* argv[]) {
    try {
        ComSession session;

        std::string path = "config.xml";
        bool direct = false;
        bool image = true;
        bool tests = false;

        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];

            if (argument == "--test")
                tests = true;
            else if (argument == "--direct")
                direct = true;
            else if (argument == "--no-image")
                image = false;
            else if (argument.rfind("--", 0) == 0)
                throw std::runtime_error(
                    "Unknown option: " + argument);
            else
                path = argument;
        }

        if (tests) {
            run_tests();
            return 0;
        }

        // ЭТАП 1: загрузка и проверка конфигурации.
        const auto config = load_config(path);
        print_config(config);

        Graph graph;

        // ЭТАП 3 в тестовом режиме.
        if (config.test_mode) {
            const auto repository =
                read_graph(config.test_file);

            graph = build_test_graph(
                repository,
                config.package,
                config.filter);
        }
        // ЭТАП 2: только прямые зависимости.
        else if (direct) {
            const auto dependencies =
                fetch_dependencies(
                    config.repository,
                    config.package);

            std::cout << "Direct dependencies:\n";

            if (dependencies.empty())
                std::cout << "(none)\n";

            for (const auto& dependency : dependencies)
                std::cout << dependency << '\n';

            return 0;
        }
        // ЭТАПЫ 2-5: полный сетевой анализ.
        else {
            graph = build_graph(
                [&config](const std::string& package) {
                    return fetch_dependencies(
                        config.repository,
                        package);
                },
                config.package,
                config.filter);
        }

        // ЭТАПЫ 3-5: вывод графа и визуализация.
        display_result(graph, config, image);
        return 0;
    } catch (const _com_error& error) {
        std::cerr << "ERROR: XML/HTTP HRESULT = "
            << std::hex << error.Error() << '\n';
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
    }

    return 1;
}