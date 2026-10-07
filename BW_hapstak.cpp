#include <M5Unified.h>

#include <ArduinoJson.h>
#include <WiFi.h>
#include <WebSocketsServer.h>

#include <driver/gpio.h>
#include <driver/i2s_std.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

#include <cstring>

extern const unsigned char keninryoku[556];

constexpr gpio_num_t I2S_BCLK = static_cast<gpio_num_t>(6);
constexpr gpio_num_t I2S_WS = static_cast<gpio_num_t>(8);
constexpr gpio_num_t I2S_DOUT = static_cast<gpio_num_t>(5);
constexpr size_t VIBRATION_SAMPLE_SIZE = 556;
constexpr uint8_t VIBRATION_REPEAT_COUNT = 20;

const char *const ACCESS_POINT_SSID = "Navigation-HapStak";
const char *const ACCESS_POINT_PASSWORD = "hapstak1234";
const char *const BLE_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
const char *const BLE_CHARACTERISTIC_UUID_RX = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";

i2s_chan_handle_t tx_handle = nullptr;
BLECharacteristic *rxCharacteristic = nullptr;
BLEServer *bleServer = nullptr;
WebSocketsServer webSocket(8080);

volatile bool vibrateRequested = false;
volatile bool stopRequested = false;
bool bluetoothConnected = false;
bool wifiReady = false;
bool vibrationPlaying = false;
uint8_t vibrationRepeatsRemaining = 0;

void requestVibration() {
  stopRequested = false;
  vibrateRequested = true;
}

void requestStop() {
  vibrateRequested = false;
  stopRequested = true;
}

void stopVibration() {
  if (vibrationPlaying) {
    i2s_channel_disable(tx_handle);
  }

  vibrationPlaying = false;
  vibrationRepeatsRemaining = 0;
  vibrateRequested = false;
  stopRequested = false;
  Serial.println("VIBRATION STOPPED");
}

void startVibration() {
  const esp_err_t error = i2s_channel_enable(tx_handle);
  if (error != ESP_OK) {
    Serial.printf("I2S enable failed: %d\n", error);
    return;
  }

  vibrationRepeatsRemaining = VIBRATION_REPEAT_COUNT;
  vibrationPlaying = true;
  Serial.println("VIBRATION STARTED");
}

void writeVibrationSample() {
  size_t bytesWritten = 0;
  const esp_err_t error = i2s_channel_write(
    tx_handle,
    keninryoku,
    VIBRATION_SAMPLE_SIZE,
    &bytesWritten,
    pdMS_TO_TICKS(50)
  );

  if (error != ESP_OK || bytesWritten != VIBRATION_SAMPLE_SIZE) {
    Serial.printf("I2S write failed: error=%d bytes=%u\n", error, static_cast<unsigned>(bytesWritten));
    stopVibration();
    return;
  }

  --vibrationRepeatsRemaining;
  if (vibrationRepeatsRemaining == 0) {
    stopVibration();
  }
}

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    bluetoothConnected = true;
    Serial.println("Bluetooth connected");
  }

  void onDisconnect(BLEServer *server) override {
    bluetoothConnected = false;
    Serial.println("Bluetooth disconnected");
    server->startAdvertising();
  }
};

class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    const String value = characteristic->getValue();
    Serial.printf("BLE received: %s\n", value.c_str());

    if (value == "VIBRATE") {
      requestVibration();
    } else if (value == "STOP") {
      requestStop();
    }
  }
};

void handleWebSocketEvent(uint8_t clientNumber, WStype_t type, uint8_t *payload, size_t length) {
  if (type == WStype_CONNECTED) {
    Serial.printf("WiFi client %u connected\n", clientNumber);
    return;
  }

  if (type == WStype_DISCONNECTED) {
    Serial.printf("WiFi client %u disconnected\n", clientNumber);
    return;
  }

  if (type != WStype_TEXT || payload == nullptr) {
    return;
  }

  if (length == 7 && std::memcmp(payload, "VIBRATE", 7) == 0) {
    requestVibration();
    return;
  }

  if (length == 4 && std::memcmp(payload, "STOP", 4) == 0) {
    requestStop();
    return;
  }

  StaticJsonDocument<256> command;
  const DeserializationError error = deserializeJson(command, payload, length);
  if (error) {
    Serial.printf("Invalid WiFi command: %s\n", error.c_str());
    return;
  }

  const char *commandType = command["type"] | "";
  if (std::strcmp(commandType, "VIBRATE") == 0) {
    requestVibration();
  } else if (std::strcmp(commandType, "STOP") == 0) {
    requestStop();
  } else {
    Serial.printf("Unknown WiFi command: %s\n", commandType);
  }
}

void setupI2S() {
  const i2s_chan_config_t channelConfig = I2S_CHANNEL_DEFAULT_CONFIG(
    I2S_NUM_AUTO,
    I2S_ROLE_MASTER
  );

  ESP_ERROR_CHECK(i2s_new_channel(&channelConfig, &tx_handle, nullptr));

  const i2s_std_config_t standardConfig = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100),
    .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
      I2S_DATA_BIT_WIDTH_16BIT,
      I2S_SLOT_MODE_MONO
    ),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = I2S_BCLK,
      .ws = I2S_WS,
      .dout = I2S_DOUT,
      .din = I2S_GPIO_UNUSED,
      .invert_flags = {
        .mclk_inv = false,
        .bclk_inv = false,
        .ws_inv = false,
      },
    },
  };

  ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &standardConfig));
  Serial.println("I2S ready");
}

void setupBluetooth() {
  BLEDevice::init("Navigation-HapStak");
  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(new MyServerCallbacks());

  BLEService *service = bleServer->createService(BLE_SERVICE_UUID);
  rxCharacteristic = service->createCharacteristic(
    BLE_CHARACTERISTIC_UUID_RX,
    BLECharacteristic::PROPERTY_WRITE
  );
  rxCharacteristic->setCallbacks(new MyCallbacks());
  service->start();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(BLE_SERVICE_UUID);
  advertising->setScanResponse(true);
  advertising->start();

  Serial.println("Bluetooth ready: Navigation-HapStak");
}

void setupWiFi() {
  WiFi.mode(WIFI_AP);

  const IPAddress accessPointIP(192, 168, 0, 10);
  const IPAddress subnet(255, 255, 255, 0);
  if (!WiFi.softAPConfig(accessPointIP, accessPointIP, subnet)) {
    Serial.println("WiFi access point IP setup failed");
    return;
  }

  if (!WiFi.softAP(ACCESS_POINT_SSID, ACCESS_POINT_PASSWORD)) {
    Serial.println("WiFi access point start failed");
    return;
  }

  webSocket.begin();
  webSocket.onEvent(handleWebSocketEvent);
  wifiReady = true;

  Serial.printf("WiFi AP ready: %s\n", ACCESS_POINT_SSID);
  Serial.printf("WebSocket server: ws://%s:8080\n", WiFi.softAPIP().toString().c_str());
}

void setup() {
  M5.begin();
  Serial.begin(115200);

  setupI2S();
  setupBluetooth();
  setupWiFi();
}

void loop() {
  M5.update();

  if (wifiReady) {
    webSocket.loop();
  }

  if (M5.BtnA.wasPressed()) {
    requestVibration();
  }

  if (stopRequested) {
    stopVibration();
  }

  if (vibrateRequested && !vibrationPlaying) {
    vibrateRequested = false;
    startVibration();
  }

  if (vibrationPlaying) {
    writeVibrationSample();
  }

  delay(2);
}