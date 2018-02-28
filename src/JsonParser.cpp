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

#include "JsonParser.h"
#include "Utils.h"
#include "Logging.h"

pbnjson::JValue JsonParser::createInputAlertInfo(pbnjson::JValue src, std::string &errText)
{
    pbnjson::JValue alertInfo = pbnjson::Object();

    std::string appId = src["appId"].asString();
    if (appId.length() == 0)
    {
        LOG_WARNING(MSGID_CIA_APPID_EMPTY, 0, " ");
        errText = "Input application id can't be empty";
        return pbnjson::JValue();
    }
    alertInfo.put("appId", appId);

    std::string portType = src["portType"].asString();
    if (portType.length() == 0)
    {
        LOG_WARNING(MSGID_CIA_PTYPE_EMPTY, 0, " ");
        errText = "Port type can't be empty";
        return pbnjson::JValue();
    }
    alertInfo.put("portType", portType);

    std::string portName = src["portName"].asString();
    if (portName.length() == 0)
    {
        LOG_WARNING(MSGID_CIA_PNAME_EMPTY, 0, "Empty message is given in %s", __PRETTY_FUNCTION__);
        errText = "Port name can't be empty";
        return pbnjson::JValue();
    }
    alertInfo.put("portName", portName);

    //Check the port icon
    std::string portIcon = src["portIcon"].asString();
    if (portIcon.length() != 0)
    {
        if (Utils::verifyFileExist(portIcon.c_str()))
        {
            alertInfo.put("portIcon", "file://"+portIcon);
        }
        else
        {
            LOG_WARNING(MSGID_CIA_PICON_VALID, 1,
                PMLOGKS("PATH", portIcon.c_str()),
                " ");
            errText = "Port icon file name should be valid";
            return pbnjson::JValue();
        }
    }
    else
    {
        LOG_WARNING(MSGID_CIA_PICON_EMPTY, 0, " ");
        errText = "Port icon can't be empty";
        return pbnjson::JValue();
    }

    std::string deviceName = src["deviceName"].asString();
    if(deviceName.length() != 0)
        alertInfo.put("deviceName", deviceName);

    //Check the device icon
    std::string deviceIcon = src["deviceIcon"].asString();
    if (deviceIcon.length() != 0)
    {
        if (Utils::verifyFileExist(deviceIcon.c_str()))
        {
            alertInfo.put("deviceIcon", "file://"+deviceIcon);
        }
        else
        {
            LOG_WARNING(MSGID_CIA_DICON_VALID, 1,
                PMLOGKS("PATH", deviceIcon.c_str()),
                " ");
            errText = "Device icon file name should be valid or empty";
            return pbnjson::JValue();
        }
    }

    //Check for params property
    if(!src["params"].isNull())
        alertInfo.put("params", src["params"]);

    return alertInfo;
}

pbnjson::JValue JsonParser::createActionInfo(pbnjson::JValue src)
{
    pbnjson::JValue action = pbnjson::Object();

    if (src.isNull())
        return action;

    std::string uri = src["uri"].asString();
    if (Utils::isValidURI(uri))
    {
        unsigned found = uri.find_last_of("/");
        action.put("serviceURI", uri.substr(0, found + 1));
        action.put("serviceMethod", uri.substr(found + 1));
        if (!src["params"].isNull())
            action.put("launchParams", src["params"]);
        else
            action.put("launchParams", {});
    }

    return action;
}
