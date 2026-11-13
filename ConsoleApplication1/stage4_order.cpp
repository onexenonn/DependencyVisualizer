


#include "common.h"

#include <map>
#include <queue>
#include <stdexcept>
#include <vector>

// Строит порядок загрузки пакетов.
std::vector<std::string> get_load_order(const Graph& graph) {
    // Сколько зависимостей ещё не обработано у каждого пакета.
    std::map<std::string, std::size_t> remaining;

    // Какие пакеты зависят от данного пакета.
    Graph users;

    for (const auto& item : graph) {
        remaining[item.first] = item.second.size();

        for (const auto& dependency : item.second)
            users[dependency].push_back(item.first);
    }

    // Сначала готовы пакеты без зависимостей.
    std::queue<std::string> ready;

    for (const auto& item : remaining)
        if (!item.second)
            ready.push(item.first);

    std::vector<std::string> order;

    while (!ready.empty()) {
        const auto name = ready.front();
        ready.pop();

        order.push_back(name);

        // После обработки пакета уменьшаем счётчики зависимых пакетов.
        for (const auto& user : users[name])
            if (--remaining.at(user) == 0)
                ready.push(user);
    }

    // Если не все пакеты попали в порядок, существует цикл.
    if (order.size() != graph.size())
        throw std::runtime_error(
            "Dependency cycle: no valid load order");

    return order;
}