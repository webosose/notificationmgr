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

#include "PowerStatus.h"
#include "JUtil.h"
#include "LSUtils.h"
#include "NotificationService.h"
#include "Logging.h"

void PowerStatus::startMonitor()
{
    pbnjson::JValue json = pbnjson::Object();
    json.put("serviceName", "com.webos.service.tvpower");

    LSErrorSafe lserror;
    if (!LSCall(NotificationService::instance()->getHandle(),
        "luna://com.webos.service.bus/signal/registerServerStatus",
        JUtil::jsonToString(json).c_str(),
        PowerStatus::cbRegisterServerStatus, this, NULL, &lserror))
    {
        LOG_WARNING(MSGID_POWERSTATUS_REG_FAIL, 1, PMLOGKS("REASON", lserror.message), " ");
    }
}

bool PowerStatus::cbRegisterServerStatus(LSHandle *lshandle, LSMessage *msg, void *user_data)
{
    pbnjson::JValue json = JUtil::parse(LSMessageGetPayload(msg), std::string(""));
    if (json.isNull())
        return false;

    bool connected = json["connected"].asBool();
    if (connected)
    {
        pbnjson::JValue param = pbnjson::Object();
        param.put("subscribe", true);

        LSErrorSafe lserror;
        if (!LSCall(NotificationService::instance()->getHandle(),
            "luna://com.webos.service.tvpower/power/getPowerState",
            JUtil::jsonToString(param).c_str(),
            PowerStatus::cbGetPowerState, user_data, NULL, &lserror))
        {
            LOG_WARNING(MSGID_POWERSTATUS_GETSTATE_FAIL, 1, PMLOGKS("REASON", lserror.message), " ");
        }
    }

    return true;
}

bool PowerStatus::cbGetPowerState(LSHandle *lshandle, LSMessage *msg, void *user_data)
{
    PowerStatus *power = static_cast<PowerStatus*>(user_data);
    if (!power)
        return false;

    pbnjson::JValue json = JUtil::parse(LSMessageGetPayload(msg), std::string(""));
    if (json.isNull())
        return false;

    std::string state = json["state"].asString();
    if ( (state != "Active") &&
         (state != "Suspend") &&
         (state != "Active Standby") )
        return true;

    if (power->m_state != state)
    {
        std::string oldstate = power->m_state;
        power->m_state = state;

        LOG_DEBUG("[PowerStatus] state changed : %s -> %s", oldstate.c_str(), state.c_str());

        if (power->m_state == "Active")
        {
            if (oldstate == "Suspend" || oldstate == "Active Standby")
                power->sigBoot("warm");
            else
                power->sigBoot("cold");
        }
    }

    return true;
}
