// Copyright (c) 2020 LG Electronics, Inc.
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

#include "LSUtils.h"

#include <boost/algorithm/string.hpp>

std::string LSUtils::getMethod(LSMessage *msg, bool includeCategory)
{
    const char *m = LSMessageGetMethod(msg);
    std::string method = m ? m : "";

    if (!includeCategory)
        return method;

    const char *c = LSMessageGetCategory(msg);
    std::string category = c ? c : "";

    if (category != "/")
    {
        method = category + "/" + method;
        method.erase(0, 1);
    }

    return method;
}

std::string LSUtils::getCallerId(LSMessage *msg)
{
    const char *appid = LSMessageGetApplicationID(msg);
    const char *servicename = LSMessageGetSenderServiceName(msg);

    const char *name = appid;
    if (!name)
        name = servicename;
    if (!name)
        return std::string("");

    // for webapp
    if (appid && servicename && boost::algorithm::starts_with(servicename, appid))
        name = servicename;

    return std::string(name);
}
