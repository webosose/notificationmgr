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

#ifndef __APPLIST_H__
#define __APPLIST_H__

#include <string>
#include <stdlib.h>
#include <luna-service2/lunaservice.h>
#include <pbnjson.hpp>
#include <list>

class AppList {

public:
	AppList();
	~AppList();
	static AppList* instance();

	static bool cbAppMgrBusStatusNotification(LSHandle* lshandle, LSMessage *message, void *user_data);
	static bool cbAppMgrAppList(LSHandle* lshandle, LSMessage *message, void *user_data);
	static bool cbAppMgrGetAppInfo(LSHandle* lshandle, LSMessage *message, void *user_data);

	bool isAppExist(const std::string& id);
	std::string getIcon(const std::string& id);

private:
	void handleAppResponse(const std::string& change, pbnjson::JValue app);

	void addToList(const std::string& id, const std::string& icon);
	void updateFromList(const std::string& id, const std::string& icon);
	void removeFromList(const std::string& id);
	void removeAllList();

	void init();

private:
	struct AppInfo {
		std::string appId;
		std::string icon;
	};
	std::list<AppInfo> m_applist;
};

#endif
