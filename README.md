# ESP32 BLE MIDI Synth & Looper

This repository contains a set of Arduino sketches for building a standalone BLE MIDI synthesizer and looper based on the ESP32.

The project is designed to work with BLE MIDI controllers such as the M-VAVE and does not require a PC or smartphone during normal use.

## Included sketches

### 1. BLE MAC Scanner
Scans nearby BLE devices and prints their MAC address, device name, and RSSI value to the Serial Monitor.

This sketch is useful for finding the MAC address of the BLE MIDI controller before connecting it to the ESP32.

### 2. MIDI Mapper
Connects to the BLE MIDI controller and prints the received MIDI messages to the Serial Monitor.

It can be used to identify and map:

- Keyboard notes
- MIDI channels
- Note On / Note Off events
- Velocity values
- Control Change messages
- Buttons
- Knobs
- Pads
- Other MIDI controls

This makes it possible to build a complete control map for the connected MIDI device.

### 3. ESP32 Retro Synth & 7-Track Looper
Turns the ESP32 into a standalone polyphonic BLE MIDI synthesizer and multi-track MIDI looper.

Main features:

- Direct BLE MIDI connection
- No PC or smartphone required
- Polyphonic synthesis
- 7 independent MIDI loop tracks
- Multiple synthesized instruments
- Retro 8-bit style sounds
- Bass, organ, lead, bell and pluck sounds
- Kick, snare, hi-hat, tom and clap percussion
- Drum kit mode
- Tremolo and vibrato effects
- MIDI control mapping
- Real-time MIDI recording and loop playback

## Audio Output

The project uses the internal DAC of the classic ESP32.

Audio is generated directly from:

GPIO25

GPIO25 can be connected to the AUX / LINE input of a powered speaker or external amplifier.

A simple connection is:

ESP32 GPIO25 -> coupling capacitor -> AUX / amplifier input  
ESP32 GND -> AUX / amplifier GND

A capacitor between approximately 1 µF and 10 µF can be used for AC coupling.

The ESP32 DAC output should be connected to a powered speaker, active monitor, AUX input, or external amplifier.

Do not connect a passive 4 Ω or 8 Ω speaker directly to GPIO25.

## How it works

The BLE MIDI controller sends MIDI commands directly to the ESP32.

The ESP32:

1. Receives the BLE MIDI messages
2. Decodes Note On, Note Off and Control Change messages
3. Generates the sound in real time
4. Mixes multiple voices for polyphonic playback
5. Records MIDI events into the internal loop tracks
6. Sends the generated audio to the ESP32 internal DAC on GPIO25

The synthesizer does not play pre-recorded audio samples for its basic sounds. Most instruments are generated algorithmically in real time, giving the project a characteristic retro and 8-bit sound.

## Hardware

- ESP32 with internal DAC support
- BLE MIDI controller
- Powered speaker or external audio amplifier
- Optional coupling capacitor
- USB power supply

## Project Goal

The goal of this project is to turn a low-cost ESP32 into a compact standalone BLE MIDI instrument, combining a retro sound generator, polyphonic synthesizer and multi-track looper in a single embedded device.

The M-VAVE SMK-25II is available in aliexpress n this link : 

https://www.alibaba.com/pla/Portable-Pad-MidiKeyboard-Controller-Factory-Supply-Arrangement_1601727746339.html?mark=google_shopping&biz=pla&searchText=electronic+organ&product_id=1601727746339&pcy=es_es&src=sem_ggl&field=UG&from=sem_ggl&cmpgn=24024053425&adgrp=201850921841&fditm=&tgt=pla-2492519672538&locintrst=&locphyscl=9218262&mtchtyp=&ntwrk=g&device=c&dvcmdl=&creative=816372190514&plcmnt=&plcmntcat=&aceid=&position=&gad_source=1&gad_campaignid=24024053425&gbraid=0AAAAAD8m77q0seVh8GnIcAJDMSjdY5SH_&gclid=Cj0KCQjw5vLVBhCiARIsAD56SFJjh_CnlBsnNFpaKY8HEV8XigcUXRvI4F_Gps1ZrnITcdpP_daV7ZcaAuP1EALw_wcB
