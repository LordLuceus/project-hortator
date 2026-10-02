#ifndef OPENMW_MWACCESSIBILITY_CAPTUREFILES_H
#define OPENMW_MWACCESSIBILITY_CAPTUREFILES_H

#include <string>
#include <string_view>

namespace MWAccessibility
{
    /// Encode an absolute generic UTF-8 path without invoking a shell.
    inline std::string captureFolderUri(std::string_view path)
    {
        std::string result = path.starts_with("//") ? "file:" : (path.starts_with('/') ? "file://" : "file:///");
        constexpr char hex[] = "0123456789ABCDEF";
        for (unsigned char c : path)
        {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' || c == ':'
                || c == '-' || c == '_' || c == '.' || c == '~')
                result += static_cast<char>(c);
            else
            {
                result += '%';
                result += hex[c >> 4];
                result += hex[c & 15];
            }
        }
        return result;
    }
}

#endif
