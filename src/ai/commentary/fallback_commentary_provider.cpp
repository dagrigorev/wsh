#include "fallback_commentary_provider.h"
#include "str_util.h"
#include <cstdio>
#include <cstring>
#include <string>

namespace wsh {

FallbackCommentaryProvider::FallbackCommentaryProvider() {}
FallbackCommentaryProvider::~FallbackCommentaryProvider() {}

std::string FallbackCommentaryProvider::Generate(const std::string& command) {
    if (command.empty()) return "";

    /* Detect language: if command contains Cyrillic, use Russian template */
    bool ru = false;
    for (unsigned char c : command) {
        if (c >= 0xC0 && c <= 0xFF) { ru = true; break; }
    }

    char buf[512];
    if (ru) {
        _snprintf(buf, sizeof(buf),
            "\xd0\x97\xd0\xb0\xd0\xb1\xd0\xb0\xd0\xb2\xd0\xbd\xd0\xbe "
            "\xd0\xbf\xd1\x80\xd0\xbe\xd0\xba\xd0\xbe\xd0\xbc\xd0\xbc\xd0\xb5\xd0\xbd\xd1\x82\xd0\xb8\xd1\x80\xd1\x83\xd0\xb9 "
            "\xd1\x8d\xd1\x82\xd0\xbe\xd1\x82 "
            "\xd0\xb2\xd0\xb2\xd0\xbe\xd0\xb4 \xd0\xbf\xd0\xbe\xd0\xbb\xd1\x8c\xd0\xb7\xd0\xbe\xd0\xb2\xd0\xb0\xd1\x82\xd0\xb5\xd0\xbb\xd1\x8f "
            "\xd0\xbd\xd0\xb0 \xd1\x80\xd1\x83\xd1\x81\xd1\x81\xd0\xba\xd0\xbe\xd0\xbc - `%s`",
            command.c_str());
    } else {
        _snprintf(buf, sizeof(buf),
            "Make a fun react on this user input in english - `%s`",
            command.c_str());
    }

    return std::string(buf);
}

} /* namespace wsh */
