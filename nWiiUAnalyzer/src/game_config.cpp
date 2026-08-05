#include "nwiiu/analyzer/game_config.h"

#include <charconv>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace nwiiu::analyzer {
namespace {
// A deliberately small TOML subset: comments, `[table]` headers, and
// `key = value` where value is a basic string, a decimal or 0x integer, or a
// boolean. No arrays, no inline tables, no multi-line strings. The profiles
// this reads are flat by construction, and a hand-rolled reader keeps the
// analyzer's dependency list at OpenSSL and zlib.
[[noreturn]] void config_error(size_t line, std::string_view message) {
    std::ostringstream text;
    text << "config error on line " << line << ": " << message;
    throw std::runtime_error(text.str());
}

std::string_view trim(std::string_view value) {
    const auto is_space = [](char item) {
        return item == ' ' || item == '\t' || item == '\r';
    };
    while (!value.empty() && is_space(value.front())) {
        value.remove_prefix(1);
    }
    while (!value.empty() && is_space(value.back())) {
        value.remove_suffix(1);
    }
    return value;
}

// Drops a trailing `#` comment. Scans for the quote state first so that a `#`
// inside a value — a colour, a fragment identifier — survives.
std::string_view strip_comment(std::string_view line, size_t number) {
    bool quoted = false;
    for (size_t index = 0; index < line.size(); ++index) {
        const char item = line[index];
        if (item == '"') {
            quoted = !quoted;
        } else if (item == '#' && !quoted) {
            return line.substr(0, index);
        }
    }
    if (quoted) {
        config_error(number, "unterminated string");
    }
    return line;
}

// Basic strings only: TOML's escape set minus the \uXXXX forms, which no
// profile field needs.
std::string decode_string(std::string_view token, size_t number) {
    if (token.size() < 2 || token.front() != '"' || token.back() != '"') {
        config_error(number, "expected a quoted string");
    }
    token.remove_prefix(1);
    token.remove_suffix(1);
    std::string value;
    value.reserve(token.size());
    for (size_t index = 0; index < token.size(); ++index) {
        if (token[index] != '\\') {
            value.push_back(token[index]);
            continue;
        }
        if (++index == token.size()) {
            config_error(number, "trailing escape in string");
        }
        switch (token[index]) {
        case '"':
            value.push_back('"');
            break;
        case '\\':
            value.push_back('\\');
            break;
        case 'n':
            value.push_back('\n');
            break;
        case 't':
            value.push_back('\t');
            break;
        default:
            config_error(number, "unsupported escape in string");
        }
    }
    return value;
}

uint32_t decode_u32(std::string_view token, size_t number) {
    int base = 10;
    if (token.size() > 2 && token[0] == '0' &&
        (token[1] == 'x' || token[1] == 'X')) {
        base = 16;
        token.remove_prefix(2);
    }
    uint64_t value = 0;
    const auto* first = token.data();
    const auto* last = token.data() + token.size();
    const auto result = std::from_chars(first, last, value, base);
    if (result.ec != std::errc{} || result.ptr != last || token.empty()) {
        config_error(number, "expected an integer");
    }
    if (value > 0xFFFFFFFFull) {
        config_error(number, "integer does not fit in 32 bits");
    }
    return static_cast<uint32_t>(value);
}

// Guest addresses are written as bare quoted hex ("0275F480") to match the
// NWiiRecomp `[hle_hooks]` tables, so the key decoder accepts hex without the
// 0x prefix that decode_u32 requires.
uint32_t decode_address(std::string_view token, size_t number) {
    if (token.size() > 2 && token[0] == '0' &&
        (token[1] == 'x' || token[1] == 'X')) {
        return decode_u32(token, number);
    }
    uint64_t value = 0;
    const auto* first = token.data();
    const auto* last = token.data() + token.size();
    const auto result = std::from_chars(first, last, value, 16);
    if (result.ec != std::errc{} || result.ptr != last || token.empty()) {
        config_error(number, "expected a guest address");
    }
    if (value > 0xFFFFFFFFull) {
        config_error(number, "guest address does not fit in 32 bits");
    }
    return static_cast<uint32_t>(value);
}

bool decode_bool(std::string_view token, size_t number) {
    if (token == "true") {
        return true;
    }
    if (token == "false") {
        return false;
    }
    config_error(number, "expected true or false");
}

std::string decode_key(std::string_view token, size_t number) {
    if (!token.empty() && token.front() == '"') {
        return decode_string(token, number);
    }
    if (token.empty()) {
        config_error(number, "empty key");
    }
    for (const char item : token) {
        const bool allowed = (item >= 'a' && item <= 'z') ||
                             (item >= 'A' && item <= 'Z') ||
                             (item >= '0' && item <= '9') || item == '_' ||
                             item == '-';
        if (!allowed) {
            config_error(number, "unquoted key has an unsupported character");
        }
    }
    return std::string(token);
}

void assign_root(GameConfig& config, const std::string& key,
                 std::string_view value, size_t number) {
    if (key == "project_name") {
        config.project_name = decode_string(value, number);
    } else if (key == "platform") {
        config.platform = decode_string(value, number);
    } else if (key == "input" || key == "input_rpx") {
        config.input = decode_string(value, number);
    } else if (key == "output_dir") {
        config.output_dir = decode_string(value, number);
    } else if (key == "title_root") {
        config.title_root = decode_string(value, number);
    } else if (key == "save_root") {
        config.save_root = decode_string(value, number);
    } else if (key == "symbols_csv") {
        config.symbols_csv = decode_string(value, number);
    } else if (key == "blocks_per_shard") {
        config.blocks_per_shard = decode_u32(value, number);
    } else if (key == "target_prefix") {
        config.target_prefix_override = decode_string(value, number);
    } else {
        config_error(number, "unknown key '" + key + "'");
    }
}

void assign_target(Target& target, const std::string& key,
                   std::string_view value, size_t number) {
    if (key == "product_code") {
        target.product_code = decode_string(value, number);
    } else if (key == "title_id") {
        target.title_id = decode_string(value, number);
    } else if (key == "title_version") {
        target.title_version = decode_u32(value, number);
    } else if (key == "sha256") {
        target.sha256 = decode_string(value, number);
    } else if (key == "entry_point") {
        target.entry_point = decode_u32(value, number);
    } else if (key == "name") {
        target.name = decode_string(value, number);
    } else if (key == "verify_hash") {
        // A profile that carries a digest but wants it treated as advisory
        // clears it here rather than deleting the ground truth from the file.
        if (!decode_bool(value, number)) {
            target.sha256.clear();
        }
    } else {
        config_error(number, "unknown [target] key '" + key + "'");
    }
}
} // namespace

