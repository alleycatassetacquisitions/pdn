//
// Created by Elli Furedy on 10/9/2024.
//

#include "device/pdn-serial.hpp"
#include <HardwareSerial.h>

#include "device-constants.hpp"

PDNSerialOut::PDNSerialOut() {
}

void PDNSerialOut::begin() {
    pinMode(TXt, OUTPUT);
    pinMode(TXr, INPUT);

    Serial1.begin(BAUDRATE, SERIAL_8E2, TXr, TXt, true);
}

int PDNSerialOut::availableForWrite() {
    return Serial1.availableForWrite();
}

int PDNSerialOut::available() {
    return Serial1.available();
}

int PDNSerialOut::peek() {
    return Serial1.peek();
}

int PDNSerialOut::read() {
    return Serial1.read();
}

string PDNSerialOut::readStringUntil(char terminator) {
    return string(Serial1.readStringUntil(terminator).c_str());
}

void PDNSerialOut::print(char msg) {
    Serial1.print(msg);
}

void PDNSerialOut::println(char *msg) {
    Serial1.println(msg);
}

void PDNSerialOut::println(string msg) {
    Serial1.println(msg.c_str());
}

void PDNSerialOut::flush() {
    Serial1.flush();
}

PDNSerialIn::PDNSerialIn() {
}


void PDNSerialIn::begin() {
    pinMode(RXt, OUTPUT);
    pinMode(RXr, INPUT);

    Serial2.begin(BAUDRATE, SERIAL_8E2, RXr, RXt, true);
}

int PDNSerialIn::availableForWrite() {
    return Serial2.availableForWrite();
}

int PDNSerialIn::available() {
    return Serial2.available();
}

int PDNSerialIn::peek() {
    return Serial2.peek();
}

int PDNSerialIn::read() {
    return Serial2.read();
}

string PDNSerialIn::readStringUntil(char terminator) {
    return string(Serial2.readStringUntil(terminator).c_str());
}

void PDNSerialIn::print(char msg) {
    Serial2.print(msg);
}

void PDNSerialIn::println(char *msg) {
    Serial2.println(msg);
}

void PDNSerialIn::println(string msg) {
    Serial2.println(msg.c_str());
}

void PDNSerialIn::flush() {
    Serial2.flush();
}
