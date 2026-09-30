#include <NimBLEDevice.h>

// ============================================================
// BLE MIDI UUID
// ============================================================

static NimBLEUUID midiServiceUUID(
  "03B80E5A-EDE8-4B33-A751-6CE34EC4C700"
);

static NimBLEUUID midiCharUUID(
  "7772E5DB-3868-4112-A1A9-F2669D106BF3"
);

// METTI QUI IL MAC DEL TUO M-VAVE
const char* TARGET_MAC = "69:1F:9E:E9:97:CF";

NimBLEClient* client = nullptr;
NimBLERemoteCharacteristic* midiChar = nullptr;


// ============================================================
// STAMPA MESSAGGIO MIDI DECODIFICATO
// ============================================================

void printMidiMessage(
  uint8_t status,
  uint8_t data1,
  uint8_t data2
) {

  uint8_t command =
    status & 0xF0;

  uint8_t channel =
    (status & 0x0F) + 1;


  Serial.print("CH");
  Serial.print(channel);
  Serial.print("  ");


  switch (command) {

    // --------------------------------------------------------
    // NOTE OFF
    // --------------------------------------------------------

    case 0x80:

      Serial.print("NOTE_OFF");

      Serial.print("  NOTE=");
      Serial.print(data1);

      Serial.print("  VEL=");
      Serial.println(data2);

      break;


    // --------------------------------------------------------
    // NOTE ON
    // --------------------------------------------------------

    case 0x90:

      if (data2 == 0) {

        Serial.print("NOTE_OFF");

        Serial.print("  NOTE=");
        Serial.print(data1);

        Serial.print("  VEL=0");

        Serial.println();

      } else {

        Serial.print("NOTE_ON");

        Serial.print("   NOTE=");
        Serial.print(data1);

        Serial.print("  VEL=");
        Serial.println(data2);
      }

      break;


    // --------------------------------------------------------
    // CONTROL CHANGE
    // --------------------------------------------------------

    case 0xB0:

      Serial.print("CC");

      Serial.print("        CC=");
      Serial.print(data1);

      Serial.print("  VALUE=");
      Serial.println(data2);

      break;


    // --------------------------------------------------------
    // PROGRAM CHANGE
    // --------------------------------------------------------

    case 0xC0:

      Serial.print("PROGRAM_CHANGE");

      Serial.print("  PROGRAM=");
      Serial.println(data1);

      break;


    // --------------------------------------------------------
    // CHANNEL PRESSURE
    // --------------------------------------------------------

    case 0xD0:

      Serial.print("CHANNEL_PRESSURE");

      Serial.print("  VALUE=");
      Serial.println(data1);

      break;


    // --------------------------------------------------------
    // PITCH BEND
    // --------------------------------------------------------

    case 0xE0:
    {

      int pitch =
        ((int)data2 << 7)
        |
        data1;

      Serial.print("PITCH_BEND");

      Serial.print("  VALUE=");
      Serial.println(pitch);

      break;
    }


    default:

      Serial.print("ALTRO STATUS=0x");

      Serial.println(
        status,
        HEX
      );

      break;
  }
}


// ============================================================
// CALLBACK BLE MIDI
// ============================================================

void midiNotify(
  NimBLERemoteCharacteristic* pCharacteristic,
  uint8_t* data,
  size_t length,
  bool isNotify
) {

  if (length < 3) {
    return;
  }


  // =========================================================
  // RAW PACKET
  // =========================================================

  Serial.print("RAW: ");

  for (
    size_t i = 0;
    i < length;
    i++
  ) {

    if (data[i] < 0x10) {
      Serial.print("0");
    }

    Serial.print(
      data[i],
      HEX
    );

    Serial.print(" ");
  }

  Serial.println();


  // =========================================================
  // BLE MIDI PARSER
  //
  // per il tuo M-VAVE:
  // primi due byte = timestamp BLE
  // =========================================================

  size_t i = 2;

  uint8_t runningStatus = 0;


  while (i < length) {

    uint8_t b = data[i];


    // --------------------------------------------------------
    // NUOVO STATUS MIDI
    // --------------------------------------------------------

    if (
      b >= 0x80 &&
      b <= 0xEF
    ) {

      runningStatus = b;

      i++;
    }


    if (runningStatus == 0) {

      i++;

      continue;
    }


    uint8_t command =
      runningStatus & 0xF0;


    // --------------------------------------------------------
    // MIDI A 3 BYTE
    // --------------------------------------------------------

    if (
      command == 0x80 ||
      command == 0x90 ||
      command == 0xA0 ||
      command == 0xB0 ||
      command == 0xE0
    ) {

      if (i + 1 >= length) {
        break;
      }


      // se arriva un altro status,
      // riparte dal prossimo giro

      if (data[i] >= 0x80) {
        continue;
      }


      uint8_t d1 =
        data[i++];


      if (i >= length) {
        break;
      }


      if (data[i] >= 0x80) {
        continue;
      }


      uint8_t d2 =
        data[i++];


      printMidiMessage(
        runningStatus,
        d1,
        d2
      );
    }


    // --------------------------------------------------------
    // MIDI A 2 BYTE
    // --------------------------------------------------------

    else if (
      command == 0xC0 ||
      command == 0xD0
    ) {

      if (i >= length) {
        break;
      }


      if (data[i] >= 0x80) {
        continue;
      }


      uint8_t d1 =
        data[i++];


      printMidiMessage(
        runningStatus,
        d1,
        0
      );
    }


    else {

      i++;
    }
  }


  Serial.println(
    "--------------------------------------------"
  );
}


// ============================================================
// CONNESSIONE BLE
// ============================================================

bool connectMidi() {

  Serial.println();

  Serial.print(
    "Connessione a "
  );

  Serial.println(
    TARGET_MAC
  );


  NimBLEAddress address(
    TARGET_MAC,
    BLE_ADDR_PUBLIC
  );


  client =
    NimBLEDevice::createClient();


  client->setConnectionParams(
    6,
    12,
    0,
    200
  );


  if (
    !client->connect(address)
  ) {

    Serial.println(
      "Connessione fallita"
    );

    return false;
  }


  Serial.println(
    "BLE connesso"
  );


  NimBLERemoteService* service =
    client->getService(
      midiServiceUUID
    );


  if (!service) {

    Serial.println(
      "Servizio BLE MIDI non trovato"
    );

    return false;
  }


  midiChar =
    service->getCharacteristic(
      midiCharUUID
    );


  if (!midiChar) {

    Serial.println(
      "Characteristic MIDI non trovata"
    );

    return false;
  }


  if (
    midiChar->canNotify()
  ) {

    if (
      midiChar->subscribe(
        true,
        midiNotify
      )
    ) {

      Serial.println();
      Serial.println(
        "=================================="
      );

      Serial.println(
        " MIDI MAPPER PRONTO"
      );

      Serial.println(
        " Premi un tasto alla volta"
      );

      Serial.println(
        "=================================="
      );

      return true;
    }
  }


  return false;
}


// ============================================================
// SETUP
// ============================================================

void setup() {

  Serial.begin(115200);

  delay(1000);


  Serial.println();

  Serial.println(
    "M-VAVE BLE MIDI MAPPER"
  );


  NimBLEDevice::init(
    "ESP32-MIDI-MAPPER"
  );


  connectMidi();
}


// ============================================================
// LOOP
// ============================================================

void loop() {

  if (
    client != nullptr &&
    !client->isConnected()
  ) {

    Serial.println(
      "M-VAVE disconnesso"
    );


    delay(1000);


    connectMidi();
  }


  delay(10);
}
