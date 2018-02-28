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
#include "PowerStatus.h"
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
#define PRIVILEGED_CLOUDLINK_SOURCE "com.lge.service.cloudlink"
#define ALERTAPP "com.webos.app.commercial.alert"

static NotificationService* s_instance = 0;


//LS2 Functions
static LSMethod s_methods[] =
{
    { "createToast", NotificationService::cb_createToast},
    { "createAlert", NotificationService::cb_createAlert},
    { "closeToast", NotificationService::cb_closeToast},
    { "closeAlert", NotificationService::cb_closeAlert},
    { "createPincodePrompt", NotificationService::cb_createPincodePrompt},
    { "createNotification", NotificationService::cb_createNotification},
    { "removeNotification", NotificationService::cb_removeNotification},
    { "createInputAlert", NotificationService::cb_createInputAlert},
    { "closeInputAlert", NotificationService::cb_closeInputAlert},
    { "createSignageAlert", NotificationService::cb_createSignageAlert},
    { "getToastNotification", NotificationService::cb_getNotification},
    { "getAlertNotification", NotificationService::cb_getNotification},
    { "getInputAlertNotification", NotificationService::cb_getNotification},
    { "getPincodePromptNotification", NotificationService::cb_getNotification},
    { "closePincodePrompt", NotificationService::cb_closePincodePrompt},
    { "enableToast", NotificationService::cb_enableToast},
    { "disableToast", NotificationService::cb_disableToast},  
    { "closeAllAlerts", NotificationService::cb_closeAllAlerts},
    { "enable", NotificationService::cb_enable},
    { "disable", NotificationService::cb_disable},
    { "getNotification", NotificationService::cb_getNotification},
    { "getNotificationInfo", NotificationService::cb_getNotificationInfo},
    { "removeAllNotification", NotificationService::cb_removeAllNotification},
    {0, 0}
};

using namespace std::placeholders;

