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

//->Start of API documentation comment block
/**
@page com_webos_notification com.webos.notification

@brief Manages system notifications. Provides APIs to create toast and alert notification.

@{
@}
*/
//->End of API documentation comment block

#include "NotificationService.h"
#include "LSUtils.h"
#include "UiStatus.h"
#include "SystemTime.h"
#include "JsonParser.h"
#include "PincodeValidator.h"

#include <string>
#include <Utils.h>
#include <JUtil.h>
#include <Logging.h>
#include <pbnjson.hpp>
#include <vector>
#include "sax_parser.h"


#define APPMGR_LAUNCH_CALL "palm://com.webos.applicationManager/"
#define SETTING_API_CALL(handle, uri, params, ...) \
    LSCallOneReply(handle, uri, params, ##__VA_ARGS__); \
    LOG_DEBUG("%s: SETTING_ call %s %s", __FUNCTION__, uri, params)
#define APPMGR_LAUNCH_METHOD "launch"
#define PRIVILEGED_SOURCE "com.lge.service.remotenotification"
#define PRIVILEGED_APP_SOURCE "com.lge.app.remotenotification"
#define PRIVILEGED_SYSTEM_UI_SOURCE "com.webos.surfacemanager"
#define PRIVILEGED_SYSTEM_UI_NOTI "com.webos.app.notification"
#define PRIVILEGED_CLOUDLINK_SOURCE "com.lge.service.cloudlink"
#define ALERTAPP "com.webos.app.commercial.alert"

static NotificationService* s_instance = 0;
std::string NotificationService::m_user_name = "guest";
int NotificationService::m_display_id = 0;
NotificationService::toastCount NotificationService::toastCountVector[] = {};

//LS2 Functions
static LSMethod s_methods[] =
{
    { "createToast", NotificationService::cb_createToast},
    { "createAlert", NotificationService::cb_createAlert},
    { "closeToast", NotificationService::cb_closeToast},
    { "closeAlert", NotificationService::cb_closeAlert},
    { "removeNotification", NotificationService::cb_removeNotification},
    { "getToastNotification", NotificationService::cb_getNotification},
    { "getAlertNotification", NotificationService::cb_getNotification},
    { "closeAllAlerts", NotificationService::cb_closeAllAlerts},
    { "enable", NotificationService::cb_enable},
    { "disable", NotificationService::cb_disable},
    { "removeAllNotification", NotificationService::cb_removeAllNotification},
    { "getToastCount", NotificationService::cb_getToastCount},
    { "getToastList", NotificationService::cb_getToastList},
    { "setToastStatus", NotificationService::cb_setToastStatus},
    {0, 0}
};

using namespace std::placeholders;

NotificationService::NotificationService()
    : UI_ENABLED(false), BLOCK_ALERT_NOTIFICATION(false), BLOCK_TOAST_NOTIFICATION(false)
{
    m_service = 0;
    if (UiStatus::instance().alert())
        m_connAlertStatus = (UiStatus::instance().alert())->sigStatus.connect(
        std::bind(&NotificationService::onAlertStatus, this, _1)
        );
}

NotificationService::~NotificationService()
{
}

NotificationService* NotificationService::instance()
{

	if(!s_instance) {
		s_instance = new NotificationService();
	}
	return s_instance;
}

bool NotificationService::attach(GMainLoop *gml)
{

	LSErrorSafe lse;

	if(!LSRegister(get_service_name(), &m_service, &lse))
	{
		LOG_ERROR(MSGID_SERVICE_REG_ERR, 2, PMLOGKS("SERVICE_NAME", get_service_name()), PMLOGKS("ERROR_MESSAGE", lse.message), "Failed to register service in %s", __PRETTY_FUNCTION__);
		return false;
	}

	if(!LSRegisterCategory(m_service, get_category(), s_methods, NULL, NULL, &lse))
	{
		LOG_ERROR(MSGID_CATEGORY_REG_ERR, 2, PMLOGKS("SERVICE_NAME", get_service_name()), PMLOGKS("ERROR_MESSAGE", lse.message), "Failed to register category in %s", __PRETTY_FUNCTION__);
		return false;
	}

	if(!LSGmainAttach(m_service, gml, &lse))
	{
		LOG_ERROR(MSGID_SERVICE_ATTACH_ERR, 2, PMLOGKS("SERVICE_NAME", get_service_name()), PMLOGKS("ERROR_MESSAGE", lse.message), "Failed to attach error in %s", __PRETTY_FUNCTION__);
		return false;
	}

	if (!LSSubscriptionSetCancelFunction(getHandle(), cb_SubscriptionCanceled, this, &lse))
	{
		LOG_ERROR(MSGID_SERVICE_SET_CANCEL_ERR, 2,
			PMLOGKS("SERVICE_NAME", get_service_name()),
			PMLOGKS("ERROR_MESSAGE", lse.message), " ");
		return false;
	}

	AppList::instance();
	Settings::instance();

	SystemTime::instance().startSync();
	History::instance();

	return true;
}

void NotificationService::detach()
{
	LSErrorSafe lse;
	if(!LSUnregister(m_service, &lse))
	{
		LOG_ERROR(MSGID_SERVICE_DETACH_ERR, 2, PMLOGKS("SERVICE_NAME", get_service_name()), PMLOGKS("ERROR_MESSAGE", lse.message), "Failed to detach error in %s", __PRETTY_FUNCTION__);
		return;
	}

	m_service = 0;
}

const char* NotificationService::getServiceName(LSMessage *msg)
{
	const char* caller = LSMessageGetSenderServiceName(msg);
	if(!caller)
    {
       LOG_WARNING(MSGID_CA_CALLERID_MISSING, 0, "Caller ID is missing in %s", __PRETTY_FUNCTION__);
       caller = "Anonymous";
    }

	return caller;
}

void NotificationService::pushNotiMsgQueue(pbnjson::JValue payload, bool remove, bool removeAll) {
    LOG_WARNING("notificationmgr", 0, "[%s:%d] %s %d %d", __FUNCTION__, __LINE__, JUtil::jsonToString(payload).c_str(), remove, removeAll);
    notiMsgItem *item = new NotiMsgItem(payload, remove, removeAll);
    notiMsgQueue.push(item);
}

void NotificationService::popNotiMsgQueue() {
    LOG_WARNING("notificationmgr", 0, "[%s:%d]", __FUNCTION__, __LINE__);
    if(!notiMsgQueue.empty()) {
        delete notiMsgQueue.front();
        notiMsgQueue.pop();
    }
}

//->Start of API documentation comment block
/**
@page com_webos_notification com.webos.notification
@{
@section com_webos_notification_getNotification getNotification

System UI subscribes to this method and show notification on the screen

@par Parameters
Name | Required | Type | Description
-----|----------|------|------------
subscribe | yes  | Boolean | True

@par Returns(Call)
None

@par Returns(Subscription)
Name | Required | Type | Description
-----|----------|------|------------
subscribed | yes  | Boolean | True
returnValue | yes | Boolean | True

@}
*/
//->End of API documentation comment block

bool NotificationService::cb_getNotification(LSHandle* lshandle, LSMessage *msg, void *user_data)
{
	LSErrorSafe lserror;

	bool subscribed = false;
	bool success = false;
	bool subscribeUI = false;
    std::string checkCaller = "";
    std::string caller = LSUtils::getCallerId(msg);
    if (caller.empty())
        caller = "Anonymous";
    // Check for Caller Id
    checkCaller = Utils::extractSourceIdFromCaller(caller);
    LOG_DEBUG("cb_getNotification Caller = %s", checkCaller.c_str());

    if ((std::string(checkCaller).find(PRIVILEGED_SYSTEM_UI_SOURCE) != std::string::npos)
       ||(std::string(checkCaller).find(PRIVILEGED_SYSTEM_UI_NOTI) != std::string::npos))
    {
        subscribeUI = true;
        if(LSMessageIsSubscription(msg))
		{
			success = LSSubscriptionProcess(lshandle, msg, &subscribed, &lserror);
		}
    }

	LOG_DEBUG("cb_getNotification success = %d, subscribeUI = %d", success, subscribeUI);
	pbnjson::JValue json = pbnjson::Object();
	json.put("returnValue", success);

	if(success && subscribeUI)
	{
            json.put("subscribed", true);
            Utils::async([=] { UiStatus::instance().enable(UiStatus::ENABLE_UI); });
            NotificationService::instance()->setUIEnabled(true);
	        NotificationService::instance()->processNotiMsgQueue();
	        NotificationService::instance()->processAlertMsgQueue();
	        NotificationService::instance()->processToastMsgQueue();
	}
	else
	{
		json.put("subscribed", false);
	}

	std::string result = pbnjson::JGenerator::serialize(json, pbnjson::JSchemaFragment("{}"));
	if(!LSMessageReply(lshandle, msg, result.c_str(), &lserror))
	{
		return false;
	}

	return true;
}

bool NotificationService::cb_getToastCount(LSHandle* lshandle, LSMessage *msg, void *user_data)
{
    LSErrorSafe lserror;

    bool subscribed = false;
    bool subscribeUI = false;
    std::string checkCaller="";
    std::string caller = LSUtils::getCallerId(msg);
    if (caller.empty())
        caller = "Anonymous";
    // Check for Caller Id of notification app
    checkCaller = Utils::extractSourceIdFromCaller(caller);
    std::string method = LSUtils::getMethod(msg);
    LOG_DEBUG("cb_getToastCount Caller = %s", checkCaller.c_str());

    pbnjson::JValue request = pbnjson::Object();
    request = JUtil::parse(LSMessageGetPayload(msg), "", nullptr);

    if ((std::string(checkCaller).find(PRIVILEGED_SYSTEM_UI_SOURCE) != std::string::npos )
       ||(std::string(checkCaller).find(PRIVILEGED_SYSTEM_UI_NOTI) != std::string::npos))
    {
        subscribeUI = true;

        if (LSMessageIsSubscription(msg))
            subscribed = LSSubscriptionProcess(lshandle, msg, &subscribed, &lserror);
    }

    LOG_INFO(MSGID_SVC_GET_NOTIFICATION, 4,
        PMLOGKS("caller", checkCaller.c_str()),
        PMLOGKS("method", method.c_str()),
        PMLOGKS("subscribe", LSMessageIsSubscription(msg) ? "true" : "false"),
        PMLOGKS("subscribed", subscribed ? "true" : "false"), " ");

    pbnjson::JValue json = pbnjson::Object();

    if (!request["displayId"].isNumber())
    {
        json.put("returnValue", false);
        json.put("errorText", "displayId should be a number");
    }
    else
    {
        int displayId = request["displayId"].asNumber<int>();
        if (displayId != 0 && displayId != 1)
        {
            json.put("returnValue", false);
            json.put("errorText", "Invalid displayId. Must be 0 or 1");
        }
        else
        {
            json.put("readCount", toastCountVector[displayId].readCount);
            json.put("unreadCount", toastCountVector[displayId].unreadCount);
            json.put("returnValue", true);
            json.put("subscribed", subscribed);
        }
    }

    if (subscribeUI && subscribed)
    {
        Utils::async([=] {
            if (method == "getToastCount" && UiStatus::instance().toast())
                (UiStatus::instance().toast())->enable(UiStatus::ENABLE_UI);
        });

        //BreadnutMergeTODO: Below 2 lines are not working properly
        NotificationService::instance()->setUIEnabled(true);
        NotificationService::instance()->processNotiMsgQueue();
    }

    std::string result = pbnjson::JGenerator::serialize(json, pbnjson::JSchemaFragment("{}"));

    if(!LSMessageReply(lshandle, msg, result.c_str(), &lserror))
    {
        return false;
    }

    return true;
}

bool NotificationService::cb_SubscriptionCanceled(LSHandle *lshandle, LSMessage *msg, void *user_data)
{
	const char *val = LSMessageGetMethod(msg);
	std::string method = val ? val : std::string("");

	val = LSMessageGetKind(msg);
	std::string kind = val ? val : std::string("");
	unsigned int subscribers = LSSubscriptionGetHandleSubscribersCount(lshandle, kind.c_str());

    LOG_DEBUG("cb_SubscriptionCanceled: %s, subscribers:%u", method.c_str(), subscribers);

	if (method == "getToastNotification" ||
		method == "getAlertNotification")
        {
		if (subscribers > 1)
			return true;

		Utils::async([=] { UiStatus::instance().disable(UiStatus::ENABLE_UI); });
		return true;
	}

	return true;
}

//->Start of API documentation comment block
/**
@page com_webos_notification com.webos.notification
@{
@section com_webos_notification_createToast createToast

Creates toast notification

@par Parameters
Name | Required | Type | Description
-----|----------|------|------------
sourceId | yes  | String | It should be App or Service Id that creates the toast
iconUrl  | no   | String | File path to icon. The path should be local to device
message  | yes  | String | Toast message which can be upto 60 characters long
onclick  | no   | Object | Defines the toast action
noaction | no   | Boolean | Indicates no action is required.
stale    | no   | Boolean | Indicates toast is old and doesn't need to be displayed
persistent | no | Boolean | Indicates toast is saved on history
schedule | no   | Object | Defines the persistent message schedule
type     | no   | String | Defines toast type
extra    | no   | Object | Defines extra resources

@par Returns(Call)
Name | Required | Type | Description
-----|----------|------|------------
returnValue | yes | Boolean | True
toastId | yes | String | This would be sourceId + "-" + Timestamp.

@par Returns(Subscription)
None

@}
*/
//->End of API documentation comment block

bool NotificationService::cb_createToast(LSHandle* lshandle, LSMessage *msg, void *user_data)
{
    int displayId = 0;
    LSErrorSafe lserror;

    bool success = false;

    std::string errText;
    std::string sourceId;
    std::string message;
    std::string target;
    std::string launchAppId;
    std::string iconPath;
    bool noaction = false;
    bool staleMsg = false;
    bool persistentMsg = false;
    bool privilegedSource = false;
    std::string title;
    std::string type;
    bool isSysReq = false;
    bool isCradleReq = false;
    bool ignoreDisable = false;

    pbnjson::JValue request;
    pbnjson::JValue postCreateToast;
    pbnjson::JValue onclick;
    pbnjson::JValue action;
    pbnjson::JValue appMgrParams;

    pbnjson::JValue reqSchedule;
    pbnjson::JValue schedule;

    pbnjson::JValue reqExtra;

    std::string timestamp;

    JUtil::Error error;

    std::string errorText;
    pbnjson::JValue getActiveUserParams;
    pbnjson::JValue postToastCount = pbnjson::Object();
    bool toastCountStatus = false;
    int readCount, unreadCount = 0;

    std::string caller = LSUtils::getCallerId(msg);
    if (caller.empty())
    {
        LOG_WARNING(MSGID_CT_CALLERID_MISSING, 0, "Caller ID is missing in %s", __PRETTY_FUNCTION__);
        errText = "Unknown Source";
        goto Done;
    }
    LOG_WARNING(MSGID_NOTIFICATIONMGR, 0, "[%s:%d] Caller: %s", __FUNCTION__, __LINE__, caller.c_str());

    request = JUtil::parse(LSMessageGetPayload(msg), "createToast", &error);

    if (request.isNull())
    {
        LOG_WARNING(MSGID_CT_PARSE_FAIL, 0, "Message parsing error in %s", __PRETTY_FUNCTION__);
        errText = "Message is not parsed";
        goto Done;
    }

    if (Settings::instance()->isPrivilegedSource(caller) || Settings::instance()->isPartOfAggregators(std::string(caller)))
    {
        privilegedSource = true;
    }

    m_display_id = request["displayId"].asNumber<int>();
    sourceId = request["sourceId"].asString();
    if (request.hasKey("displayId"))
    {
        displayId = request["displayId"].asNumber<int>();
        LOG_DEBUG("Key Display ID: %d", displayId);
        // LOG_INFO("port Key Display ID: %d", displayId);
        LOG_WARNING(MSGID_NOTIFICATIONMGR, 0, "port [%s:%d] displayId: %d", __FUNCTION__, __LINE__, displayId);
    }

    if (displayId >= 0)
        toastCountVector[displayId].unreadCount++;

    if (sourceId.length() == 0)
    {
        sourceId = Utils::extractSourceIdFromCaller(caller);
    }

    // SourceId and Caller should match for non-privileged apps
    if (!privilegedSource)
    {
        if (std::string(caller).find(sourceId, 0) == std::string::npos)
        {
            LOG_WARNING(MSGID_CT_SOURCEID_INVALID, 0, "Source ID is invalid in %s", __PRETTY_FUNCTION__);
            errText = "Invalid source id specified";
            goto Done;
        }
    }

    message = request["message"].asString();
    if (message.length() == 0)
    {
        LOG_WARNING(MSGID_CT_MSG_EMPTY, 0, "Empty message is given in %s", __PRETTY_FUNCTION__);
        errText = "Message can't be empty";
        goto Done;
    }

    title = request["title"].asString();

    ignoreDisable = request["ignoreDisable"].asBool();

    if (!ignoreDisable && UiStatus::instance().toast() && !(UiStatus::instance().toast())->isEnabled(UiStatus::ENABLE_ALL & ~UiStatus::ENABLE_UI))
    {
        errText = "Toast is blocked by " + (UiStatus::instance().toast())->reason();
        goto Done;
    }

    postCreateToast = pbnjson::Object();
    postCreateToast.put("sourceId", sourceId);
    postCreateToast.put("displayId", displayId);
    action = pbnjson::Object();

    if (Settings::instance()->isPrivilegedSource(caller))
    {
        iconPath = request["iconUrl"].asString();
    }
    else
    {
        iconPath = AppList::instance()->getIcon(sourceId);
    }

    if (iconPath.length() != 0 && Utils::verifyFileExist(iconPath.c_str()))
    {
        postCreateToast.put("iconUrl", "file://" + iconPath);
        postCreateToast.put("iconPath", iconPath);
    }
    else
    {
        postCreateToast.put("iconUrl", "file://" + Settings::instance()->getDefaultIcon("toast"));
        postCreateToast.put("iconPath", Settings::instance()->getDefaultIcon("toast"));
    }

    // Remove if there is any space character except ' '
    std::replace_if(message.begin(), message.end(), Utils::isEscapeChar, ' ');
    postCreateToast.put("message", message);

    std::replace_if(title.begin(), title.end(), Utils::isEscapeChar, ' ');
    postCreateToast.put("title", title);

    staleMsg = request["stale"].asBool();
    persistentMsg = request["persistent"].asBool();

    Utils::createTimestamp(timestamp);
    postCreateToast.put("timestamp", timestamp);
    if (SystemTime::instance().isSynced())
        postCreateToast.put("timesource", SystemTime::instance().getTimeSource());

    postCreateToast.put("type", request["type"].asString());

    if (!staleMsg && UiStatus::instance().toast() && !(UiStatus::instance().toast())->isEnabled(UiStatus::ENABLE_UI))
    {
        errText = "UI is not yet ready";
        goto Done;
    }

    if (request["onlyToast"].isNull())
    {
        postCreateToast.put("onlyToast", true);
    }
    else
    {
        postCreateToast.put("onlyToast", request["onlyToast"].asBool());
    }

    if (request["isSysReq"].isNull())
    {
        postCreateToast.put("isSysReq", isSysReq);
    }
    else
    {
        postCreateToast.put("isSysReq", request["isSysReq"].asBool());
    }

    if (request["isCradleReq"].isNull())
    {
        postCreateToast.put("isCradleReq", isCradleReq);
    }
    else
    {
        postCreateToast.put("isCradleReq", request["isCradleReq"].asBool());
    }

    reqSchedule = request["schedule"];
    if (reqSchedule.isObject())
    {
        int64_t expire = 0;
        if (reqSchedule.hasKey("expire"))
            expire = reqSchedule["expire"].asNumber<int64_t>();

        if (expire != 0)
        {
            if (!SystemTime::instance().isSynced())
            {
                errText = std::string("System time is not synced yet");
                goto Done;
            }

            time_t currTime = time(NULL);
            if (expire < currTime)
            {
                errText = std::string("Expire time already has passed: expire(")
                    + Utils::toString(expire)
                    + std::string(") < curtime(")
                    + Utils::toString(currTime)
                    + std::string(")");
                goto Done;
            }

            pbnjson::JValue schedule = pbnjson::Object();
            schedule.put("expire", expire);
            postCreateToast.put("schedule", schedule);
        }
    }
    else
    {
        time_t currTime = time(NULL);
        int64_t expire = currTime +
                         (static_cast<int64_t>(Settings::instance()->getRetentionPeriod()) * 24 * 60 * 60);

        pbnjson::JValue schedule = pbnjson::Object();
        schedule.put("expire", expire);
        postCreateToast.put("schedule", schedule);
    }

    reqExtra = request["extra"];
    if (reqExtra.isObject())
    {
        pbnjson::JValue reqImages = reqExtra["images"];
        if (reqImages.isArray() && reqImages.arraySize() > 0)
        {
            pbnjson::JValue images = pbnjson::Array();

            size_t arraySize = reqImages.arraySize();
            for (size_t i = 0; i < arraySize; ++i)
            {
                pbnjson::JValue reqImage = reqImages[i];
                std::string uri = reqImage["uri"].asString();

                if (uri.empty())
                {
                    errText = std::string("image should have uri");
                    goto Done;
                }

                pbnjson::JValue image = pbnjson::Object();
                image.put("uri", uri);
                images.append(image);
            }

            postCreateToast.put("images", images);
        }
    }

    noaction = request["noaction"].asBool();
    if (noaction)
    {
        postCreateToast.put("action", action);
        success = NotificationService::instance()->postToastNotification(postCreateToast, staleMsg, persistentMsg, errText);
        goto Done;
    }

    postCreateToast.put("readStatus", false);
    postCreateToast.put("user", m_user_name);
    LOG_DEBUG("Toast Payload: %s", JUtil::jsonToString(postCreateToast).c_str());

    appMgrParams = pbnjson::Object();
    onclick = request["onclick"];

    if (onclick.isNull()) // launch the app that creates the toast.
    {
        postToastCount.put("displayId", displayId);
        if (displayId >= 0)
        {
            postToastCount.put("readCount", toastCountVector[displayId].readCount);
            postToastCount.put("unreadCount", toastCountVector[displayId].unreadCount);
            postToastCount.put("totalCount", toastCountVector[displayId].readCount + toastCountVector[displayId].unreadCount);
        }
        toastCountStatus = NotificationService::instance()->postToastCountNotification(postToastCount, staleMsg, persistentMsg, errText);
        // Check the SourceId exist in the App list.
        if (AppList::instance()->isAppExist(sourceId))
        {
            appMgrParams.put("id", sourceId);
        }
        else
        {
            postCreateToast.put("action", action);
            success = NotificationService::instance()->postToastNotification(postCreateToast, staleMsg, persistentMsg, errText);
            goto Done;
        }
    }
    else
    {
        launchAppId = onclick["appId"].asString();
        target = onclick["target"].asString();
        if (launchAppId.length() != 0)
        { // check for valid App
            appMgrParams.put("id", launchAppId);
            if (!onclick["params"].isNull())
                appMgrParams.put("params", onclick["params"]);
        }
        else if (target.length() != 0)
        {
            appMgrParams.put("target", target);
        }
        else
        {
            // Check the SourceId exist in the App list.
            if (AppList::instance()->isAppExist(sourceId))
            {
                appMgrParams.put("id", sourceId);
            }
            else
            {
                postCreateToast.put("action", action);
                success = NotificationService::instance()->postToastNotification(postCreateToast, staleMsg, persistentMsg, errText);
                goto Done;
            }
        }
    }
    action.put("serviceURI", APPMGR_LAUNCH_CALL);
    action.put("serviceMethod", APPMGR_LAUNCH_METHOD);
    action.put("launchParams", appMgrParams);
    postCreateToast.put("action", action);

    // Post a message
    success = NotificationService::instance()->postToastNotification(postCreateToast, staleMsg, persistentMsg, errText);

Done:
    pbnjson::JValue json = pbnjson::Object();
    json.put("returnValue", success);

    if (!success)
    {
        json.put("errorText", errText);

        LOG_WARNING(MSGID_NOTIFY_INVOKE_FAILED, 4,
                    PMLOGKS("SOURCE_ID", sourceId.c_str()),
                    PMLOGKS("TYPE", "TOAST"),
                    PMLOGKS("ERROR", errText.c_str()),
                    PMLOGKS("CONTENT", message.c_str()),
                    " ");
    }
    else
    {
        json.put("toastId", (sourceId + "-" + timestamp));

        LOG_INFO_WITH_CLOCK(MSGID_NOTIFY_INVOKE, 3,
            PMLOGKS("SOURCE_ID", sourceId.c_str()),
            PMLOGKS("TYPE", "TOAST"),
            PMLOGKS("CONTENT", message.c_str()),
            " ");
    }

    std::string result = pbnjson::JGenerator::serialize(json, pbnjson::JSchemaFragment("{}"));
    if (!LSMessageReply(lshandle, msg, result.c_str(), &lserror))
    {
        return false;
    }

    return true;
}

bool NotificationService::alertRespondWithError(LSMessage* message, const std::string& sourceId, const std::string& alertId, const std::string& alertTitle, const std::string& alertMessage, const std::string& errorText)
{
	pbnjson::JValue json = pbnjson::Object();
	json.put("returnValue", false);
	json.put("errorText", errorText);

	LOG_WARNING(MSGID_NOTIFY_INVOKE_FAILED, 5,
		PMLOGKS("SOURCE_ID", sourceId.c_str()),
		PMLOGKS("TYPE", "ALERT"),
		PMLOGKS("ERROR", errorText.c_str()),
		PMLOGKS("TITLE", alertTitle.c_str()),
		PMLOGKS("MESSAGE", alertMessage.c_str()),
		" ");

	std::string result = pbnjson::JGenerator::serialize(json, pbnjson::JSchemaFragment("{}"));
	return LSMessageRespond(message, result.c_str(), NULL);
}

bool NotificationService::alertRespond(LSMessage* msg, const std::string& sourceId, const std::string& alertId, const std::string& alertTitle, const std::string& alertMessage, const pbnjson::JValue& postCreateAlert)
{
	if (!UiStatus::instance().isEnabled(UiStatus::ENABLE_UI))
	{
		//save the message in the queue.
		LOG_DEBUG("createAlert: UI is not yet ready. push into msg queue.");
		NotificationService::instance()->alertMsgQueue.push(postCreateAlert);
	}
	else
	{
		//Post the message
		std::string errText;
		if(!NotificationService::instance()->postAlertNotification(postCreateAlert, errText))
		{
			return alertRespondWithError(msg, sourceId, alertId, alertTitle, alertMessage, errText);
		}
	}

	pbnjson::JValue json = pbnjson::Object();
	json.put("returnValue", true);
	json.put("alertId", alertId);

	LOG_INFO(MSGID_NOTIFY_INVOKE, 4,
		PMLOGKS("SOURCE_ID", sourceId.c_str()),
		PMLOGKS("TYPE", "ALERT"),
		PMLOGKS("TITLE", alertTitle.c_str()),
		PMLOGKS("MESSAGE", alertMessage.c_str()),
		" ");

	std::string result = pbnjson::JGenerator::serialize(json, pbnjson::JSchemaFragment("{}"));
	return LSMessageRespond(msg, result.c_str(), NULL);
}

struct AlertData
{
	LSMessage* message;
	std::string sourceId;
	std::string alertId;
	std::string alertTitle;
	std::string alertMessage;
	pbnjson::JValue postCreateAlert;
	std::vector<std::string> uriList;
	std::string uriVerified;
	std::string serviceNameCreateAlert;
};

bool NotificationService::alertRespond(bool success, const std::string &errorText,
        LSMessageWrapper msg, const std::string& sourceId,
        const std::string& alertId, const std::string& alertTitle, const std::string& alertMessage,
        const pbnjson::JValue& postCreateAlert)
{
    std::string errText = errorText;

    if (success)
    {
        if (UiStatus::instance().alert() && !(UiStatus::instance().alert())->isEnabled(UiStatus::ENABLE_UI))
        {
            //save the message in the queue.
            LOG_DEBUG("createAlert: UI is not yet ready. push into msg queue.");
            NotificationService::instance()->alertMsgQueue.push(postCreateAlert);
        }
        else
        {
            //Post the message
            success = NotificationService::instance()->postAlertNotification(postCreateAlert, errText);
        }
    }

    pbnjson::JValue json = pbnjson::Object();
    if (!success)
    {
        json.put("returnValue", false);
        json.put("errorText", errText);

        LOG_WARNING(MSGID_NOTIFY_INVOKE_FAILED, 5,
                PMLOGKS("SOURCE_ID", sourceId.c_str()),
                PMLOGKS("TYPE", "ALERT"),
                PMLOGKS("ERROR", errText.c_str()),
                PMLOGKS("TITLE", alertTitle.c_str()),
                PMLOGKS("MESSAGE", alertMessage.c_str()),
                " ");
    }
    else
    {
        json.put("returnValue", true);
        json.put("alertId", alertId);

        LOG_INFO(MSGID_NOTIFY_INVOKE, 4,
                PMLOGKS("SOURCE_ID", sourceId.c_str()),
                PMLOGKS("TYPE", "ALERT"),
                PMLOGKS("TITLE", alertTitle.c_str()),
                PMLOGKS("MESSAGE", alertMessage.c_str()),
                " ");
    }

    return LSMessageRespond(msg, json.stringify().c_str(), NULL);
}
//->Start of API documentation comment block
/**
@page com_webos_notification com.webos.notification
@{
@section com_webos_notification_createAlert createAlert

Creates system alert notification

@par Parameters
Name | Required | Type | Description
-----|----------|------|------------
iconUrl  | no   | String | File path to icon. The path should be local to device
title    | no   | String | Alert title
message  | yes  | String | Alert message
modal    | no   | Boolean | Set to true for modal alert
buttons  | yes  | Object | Defines the button label and onclick action

@par Returns(Call)
Name | Required | Type | Description
-----|----------|------|------------
returnValue | yes | Boolean | True
alertId | yes | String | This would be sourceId + "-" + Timestamp.

@par Returns(Subscription)
None

@}
*/
//->End of API documentation comment block

bool NotificationService::cb_createAlert(LSHandle* lshandle, LSMessage *msg, void *user_data)
{
	LSErrorSafe lserror;
	std::string alertId;
	pbnjson::JValue request;
	pbnjson::JValue postCreateAlert;
	pbnjson::JValue alertInfo;

	pbnjson::JValue buttonArray;
	pbnjson::JValue action;
	pbnjson::JValue buttonsCreated = pbnjson::Array();

	std::string sourceId;
	std::string message;
	std::string title;
	std::string timestamp;
	std::string iconPath;
	std::string onclickString;
	std::string oncloseString;
	std::string buttonLabel;
	std::string clickSchemaString;

    std::string alertType;
    std::string buttonIconPath;
    std::string buttonType;
    int checkConfirmType;
    int checkWarningType;
    int checkBattertType;
    int checkOkType;
    int checkCancelType;
    bool ignoreDisable = false;
    int displayId = 0;

	unsigned found = 0;
	JUtil::Error error;

	std::vector<std::string> uriList;

    std::string caller = LSUtils::getCallerId(msg);
	if(caller.empty())
	{
	    LOG_WARNING(MSGID_CALLERID_MISSING, 1, PMLOGKS("API", "createAlert"), " ");
	    return alertRespond(false, "Unknown Source", msg, sourceId, alertId, "", "");
	}

	sourceId = std::string(caller);

	//Check for Caller Id
	if(!Settings::instance()->isPrivilegedSource(caller) && !Settings::instance()->isPartOfAggregators(sourceId))
	{
		LOG_WARNING(MSGID_PERMISSION_DENY, 1,
			PMLOGKS("API", "createAlert"), " ");
		return alertRespondWithError(msg, sourceId, alertId, "", "", "Permission Denied");
	}

	request = JUtil::parse(LSMessageGetPayload(msg), "createAlert", &error);

	if(request.isNull())
	{
		LOG_WARNING(MSGID_CA_PARSE_FAIL, 0, "Message parsing error in %s", __PRETTY_FUNCTION__ );
		return alertRespondWithError(msg, sourceId, alertId, "", "", "Message is not parsed");
	}

	alertInfo = pbnjson::Object();
	postCreateAlert = pbnjson::Object();

	alertInfo.put("sourceId",sourceId);

	if (request.hasKey("displayId")) {
	    displayId = request["displayId"].asNumber<int>();
//	    LOG_INFO("port displayId: %d", displayId);
	    LOG_WARNING(MSGID_NOTIFICATIONMGR, 0, "port [%s:%d] displayId: %d", __FUNCTION__, __LINE__, displayId);
	}
	alertInfo.put("displayId", displayId);

	if(!request["title"].isNull())
	{
		title = request["title"].asString();
		//Remove if there is any space character except ' '
		std::replace_if(title.begin(), title.end(), Utils::isEscapeChar, ' ');
		alertInfo.put("title", title);
	}

	message = request["message"].asString();
	if(message.length() == 0)
	{
		LOG_WARNING(MSGID_CA_MSG_EMPTY, 0, "Empty message is given in %s", __PRETTY_FUNCTION__);
		return alertRespondWithError(msg, sourceId, alertId, title, "", "Message can't be empty");
	}

        ignoreDisable = request["ignoreDisable"].asBool();

        if (!ignoreDisable && UiStatus::instance().alert() && !(UiStatus::instance().alert())->isEnabled(UiStatus::ENABLE_ALL & ~UiStatus::ENABLE_UI))
        {
            return alertRespondWithError(msg, sourceId, alertId, title, message, "Alert is blocked by " + (UiStatus::instance().alert())->reason());
        }

	//Copy the message
	//Remove if there is any space character except ' '
        std::replace_if(message.begin(), message.end(), Utils::isEscapeChar, ' ');
        alertInfo.put("message", message);

	//Check the icon and copy it.
	if(!request["iconUrl"].isNull())
	{
		iconPath = request["iconUrl"].asString();
		if(iconPath.length() != 0 && Utils::verifyFileExist(iconPath.c_str()))
		{
			alertInfo.put("iconUrl", "file://"+iconPath);
		}
		else
		{
			alertInfo.put("iconUrl", "file://"+ Settings::instance()->getDefaultIcon("alert"));
		}
	}

	//Check for modal property. If not defined, add it and set to false.
	if(request["modal"].isNull())
	{
		alertInfo.put("modal", false);
	}
	else
	{
		alertInfo.put("modal", request["modal"]);
	}

	//Check for timeout property
	if(!request["autoTimeout"].isNull())
	{
		int timeout = request["autoTimeout"].asNumber<int>();
		timeout = (timeout > 15) ? 15 : timeout;
		alertInfo.put("timeout", timeout);
	}

	//Check Alert Type
    if(!request["type"].isNull())
    {
        alertType = request["type"].asString();

        checkConfirmType = alertType.compare("confirm");
        checkWarningType = alertType.compare("warning");
        checkBattertType = alertType.compare("battery");

        if(checkConfirmType == 0 || checkWarningType == 0 || checkBattertType == 0)
        {
            alertInfo.put("type", alertType);
        }
        if(checkConfirmType != 0 && checkWarningType != 0 && checkBattertType != 0)
        {
            LOG_WARNING("CA_MSG_TYPE_FAIL", 0, "Message type is wrong %s", __PRETTY_FUNCTION__);
            return alertRespondWithError(msg, sourceId, alertId, title, message, "Message type is only confirm, warning, or battery");
        }
    }

    if(request["isSysReq"].isNull())
    {
        alertInfo.put("isSysReq", false);
    }
    else
    {
        alertInfo.put("isSysReq", request["isSysReq"].asBool());
    }
////////// 15.01.05
    if(request["isNotiSave"].isNull())
    {
        alertInfo.put("isNotiSave", false);
    }
    else
    {
        alertInfo.put("isNotiSave", request["isNotiSave"].asBool());
    }

	//Check for onclose event object
	if(!request["onclose"].isNull())
	{
        pbnjson::JValue oncloseObj = request["onclose"];
		oncloseString = oncloseObj["uri"].asString();
	}

	alertInfo.put("onCloseAction", JsonParser::createActionInfo(request["onclose"]));
	alertInfo.put("onFailAction", JsonParser::createActionInfo(request["onfail"]));

	if(!oncloseString.empty())
	{
		uriList.push_back(oncloseString);
	}

	if(!request["buttons"].isNull())
    {
		buttonArray = request["buttons"];

		if(buttonArray.isArray())
		{
			if(buttonArray.arraySize() == 0)
			{
				LOG_WARNING(MSGID_CA_BUTTONS_EMPTY, 0, "No buttons are given in %s", __PRETTY_FUNCTION__);
				return alertRespondWithError(msg, sourceId, alertId, title, message, "Buttons can't be empty!");
			}

			for(ssize_t index = 0; index < buttonArray.arraySize(); ++index) {
				pbnjson::JValue buttonObj = pbnjson::Object();

				//Copy label
                buttonLabel = buttonArray[index]["label"].asString();
                std::replace_if(buttonLabel.begin(), buttonLabel.end(), Utils::isEscapeChar, ' ');

				buttonObj.put("label", buttonLabel);

				 //Check for buttonType
                if(!buttonArray[index]["buttonType"].isNull())
                {
                    buttonType = buttonArray[index]["buttonType"].asString();

                    checkOkType = buttonType.compare("ok");
                    checkCancelType = buttonType.compare("cancel");

                    if(checkOkType == 0 || checkCancelType == 0)
                    {
                        buttonObj.put("type", buttonType);
                    }
                    if(checkOkType != 0 && checkCancelType != 0)
                    {
                        LOG_WARNING("CA_MSG_TYPE_FAIL", 0, "Button type is wrong %s", __PRETTY_FUNCTION__);
                        return alertRespondWithError(msg, sourceId, alertId, title, message, "Button type is only ok or cancel");
                    }
                }

				//Check for onclick
				if(!buttonArray[index]["onclick"].isNull() || !buttonArray[index]["onClick"].isNull())
				{
					if(!buttonArray[index]["onclick"].isNull()) {
						clickSchemaString = "onclick";
					}
					else if(!buttonArray[index]["onClick"].isNull()) {
						clickSchemaString = "onClick";
					}
					action = pbnjson::Object();
					if(!Utils::isValidURI(buttonArray[index][clickSchemaString].asString()))
					{
						LOG_WARNING(MSGID_CA_SERVICEURI_INVALID, 0, "Invalid ServiceURI is given in %s", __PRETTY_FUNCTION__);
						return alertRespondWithError(msg, sourceId, alertId, title, message, "Invalid Service Uri in the onclick");
					}

					onclickString = buttonArray[index][clickSchemaString].asString();
					found = onclickString.find_last_of("/");

					action.put("serviceURI", onclickString.substr(0, found+1));
					action.put("serviceMethod", onclickString.substr(found+1));

					if(!buttonArray[index]["params"].isNull())
					{
						action.put("launchParams", buttonArray[index]["params"]);
					}
					else
					{
						action.put("launchParams", {});
					}

					buttonObj.put("action", action);
					if(!onclickString.empty())
					{
						uriList.push_back(onclickString);
					}
				}

				// Copy focus
				buttonObj.put("focus", buttonArray[index]["focus"].asBool());

				buttonsCreated.put(index, buttonObj);
			}
		}
		alertInfo.put("buttons", buttonsCreated);
	}

	postCreateAlert.put("alertAction", "open");
	postCreateAlert.put("alertInfo", alertInfo);

	Utils::createTimestamp(timestamp);
	postCreateAlert.put("timestamp", timestamp);

	alertId = sourceId + "-" + timestamp;
	alertInfo.put("alertId", alertId);


	if(uriList.empty())
	{
		return alertRespond(msg, sourceId, alertId, title, message, postCreateAlert);
	}

	LSMessageRef(msg);
	std::string uri = uriList.back();
	uriList.pop_back();

	AlertData *data = new AlertData();
	data->message = msg;
	data->sourceId = sourceId;
	data->alertId = alertId;
	data->alertTitle = title;
	data->alertMessage = message;
	data->uriList = uriList;
	data->uriVerified = uri;
        const char *serviceName = NotificationService::instance()->getServiceName(msg);
        if (serviceName)
            data->serviceNameCreateAlert = serviceName;
	data->postCreateAlert = postCreateAlert;

	std::string params = "{\"uri\": \"" + uri + "\", \"requester\": \"" + data->serviceNameCreateAlert + "\"}";
        if(!LSCall(NotificationService::instance()->getHandle(), "palm://com.palm.bus/isCallAllowed", params.c_str(), cb_createAlertIsAllowed, data, NULL, &lserror) && lserror.message)
        {
                delete data;
                return alertRespondWithError(msg, sourceId, alertId, title, message, std::string("Call failed - ") + lserror.message);
        }
	return true;
}

bool NotificationService::cb_createAlertIsAllowed(LSHandle* lshandle, LSMessage *msg, void *user_data)
{
        AlertData *data = static_cast<AlertData*>(user_data);
        LSErrorSafe lserror;
        JUtil::Error error;
        pbnjson::JValue request = JUtil::parse(LSMessageGetPayload(msg), "", &error);

        LSMessage* message = data->message;
        std::string sourceId = data->sourceId;
        std::string alertId = data->alertId;
        std::string alertTitle = data->alertTitle;
        std::string alertMessage = data->alertMessage;
        pbnjson::JValue postCreateAlert = data->postCreateAlert;
        std::string uriVerified = data->uriVerified;

        if(request.isNull())
        {
                LOG_WARNING(MSGID_CA_PARSE_FAIL, 0, "Message parsing error in %s", __PRETTY_FUNCTION__ );
                delete data;
                return alertRespondWithError(message, sourceId, alertId, alertTitle, alertMessage, "Message is not parsed");
        }

        if(!request["returnValue"].asBool())
        {

            LOG_WARNING(MSGID_CA_MSG_EMPTY, 0, "Call failed in %s", __PRETTY_FUNCTION__ );
            delete data;
                return alertRespondWithError(message, sourceId, alertId, alertTitle, alertMessage, "Call failed");
        }

        if(!request["allowed"].asBool())
        {
                delete data;
                return alertRespondWithError(message, sourceId, alertId, alertTitle, alertMessage, "Not allowed to call method specified in the uri: " + uriVerified);
        }

        if(data->uriList.empty())
        {
                delete data;
                return alertRespond(message, sourceId, alertId, alertTitle, alertMessage, postCreateAlert);
        }

        std::string uri = data->uriList.back();
        data->uriList.pop_back();

        data->uriVerified = uri;

        std::string params = "{\"uri\": \"" + uri + "\", \"requester\": \"" + data->serviceNameCreateAlert + "\"}";
        if(!LSCall(NotificationService::instance()->getHandle(), "palm://com.palm.bus/isCallAllowed", params.c_str(), cb_createAlertIsAllowed, data, NULL, &lserror) && lserror.message)
        {
                delete data;
                return alertRespondWithError(message, sourceId, alertId, alertTitle, alertMessage, std::string("Call failed - ") + lserror.message);
        }

        return true;
}

bool NotificationService::postToastNotification(pbnjson::JValue toastNotificationPayload, bool staleMsg, bool persistentMsg, std::string &errorText)
{
    LSErrorSafe lserror;
    std::string toastPayload;

    //Save the message
    if (persistentMsg)
    {
        History::instance()->saveMessage(toastNotificationPayload);
    }

    if (staleMsg || (UiStatus::instance().toast() && (UiStatus::instance().toast())->isSilence()))
    {
        std::string reason;
        if (staleMsg) reason = "stale";
        else if (UiStatus::instance().toast() && (UiStatus::instance().toast())->isSilence()) reason = "silence";

        LOG_INFO(MSGID_NOTIFY_NOTPOST, 1,
                PMLOGKS("REASON", reason.c_str()),
                " ");

        return true;
    }

    //In Factory Mode, disable toast notifications
    if(BLOCK_TOAST_NOTIFICATION) {
        LOG_DEBUG("Toast is blocked in factory mode");
        return false;
    }

    LOG_DEBUG("postToastNotification UI_ENABLED = %d", UI_ENABLED);
    if(!UI_ENABLED)
    {
        //save the message in the queue.
        toastMsgQueue.push(toastNotificationPayload);
        return false;
    }

    //Add returnValue to true
    toastNotificationPayload.put("returnValue", true);
    toastPayload = pbnjson::JGenerator::serialize(toastNotificationPayload, pbnjson::JSchemaFragment("{}"));

    if(!LSSubscriptionPost(getHandle(), get_category(), "getToastNotification", toastPayload.c_str(), &lserror) && lserror.message)
    {
        errorText = lserror.message;
        return false;
    }

    return true;
}

bool NotificationService::postToastCountNotification(pbnjson::JValue toastCountPayload, bool staleMsg, bool persistentMsg, std::string &errorText)
{
    LSErrorSafe lserror;
    std::string countPayload;

    //Add returnValue to true
    toastCountPayload.put("returnValue", true);
    countPayload = pbnjson::JGenerator::serialize(toastCountPayload, pbnjson::JSchemaFragment("{}"));

    if(!LSSubscriptionPost(getHandle(), get_category(), "getToastCount", countPayload.c_str(), &lserror) && lserror.message)
    {
        errorText = lserror.message;
        return false;
    }

    return true;
}

bool NotificationService::postAlertNotification(pbnjson::JValue alertNotificationPayload, std::string &errorText)
{
	LSErrorSafe lserror;
	std::string alertPayload;

	//In Factory Mode, disable alert notifications
    if(BLOCK_ALERT_NOTIFICATION) {
        LOG_DEBUG("Alert is blocked in factory mode");
        return false;
    }

    if(!UI_ENABLED)
    {
        //save the message in the queue.
        alertMsgQueue.push(alertNotificationPayload);
        return false;
    }

	//Add returnValue to true
	alertNotificationPayload.put("returnValue", true);

	alertPayload = pbnjson::JGenerator::serialize(alertNotificationPayload, pbnjson::JSchemaFragment("{}"));

    if(!LSSubscriptionPost(getHandle(), get_category(), "getAlertNotification", alertPayload.c_str(), &lserror) && lserror.message)
    {
        errorText = lserror.message;
        return false;
    }

    return true;
}

//->Start of API documentation comment block
/**
@page com_webos_notification com.webos.notification
@{
@section com_webos_notification_closeToast closeToast

Removes toast from Notification Center

@par Parameters
Name | Required | Type | Description
-----|----------|------|------------
toastId | No  | String | It should be the same id that was received when creating toast. Either toastId or sourceId is required.
sourceId | No  | String | It should be the same id that was received when creating toast. Either toastId or sourceId is required.

@par Returns(Call)
Name | Required | Type | Description
-----|----------|------|------------
returnValue | yes | Boolean | True

@par Returns(Subscription)
None

@}
*/
//->End of API documentation comment block

bool NotificationService::cb_closeToast(LSHandle* lshandle, LSMessage *msg, void *user_data)
{
    LSErrorSafe lserror;

    bool success = false;

    std::string toastId;
    std::string sourceId;
    std::string errText;
    std::string timestamp;

    pbnjson::JValue request;
    JUtil::Error error;

    request = JUtil::parse(LSMessageGetPayload(msg), "closeToast", &error);

    if(request.isNull())
    {
        LOG_WARNING(MSGID_CLT_PARSE_FAIL, 0, "Parsing Error in %s", __PRETTY_FUNCTION__ );
        errText = "Message is not parsed";
        goto Done;
    }

    toastId = request["toastId"].asString();
    sourceId = request["sourceId"].asString();
    if(toastId.empty() && sourceId.empty())
    {
        LOG_WARNING(MSGID_CLT_TOASTID_MISSING, 0, "Both Toast ID and Source ID are missing in %s", __PRETTY_FUNCTION__);
        errText = "Both Toast Id and Source Id can't be Empty";
        goto Done;
    }

    if (!toastId.empty())
    {
        timestamp = Utils::extractTimestampFromId(toastId);
        if(timestamp.empty())
        {
            LOG_WARNING(MSGID_CLT_TOASTID_PARSE_FAIL, 0, "Unable to extract timestamp from toastId in %s", __PRETTY_FUNCTION__);
            errText = "Toast Id parse error";
            goto Done;
        }

        History::instance()->deleteMessage("timestamp", timestamp);
    }
    else if (!sourceId.empty())
    {
        const char* caller = LSMessageGetApplicationID(msg);
        if(!caller)
        {
            caller = LSMessageGetSenderServiceName(msg);
            if(!caller)
            {
                LOG_WARNING(MSGID_CALLERID_MISSING, 1,
                    PMLOGKS("API", "closeToast"), " ");
                errText = "Unknown Source";
                goto Done;
            }
        }

        bool privilegedSource = false;
        if (Settings::instance()->isPrivilegedSource(caller) || Settings::instance()->isPartOfAggregators(std::string(caller)))
            privilegedSource = true;

        if (!privilegedSource)
        {
            LOG_WARNING(MSGID_PERMISSION_DENY, 1,
                PMLOGKS("API", "closeToast"), " ");
            errText = "Permission Denied";
            goto Done;
        }

        History::instance()->deleteMessage("sourceId", sourceId);
    }

    success = true;

Done:
    pbnjson::JValue json = pbnjson::Object();
    json.put("returnValue", success);

    if(!success)
    {
        json.put("errorText", errText);
    }

    std::string result = pbnjson::JGenerator::serialize(json, pbnjson::JSchemaFragment("{}"));
    if(!LSMessageReply( lshandle, msg, result.c_str(), &lserror))
    {
        return false;
    }

    return true;
}

//->Start of API documentation comment block
/**
@page com_webos_notification com.webos.notification
@{
@section com_webos_notification_closeAlert closeAlert

Closes the alert that is being displayed

@par Parameters
Name | Required | Type | Description
-----|----------|------|------------
alertId | yes  | String | It should be the same id that was received when creating alert

@par Returns(Call)
Name | Required | Type | Description
-----|----------|------|------------
returnValue | yes | Boolean | True

@par Returns(Subscription)
None

@}
*/
//->End of API documentation comment block

bool NotificationService::cb_closeAlert(LSHandle* lshandle, LSMessage *msg, void *user_data)
{
	LSErrorSafe lserror;

	bool success = false;

	std::string alertId;
	std::string errText;
	std::string timestamp;
	std::string sourceId;

	pbnjson::JValue request;
	pbnjson::JValue postAlertMessage;
	pbnjson::JValue alertInfo;

	JUtil::Error error;

	request = JUtil::parse(LSMessageGetPayload(msg), "closeAlert", &error);

	if(request.isNull())
	{
		LOG_WARNING(MSGID_CLA_PARSE_FAIL, 0, "Parsing Error in %s", __PRETTY_FUNCTION__ );
		errText = "Message is not parsed";
		goto Done;
	}

	alertId = request["alertId"].asString();
	if(alertId.length() == 0)
	{
		LOG_WARNING(MSGID_CLA_ALERTID_MISSING, 0, "Alert ID is missing in %s", __PRETTY_FUNCTION__);
		errText = "Alert Id can't be Empty";
		goto Done;
	}

	timestamp = Utils::extractTimestampFromId(alertId);
	if(timestamp.empty())
	{
		LOG_WARNING(MSGID_CLA_ALERTID_PARSE_FAIL, 0, "Unable to extract timestamp from alertId in %s", __PRETTY_FUNCTION__);
		errText = "Alert Id parse error";
		goto Done;
	}

	postAlertMessage = pbnjson::Object();
	alertInfo = pbnjson::Object();

	alertInfo.put("timestamp", timestamp);
	postAlertMessage.put("alertAction", "close");
	postAlertMessage.put("alertInfo", alertInfo);

	//Post the message
	success = NotificationService::instance()->postAlertNotification(postAlertMessage, errText);

Done:
	pbnjson::JValue json = pbnjson::Object();
	json.put("returnValue", success);

	if(!success)
	{
		json.put("errorText", errText);
	}

	std::string result = pbnjson::JGenerator::serialize(json, pbnjson::JSchemaFragment("{}"));
	if(!LSMessageReply( lshandle, msg, result.c_str(), &lserror))
	{
		return false;
	}

	return true;
}

//->Start of API documentation comment block
/**
@page com_webos_notification com.webos.notification
@{
@section com_webos_notification_closeAllAlerts closeAllAlerts

Closes all alerts that are being displayed

@par Returns(Call)
Name | Required | Type | Description
-----|----------|------|------------
returnValue | yes | Boolean | True

@par Returns(Subscription)
None

@}
*/
//->End of API documentation comment block

bool NotificationService::cb_closeAllAlerts(LSHandle* lshandle, LSMessage *msg, void *user_data)
{
	LSErrorSafe lserror;

	bool success = false;

	std::string sourceId;
	std::string errText;

	pbnjson::JValue request;
	pbnjson::JValue postAlertMessage;

	JUtil::Error error;

	const char* caller = LSMessageGetApplicationID(msg);

	if(!caller)
	{
		caller = LSMessageGetSenderServiceName(msg);
		if(!caller)
		{
			LOG_WARNING(MSGID_CALLERID_MISSING, 1,
				PMLOGKS("API", "closeAllAlerts"), " ");
			errText = "Unknown Source";
			goto Done;
		}
	}

	request = JUtil::parse(LSMessageGetPayload(msg), "closeAllAlerts", &error);
	sourceId = caller;

	if(request.isNull())
	{
		LOG_WARNING(MSGID_CLA_PARSE_FAIL, 0, "Parsing Error in %s", __PRETTY_FUNCTION__ );
		errText = "Message is not parsed";
		goto Done;
	}

	if(sourceId != "com.webos.service.battest")
	{
		LOG_WARNING(MSGID_CT_SOURCEID_INVALID, 0, "Source ID is invalid in %s", __PRETTY_FUNCTION__);
		errText = "Invalid source id specified";
		goto Done;
	}

	postAlertMessage = pbnjson::Object();
	postAlertMessage.put("alertAction", "closeAll");

	//Post the message
	success = NotificationService::instance()->postAlertNotification(postAlertMessage, errText);

Done:
	pbnjson::JValue json = pbnjson::Object();
	json.put("returnValue", success);

	if(!success)
	{
		json.put("errorText", errText);
	}

	std::string result = pbnjson::JGenerator::serialize(json, pbnjson::JSchemaFragment("{}"));
	if(!LSMessageReply( lshandle, msg, result.c_str(), &lserror))
	{
		return false;
	}

	return true;
}

void NotificationService::postNotification(pbnjson::JValue notificationPayload, bool remove, bool removeAll)
{
    LSErrorSafe lserror;
    std::string notiPayload;

    //In Factory Mode, disable alert notifications
    if(BLOCK_ALERT_NOTIFICATION) {
        LOG_DEBUG("Noti is blocked in factory mode");
        return;
    }

    //Add returnValue to true
    notificationPayload.put("returnValue", true);

    notiPayload = JUtil::jsonToString(notificationPayload);
    LOG_WARNING(MSGID_NOTIFICATIONMGR, 0, "[%s:%d] ==== postAlertNotification alertPayload ==== %s", __FUNCTION__, __LINE__, notiPayload.c_str());
    LOG_DEBUG("==== postNotification notiPayload ==== %s", notiPayload.c_str());

    LOG_DEBUG("postNotification UI_ENABLED = %d", UI_ENABLED);

    if(!UI_ENABLED)
    {
        //save the message in the queue.
        pushNotiMsgQueue(notificationPayload, remove, removeAll);
        return;
    }
    //Save the message
    if(!remove && !removeAll)
    {
        History::instance()->saveMessage(notificationPayload);
    }
    //Remove the message
    else if(remove && !removeAll)
    {
        History::instance()->deleteNotiMessage(notificationPayload);
    }
    //Remove all message
    else
    {
        LOG_DEBUG("==== postNotification remove user notifications ====");
        int displayId = notificationPayload["displayId"].asNumber<int>();
        History::instance()->resetUserNotifications(displayId);
    }

    if(!LSSubscriptionPost(getHandle(), get_category(), "getNotification", notiPayload.c_str(), &lserror))
        return;
}

bool NotificationService::cb_removeNotification(LSHandle* lshandle, LSMessage *msg, void *user_data)
{
    LSErrorSafe lserror;

    bool success = false;

    std::string errText;
    std::string timestamp;
    std::string removeNotiBySourceId;

    pbnjson::JValue request;
    pbnjson::JValue postRemoveNotiMessage = pbnjson::Object();
    pbnjson::JValue notiIdArray;
    pbnjson::JValue removeNotiInfo = pbnjson::Array();

    JUtil::Error error;

    std::string caller = LSUtils::getCallerId(msg);
    LOG_WARNING(MSGID_NOTIFICATIONMGR, 0, "[%s:%d] Caller: %s", __FUNCTION__, __LINE__, caller.c_str());

    request = JUtil::parse(LSMessageGetPayload(msg), "removeNotification", &error);

    if(request.isNull())
    {
        LOG_WARNING(MSGID_CLA_PARSE_FAIL, 0, "Parsing Error in %s", __PRETTY_FUNCTION__ );
        errText = "Message is not parsed.";
        goto Done;
    }

    if(request["sourceId"].isNull() && request["removeNotiId"].isNull())
    {
        LOG_WARNING(MSGID_CLA_PARSE_FAIL, 0, "Parsing Error in %s", __PRETTY_FUNCTION__ );
        errText = "input sourceId or removeNotiId parameter";
        goto Done;
    }

    if(!request["sourceId"].isNull())
    {
        removeNotiBySourceId = request["sourceId"].asString();
        if(removeNotiBySourceId.length() == 0)
        {
            LOG_WARNING(MSGID_CT_MSG_EMPTY, 0, "Empty sourceId is given in %s", __PRETTY_FUNCTION__);
            errText = "sourceId can't be empty";
            goto Done;
        }
        else
        {
            postRemoveNotiMessage.put("sourceId", removeNotiBySourceId);
        }
    }

    if(!request["removeNotiId"].isNull())
    {
        notiIdArray = request["removeNotiId"];

        if(notiIdArray.isArray())
        {
            if(notiIdArray.arraySize() == 0)
            {
                LOG_WARNING(MSGID_CLA_ALERTID_MISSING, 0, "Noti ID is missing in %s", __PRETTY_FUNCTION__);
                errText = "Noti Id can't be Empty";
                goto Done;
            }
            for(ssize_t index = 0; index < notiIdArray.arraySize() ; ++index)
            {
                if(!notiIdArray[index].isNull())
                {
                    timestamp = Utils::extractTimestampFromId(notiIdArray[index].asString());

                    if(timestamp.empty())
                    {
                        LOG_WARNING(MSGID_CLA_PARSE_FAIL, 0, "Parsing Error in %s", __PRETTY_FUNCTION__ );
                        errText = "notiId parse error";
                        goto Done;
                    }
                    LOG_DEBUG("timestamp = %s", timestamp.c_str());
                    LOG_DEBUG("notiIdArray = %d, %s", index, notiIdArray[index].asString().c_str());
                    removeNotiInfo.put(index, notiIdArray[index]);
                }
                else
                {
                    LOG_WARNING(MSGID_CLA_PARSE_FAIL, 0, "Parsing Error in %s", __PRETTY_FUNCTION__ );
                    errText = "notiId can't be null";
                    goto Done;
                }
            }
            postRemoveNotiMessage.put("removeNotiId", removeNotiInfo);
        }
    }

    //Post the message
    NotificationService::instance()->postNotification(postRemoveNotiMessage, true, false);
    success = true;

Done:
    pbnjson::JValue json = pbnjson::Object();
    json.put("returnValue", success);

    if(notiIdArray.arraySize() != 0)
        json.put("removeNotiId", removeNotiInfo);
    if(removeNotiBySourceId.length() != 0)
        json.put("sourceId", removeNotiBySourceId);

    if(!success)
    {
        json.put("errorText", errText);
    }

    std::string result = JUtil::jsonToString(json);
    if(!LSMessageReply( lshandle, msg, result.c_str(), &lserror))
    {
        return false;
    }

    return true;
}

bool NotificationService::cb_removeAllNotification(LSHandle* lshandle, LSMessage *msg, void *user_data)
{
    LSErrorSafe lserror;

    bool success = false;

    std::string errText;
    std::string checkCaller;

    pbnjson::JValue postRemoveAllNotiMessage = pbnjson::Object();

    JUtil::Error error;

    pbnjson::JValue request;

    int displayId = 0;
    std::string errorText = "";
    pbnjson::JValue setCountParams = pbnjson::Object();
    std::string displayCategory;
    pbnjson::JValue countKeyObj = pbnjson::Object();

    std::string caller = LSUtils::getCallerId(msg);
    if(caller.empty())
    {
        errText = "Unknown Source";
        goto Done;
    }
    LOG_WARNING(MSGID_NOTIFICATIONMGR, 0, "[%s:%d] Caller: %s", __FUNCTION__, __LINE__, caller.c_str());

    // Check for Caller Id
    checkCaller = Utils::extractSourceIdFromCaller(caller);
    LOG_DEBUG("cb_removeAllNotification Caller = %s, %zu", checkCaller.c_str(), std::string(checkCaller).find(PRIVILEGED_SYSTEM_UI_SOURCE));
    if (std::string(checkCaller).find(PRIVILEGED_SYSTEM_UI_SOURCE) == std::string::npos)
    {
        LOG_WARNING(MSGID_CA_PERMISSION_DENY, 0, "Caller is neither privileged source nor part of aggregators in %s", __PRETTY_FUNCTION__);
        success = false;
        errText = "Permission Denied";
        goto Done;
    }

    request = JUtil::parse(LSMessageGetPayload(msg), "removeAllNotification", &error);
    if (request.hasKey("displayId"))
    {
        displayId = request["displayId"].asNumber<int>();
        LOG_DEBUG("Display ID: %d", displayId);
    }
    LOG_DEBUG("Remove Payload: %s", JUtil::jsonToString(request).c_str());

    postRemoveAllNotiMessage.put("removeAllNotiId", true);
    postRemoveAllNotiMessage.put("displayId", displayId);

    //Post the message
    NotificationService::instance()->postNotification(postRemoveAllNotiMessage, false, true);
    success = true;
    if (displayId >= 0) {
        toastCountVector[displayId].readCount = 0;
        toastCountVector[displayId].unreadCount = 0;
    }

Done:
    pbnjson::JValue json = pbnjson::Object();
    json.put("returnValue", success);
    json.put("removeAllNotiId", success);
    json.put("displayId", displayId);

    if(!success)
    {
        json.put("errorText", errText);
    }

    std::string result = JUtil::jsonToString(json);
    if(!LSMessageReply( lshandle, msg, result.c_str(), &lserror))
    {
        return false;
    }

    return true;
}

bool NotificationService::cb_getToastList(LSHandle* lshandle, LSMessage *msg, void *user_data)
{
    LSErrorSafe lserror;

    bool success = false;

    std::string sourceId;
    std::string errText;
    std::string timestamp;

    bool all = false;
    bool privilegedSource = false;

    int displayId;

    pbnjson::JValue request;
    pbnjson::JValue postToastInfoMessage;

    JUtil::Error error;

    History* getReq = NULL;

    std::string caller = LSUtils::getCallerId(msg);
    if(caller.empty())
    {
        errText = "Unknown Source";
        goto Done;
    }
    LOG_WARNING(MSGID_NOTIFICATIONMGR, 0, "[%s:%d] Caller: %s", __FUNCTION__, __LINE__, caller.c_str());

    request = JUtil::parse(LSMessageGetPayload(msg), "getToastList", &error);

    if(request.isNull())
    {
        LOG_WARNING(MSGID_CLA_PARSE_FAIL, 0, "Parsing Error in %s", __PRETTY_FUNCTION__ );
        errText = "Message is not parsed";
        goto Done;
    }

    postToastInfoMessage = pbnjson::Object();

    getReq = new History();

    displayId = request["displayId"].asNumber<int>();
    postToastInfoMessage.put("displayId", displayId);

    if(Settings::instance()->isPrivilegedSource(caller))
    {
        privilegedSource = true;
    }

    if(!privilegedSource)
    {
        LOG_WARNING(MSGID_PERMISSION_DENY, 0, "Permission Denied in %s", __PRETTY_FUNCTION__);
        errText = "Permission Denied";
        goto Done;
    }

    postToastInfoMessage.put("sourceId", sourceId);

    if(getReq)
    {
        success = getReq->selectToastMessage(lshandle, sourceId, msg);
        if (!success)
        {
            errText = "can't get the notification info from db";
        }
    }
Done:
    pbnjson::JValue json = pbnjson::Object();
    json.put("returnValue", success);

    if(!success)
    {
        json.put("errorText", errText);

        std::string result = JUtil::jsonToString(json);
        if(!LSMessageReply( lshandle, msg, result.c_str(), &lserror))
        {
            return false;
        }
    }

    return true;
}

//->Start of API documentation comment block
/**
@page com_webos_notification com.webos.notification
@{
@section com_webos_notification_enable enable

Global blocker is released. But some notifications can be still blocked
by system status.

@par Parameters
Name   | Required | Type   | Description
-------|----------|--------|------------

@par Returns(Call)
Name        | Required | Type    | Description
------------|----------|---------|------------
returnValue | yes      | Boolean | True

@par Returns(Subscription)
None
@}
*/
//->End of API documentation comment block

bool NotificationService::cb_enable(LSHandle* lshandle, LSMessage *msg, void *user_data)
{
    bool success = false;
    std::string errText;

    const char* caller = LSMessageGetApplicationID(msg);
    if(!caller)
    {
        caller = LSMessageGetSenderServiceName(msg);
        if(!caller)
        {
            LOG_WARNING(MSGID_CALLERID_MISSING, 1,
                PMLOGKS("API", "enable"), " ");
            errText = "Unknown Source";
            goto Done;
        }
    }

    if(!Settings::instance()->isPrivilegedSource(caller) && !Settings::instance()->isPartOfAggregators(std::string(caller)))
    {
        LOG_WARNING(MSGID_PERMISSION_DENY, 1,
            PMLOGKS("API", "enable"), " ");
        success = false;
        errText = "Permission Denied";
        goto Done;
    }

    UiStatus::instance().enable(UiStatus::ENABLE_EXTERNAL, caller);

    LOG_INFO(MSGID_NOTIFY_ENABLE, 1,
        PMLOGKS("CALLER", caller), " ");
    success = true;

Done:
    pbnjson::JValue json = pbnjson::Object();
    json.put("returnValue", success);
    if(!success)
        json.put("errorText", errText);

    std::string result = pbnjson::JGenerator::serialize(json, pbnjson::JSchemaFragment("{}"));
    if(!LSMessageReply( lshandle, msg, result.c_str(), NULL))
    {
        return false;
    }
    return true;
}

//->Start of API documentation comment block
/**
@page com_webos_notification com.webos.notification
@{
@section com_webos_notification_disable disable

Block any notifications

@par Parameters
Name   | Required | Type   | Description
-------|----------|--------|------------

@par Returns(Call)
Name        | Required | Type    | Description
------------|----------|---------|------------
returnValue | yes      | Boolean | True

@par Returns(Subscription)
None
@}
*/
//->End of API documentation comment block

bool NotificationService::cb_disable(LSHandle* lshandle, LSMessage *msg, void *user_data)
{
    bool success = false;
    std::string errText;

    const char* caller = LSMessageGetApplicationID(msg);
    if(!caller)
    {
        caller = LSMessageGetSenderServiceName(msg);
        if(!caller)
        {
            LOG_WARNING(MSGID_CALLERID_MISSING, 1,
                PMLOGKS("API", "disable"), " ");
            errText = "Unknown Source";
            goto Done;
        }
    }

    if(!Settings::instance()->isPrivilegedSource(caller) && !Settings::instance()->isPartOfAggregators(std::string(caller)))
    {
        LOG_WARNING(MSGID_PERMISSION_DENY, 1,
            PMLOGKS("API", "disable"), " ");
        success = false;
        errText = "Permission Denied";
        goto Done;
    }

    UiStatus::instance().disable(UiStatus::ENABLE_EXTERNAL, caller);

    LOG_INFO(MSGID_NOTIFY_DISABLE, 1,
        PMLOGKS("CALLER", caller), " ");
    success = true;

Done:
    pbnjson::JValue json = pbnjson::Object();
    json.put("returnValue", success);
    if (!success)
        json.put("errorText", errText);

    std::string result = pbnjson::JGenerator::serialize(json, pbnjson::JSchemaFragment("{}"));
    if(!LSMessageReply( lshandle, msg, result.c_str(), NULL))
    {
        return false;
    }
    return true;
}

void NotificationService::processAlertMsgQueue()
{
    while(!alertMsgQueue.empty())
    {
        std::string errText;
        NotificationService::instance()->postAlertNotification(alertMsgQueue.front(), errText);
        alertMsgQueue.pop();
    }
}

void NotificationService::processNotiMsgQueue()
{
    LOG_WARNING("notificationmgr", 0, "[%s:%d] %s", __FUNCTION__, __LINE__, notiMsgQueue.empty()? "yes" : "no");
    while(!notiMsgQueue.empty())
    {
        NotificationService::instance()->postNotification(notiMsgQueue.front()->getPayLoad(),notiMsgQueue.front()->getRemove(),notiMsgQueue.front()->getRemoveAll());
        pbnjson::JValue notificationPayload = notiMsgQueue.front()->getPayLoad();
        LOG_WARNING("notificationmgr", 0, "[%s:%d] notiMsgQueue = %s", __FUNCTION__, __LINE__, JUtil::jsonToString(notificationPayload).c_str());
        popNotiMsgQueue();
    }
}

void NotificationService::processToastMsgQueue()
{
	std::string errText;
    LOG_DEBUG("processToastMsgQueue processToastMsgQueue.empty() = %s", toastMsgQueue.empty()? "yes" : "no");
    while(!toastMsgQueue.empty())
    {
        NotificationService::instance()->postToastNotification(toastMsgQueue.front(), false, false, errText);
        pbnjson::JValue toastPayload = toastMsgQueue.front();
        LOG_DEBUG("processToastMsgQueue toastMsgQueue = %s", JUtil::jsonToString(toastPayload).c_str());
        toastMsgQueue.pop();
    }
}

void NotificationService::onAlertStatus(bool enabled)
{
    LOG_DEBUG("onAlertStatus : %d", enabled);
    if (enabled)
    {
        processAlertMsgQueue();
    }
}

void NotificationService::setUIEnabled(bool enabled)
{
    UI_ENABLED = enabled;
}

NotificationService::NotiMsgItem::NotiMsgItem(pbnjson::JValue payload, bool remove, bool removeAll) {
    this->payload = payload;
    this->remove = remove;
    this->removeAll = removeAll;
};

//Parsing XML
bool NotificationService::parseDoc(const char *docname)
{
    // Set the global C and C++ locale to the user-configured locale,
    // so we can use std::cout with UTF-8, via Glib::ustring, without exceptions.
    std::locale::global(std::locale(""));

    std::string filepath(docname);

    try
    {
        Schedule::schedule_parsing = true;
        Canvas::canvas_parsing = false;
        LOG_DEBUG("Creating Schedule Parse object");
        MySaxParser::level=0;
        MySaxParser parser;
        parser.set_substitute_entities(true);
        parser.parse_file(filepath);
    }

    catch(const xmlpp::exception& ex)
    {
        LOG_ERROR(MSGID_XML_PARSING_ERROR, 1, PMLOGKS("libxml++ exception:", ex.what()),"");
        return false;
    }

    std::string xmlpath;
    std::string canvasPath;
    /*
    //Keeping this code, if in future we have to pasre the XML file to get "CanvasPath"
    std::string path = Schedule::instance()->Period["CanvasPath"].asString();
    int found = path.find_last_of("/");

    if( found == (path.length()-1))
        xmlpath =  Schedule::instance()->Period["CanvasPath"].asString() + Schedule::instance()->CanvasName;
    else
        xmlpath =  Schedule::instance()->Period["CanvasPath"].asString() + "/" + Schedule::instance()->CanvasName;
    */

    int lastOccurrence = filepath.find_last_of("/");

    if (lastOccurrence > -1)
        canvasPath = filepath.substr(0, lastOccurrence);
    else
    {
        LOG_ERROR(MSGID_XML_PARSING_ERROR, 1 ,PMLOGKS("Wrong Input Param:", "Canvas path is not proper"),"");
        return false;
    }

    xmlpath =  canvasPath + "/" + Schedule::instance()->CanvasName;

    LOG_INFO(MSGID_XML_CANVAS_PATH, 1, PMLOGKS("PATH",xmlpath.c_str()),"");
    try
    {
        Schedule::schedule_parsing = false;
        Canvas::canvas_parsing = true;
        LOG_DEBUG("Creating Canvas Parse object");
        MySaxParser::level=0;
        MySaxParser parser;
        parser.set_substitute_entities(true);
        parser.parse_file(xmlpath);
    }

    catch(const xmlpp::exception& ex)
    {
        LOG_ERROR(MSGID_XML_PARSING_ERROR, 1 ,PMLOGKS("libxml++ exception:", ex.what()),"");
        return false;
    }
    return true;
}
bool NotificationService::cb_setToastStatus(LSHandle *lshandle, LSMessage *msg, void *user_data)
{
    bool success = false;

    pbnjson::JValue json = pbnjson::Object();
    std::string errText = "";
    LSErrorSafe lserror;

    json = JUtil::parse(LSMessageGetPayload(msg), "", nullptr);

    std::string toastId = json["toastId"].asString();
    bool status = json["readStatus"].asBool();
    int displayId = json["displayId"].asNumber<int>();

    if (!json.hasKey("toastId"))
    {
        LOG_DEBUG("Missing toastId parameter");
        json.put("errorText", "Missing toastId parameter");
        json.put("returnValue", false);
        goto Done;
    }
    else if(json.hasKey("toastId") && !json["toastId"].isString())
    {
        LOG_DEBUG("toastId should be a string");
        json.put("errorText", "toastId should be a string");
        json.put("returnValue", false);
        goto Done;
    }
    else
    {
        toastId = json["toastId"].asString();
        if(toastId.find("com.palm.",0) == std::string::npos && toastId.find("com.webos.", 0) == std::string::npos && toastId.find("com.lge.",0) == std::string::npos)
        {
            LOG_DEBUG("Invalid toastId");
            json.put("errorText", "Invalid toastId");
            json.put("returnValue", false);
            goto Done;
        }
    }

    if (!json.hasKey("readStatus"))
    {
        LOG_DEBUG("Missing readStatus parameter");
        json.put("errorText", "Missing readStatus parameter");
        json.put("returnValue", false);
        goto Done;
    }
    else if(json.hasKey("readStatus") && !json["readStatus"].isBoolean())
    {
        LOG_DEBUG("readStatus should be a boolean value");
        json.put("errorText", "readStatus should be a boolean value");
        json.put("returnValue", false);
        goto Done;
    }
    else
    {
        status = json["readStatus"].asBool();
    }

    success = History::instance()->setReadStatus(toastId, status);

    if(!success)
    {
        json.put("errorText", "Failed to set status");
        json.put("returnValue", false);
    }
    else
    {
        json.put("returnValue", true);
        if (displayId >= 0)
        {
            if (status)
            {
                toastCountVector[displayId].readCount++;
                if (toastCountVector[displayId].unreadCount > 0)
                    toastCountVector[displayId].unreadCount--;
            }
            else
            {
                toastCountVector[displayId].unreadCount++;
                if (toastCountVector[displayId].readCount > 0)
                    toastCountVector[displayId].readCount--;
            }
        }
    }

Done:
    std::string result = pbnjson::JGenerator::serialize(json, pbnjson::JSchemaFragment("{}"));
    if(!LSMessageReply( lshandle, msg, result.c_str(), &lserror))
    {
        return false;
    }

    return true;
}
