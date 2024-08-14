// Copyright (c) 2015-2024 LG Electronics, Inc.
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

#include "UiStatus.h"
#include "Logging.h"

using namespace std::placeholders;

UiStatus::UiComp::UiComp()
    : m_enabled(ENABLE_SYSTEM | ENABLE_EXTERNAL)
{
}

void UiStatus::UiComp::enable(int mask, const char *reason)
{
    std::string log = std::string("Enabling notifications(");
    log += name();
    log += std::string(") : [");
    if (mask & ENABLE_SYSTEM)
        log += std::string(" system ");
    if (mask & ENABLE_EXTERNAL)
        log += std::string(" external ");
    if (mask & ENABLE_UI)
        log += std::string(" ui ");
    log += std::string("]");
    if (reason)
    {
        log += std::string(" by ");
        log += std::string(reason);
    }
    LOG_DEBUG("%s", log.c_str());

    bool oldEnabled = isEnabled();

    m_enabled |= mask;

    if (reason)
        m_reason = std::string(reason);

    if (!oldEnabled && isEnabled())
        sigStatus(true);
}

void UiStatus::UiComp::disable(int mask, const char *reason)
{
    std::string log = std::string("Disabling notifications(");
    log += name();
    log += std::string(") : [");
    if (mask & ENABLE_SYSTEM)
        log += std::string(" system ");
    if (mask & ENABLE_EXTERNAL)
        log += std::string(" external ");
    if (mask & ENABLE_UI)
        log += std::string(" ui ");
    log += std::string("]");
    if (reason)
    {
        log += std::string(" by ");
        log += std::string(reason);
    }
    LOG_DEBUG("%s", log.c_str());

    bool oldEnabled = isEnabled();

    m_enabled &= ~mask;

    if (reason)
        m_reason = std::string(reason);

    if (oldEnabled && !isEnabled())
        sigStatus(false);
}

bool UiStatus::UiComp::isEnabled(int mask) const
{
    return ( (m_enabled & mask) == mask ) ? true : false;
}

std::string UiStatus::UiComp::reason() const
{
    std::string reason = std::string("[");
    if ( (m_enabled & ENABLE_SYSTEM) == 0 ) reason += std::string(" system ");
    if ( (m_enabled & ENABLE_UI) == 0 ) reason += std::string(" ui ");
    if ( (m_enabled & ENABLE_EXTERNAL) == 0)
    {
         reason += std::string(" ");
         reason += m_reason;
         reason += std::string(" ");
    }
    reason += std::string("]");

    return reason;
}

UiStatus::UiToast::UiToast()
    : m_silence(false)
{
}

void UiStatus::UiToast::setSilence(bool silence)
{
    m_silence = silence;
}

bool UiStatus::UiToast::isSilence() const
{
    return m_silence;
}

UiStatus::UiStatus()
{
    m_comps["toast"] = std::make_shared<UiToast>();
    m_comps["alert"] = std::make_shared<UiAlert>();
    m_comps["input"] = std::make_shared<UiInput>();
    m_comps["prompt"] = std::make_shared<UiPrompt>();
}

void UiStatus::enable(int mask, const char *reason)
{
    std::for_each(m_comps.begin(), m_comps.end(),
        std::bind(&UiComp::enable,
            std::bind(&std::map< std::string, std::shared_ptr<UiComp> >::value_type::second, _1),
            mask, reason)
    );
}

void UiStatus::disable(int mask, const char *reason)
{
    std::for_each(m_comps.begin(), m_comps.end(),
        std::bind(&UiComp::disable,
            std::bind(&std::map< std::string, std::shared_ptr<UiComp> >::value_type::second, _1),
            mask, reason)
    );
}

bool UiStatus::isEnabled(int mask) const
{
    for(const auto& entry : m_comps)
    {
        if (!entry.second->isEnabled(mask))
            return false;
    }
    return true;
}
