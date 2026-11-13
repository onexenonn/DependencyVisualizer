
#include "common.h"

#include <comdef.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

#import <msxml6.dll> rename_namespace("Xml")

// Убирает пробелы, табуляции и переводы строк с начала и конца строки.
std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");

    if (first == std::string::npos)
        return "";

    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

// Преобразует строку COM (_bstr_t, UTF-16) в UTF-8.
std::string utf8(const _bstr_t& text) {
    const int length = static_cast<int>(text.length());

    if (!length)
        return "";

    // Сначала определяем необходимое количество байтов.
    const int size = WideCharToMultiByte(
        CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);

    if (!size)
        throw std::runtime_error("UTF-8 conversion failed");

    std::string result(size, '\0');

    WideCharToMultiByte(
        CP_UTF8, 0, text, length,
        &result[0], size, nullptr, nullptr);

    return result;
}

// Находит в XML ровно один тег /config/имя_тега.
std::string xml_value(Xml::IXMLDOMDocument2Ptr doc, const char* key) {
    const std::string path = std::string("/config/") + key;

    const auto nodes = doc->selectNodes(_bstr_t(path.c_str()));

    if (nodes->length != 1)
        throw std::runtime_error(
            std::string("Missing/duplicate: ") + key);

    return trim(utf8(nodes->item[0]->text));
}

// Преобразует строку "true" или "false" в bool.
bool parse_bool(const std::string& value, const char* name) {
    if (value == "true")
        return true;

    if (value == "false")
        return false;

    throw std::runtime_error(
        std::string(name) + " must be true/false");
}

// Проверяет, что имя пакета безопасно для URL.
bool valid_package(const std::string& name) {
    return !name.empty()
        && name.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyz"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "0123456789_-") == std::string::npos;
}

// Проверяет все настройки конфигурации.
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

        if (!file)
            throw std::runtime_error("Cannot open test_file");
    }
}

// Загружает и проверяет XML-конфигурацию.
Config load_config(const std::string& path) {
    Xml::IXMLDOMDocument2Ptr doc;

    if (FAILED(doc.CreateInstance(__uuidof(Xml::DOMDocument60))))
        throw std::runtime_error("Cannot create XML parser");

    doc->async = VARIANT_FALSE;
    doc->validateOnParse = VARIANT_FALSE;
    doc->resolveExternals = VARIANT_FALSE;
    doc->setProperty(L"ProhibitDTD", _variant_t(true));

    if (doc->load(_variant_t(path.c_str())) != VARIANT_TRUE)
        throw std::runtime_error(
            "Cannot load " + path + ": "
            + utf8(doc->parseError->reason));

    Config config;

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

// Выводит все настройки программы.
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