NotificationService::NotificationService()
    : m_pincode_message(NULL),UI_ENABLED(false), BLOCK_ALERT_NOTIFICATION(false), BLOCK_TOAST_NOTIFICATION(false)
{
	m_service = 0;
    m_connAlertStatus = UiStatus::instance().alert().sigStatus.connect(
        std::bind(&NotificationService::onAlertStatus, this, _1)
    );
    m_connPincodePromptStatus = UiStatus::instance().prompt().sigStatus.connect(
        std::bind(&NotificationService::onPincodePromptStatus, this, _1)
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

	SystemTime::instance().startSync(); //BreadnutMergeTODO:Not needed for wearable
	PowerStatus::instance().startMonitor();
	History::instance(); // for connect SystemTime sync signal //BreadnutMergeTODO:Not needed for wearable

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

const char* NotificationService::getCaller(LSMessage *msg, const char* defaultName)
{
    const char* caller = LSMessageGetApplicationID(msg);

    if(!caller)
    {
        caller = LSMessageGetSenderServiceName(msg);
        if(!caller)
        {
            LOG_WARNING(MSGID_CA_CALLERID_MISSING, 0, "Caller ID is missing in %s", __PRETTY_FUNCTION__);
            caller = defaultName;
        }
    }

    return caller;
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

	const char* caller = NULL;
    caller = NotificationService::instance()->getCaller(msg, "Anonymous");

    // Check for Caller Id
    checkCaller = Utils::extractSourceIdFromCaller(caller);
    LOG_DEBUG("cb_getNotification Caller = %s", checkCaller.c_str());

    if (std::string(checkCaller).find(PRIVILEGED_SYSTEM_UI_SOURCE) != std::string::npos)
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
            Utils::async([=] { UiStatus::instance().enable(UiStatus::ENABLE_UI); }); //BreadnutMergeTODO:Not needed for wearable
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

bool NotificationService::cb_SubscriptionCanceled(LSHandle *lshandle, LSMessage *msg, void *user_data)
{
	const char *val = LSMessageGetMethod(msg);
	std::string method = val ? val : std::string("");

	val = LSMessageGetKind(msg);
	std::string kind = val ? val : std::string("");
	unsigned int subscribers = LSSubscriptionGetHandleSubscribersCount(lshandle, kind.c_str());

	LOG_DEBUG("cb_SubscriptionCanceled: %s, subscribers:%u", method.c_str(), subscribers);

	if (method == "getToastNotification" ||
		method == "getAlertNotification" ||
		method == "getInputAlertNotification" ||
		method == "getPincodePromptNotification")
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

	const char* caller = NULL;
    caller = NotificationService::instance()->getCaller(msg, NULL);

    if(!caller)
    {
        LOG_WARNING(MSGID_CT_CALLERID_MISSING, 0, "Caller ID is missing in %s", __PRETTY_FUNCTION__);
        errText = "Unknown Source";
        goto Done;
    }
    LOG_WARNING(MSGID_NOTIFICATIONMGR, 0, "[%s:%d] Caller: %s", __FUNCTION__, __LINE__, caller);



	request = JUtil::parse(LSMessageGetPayload(msg), "createToast", &error);

	if(request.isNull())
	{
		LOG_WARNING(MSGID_CT_PARSE_FAIL, 0, "Message parsing error in %s", __PRETTY_FUNCTION__ );
		errText = "Message is not parsed";
		goto Done;
	}

	if(Settings::instance()->isPrivilegedSource(caller) || Settings::instance()->isPartOfAggregators(std::string(caller)))
	{
		privilegedSource = true;
	}

	sourceId = request["sourceId"].asString();

    if(sourceId.length() == 0)
    {
        sourceId = Utils::extractSourceIdFromCaller(caller);
    }

	// SourceId and Caller should match for non-privileged apps
	if(!privilegedSource)
	{
		if(std::string(caller).find(sourceId, 0) == std::string::npos)
		{
		    LOG_WARNING(MSGID_CT_SOURCEID_INVALID, 0, "Source ID is invalid in %s", __PRETTY_FUNCTION__);
			errText = "Invalid source id specified";
			goto Done;
		}
	}

	message = request["message"].asString();
	if(message.length() == 0)
	{
		LOG_WARNING(MSGID_CT_MSG_EMPTY, 0, "Empty message is given in %s", __PRETTY_FUNCTION__);
		errText = "Message can't be empty";
		goto Done;
	}

	title = request["title"].asString();

        ignoreDisable = request["ignoreDisable"].asBool();

	if (!ignoreDisable && !UiStatus::instance().toast().isEnabled(UiStatus::ENABLE_ALL & ~UiStatus::ENABLE_UI))
	{
		errText = "Toast is blocked by " + UiStatus::instance().toast().reason();
		goto Done;
	}

	postCreateToast = pbnjson::Object();
	postCreateToast.put("sourceId", sourceId);
	action = pbnjson::Object();

	if(Settings::instance()->isPrivilegedSource(caller))
	{
		iconPath = request["iconUrl"].asString();
	}
	else
	{
		iconPath = AppList::instance()->getIcon(sourceId);
	}

	if(iconPath.length() != 0 && Utils::verifyFileExist(iconPath.c_str()))
	{
		postCreateToast.put("iconUrl", "file://"+iconPath);
		postCreateToast.put("iconPath", iconPath);
	}
	else
	{
		postCreateToast.put("iconUrl", "file://"+ Settings::instance()->getDefaultIcon("toast"));
		postCreateToast.put("iconPath", Settings::instance()->getDefaultIcon("toast"));
	}

	//Remove if there is any space character except ' '
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

	if (!staleMsg && !UiStatus::instance().toast().isEnabled(UiStatus::ENABLE_UI))
	{
		errText = "UI is not yet ready";
		goto Done;
	}

	if(request["onlyToast"].isNull())
    {
        postCreateToast.put("onlyToast", true);
    }
    else
    {
        postCreateToast.put("onlyToast", request["onlyToast"].asBool());
    }

    if(request["isSysReq"].isNull())
    {
        postCreateToast.put("isSysReq", isSysReq);
    }
    else
    {
        postCreateToast.put("isSysReq", request["isSysReq"].asBool());
    }


    if(request["isCradleReq"].isNull())
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
			for(size_t i = 0; i < arraySize; ++i)
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
	if(noaction)
	{
		postCreateToast.put("action", action);
		success = NotificationService::instance()->postToastNotification(postCreateToast, staleMsg, persistentMsg, errText);
		goto Done;
	}

	appMgrParams = pbnjson::Object();
	onclick = request["onclick"];

	if(onclick.isNull()) //launch the app that creates the toast.
	{
		//Check the SourceId exist in the App list.
		if(AppList::instance()->isAppExist(sourceId))
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
		if(launchAppId.length() != 0)
		{//check for valid App
			appMgrParams.put("id", launchAppId);
			if(!onclick["params"].isNull())
				appMgrParams.put("params", onclick["params"]);
		}
		else if(target.length() != 0)
		{
			appMgrParams.put("target", target);
		}
		else
		{
			//Check the SourceId exist in the App list.
			if(AppList::instance()->isAppExist(sourceId))
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

	//Post a message
	success = NotificationService::instance()->postToastNotification(postCreateToast, staleMsg, persistentMsg, errText);

Done:
	pbnjson::JValue json = pbnjson::Object();
	json.put("returnValue", success);

	if(!success)
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
	if(!LSMessageReply( lshandle, msg, result.c_str(), &lserror))
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
	if(!LSCall(NotificationService::instance()->getHandle(), "palm://com.palm.bus/isCallAllowed", params.c_str(), cb_createAlertIsAllowed, data, NULL, &lserror))
	{
		delete data;
		return alertRespondWithError(message, sourceId, alertId, alertTitle, alertMessage, std::string("Call failed - ") + lserror.message);
	}

	return true;
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
	std::string clickSchemaString; // to take care of onclick (for TV) and onClick(for wearable)field in schema 

    std::string alertType;
    std::string buttonIconPath;
    std::string buttonType;
    int checkConfirmType;
    int checkWarningType;
    int checkBattertType;
    int checkOkType;
    int checkCancelType;
    bool ignoreDisable = false;

	unsigned found = 0;
	JUtil::Error error;

	std::vector<std::string> uriList;

	const char* caller = NULL;
    caller = NotificationService::instance()->getCaller(msg, NULL);

	if(!caller)
	{
		caller = LSMessageGetSenderServiceName(msg);
		if(!caller)
		{
		        LOG_WARNING(MSGID_CALLERID_MISSING, 1,
				PMLOGKS("API", "createAlert"), " ");
			return alertRespondWithError(msg, sourceId, alertId, "", "", "Unknown Source");
		}
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

	if (!ignoreDisable && !UiStatus::instance().alert().isEnabled(UiStatus::ENABLE_ALL & ~UiStatus::ENABLE_UI))
	{
		return alertRespondWithError(msg, sourceId, alertId, title, message, "Alert is blocked by " + UiStatus::instance().alert().reason());
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
	data->serviceNameCreateAlert = NotificationService::instance()->getServiceName(msg);
	data->postCreateAlert = postCreateAlert;

	std::string params = "{\"uri\": \"" + uri + "\", \"requester\": \"" + data->serviceNameCreateAlert + "\"}";
	if(!LSCall(NotificationService::instance()->getHandle(), "palm://com.palm.bus/isCallAllowed", params.c_str(), cb_createAlertIsAllowed, data, NULL, &lserror))
	{
		delete data;
		return alertRespondWithError(msg, sourceId, alertId, title, message, std::string("Call failed - ") + lserror.message);
	}


	return true;
}

//->Start of API documentation comment block
/**
@page com_webos_notification com.webos.notification
@{
@section com_webos_notification_createInputAlert createInputAlert

Creates system alert notification

@par Parameters
Name       | Required |  Type   | Description
-----------|----------|---------|------------
appId      | yes      | String  | Input Application ID
portType   | yes      | String  | Input port type
portName   | yes      | String  | Input port name
portIcon   | yes      | String  | Input port icon file name
deviceName | no       | String  | Connected device name
deviceIcon | no       | String  | Connected device icon file name
timeout    | no       | number  | Time (in sec) when alert will close automatically
params     | no       | object  | Parameters to be passed to the application
inputs     | no       | array   | Input object array

@par Returns(Call)
Name         | Required | Type    | Description
-------------|----------|---------|------------
returnValue  | yes      | Boolean | True
inputAlertId | yes      | String  | This would be appId + "-" + Timestamp.

@par Returns(Subscription)
None

@}
*/
//->End of API documentation comment block

bool NotificationService::cb_createInputAlert(LSHandle* lshandle, LSMessage *msg, void *user_data)
{
    LSErrorSafe lserror;
    std::string errText;
    pbnjson::JValue request;
    bool success = false;
    pbnjson::JValue postCreateAlert;
    pbnjson::JValue alertInfo;

    std::string sourceId;
    std::string portName; // for log
    std::string timestamp;
    JUtil::Error error;

    const char* caller = LSMessageGetApplicationID(msg);
    if(!caller) {
        caller = LSMessageGetSenderServiceName(msg);
        if(!caller) {
            LOG_WARNING(MSGID_CALLERID_MISSING, 1,
                PMLOGKS("API", "createInputAlert"), " ");
            errText = "Unknown Source";
            goto Done;
        }
    }
    sourceId = std::string(caller);

    //Check for Caller Id
    if(!Settings::instance()->isPrivilegedSource(caller) && !Settings::instance()->isPartOfAggregators(std::string(caller))) {
        LOG_WARNING(MSGID_PERMISSION_DENY, 1,
            PMLOGKS("API", "createInputAlert"), " ");
        success = false;
        errText = "Permission Denied";
        goto Done;
    }

    request = JUtil::parse(LSMessageGetPayload(msg), "createInputAlert", &error);

    if(request.isNull()) {
        LOG_WARNING(MSGID_CIA_PARSE_FAIL, 0, "Message parsing error in %s", __PRETTY_FUNCTION__ );
        errText = "Message is not parsed";
        goto Done;
    }

    if (!UiStatus::instance().input().isEnabled())
    {
        errText = "Input alert is blocked by " + UiStatus::instance().input().reason();
        goto Done;
    }

    alertInfo = pbnjson::Object();
    postCreateAlert = pbnjson::Object();

    if (request.hasKey("inputs"))
    {
        pbnjson::JValue reqInputs = request["inputs"];
        pbnjson::JValue inputs = pbnjson::Array();

        size_t arraySize = reqInputs.arraySize();
        if (arraySize == 0) {
            LOG_WARNING(MSGID_CIA_INPUTS_EMPTY, 0, " ");
            errText = "inputs can't be empty";
            goto Done;
        }

        for(size_t i = 0;i < arraySize;++i)
        {
            pbnjson::JValue input = reqInputs[i];
            pbnjson::JValue info = JsonParser::createInputAlertInfo(input, errText);
            if (info.isNull())
                goto Done;
            inputs.append(info);

            if (portName.empty())
                portName = info["portName"].asString();
            else
                portName += std::string(",") + info["portName"].asString();
        }

        alertInfo.put("inputs", inputs);
    }
    else
    {
        alertInfo = JsonParser::createInputAlertInfo(request, errText);
        if (alertInfo.isNull())
            goto Done;

        portName = alertInfo["portName"].asString();
    }

    //Check for timeout property
    int timeout;
    timeout = 10;
    if(!request["timeout"].isNull()) {
        timeout = request["timeout"].asNumber<int>();
        timeout = (request["timeout"].asNumber<int>() < timeout) ? timeout : request["timeout"].asNumber<int>();
    }
    alertInfo.put("timeout", timeout);

    postCreateAlert.put("alertAction", "open");
    postCreateAlert.put("alertInfo", alertInfo);

    Utils::createTimestamp(timestamp);
    postCreateAlert.put("timestamp", timestamp);

    //Post the message
    success = NotificationService::instance()->postInputAlertNotification(postCreateAlert, errText);

Done:
    pbnjson::JValue json = pbnjson::Object();
    json.put("returnValue", success);

    if(!success)
    {
        json.put("errorText", errText);

        LOG_WARNING(MSGID_NOTIFY_INVOKE_FAILED, 4,
            PMLOGKS("SOURCE_ID", sourceId.c_str()),
            PMLOGKS("TYPE", "INPUTALERT"),
            PMLOGKS("ERROR", errText.c_str()),
            PMLOGKS("CONTENT", portName.c_str()),
            " ");
    }
    else
    {
        json.put("alertId", (sourceId + "-" + timestamp));

        LOG_INFO(MSGID_NOTIFY_INVOKE, 3,
            PMLOGKS("SOURCE_ID", sourceId.c_str()),
            PMLOGKS("TYPE", "INPUTALERT"),
            PMLOGKS("CONTENT", portName.c_str()),
            " ");
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
@section com_webos_notification_closePincodePrompt

closePincodePrompt notification

@par Parameters
Name        | Required | Type   | Description
-------------|----------|----------|------------
pincode     | no     |  string |
closeType   | yes      | string |


@par Returns(Call)
Name                 | Required  | Type     | Description
----------------------|-------------|-----------|------------
returnValue 	        | yes        | Boolean | True


@par Returns(Subscription)
None

@}
*/
//->End of API documentation comment block

bool NotificationService::cb_closePincodePrompt(LSHandle* lshandle, LSMessage *msg, void *user_data)
{
	std::string pincode = "";
	std::string sourceId = "";
	std::string closeType = "";
	std::string mode = "";
	std::string errText = "";
	bool success = false;
	bool needPost = false;
	LSErrorSafe lserror;
	JUtil::Error error;
	pbnjson::JValue reply;
	LSMessage *promptMsg = NULL;
	const char* caller = LSMessageGetApplicationID(msg);

	if(!caller)
	{
		caller = LSMessageGetSenderServiceName(msg);
		if(!caller)
		{
			LOG_WARNING(MSGID_CALLERID_MISSING, 1,
				PMLOGKS("API", "closePincodePrompt"), " ");
			errText = "Unknown Source";
			goto Done;
		}
	}

	//check for Caller Prompt
	if (NotificationService::instance()->getPincode_message(&promptMsg) == false)
	{
		LOG_WARNING(MSGID_PERMISSION_DENY, 1,
			PMLOGKS("API", "closePincodePrompt"), " ");
		errText = "Permission Denied (Pincode prompt is not active)";
		goto Done;
	}

	reply = JUtil::parse(LSMessageGetPayload(msg), "closePincodePrompt", &error);
	LOG_DEBUG("cb_closePincodePrompt reply = %s", JUtil::jsonToString(reply).c_str());

	if (reply.isNull())
	{
		LOG_WARNING(MSGID_RP_PARSE_FAIL, 0, "Message parsing error in %s", __PRETTY_FUNCTION__ );
		errText = "Message is not parsed";
		goto Done;
	}

	sourceId = std::string(caller);
	pincode = reply["pincode"].asString();
	closeType = reply["closeType"].asString();

	if(!reply["mode"].isNull())
	{
		mode = reply["mode"].asString();
		LOG_DEBUG("pincodePrompt sourceId: %s, close mode: %s, pincode = %s",
                   sourceId.c_str(), mode.c_str(), pincode.c_str());
	}
	success = true;

Done:
    pbnjson::JValue jsonNumber = pbnjson::Object();
    pbnjson::JValue postCreatePincodePrompt = pbnjson::Object();
    pbnjson::JValue pincodePromptInfo = pbnjson::Object();
    pbnjson::JValue jsonPrompt = pbnjson::Object();
    jsonNumber.put("returnValue", success);
    if(!success)
        jsonNumber.put("errorText", errText);

    std::string resultNumber = pbnjson::JGenerator::serialize(jsonNumber, pbnjson::JSchemaFragment("{}"));
    if(!LSMessageReply( lshandle, msg, resultNumber.c_str(), &lserror))
    {
        return false;
    }

    if (!success)
    {
        LOG_WARNING(MSGID_NOTIFY_CLOSE_FAILED, 4,
            PMLOGKS("SOURCE_ID", sourceId.c_str()),
            PMLOGKS("TYPE", "PINCODE"),
            PMLOGKS("ERROR", errText.c_str()),
            PMLOGKS("CONTENT", closeType.c_str()),
            " ");
        return true;
    }
    else
    {
        LOG_WARNING(MSGID_NOTIFY_CLOSE, 3,
            PMLOGKS("SOURCE_ID", sourceId.c_str()),
            PMLOGKS("TYPE", "PINCODE"),
            PMLOGKS("CONTENT", closeType.c_str()),
            " ");
    }

	postCreatePincodePrompt.put("pincodePromptAction", "close");
	pincodePromptInfo.put("timestamp", NotificationService::instance()->m_pincode_timestamp);
	postCreatePincodePrompt.put("pincodePromptInfo", pincodePromptInfo);
	LOG_DEBUG("cb_createPincodePrompt postCreatePincodePrompt = %s", JUtil::jsonToString(postCreatePincodePrompt).c_str());
	NotificationService::instance()->postPincodePromptNotification(postCreatePincodePrompt);
	LOG_DEBUG("pincodePrompt close Type: %s", closeType.c_str());

	if (closeType != "relay")
	{
		jsonPrompt.put("matched", false);
	}
	else if (closeType == "relay")
	{
		if (mode == "")
		{
			PincodeValidator pincodeValidator(Settings::instance()->m_system_pincode);
			if (pincodeValidator.check(pincode))
			{
				jsonPrompt.put("matched", true);
			}
			else
			{
				LOG_WARNING(MSGID_RP_PINCODE_INVALID, 0, "Invalid pincode in %s", __PRETTY_FUNCTION__);
				jsonPrompt.put("matched", false);
				pincodePromptInfo.put("retry", true);
				pincodePromptInfo.put("promptType", "parental");
				needPost = true;
			}
		}
		else if (mode == "set_match")
		{
			PincodeValidator pincodeValidator(Settings::instance()->m_system_pincode);
			if (pincodeValidator.check(pincode))
			{
				pincodePromptInfo.put("promptType", "set_newpin");
			}
			else
			{
				LOG_WARNING(MSGID_RP_PINCODE_INVALID, 0, "Invalid pincode in %s", __PRETTY_FUNCTION__);
				pincodePromptInfo.put("retry", true);
				pincodePromptInfo.put("promptType", "set_match");
			}
			needPost = true;
		}
		else if (mode == "set_newpin")
		{
			if (pincode.length() == 0 || NotificationService::instance()->checkUnacceptablePincode(pincode))
			{
				LOG_WARNING(MSGID_RP_PINCODE_INVALID, 0, "Invalid pincode in %s", __PRETTY_FUNCTION__);
				pincodePromptInfo.put("retry", true);
				pincodePromptInfo.put("promptType", "set_newpin");
			}
			else
			{
				//set tmp password
				NotificationService::instance()->m_tmp_pincode = pincode;
				pincodePromptInfo.put("promptType", "set_verify");
			}
			needPost = true;
		}
		else if (mode == "set_verify")
		{
			if (pincode.length() != 0 && pincode == NotificationService::instance()->m_tmp_pincode)
			{
				bool result = false;
				LSErrorSafe lserrorSetting;
				std::string setSystemPinRequest;
				//set pincode
				setSystemPinRequest  = "{";
				setSystemPinRequest += "\"settings\":{\"systemPin\":\"";
				setSystemPinRequest += NotificationService::instance()->m_tmp_pincode;
				setSystemPinRequest += "\"}}";
				result = SETTING_API_CALL(NotificationService::instance()->getHandle(),
										"luna://com.webos.settingsservice/setSystemSettings",
										setSystemPinRequest.c_str(),
										NotificationService::cb_setSystemSetting, msg, NULL, &lserrorSetting);
				if (!result)
				{
					LOG_WARNING(MSGID_RP_PINCODE_SETFAIL, 1, PMLOGKS("REASON", lserrorSetting.message), " ");
				}

				//initalPincode
				setSystemPinRequest  = "{";
				setSystemPinRequest += "\"category\":\"lock\", \"settings\":{\"initialPinCode\":true}";
				setSystemPinRequest += "}";
				result = SETTING_API_CALL(NotificationService::instance()->getHandle(),
										"luna://com.webos.settingsservice/setSystemSettings",
										setSystemPinRequest.c_str(),
										NotificationService::cb_setSystemSetting, msg, NULL, &lserrorSetting);
				if (!result)
				{
					LOG_WARNING(MSGID_RP_PINCODE_SETFAIL, 1, PMLOGKS("REASON", lserrorSetting.message), " ");
				}

				NotificationService::instance()->m_tmp_pincode = "";
			}
			else
			{
				pincodePromptInfo.put("retry", true);
				pincodePromptInfo.put("promptType", "set_verify");
				needPost = true;
			}
		}
	}

	if (needPost)
	{
		postCreatePincodePrompt.put("pincodePromptAction", "open");
		Utils::createTimestamp(NotificationService::instance()->m_pincode_timestamp);
		postCreatePincodePrompt.put("timestamp", NotificationService::instance()->m_pincode_timestamp);
		postCreatePincodePrompt.put("pincodePromptInfo", pincodePromptInfo);
		NotificationService::instance()->postPincodePromptNotification(postCreatePincodePrompt);

		return true;
	}

	if (promptMsg)
	{
		jsonPrompt.put("returnValue", success);
		std::string resultPrompt = pbnjson::JGenerator::serialize(jsonPrompt, pbnjson::JSchemaFragment("{}"));

		LOG_DEBUG("cb_closePincodePrompt resultPrompt = %s", resultPrompt.c_str());

		if(!LSMessageReply( lshandle, promptMsg, resultPrompt.c_str(), &lserror))
		{
			LOG_WARNING(MSGID_RP_REPLY_FAIL, 0, "Reply for createPincodePrompt failed in %s", __PRETTY_FUNCTION__);
		}

		LSMessageUnref(promptMsg);
		NotificationService::instance()->resetPincode_message();
	}
	else
	{
		LOG_WARNING(MSGID_RP_PINCODE_INVALID, 0, "Invalid promptMsg!!! in %s", __PRETTY_FUNCTION__);
	}

	return true;
}

//->Start of API documentation comment block
/**
@page com_webos_notification com.webos.notification
@{
@section com_webos_notification_createPincodePrompt

Creates PincodePrompt  notification

@par Parameters
Name        | Required | Type   | Description
------------|----------|--------|------------
type        | yes	   | String | type of the dialog
appId       | no       | String | app Id
title       | no 	   | String | show title of the dialog
messsage    | no 	   | String | show message of the dialog

@par Returns(Call)
Name                 | Required  | Type     | Description
---------------------|-----------|----------|------------
returnValue   	     | yes 		 | Boolean  | True
matched 	 	 	 | yes   	 | String   | result of the match

@par Returns(Subscription)
None

@}
*/
//->End of API documentation comment block

bool NotificationService::cb_createPincodePrompt(LSHandle* lshandle, LSMessage *msg, void *user_data)
{
	std::string errText;
	std::string type;
	std::string message;
	std::string title;
	std::string appId;
	std::string sourceId;
	bool success = false;
	LSErrorSafe lserror;
	JUtil::Error error;
	pbnjson::JValue request;
	pbnjson::JValue postCreatePincodePrompt;
	pbnjson::JValue pincodePromptInfo;
	LSMessage *prev_msg = NULL;
        bool ignoreDisable = false;

	const char* caller = LSMessageGetApplicationID(msg);

	if(!caller)
	{
		caller = LSMessageGetSenderServiceName(msg);
		if(!caller)
		{
			LOG_WARNING(MSGID_CALLERID_MISSING, 1,
				PMLOGKS("API", "createPincodePrompt"), " ");
			errText = "Unknown Source";
			goto Done;
		}
	}

	//Check for Caller Id
	if(!Settings::instance()->isPrivilegedSource(caller) && !Settings::instance()->isPartOfAggregators(std::string(caller)))
	{
		LOG_WARNING(MSGID_PERMISSION_DENY, 1,
			PMLOGKS("API", "createPincodePrompt"), " ");
		success = false;
		errText = "Permission Denied";
		goto Done;
	}

	request = JUtil::parse(LSMessageGetPayload(msg), "createPincodePrompt", &error);

	if(request.isNull())
	{
		LOG_WARNING(MSGID_CP_PARSE_FAIL, 0, "Message parsing error in %s", __PRETTY_FUNCTION__ );
		errText = "Message is not parsed";
		goto Done;
	}

	sourceId = std::string(caller);

	type = request["promptType"].asString();
	if(type.length() == 0)
	{
		LOG_WARNING(MSGID_CP_PROMPT_TYPE_INVALID, 0, "promptType is empty in %s", __PRETTY_FUNCTION__);
		errText = "promptType is empty";
		goto Done;
	}
	else if((type != "parental" && type != "set_match" ))
	{
		LOG_WARNING(MSGID_CP_PROMPT_TYPE_INVALID, 0, "Invalid promptType %s", __PRETTY_FUNCTION__);
		errText = "Invalid promptType";
		goto Done;
	}
	else if((type == "parental" || type == "set_match" ) && NotificationService::instance()->getPincode_message(&prev_msg))
	{
		//Check for active pincode prompt
		LOG_WARNING(MSGID_CP_PROMPT_ACTIVE, 0, "Pincode prompt is active in %s", __PRETTY_FUNCTION__);
		errText = "Pincode prompt is active";
		goto Done;
	}

        ignoreDisable = request["ignoreDisable"].asBool();

	if (!ignoreDisable && !UiStatus::instance().prompt().isEnabled())
	{
		errText = "Pincode prompt is blocked by " + UiStatus::instance().prompt().reason();
		goto Done;
	}

	pincodePromptInfo = pbnjson::Object();
	postCreatePincodePrompt = pbnjson::Object();

	if(!request["title"].isNull())
	{
		title = request["title"].asString();
		std::replace_if(title.begin(), title.end(), Utils::isEscapeChar, ' ');
		pincodePromptInfo.put("title", title);
	}

	if(!request["message"].isNull())
	{
		message = request["message"].asString();
		std::replace_if(message.begin(), message.end(), Utils::isEscapeChar, ' ');
		pincodePromptInfo.put("message", message);
	}

	if(!request["appId"].isNull())
	{
		appId = request["appId"].asString();
		//Remove if there is any space character except ' '
		std::replace_if(appId.begin(), appId.end(), Utils::isEscapeChar, ' ');
		pincodePromptInfo.put("appId", appId);
	}

	//add sourceId
	pincodePromptInfo.put("sourceId", sourceId);
	LOG_DEBUG("pincode Prompt sourceId = %s", sourceId.c_str());

	postCreatePincodePrompt.put("pincodePromptAction", "open");
	Utils::createTimestamp(NotificationService::instance()->m_pincode_timestamp);
	postCreatePincodePrompt.put("timestamp", NotificationService::instance()->m_pincode_timestamp);
	pincodePromptInfo.put("promptType", type.c_str());
	pincodePromptInfo.put("retry", false);
	pincodePromptInfo.put("keys", appId);
	postCreatePincodePrompt.put("pincodePromptInfo", pincodePromptInfo);

	//Post the message
	NotificationService::instance()->postPincodePromptNotification(postCreatePincodePrompt);//open dialog
	success = true;


Done:
    pbnjson::JValue json = pbnjson::Object();
    json.put("returnValue", success);

    if(!success)
    {
        json.put("errorText", errText);

        LOG_WARNING(MSGID_NOTIFY_INVOKE_FAILED, 4,
            PMLOGKS("SOURCE_ID", sourceId.c_str()),
            PMLOGKS("TYPE", "PINCODE"),
            PMLOGKS("ERROR", errText.c_str()),
            PMLOGKS("CONTENT", type.c_str()),
            " ");

        std::string result = pbnjson::JGenerator::serialize(json, pbnjson::JSchemaFragment("{}"));
        if (!LSMessageReply(lshandle, msg, result.c_str(), &lserror))
        {
            return false;
        }
    }
    else
    {
        LOG_WARNING(MSGID_NOTIFY_INVOKE, 3,
            PMLOGKS("SOURCE_ID", sourceId.c_str()),
            PMLOGKS("TYPE", "PINCODE"),
            PMLOGKS("CONTENT", type.c_str()),
            " ");

        NotificationService::instance()->setPincode_message(msg);//store
        LSMessageRef(msg);
    }

    return true;
}

bool NotificationService::cb_setSystemSetting(LSHandle* lshandle, LSMessage *msg, void *user_data)
{
	pbnjson::JValue list = JUtil::parse(LSMessageGetPayload(msg), std::string(""));
	if (list.isNull())
	{
		LOG_WARNING(MSGID_CA_MSG_EMPTY, 0, "cb_setSystemSetting Message is missing in %s", __PRETTY_FUNCTION__);
		return false;
	}
	return true;
}

bool NotificationService::cb_createNotification(LSHandle* lshandle, LSMessage *msg, void *user_data)
{
    LSErrorSafe lserror;
    std::string errText;
    pbnjson::JValue request;
    bool success = false;
    pbnjson::JValue postCreateNoti;
 //   pbnjson::JValue createNotiInfo;

    bool autoRemove = false;
    bool forceLcdTurnOn= true;
    bool needSoundPlay = false;
    bool forceSoundPlay = false;
    bool isRawSound = true;
    bool needToShowPopup = true;
    bool isRemoteNotification = false;
    bool isUnDeletable = false;
    bool saveRemoteNotification = false;

    std::string sourceId;
    std::string message;
    std::string title;
    std::string timestamp;
    std::string iconPath;
    std::string onClick;
    std::string soundUri;

    JUtil::Error error;

    const char* caller = NULL;
    caller = NotificationService::instance()->getCaller(msg, NULL);

    if(!caller)
    {
        errText = "Unknown Source";
        goto Done;
    }
    LOG_WARNING(MSGID_NOTIFICATIONMGR, 0, "[%s:%d] Caller: %s", __FUNCTION__, __LINE__, caller);

    request = JUtil::parse(LSMessageGetPayload(msg), "createNotification", &error);

    if(request.isNull())
    {
        LOG_WARNING(MSGID_CA_PARSE_FAIL, 0, "Message parsing error in %s", __PRETTY_FUNCTION__ );
        errText = "Message is not parsed.";
        goto Done;
    }

    //createNotiInfo = pbnjson::Object();
    postCreateNoti = pbnjson::Object();

    sourceId = request["sourceId"].asString();
    if(sourceId.length() == 0)
    {
        sourceId = Utils::extractSourceIdFromCaller(caller);
        postCreateNoti.put("sourceId", sourceId);
    }
    else
    {
        postCreateNoti.put("sourceId", sourceId);
    }

    message = request["message"].asString();
    if(message.length() == 0)
    {
        LOG_WARNING(MSGID_CA_MSG_EMPTY, 0, "Empty message is given in %s", __PRETTY_FUNCTION__);
        errText = "Message can't be empty";
        goto Done;
    }
    else
    {
        //Copy the message
        //Remove if there is any space character except ' '
        std::replace_if(message.begin(), message.end(), Utils::isEscapeChar, ' ');
        postCreateNoti.put("message", message);
    }

    //Check the icon and copy it.
    if(!request["iconUrl"].isNull())
    {
        iconPath = request["iconUrl"].asString();
        if(iconPath.length() != 0 && Utils::verifyFileExist(iconPath.c_str()))
        {
            postCreateNoti.put("iconUrl", "file://"+iconPath);
        }
        else
        {
            postCreateNoti.put("iconUrl", "file://"+ Settings::instance()->getDefaultIcon("alert"));
        }
    }

    if(!request["title"].isNull())
    {
        title = request["title"].asString();
        std::replace_if(title.begin(), title.end(), Utils::isEscapeChar, ' ');
    }
    else
    {
        title = "";
    }
    postCreateNoti.put("title", title);

    if(!request["autoRemove"].isNull())
    {
        autoRemove = request["autoRemove"].asBool();
        postCreateNoti.put("autoRemove", autoRemove);
    }
    else
    {
        postCreateNoti.put("autoRemove", false);
    }

    if(!request["onClick"].isNull())
    {
        onClick = request["onClick"].asString();
        if(!Utils::isValidURI(onClick))
        {
            LOG_WARNING(MSGID_CA_SERVICEURI_INVALID, 0, "Invalid ServiceURI is given in %s", __PRETTY_FUNCTION__);
            errText = "Invalid Service Uri in the onclick";
            goto Done;
        }

        if(onClick.length() != 0)
        {
            postCreateNoti.put("onClick", onClick);
        }
    }

    if(!request["params"].isNull())
    {
         postCreateNoti.put("params", request["params"]);
    }

    if(!request["forceLcdTurnOn"].isNull())
    {
        forceLcdTurnOn = request["forceLcdTurnOn"].asBool();
        postCreateNoti.put("forceLcdTurnOn", forceLcdTurnOn);
    }
    else
    {
        postCreateNoti.put("forceLcdTurnOn", forceLcdTurnOn);
    }

    if(!request["needSoundPlay"].isNull())
    {
        needSoundPlay = request["needSoundPlay"].asBool();
        postCreateNoti.put("needSoundPlay", needSoundPlay);
    }
    else
    {
        postCreateNoti.put("needSoundPlay", needSoundPlay);
    }

    if(!request["forceSoundPlay"].isNull())
    {
        forceSoundPlay = request["forceSoundPlay"].asBool();
        postCreateNoti.put("forceSoundPlay", forceSoundPlay);
    }
    else
    {
        postCreateNoti.put("forceSoundPlay", forceSoundPlay);
    }

    if(!request["soundUri"].isNull())
    {
        soundUri = request["soundUri"].asString();

        if(soundUri.length() != 0 && Utils::verifyFileExist(soundUri.c_str()))
        {
            postCreateNoti.put("soundUri", "file://"+soundUri);
        }
        else if(soundUri.length() != 0 && !Utils::verifyFileExist(soundUri.c_str()))
        {
            LOG_WARNING(MSGID_CLT_SOUNDURI_MISSING, 0, "File does not exist on the local file system in %s", __PRETTY_FUNCTION__);
            errText = "File does not exist on the local file system";
            goto Done;
        }
        else
        {
            LOG_WARNING(MSGID_CA_SERVICEURI_INVALID, 0, "Invalid SoundURI is given in %s", __PRETTY_FUNCTION__);
            errText = "Invalid Sound Uri";
            goto Done;
        }
    }

    if(!request["isRawSound"].isNull())
    {
        isRawSound = request["isRawSound"].asBool();
        postCreateNoti.put("isRawSound", isRawSound);
    }
    else
    {
        postCreateNoti.put("isRawSound", isRawSound);
    }

    if(!request["needToShowPopup"].isNull())
    {
        needToShowPopup = request["needToShowPopup"].asBool();
        postCreateNoti.put("needToShowPopup", needToShowPopup);
    }
    else
    {
        postCreateNoti.put("needToShowPopup", needToShowPopup);
    }

    if(!request["isRemoteNotification"].isNull())
    {
        isRemoteNotification = request["isRemoteNotification"].asBool();
        postCreateNoti.put("isRemoteNotification", isRemoteNotification);
    }
    else
    {
        postCreateNoti.put("isRemoteNotification", isRemoteNotification);
    }

    if(!request["isUnDeletable"].isNull())
    {
        isUnDeletable = request["isUnDeletable"].asBool();
        postCreateNoti.put("isUnDeletable", isUnDeletable);
    }
    else
    {
        postCreateNoti.put("isUnDeletable", isUnDeletable);
    }

    if(request["isSysReq"].isNull())
    {
        postCreateNoti.put("isSysReq", false);
    }
    else
    {
        postCreateNoti.put("isSysReq", request["isSysReq"].asBool());
    }

    Utils::createTimestamp(timestamp);

    postCreateNoti.put("notiId", (sourceId + "-" + timestamp));
    postCreateNoti.put("timestamp", timestamp);
   // postCreateNoti.put("notiInfo", createNotiInfo);
    postCreateNoti.put("saveRemoteNotification", saveRemoteNotification);

    //Post the message
    NotificationService::instance()->postNotification(postCreateNoti, false, false);
    success = true;

Done:
    pbnjson::JValue json = pbnjson::Object();
    json.put("returnValue", success);

    if(!success)
    {
        json.put("errorText", errText);
    }
    else
    {
        json.put("notiId", (sourceId + "-" + timestamp));
    }

    std::string result = JUtil::jsonToString(json);
    if(!LSMessageReply( lshandle, msg, result.c_str(), &lserror))
    {
        return false;
    }

    return true;
}

bool NotificationService::cb_createRemoteNotification(LSHandle* lshandle, LSMessage *msg, void *user_data)
{
    LSErrorSafe lserror;
    std::string errText;

    bool success = false;

    pbnjson::JValue request;
    pbnjson::JValue postCreateRemoteNoti = pbnjson::Object();
  //  pbnjson::JValue createRemoteNotiInfo = pbnjson::Object();
    pbnjson::JValue remoteActionParam;
    pbnjson::JValue removeActionArray = pbnjson::Array();

    std::string remoteSourceId;
    std::string remotePackageName;
    std::string remoteMessage;
    std::string remoteTitle;
    std::string remoteTickerText;
    std::string timestamp;
    std::string remoteIconPath;
    std::string remoteAppName;
    std::string remoteBgImage;
    std::string checkCaller;
    std::string parentNotiId;
    std::string groupId;
    std::string notificationType;
    std::string remoteSubText;
    std::string remoteTag;
    std::string remoteResultKey;
    std::string enhancedNotification;

    int remoteId;
    int remoteUserId;
    int pageNumber;
    int remoteCount;

    double remotePostTime;

    bool isUnDeletable = false;
    bool isRemoteNotification = true;
    bool saveRemoteNotification = true;
    bool remoteAlert = false;

    JUtil::Error error;

    const char* caller = NULL;
    caller = NotificationService::instance()->getCaller(msg, NULL);

    if(!caller)
    {
        errText = "Unknown Source";
        goto Done;
    }
    LOG_WARNING(MSGID_NOTIFICATIONMGR, 0, "[%s:%d] Caller: %s", __FUNCTION__, __LINE__, caller);

    // Check for Caller Id
    checkCaller = Utils::extractSourceIdFromCaller(caller);
    LOG_DEBUG("cb_createRemoteNotification Caller = %s, %d", checkCaller.c_str(), std::string(checkCaller).compare(PRIVILEGED_SOURCE));

    if (std::string(checkCaller).compare(PRIVILEGED_SOURCE) != 0 && std::string(checkCaller).compare(PRIVILEGED_CLOUDLINK_SOURCE) != 0)
    {
        LOG_WARNING(MSGID_CA_PERMISSION_DENY, 0, "Caller is neither privileged source nor part of aggregators in %s", __PRETTY_FUNCTION__);
        success = false;
        errText = "Permission Denied";
        goto Done;
    }

    request = JUtil::parse(LSMessageGetPayload(msg), "createRemoteNotification", &error);

    if(request.isNull())
    {
        LOG_WARNING(MSGID_CA_PARSE_FAIL, 0, "Message parsing error in %s", __PRETTY_FUNCTION__ );
        errText = "Message is not parsed.";
        goto Done;
    }

    remoteSourceId = request["remoteSourceId"].asString();

    if(remoteSourceId.length() != 0)
    {
        if (std::string(remoteSourceId).compare(PRIVILEGED_SOURCE) != 0)
        {
            LOG_WARNING(MSGID_CA_PERMISSION_DENY, 0, "Caller is neither privileged source nor part of aggregators in %s", __PRETTY_FUNCTION__);
            success = false;
            errText = "remoteSourceId is possible only remotenotification";
            goto Done;
        }
    }

    remoteSourceId = checkCaller;
    postCreateRemoteNoti.put("remoteSourceId", remoteSourceId);

    parentNotiId = request["parentNotiId"].asString();

    if(parentNotiId.length() != 0)
    {
        postCreateRemoteNoti.put("parentNotiId", parentNotiId);
    }

    remoteMessage = request["remoteMessage"].asString();
    if(remoteMessage.length() == 0)
    {
        LOG_WARNING(MSGID_CA_MSG_EMPTY, 0, "Empty message is given in %s", __PRETTY_FUNCTION__);
        errText = "remoteMessage can't be empty";
        goto Done;
    }
    else
    {
        std::replace_if(remoteMessage.begin(), remoteMessage.end(), Utils::isEscapeChar, ' ');
        postCreateRemoteNoti.put("remoteMessage", remoteMessage);
    }

    remotePackageName = request["remotePackageName"].asString();
    if(remotePackageName.length() == 0)
    {
        LOG_WARNING(MSGID_CA_MSG_EMPTY, 0, "Empty remotePackageName is given in %s", __PRETTY_FUNCTION__);
        errText = "remotePackageName can't be empty";
        goto Done;
    }
    else
    {
        postCreateRemoteNoti.put("remotePackageName", remotePackageName);
    }

    if(!request["remoteTitle"].isNull())
    {
        remoteTitle = request["remoteTitle"].asString();
        std::replace_if(remoteTitle.begin(), remoteTitle.end(), Utils::isEscapeChar, ' ');
    }
    else
    {
        remoteTitle = "";
    }
    postCreateRemoteNoti.put("remoteTitle", remoteTitle);

    if(!request["remoteTickerText"].isNull())
    {
        remoteTickerText = request["remoteTickerText"].asString();
        std::replace_if(remoteTickerText.begin(), remoteTickerText.end(), Utils::isEscapeChar, ' ');
        postCreateRemoteNoti.put("remoteTickerText", remoteTickerText);
    }

    if(!request["remoteId"].isNull())
    {
        remoteId = request["remoteId"].asNumber<int32_t>();
        postCreateRemoteNoti.put("remoteId", remoteId);
    }

    if(!request["remoteUserId"].isNull())
    {
        remoteUserId = request["remoteUserId"].asNumber<int32_t>();
        postCreateRemoteNoti.put("remoteUserId", remoteUserId);
    }

    if(!request["remotePostTime"].isNull())
    {
        remotePostTime = request["remotePostTime"].asNumber<double>();
        postCreateRemoteNoti.put("remotePostTime", remotePostTime);
    }

    if(!request["remoteAppName"].isNull())
    {
        remoteAppName = request["remoteAppName"].asString();
        postCreateRemoteNoti.put("remoteAppName", remoteAppName);
    }
    else
    {
        LOG_WARNING(MSGID_CLA_ALERTID_MISSING, 0, "remoteAppName is missing in %s", __PRETTY_FUNCTION__);
        errText = "remoteAppName can't be Empty";
        goto Done;
    }

    if(!request["remoteBgImage"].isNull())
    {
        remoteBgImage = request["remoteBgImage"].asString();
        if(remoteBgImage.length() != 0 && Utils::verifyFileExist(remoteBgImage.c_str()))
        {
            postCreateRemoteNoti.put("remoteBgImage", remoteBgImage);
        }
        else
        {
            postCreateRemoteNoti.put("remoteBgImage", Settings::instance()->getDefaultIcon("alert"));
        }
    }

    if(!request["remoteIconUrl"].isNull())
    {
        remoteIconPath = request["remoteIconUrl"].asString();
        if(remoteIconPath.length() != 0 && Utils::verifyFileExist(remoteIconPath.c_str()))
        {
            postCreateRemoteNoti.put("remoteIconUrl", remoteIconPath);
        }
        else
        {
            postCreateRemoteNoti.put("remoteIconUrl", Settings::instance()->getDefaultIcon("alert"));
        }
    }

    if(!request["remoteActionParam"].isNull())
    {
        remoteActionParam = request["remoteActionParam"];

        if(remoteActionParam.isArray())
        {
            if(remoteActionParam.arraySize() == 0)
            {
                LOG_WARNING(MSGID_CLA_ALERTID_MISSING, 0, "Noti ID is missing in %s", __PRETTY_FUNCTION__);
                errText = "remoteActionParam can't be Empty";
                goto Done;
            }
            for(ssize_t index = 0; index < remoteActionParam.arraySize() ; ++index)
            {
                if(!remoteActionParam[index].isNull())
                {
                    removeActionArray.put(index, remoteActionParam[index]);
                }
                else
                {
                    LOG_WARNING(MSGID_CLA_PARSE_FAIL, 0, "Parsing Error in %s", __PRETTY_FUNCTION__ );
                    errText = "remoteActionParam can't be null";
                    goto Done;
                }
            }
            postCreateRemoteNoti.put("remoteActionParam", removeActionArray);
        }
    }

    if(!request["isUnDeletable"].isNull())
    {
        isUnDeletable = request["isUnDeletable"].asBool();
        postCreateRemoteNoti.put("isUnDeletable", isUnDeletable);
    }
    else
    {
        postCreateRemoteNoti.put("isUnDeletable", isUnDeletable);
    }

    if(!request["isRemoteNotification"].isNull())
    {
        isRemoteNotification = request["isRemoteNotification"].asBool();
        postCreateRemoteNoti.put("isRemoteNotification", isRemoteNotification);
    }
    else
    {
        postCreateRemoteNoti.put("isRemoteNotification", isRemoteNotification);
    }

    if(!request["groupId"].isNull())
    {
        groupId = request["groupId"].asString();
        if(groupId.length() != 0)
        {
            postCreateRemoteNoti.put("groupId", groupId);
        }
    }

    if(!request["notificationType"].isNull())
    {
        notificationType = request["notificationType"].asString();
        if(notificationType.length() != 0)
        {
            postCreateRemoteNoti.put("notificationType", notificationType);
        }
    }

    if(!request["pageNumber"].isNull())
    {
        pageNumber = request["pageNumber"].asNumber<int32_t>();
        postCreateRemoteNoti.put("pageNumber", pageNumber);
    }

    if(!request["remoteSubText"].isNull())
    {
        remoteSubText = request["remoteSubText"].asString();
        if(remoteSubText.length() != 0)
        {
            postCreateRemoteNoti.put("remoteSubText", remoteSubText);
        }
    }

    if(!request["remoteCount"].isNull())
    {
        remoteCount = request["remoteCount"].asNumber<int32_t>();
        postCreateRemoteNoti.put("remoteCount", remoteCount);
    }

    if(!request["remoteTag"].isNull())
    {
        remoteTag = request["remoteTag"].asString();
        if(remoteTag.length() != 0)
        {
            postCreateRemoteNoti.put("remoteTag", remoteTag);
        }
    }

    if(!request["remoteResultKey"].isNull())
    {
        remoteResultKey = request["remoteResultKey"].asString();
        postCreateRemoteNoti.put("remoteResultKey", remoteResultKey);
    }

    if(!request["remoteAlert"].isNull())
    {
        remoteAlert = request["remoteAlert"].asBool();
        postCreateRemoteNoti.put("remoteAlert", remoteAlert);
    }

    if(!request["enhancedNotification"].isNull())
    {
        enhancedNotification = request["enhancedNotification"].asString();
        postCreateRemoteNoti.put("enhancedNotification", enhancedNotification);
    }

    Utils::createTimestamp(timestamp);

    postCreateRemoteNoti.put("remoteNotiId", (remoteSourceId + "-" + timestamp));
    postCreateRemoteNoti.put("timestamp", (timestamp));
  //  postCreateRemoteNoti.put("notiInfo", createRemoteNotiInfo);
    postCreateRemoteNoti.put("saveRemoteNotification", saveRemoteNotification);

    //Post the message
    NotificationService::instance()->postNotification(postCreateRemoteNoti, false, false);
    success = true;

Done:
    pbnjson::JValue json = pbnjson::Object();
    json.put("returnValue", success);

    if(!success)
    {
        json.put("errorText", errText);
    }
    else
    {
        json.put("remoteNotiId", (remoteSourceId+ "-" + timestamp));
    }

    std::string result = JUtil::jsonToString(json);
    if(!LSMessageReply( lshandle, msg, result.c_str(), &lserror))
    {
        return false;
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

    if (staleMsg || UiStatus::instance().toast().isSilence())
    {
        std::string reason;
        if (staleMsg) reason = "stale";
        else if (UiStatus::instance().toast().isSilence()) reason = "silence";

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

    if(!LSSubscriptionPost(getHandle(), get_category(), "getToastNotification", toastPayload.c_str(), &lserror))
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

    if(!LSSubscriptionPost(getHandle(), get_category(), "getAlertNotification", alertPayload.c_str(), &lserror))
    {
        errorText = lserror.message;
        return false;
    }

    return true;
}

bool NotificationService::postInputAlertNotification(pbnjson::JValue inputAlertNotificationPayload, std::string &errorText)
{
    LSErrorSafe lserror;
    std::string alertPayload;

    //Add returnValue to true
    inputAlertNotificationPayload.put("returnValue", true);

    alertPayload = pbnjson::JGenerator::serialize(inputAlertNotificationPayload, pbnjson::JSchemaFragment("{}"));

    if(!LSSubscriptionPost(getHandle(), get_category(), "getInputAlertNotification", alertPayload.c_str(), &lserror))
    {
        errorText = lserror.message;
        return false;
    }

    return true;
}

void NotificationService::postPincodePromptNotification(pbnjson::JValue pincodePromptNotificationPayload)
{
	LSErrorSafe lserror;
	std::string pincodePromptPayload;

	//Add returnValue to true
	pincodePromptNotificationPayload.put("returnValue", true);

	pincodePromptPayload = pbnjson::JGenerator::serialize(pincodePromptNotificationPayload, pbnjson::JSchemaFragment("{}"));

	if(!LSSubscriptionPost(getHandle(), get_category(), "getPincodePromptNotification", pincodePromptPayload.c_str(), &lserror))
		return;
}

//->Start of API documentation comment block
/**
@page com_webos_notification com.webos.notification
@{
@section com_webos_notification_enableToast enableToast

Enable Toast globally or for an App

@par Parameters
Name | Required | Type | Description
-----|----------|------|------------
source | no  | String | It should be App or Service Id that creates the toast

@par Returns(Call)
Name | Required | Type | Description
-----|----------|------|------------
returnValue | yes | Boolean | True

@par Returns(Subscription)
None
@}
*/
//->End of API documentation comment block

bool NotificationService::cb_enableToast(LSHandle* lshandle, LSMessage *msg, void *user_data)
{
	LSErrorSafe lserror;

	bool success = false;

	std::string sourceId;
	std::string errText;

	pbnjson::JValue request;

	JUtil::Error error;

	request = JUtil::parse(LSMessageGetPayload(msg), "enableToast", &error);

	if(request.isNull())
	{
		LOG_WARNING(MSGID_ET_PARSE_FAIL, 0, "Parsing Error in %s", __PRETTY_FUNCTION__ );
		errText = "Message is not parsed";
		goto Done;
	}

	sourceId = request["source"].asString();
	if(sourceId.length() == 0)
	{
		//Assume this is for Global.
		success = Settings::instance()->enableToastNotification();
		goto Done;
	}

	//This is for an individual App. SourceId is same as the AppId. Check the AppId.
	if(AppList::instance()->isAppExist(sourceId))
	{
		success = Settings::instance()->enableToastNotificationForApp(sourceId);
	}
	else
	{
		//This should never happen.
		errText = "Unknown Source ID";
	}

Done:
	pbnjson::JValue json = pbnjson::Object();
	json.put("returnValue", success);

	const char* caller = LSMessageGetApplicationID(msg);
	if (!caller)
		caller = LSMessageGetSenderServiceName(msg);
	if (caller)
		sourceId = std::string(caller);

	if (success)
	{
		LOG_INFO(MSGID_NOTIFY_CLOSE, 2,
			PMLOGKS("SOURCE_ID", sourceId.c_str()),
			PMLOGKS("TYPE", "ALERT"),
			" ");
	}
        else
        {
		LOG_WARNING(MSGID_NOTIFY_INVOKE_FAILED, 3,
			PMLOGKS("SOURCE_ID", sourceId.c_str()),
			PMLOGKS("TYPE", "ALERT"),
			PMLOGKS("ERROR", errText.c_str()),
			" ");
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
@section com_webos_notification_disableToast disableToast

Disable Toast globally or for an App

@par Parameters
Name | Required | Type | Description
-----|----------|------|------------
source | no  | String | It should be App or Service Id that creates the toast

@par Returns(Call)
Name | Required | Type | Description
-----|----------|------|------------
returnValue | yes | Boolean | True

@par Returns(Subscription)
None

@}
*/
//->End of API documentation comment block

bool NotificationService::cb_disableToast(LSHandle* lshandle, LSMessage *msg, void *user_data)
{
	LSErrorSafe lserror;

	bool success = false;

	std::string sourceId;
	std::string errText;

	pbnjson::JValue request;

	JUtil::Error error;

	request = JUtil::parse(LSMessageGetPayload(msg), "enableToast", &error);

	if(request.isNull())
	{
		LOG_WARNING(MSGID_DT_PARSE_FAIL, 0, "Parsing Error in %s", __PRETTY_FUNCTION__ );
		errText = "Message is not parsed";
		goto Done;
	}

	sourceId = request["source"].asString();
	if(sourceId.length() == 0)
	{
		//Assume this is for Global.
		success = Settings::instance()->disableToastNotification();
		goto Done;
	}

	//This is for an individual App. SourceId is same as the AppId. Check the AppId.
	if(AppList::instance()->isAppExist(sourceId))
	{
		success = Settings::instance()->disableToastNotificationForApp(sourceId);
	}
	else
	{
		//This should never happen.
		errText = "Unknown Source ID";
	}

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

//->Start of API documentation comment block
/**
@page com_webos_notification com.webos.notification
@{
@section com_webos_notification_closeInputAlert closeInputAlert

Closes the alert that is being displayed

@par Parameters
Name | Required | Type | Description
-----|----------|------|------------
inputAlertId | yes  | String | It should be the same id that was received when creating alert

@par Returns(Call)
Name | Required | Type | Description
-----|----------|------|------------
returnValue | yes | Boolean | True

@par Returns(Subscription)
None

@}
*/
//->End of API documentation comment block

bool NotificationService::cb_closeInputAlert(LSHandle* lshandle, LSMessage *msg, void *user_data)
{
    LSErrorSafe lserror;

    bool success = false;

    std::string inputAlertId;
    std::string errText;
    std::string timestamp;

    pbnjson::JValue request;
    pbnjson::JValue postAlertMessage;
    pbnjson::JValue alertInfo;

    JUtil::Error error;

    request = JUtil::parse(LSMessageGetPayload(msg), "closeInputAlert", &error);

    if(request.isNull())
    {
        LOG_WARNING(MSGID_CLIA_PARSE_FAIL, 0, "Parsing Error in %s", __PRETTY_FUNCTION__ );
        errText = "Message is not parsed";
        goto Done;
    }

    inputAlertId = request["inputAlertId"].asString();
    if(inputAlertId.length() == 0)
    {
        LOG_WARNING(MSGID_CLIA_ALERTID_MISSING, 0, "Input Alert ID is missing in %s", __PRETTY_FUNCTION__);
        errText = "Input Alert Id can't be Empty";
        goto Done;
    }

    timestamp = Utils::extractTimestampFromId(inputAlertId);
    if(timestamp.empty())
    {
        LOG_WARNING(MSGID_CLIA_ALERTID_PARSE_FAIL, 0, "Unable to extract timestamp from inputAlertId in %s", __PRETTY_FUNCTION__);
        errText = "Input Alert Id parse error";
        goto Done;
    }

    postAlertMessage = pbnjson::Object();
    alertInfo = pbnjson::Object();

    alertInfo.put("timestamp", timestamp);
    postAlertMessage.put("alertAction", "close");
    postAlertMessage.put("alertInfo", alertInfo);

    //Post the message
    success = NotificationService::instance()->postInputAlertNotification(postAlertMessage, errText);

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
        LOG_DEBUG("==== postNotification removeAll ====");
        History::instance()->purgeAllData();
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

    const char* caller = NotificationService::instance()->getCaller(msg, "");
    LOG_WARNING(MSGID_NOTIFICATIONMGR, 0, "[%s:%d] Caller: %s", __FUNCTION__, __LINE__, caller);

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

bool NotificationService::cb_removeRemoteNotification(LSHandle* lshandle, LSMessage *msg, void *user_data)
{
    LSErrorSafe lserror;

    bool success = false;

    std::string errText;
    std::string timestamp;
    std::string removeRemoteNotiByPkgName;
    std::string checkCaller;

    pbnjson::JValue request;
    pbnjson::JValue postRemoveRemoteNotiMessage = pbnjson::Object();
    pbnjson::JValue remoteNotiIdArray;
    pbnjson::JValue removeRemoteNotiInfo = pbnjson::Array();

    JUtil::Error error;

    History* getReq = NULL;

    const char* caller = NULL;
    caller = NotificationService::instance()->getCaller(msg, NULL);

    if(!caller)
    {
        errText = "Unknown Source";
        goto Done;
    }
    LOG_WARNING(MSGID_NOTIFICATIONMGR, 0, "[%s:%d] Caller: %s", __FUNCTION__, __LINE__, caller);

    checkCaller = Utils::extractSourceIdFromCaller(caller);
    LOG_DEBUG("cb_removeRemoteNotification Caller = %s, %d, %d", checkCaller.c_str(), std::string(checkCaller).compare(PRIVILEGED_SOURCE), std::string(checkCaller).compare(PRIVILEGED_APP_SOURCE));

    // Check for Caller Id
    if (std::string(checkCaller).compare(PRIVILEGED_SOURCE) != 0 && std::string(checkCaller).compare(PRIVILEGED_APP_SOURCE) != 0 &&
        std::string(checkCaller).compare(PRIVILEGED_CLOUDLINK_SOURCE) != 0)
    {
        LOG_WARNING(MSGID_CA_PERMISSION_DENY, 0, "Caller is neither privileged source nor part of aggregators in %s", __PRETTY_FUNCTION__);
        success = false;
        errText = "Permission Denied";
        goto Done;
    }

    request = JUtil::parse(LSMessageGetPayload(msg), "removeRemoteNotification", &error);

    if(request.isNull())
    {
        LOG_WARNING(MSGID_CLA_PARSE_FAIL, 0, "Parsing Error in %s", __PRETTY_FUNCTION__ );
        errText = "Message is not parsed.";
        goto Done;
    }

    if(request["remotePackageName"].isNull() && request["removeRemoteNotiId"].isNull())
    {
        LOG_WARNING(MSGID_CLA_PARSE_FAIL, 0, "Parsing Error in %s", __PRETTY_FUNCTION__ );
        errText = "input remotePackageName or removeRemoteNotiId parameter";
        goto Done;
    }

    if(!request["remotePackageName"].isNull())
    {
        removeRemoteNotiByPkgName = request["remotePackageName"].asString();
        if(removeRemoteNotiByPkgName.length() == 0)
        {
            LOG_WARNING(MSGID_CT_MSG_EMPTY, 0, "Empty sourceId is given in %s", __PRETTY_FUNCTION__);
            errText = "sourceId can't be empty";
            goto Done;
        }
        else
        {
            postRemoveRemoteNotiMessage.put("remotePackageName", removeRemoteNotiByPkgName);
        }
    }

    if(!request["removeRemoteNotiId"].isNull())
    {
        remoteNotiIdArray = request["removeRemoteNotiId"];

        if(remoteNotiIdArray.isArray())
        {
            if(remoteNotiIdArray.arraySize() == 0)
            {
                LOG_WARNING(MSGID_CLA_ALERTID_MISSING, 0, "remoteNotiId is missing in %s", __PRETTY_FUNCTION__);
                errText = "remoteNotiId can't be Empty";
                goto Done;
            }
            for(ssize_t index = 0; index < remoteNotiIdArray.arraySize() ; ++index)
            {
                if(!remoteNotiIdArray[index].isNull())
                {
                    timestamp = Utils::extractTimestampFromId(remoteNotiIdArray[index].asString());

                    if(timestamp.empty())
                    {
                        LOG_WARNING(MSGID_CLA_PARSE_FAIL, 0, "Parsing Error in %s", __PRETTY_FUNCTION__ );
                        errText = "remoteNotiId parse error";
                        goto Done;
                    }
                    LOG_DEBUG("timestamp = %s", timestamp.c_str());
                    LOG_DEBUG("remoteNotiIdArray = %d, %s", index, remoteNotiIdArray[index].asString().c_str());
                    removeRemoteNotiInfo.put(index, remoteNotiIdArray[index]);
                }
                else
                {
                    LOG_WARNING(MSGID_CLA_PARSE_FAIL, 0, "Parsing Error in %s", __PRETTY_FUNCTION__ );
                    errText = "notiId can't be null";
                    goto Done;
                }
            }
            postRemoveRemoteNotiMessage.put("removeRemoteNotiId", removeRemoteNotiInfo);
        }
    }

    getReq = new History();

    if(getReq)
    {
        success = getReq->deleteRemoteNotiMessage(lshandle, postRemoveRemoteNotiMessage);
        if (!success)
        {
            errText = "can't delete the remote notification info from db";
        }
    }

Done:
    pbnjson::JValue json = pbnjson::Object();
    json.put("returnValue", success);

    if(remoteNotiIdArray.arraySize() != 0)
        json.put("removeRemoteNotiId", removeRemoteNotiInfo);
    if(removeRemoteNotiByPkgName.length() != 0)
        json.put("remotePackageName", removeRemoteNotiByPkgName);

    if(!success)
    {
        json.put("errorText", errText);
    }

    std::string result = JUtil::jsonToString(json);
    LOG_DEBUG("==== removeRemoteNotification Payload ==== %s", result.c_str());
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

    const char* caller = NULL;
    caller = NotificationService::instance()->getCaller(msg, NULL);

    if(!caller)
    {
        errText = "Unknown Source";
        goto Done;
    }
    LOG_WARNING(MSGID_NOTIFICATIONMGR, 0, "[%s:%d] Caller: %s", __FUNCTION__, __LINE__, caller);

    // Check for Caller Id
    checkCaller = Utils::extractSourceIdFromCaller(caller);
    LOG_DEBUG("cb_removeAllNotification Caller = %s, %d", checkCaller.c_str(), std::string(checkCaller).find(PRIVILEGED_SYSTEM_UI_SOURCE));
    if (std::string(checkCaller).find(PRIVILEGED_SYSTEM_UI_SOURCE) == std::string::npos)
    {
        LOG_WARNING(MSGID_CA_PERMISSION_DENY, 0, "Caller is neither privileged source nor part of aggregators in %s", __PRETTY_FUNCTION__);
        success = false;
        errText = "Permission Denied";
        goto Done;
    }

    postRemoveAllNotiMessage.put("removeAllNotiId", true);

    //Post the message
    NotificationService::instance()->postNotification(postRemoveAllNotiMessage, false, true);
    success = true;

Done:
    pbnjson::JValue json = pbnjson::Object();
    json.put("returnValue", success);
    json.put("removeAllNotiId", success);

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

bool NotificationService::cb_getNotificationInfo(LSHandle* lshandle, LSMessage *msg, void *user_data)
{
    LSErrorSafe lserror;

    bool success = false;

    std::string sourceId;
    std::string errText;
    std::string timestamp;

    bool all = false;
    bool privilegedSource = false;

    pbnjson::JValue request;
    pbnjson::JValue postNotiInfoMessage;

    JUtil::Error error;

    History* getReq = NULL;

    const char* caller = NULL;
    caller = NotificationService::instance()->getCaller(msg, NULL);

    if(!caller)
    {
        errText = "Unknown Source";
        goto Done;
    }
    LOG_WARNING(MSGID_NOTIFICATIONMGR, 0, "[%s:%d] Caller: %s", __FUNCTION__, __LINE__, caller);

    request = JUtil::parse(LSMessageGetPayload(msg), "getNotificationInfo", &error);

    if(request.isNull())
    {
        LOG_WARNING(MSGID_CLA_PARSE_FAIL, 0, "Parsing Error in %s", __PRETTY_FUNCTION__ );
        errText = "Message is not parsed";
        goto Done;
    }

    postNotiInfoMessage = pbnjson::Object();

    if(!request["all"].isNull())
    {
        getReq = new History();
        all = request["all"].asBool();
        postNotiInfoMessage.put("all", all);

        // get notification info about sourceId
        if(all == false)
        {
            sourceId = request["sourceId"].asString();
            sourceId = Utils::extractSourceIdFromCaller(sourceId);

            if(sourceId.length() == 0)
            {
                LOG_WARNING(MSGID_CA_CALLERID_MISSING, 0, "%s : invalid id specified", __PRETTY_FUNCTION__);
                errText = "Invalid source id specified";
                goto Done;
            }
            else
            {
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

                postNotiInfoMessage.put("sourceId", sourceId);

                if(getReq)
                {
                    success = getReq->selectMessage(lshandle, sourceId, msg);
                    if (!success)
                    {
                        errText = "can't get the notification info from db";
                    }
                }
            }
        }
        // get all notification info
        else
        {
            sourceId = request["sourceId"].asString();
            if(sourceId.length() != 0)
            {
                LOG_WARNING("don't input source id", 0, "%s : invalid id specified", __PRETTY_FUNCTION__);
                errText = "Do not input source id when all is true";
                goto Done;
            }

            if(getReq)
            {
                success = getReq->selectMessage(lshandle, "all", msg);
                if (!success)
                {
                    errText = "can't get the notification info from db";
                }
            }
        }
    }
    else
    {
        LOG_WARNING(MSGID_CLA_ALERTID_MISSING, 0, "all is missing in %s", __PRETTY_FUNCTION__);
        errText = "all can't be Empty";
        goto Done;
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

bool NotificationService::cb_getRemoteNotificationInfo(LSHandle* lshandle, LSMessage *msg, void *user_data)
{
    LSErrorSafe lserror;

    bool success = false;

    std::string remotePackageName;
    std::string remoteSourceId;
    std::string errText;
    std::string timestamp;
    std::string checkCaller;

    bool all = false;

    pbnjson::JValue request;
    pbnjson::JValue postNotiInfoMessage;

    JUtil::Error error;

    History* getReq = NULL;

    const char* caller = NULL;
    caller = NotificationService::instance()->getCaller(msg, NULL);

    if(!caller)
    {
        errText = "Unknown Source";
        goto Done;
    }
    LOG_WARNING(MSGID_NOTIFICATIONMGR, 0, "[%s:%d] Caller: %s", __FUNCTION__, __LINE__, caller);

    checkCaller = Utils::extractSourceIdFromCaller(caller);
    LOG_DEBUG("cb_getRemoteNotificationInfo Caller = %s, %d, %d", checkCaller.c_str(), std::string(checkCaller).compare(PRIVILEGED_SOURCE), std::string(checkCaller).compare(PRIVILEGED_APP_SOURCE));

    // Check for Caller Id
    if (std::string(checkCaller).compare(PRIVILEGED_SOURCE) != 0 && std::string(checkCaller).compare(PRIVILEGED_APP_SOURCE) != 0)
    {
        LOG_WARNING(MSGID_CA_PERMISSION_DENY, 0, "Caller is neither privileged source nor part of aggregators in %s", __PRETTY_FUNCTION__);
        success = false;
        errText = "Permission Denied";
        goto Done;
    }

    request = JUtil::parse(LSMessageGetPayload(msg), "getRemoteNotificationInfo", &error);

    if(request.isNull())
    {
        LOG_WARNING(MSGID_CLA_PARSE_FAIL, 0, "Parsing Error in %s", __PRETTY_FUNCTION__ );
        errText = "Message is not parsed";
        goto Done;
    }

    postNotiInfoMessage = pbnjson::Object();

    if(!request["all"].isNull())
    {
        getReq = new History();
        all = request["all"].asBool();
        postNotiInfoMessage.put("all", all);

        // get notification info about sourceId
        if(all == false)
        {
            remotePackageName= request["remotePackageName"].asString();
            if(remotePackageName.length() == 0)
            {
                LOG_WARNING(MSGID_CA_CALLERID_MISSING, 0, "%s : invalid remotePackageName specified", __PRETTY_FUNCTION__);
                errText = "Invalid remotePackageName specified";
                goto Done;
            }
            else
            {
                postNotiInfoMessage.put("remotePackageName", remotePackageName);

                if(getReq)
                {
                    success = getReq->selectRemoteMessage(lshandle, remotePackageName, msg);
                    if (!success)
                    {
                        errText = "can't get the remote notification info from db";
                    }
                }
            }
        }
        // get all notification info
        else
        {
             remotePackageName= request["remotePackageName"].asString();
             if(remotePackageName.length() != 0)
             {
                 LOG_WARNING("don't input remotePackageName", 0, "%s : invalid remotePackageName specified", __PRETTY_FUNCTION__);
                 errText = "Do not input remotePackageName when all is true";
                 goto Done;
             }

            if(getReq)
            {
                success = getReq->selectRemoteMessage(lshandle, "all", msg);
                if (!success)
                {
                    errText = "can't get the remote notification info from db";
                }
            }
        }
    }
    else
    {
        LOG_WARNING(MSGID_CLA_ALERTID_MISSING, 0, "all is missing in %s", __PRETTY_FUNCTION__);
        errText = "all can't be Empty";
        goto Done;
    }

    success = true;

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
@section com_webos_notification_createSignageAlert createSignageAlert

Tv Service call this api to create signage alert to display scrolling text on Signage.

@par Parameters
Name | Required | Type | Description
-----|----------|------|------------
path | yes  | String | path of the XML file

@par Returns(Call)
Name | Required | Type | Description
-----|----------|------|------------
returnValue | yes | Boolean | True

@par Returns(Subscription)
None
@}
*/
//->End of API documentation comment block

bool NotificationService::cb_createSignageAlert(LSHandle* lshandle, LSMessage *msg, void *user_data)
{
    LSErrorSafe lserror;
    pbnjson::JValue request;
    std::string path;
    std::string errorText;
    JUtil::Error error;
    request = JUtil::parse(LSMessageGetPayload(msg), "", &error);

    if(request.isNull()){
        errorText = "'path' is a required parameter";
    }
    else
    {
        path = request["path"].asString();
        if((0 == path.length()) || (0 != access(path.c_str(), F_OK))){
            LOG_WARNING(MSGID_PATH_MISSING, 0, "Unable to extract path %s", __PRETTY_FUNCTION__);
            errorText = "'path' is missing Or Invalid Path";
        }
        else{
            LOG_INFO(MSGID_XML_PATH, 1, PMLOGKS("path",path.c_str()),"");
            bool result = NotificationService::parseDoc(path.c_str());
            if(result)
            {
                pbnjson::JValue launchParams = pbnjson::Object();
                pbnjson::JValue parameters = pbnjson::Object();
                Canvas *can = Canvas::instance();
                parameters.put("Period",can->canvas);
                parameters.put("Win_Region",can->wind_Region);
                parameters.put("Text_Region",can->text_Region);
                parameters.put("Content",can->content);
                parameters.put("Line_clr",can->line_color);
                parameters.put("text_clr",can->text_color);
                parameters.put("font",can->font);
                parameters.put("bold",can->bold);
                parameters.put("Italic",can->italic);
                parameters.put("underLine",can->underline);
                parameters.put("Space",can->space);
                parameters.put("Speed",can->speed);
                parameters.put("LineView",can->line_view);
                parameters.put("Text_size",can->text_size);
                parameters.put("Effect",can->effect);
                parameters.put("Message",can->message);
                parameters.put("Thickness",can->line_thickness);
                parameters.put("HAlign",can->hAlign);
                parameters.put("VAlign",can->vAlign);
                parameters.put("LineSpace",can->lineSpacing);
                parameters.put("Repeat",can->repeat);
                parameters.put("Window_BkColor",can->win_bckGrndclr);
                parameters.put("Text_BkColor",can->text_bckGrndclr);
                parameters.put("duration", Schedule::instance()->sched["Duration"]);

                launchParams.put("id", ALERTAPP);//Alert Application
                launchParams.put("noSplash", true);
                launchParams.put("params",parameters);

                if( !LSCallOneReply( NotificationService::instance()->getHandle(),"palm://com.webos.applicationManager/launch" ,
                	JUtil::jsonToString(launchParams).c_str(), NotificationService::cb_launch, NULL, NULL, &lserror))
                {
                    LSErrorPrint (&lserror, stderr);
                    errorText = "Failed LSCall to SAM";
                }

                if (!can->message.empty())
                    can->message.clear();
            }
            else{
                errorText = "Xml parsing Failed";
            }
        }
    }

    pbnjson::JValue json = pbnjson::Object();
    if(errorText.empty()){
        json.put("returnValue", true);
    }
    else
    {
        json.put("returnValue", false);
        json.put("errorText", errorText);
    }
    std::string result = pbnjson::JGenerator::serialize(json, pbnjson::JSchemaFragment("{}"));
    if(!LSMessageReply( lshandle, msg, result.c_str(), &lserror))
    {
        return false;
    }

    return true;

}

bool NotificationService::cb_launch(LSHandle* lshandle, LSMessage *msg, void *user_data)
{
    LSErrorSafe lserror;
    pbnjson::JValue request;
    std::string path;
    std::string errorText;
    JUtil::Error error;

    request = JUtil::parse(LSMessageGetPayload(msg), "", &error);
    if(request.isNull()){
        LOG_DEBUG("Error In ApplicationManager response: %s", __PRETTY_FUNCTION__);
        return false;
    }
    else
    {
        if(request["returnValue"].asBool()){
             LOG_INFO(MSGID_LAUNCH_ALERTAPP, 1, PMLOGKS("proceesId",(request["processId"].asString()).c_str()),"");
        }
        else{
            LOG_DEBUG("Error in Launching App: %s",(request["errorText"].asString()).c_str());
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
        //NotificationService::instance()->postNotification(alertMsgQueue.front(),false,false); BreadNutMergeTODO : This line is used instead for wearable

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

void NotificationService::processInputAlertMsgQueue()
{
    while(!inputAlertMsgQueue.empty())
    {
        std::string errText;
        NotificationService::instance()->postInputAlertNotification(inputAlertMsgQueue.front(), errText);

        inputAlertMsgQueue.pop();
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

void NotificationService::resetPincode_message()
{
	LOG_DEBUG("resetPincode_message cur - %p , %s in %s",m_pincode_message, __func__, __FILE__ );
	m_pincode_message = NULL;
	LOG_DEBUG("resetPincode_message reset - %p , %s in %s",m_pincode_message, __func__, __FILE__ );
}

void NotificationService::setPincode_message(LSMessage *msg)
{

	LOG_DEBUG("setPincode_message -%p , %s in %s",msg, __func__, __FILE__ );

	m_pincode_message = msg;
}

bool NotificationService::getPincode_message(LSMessage **msg)
{
	if (m_pincode_message == NULL)
		return false;

	*msg = m_pincode_message;
	LOG_DEBUG("getPincode_message -0x%p , %s in %s",msg, __func__, __FILE__ );

	return true;
}

bool NotificationService::checkUnacceptablePincode(const std::string &rPincode)
{
	// If french(FRA) country code, 0000 can not be set.
	// Spec. UX_2015_Beehive_Advanced Settings_v1.5_140825, 155 p.
	if (Settings::instance()->m_system_country == "FRA" && rPincode == "0000")
	{
		LOG_WARNING(MSGID_RP_PINCODE_INVALID, 0, "Prevent certain password for specific country in %s", __PRETTY_FUNCTION__);
		return true;
	}

	return false;
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

void NotificationService::onPincodePromptStatus(bool enabled)
{
    LOG_DEBUG("onPincodePromptStaus : %d", enabled);
    if (!enabled)
    {
        if (m_pincode_message)
        {
            pbnjson::JValue reply = pbnjson::Object();
            reply.put("returnValue", false);
            reply.put("errorText", "Pincode UI is not available");

            if(!LSMessageRespond(m_pincode_message, JUtil::jsonToString(reply).c_str(), NULL))
            {
            	LOG_ERROR(MSGID_FAILED_TO_RESPOND, 2, PMLOGKS("SERVICE_NAME", get_service_name()), PMLOGKS("ERROR_MESSAGE", "Failed to respond"), "Failed to respond in %s", __PRETTY_FUNCTION__);      	
            }

            LSMessageUnref(m_pincode_message);
            resetPincode_message();
        }
    }
}

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
