#include <NimBLEDevice.h>
#include <math.h>

// ============================================================
// CONFIG
// ============================================================

#define AUDIO_PIN 25

#define SAMPLE_RATE 12000
#define SAMPLE_PERIOD_US (1000000UL / SAMPLE_RATE)

#define FIXED_VELOCITY 110
#define MIN_NOTE_DURATION_MS 120

#define MAX_VOICES 16

#define NUM_TRACKS 7
#define MAX_EVENTS_PER_TRACK 320

#define SOURCE_LIVE 255

#define MIDI_QUEUE_SIZE 64


// ============================================================
// BLE MIDI
// ============================================================

static NimBLEUUID midiServiceUUID(
  "03B80E5A-EDE8-4B33-A751-6CE34EC4C700"
);

static NimBLEUUID midiCharUUID(
  "7772E5DB-3868-4112-A1A9-F2669D106BF3"
);

const char* TARGET_MAC =  //Mac andress our synth
  "INSERT YOUR MAC ANDRESS";

NimBLEClient* client = nullptr;
NimBLERemoteCharacteristic* midiChar = nullptr;


// ============================================================
// MIDI QUEUE
// ============================================================

struct QueuedMidiMessage {

  uint8_t status;
  uint8_t data1;
  uint8_t data2;
};

QueuedMidiMessage midiQueue[MIDI_QUEUE_SIZE];

volatile uint8_t midiQueueRead = 0;
volatile uint8_t midiQueueWrite = 0;


// ============================================================
// AUDIO
// ============================================================

unsigned long lastSampleMicros = 0;

int8_t sineTable[256];

uint32_t noiseState = 0x12345678;


// ============================================================
// STRUMENTI
// ============================================================

enum Instrument {

  INST_SQUARE = 0,
  INST_SAW,
  INST_TRIANGLE,
  INST_SINE,
  INST_ORGAN,
  INST_BASS,
  INST_LEAD,
  INST_PLUCK,
  INST_BELL,

  INST_KICK,
  INST_SNARE,
  INST_HIHAT_CLOSED,
  INST_HIHAT_OPEN,
  INST_TOM,
  INST_CLAP,

  INST_DRUMKIT,

  NUM_INSTRUMENTS
};


const char* instrumentNames[] = {

  "Square Retro",
  "Saw Synth",
  "Triangle",
  "Soft Sine",
  "Organ",
  "Bass",
  "Lead",
  "Pluck",
  "Bell",

  "Kick",
  "Snare",
  "Closed Hi-Hat",
  "Open Hi-Hat",
  "Tom",
  "Clap",

  "Drum Kit"
};


uint8_t currentInstrument =
  INST_SQUARE;


// ============================================================
// EFFETTI
// ============================================================

float tremoloDepth = 0.0;
float tremoloSpeed = 5.0;

float vibratoDepth = 0.006;
float vibratoSpeed = 5.2;


// ============================================================
// VOICE
// ============================================================

struct Voice {

  bool active;

  uint8_t note;
  uint8_t velocity;
  uint8_t instrument;
  uint8_t source;

  float frequency;
  float phase;

  unsigned long startMs;

  bool releasing;
  bool releaseRequested;

  unsigned long releaseMs;
};

Voice voices[MAX_VOICES];


// ============================================================
// MIDI LOOP
// ============================================================

struct MidiEvent {

  uint32_t timeMs;

  bool noteOn;

  uint8_t note;
  uint8_t velocity;
  uint8_t instrument;
};


struct LoopTrack {

  MidiEvent events[MAX_EVENTS_PER_TRACK];

  uint16_t eventCount;

  bool recording;
  bool playing;

  uint32_t recordStart;
  uint32_t duration;

  uint32_t playStart;
  uint32_t playhead;

  uint16_t playbackIndex;
};

LoopTrack tracks[NUM_TRACKS];


// ============================================================
// CONTROLLI M-VAVE
// ============================================================

const uint8_t recNotes[NUM_TRACKS] = {
  40, 41, 42, 43, 48, 49, 50
};

const uint8_t playNotes[NUM_TRACKS] = {
  36, 37, 38, 39, 44, 45, 46
};


// ============================================================
// NOISE
// ============================================================

