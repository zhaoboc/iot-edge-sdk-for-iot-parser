#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <device_management.h>
#include <uuid/uuid.h>
#include "device_management_stub.h"
#include "test_conf.h"
#include "test_util.h"

typedef std::function<void (ShadowAction, ShadowAckStatus, ShadowActionAck *, void *)> ActionCallback;

TEST(InitTest, DoubleInit) {
    device_management_init();
    device_management_init();
    device_management_fini();
}

TEST(InitTest, DoubleFini) {
    device_management_fini();
    device_management_fini();
}

class UpdateTest: public ::testing::Test {
protected:
    virtual void SetUp() override;
    virtual void TearDown() override;

    std::shared_ptr<DeviceManagementStub> stub;
};

void UpdateTest::SetUp() {
    stub = DeviceManagementStub::create();
    stub->start();
}

void UpdateTest::TearDown() {
    stub->clearListeners();
}

class Listener {
public:
    virtual void ServerCallBack(const std::string &deviceName, const std::string action) = 0;
    virtual void ClientCallback(ShadowAction action, ShadowAckStatus status, ShadowActionAck *ack, void *context) = 0;
};

class MockListener: public virtual Listener {
public:
    MOCK_METHOD2(ServerCallBack, void(const std::string &deviceName, const std::string action));

    MOCK_METHOD4(ClientCallback, void(ShadowAction action, ShadowAckStatus status, ShadowActionAck *ack, void *context));

    int called = 0;
};

TEST_F(UpdateTest, UpdateHappy) {
    device_management_init();
    DeviceManagementClient client;
    std::string seed = TestUtil::uuid();
    std::string deviceName = "UpdateHappy-" + seed;
    device_management_create(&client, TestConf::testMqttBroker.data(), deviceName.data(),
                             TestConf::testMqttUsername.data(), TestConf::testMqttPassword.data());
    device_management_connect(client);

    MockListener listener;

    stub->addListener([&listener](const std::string &deviceName, const std::string action) {
        listener.ServerCallBack(deviceName, action);
        listener.called++;
    });
    EXPECT_CALL(listener,ServerCallBack(deviceName, "update")).Times(1);

    ShadowActionCallback cb {[](ShadowAction action, ShadowAckStatus status, ShadowActionAck *ack, void *context) {
        MockListener *pListener = static_cast<MockListener *>(context);
        pListener->ClientCallback(action, status, ack, context);
        pListener->called++;
    }};
    EXPECT_CALL(listener,ClientCallback(SHADOW_UPDATE, SHADOW_ACK_ACCEPTED, testing::_, &listener)).Times(1);

    cJSON *reported = cJSON_CreateObject();
    cJSON_AddStringToObject(reported, "color", "green");
    device_management_shadow_update(client, cb, &listener, 10, reported);
    cJSON_Delete(reported);

    for (int i = 0; i < 60; ++i) {
        if (listener.called == 2) {
            break;
        }
        sleep(1);
    }
    ASSERT_EQ(2, listener.called);
}