// Copyright (c) 2013-2024 LG Electronics, Inc.
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

#include "Settings.h"
#include "NotificationService.h"

#include "LSUtils.h"
#include "UiStatus.h"
#include "JUtil.h"

#include <string>
#include <Utils.h>
#include <JUtil.h>
#include <Logging.h>
#include <pbnjson.hpp>

#define DIFFTIME(x,y) ((x) > (y) ? ((x)-(y)) : ((y)-(x)))
#define MSGHOLDTIMEOUT 30

static Settings* s_settings_instance = 0;

Settings::Settings():m_disableToastTimestamp(0),m_thresholdTimer(120),m_retentionPeriod(0)
{
	s_settings_instance = this;
	loadSettings();
}

Settings::~Settings()
{
	s_settings_instance = 0;
}

Settings* Settings::instance()
{
	if(!s_settings_instance) {
		return new Settings();
	}

	return s_settings_instance;
}

void Settings::loadSettings()
{
	char* settingsData;
	pbnjson::JValue aggregators;
	pbnjson::JValue sData;
	JUtil::Error error;

        settingsData = Utils::readFile(s_settingsFile);
	if(settingsData)
	{
	    sData = JUtil::parse(settingsData, "", &error);
            delete[] settingsData;
            if (sData.isNull()) {
        	LOG_WARNING(MSGID_SETTINGS_DATA_EMPTY, 0, "Settings data is empty in %s", __PRETTY_FUNCTION__ );
            return;
        }
	}
        else
        {
    	   LOG_WARNING(MSGID_SETTINGS_FILE_LOAD_FAILED, 0, "Unable to load settings in %s", __PRETTY_FUNCTION__ );
	   return;
        }

	int threshold = sData["DisableThreasholdTimer"].asNumber<int32_t>();
	if(threshold > 0)
	{
		m_thresholdTimer = threshold;
	}

	int retentionPeriod = sData["RetentionPeriod"].asNumber<int32_t>();
	if(retentionPeriod > 0)
	{
		m_retentionPeriod = retentionPeriod;
	}

	aggregators = sData["NotificationAggregator"];
	if(aggregators.isArray())
	{
		const int aSize = aggregators.arraySize();
		for(ssize_t index = 0; index < aSize; ++index) {
			m_notificationAggregator.push_back(aggregators[index].asString());
		}
	}

	bool result;
	LSError lsError;
	LSErrorInit(&lsError);
	result = LSCall(NotificationService::instance()->getHandle(),
					"palm://com.palm.bus/signal/registerServerStatus",
					"{\"serviceName\":\"com.webos.settingsservice\", \"subscribe\":true}",
					Settings::cbSystemSettingsStatusNotification, this, NULL, &lsError);
	if (!result) {
		LOG_DEBUG("unable to register for settingsservice status");
		LSErrorPrint (&lsError, stderr);
		LSErrorFree (&lsError);
	}
}

