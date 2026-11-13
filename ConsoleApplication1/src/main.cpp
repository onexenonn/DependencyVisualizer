
// cd /d "C:\Users\User\Desktop\DependencyVisualizer\ConsoleApplication1"
//set "PATH=C:\Program Files\Graphviz\bin;%PATH%"
// dot -V  


#define NOMINMAX
#include <windows.h>       
#include <comdef.h>        
#import <msxml6.dll> rename_namespace("Xml")
#pragma comment(lib, "ole32.lib")   
#pragma comment(lib, "oleaut32.lib") 

#include <algorithm>     
#include <cstdlib>       
#include <fstream>       
#include <functional>   
#include <iostream>    
#include <map>           
#include <queue>        
#include <set>          
#include <sstream>      
#include <stdexcept>     
#include <string>        
#include <utility>       
#include <vector>        

// A -> B C
// B -> D
// C -> D E
using Graph = std::map<std::string, std::vector<std::string>>;


// Э1
// из XML-файла.
struct Config {
    std::string package;     
    std::string repository;  
    std::string test_file;  
    std::string filter;      

    bool test_mode = true;   // true — читать граф из файла; false — crates.io.
    bool ascii_tree = true;  
};


// создаем COM  для MSXML для самого парсера и клиента.
struct ComSession {
    ComSession() {
        // Инициализируем COM в однопоточный режим
        if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
            throw std::runtime_error("Cannot initialize COM");
    }
    ~ComSession() {
        CoUninitialize();
    }

    // запрет на копирование 
    ComSession(const ComSession&) = delete;
    ComSession& operator=(const ComSession&) = delete;
};

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");

    if (first == std::string::npos)
        return "";

    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

// Преобразует строку COM в обычную UTF-8 строку C+/ чтобы корректно работать с символами Unicode
std::string utf8(const _bstr_t& text) {
    const int length = static_cast<int>(text.length());

    if (!length)
        return "";

    // к-во байтов 
    const int size = WideCharToMultiByte(
        CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);

    if (!size)
        throw std::runtime_error("UTF-8 conversion failed");

    std::string result(size, '\0');

    // само преобразование
    WideCharToMultiByte(
        CP_UTF8, 0, text, length,
        &result[0], size, nullptr, nullptr);

    return result;
}

std::string xml_value(Xml::IXMLDOMDocument2Ptr doc, const char* key) {
    const std::string path = std::string("/config/") + key;

    const auto nodes = doc->selectNodes(_bstr_t(path.c_str()));

    if (nodes->length != 1)
        throw std::runtime_error(
            std::string("Missing/duplicate: ") + key);

    return trim(utf8(nodes->item[0]->text));
}

bool parse_bool(const std::string& value, const char* name) {
    if (value == "true")
        return true;

    if (value == "false")
        return false;

    throw std::runtime_error(
        std::string(name) + " must be true/false");
}

bool valid_package(const std::string& name) {
    return !name.empty()
        && name.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyz"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "0123456789_-") == std::string::npos;
}

// Проверка на корректность всех настроек до начала работы с сетью
void validate_config(const Config& config) {
    if (!valid_package(config.package))
        throw std::runtime_error("Invalid or empty package name");

    if (config.repository.find("https://") != 0 // насторойка для репозитория и проверка на сущ файл с текстовым графр 
        || config.repository.size() <= 8
        || config.repository.find_first_of(" \t\r\n?#")
        != std::string::npos)
        throw std::runtime_error("Invalid HTTPS repository URL");

    if (config.test_mode) {
        if (config.test_file.empty())
            throw std::runtime_error("Empty test_file");

        std::ifstream file(config.test_file);

        if (!file)
            throw std::runtime_error("Cannot open test_file");
    }
}