int fastNoise() {

  noiseState ^= noiseState << 13;
  noiseState ^= noiseState >> 17;
  noiseState ^= noiseState << 5;

  return
    (int)((noiseState >> 24) & 0xFF) - 128;
}


// ============================================================
// MIDI -> FREQUENZA
// ============================================================

float midiToFrequency(uint8_t note) {

  return
    440.0 *
    pow(
      2.0,
      ((float)note - 69.0) / 12.0
    );
}


// ============================================================
// MIDI QUEUE WRITE
// ============================================================

void queueMidiMessage(
  uint8_t status,
  uint8_t data1,
  uint8_t data2
) {

  uint8_t next =
    (uint8_t)(
      (midiQueueWrite + 1)
      %
      MIDI_QUEUE_SIZE
    );


  // coda piena
  if (
    next == midiQueueRead
  ) {

    return;
  }


  midiQueue[midiQueueWrite].status =
    status;

  midiQueue[midiQueueWrite].data1 =
    data1;

  midiQueue[midiQueueWrite].data2 =
    data2;


  midiQueueWrite =
    next;
}


// ============================================================
// TROVA VOCE
// ============================================================

int findVoice(
  uint8_t note,
  uint8_t source
) {

  for (
    int i = 0;
    i < MAX_VOICES;
    i++
  ) {

    if (
      voices[i].active &&
      voices[i].note == note &&
      voices[i].source == source
    ) {

      return i;
    }
  }

  return -1;
}


// ============================================================
// ALLOCA VOCE
// ============================================================

int allocateVoice() {

  for (
    int i = 0;
    i < MAX_VOICES;
    i++
  ) {

    if (
      !voices[i].active
    ) {

      return i;
    }
  }


  int oldest = 0;

  unsigned long oldestTime =
    voices[0].startMs;


  for (
    int i = 1;
    i < MAX_VOICES;
    i++
  ) {

    if (
      voices[i].startMs <
      oldestTime
    ) {

      oldestTime =
        voices[i].startMs;

      oldest =
        i;
    }
  }

  return oldest;
}


// ============================================================
// CONFIGURE VOICE
// ============================================================

void configureVoice(
  int v,
  uint8_t note,
  uint8_t velocity,
  uint8_t instrument,
  uint8_t source
) {

  // IMPORTANTE:
  // disattiviamo prima

  voices[v].active =
    false;


  voices[v].note =
    note;

  voices[v].velocity =
    velocity;

  voices[v].instrument =
    instrument;

  voices[v].source =
    source;

  voices[v].frequency =
    midiToFrequency(note);

  voices[v].phase =
    0.0;

  voices[v].startMs =
    millis();

  voices[v].releasing =
    false;

  voices[v].releaseRequested =
    false;

  voices[v].releaseMs =
    0;


  // attiva SOLO alla fine

  voices[v].active =
    true;
}


// ============================================================
// START VOICE
// ============================================================

void startVoice(
  uint8_t note,
  uint8_t velocity,
  uint8_t instrument,
  uint8_t source
) {

  int v =
    findVoice(
      note,
      source
    );


  if (
    v < 0
  ) {

    v =
      allocateVoice();
  }


  configureVoice(
    v,
    note,
    velocity,
    instrument,
    source
  );


  Serial.print(
    "VOICE ON "
  );

  Serial.print(
    note
  );

  Serial.print(
    " INST:"
  );

  Serial.print(
    instrumentNames[
      instrument
    ]
  );

  Serial.print(
    " SRC:"
  );


  if (
    source == SOURCE_LIVE
  ) {

    Serial.println(
      "LIVE"
    );

  } else {

    Serial.println(
      source + 1
    );
  }
}


// ============================================================
// STOP VOICE
// ============================================================

void stopVoice(
  uint8_t note,
  uint8_t source
) {

  int v =
    findVoice(
      note,
      source
    );


  if (
    v < 0
  ) {

    return;
  }


  if (
    voices[v].instrument >=
    INST_KICK
  ) {

    return;
  }


  unsigned long age =
    millis()
    -
    voices[v].startMs;


  if (
    age <
    MIN_NOTE_DURATION_MS
  ) {

    voices[v].releaseRequested =
      true;

    return;
  }


  voices[v].releasing =
    true;

  voices[v].releaseRequested =
    false;

  voices[v].releaseMs =
    millis();
}


