

#include "common.h"

#include <fstream>
#include <iostream>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>

// Разбирает одну строку тестового файла.
void parse_graph_line(
    Graph& graph,
    const std::string& line,
    int number) {

    const auto colon = line.find(':');

    if (colon == std::string::npos)
        throw std::runtime_error(
            "Missing ':' on line " + std::to_string(number));

    const auto name = trim(line.substr(0, colon));

    if (name.empty()
        || name.find_first_not_of(
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ") != std::string::npos
        || graph.count(name))
        throw std::runtime_error(
            "Invalid/duplicate test package: " + name);

    std::istringstream input(line.substr(colon + 1));
    std::set<std::string> seen;

    graph[name] = {};

    std::string dependency;

    while (input >> dependency) {
        if (dependency.find_first_not_of(
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ") != std::string::npos)
            throw std::runtime_error(
                "Invalid test dependency: " + dependency);

        if (seen.insert(dependency).second)
            graph[name].push_back(dependency);
    }
}

// Читает тестовый файл с описанием графа.
Graph read_graph(const std::string& path) {
    std::ifstream file(path);

    if (!file)
        throw std::runtime_error("Cannot open " + path);

    Graph graph;
    std::string line;
    int number = 0;

    while (std::getline(file, line)) {
        ++number;

        if (number == 1
            && line.compare(0, 3, "\xEF\xBB\xBF") == 0)
            line.erase(0, 3);

        line = trim(line);

        if (line.empty() || line.front() == '#')
            continue;

        parse_graph_line(graph, line, number);
    }

    if (graph.empty())
        throw std::runtime_error("Empty test repository");

    return graph;
}

// Строит граф зависимостей методом BFS.
Graph build_graph(
    const Provider& provider,
    const std::string& root,
    const std::string& filter) {

    Graph result;

    std::queue<std::string> pending;
    std::set<std::string> discovered = { root };

    pending.push(root);

    while (!pending.empty()) {
        const auto name = pending.front();
        pending.pop();

        result[name] = {};

        const auto dependencies = provider(name);
        std::set<std::string> unique;

        for (const auto& dependency : dependencies) {
            if (!filter.empty()
                && dependency.find(filter) != std::string::npos)
                continue;

            if (!unique.insert(dependency).second)
                continue;

            result[name].push_back(dependency);

            if (discovered.insert(dependency).second) {
                if (discovered.size() > 200)
                    throw std::runtime_error(
                        "Safety limit: 200 packages");

                pending.push(dependency);
            }
        }
    }

    return result;
}

// Строит граф из локального тестового репозитория.
Graph build_test_graph(
    const Graph& repository,
    const std::string& root,
    const std::string& filter) {

    return build_graph(
        [&repository](const std::string& name) {
            const auto found = repository.find(name);

            if (found == repository.end())
                throw std::runtime_error(
                    "Unknown test package: " + name);

            return found->second;
        },
        root,
        filter);
}

// Выводит граф в консоль.
void print_graph(const Graph& graph) {
    std::cout << "\nDependency graph:\n";

    for (const auto& item : graph) {
        std::cout << item.first << " -> ";

        if (item.second.empty())
            std::cout << "(no dependencies)";

        for (const auto& dependency : item.second)
            std::cout << dependency << ' ';

        std::cout << '\n';
    }

    std::cout << "Total packages: "
        << graph.size() << '\n';
}