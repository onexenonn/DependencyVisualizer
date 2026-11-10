#define NOMINMAX
#include <windows.h>
#include <comdef.h>
#import <msxml6.dll> rename_namespace("Xml")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#include "json.hpp"
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
#include <vector>

using Graph = std::map<std::string, std::vector<std::string>>;

struct Config {
    std::string package, repository, test_file, filter;
    bool test_mode = true;
    bool ascii_tree = true;
};

// Own COM initialization for the lifetime of the application.
struct ComSession {
    ComSession() {
        if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
            throw std::runtime_error("Cannot initialize COM");
    }
    ~ComSession() { CoUninitialize(); }
    ComSession(const ComSession&) = delete;
    ComSession& operator=(const ComSession&) = delete;
};

// Remove surrounding ASCII whitespace.
std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

// Convert COM strings without losing Unicode JSON characters.
std::string utf8(const _bstr_t& text) {
    const int length = static_cast<int>(text.length());
    if (!length) return "";
    const int size = WideCharToMultiByte(
        CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    if (!size) throw std::runtime_error("UTF-8 conversion failed");
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, length,
        &result[0], size, nullptr, nullptr);
    return result;
}

// Require exactly one simple XML setting.
std::string xml_value(Xml::IXMLDOMDocument2Ptr doc, const char* key) {
    const std::string path = std::string("/config/") + key;
    const auto nodes = doc->selectNodes(_bstr_t(path.c_str()));
    if (nodes->length != 1)
        throw std::runtime_error(std::string("Missing/duplicate: ") + key);
    return trim(utf8(nodes->item[0]->text));
}

// Accept strict XML boolean values used by this application.
bool parse_bool(const std::string& value, const char* name) {
    if (value == "true") return true;
    if (value == "false") return false;
    throw std::runtime_error(std::string(name) + " must be true/false");
}

// Validate crate identifiers before including them in API paths.
bool valid_package(const std::string& name) {
    return !name.empty() && name.find_first_not_of(
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-"
    ) == std::string::npos;
}

// Check parameter values independently of subsequent network requests.
void validate_config(const Config& config) {
    if (!valid_package(config.package))
        throw std::runtime_error("Invalid or empty package name");
    if (config.repository.find("https://") != 0
        || config.repository.size() <= 8
        || config.repository.find_first_of(" \t\r\n?#")
            != std::string::npos)
        throw std::runtime_error("Invalid HTTPS repository URL");
    if (config.test_mode) {
        if (config.test_file.empty())
            throw std::runtime_error("Empty test_file");
        std::ifstream file(config.test_file);
        if (!file) throw std::runtime_error("Cannot open test_file");
    }
}

// Parse XML with MSXML; prohibit external entities and DTDs.
Config load_config(const std::string& path) {
    Xml::IXMLDOMDocument2Ptr doc;
    if (FAILED(doc.CreateInstance(__uuidof(Xml::DOMDocument60))))
        throw std::runtime_error("Cannot create XML parser");
    doc->async = VARIANT_FALSE;
    doc->validateOnParse = VARIANT_FALSE;
    doc->resolveExternals = VARIANT_FALSE;
    doc->setProperty(L"ProhibitDTD", _variant_t(true));
    if (doc->load(_variant_t(path.c_str())) != VARIANT_TRUE)
        throw std::runtime_error("Cannot load " + path + ": "
            + utf8(doc->parseError->reason));
    Config c;
    c.package = xml_value(doc, "package");
    c.repository = xml_value(doc, "repository");
    c.test_file = xml_value(doc, "test_file");
    c.filter = xml_value(doc, "filter");
    c.test_mode = parse_bool(xml_value(doc, "test_mode"), "test_mode");
    c.ascii_tree = parse_bool(xml_value(doc, "ascii_tree"), "ascii_tree");
    validate_config(c);
    return c;
}

// Display all user-configurable settings.
void print_config(const Config& c) {
    std::cout << std::boolalpha << "Configuration:\n"
        << "package = " << c.package << '\n'
        << "repository = " << c.repository << '\n'
        << "test_mode = " << c.test_mode << '\n'
        << "test_file = " << c.test_file << '\n'
        << "ascii_tree = " << c.ascii_tree << '\n'
        << "filter = " << c.filter << "\n\n";
}

