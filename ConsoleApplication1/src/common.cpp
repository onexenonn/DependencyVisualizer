
#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

// Граф зависимостей:
// ключ — имя пакета;
// значение — список его прямых зависимостей.
using Graph = std::map<std::string, std::vector<std::string>>;

// Функция, которая по имени пакета возвращает список его зависимостей.
using Provider =
std::function<std::vector<std::string>(const std::string&)>;

// Настройки программы из XML-конфигурации.
struct Config {
    std::string package;     // Имя анализируемого пакета.
    std::string repository;  // Адрес репозитория.
    std::string test_file;   // Путь к тестовому файлу.
    std::string filter;      // Подстрока для исключения зависимостей.

    bool test_mode = true;   // true — локальный тестовый граф.
    bool ascii_tree = true;  // Выводить ASCII-дерево.
};

// ============================================================================
// ЭТАП 1. КОНФИГУРАЦИЯ
// ============================================================================

// Читает и проверяет XML-конфигурацию.
Config load_config(const std::string& path);

// Выводит настройки программы.
void print_config(const Config& config);

// Убирает пробелы и переводы строк по краям строки.
std::string trim(const std::string& text);

// Преобразует строку COM в UTF-8.
std::string utf8(const _bstr_t& text);

// ============================================================================
// ЭТАП 2. ПОЛУЧЕНИЕ ЗАВИСИМОСТЕЙ
// ============================================================================

// Выполняет HTTP GET-запрос и разбирает JSON-ответ.
struct JsonValue;
JsonValue request_json(const std::string& url);

// Получает список обычных зависимостей пакета.
std::vector<std::string> fetch_dependencies(
    std::string base,
    const std::string& package);

// ============================================================================
// ЭТАП 3. ПОСТРОЕНИЕ ГРАФА
// ============================================================================

// Строит граф зависимостей методом BFS.
Graph build_graph(
    const Provider& provider,
    const std::string& root,
    const std::string& filter);

// Строит граф из локального тестового репозитория.
Graph build_test_graph(
    const Graph& repository,
    const std::string& root,
    const std::string& filter);

// Читает тестовый файл с графом.
Graph read_graph(const std::string& path);

// Выводит граф в консоль.
void print_graph(const Graph& graph);

// ============================================================================
// ЭТАП 4. ПОРЯДОК ЗАГРУЗКИ
// ============================================================================

// Строит порядок загрузки пакетов.
std::vector<std::string> get_load_order(const Graph& graph);

// ============================================================================
// ЭТАП 5. ВИЗУАЛИЗАЦИЯ
// ============================================================================

// Сохраняет граф в формате Graphviz DOT.
void save_dot(const Graph& graph);

// Выводит ASCII-дерево зависимостей.
void print_ascii_tree(
    const Graph& graph,
    const std::string& root);

// Создаёт graph.png и открывает его.
void show_graph_image();

// Выводит результаты и создаёт визуализацию.
void display_result(
    const Graph& graph,
    const Config& config,
    bool image);
