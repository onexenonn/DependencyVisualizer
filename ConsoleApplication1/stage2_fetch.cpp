

#include "common.h"

#include <comdef.h>
#include <set>
#include <stdexcept>

#import <msxml6.dll> rename_namespace("Xml")

// Минимальное JSON-значение.
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

    std::string text;
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

    const std::string& as_string() const {
        if (type != String)
            throw std::runtime_error("Expected JSON string");

        return text;
    }

    const std::vector<JsonValue>& as_array() const {
        if (type != Array)
            throw std::runtime_error("Expected JSON array");

        return items;
    }
};

// Небольшой JSON-парсер.
class JsonParser {
    const std::string& source;
    std::size_t position = 0;

    void fail() const {
        throw std::runtime_error(
            "Invalid JSON near byte "
            + std::to_string(position));
    }

    char peek() const {
        return position < source.size()
            ? source[position]
            : '\0';
    }

    void whitespace() {
        while (position < source.size()
            && (source[position] == ' '
                || source[position] == '\t'
                || source[position] == '\r'
                || source[position] == '\n'))
            ++position;
    }

    bool take(char character) {
        whitespace();

        if (position < source.size()
            && source[position] == character) {
            ++position;
            return true;
        }

        return false;
    }

    void expect(char character) {
        if (!take(character))
            fail();
    }

    static bool digit(char character) {
        return character >= '0' && character <= '9';
    }

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

    std::string parse_string() {
        expect('"');

        std::string result;

        while (position < source.size()) {
            const unsigned char character =
                static_cast<unsigned char>(source[position++]);

            if (character == '"')
                return result;

            if (character < 0x20)
                fail();

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

    JsonValue value(unsigned depth) {
        if (depth > 128)
            throw std::runtime_error(
                "JSON nesting limit exceeded");

        whitespace();

        JsonValue result;

        switch (peek()) {
        case '{':
            ++position;
            result.type = JsonValue::Object;

            if (take('}'))
                return result;

            do {
                const auto key = parse_string();
                expect(':');
                auto child = value(depth + 1);

                if (!result.fields.emplace(key, std::move(child)).second)
                    throw std::runtime_error(
                        "Duplicate JSON field: " + key);
            } while (take(','));

            expect('}');
            return result;

        case '[':
            ++position;
            result.type = JsonValue::Array;

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

    JsonValue parse() {
        auto result = value(0);
        whitespace();

        if (position != source.size())
            fail();

        return result;
    }
};

// Выполняет HTTP GET-запрос и разбирает ответ как JSON.
JsonValue request_json(const std::string& url) {
    Sleep(1100);

    Xml::IServerXMLHTTPRequest2Ptr request;

    if (FAILED(request.CreateInstance(
        __uuidof(Xml::ServerXMLHTTP60))))
        throw std::runtime_error("Cannot create HTTP client");

    request->setTimeouts(10000, 10000, 10000, 30000);

    request->open(
        _bstr_t("GET"),
        _bstr_t(url.c_str()),
        _variant_t(false));

    request->setRequestHeader(
        _bstr_t("User-Agent"),
        _bstr_t("DependencyVisualizer-EducationalProject/1.0"));

    request->send();

    if (request->status != 200)
        throw std::runtime_error(
            "HTTP " + std::to_string(request->status)
            + ": " + url);

    const std::string response = utf8(request->responseText);

    return JsonParser(response).parse();
}

// Получает список обычных зависимостей пакета.
std::vector<std::string> fetch_dependencies(
    std::string base,
    const std::string& package) {

    if (!valid_package(package))
        throw std::runtime_error(
            "Invalid crate name: " + package);

    while (!base.empty() && base.back() == '/')
        base.pop_back();

    base += "/api/v1/crates/" + package;

    const auto metadata = request_json(base);

    const std::string version =
        metadata.at("crate").at("max_version").as_string();

    if (version.empty()
        || version.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyz"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "0123456789.-_+") != std::string::npos)
        throw std::runtime_error("Invalid crate version");

    const auto data =
        request_json(base + "/" + version + "/dependencies");

    std::set<std::string> names;

    for (const auto& dependency : data.at("dependencies").as_array()) {
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