// Загружает и проверяет XML-конфигурацию.
Config load_config(const std::string& path) {
    Xml::IXMLDOMDocument2Ptr doc;

    if (FAILED(doc.CreateInstance(__uuidof(Xml::DOMDocument60)))) // Создаём XML-парсер MSXML версии 6, синхронная загрузка, без хSD-схемы, Запрещаем DTD
        throw std::runtime_error("Cannot create XML parser");

    doc->async = VARIANT_FALSE;

    doc->validateOnParse = VARIANT_FALSE;

    // Запрещаем загрузку внешних ресурсов из XML.
    doc->resolveExternals = VARIANT_FALSE;

    doc->setProperty(L"ProhibitDTD", _variant_t(true));

    if (doc->load(_variant_t(path.c_str())) != VARIANT_TRUE)
        throw std::runtime_error(
            "Cannot load " + path + ": "
            + utf8(doc->parseError->reason));

    Config config;

//чтение
    config.package = xml_value(doc, "package");
    config.repository = xml_value(doc, "repository");
    config.test_file = xml_value(doc, "test_file");
    config.filter = xml_value(doc, "filter");

    config.test_mode =
        parse_bool(xml_value(doc, "test_mode"), "test_mode");

    config.ascii_tree =
        parse_bool(xml_value(doc, "ascii_tree"), "ascii_tree");

    validate_config(config);

    return config;
}

// настройки программы
void print_config(const Config& config) {
    std::cout << std::boolalpha
        << "Configuration:\n"
        << "package = " << config.package << '\n'
        << "repository = " << config.repository << '\n'
        << "test_mode = " << config.test_mode << '\n'
        << "test_file = " << config.test_file << '\n'
        << "ascii_tree = " << config.ascii_tree << '\n'
        << "filter = " << config.filter << "\n\n";
}


// Э3 (тестовый режим). ЧТЕНИЕ ЛОКАЛЬНОГО ГРАФА