std::string GameConfig::target_prefix() const {
    const std::string& source =
        target_prefix_override.empty() ? project_name : target_prefix_override;
    std::string prefix;
    prefix.reserve(source.size());
    for (const char item : source) {
        if (item >= 'A' && item <= 'Z') {
            prefix.push_back(static_cast<char>(item - 'A' + 'a'));
        } else if ((item >= 'a' && item <= 'z') ||
                   (item >= '0' && item <= '9')) {
            prefix.push_back(item);
        } else if (!prefix.empty() && prefix.back() != '-') {
            prefix.push_back('-');
        }
    }
    while (!prefix.empty() && prefix.back() == '-') {
        prefix.pop_back();
    }
    if (prefix.empty() || (prefix.front() >= '0' && prefix.front() <= '9')) {
        prefix.insert(prefix.begin(), 'g');
    }
    return prefix;
}

GameConfig parse_game_config(std::string_view text,
                             const std::filesystem::path& origin) {
    GameConfig config;
    config.source_path = origin;
    std::string table;
    size_t number = 0;
    size_t cursor = 0;
    while (cursor <= text.size()) {
        const size_t break_at = text.find('\n', cursor);
        const size_t end = break_at == std::string_view::npos ? text.size()
                                                              : break_at;
        std::string_view line = text.substr(cursor, end - cursor);
        cursor = end + 1;
        ++number;

        line = trim(strip_comment(line, number));
        if (line.empty()) {
            if (break_at == std::string_view::npos) {
                break;
            }
            continue;
        }

        if (line.front() == '[') {
            if (line.back() != ']' || line.size() < 3) {
                config_error(number, "malformed table header");
            }
            table = decode_key(trim(line.substr(1, line.size() - 2)), number);
            if (table != "target" && table != "hle_hooks" &&
                table != "system") {
                config_error(number, "unknown table '" + table + "'");
            }
            if (break_at == std::string_view::npos) {
                break;
            }
            continue;
        }

        const size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            config_error(number, "expected key = value");
        }
        const std::string key =
            decode_key(trim(line.substr(0, equals)), number);
        const std::string_view value = trim(line.substr(equals + 1));
        if (value.empty()) {
            config_error(number, "missing value");
        }

        if (table.empty()) {
            assign_root(config, key, value, number);
        } else if (table == "target") {
            assign_target(config.target, key, value, number);
        } else if (table == "system") {
            if (key != "platform") {
                config_error(number, "unknown [system] key '" + key + "'");
            }
            config.platform = decode_string(value, number);
        } else {
            const uint32_t address = decode_address(key, number);
            if (!config.hle_hooks.emplace(address, decode_string(value, number))
                     .second) {
                config_error(number, "duplicate hook address");
            }
        }

        if (break_at == std::string_view::npos) {
            break;
        }
    }

    if (config.project_name.empty()) {
        throw std::runtime_error("config error: project_name is required");
    }
    if (config.target.name.empty()) {
        config.target.name = config.project_name;
    }
    return config;
}

GameConfig load_game_config(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("cannot open config: " + path.string());
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (input.bad()) {
        throw std::runtime_error("cannot read config: " + path.string());
    }
    return parse_game_config(buffer.str(), path);
}
} // namespace nwiiu::analyzer
