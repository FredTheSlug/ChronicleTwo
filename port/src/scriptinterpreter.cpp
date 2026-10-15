#include "scriptinterpreter.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

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

// Retail keeps each tag's arguments and their strings in locals of GetNextTAG, which Run calls in
// a loop at the same stack depth, so a slot past a tag's last argument still holds the value an
// earlier tag left there. Disc data depends on it: s05.cfg's fc_e230 (Master Utan's poisoned
// apples in the Fish Monster Swamp) lists seven FUNC_POS values, and its last two scales come
// from the fc_e220 tag before it. The port zeroes locals, which made those scales zero and the
// trigger unreachable. One persistent buffer per nesting level keeps the retail leftovers.
constexpr int kPersistentDepths = 8;

struct TagStorage {
    char      strings[SPI_STRING_BUFF_SIZE];
    SPI_STACK arguments[SPI_STACK_SIZE];
};

TagStorage persistent_storage[kPersistentDepths];
int        tag_depth = 0;

struct DepthScope {
    DepthScope() { ++tag_depth; }

    ~DepthScope() { --tag_depth; }
};

} // namespace

PORT_OVERRIDE int CScriptInterpreter::GetNextTAG(int call) {
    if (tag == nullptr) {
        return -1;
    }

    DepthScope                  scope;
    std::unique_ptr<TagStorage> deeper;
    TagStorage                 *storage = &persistent_storage[0];
    if (tag_depth <= kPersistentDepths) {
        storage = &persistent_storage[tag_depth - 1];
    } else {
        deeper = std::make_unique<TagStorage>();
        storage = deeper.get();
    }
    SetStringBuff(storage->strings, SPI_STRING_BUFF_SIZE);

    for (;;) {
        int index;
        SetStack(storage->arguments, SPI_STACK_SIZE);

        if (!SearchCommand(&index)) {
            return -1;
        }

        if (!binary) {
            if (index >= tag_count || index < 0) {
                // An unknown tag is passed over up to its semicolon.
                int c;
                while (get(&c) && c != ';') {
                }
                continue;
            }

            const int argument_count = GetArg();

            if (call && tag[index].function != nullptr) {
                tag[index].function(stack, argument_count);
            }
        } else {
            const int argument_count = GetArgBin();

            if (index < tag_count && index >= 0 && call && tag[index].function != nullptr) {
                tag[index].function(stack, argument_count);
            }
        }

        return index;
    }
}

PORT_OVERRIDE int CScriptInterpreter::GetArg() {
    int  count = 0;
    bool more = true;
    while (more && SkipArgumentSpace(*this)) {
        std::string text;
        bool        inside_quotes = false;
        int         byte;
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
            const char       *value = text.c_str() + 1;
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