// Разбирает одну строку тестового файла.
// Пример строки:
// A: B C
void parse_graph_line(
    Graph& graph,
    const std::string& line,
    int number) {

    const auto colon = line.find(':');

    if (colon == std::string::npos)
        throw std::runtime_error(
            "Missing ':' on line " + std::to_string(number));

    // 
    const auto name = trim(line.substr(0, colon));

    if (name.empty()
        || name.find_first_not_of(
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ") != std::string::npos
        || graph.count(name))
        throw std::runtime_error(
            "Invalid/duplicate test package: " + name);

    // Зависимости находятся после двоеточия.
    std::istringstream input(line.substr(colon + 1));

    // Защита от повторяющихся зависимостей в одной строке.
    std::set<std::string> seen;

    graph[name] = {};

    std::string dependency;

    // Читаем зависимости через пробел.
    while (input >> dependency) {
        if (dependency.find_first_not_of(
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ") != std::string::npos)
            throw std::runtime_error(
                "Invalid test dependency: " + dependency);

        // Добавляем зависимость только один раз.
        if (seen.insert(dependency).second)
            graph[name].push_back(dependency);
    }
}

// Читает весь тестовый файл с описанием графа.
// Поддерживает пустые строки, комментарии и BOM в начале файла.
Graph read_graph(const std::string& path) {
    std::ifstream file(path);

    if (!file)
        throw std::runtime_error("Cannot open " + path);

    Graph graph;
    std::string line;
    int number = 0;

    // Читаем файл построчно.
    while (std::getline(file, line)) {
        ++number;

        // Удаляем BOM, если он есть в первой строке.
        if (number == 1
            && line.compare(0, 3, "\xEF\xBB\xBF") == 0)
            line.erase(0, 3);

        line = trim(line);

        // Пустые строки и комментарии игнорируем.
        if (line.empty() || line.front() == '#')
            continue;

        parse_graph_line(graph, line, number);
    }

    // Файл не должен быть пустым.
    if (graph.empty())
        throw std::runtime_error("Empty test repository");

    return graph;
}


// Э2. ПОЛУЧЕНИЕ ДАННЫХ И РАЗБОР JSON

// ище минимальное JSON-значение.
struct JsonValue {
    
    enum Type {
        Null,     
        Boolean,  
        Number,   
        String,   
        Array,    
        Object    
    };

    Type type = Null;

    std::string text;                            // екст, элементы, пполя
    std::vector<JsonValue> items;               
    std::map<std::string, JsonValue> fields;     


    const JsonValue& at(const std::string& key) const {
        if (type != Object)
            throw std::runtime_error("Expected JSON object");

        const auto found = fields.find(key);

        if (found == fields.end())
            throw std::runtime_error(
                "Missing JSON field: " + key);

        return found->second;
    }

    // значение_-строку.
    const std::string& as_string() const {
        if (type != String)
            throw std::runtime_error("Expected JSON string");

        return text;
    }

    // Возвращает значение как массив.
    const std::vector<JsonValue>& as_array() const {
        if (type != Array)
            throw std::runtime_error("Expected JSON array");

        return items;
    }
};

// Небольшой JSON-парсер, достаточный для ответов crates.io.
class JsonParser {
    const std::string& source;   // Исходный JSON-текст.
    std::size_t position = 0;    // Текущая позиция чтения.

    // Сообщает об ошибке и указывает позицию в JSON.
    void fail() const {
        throw std::runtime_error(
            "Invalid JSON near byte "
            + std::to_string(position));
    }

    // Возвращает текущий символ, не сдвигая позицию.
    char peek() const {
        return position < source.size()
            ? source[position]
            : '\0';
    }

    // Пропускает пробелы, табуляции и переводы строк.
    void whitespace() {
        while (position < source.size()
            && (source[position] == ' '
                || source[position] == '\t'
                || source[position] == '\r'
                || source[position] == '\n'))
            ++position;
    }

    // Если следующий символ совпадает с ожидаемым, пропускает его.
    bool take(char character) {
        whitespace();

        if (position < source.size()
            && source[position] == character) {
            ++position;
            return true;
        }

        return false;
    }

    // Требует конкретный символ, иначе сообщает об ошибке.
    void expect(char character) {
        if (!take(character))
            fail();
    }

    // Проверяет, является ли символ цифрой.
    static bool digit(char character) {
        return character >= '0' && character <= '9';
    }

    // Читает четыре шестнадцатеричных символа после \u.
    unsigned hex4() {
        unsigned value = 0;

        for (int index = 0; index < 4; ++index) {
            if (position >= source.size())
                fail();

            const char character = source[position++];

            unsigned part;

            if (character >= '0' && character <= '9')
                part = character - '0';
            else if (character >= 'a' && character <= 'f')
                part = character - 'a' + 10;
            else if (character >= 'A' && character <= 'F')
                part = character - 'A' + 10;
            else {
                fail();
                return 0;
            }

            value = value * 16 + part;
        }

        return value;
    }

    // Преобразует Unicode-код в UTF-8.
    static void append_utf8(std::string& result, unsigned code) {
        if (code <= 0x7F) {
            result += static_cast<char>(code);
        }
        else if (code <= 0x7FF) {
            result += static_cast<char>(0xC0 | (code >> 6));
            result += static_cast<char>(0x80 | (code & 0x3F));
        }
        else if (code <= 0xFFFF) {
            result += static_cast<char>(0xE0 | (code >> 12));
            result += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (code & 0x3F));
        }
        else {
            result += static_cast<char>(0xF0 | (code >> 18));
            result += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            result += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    // Читает JSON-строку, включая escape-последовательности.
    std::string parse_string() {
        expect('"');

        std::string result;

        while (position < source.size()) {
            const unsigned char character =
                static_cast<unsigned char>(source[position++]);

            // Кавычка означает конец строки.
            if (character == '"')
                return result;

            // Управляющие символы без экранирования запрещены.
            if (character < 0x20)
                fail();

            // Обычный символ добавляем как есть.
            if (character != '\\') {
                result += static_cast<char>(character);
                continue;
            }

            if (position >= source.size())
                fail();

            const char escaped = source[position++];

            switch (escaped) {
            case '"':
                result += '"';
                break;

            case '\\':
                result += '\\';
                break;

            case '/':
                result += '/';
                break;

            case 'b':
                result += '\b';
                break;

            case 'f':
                result += '\f';
                break;

            case 'n':
                result += '\n';
                break;

            case 'r':
                result += '\r';
                break;

            case 't':
                result += '\t';
                break;

            case 'u': {
                unsigned code = hex4();

                // Обрабатываем surrogate pairs для символов вне BMP.
                if (code >= 0xD800 && code <= 0xDBFF) {
                    if (position + 2 > source.size()
                        || source[position] != '\\'
                        || source[position + 1] != 'u')
                        fail();

                    position += 2;

                    const unsigned low = hex4();

                    if (low < 0xDC00 || low > 0xDFFF)
                        fail();

                    code = 0x10000
                        + ((code - 0xD800) << 10)
                        + (low - 0xDC00);
                }
                else if (code >= 0xDC00 && code <= 0xDFFF) {
                    fail();
                }

                append_utf8(result, code);
                break;
            }

            default:
                fail();
            }
        }

        fail();
        return "";
    }

    // Читает число: целое, десятичное или экспоненциальное.
    JsonValue parse_number() {
        const auto start = position;

        if (peek() == '-')
            ++position;

        if (peek() == '0') {
            ++position;
        }
        else {
            if (peek() < '1' || peek() > '9')
                fail();

            while (digit(peek()))
                ++position;
        }

        if (peek() == '.') {
            ++position;

            if (!digit(peek()))
                fail();

            while (digit(peek()))
                ++position;
        }

        if (peek() == 'e' || peek() == 'E') {
            ++position;

            if (peek() == '+' || peek() == '-')
                ++position;

            if (!digit(peek()))
                fail();

            while (digit(peek()))
                ++position;
        }

        JsonValue result;
        result.type = JsonValue::Number;
        result.text = source.substr(start, position - start);
        return result;
    }

    // Читает true, false или null.
    JsonValue literal(const char* word, JsonValue::Type type) {
        const std::string expected = word;

        if (source.compare(
            position, expected.size(), expected) != 0)
            fail();

        position += expected.size();

        JsonValue result;
        result.type = type;
        result.text = expected;
        return result;
    }

    // Рекурсивно читает любое JSON-значение.
    JsonValue value(unsigned depth) {
        // Защита от слишком глубокой вложенности.
        if (depth > 128)
            throw std::runtime_error(
                "JSON nesting limit exceeded");

        whitespace();

        JsonValue result;

        switch (peek()) {
        case '{':
            ++position;
            result.type = JsonValue::Object;

            // Пустой объект: {}
            if (take('}'))
                return result;

            do {
                const auto key = parse_string();
                expect(':');
                auto child = value(depth + 1);

                // Повторяющиеся ключи запрещены.
                if (!result.fields.emplace(key, std::move(child)).second)
                    throw std::runtime_error(
                        "Duplicate JSON field: " + key);
            } while (take(','));

            expect('}');
            return result;

        case '[':
            ++position;
            result.type = JsonValue::Array;

            // Пустой массив: []
            if (take(']'))
                return result;

            do {
                result.items.push_back(value(depth + 1));
            } while (take(','));

            expect(']');
            return result;

        case '"':
            result.type = JsonValue::String;
            result.text = parse_string();
            return result;

        case 't':
            return literal("true", JsonValue::Boolean);

        case 'f':
            return literal("false", JsonValue::Boolean);

        case 'n':
            return literal("null", JsonValue::Null);

        default:
            return parse_number();
        }
    }

public:
    explicit JsonParser(const std::string& input)
        : source(input) {
    }

    // Разбирает весь JSON-текст.
    JsonValue parse() {
        auto result = value(0);
        whitespace();

        // После корректного JSON не должно остаться лишних символов.
        if (position != source.size())
            fail();

        return result;
    }
};

// Выполняет HTTP GET-запрос и разбирает ответ как JSON.
JsonValue request_json(const std::string& url) {
    // Пауза между запросами снижает нагрузку на crates.io.
    Sleep(1100);

    Xml::IServerXMLHTTPRequest2Ptr request;

    // Создаём HTTP-клиент MSXML.
    if (FAILED(request.CreateInstance(
        __uuidof(Xml::ServerXMLHTTP60))))
        throw std::runtime_error("Cannot create HTTP client");

    // Таймауты: DNS, соединение, отправка, получение ответа.
    request->setTimeouts(10000, 10000, 10000, 30000);

    // Синхронный GET-запрос.
    request->open(
        _bstr_t("GET"),
        _bstr_t(url.c_str()),
        _variant_t(false));

    // Идентифицируем программу для сервера.
    request->setRequestHeader(
        _bstr_t("User-Agent"),
        _bstr_t("DependencyVisualizer-EducationalProject/1.0"));

    request->send();

    // Код 200 означает успешный ответ.
    if (request->status != 200)
        throw std::runtime_error(
            "HTTP " + std::to_string(request->status)
            + ": " + url);

    const std::string response = utf8(request->responseText);

    // Разбираем JSON без сторонних библиотек.
    return JsonParser(response).parse();
}

// Получает список обычных зависимостей пакета.
//
// Сначала запрашивается информация о пакете,
// затем — зависимости его последней версии.
std::vector<std::string> fetch_dependencies(
    std::string base,
    const std::string& package) {

    // Проверяем имя пакета перед вставкой в URL.
    if (!valid_package(package))
        throw std::runtime_error(
            "Invalid crate name: " + package);

    // Убираем лишний слэш в конце адреса репозитория.
    while (!base.empty() && base.back() == '/')
        base.pop_back();

    // Формируем адрес информации о пакете.
    base += "/api/v1/crates/" + package;

    const auto metadata = request_json(base);

    // Берём последнюю доступную версию пакета.
    const std::string version =
        metadata.at("crate").at("max_version").as_string();

    // Проверяем версию перед вставкой в URL.
    if (version.empty()
        || version.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyz"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "0123456789.-_+") != std::string::npos)
        throw std::runtime_error("Invalid crate version");

    // Запрашиваем зависимости выбранной версии.
    const auto data =
        request_json(base + "/" + version + "/dependencies");

    // Множество автоматически убирает дубликаты.
    std::set<std::string> names;

    for (const auto& dependency : data.at("dependencies").as_array()) {
        // Берём только обычные зависимости, а не dev/build.
        if (dependency.at("kind").as_string() == "normal") {
            const auto& name =
                dependency.at("crate_id").as_string();

            if (!valid_package(name))
                throw std::runtime_error(
                    "Invalid dependency name: " + name);

            names.insert(name);
        }
    }

    std::cout << "Fetched: " << package << " "
        << version << std::endl;

    return { names.begin(), names.end() };
}

// ============================================================================
// ЭТАП 3. ПОСТРОЕНИЕ ГРАФА ЗАВИСИМОСТЕЙ
// ============================================================================

// Функция, которая по имени пакета возвращает его зависимости.
using Provider =
std::function<std::vector<std::string>(const std::string&)>;

// Строит граф зависимостей методом BFS (обход в ширину).
//
// Алгоритм:
// 1. Берём начальный пакет.
// 2. Получаем его зависимости.
// 3. Добавляем новые пакеты в очередь.
// 4. Повторяем, пока очередь не опустеет.
Graph build_graph(
    const Provider& provider,
    const std::string& root,
    const std::string& filter) {

    Graph result;

    // Очередь пакетов, которые ещё нужно обработать.
    std::queue<std::string> pending;

    // Множество уже обнаруженных пакетов.
    std::set<std::string> discovered = { root };

    pending.push(root);

    while (!pending.empty()) {
        // Берём следующий пакет из очереди.
        const auto name = pending.front();
        pending.pop();

        result[name] = {};

        // Получаем зависимости текущего пакета.
        const auto dependencies = provider(name);

        // Защита от повторов в списке зависимостей.
        std::set<std::string> unique;

        for (const auto& dependency : dependencies) {
            // Фильтр исключает зависимости по подстроке.
            if (!filter.empty()
                && dependency.find(filter) != std::string::npos)
                continue;

            if (!unique.insert(dependency).second)
                continue;

            result[name].push_back(dependency);

            // Новый пакет добавляется в очередь только один раз.
            if (discovered.insert(dependency).second) {
                // Ограничение защищает от слишком большого графа.
                if (discovered.size() > 200)
                    throw std::runtime_error(
                        "Safety limit: 200 packages");

                pending.push(dependency);
            }
        }
    }

    return result;
}

// Адаптирует локальный тестовый репозиторий для общего BFS.
Graph build_test_graph(
    const Graph& repository,
    const std::string& root,
    const std::string& filter) {

    return build_graph(
        [&repository](const std::string& name) {
            const auto found = repository.find(name);

            // Пакет должен быть описан в тестовом файле.
            if (found == repository.end())
                throw std::runtime_error(
                    "Unknown test package: " + name);

            return found->second;
        },
        root,
        filter);
}

// Выводит все пакеты и их прямые зависимости.
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

//// ============================================================================
//// ЭТАП 4. ПОРЯДОК ЗАГРУЗКИ
//// ============================================================================
//
//// Строит порядок загрузки с помощью алгоритма Кана.
////
//// Сначала обрабатываются пакеты без зависимостей,
//// затем пакеты, у которых все зависимости уже готовы.
//std::vector<std::string> get_load_order(const Graph& graph) {
//    // Сколько зависимостей ещё не обработано у каждого пакета.
//    std::map<std::string, std::size_t> remaining;
//
//    // Кто зависит от данного пакета.
//    Graph users;
//
//    for (const auto& item : graph) {
//        remaining[item.first] = item.second.size();
//
//        for (const auto& dependency : item.second)
//            users[dependency].push_back(item.first);
//    }
//
//    // В очередь сначала попадают пакеты без зависимостей.
//    std::queue<std::string> ready;
//
//    for (const auto& item : remaining)
//        if (!item.second)
//            ready.push(item.first);
//
//    std::vector<std::string> order;
//
//    while (!ready.empty()) {
//        const auto name = ready.front();
//        ready.pop();
//
//        order.push_back(name);
//
//        // После обработки пакета уменьшаем счётчики зависимых пакетов.
//        for (const auto& user : users[name])
//            if (--remaining.at(user) == 0)
//                ready.push(user);
//    }
//
//    // Если не все пакеты попали в порядок, существует цикл.
//    if (order.size() != graph.size())
//        throw std::runtime_error(
//            "Dependency cycle: no valid load order");
//
//    return order;
//}
//
//// ============================================================================
//// ЭТАП 5. ВИЗУАЛИЗАЦИЯ
//// ============================================================================
//
//// Экранирует имя пакета для языка DOT.
//std::string dot_quote(const std::string& text) {
//    std::string result = "\"";
//
//    for (const char character : text) {
//        // Экранируем обратный слэш и кавычку.
//        if (character == '\\' || character == '"')
//            result += '\\';
//
//        if (character == '\n')
//            result += "\\n";
//        else if (character != '\r')
//            result += character;
//    }
//
//    return result + '"';
//}
//
//// Сохраняет граф в формате Graphviz DOT.
//void save_dot(const Graph& graph) {
//    std::ofstream file("graph.dot");
//
//    if (!file)
//        throw std::runtime_error("Cannot create graph.dot");
//
//    file << "digraph dependencies {\n";
//    file << "  rankdir=LR;\n";
//    file << "  node [shape=box];\n";
//
//    // Добавляем все узлы, включая пакеты без зависимостей.
//    for (const auto& item : graph) {
//        file << "  "
//            << dot_quote(item.first) << ";\n";
//
//        // Добавляем стрелки от пакета к его зависимостям.
//        for (const auto& dependency : item.second)
//            file << "  "
//            << dot_quote(item.first) << " -> "
//            << dot_quote(dependency) << ";\n";
//    }
//
//    file << "}\n";
//    file.close();
//
//    if (!file)
//        throw std::runtime_error("Cannot finish graph.dot");
//
//    std::cout << "Saved: graph.dot\n";
//}
//
//// Выводит ASCII-дерево зависимостей в консоль.
//void print_ascii_tree(
//    const Graph& graph,
//    const std::string& root) {
//
//    struct Entry {
//        std::string name;    // Имя пакета.
//        std::string prefix;  // Отступы и линии дерева.
//        bool last;           // Последняя ли это зависимость.
//        bool root;           // Корневой ли это пакет.
//    };
//
//    std::vector<Entry> stack = {
//        { root, "", true, true }
//    };
//
//    // Пакеты, которые уже были показаны.
//    std::set<std::string> expanded;
//
//    std::cout << "\nASCII tree:\n";
//
//    while (!stack.empty()) {
//        const auto entry = stack.back();
//        stack.pop_back();
//
//        std::cout << entry.prefix;
//
//        if (!entry.root)
//            std::cout << (entry.last ? "`-- " : "|-- ");
//
//        std::cout << entry.name;
//
//        // Повторный пакет не раскрываем второй раз.
//        if (!expanded.insert(entry.name).second) {
//            std::cout << " [already shown]\n";
//            continue;
//        }
//
//        std::cout << '\n';
//
//        auto prefix = entry.prefix;
//
//        if (!entry.root)
//            prefix += entry.last ? "    " : "|   ";
//
//        const auto& dependencies = graph.at(entry.name);
//
//        // Добавляем зависимости в обратном порядке,
//        // чтобы они выводились в правильном порядке.
//        for (std::size_t index = dependencies.size(); index > 0; --index)
//            stack.push_back({
//                dependencies[index - 1],
//                prefix,
//                index == dependencies.size(),
//                false
//                });
//    }
//}
//
//// Создаёт graph.png через Graphviz и открывает его.
//void show_graph_image() {
//    const int result =
//        std::system("dot -Tpng graph.dot -o graph.png");
//
//    if (result != 0) {
//        std::cerr
//            << "WARNING: PNG not created. Check dot -V.\n";
//        return;
//    }
//
//    std::cout << "Saved: graph.png\n";
//
//    // Открываем PNG стандартным просмотрщиком Windows.
//    if (std::system("start \"\" \"graph.png\"") != 0)
//        std::cerr << "WARNING: open graph.png manually\n";
//}
//
//// Выводит граф, сохраняет DOT, строит ASCII-дерево,
//// определяет порядок загрузки и создаёт PNG.
//void display_result(
//    const Graph& graph,
//    const Config& config,
//    bool image) {
//
//    print_graph(graph);
//    save_dot(graph);
//
//    if (config.ascii_tree)
//        print_ascii_tree(graph, config.package);
//
//    try {
//        const auto order = get_load_order(graph);
//
//        std::cout << "\nLoad order: ";
//
//        for (std::size_t index = 0; index < order.size(); ++index) {
//            if (index)
//                std::cout << " -> ";
//
//            std::cout << order[index];
//        }
//
//        std::cout << '\n';
//    }
//    catch (const std::runtime_error& error) {
//        std::cout << "\nLoad unavailable: "
//            << error.what() << '\n';
//    }
//
//    if (image)
//        show_graph_image();
//}
//
//// ============================================================================
//// ТЕСТЫ
//// ============================================================================
//
//// Проверяет условие и увеличивает счётчик пройденных тестов.
//void check(
//    bool condition,
//    const char* name,
//    int& count) {
//
//    if (!condition)
//        throw std::runtime_error(
//            std::string("FAIL: ") + name);
//
//    ++count;
//    std::cout << "PASS: " << name << '\n';
//}
//
//// Запускает локальные тесты без интернета и Graphviz.
//void run_tests() {
//    int count = 0;
//
//    // Тестовый репозиторий пакетов.
//    const Graph repository = {
//        { "A",{ "B", "C" } },
//        { "B",{ "D" } },
//        { "C",{ "D", "E" } },
//        { "D",{ "F" } },
//        { "E",{ "F" } },
//        { "F",{} },
//        { "Z",{} }
//    };
//
//    // Строим граф от корня A.
//    const auto graph =
//        build_test_graph(repository, "A", "");
//
//    // BFS должен найти только доступные из A пакеты.
//    check(graph.size() == 6, "BFS reachable nodes", count);
//    check(!graph.count("Z"), "Unreachable nodes excluded", count);
//
//    // Фильтр должен исключить пакет E.
//    const auto filtered =
//        build_test_graph(repository, "A", "E");
//
//    check(!filtered.count("E"), "Filter excludes node", count);
//    check(filtered.at("C").size() == 1, "Filter excludes edge", count);
//
//    // Проверяем корректный порядок загрузки.
//    const auto order = get_load_order(graph);
//    std::map<std::string, std::size_t> positions;
//
//    for (std::size_t index = 0; index < order.size(); ++index)
//        positions[order[index]] = index;
//
//    bool valid = order.size() == graph.size();
//
//    // Каждая зависимость должна быть раньше зависящего пакета.
//    for (const auto& item : graph)
//        for (const auto& dependency : item.second)
//            valid = valid
//            && positions.at(dependency)
//            < positions.at(item.first);
//
//    check(valid, "Dependency-first order", count);
//
//    // Проверяем обработку цикла.
//    const Graph cycle = {
//        { "A",{ "B" } },
//        { "B",{ "A" } }
//    };
//
//    check(
//        build_test_graph(cycle, "A", "").size() == 2,
//        "BFS terminates on cycle",
//        count);
//
//    bool detected = false;
//
//    try {
//        get_load_order(cycle);
//    }
//    catch (const std::runtime_error&) {
//        detected = true;
//    }
//
//    check(detected, "Cycle detected", count);
//
//    // Проверяем пакет без зависимостей.
//    check(
//        get_load_order({ { "A",{} } })
//        == std::vector<std::string>{"A"},
//        "Isolated root",
//        count);
//
//    // Неизвестный корневой пакет должен вызвать ошибку.
//    bool rejected = false;
//
//    try {
//        build_test_graph(repository, "MISSING", "");
//    }
//    catch (const std::runtime_error&) {
//        rejected = true;
//    }
//
//    check(rejected, "Unknown root rejected", count);
//
//    // Неверное логическое значение должно вызвать ошибку.
//    rejected = false;
//
//    try {
//        parse_bool("maybe", "test_mode");
//    }
//    catch (const std::runtime_error&) {
//        rejected = true;
//    }
//
//    check(rejected, "Invalid boolean rejected", count);
//
//    // Проверяем корректные значения true и false.
//    check(
//        parse_bool("true", "x")
//        && !parse_bool("false", "x"),
//        "Valid booleans",
//        count);
//
//    // Проверяем экранирование кавычки для DOT.
//    check(
//        dot_quote("A\"B") == "\"A\\\"B\"",
//        "DOT quoting",
//        count);
//
//    std::cout << "ALL " << count << " TESTS PASSED\n";
//}
//
////ТВ
//
//// Запуск всех тестовых доков
//int main(int argc, char* argv[]) {
//    try {
//        ComSession session;
//
//        // По умолчанию используется config.xml.
//        std::string path = "config.xml";
//
//        bool direct = false;
//        bool image = true;
//        bool tests = false;
//
//        // Разбираем аргументы командной строки.
//        for (int index = 1; index < argc; ++index) {
//            const std::string argument = argv[index];
//
//            if (argument == "--test")
//                tests = true;
//            else if (argument == "--direct")
//                direct = true;
//            else if (argument == "--no-image")
//                image = false;
//            else if (argument.rfind("--", 0) == 0)
//                throw std::runtime_error(
//                    "Unknown option: " + argument);
//            else
//                path = argument;
//        }
//
//        // Режим тестирования.
//        if (tests) {
//            run_tests();
//            return 0;
//        }
//
//        // Загружаем конфигурацию.
//        const auto config = load_config(path);
//        print_config(config);
//
//        Graph graph;
//
//        // Тестовый режим: граф берётся из файла.
//        if (config.test_mode) {
//            const auto repository =
//                read_graph(config.test_file);
//
//            graph = build_test_graph(
//                repository,
//                config.package,
//                config.filter);
//        }
//        // Режим прямых зависимостей: только зависимости корневого пакета.
//        else if (direct) {
//            const auto dependencies =
//                fetch_dependencies(
//                    config.repository,
//                    config.package);
//
//            std::cout << "Direct dependencies:\n";
//
//            if (dependencies.empty())
//                std::cout << "(none)\n";
//
//            for (const auto& dependency : dependencies)
//                std::cout << dependency << '\n';
//
//            return 0;
//        }
//        // Полный сетевой режим: обход всех зависимостей.
//        else {
//            graph = build_graph(
//                [&config](const std::string& package) {
//                    return fetch_dependencies(
//                        config.repository,
//                        package);
//                },
//                config.package,
//                config.filter);
//        }
//        //картиночка тут
//        display_result(graph, config, image);
//        return 0;
//    }
//    catch (const _com_error& error) {
//        std::cerr << "ERROR: XML/HTTP HRESULT = "
//            << std::hex << error.Error() << '\n';
//    }
//    catch (const std::exception& error) {
//        // Ошибки
//        std::cerr << "ERROR: " << error.what() << '\n';
//    }
//
//    return 1;
//}