// ============================================================
// STOP TRACK VOICES
// ============================================================

void stopTrackVoices(
  uint8_t track
) {

  for (
    int i = 0;
    i < MAX_VOICES;
    i++
  ) {

    if (
      voices[i].active &&
      voices[i].source == track
    ) {

      voices[i].active =
        false;
    }
  }
}


// ============================================================
// NEXT INSTRUMENT
// ============================================================

void nextInstrument() {

  currentInstrument++;


  if (
    currentInstrument >=
    NUM_INSTRUMENTS
  ) {

    currentInstrument =
      0;
  }


  Serial.print(
    "STRUMENTO: "
  );

  Serial.print(
    currentInstrument + 1
  );

  Serial.print(
    "/"
  );

  Serial.print(
    NUM_INSTRUMENTS
  );

  Serial.print(
    " - "
  );

  Serial.println(
    instrumentNames[
      currentInstrument
    ]
  );
}


// ============================================================
// DRUM MAP
// ============================================================

uint8_t drumInstrumentFromNote(
  uint8_t note
) {

  switch (
    note
  ) {

    case 36:
      return INST_KICK;

    case 38:
      return INST_SNARE;

    case 39:
      return INST_CLAP;

    case 42:
      return INST_HIHAT_CLOSED;

    case 46:
      return INST_HIHAT_OPEN;

    case 41:
    case 43:
    case 45:
    case 47:
      return INST_TOM;
  }


  switch (
    note % 5
  ) {

    case 0:
      return INST_KICK;

    case 1:
      return INST_SNARE;

    case 2:
      return INST_HIHAT_CLOSED;

    case 3:
      return INST_TOM;

    default:
      return INST_CLAP;
  }
}


// ============================================================
// RECORD EVENT
// ============================================================

void recordEventToActiveTracks(
  bool noteOn,
  uint8_t note,
  uint8_t velocity,
  uint8_t instrument
) {

  for (
    int t = 0;
    t < NUM_TRACKS;
    t++
  ) {

    if (
      !tracks[t].recording
    ) {

      continue;
    }


    if (
      tracks[t].eventCount >=
      MAX_EVENTS_PER_TRACK
    ) {

      continue;
    }


    uint16_t i =
      tracks[t].eventCount;


    tracks[t].events[i].timeMs =
      millis()
      -
      tracks[t].recordStart;


    tracks[t].events[i].noteOn =
      noteOn;

    tracks[t].events[i].note =
      note;

    tracks[t].events[i].velocity =
      velocity;

    tracks[t].events[i].instrument =
      instrument;


    tracks[t].eventCount++;
  }
}


// ============================================================
// TOGGLE RECORD
// ============================================================

void toggleRecord(
  uint8_t track
) {

  if (
    !tracks[track].recording
  ) {

    tracks[track].recording =
      true;

    tracks[track].playing =
      false;

    tracks[track].eventCount =
      0;

    tracks[track].duration =
      0;

    tracks[track].playhead =
      0;

    tracks[track].playbackIndex =
      0;

    tracks[track].recordStart =
      millis();


    stopTrackVoices(
      track
    );


    Serial.print(
      "REC "
    );

    Serial.print(
      track + 1
    );

    Serial.println(
      " START"
    );
  }

  else {

    tracks[track].recording =
      false;


    tracks[track].duration =
      millis()
      -
      tracks[track].recordStart;


    Serial.print(
      "REC "
    );

    Serial.print(
      track + 1
    );

    Serial.println(
      " STOP"
    );


    Serial.print(
      "EVENTI: "
    );

    Serial.println(
      tracks[track].eventCount
    );


    Serial.print(
      "DURATA: "
    );

    Serial.print(
      tracks[track].duration
    );

    Serial.println(
      " ms"
    );
  }
}


// ============================================================
// TOGGLE PLAY
// ============================================================

