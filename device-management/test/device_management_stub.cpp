//
// Created by zhaobo03 on 17-6-8.
//
#include "device_management_stub.h"
#include <regex>
#include <MQTTClient.h>

#include <cjson/cJSON.h>
#include <boost/format.hpp>
#include <mutex>
#include <list>

#include "test_conf.h"
#include "test_util.h"

class DeviceManagementStubImpl: public DeviceManagementStub {
public:
    DeviceManagementStubImpl(const std::string &broker, const std::string &username, const std::string &password,
                             const std::string &clientId);
    virtual ~DeviceManagementStubImpl();
    void start();

    virtual void setAutoResponse(bool value) override;

    virtual void addListener(CallBack f) override;

    virtual void clearListeners() override;

private:
    std::string username;
    std::string password;
    MQTTClient client;

    bool autoResponse;
    std::list<CallBack> callbacks;
    std::mutex callbackMutex;

    static const std::regex topicRegex;
    static const std::string update;
    static const std::string requestIdKey;
    static const std::string acceptedFormat;
    static const std::string rejectedFormat;

    // MQTT callbacks
    static void connection_lost(void *context, char *cause);

    static int message_arrived(void *context, char *topicName, int topicLen, MQTTClient_message *message);

    static void delivery_complete(void *context, MQTTClient_deliveryToken dt);

    // Business logic
    void processUpdate(const std::string &device, const std::string requestId, cJSON *document);
};

// First group matches device name and second group matches action.
const std::regex DeviceManagementStubImpl::topicRegex = std::regex(TestConf::topicPrefix + "(.*)/(.*)");

const std::string DeviceManagementStubImpl::update = "update";

const std::string DeviceManagementStubImpl::requestIdKey = "requestId";

const std::string DeviceManagementStubImpl::acceptedFormat = TestConf::topicPrefix + "%s/%s/accepted";

const std::string DeviceManagementStubImpl::rejectedFormat = TestConf::topicPrefix + "%s/%s/rejected";

DeviceManagementStubImpl::DeviceManagementStubImpl(const std::string &broker, const std::string &username,
                                                   const std::string &password, const std::string &clientId) :
        username(username), password(password) {
    MQTTClient_create(&client, broker.data(), clientId.data(), MQTTCLIENT_PERSISTENCE_NONE, NULL);
    autoResponse = true;
}

DeviceManagementStubImpl::~DeviceManagementStubImpl() {
    if (client != NULL) {
        MQTTClient_disconnect(client, 10);
        MQTTClient_destroy(&client);
    }
}

void DeviceManagementStubImpl::start() {
    MQTTClient_connectOptions options = MQTTClient_connectOptions_initializer;
    options.username = username.data();
    options.password = password.data();
    MQTTClient_setCallbacks(client, this, connection_lost, message_arrived, delivery_complete);
    MQTTClient_connect(client, &options);
    std::string topicFilter = TestConf::topicPrefix + "+/update";
    MQTTClient_subscribe(client, topicFilter.data(), 1);
}

void DeviceManagementStubImpl::setAutoResponse(bool value) {
    autoResponse = value;
}

void DeviceManagementStubImpl::addListener(CallBack f) {
    std::lock_guard<std::mutex> lock(callbackMutex);
    callbacks.push_back(f);
}

void DeviceManagementStubImpl::clearListeners() {
    std::lock_guard<std::mutex> lock(callbackMutex);
    callbacks.clear();
}

int DeviceManagementStubImpl::message_arrived(void *context, char *topicName, int topicLen,
                                               MQTTClient_message *message) {
    DeviceManagementStubImpl *impl = static_cast<DeviceManagementStubImpl *>(context);
    std::cmatch results;
    bool matched = std::regex_match(topicName, results, topicRegex, std::regex_constants::match_default);
    if (matched) {
        const std::string &device = results[1];
        const std::string &action = results[2];
        char *payload = static_cast<char *>(message->payload);
        char *jsonString = payload;
        if (jsonString[message->payloadlen - 1] != '\0') {
            // Make a copy
            jsonString = static_cast<char *>(malloc(message->payloadlen + 1));
            strncpy(jsonString, payload, message->payloadlen);
            jsonString[message->payloadlen] = '\0';
        }

        cJSON *document = cJSON_Parse(jsonString);
        if (jsonString != payload) {
            free(jsonString);
        }
        const std::string requestId(cJSON_GetObjectItem(document, requestIdKey.data())->valuestring);
        if (action == DeviceManagementStubImpl::update) {
            impl->processUpdate(device, requestId, document);
        }

        for (CallBack &callback : impl->callbacks) {
            callback.operator()(device, action);
        }

        cJSON_Delete(document);

    }

    MQTTClient_free(topicName);
    MQTTClient_freeMessage(&message);
    return 1;
}

void DeviceManagementStubImpl::connection_lost(void *context, char *cause) {
    DeviceManagementStubImpl *impl = static_cast<DeviceManagementStubImpl *>(context);

}

void DeviceManagementStubImpl::delivery_complete(void *context, MQTTClient_deliveryToken tok) {

}

void DeviceManagementStubImpl::processUpdate(const std::string &device, const std::string requestId, cJSON *document) {
    boost::format format(acceptedFormat);
    std::string topic = boost::str(format % device % update);
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "requestId", requestId.data());
    char *payload = cJSON_Print(json);
    // Send ack
    MQTTClient_publish(client, topic.data(), strlen(payload) + 1, payload, 1, 0, NULL);
    free(payload);
}

std::shared_ptr<DeviceManagementStub> DeviceManagementStub::create() {
    return std::shared_ptr<DeviceManagementStub>(
            new DeviceManagementStubImpl(TestConf::testMqttBroker,
                                         TestConf::testMqttUsername,
                                         TestConf::testMqttPassword,
                                         TestUtil::uuid()));
}