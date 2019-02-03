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

#include "AppList.h"
#include "NotificationService.h"

#include "LSUtils.h"

#include <string>
#include <Utils.h>
#include <JUtil.h>
#include <Logging.h>
#include <pbnjson.hpp>

static AppList* s_applist_instance = 0;

AppList::AppList()
{
	s_applist_instance = this;
	init();
}

AppList::~AppList()
{
	s_applist_instance = 0;
}

AppList* AppList::instance()
{
	if(!s_applist_instance) {
		return new AppList();
	}

	return s_applist_instance;
}

void AppList::init()
{
	bool result;
	LSError lsError;
	LSErrorInit(&lsError);

	result = LSCall(NotificationService::instance()->getHandle(), "palm://com.palm.bus/signal/registerServerStatus",
						"{\"serviceName\":\"com.webos.applicationManager\", \"subscribe\":true}",AppList::cbAppMgrBusStatusNotification, this, NULL, &lsError);


	if (!result) {
			LOG_DEBUG("Unable to register for application manager notification in %s\n", __PRETTY_FUNCTION__);
			LSErrorPrint (&lsError, stderr);
			LSErrorFree (&lsError);
	}
}

bool AppList::cbAppMgrBusStatusNotification(LSHandle* lshandle, LSMessage *message, void *user_data)
{
	LSErrorSafe lserror;

	pbnjson::JValue request;
	JUtil::Error error;

	request = JUtil::parse(LSMessageGetPayload(message), "", &error);

	if(request.isNull())
	{
		LOG_ERROR(MSGID_APP_MGR_MSG_EMPTY, 0, "AppMgr Bus notification payload is empty in %s", __PRETTY_FUNCTION__);
		return false;
	}

	if(request["connected"].asBool())
	{
		pbnjson::JValue params = pbnjson::Object();

		pbnjson::JValue props = pbnjson::Array();
		props.append("id");
		props.append("icon");
		props.append("folderPath");

		params.put("properties", props);
		params.put("subscribe", true);

		//the application manager is on the bus...make a call to receive list of installed apps.
		if (LSCall(NotificationService::instance()->getHandle(),"palm://com.webos.applicationManager/listApps",
						JUtil::jsonToString(params).c_str(),
						AppList::cbAppMgrAppList,NULL,NULL, &lserror) == false) {
			 LOG_ERROR(MSGID_LISTAPPS_CALL_FAILED, 0, "ListApps call failed in %s", __PRETTY_FUNCTION__);
		}
	}

	return true;

}

bool AppList::cbAppMgrAppList(LSHandle* lshandle, LSMessage *message, void *user_data)
{
	LSErrorSafe lserror;

	pbnjson::JValue response;
	JUtil::Error error;

	response = JUtil::parse(LSMessageGetPayload(message), "", &error);

	if(response.isNull())
	{
		LOG_ERROR(MSGID_LISTAPPS_MSG_EMPTY, 0, "ListApps payload is empty in %s", __PRETTY_FUNCTION__);
		return false;
	}

	if (response.hasKey("apps"))
	{
		pbnjson::JValue apps = response["apps"];
		if (!apps.isArray())
			return false;

		AppList::instance()->removeAllList();
		for(ssize_t index = 0; index < apps.arraySize(); index++)
			AppList::instance()->handleAppResponse("added", apps[index]);
	}
	else if (response.hasKey("app"))
	{
		pbnjson::JValue app = response["app"];
		if (!app.isObject())
			return false;

		AppList::instance()->handleAppResponse(response["change"].asString(), app);
	}
	else
		return false;

	return true;
}

void AppList::handleAppResponse(const std::string& change, pbnjson::JValue app)
{
	std::string id = app["id"].asString();
	std::string icon = app["folderPath"].asString() + "/" + app["icon"].asString();

	if (change == "added")
		addToList(id, icon);
	else if (change == "updated")
		updateFromList(id, icon);
	else if (change == "removed")
		removeFromList(id);
}

bool AppList::isAppExist(const std::string& id)
{
	for(std::list<AppInfo>::const_iterator it = m_applist.begin(); it != m_applist.end(); ++it)
	{
		const AppInfo& item = (*it);
		if(item.appId == id) {
			return true;
		}
	}
	return false;
}

std::string AppList::getIcon(const std::string& id)
{
	for(std::list<AppInfo>::const_iterator it = m_applist.begin(); it != m_applist.end(); ++it)
	{
		const AppInfo& item = (*it);
		if(item.appId == id) {
			return item.icon;
		}
	}
	return "";
}

void AppList::addToList(const std::string& id, const std::string& icon)
{
	AppInfo appInfo;
	appInfo.appId = id;
	appInfo.icon = icon;
	m_applist.push_back(appInfo);
}

void AppList::updateFromList(const std::string &id, const std::string &icon)
{
	for(auto &item : m_applist)
	{
		if (item.appId == id)
		{
			item.icon = icon;
			return;
		}
	}

	addToList(id, icon);
}

void AppList::removeFromList(const std::string& id)
{
	for(std::list<AppInfo>::iterator it = m_applist.begin(); it != m_applist.end(); ++it)
    {
		AppInfo& item = (*it);
        if(item.appId == id) {
			m_applist.erase(it);
			break;
        }
     }
}

void AppList::removeAllList()
{
	m_applist.clear();
}
