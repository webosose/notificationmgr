// Copyright (c) 2013-2020 LG Electronics, Inc.
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

#ifndef __HISTORY_H__
#define __HISTORY_H__

#include <string>
#include <stdlib.h>
#include <luna-service2/lunaservice.h>
#include <pbnjson.hpp>
#include <boost/signals2.hpp>

class History
{
public:
    History();
    ~History();
    static History* instance();

    static bool cbDb8Response(LSHandle* lshandle, LSMessage *message, void *user_data);
    static bool cbDb8getNotiResponse(LSHandle * lshandle,LSMessage * message,void * user_data);
    static bool cbDb8getRemoteNotiResponse(LSHandle * lshandle,LSMessage * message,void * user_data);
    static bool cbDb8getToastResponse(LSHandle* lshandle, LSMessage *message, void *user_data);

    void saveMessage(pbnjson::JValue msg);
    void deleteMessage(const std::string &key, const std::string& value);
    bool purgeAllData();
    bool purgeExpireData();
    bool setReadStatus(std::string toastId, bool readStatus);
    bool resetUserNotifications(int displayId);

    bool selectMessage(LSHandle* lshandle, const std::string& id, LSMessage *message);
    bool selectToastMessage(LSHandle* lshandle, const std::string& id, LSMessage *message);
    bool selectRemoteMessage(LSHandle* lshandle, const std::string& id, LSMessage *message);
    bool deleteNotiMessage(pbnjson::JValue notificationPayload);
    bool deleteRemoteNotiMessage(LSHandle* lsHandle, pbnjson::JValue notificationPayload);
    LSMessage* getReplyMsg();

protected:
    void onSystemTimeSync(bool sync);
    void onBoot(const std::string &boot);

private:
    LSMessage* replyMsg;
    bool m_expireData;
    bool selectNotiMessageFromDb(LSHandle* lshandle, const std::string& id, LSMessage *message, const std::string& property, const std::string& isRemote);
    bool deleteNotiMessageFromDb(LSHandle* lsHandle, pbnjson::JValue notificationPayload, const std::string& id, const std::string& idName, const std::string& propertyName, const std::string& propertyNameInArray);

    boost::signals2::scoped_connection m_connSystemTimeSync;
    boost::signals2::scoped_connection m_connBootStatus;
};

#endif