// Parse one nonempty, noncomment line of the test repository.
void parse_graph_line(Graph& graph, const std::string& line, int number) {
    const auto colon = line.find(':');
    if (colon == std::string::npos)
        throw std::runtime_error("Missing ':' on line "
            + std::to_string(number));
    const auto name = trim(line.substr(0, colon));
    if (name.empty() || name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ")
        != std::string::npos || graph.count(name))
        throw std::runtime_error("Invalid/duplicate test package: " + name);
    std::istringstream input(line.substr(colon + 1));
    std::set<std::string> seen;
    graph[name] = {};
    std::string dep;
    while (input >> dep) {
        if (dep.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ")
            != std::string::npos)
            throw std::runtime_error("Invalid test dependency: " + dep);
        if (seen.insert(dep).second) graph[name].push_back(dep);
    }
}

// Load the complete test repository, including isolated nodes.
Graph read_graph(const std::string& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot open " + path);
    Graph graph;
    std::string line;
    int number = 0;
    while (std::getline(file, line)) {
        ++number;
        if (number == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0)
            line.erase(0, 3);
        line = trim(line);
        if (line.empty() || line.front() == '#') continue;
        parse_graph_line(graph, line, number);
    }
    if (graph.empty()) throw std::runtime_error("Empty test repository");
    return graph;
}

// Fetch JSON using Windows HTTP, not a package-manager library.
nlohmann::json request_json(const std::string& url) {
    Sleep(1100);
    Xml::IServerXMLHTTPRequest2Ptr request;
    if (FAILED(request.CreateInstance(__uuidof(Xml::ServerXMLHTTP60))))
        throw std::runtime_error("Cannot create HTTP client");
    request->setTimeouts(10000, 10000, 10000, 30000);
    request->open(_bstr_t("GET"), _bstr_t(url.c_str()), _variant_t(false));
    request->setRequestHeader(_bstr_t("User-Agent"),
        _bstr_t("DependencyVisualizer-EducationalProject/1.0"));
    request->send();
    if (request->status != 200)
        throw std::runtime_error("HTTP " + std::to_string(request->status)
            + ": " + url);
    return nlohmann::json::parse(utf8(request->responseText));
}

// Use latest version per name; collect all declared normal dependencies.
std::vector<std::string> fetch_dependencies(std::string base,
    const std::string& package) {
    if (!valid_package(package))
        throw std::runtime_error("Invalid crate name: " + package);
    while (!base.empty() && base.back() == '/') base.pop_back();
    base += "/api/v1/crates/" + package;
    const auto metadata = request_json(base);
    const auto version = metadata.at("crate").at("max_version")
        .get<std::string>();
    const auto data = request_json(base + "/" + version + "/dependencies");
    std::set<std::string> names;
    for (const auto& dep : data.at("dependencies")) {
        if (dep.value("kind", std::string()) == "normal")
            names.insert(dep.at("crate_id").get<std::string>());
    }
    std::cout << "Fetched: " << package << " " << version << std::endl;
    return {names.begin(), names.end()};
}

using Provider = std::function<std::vector<std::string>(const std::string&)>;

// Shared nonrecursive BFS for both network and local repositories.
Graph build_graph(const Provider& provider, const std::string& root,
    const std::string& filter) {
    Graph result;
    std::queue<std::string> pending;
    std::set<std::string> discovered = {root};
    pending.push(root);
    while (!pending.empty()) {
        const auto name = pending.front();
        pending.pop();
        result[name] = {};
        const auto dependencies = provider(name);
        std::set<std::string> unique;
        for (const auto& dep : dependencies) {
            if (!filter.empty() && dep.find(filter) != std::string::npos)
                continue;
            if (!unique.insert(dep).second) continue;
            result[name].push_back(dep);
            if (discovered.insert(dep).second) {
                if (discovered.size() > 200)
                    throw std::runtime_error("Safety limit: 200 packages");
                pending.push(dep);
            }
        }
    }
    return result;
}

// Adapt a local repository to the shared BFS interface.
Graph build_test_graph(const Graph& repository, const std::string& root,
    const std::string& filter) {
    return build_graph([&repository](const std::string& name) {
        const auto found = repository.find(name);
        if (found == repository.end())
            throw std::runtime_error("Unknown test package: " + name);
        return found->second;
    }, root, filter);
}