bool Settings::cbSystemSettingsStatusNotification(LSHandle* lshandle, LSMessage *message, void *user_data)
{
	LSErrorSafe lserror;

	pbnjson::JValue request;
	JUtil::Error error;

	request = JUtil::parse(LSMessageGetPayload(message), "", &error);

	if(request.isNull())
	{
		LOG_WARNING(MSGID_ET_PARSE_FAIL, 0, "Parsing Error in %s", __PRETTY_FUNCTION__ );
		return false;
	}

	if(request["connected"].asBool())
	{
		// get system PIN
		std::string getSystemPinRequest;
		getSystemPinRequest  = "{";
		getSystemPinRequest += "\"keys\":[\"systemPin\"], \"subscribe\":true";
		getSystemPinRequest += "}";

		if (LSCall(NotificationService::instance()->getHandle(),
								"palm://com.webos.settingsservice/getSystemSettings",
								getSystemPinRequest.c_str(),
								Settings::cb_getSystemSettingForPIN, NULL, NULL, &lserror) == false)
		{
			LOG_DEBUG("Get Substrate com.webos.settingsservice/getSystemSettings for getSystemPinRequest call failed");
		}

		// get country and storeMode
		pbnjson::JValue jsonOption = pbnjson::Object();
		pbnjson::JValue jsonOptionKeys = pbnjson::Array();
		jsonOptionKeys.append("country");
		jsonOptionKeys.append("storeMode");
		jsonOptionKeys.append("enableToastPopup");

		jsonOption.put("category", "option");
		jsonOption.put("keys", jsonOptionKeys);
		jsonOption.put("subscribe", true);

		if (LSCall(NotificationService::instance()->getHandle(),
			"palm://com.webos.settingsservice/getSystemSettings",
			JUtil::jsonToString(std::move(jsonOption)).c_str(),
			Settings::cb_getSystemSettingForOption, NULL, NULL, &lserror) == false)
		{
			LOG_WARNING(MSGID_SETTINGS_GETSYSTEMSETTINGS_OPTION_FAILED, 1,
				PMLOGKS("ERROR", lserror.message), " ");
		}
	}

	return true;
}

bool Settings::cb_getSystemSettingForPIN(LSHandle* lshandle, LSMessage *msg, void *user_data)
{
	pbnjson::JValue list = JUtil::parse(LSMessageGetPayload(msg), std::string(""));

	if (list.isNull())
	{
		LOG_DEBUG("cb_getSystemSettingForPIN Message is wrong!!!!\n");
		return false;
	}

	pbnjson::JValue settings = list["settings"];
	std::string pincode;

	if( settings.isNull() )
	{
		LOG_DEBUG("cb_getSystemSettingForPIN Message is not parsed\n");
		return false;
	}

	pincode = settings["systemPin"].asString();
	LOG_DEBUG("cb_getSystemSettingForPIN pin - %s, %s in %s", pincode.c_str(), __func__, __FILE__ );

	Settings::instance()->m_system_pincode =  std::move(pincode);

	return true;
}

bool Settings::cb_getSystemSettingForOption(LSHandle* lshandle, LSMessage *msg, void *user_data)
{
	pbnjson::JValue list = JUtil::parse(LSMessageGetPayload(msg), std::string(""));

	if (list.isNull())
	{
		LOG_DEBUG("cb_getSystemSettingForOption Message is wrong!!!!\n");
		return false;
	}

	pbnjson::JValue settings = list["settings"];

	if( settings.isNull() )
	{
		LOG_DEBUG("cb_getSystemSettingForOption Message is not parsed\n");
		return false;
	}

	if (settings.hasKey("country"))
	{
		std::string country = settings["country"].asString();

		LOG_INFO(MSGID_SETTINGS_OPTION_COUNTRY, 1,
			PMLOGKS("country", country.c_str()), " ");

		Settings::instance()->m_system_country = std::move(country);
	}

	if (settings.hasKey("storeMode"))
	{
		std::string storeMode = settings["storeMode"].asString();
		Settings::instance()->m_store_mode = storeMode;

		LOG_INFO(MSGID_SETTINGS_OPTION_STOREMODE, 1,
			PMLOGKS("storeMode", storeMode.c_str()), " ");
	}

	if (settings.hasKey("enableToastPopup"))
	{
		std::string enableToast = settings["enableToastPopup"].asString();
		Settings::instance()->m_enable_toast = enableToast;

		LOG_INFO(MSGID_SETTINGS_OPTION_ENABLETOAST, 1,
			PMLOGKS("enableToastPopup", enableToast.c_str()), " ");
	}

        if (UiStatus::instance().toast()) {
            if (Settings::instance()->m_store_mode == "store" && Settings::instance()->m_enable_toast == "off")
                (UiStatus::instance().toast())->setSilence(true);
            else
                (UiStatus::instance().toast())->setSilence(false);
        }

	return true;
}

