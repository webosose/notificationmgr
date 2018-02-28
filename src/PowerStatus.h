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

#ifndef POWER_STATUS_H
#define POWER_STATUS_H

#include <luna-service2/lunaservice.h>
#include <boost/signals2.hpp>

#include "Singleton.hpp"

class PowerStatus : public Singleton<PowerStatus>
{
public:
    void startMonitor();

    boost::signals2::signal<void (const std::string&)> sigBoot;

protected:
    static bool cbRegisterServerStatus(LSHandle *lshandle, LSMessage *msg, void *user_data);
    static bool cbGetPowerState(LSHandle *lshandle, LSMessage *msg, void *user_data);

private:
    std::string m_state;
};

#endif
