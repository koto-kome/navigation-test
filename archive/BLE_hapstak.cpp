#include <M5Unified.h>

#include <driver/i2s_std.h>
#include <driver/gpio.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

// ==================================================
// hapStakの波形
// ==================================================

extern const unsigned char keninryoku[556];


// ==================================================
// hapStak I2S設定
// ==================================================

#define I2S_BCLK 6
#define I2S_WS   8
#define I2S_DOUT 5

i2s_chan_handle_t tx_handle = NULL;


// ==================================================
// Bluetooth設定
// ==================================================

// Bluetoothで使うサービス
#define SERVICE_UUID \
  "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"

// Bluetoothで命令を受け取る場所
#define CHARACTERISTIC_UUID_RX \
  "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"


// ==================================================
// Bluetooth
// ==================================================

BLECharacteristic *rxCharacteristic;

bool deviceConnected = false;


// ==================================================
// 振動命令
// ==================================================

volatile bool vibrateRequested = false;


// ==================================================
// Bluetooth接続状態
// ==================================================

class MyServerCallbacks : public BLEServerCallbacks {

  void onConnect(BLEServer* pServer) {

    deviceConnected = true;

    Serial.println("Bluetooth Connected!");

  }

  void onDisconnect(BLEServer* pServer) {

    deviceConnected = false;

    Serial.println("Bluetooth Disconnected!");

    // 再び接続できるようにする
    pServer->startAdvertising();

  }

};


// ==================================================
// Bluetoothでデータを受信
// ==================================================

class MyCallbacks : public BLECharacteristicCallbacks {

  void onWrite(BLECharacteristic *pCharacteristic) {

    String value =
      pCharacteristic->getValue();

    Serial.print("Received: ");
    Serial.println(value);


    // 「VIBRATE」を受信したら
    if (value == "VIBRATE") {

      vibrateRequested = true;

    }

  }

};


// ==================================================
// hapStakを振動させる
// ==================================================

void vibrate() {

  Serial.println("VIBRATE!");

  i2s_channel_enable(tx_handle);

  size_t bytes_written = 0;

  for(int i = 0; i < 20; i++){
    esp_err_t err = i2s_channel_write(
      tx_handle,
      keninryoku,
      556,
      &bytes_written,
      portMAX_DELAY
    );
  
    Serial.printf(
      "err=%d bytes=%d\n",
      err,
      bytes_written
    );
  }
  i2s_channel_disable(tx_handle);
  Serial.println("VIBRATE END");

}


// ==================================================
// setup
// ==================================================

void setup() {

  M5.begin();

  Serial.begin(115200);


  // ==================================================
  // I2S設定
  // ==================================================

  i2s_chan_config_t chan_cfg =
    I2S_CHANNEL_DEFAULT_CONFIG(
      I2S_NUM_AUTO,
      I2S_ROLE_MASTER
    );


  ESP_ERROR_CHECK(
    i2s_new_channel(
      &chan_cfg,
      &tx_handle,
      NULL
    )
  );


  i2s_std_config_t std_cfg = {

    .clk_cfg =
      I2S_STD_CLK_DEFAULT_CONFIG(44100),

    .slot_cfg =
      I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_16BIT,
        I2S_SLOT_MODE_MONO
      ),

    .gpio_cfg = {

      .mclk = I2S_GPIO_UNUSED,

      .bclk =
        (gpio_num_t)I2S_BCLK,

      .ws =
        (gpio_num_t)I2S_WS,

      .dout =
        (gpio_num_t)I2S_DOUT,

      .din =
        I2S_GPIO_UNUSED,

      .invert_flags = {

        .mclk_inv = false,
        .bclk_inv = false,
        .ws_inv = false,

      },

    },

  };


  ESP_ERROR_CHECK(
    i2s_channel_init_std_mode(
      tx_handle,
      &std_cfg
    )
  );


  ESP_ERROR_CHECK(
    i2s_channel_enable(tx_handle)
  );


  Serial.println("I2S Ready");


  // ==================================================
  // Bluetooth開始
  // ==================================================

  BLEDevice::init("Navigation-HapStak");

  BLEServer *server =
    BLEDevice::createServer();

  server->setCallbacks(
    new MyServerCallbacks()
  );


  BLEService *service =
    server->createService(
      SERVICE_UUID
    );


  rxCharacteristic =
    service->createCharacteristic(
      CHARACTERISTIC_UUID_RX,
      BLECharacteristic::PROPERTY_WRITE
    );


  rxCharacteristic->setCallbacks(
    new MyCallbacks()
  );


  service->start();


  BLEAdvertising *advertising =
    BLEDevice::getAdvertising();

  advertising->addServiceUUID(
    SERVICE_UUID
  );

  advertising->setScanResponse(true);

  advertising->start();


  Serial.println(
    "Bluetooth Ready!"
  );

  Serial.println(
    "Device: Navigation-HapStak"
  );

}


// ==================================================
// loop
// ==================================================

void loop() {

  M5.update();


  // ==================================================
  // Bluetoothから振動命令が来た
  // ==================================================

  if (vibrateRequested) {

    vibrate();

    vibrateRequested = false;

  }


  // ==================================================
  // 今まで通りボタンでも振動できるようにする
  // ==================================================

  if (M5.BtnA.wasPressed()) {

    vibrate();

  }


  delay(10);

}
