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

#ifndef __NOTIFICATIONSERVICE_H__
#define __NOTIFICATIONSERVICE_H__

#include <string>
#include <stdlib.h>
#include <glib.h>
#include <luna-service2/lunaservice.h>
#include <JUtil.h>
#include <Logging.h>
#include <pbnjson.hpp>
#include <queue>
#include <boost/signals2.hpp>

#include "AppList.h"
#include "Settings.h"
#include "History.h"

class NotificationService
{
public:
    static NotificationService *instance();

    NotificationService();
    ~NotificationService();

    bool attach(GMainLoop* gml);
    void detach();

    LSHandle* getHandle() const { return m_service;}

    static bool cb_SubscriptionCanceled(LSHandle* lshandle, LSMessage *msg, void *user_data);

    static bool cb_getNotification(LSHandle* lshandle, LSMessage *msg, void *user_data);
    static bool cb_createToast(LSHandle* lshandle, LSMessage *msg, void *user_data);
    static bool cb_createAlert(LSHandle* lshandle, LSMessage *msg, void *user_data);
    static bool cb_createAlertIsAllowed(LSHandle* lshandle, LSMessage *msg, void *user_data);
    static bool cb_createInputAlert(LSHandle* lshandle, LSMessage *msg, void *user_data);
    static bool cb_enableToast(LSHandle* lshandle, LSMessage *msg, void *user_data);
    static bool cb_disableToast(LSHandle* lshandle, LSMessage *msg, void *user_data);
    static bool cb_closeToast(LSHandle* lshandle, LSMessage *msg, void *user_data);
    static bool cb_closeAlert(LSHandle* lshandle, LSMessage *msg, void *user_data);
    static bool cb_closeAllAlerts(LSHandle* lshandle, LSMessage *msg, void *user_data);
    static bool cb_closeInputAlert(LSHandle* lshandle, LSMessage *msg, void *user_data);
    static bool cb_enable(LSHandle* lshandle, LSMessage *msg, void *user_data);
    static bool cb_disable(LSHandle* lshandle, LSMessage *msg, void *user_data);
    static bool cb_createPincodePrompt(LSHandle* lshandle, LSMessage *msg, void *user_data);
    static bool cb_closePincodePrompt(LSHandle* lshandle, LSMessage *msg, void *user_data);
    static bool cb_getSystemSetting(LSHandle* lshandle, LSMessage *msg, void *user_data);
    static bool cb_setSystemSetting(LSHandle* lshandle, LSMessage *msg, void *user_data);
    static bool cb_getSystemSettingForCountry(LSHandle* lshandle, LSMessage *msg, void *user_data);

    static bool cb_createNotification(LSHandle* lshandle, LSMessage *msg, void *user_data);
    static bool cb_createRemoteNotification(LSHandle* lshandle, LSMessage *msg, void *user_data);
    static bool cb_removeNotification(LSHandle* lshandle, LSMessage *msg, void *user_data);
    static bool cb_removeRemoteNotification(LSHandle* lshandle, LSMessage *msg, void *user_data);
    static bool cb_removeAllNotification(LSHandle* lshandle, LSMessage *msg, void *user_data);
    static bool cb_getNotificationInfo(LSHandle* lshandle, LSMessage *msg, void *user_data);
    static bool cb_getRemoteNotificationInfo(LSHandle* lshandle, LSMessage *msg, void *user_data);
    static bool cb_createSignageAlert(LSHandle* lshandle, LSMessage *msg, void *user_data);
    static bool cb_launch(LSHandle* lshandle, LSMessage *msg, void *user_data);
    static bool parseDoc(const char *docname);

    bool postToastNotification(pbnjson::JValue toastNotificationPayload, bool staleMsg, bool persistentMsg, std::string &errorText);
    bool postAlertNotification(pbnjson::JValue alertNotificationPayload, std::string &errorText);
    bool postInputAlertNotification(pbnjson::JValue alertNotificationPayload, std::string &errText);
    void postPincodePromptNotification(pbnjson::JValue pincodePromptNotificationPayload);
    void postNotification(pbnjson::JValue alertNotificationPayload, bool remove, bool removeAll);

    void setUIEnabled(bool enabled);
    void processNotiMsgQueue();
    void processAlertMsgQueue();
    void processToastMsgQueue();
    void processInputAlertMsgQueue();
public:
    void resetPincode_message();
    void setPincode_message(LSMessage *msg);
    bool getPincode_message(LSMessage **msg);
    bool checkUnacceptablePincode(const std::string &rPincode);

private:
    void onAlertStatus(bool enabled);
    void onPincodePromptStatus(bool enabled);

private:
    std::queue<pbnjson::JValue> alertMsgQueue;
    std::queue<pbnjson::JValue> inputAlertMsgQueue;

    static bool alertRespondWithError(LSMessage* message, const std::string& sourceId, const std::string& alertId, const std::string& alertTitle, const std::string& alertMessage, const std::string& errorText);
    static bool alertRespond(LSMessage* msg, const std::string& sourceId, const std::string& alertId, const std::string& alertTitle, const std::string& alertMessage, const pbnjson::JValue& postCreateAlert);

private:
    LSMessage* m_pincode_message;
    std::string m_tmp_pincode;
    std::string m_pincode_timestamp;

    boost::signals2::scoped_connection m_connAlertStatus;
    bool UI_ENABLED;
    bool BLOCK_ALERT_NOTIFICATION;
    bool BLOCK_TOAST_NOTIFICATION;

    class NotiMsgItem {
        pbnjson::JValue payload;
        bool remove;
        bool removeAll;

        public:
        NotiMsgItem(pbnjson::JValue payload, bool remove, bool removeAll);
        pbnjson::JValue getPayLoad() { return payload; }
        bool getRemove() { return remove; }
        bool getRemoveAll() { return removeAll; }
    };

    typedef class NotiMsgItem notiMsgItem;

    std::queue<notiMsgItem*> notiMsgQueue;
    std::queue<pbnjson::JValue> toastMsgQueue;

    const char* getCaller(LSMessage *msg, const char* defaultName);
    void pushNotiMsgQueue(pbnjson::JValue payload, bool remove, bool removeAll);
    void popNotiMsgQueue();
    const char* getServiceName(LSMessage *msg);
    boost::signals2::scoped_connection m_connPincodePromptStatus;

protected:
    //LSMethod* get_private_methods() const;
    //LSMethod* get_public_methods() const;
    const char* get_service_name() const { return "com.webos.notification"; };
    const char* get_category() const { return "/"; };

    LSHandle* m_service;
};
#endif
