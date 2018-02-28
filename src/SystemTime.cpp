// Copyright (c) 2013-2018 LG Electronics, Inc.
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

#include "SystemTime.h"
#include "JUtil.h"
#include "LSUtils.h"
#include "NotificationService.h"
#include "Logging.h"

SystemTime::SystemTime()
    : m_isSynced(false)
    , m_utc_time(0)
{
}

void SystemTime::startSync()
{
    pbnjson::JValue json = pbnjson::Object();
    json.put("serviceName", "com.palm.systemservice");

    LSErrorSafe lserror;
    if (!LSCall(NotificationService::instance()->getHandle(),
        "palm://com.palm.bus/signal/registerServerStatus",
        JUtil::jsonToString(json).c_str(),
        SystemTime::cbRegisterServerStatus, this, NULL, &lserror))
    {
        LOG_WARNING(MSGID_SYSTEMTIME_REG_FAIL, 1, PMLOGKS("REASON", lserror.message), " ");
    }
}

void SystemTime::setSync(bool sync, std::string time_source, int64_t utc_time)
{
    if (sync == isSynced())
    {
        // no populate signal
        m_utc_time = utc_time;
        m_time_source = time_source;
        return;
    }

    m_isSynced = sync;
    m_utc_time = utc_time;
    m_time_source = time_source;

    sigSync(sync);
}

bool SystemTime::isSynced() const
{
    return m_isSynced;
}

int64_t SystemTime::getUtcTime() const
{
    return m_utc_time;
}

std::string SystemTime::getTimeSource() const
{
    return m_time_source;
}

bool SystemTime::cbRegisterServerStatus(LSHandle *lshandle, LSMessage *message, void *user_data)
{
    pbnjson::JValue json = JUtil::parse(LSMessageGetPayload(message), std::string(""));
    if (json.isNull())
        return false;

    bool connected = json["connected"].asBool();
    if (connected)
    {
        pbnjson::JValue param = pbnjson::Object();
        param.put("subscribe", true);

        LSErrorSafe lserror;
        if (!LSCall(NotificationService::instance()->getHandle(),
            "palm://com.palm.systemservice/time/getSystemTime",
            JUtil::jsonToString(param).c_str(),
            SystemTime::cbGetSystemTime, user_data, NULL, &lserror))
        {
            LOG_WARNING(MSGID_SYSTEMTIME_GETTIME_FAIL, 1, PMLOGKS("REASON", lserror.message), " ");
        }
    }

    return true;
}

bool SystemTime::cbGetSystemTime(LSHandle *lshandle, LSMessage *message, void *user_data)
{
    SystemTime *systemTime = reinterpret_cast<SystemTime*>(user_data);
    if (!systemTime)
        return false;

    pbnjson::JValue json = JUtil::parse(LSMessageGetPayload(message), std::string(""));
    if (json.isNull())
        return false;

    std::string timeSource = json["systemTimeSource"].asString();
    int64_t utc_time = json["utc"].asNumber<int64_t>();

    LOG_INFO(MSGID_SYSTEMTIME_SYNC, 2,
        PMLOGKS("TIMESOURCE", timeSource.c_str()),
        PMLOGKFV("TIME", "%lld", utc_time),
        " ");

    if (timeSource.empty() || timeSource == std::string("factory"))
    {
        systemTime->setSync(false, timeSource);
        return true;
    }

    systemTime->setSync(true, timeSource, utc_time);

    return true;
}