// Print direct dependencies for every reachable package.
void print_graph(const Graph& graph) {
    std::cout << "\nDependency graph:\n";
    for (const auto& item : graph) {
        std::cout << item.first << " -> ";
        if (item.second.empty()) std::cout << "(no dependencies)";
        for (const auto& dep : item.second) std::cout << dep << ' ';
        std::cout << '\n';
    }
    std::cout << "Total packages: " << graph.size() << '\n';
}

// Kahn's algorithm on reversed edges: dependencies precede users.
std::vector<std::string> get_load_order(const Graph& graph) {
    std::map<std::string, std::size_t> remaining;
    Graph users;
    for (const auto& item : graph) {
        remaining[item.first] = item.second.size();
        for (const auto& dep : item.second) users[dep].push_back(item.first);
    }
    std::queue<std::string> ready;
    for (const auto& item : remaining)
        if (!item.second) ready.push(item.first);
    std::vector<std::string> order;
    while (!ready.empty()) {
        const auto name = ready.front();
        ready.pop();
        order.push_back(name);
        for (const auto& user : users[name])
            if (--remaining.at(user) == 0) ready.push(user);
    }
    if (order.size() != graph.size())
        throw std::runtime_error("Dependency cycle: no valid load order");
    return order;
}

// Escape identifiers for the DOT language.
std::string dot_quote(const std::string& text) {
    std::string result = "\"";
    for (const char c : text) {
        if (c == '\\' || c == '"') result += '\\';
        if (c == '\n') result += "\\n";
        else if (c != '\r') result += c;
    }
    return result + '"';
}

// Save explicit nodes so isolated packages also appear in the image.
void save_dot(const Graph& graph) {
    std::ofstream file("graph.dot");
    if (!file) throw std::runtime_error("Cannot create graph.dot");
    file << "digraph dependencies {\n  rankdir=LR;\n  node [shape=box];\n";
    for (const auto& item : graph) {
        file << "  " << dot_quote(item.first) << ";\n";
        for (const auto& dep : item.second)
            file << "  " << dot_quote(item.first) << " -> "
                << dot_quote(dep) << ";\n";
    }
    file << "}\n";
    file.close();
    if (!file) throw std::runtime_error("Cannot finish graph.dot");
    std::cout << "Saved: graph.dot\n";
}

// Produce a finite ASCII view even when shared nodes or cycles exist.
void print_ascii_tree(const Graph& graph, const std::string& root) {
    struct Entry { std::string name, prefix; bool last, root; };
    std::vector<Entry> stack = {{root, "", true, true}};
    std::set<std::string> expanded;
    std::cout << "\nASCII tree:\n";
    while (!stack.empty()) {
        const auto entry = stack.back();
        stack.pop_back();
        std::cout << entry.prefix;
        if (!entry.root) std::cout << (entry.last ? "`-- " : "|-- ");
        std::cout << entry.name;
        if (!expanded.insert(entry.name).second) {
            std::cout << " [already shown]\n";
            continue;
        }
        std::cout << '\n';
        auto prefix = entry.prefix;
        if (!entry.root) prefix += entry.last ? "    " : "|   ";
        const auto& deps = graph.at(entry.name);
        for (std::size_t i = deps.size(); i > 0; --i)
            stack.push_back({deps[i - 1], prefix, i == deps.size(), false});
    }
}

// Render fixed output paths; no user text is inserted into shell commands.
void show_graph_image() {
    const int result = std::system("dot -Tpng graph.dot -o graph.png");
    if (result != 0) {
        std::cerr << "WARNING: PNG not created. Check dot -V.\n";
        return;
    }
    std::cout << "Saved: graph.png\n";
    if (std::system("start \"\" \"graph.png\"") != 0)
        std::cerr << "WARNING: open graph.png manually\n";
}