bool Settings::cbGetSystemProperties(LSHandle* lshandle, LSMessage *message, void *user_data)
{
	LSErrorSafe lserror;

	pbnjson::JValue request;
	JUtil::Error error;

	// default value is based on normal mode.
	static bool sSubstrateMode = false;
	static bool sPowerOnlyMode = false;
	static bool sInstopCompleted = true;

	request = JUtil::parse(LSMessageGetPayload(message), "", &error);
	if(request.isNull())
	{
		LOG_WARNING(MSGID_ET_PARSE_FAIL, 0, "Parsing Error in %s", __PRETTY_FUNCTION__ );
		return false;
	}

	// it assumes these properties will be set at the first callback.
	// CAUTION: the return value is 'true' or 'false', but a type of value is string, not boolean.
	std::string substrateMode = request["substrateMode"].asString();
	if(substrateMode == "true")
		sSubstrateMode = true;
	else if(substrateMode == "false")
		sSubstrateMode = false;

	std::string powerOnlyMode = request["powerOnlyMode"].asString();
	if(powerOnlyMode == "true")
		sPowerOnlyMode = true;
	else if(powerOnlyMode == "false")
		sPowerOnlyMode = false;

	std::string instopCompleted = request["instopCompleted"].asString();
	if(instopCompleted == "true")
		sInstopCompleted = true;
	else if(instopCompleted == "false")
		sInstopCompleted = false;

	Settings::instance()->setBlockNotication(sSubstrateMode, sPowerOnlyMode, sInstopCompleted);

	return true;
}

void Settings::setBlockNotication(bool substrateMode, bool powerOnlyMode, bool instopCompleted)
{
	LOG_DEBUG("Current system properties: substraceMode(%s), powerOnlyMode(%s), instopCompleted(%s)",
			  substrateMode? "true" : "false", powerOnlyMode? "true" : "false", instopCompleted? "true" : "false");

        if (UiStatus::instance().toast()) {
            if (powerOnlyMode || !substrateMode) {
                (UiStatus::instance().toast())->enable(UiStatus::ENABLE_SYSTEM);
            } else {
                (UiStatus::instance().toast())->disable(UiStatus::ENABLE_SYSTEM);
            }
        }

        if (UiStatus::instance().alert()) {
            if (!powerOnlyMode && !substrateMode && instopCompleted) {
                (UiStatus::instance().alert())->enable(UiStatus::ENABLE_SYSTEM);
            } else {
                (UiStatus::instance().alert())->disable(UiStatus::ENABLE_SYSTEM);
            }
        }
}

bool Settings::disableToastNotification()
{
	m_disableToastTimestamp = time(NULL);
	return true;
}

bool Settings::enableToastNotification()
{
	m_disableToastTimestamp = 0;
	return true;
}

bool Settings::disableToastNotificationForApp(const std::string& appId)
{
	return true;
}

bool Settings::enableToastNotificationForApp(const std::string& appId)
{
	return true;
}

bool Settings::isPartOfAggregators(std::string sId)
{
	bool isExist = false;

	for(std::vector<std::string>::iterator it = m_notificationAggregator.begin(); it != m_notificationAggregator.end(); ++it)
	{
		std::string item = (*it);
		if(sId.find(item,0) == std::string::npos) {
			isExist = false;
		}
		else {
			return true;
		}
	}

	return isExist;
}

int Settings::getRetentionPeriod()
{
	return m_retentionPeriod;
}

std::string Settings::getDefaultIcon(const std::string type)
{
	if(type.empty())
		return s_defaultToastIcon;

	if(type == "toast")
	{
		return s_defaultToastIcon;
	}
	else
	{
		return s_defaultAlertIcon;
	}
}

bool Settings::isPrivilegedSource(const std::string &callerId)
{
	if(callerId.find("com.palm.",0) == std::string::npos && callerId.find("com.webos.", 0) == std::string::npos && callerId.find("com.lge.",0) == std::string::npos)
	{
		return false;
	}
	return true;
}
