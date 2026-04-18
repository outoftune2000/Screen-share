#pragma once

#include <array>
#include <cstdio>
#include <random>
#include <string>

#ifdef _WIN32
#include <objbase.h>
#else
#include <uuid/uuid.h>
#endif

inline std::string generateUUID() {
#ifdef _WIN32
    GUID guid;
    CoCreateGuid(&guid, &guid);
    char buf[37];
    snprintf(buf, sizeof(buf), "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             guid.Data1, guid.Data2, guid.Data3,
             guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
             guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    return std::string(buf);
#else
    uuid_t uuid;
    uuid_generate_random(uuid);
    char buf[37];
    uuid_unparse_lower(uuid, buf);
    return std::string(buf);
#endif
}

inline std::string getHostname() {
    char buf[256] = {};
    gethostname(buf, sizeof(buf));
    return std::string(buf);
}