// Execute graph operations shared by normal and test modes.
void display_result(const Graph& graph, const Config& config, bool image) {
    print_graph(graph);
    save_dot(graph);
    if (config.ascii_tree) print_ascii_tree(graph, config.package);
    try {
        const auto order = get_load_order(graph);
        std::cout << "\nLoad order: ";
        for (std::size_t i = 0; i < order.size(); ++i) {
            if (i) std::cout << " -> ";
            std::cout << order[i];
        }
        std::cout << '\n';
    } catch (const std::runtime_error& error) {
        std::cout << "\nLoad order unavailable: " << error.what() << '\n';
    }
    if (image) show_graph_image();
}

// Fail a test in release as well as debug configurations.
void check(bool condition, const char* name, int& count) {
    if (!condition) throw std::runtime_error(std::string("FAIL: ") + name);
    ++count;
    std::cout << "PASS: " << name << '\n';
}

// Automated local tests do not require HTTP or Graphviz.
void run_tests() {
    int count = 0;
    Graph repo = {{"A", {"B", "C"}}, {"B", {"D"}},
        {"C", {"D", "E"}}, {"D", {"F"}}, {"E", {"F"}},
        {"F", {}}, {"Z", {}}};
    const auto graph = build_test_graph(repo, "A", "");
    check(graph.size() == 6, "BFS reachable nodes", count);
    check(!graph.count("Z"), "Unreachable nodes excluded", count);
    const auto filtered = build_test_graph(repo, "A", "E");
    check(!filtered.count("E"), "Filter excludes node", count);
    check(filtered.at("C").size() == 1, "Filter excludes edge", count);
    const auto order = get_load_order(graph);
    std::map<std::string, std::size_t> positions;
    for (std::size_t i = 0; i < order.size(); ++i) positions[order[i]] = i;
    bool valid = order.size() == graph.size();
    for (const auto& item : graph)
        for (const auto& dep : item.second)
            valid = valid && positions.at(dep) < positions.at(item.first);
    check(valid, "Dependency-first order", count);
    const Graph cycle = {{"A", {"B"}}, {"B", {"A"}}};
    check(build_test_graph(cycle, "A", "").size() == 2,
        "BFS terminates on cycle", count);
    bool detected = false;
    try { get_load_order(cycle); }
    catch (const std::runtime_error&) { detected = true; }
    check(detected, "Cycle detected", count);
    check(get_load_order({{"A", {}}}) == std::vector<std::string>{"A"},
        "Isolated root", count);
    bool rejected = false;
    try { build_test_graph(repo, "MISSING", ""); }
    catch (const std::runtime_error&) { rejected = true; }
    check(rejected, "Unknown root rejected", count);
    rejected = false;
    try { parse_bool("maybe", "test_mode"); }
    catch (const std::runtime_error&) { rejected = true; }
    check(rejected, "Invalid boolean rejected", count);
    check(parse_bool("true", "x") && !parse_bool("false", "x"),
        "Valid booleans", count);
    check(dot_quote("A\"B") == "\"A\\\"B\"", "DOT quoting", count);
    std::cout << "ALL " << count << " TESTS PASSED\n";
}

// CLI: optional config path plus mode switches, documented in README.
int main(int argc, char* argv[]) {
    try {
        ComSession session;
        std::string path = "config.xml";
        bool direct = false, image = true, tests = false;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--test") tests = true;
            else if (arg == "--direct") direct = true;
            else if (arg == "--no-image") image = false;
            else if (arg.rfind("--", 0) == 0)
                throw std::runtime_error("Unknown option: " + arg);
            else path = arg;
        }
        if (tests) { run_tests(); return 0; }
        const auto config = load_config(path);
        print_config(config);
        Graph graph;
        if (config.test_mode) {
            const auto repository = read_graph(config.test_file);
            graph = build_test_graph(repository, config.package, config.filter);
        } else if (direct) {
            const auto deps = fetch_dependencies(config.repository, config.package);
            std::cout << "Direct dependencies:\n";
            if (deps.empty()) std::cout << "(none)\n";
            for (const auto& dep : deps) std::cout << dep << '\n';
            return 0;
        } else {
            graph = build_graph([&config](const std::string& package) {
                return fetch_dependencies(config.repository, package);
            }, config.package, config.filter);
        }
        display_result(graph, config, image);
        return 0;
    } catch (const _com_error& error) {
        std::cerr << "ERROR: XML/HTTP HRESULT = " << std::hex
            << error.Error() << '\n';
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
    }
    return 1;
}
