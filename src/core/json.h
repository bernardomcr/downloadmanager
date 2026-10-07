#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace dm {

// JSON mínimo para as mensagens da extensão do navegador (objetos, listas, textos, números, booleanos).
class JsonValue {
public:
    using Object = std::map<std::string, JsonValue>;
    using Array = std::vector<JsonValue>;

    JsonValue() = default;  // null
    JsonValue(bool value) : data_(value) {}
    JsonValue(double value) : data_(value) {}
    JsonValue(std::string value) : data_(std::move(value)) {}
    JsonValue(const char* value) : data_(std::string(value)) {}
    JsonValue(Object value) : data_(std::make_shared<Object>(std::move(value))) {}
    JsonValue(Array value) : data_(std::make_shared<Array>(std::move(value))) {}

    bool isNull() const { return std::holds_alternative<std::monostate>(data_); }
    bool isObject() const { return std::holds_alternative<std::shared_ptr<Object>>(data_); }

    // Valor de um campo de objeto, ou vazio se não existir / tipo diferente.
    std::string string(const std::string& key) const;
    std::optional<double> number(const std::string& key) const;
    std::optional<bool> boolean(const std::string& key) const;
    const JsonValue* field(const std::string& key) const;
    const Array* array() const;
    const std::string* text() const;

    std::string serialize() const;

private:
    std::variant<std::monostate, bool, double, std::string, std::shared_ptr<Object>, std::shared_ptr<Array>> data_;
};

// Vazio se o texto não for JSON válido.
std::optional<JsonValue> parseJson(const std::string& text);

}  // namespace dm
