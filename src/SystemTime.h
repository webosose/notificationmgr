// Copyright (c) 2015-2018 LG Electronics, Inc.
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

#ifndef __SYSTEMTIME_H__
#define __SYSTEMTIME_H__

#include <luna-service2/lunaservice.h>
#include <boost/signals2.hpp>

#include "Singleton.hpp"

class SystemTime : public Singleton<SystemTime>
{
public:
    SystemTime();

    void startSync();

    bool isSynced() const;
    int64_t getUtcTime() const;
    std::string getTimeSource() const;

    boost::signals2::signal<void (bool)> sigSync;

protected:
    void setSync(bool sync, std::string time_source, int64_t utc_time = 0);

    static bool cbRegisterServerStatus(LSHandle *lshandle, LSMessage *message, void *user_data);
    static bool cbGetSystemTime(LSHandle *lshandle, LSMessage *message, void *user_data);

private:
    bool m_isSynced;
    int64_t m_utc_time;
    std::string m_time_source;
};

#endif