void togglePlay(
  uint8_t track
) {

  if (
    tracks[track].eventCount == 0 ||
    tracks[track].duration == 0
  ) {

    Serial.print(
      "TRACK "
    );

    Serial.print(
      track + 1
    );

    Serial.println(
      " VUOTA"
    );

    return;
  }


  if (
    !tracks[track].playing
  ) {

    tracks[track].playing =
      true;


    tracks[track].playStart =
      millis()
      -
      tracks[track].playhead;


    tracks[track].playbackIndex =
      0;


    while (
      tracks[track].playbackIndex <
      tracks[track].eventCount
      &&
      tracks[track]
        .events[
          tracks[track].playbackIndex
        ]
        .timeMs
      <
      tracks[track].playhead
    ) {

      tracks[track].playbackIndex++;
    }


    Serial.print(
      "LOOP "
    );

    Serial.print(
      track + 1
    );

    Serial.println(
      " PLAY"
    );
  }

  else {

    tracks[track].playing =
      false;


    tracks[track].playhead =
      (
        millis()
        -
        tracks[track].playStart
      )
      %
      tracks[track].duration;


    stopTrackVoices(
      track
    );


    Serial.print(
      "LOOP "
    );

    Serial.print(
      track + 1
    );

    Serial.println(
      " PAUSE"
    );
  }
}


// ============================================================
// UPDATE LOOPS
// ============================================================

void updateLoops() {

  uint32_t now =
    millis();


  for (
    int t = 0;
    t < NUM_TRACKS;
    t++
  ) {

    if (
      !tracks[t].playing
    ) {

      continue;
    }


    if (
      tracks[t].duration == 0
    ) {

      continue;
    }


    uint32_t elapsed =
      now
      -
      tracks[t].playStart;


    if (
      elapsed >=
      tracks[t].duration
    ) {

      elapsed =
        elapsed
        %
        tracks[t].duration;


      tracks[t].playStart =
        now
        -
        elapsed;


      tracks[t].playbackIndex =
        0;


      stopTrackVoices(
        t
      );
    }


    tracks[t].playhead =
      elapsed;


    while (
      tracks[t].playbackIndex <
      tracks[t].eventCount
      &&
      tracks[t]
        .events[
          tracks[t].playbackIndex
        ]
        .timeMs
      <=
      elapsed
    ) {

      MidiEvent &e =
        tracks[t]
          .events[
            tracks[t].playbackIndex
          ];


      if (
        e.noteOn
      ) {

        startVoice(
          e.note,
          e.velocity,
          e.instrument,
          t
        );

      } else {

        stopVoice(
          e.note,
          t
        );
      }


      tracks[t].playbackIndex++;
    }
  }
}


// ============================================================
// MUSICAL NOTE ON
// ============================================================

void musicalNoteOn(
  uint8_t note,
  uint8_t receivedVelocity
) {

  uint8_t velocity =
    FIXED_VELOCITY;


  uint8_t inst =
    currentInstrument;


  if (
    inst ==
    INST_DRUMKIT
  ) {

    inst =
      drumInstrumentFromNote(
        note
      );
  }


  startVoice(
    note,
    velocity,
    inst,
    SOURCE_LIVE
  );


  recordEventToActiveTracks(
    true,
    note,
    velocity,
    inst
  );


  Serial.print(
    "NOTE "
  );

  Serial.print(
    note
  );

  Serial.print(
    " VEL RX:"
  );

  Serial.print(
    receivedVelocity
  );

  Serial.print(
    " -> "
  );

  Serial.println(
    velocity
  );
}


// ============================================================
// MUSICAL NOTE OFF
// ============================================================

void musicalNoteOff(
  uint8_t note
) {

  stopVoice(
    note,
    SOURCE_LIVE
  );


  recordEventToActiveTracks(
    false,
    note,
    0,
    currentInstrument
  );
}


// ============================================================
// HANDLE NOTE ON
// ============================================================

void handleNoteOn(
  uint8_t channel,
  uint8_t note,
  uint8_t velocity
) {

  // =========================================================
  // CH10 = CONTROLLI
  // =========================================================

  if (
    channel == 10
  ) {

    // NEXT INSTRUMENT

    if (
      note == 51
    ) {

      nextInstrument();

      return;
    }


    // REC

    for (
      int i = 0;
      i < NUM_TRACKS;
      i++
    ) {

      if (
        note ==
        recNotes[i]
      ) {

        toggleRecord(
          i
        );

        return;
      }
    }


    // PLAY

    for (
      int i = 0;
      i < NUM_TRACKS;
      i++
    ) {

      if (
        note ==
        playNotes[i]
      ) {

        togglePlay(
          i
        );

        return;
      }
    }


    return;
  }


  // =========================================================
  // CH1 = TASTIERA
  // =========================================================

  if (
    channel == 1
  ) {

    musicalNoteOn(
      note,
      velocity
    );
  }
}


