#include <NimBLEDevice.h>

void setup() {

  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println("=== BLE SCANNER ===");

  NimBLEDevice::init("");

  NimBLEScan* scan =
    NimBLEDevice::getScan();

  scan->setActiveScan(true);

  scan->setInterval(100);

  scan->setWindow(80);
}

void loop() {

  Serial.println();
  Serial.println("Scansione BLE...");

  NimBLEScan* scan =
    NimBLEDevice::getScan();

  NimBLEScanResults results =
    scan->start(5, false);


  Serial.print("Dispositivi trovati: ");
  Serial.println(results.getCount());

  Serial.println();


  for (
    int i = 0;
    i < results.getCount();
    i++
  ) {

    const NimBLEAdvertisedDevice* device =
      results.getDevice(i);


    Serial.print("MAC: ");

    Serial.println(
      device->getAddress().toString().c_str()
    );


    Serial.print("Nome: ");

    if (
      device->haveName()
    ) {

      Serial.println(
        device->getName().c_str()
      );

    } else {

      Serial.println(
        "(nessun nome)"
      );
    }


    Serial.print("RSSI: ");

    Serial.println(
      device->getRSSI()
    );


    Serial.println(
      "------------------------------"
    );
  }


  scan->clearResults();

  delay(3000);
}
