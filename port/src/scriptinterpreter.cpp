#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "scriptinterpreter.hpp"

namespace {

bool IsSpace(char byte) {
    return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r';
}

bool SkipArgumentSpace(input_str &input) {
    while (input.position < input.size && IsSpace(input.buffer[input.position])) {
        ++input.position;
    }
    return input.position < input.size;
}

} // namespace

PORT_OVERRIDE int CScriptInterpreter::GetArg() {
    int count = 0;
    bool more = true;
    while (more && SkipArgumentSpace(*this)) {
        std::string text;
        bool inside_quotes = false;
        int byte;
        for (;;) {
            if (!get(&byte)) {
                return count;
            }
            if (byte == '"') {
                inside_quotes = !inside_quotes;
            }
            if (inside_quotes) {
                if ((byte & 0x80) && (byte < 0xA1 || byte > 0xDF)) {
                    text.push_back(static_cast<char>(byte));
                    if (!get(&byte)) {
                        return count;
                    }
                } else if (byte == '\\') {
                    int next;
                    if (!get(&next) || next != '"') {
                        back();
                    } else {
                        byte = next;
                    }
                }
            }
            if (!inside_quotes && (byte == ',' || byte == ';')) {
                more = byte == ',';
                break;
            }
            text.push_back(static_cast<char>(byte));
        }
        if (text.empty() && !more) {
            break;
        }

        // A missing argument before a comma is invalid. It has no final
        // character to inspect, unlike a quoted empty string.
        int quotes = !text.empty() && text.front() == '"' ? 1 : 0;
        if (!text.empty() && text.back() == '"') {
            text.pop_back();
            ++quotes;
        }
        SPI_STACK argument{};
        argument.value.string = nullptr;
        argument.type = SPI_STACK_TYPE_INT;
        bool non_numeric = false;
        if (quotes == 0) {
            for (std::size_t i = 0; i < text.size(); ++i) {
                const char c = text[i];
                if (c == '.') {
                    argument.type = SPI_STACK_TYPE_FLOAT;
                }
                if (IsSpace(c)) {
                    text.resize(i);
                    break;
                }
                if (c != '-' && c != '.' && (c < '0' || c > '9')) {
                    non_numeric = true;
                }
            }
        }

        if (quotes == 2) {
            argument.type = SPI_STACK_TYPE_STRING;
            const char *value = text.c_str() + 1;
            const std::size_t bytes = std::strlen(value) + 1;
            if (string_buff_next + bytes > string_buff + string_buff_size) {
                std::printf("SPI string buffer over!!\n");
            } else {
                argument.value.string = string_buff_next;
                std::memcpy(string_buff_next, value, bytes);
                string_buff_next += bytes;
            }
        } else if (text.empty() || non_numeric) {
            argument.type = SPI_STACK_TYPE_INVALID;
        } else if (argument.type == SPI_STACK_TYPE_FLOAT) {
            argument.value.real = std::atof(text.c_str());
        } else {
            argument.value.integer = std::atoi(text.c_str());
        }
        PushStack(argument);
        ++count;
    }
    return count;
}