// ============================================================
// HANDLE NOTE OFF
// ============================================================

void handleNoteOff(
  uint8_t channel,
  uint8_t note
) {

  if (
    channel == 10
  ) {

    return;
  }


  if (
    channel == 1
  ) {

    musicalNoteOff(
      note
    );
  }
}


// ============================================================
// CC
// ============================================================

void handleCC(
  uint8_t channel,
  uint8_t cc,
  uint8_t value
) {

  if (
    channel == 1 &&
    cc == 30
  ) {

    tremoloDepth =
      (float)value
      /
      127.0;


    Serial.print(
      "TREMOLO: "
    );

    Serial.println(
      tremoloDepth,
      2
    );
  }
}


// ============================================================
// PARSE MIDI MESSAGE
// ============================================================

void parseMidiMessage(
  uint8_t status,
  uint8_t data1,
  uint8_t data2
) {

  uint8_t command =
    status
    &
    0xF0;


  uint8_t channel =
    (
      status
      &
      0x0F
    )
    +
    1;


  if (
    command ==
    0x80
  ) {

    handleNoteOff(
      channel,
      data1
    );

    return;
  }


  if (
    command ==
    0x90
  ) {

    if (
      data2 == 0
    ) {

      handleNoteOff(
        channel,
        data1
      );

    } else {

      handleNoteOn(
        channel,
        data1,
        data2
      );
    }

    return;
  }


  if (
    command ==
    0xB0
  ) {

    handleCC(
      channel,
      data1,
      data2
    );

    return;
  }
}


// ============================================================
// PROCESS MIDI QUEUE
// ============================================================

void processMidiQueue() {

  while (
    midiQueueRead != midiQueueWrite
  ) {

    QueuedMidiMessage msg =
      midiQueue[
        midiQueueRead
      ];


    midiQueueRead =
      (uint8_t)(
        (midiQueueRead + 1)
        %
        MIDI_QUEUE_SIZE
      );


    parseMidiMessage(
      msg.status,
      msg.data1,
      msg.data2
    );
  }
}


// ============================================================
// WAVES
// ============================================================

float getSine(
  float phase
) {

  int index =
    (
      (int)(
        phase * 256.0
      )
    )
    &
    255;


  return
    sineTable[index]
    /
    127.0;
}


float getSquare(
  float phase
) {

  return
    phase < 0.5
    ? 1.0
    : -1.0;
}


float getSaw(
  float phase
) {

  return
    phase * 2.0
    -
    1.0;
}


float getTriangle(
  float phase
) {

  return
    1.0
    -
    4.0
    *
    fabs(
      phase - 0.5
    );
}


// ============================================================
// GENERATE VOICE SAMPLE
// ============================================================

