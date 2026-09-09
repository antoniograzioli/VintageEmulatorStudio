// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cstddef>

namespace ves::standalone_resources
{
struct Payload
{
    const unsigned char* zipData = nullptr;
    std::size_t zipSize = 0;
    const char* sha256 = nullptr;
};

void registerPayload (Payload payload);
Payload getPayload();
}
