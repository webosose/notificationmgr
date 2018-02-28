// Copyright (c) 2016-2018 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#include "PincodeValidator.h"
#include "Settings.h"
#include "JUtil.h"
#include "Logging.h"

#include <openssl/sha.h>

static std::string bin2hex(const unsigned char *bin, size_t len)
{
    const char hex[] = "0123456789abcdef";

    std::string res;
    for(size_t i = 0; i < len; ++i)
    {
        unsigned char c = (unsigned char)bin[i];
        res += hex[c >> 4];
        res += hex[c & 0xf];
    }

    return res;
}

PincodeValidator::PincodeValidator(std::string code)
    : m_code(code)
{
}

bool PincodeValidator::check(std::string input) const
{
    if (input.empty())
        return false;

    if (m_code.empty())
    {
        LOG_WARNING(MSGID_PINCODE_EMPTY, 0, " ");

        pbnjson::JValue cache = JUtil::parseFile(std::string(s_lockFile), std::string(""));
        if (cache.isNull())
        {
            LOG_WARNING(MSGID_PINCODE_PARSE_FAIL, 1,
                PMLOGKS("FILE", s_lockFile), " ");
            return false;
        }

        std::string hash = cache["systemPin"].asString();
        if (hash.empty())
        {
            LOG_WARNING(MSGID_PINCODE_SYSTEMPIN_EMPTY, 0, " ");
            return false;
        }

        unsigned char md[SHA256_DIGEST_LENGTH] = {0};
        SHA256((const unsigned char*)input.c_str(), input.length(), md);
        return (bin2hex(md, SHA256_DIGEST_LENGTH) == hash);
    }

    return (m_code == input);
}