float generateVoiceSample(
  int voiceIndex
) {

  Voice &v =
    voices[
      voiceIndex
    ];


  if (
    !v.active
  ) {

    return 0;
  }


  unsigned long age =
    millis()
    -
    v.startMs;


  // =========================================================
  // RELEASE RITARDATO
  // =========================================================

  if (
    v.releaseRequested &&
    age >=
    MIN_NOTE_DURATION_MS
  ) {

    v.releaseRequested =
      false;

    v.releasing =
      true;

    v.releaseMs =
      millis();
  }


  float velocity =
    v.velocity
    /
    127.0;


  // =========================================================
  // RELEASE
  // =========================================================

  float releaseGain =
    1.0;


  if (
    v.releasing
  ) {

    unsigned long releaseAge =
      millis()
      -
      v.releaseMs;


    if (
      releaseAge > 80
    ) {

      v.active =
        false;

      return 0;
    }


    releaseGain =
      1.0
      -
      releaseAge
      /
      80.0;
  }


  // =========================================================
  // VIBRATO
  // =========================================================

  float frequency =
    v.frequency;


  if (
    v.instrument <=
    INST_BELL
    &&
    age > 300
  ) {

    float vib =
      sin(
        2.0 *
        PI *
        vibratoSpeed *
        (
          millis()
          /
          1000.0
        )
      );


    frequency *=
      (
        1.0
        +
        vib *
        vibratoDepth
      );
  }


  // =========================================================
  // PHASE
  // =========================================================

  v.phase +=
    frequency
    /
    SAMPLE_RATE;


  while (
    v.phase >= 1.0
  ) {

    v.phase -=
      1.0;
  }


  float sample =
    0;


// ============================================================
// STRUMENTI
// ============================================================

  switch (
    v.instrument
  ) {

    // SQUARE
    case INST_SQUARE:

      sample =
        getSquare(
          v.phase
        );

      break;


    // SAW
    case INST_SAW:

      sample =
        getSaw(
          v.phase
        );

      break;


    // TRIANGLE
    case INST_TRIANGLE:

      sample =
        getTriangle(
          v.phase
        );

      break;


    // SINE
    case INST_SINE:

      sample =
        getSine(
          v.phase
        );

      break;


    // ORGAN
    case INST_ORGAN:
    {

      float p2 =
        fmod(
          v.phase * 2.0,
          1.0
        );


      float p3 =
        fmod(
          v.phase * 3.0,
          1.0
        );


      sample =
        getSine(
          v.phase
        ) * 0.65
        +
        getSine(
          p2
        ) * 0.22
        +
        getSine(
          p3
        ) * 0.13;

      break;
    }


    // BASS
    case INST_BASS:

      sample =
        getSine(
          v.phase
        ) * 0.70
        +
        getSquare(
          v.phase
        ) * 0.30;

      break;


    // LEAD
    case INST_LEAD:

      sample =
        getSaw(
          v.phase
        ) * 0.75
        +
        getSquare(
          v.phase
        ) * 0.25;

      break;


    // PLUCK
    case INST_PLUCK:
    {

      float env =
        exp(
          -age
          /
          350.0
        );


      sample =
        getTriangle(
          v.phase
        )
        *
        env;


      if (
        age > 1800
      ) {

        v.active =
          false;
      }

      break;
    }


    // BELL
    case INST_BELL:
    {

      float env =
        exp(
          -age
          /
          850.0
        );


      float harmonic =
        fmod(
          v.phase * 2.7,
          1.0
        );


      sample =
        getSine(
          v.phase
        ) * 0.7
        +
        getSine(
          harmonic
        ) * 0.3;


      sample *=
        env;


      if (
        age > 3500
      ) {

        v.active =
          false;
      }

      break;
    }


    // KICK
    case INST_KICK:
    {

      if (
        age > 600
      ) {

        v.active =
          false;

        return 0;
      }


      float t =
        age
        /
        1000.0;


      float kickFreq =
        45.0
        +
        120.0
        *
        exp(
          -t * 18.0
        );


      v.phase +=
        kickFreq
        /
        SAMPLE_RATE;


      while (
        v.phase >= 1.0
      ) {

        v.phase -=
          1.0;
      }


      float env =
        exp(
          -t * 7.0
        );


      sample =
        getSine(
          v.phase
        )
        *
        env;

      break;
    }


    // SNARE
    case INST_SNARE:
    {

      if (
        age > 420
      ) {

        v.active =
          false;

        return 0;
      }


      float env =
        exp(
          -age
          /
          110.0
        );


      float noise =
        fastNoise()
        /
        128.0;


      sample =
        noise * 0.78
        +
        getSine(
          v.phase
        ) * 0.22;


      sample *=
        env;

      break;
    }


    // CLOSED HAT
    case INST_HIHAT_CLOSED:
    {

      if (
        age > 140
      ) {

        v.active =
          false;

        return 0;
      }


      float env =
        exp(
          -age
          /
          35.0
        );


      sample =
        (
          fastNoise()
          /
          128.0
        )
        *
        env;

      break;
    }


    // OPEN HAT
    case INST_HIHAT_OPEN:
    {

      if (
        age > 850
      ) {

        v.active =
          false;

        return 0;
      }


      float env =
        exp(
          -age
          /
          260.0
        );


      sample =
        (
          fastNoise()
          /
          128.0
        )
        *
        env;

      break;
    }


    // TOM
    case INST_TOM:
    {

      if (
        age > 650
      ) {

        v.active =
          false;

        return 0;
      }


      float t =
        age
        /
        1000.0;


      float env =
        exp(
          -t * 6.0
        );


      float freq =
        95.0
        +
        60.0
        *
        exp(
          -t * 9.0
        );


      v.phase +=
        freq
        /
        SAMPLE_RATE;


      while (
        v.phase >= 1.0
      ) {

        v.phase -=
          1.0;
      }


      sample =
        getSine(
          v.phase
        )
        *
        env;

      break;
    }


    // CLAP
    case INST_CLAP:
    {

      if (
        age > 420
      ) {

        v.active =
          false;

        return 0;
      }


      float burst =
        0;


      if (
        age < 35
        ||
        (
          age > 60 &&
          age < 90
        )
        ||
        (
          age > 120 &&
          age < 155
        )
      ) {

        burst =
          1.0;

      } else {

        burst =
          exp(
            -(age - 155)
            /
            100.0
          )
          *
          0.5;
      }


      sample =
        (
          fastNoise()
          /
          128.0
        )
        *
        burst;

      break;
    }


    default:

      sample =
        0;

      break;
  }


  return
    sample
    *
    velocity
    *
    releaseGain;
}


// ============================================================
// AUDIO UPDATE
// ============================================================

void updateAudio() {

  unsigned long now =
    micros();


  if (
    now -
    lastSampleMicros
    <
    SAMPLE_PERIOD_US
  ) {

    return;
  }


  lastSampleMicros =
    now;


  float mix =
    0;


  int activeVoices =
    0;


  for (
    int i = 0;
    i < MAX_VOICES;
    i++
  ) {

    if (
      voices[i].active
    ) {

      mix +=
        generateVoiceSample(
          i
        );


      activeVoices++;
    }
  }


  if (
    activeVoices > 1
  ) {

    mix /=
      sqrt(
        (float)activeVoices
      );
  }


  // =========================================================
  // TREMOLO
  // =========================================================

  float time =
    millis()
    /
    1000.0;


  float trem =
    (
      sin(
        2.0 *
        PI *
        tremoloSpeed *
        time
      )
      +
      1.0
    )
    *
    0.5;


  float tremoloGain =
    1.0
    -
    tremoloDepth
    *
    0.90
    *
    trem;


  mix *=
    tremoloGain;


  // =========================================================
  // LIMITER
  // =========================================================

  if (
    mix > 1.0
  ) {

    mix =
      1.0;
  }


  if (
    mix < -1.0
  ) {

    mix =
      -1.0;
  }


  int output =
    128
    +
    (
      mix * 110
    );


  output =
    constrain(
      output,
      0,
      255
    );


  dacWrite(
    AUDIO_PIN,
    output
  );
}


// ============================================================
// BLE MIDI CALLBACK
// ============================================================

void midiNotify(
  NimBLERemoteCharacteristic* pCharacteristic,
  uint8_t* data,
  size_t length,
  bool isNotify
) {

  if (
    length < 3
  ) {

    return;
  }


  // =========================================================
  // QUI NON GENERIAMO PIU' AUDIO
  //
  // SOLO DECODIFICA + QUEUE
  // =========================================================

  size_t i =
    2;


  uint8_t runningStatus =
    0;


  while (
    i < length
  ) {

    uint8_t b =
      data[i];


    if (
      b >= 0x80 &&
      b <= 0xEF
    ) {

      runningStatus =
        b;

      i++;
    }


    if (
      runningStatus == 0
    ) {

      i++;

      continue;
    }


    uint8_t command =
      runningStatus
      &
      0xF0;


    // =======================================================
    // 3-BYTE MIDI
    // =======================================================

    if (
      command == 0x80 ||
      command == 0x90 ||
      command == 0xA0 ||
      command == 0xB0 ||
      command == 0xE0
    ) {

      if (
        i + 1 >= length
      ) {

        break;
      }


      if (
        data[i] >= 0x80
      ) {

        continue;
      }


      uint8_t d1 =
        data[i++];


      if (
        i >= length
      ) {

        break;
      }


      if (
        data[i] >= 0x80
      ) {

        continue;
      }


      uint8_t d2 =
        data[i++];


      queueMidiMessage(
        runningStatus,
        d1,
        d2
      );
    }


    // =======================================================
    // 2-BYTE MIDI
    // =======================================================

    else if (
      command == 0xC0 ||
      command == 0xD0
    ) {

      if (
        i >= length
      ) {

        break;
      }


      if (
        data[i] >= 0x80
      ) {

        continue;
      }


      uint8_t d1 =
        data[i++];


      queueMidiMessage(
        runningStatus,
        d1,
        0
      );
    }


    else {

      i++;
    }
  }
}


// ============================================================
// BLE CONNECT
// ============================================================

bool connectMidi() {

  Serial.println();

  Serial.print(
    "Connessione M-VAVE: "
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
    !client->connect(
      address
    )
  ) {

    Serial.println(
      "BLE CONNECTION FAILED"
    );

    return false;
  }


  Serial.println(
    "BLE CONNECTED"
  );


  NimBLERemoteService* service =
    client->getService(
      midiServiceUUID
    );


  if (
    !service
  ) {

    Serial.println(
      "MIDI SERVICE NOT FOUND"
    );

    return false;
  }


  midiChar =
    service->getCharacteristic(
      midiCharUUID
    );


  if (
    !midiChar
  ) {

    Serial.println(
      "MIDI CHARACTERISTIC NOT FOUND"
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

      Serial.println(
        "MIDI READY"
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

  Serial.begin(
    115200
  );


  delay(
    1000
  );


  // =========================================================
  // SINE TABLE
  // =========================================================

  for (
    int i = 0;
    i < 256;
    i++
  ) {

    sineTable[i] =
      (int8_t)(
        sin(
          2.0 *
          PI *
          i /
          256.0
        )
        *
        127.0
      );
  }


  // =========================================================
  // VOICES
  // =========================================================

  for (
    int i = 0;
    i < MAX_VOICES;
    i++
  ) {

    voices[i].active =
      false;

    voices[i].releasing =
      false;

    voices[i].releaseRequested =
      false;
  }


  // =========================================================
  // TRACKS
  // =========================================================

  for (
    int t = 0;
    t < NUM_TRACKS;
    t++
  ) {

    tracks[t].eventCount =
      0;

    tracks[t].recording =
      false;

    tracks[t].playing =
      false;

    tracks[t].duration =
      0;

    tracks[t].playhead =
      0;

    tracks[t].playbackIndex =
      0;
  }


  midiQueueRead =
    0;

  midiQueueWrite =
    0;


  dacWrite(
    AUDIO_PIN,
    128
  );


  Serial.println();

  Serial.println(
    "========================================"
  );

  Serial.println(
    " ESP32 BLE SYNTH + MIDI QUEUE + LOOPER"
  );

  Serial.println(
    "========================================"
  );


  Serial.println(
    "KEYBOARD = CH1"
  );

  Serial.println(
    "VELOCITY = FIXED 110"
  );

  Serial.println(
    "MIN NOTE = 120 ms"
  );

  Serial.println(
    "REC = 40 41 42 43 48 49 50"
  );

  Serial.println(
    "PLAY = 36 37 38 39 44 45 46"
  );

  Serial.println(
    "NEXT INSTRUMENT = CH10 NOTE51"
  );

  Serial.println(
    "TREMOLO = CH1 CC30"
  );


  Serial.println();

  Serial.print(
    "STRUMENTO: "
  );

  Serial.println(
    instrumentNames[
      currentInstrument
    ]
  );


  NimBLEDevice::init(
    "ESP32-MULTILOOPER"
  );


  connectMidi();


  lastSampleMicros =
    micros();
}


// ============================================================
// LOOP
// ============================================================

void loop() {

  // =========================================================
  // 1. MIDI
  // =========================================================

  processMidiQueue();


  // =========================================================
  // 2. LOOPS
  // =========================================================

  updateLoops();


  // =========================================================
  // 3. AUDIO
  // =========================================================

  updateAudio();


  // =========================================================
  // BLE RECONNECT
  // =========================================================

  if (
    client != nullptr &&
    !client->isConnected()
  ) {

    dacWrite(
      AUDIO_PIN,
      128
    );


    for (
      int i = 0;
      i < MAX_VOICES;
      i++
    ) {

      voices[i].active =
        false;
    }


    midiQueueRead =
      0;

    midiQueueWrite =
      0;


    Serial.println(
      "M-VAVE DISCONNECTED"
    );


    delay(
      500
    );


    connectMidi();
  }
}
