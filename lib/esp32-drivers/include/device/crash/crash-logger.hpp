#pragma once

#include <string>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <strings.h>
#include <cstdlib>
#include <cstdarg>
#include <Arduino.h>
#include <esp_system.h>
#include <esp_core_dump.h>
#include <driver/usb_serial_jtag.h>
#include <ArduinoJson.h>
#include "device/drivers/esp32-s3/esp32-s3-prefs-driver.hpp"
#include "device/drivers/esp32-s3/esp32-s3-http-client-driver.hpp"
#include "device/drivers/esp32-s3/esp-now-driver.hpp"

#ifndef FIRMWARE_COMMIT_HASH
#define FIRMWARE_COMMIT_HASH "unknown"
#endif

constexpr size_t  TASK_NAME_LENGTH   = 16;
constexpr size_t  COMMIT_HASH_LENGTH = 9;
constexpr uint8_t MAX_CRASH_ENTRIES = 10;

/** NVS namespace for persisted crash records (shared with Esp32S3PrefsDriver registration). */
inline constexpr const char CRASH_LOG_NAMESPACE[] = "crashlog";

/** USB serial command (line-terminated) to dump the crash log via flushToSerial(). */
inline constexpr const char CRASH_LOG_SERIAL_COMMAND[] = "CRASHLOG";

struct CrashRecord {
    // Monotonic crash count for this device (1 = first crash ever recorded).
    uint32_t crashNumber;

    // millis() at the moment the crash is detected on reboot.
    uint32_t timestamp;

    uint8_t resetReason;
    uint32_t programCounter;
    uint32_t exceptionCause;
    char taskName[TASK_NAME_LENGTH];
    char commitHash[COMMIT_HASH_LENGTH];
};

struct CrashPacket {
    uint32_t crashNumber;
    uint32_t timestamp;
    uint8_t  resetReason;
    uint32_t programCounter;
    uint32_t exceptionCause;
    char     taskName[TASK_NAME_LENGTH];
    char     commitHash[COMMIT_HASH_LENGTH];
} __attribute__((packed));

/**
 * Captures and persists crash records across reboots, then transmits when wireless is up.
 * Register CRASH_LOG_NAMESPACE on the shared Esp32S3PrefsDriver; call capture() after
 * storage is initialized; call transmitPending() after ESP-NOW is ready; poll serial in loop().
 */
class CrashLogger {
public:
    explicit CrashLogger(Esp32S3PrefsDriver* prefsDriver, Esp32S3HttpClient* httpClientDriver)
        : prefsDriver(prefsDriver)
        , httpClientDriver(httpClientDriver)
        , espNowDriver(nullptr)
        , hasCaptured(false)
        , useHttp(true)
        , serialCommandLength(0) {
        serialCommandBuffer[0] = '\0';
    }

    explicit CrashLogger(Esp32S3PrefsDriver* prefsDriver, EspNowDriver* espNowDriver)
        : prefsDriver(prefsDriver)
        , httpClientDriver(nullptr)
        , espNowDriver(espNowDriver)
        , hasCaptured(false)
        , useHttp(false)
        , serialCommandLength(0) {
        serialCommandBuffer[0] = '\0';
    }

    void capture() {
        if (hasCaptured) return;
        hasCaptured = true;

        esp_reset_reason_t reason = esp_reset_reason();
        if (isCleanReason(reason)) return;

        CrashRecord rec{};
        rec.resetReason = static_cast<uint8_t>(reason);
        rec.timestamp   = millis();

        bool hadCoreDumpSummary = false;
        esp_core_dump_summary_t summary{};
        if (esp_core_dump_get_summary(&summary) == ESP_OK) {
            hadCoreDumpSummary = true;
            rec.programCounter = summary.exc_pc;
            rec.exceptionCause = summary.ex_info.exc_cause;
            strncpy(rec.taskName, summary.exc_task, TASK_NAME_LENGTH - 1);
            rec.taskName[TASK_NAME_LENGTH - 1] = '\0';
        } else {
            rec.programCounter = 0;
            rec.exceptionCause = 0;
            strncpy(rec.taskName, "unknown", TASK_NAME_LENGTH - 1);
        }

        copyBuildCommitHash(rec.commitHash);

        persistRecord(rec);
        LOG_I(CRASH_LOG_TAG, "Captured crash #%lu: reason=%u pc=0x%08X ec=%u task=%s commit=%s",
              static_cast<unsigned long>(rec.crashNumber),
              rec.resetReason, rec.programCounter, rec.exceptionCause, rec.taskName, rec.commitHash);

        eraseCoreDumpAfterCapture(hadCoreDumpSummary);
    }

    bool hasPending() const {
        return readSentSeq() < readCrashSeq();
    }

    void transmitPending() {
        if (!hasPending()) return;
        useHttp ? transmitHttp() : transmitEspNow();
    }

    void flushToSerial() const {
        const uint32_t crashSeq = readCrashSeq();
        const uint32_t sentSeq  = readSentSeq();

        usbMonitorWrite("\n=== CRASH LOG ===\n");

        if (crashSeq == 0) {
            usbMonitorWrite("No crash records stored.\n=================\n\n");
            return;
        }

        usbMonitorPrintf("Device crash count: %lu (%lu pending transmission)\n",
                         static_cast<unsigned long>(crashSeq),
                         static_cast<unsigned long>(crashSeq - sentSeq));

        const uint32_t firstToShow = crashSeq > MAX_CRASH_ENTRIES
            ? crashSeq - MAX_CRASH_ENTRIES + 1
            : 1;

        for (uint32_t n = firstToShow; n <= crashSeq; n++) {
            CrashRecord rec{};
            if (!loadRecordByNumber(n, rec)) {
                usbMonitorPrintf("[#%lu] <not retained in ring buffer>\n", static_cast<unsigned long>(n));
                continue;
            }

            const char* status = (n <= sentSeq) ? "sent   " : "pending";

            usbMonitorPrintf(
                "[#%lu][%s] reason=%-2u %-12s | ts=%ums | pc=0x%08X | ec=%-2u %-18s | task=%s | commit=%s\n",
                static_cast<unsigned long>(rec.crashNumber),
                status,
                rec.resetReason,
                resetReasonName(rec.resetReason),
                rec.timestamp,
                rec.programCounter,
                rec.exceptionCause,
                exceptionCauseName(rec.exceptionCause),
                rec.taskName,
                rec.commitHash[0] != '\0' ? rec.commitHash : "unknown"
            );
        }

        usbMonitorWrite("=================\n\n");
    }

    /**
     * Poll the USB Serial/JTAG port (same link as esp_log) for the CRASHLOG command.
     */
    void pollSerialCommand() {
        ensureUsbSerialJtagDriver();

        uint8_t chunk[16];
        const int bytesRead = usb_serial_jtag_read_bytes(chunk, sizeof(chunk), 0);
        if (bytesRead <= 0) {
            return;
        }

        for (int i = 0; i < bytesRead; i++) {
            processSerialInputChar(static_cast<char>(chunk[i]));
        }
    }

private:
    void processSerialInputChar(char c) {
        if (c == '\n' || c == '\r') {
            tryExecuteSerialCommand();
            serialCommandLength = 0;
            return;
        }

        if (c == 127 || c == 8) {
            if (serialCommandLength > 0) {
                serialCommandLength--;
            }
            return;
        }

        if (c == 3) {
            serialCommandLength = 0;
            return;
        }

        if (c < 32) {
            return;
        }

        if (serialCommandLength >= sizeof(serialCommandBuffer) - 1) {
            serialCommandLength = 0;
            return;
        }

        serialCommandBuffer[serialCommandLength++] = c;
        serialCommandBuffer[serialCommandLength] = '\0';

        if (isCrashLogSerialCommand(serialCommandBuffer)) {
            tryExecuteSerialCommand();
            serialCommandLength = 0;
        }
    }

    void tryExecuteSerialCommand() {
        if (serialCommandLength == 0) return;

        serialCommandBuffer[serialCommandLength] = '\0';
        trimSerialCommandBuffer();

        if (isCrashLogSerialCommand(serialCommandBuffer)) {
            flushToSerial();
            usbMonitorWrite("(crash log dump complete)\n");
        }
    }

    static void ensureUsbSerialJtagDriver() {
        static bool driverReady = false;
        if (driverReady) {
            return;
        }
        usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
        esp_err_t err = usb_serial_jtag_driver_install(&cfg);
        if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
            driverReady = true;
        }
    }

    static void usbMonitorWrite(const char* text) {
        if (text == nullptr) {
            return;
        }
        ensureUsbSerialJtagDriver();
        const size_t len = strlen(text);
        if (len == 0) {
            return;
        }
        usb_serial_jtag_write_bytes(reinterpret_cast<const uint8_t*>(text), len, pdMS_TO_TICKS(200));
    }

    static void usbMonitorPrintf(const char* format, ...) {
        char buffer[280];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        usbMonitorWrite(buffer);
    }

    static void trimSerialCommandBuffer(char* buf, size_t& length) {
        while (length > 0 && (buf[length - 1] == ' ' || buf[length - 1] == '\t')) {
            buf[--length] = '\0';
        }
        size_t start = 0;
        while (start < length && (buf[start] == ' ' || buf[start] == '\t')) {
            start++;
        }
        if (start > 0) {
            size_t newLen = length - start;
            memmove(buf, buf + start, newLen);
            length = newLen;
            buf[length] = '\0';
        }
    }

    void trimSerialCommandBuffer() {
        trimSerialCommandBuffer(serialCommandBuffer, serialCommandLength);
    }

    static constexpr const char* SEQ_KEY     = "seq";
    static constexpr const char* SENT_SEQ_KEY = "sentSeq";

    bool isCleanReason(esp_reset_reason_t reason) const {
        return reason == ESP_RST_POWERON
            || reason == ESP_RST_SW
            || reason == ESP_RST_EXT
            || reason == ESP_RST_DEEPSLEEP
            || reason == ESP_RST_USB
            || reason == ESP_RST_JTAG;
    }

    uint32_t readCrashSeq() const {
        return readUint32Pref(SEQ_KEY);
    }

    uint32_t readSentSeq() const {
        return readUint32Pref(SENT_SEQ_KEY);
    }

    void writeCrashSeq(uint32_t value) {
        writeUint32Pref(SEQ_KEY, value);
    }

    void writeSentSeq(uint32_t value) {
        writeUint32Pref(SENT_SEQ_KEY, value);
    }

    uint32_t readUint32Pref(const char* key) const {
        std::string s = prefsDriver->read(CRASH_LOG_NAMESPACE, std::string(key), "0");
        return static_cast<uint32_t>(strtoul(s.c_str(), nullptr, 10));
    }

    void writeUint32Pref(const char* key, uint32_t value) {
        char buf[11];
        snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(value));
        prefsDriver->write(CRASH_LOG_NAMESPACE, std::string(key), std::string(buf));
    }

    void persistRecord(CrashRecord& rec) {
        const uint32_t crashNumber = readCrashSeq() + 1;
        rec.crashNumber = crashNumber;

        char slotKey[12];
        snprintf(slotKey, sizeof(slotKey), "rec_%u", static_cast<unsigned>(crashNumber % MAX_CRASH_ENTRIES));

        char jsonBuf[360];
        serializeRecord(rec, jsonBuf, sizeof(jsonBuf));
        prefsDriver->write(CRASH_LOG_NAMESPACE, std::string(slotKey), std::string(jsonBuf));
        writeCrashSeq(crashNumber);
    }

    bool loadRecordByNumber(uint32_t crashNumber, CrashRecord& out) const {
        char slotKey[12];
        snprintf(slotKey, sizeof(slotKey), "rec_%u", static_cast<unsigned>(crashNumber % MAX_CRASH_ENTRIES));
        std::string json = prefsDriver->read(CRASH_LOG_NAMESPACE, std::string(slotKey), "");
        if (json.empty()) return false;
        if (!deserializeRecord(json, out)) return false;
        return out.crashNumber == crashNumber;
    }

    void serializeRecord(const CrashRecord& rec, char* buf, size_t bufSize) const {
        JsonDocument doc;
        doc["id"]              = rec.crashNumber;
        doc["timestamp"]       = rec.timestamp;
        doc["resetReason"]     = rec.resetReason;
        doc["programCounter"]  = rec.programCounter;
        doc["exceptionCause"]  = rec.exceptionCause;
        doc["task"]        = rec.taskName;
        doc["commitHash"]  = rec.commitHash;
        serializeJson(doc, buf, bufSize);
    }

    bool deserializeRecord(const std::string& jsonStr, CrashRecord& out) const {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, jsonStr.c_str());
        if (err) return false;

        out.crashNumber    = doc["id"] | 0u;
        out.timestamp      = doc["timestamp"] | doc["ts"] | 0u;
        out.resetReason    = doc["resetReason"] | doc["rr"] | static_cast<uint8_t>(0);
        out.programCounter = doc["programCounter"] | doc["pc"] | 0u;
        out.exceptionCause = doc["exceptionCause"] | doc["ec"] | 0u;
        const char* task   = doc["task"] | "unknown";
        strncpy(out.taskName, task, TASK_NAME_LENGTH - 1);
        out.taskName[TASK_NAME_LENGTH - 1] = '\0';
        const char* commitHash = doc["commitHash"] | "";
        strncpy(out.commitHash, commitHash, COMMIT_HASH_LENGTH - 1);
        out.commitHash[COMMIT_HASH_LENGTH - 1] = '\0';
        return true;
    }

    static void copyBuildCommitHash(char* dest) {
        strncpy(dest, FIRMWARE_COMMIT_HASH, COMMIT_HASH_LENGTH - 1);
        dest[COMMIT_HASH_LENGTH - 1] = '\0';
    }

    void eraseCoreDumpAfterCapture(bool hadCoreDumpSummary) {
        if (esp_core_dump_image_erase() == ESP_OK) {
            LOG_I(CRASH_LOG_TAG, "Erased core dump partition after capture");
        } else if (hadCoreDumpSummary) {
            LOG_W(CRASH_LOG_TAG, "Core dump summary was read but erase failed");
        }
    }

    void transmitHttp() {
        LOG_W(CRASH_LOG_TAG, "HTTP crash transmission not yet implemented.");
    }

    void transmitEspNow() {
        if (!espNowDriver) return;

        const uint32_t crashSeq = readCrashSeq();
        uint32_t sentSeq = readSentSeq();

        for (uint32_t n = sentSeq + 1; n <= crashSeq; n++) {
            CrashRecord rec{};
            if (!loadRecordByNumber(n, rec)) {
                LOG_W(CRASH_LOG_TAG, "Crash #%lu not in ring buffer — skipping", static_cast<unsigned long>(n));
                writeSentSeq(n);
                continue;
            }

            CrashPacket pkt{};
            pkt.crashNumber    = rec.crashNumber;
            pkt.timestamp      = rec.timestamp;
            pkt.resetReason    = rec.resetReason;
            pkt.programCounter = rec.programCounter;
            pkt.exceptionCause = rec.exceptionCause;
            memcpy(pkt.taskName, rec.taskName, TASK_NAME_LENGTH);
            memcpy(pkt.commitHash, rec.commitHash, COMMIT_HASH_LENGTH);

            const int result = espNowDriver->sendData(
                PEER_BROADCAST_ADDR,
                PktType::kCrashLog,
                reinterpret_cast<const uint8_t*>(&pkt),
                sizeof(CrashPacket)
            );

            if (result == 0) {
                writeSentSeq(n);
                LOG_I(CRASH_LOG_TAG, "Queued crash #%lu for broadcast", static_cast<unsigned long>(n));
            } else {
                LOG_E(CRASH_LOG_TAG, "Failed to queue crash #%lu — will retry on next boot",
                      static_cast<unsigned long>(n));
                break;
            }
        }
    }

    static bool isCrashLogSerialCommand(const char* line) {
        if (line == nullptr) return false;
        return strcasecmp(line, CRASH_LOG_SERIAL_COMMAND) == 0;
    }

    Esp32S3PrefsDriver* prefsDriver;
    Esp32S3HttpClient*  httpClientDriver;
    EspNowDriver*       espNowDriver;
    bool                hasCaptured;
    bool                useHttp;

    char   serialCommandBuffer[32];
    size_t serialCommandLength;

    static const char* const CRASH_LOG_TAG;

    static const char* resetReasonName(uint8_t reason) {
        switch (static_cast<esp_reset_reason_t>(reason)) {
            case ESP_RST_UNKNOWN:    return "UNKNOWN";
            case ESP_RST_POWERON:    return "POWER_ON";
            case ESP_RST_EXT:        return "EXT_PIN";
            case ESP_RST_SW:         return "SW_RESET";
            case ESP_RST_PANIC:      return "PANIC";
            case ESP_RST_INT_WDT:    return "INT_WDT";
            case ESP_RST_TASK_WDT:   return "TASK_WDT";
            case ESP_RST_WDT:        return "WDT";
            case ESP_RST_DEEPSLEEP:  return "DEEP_SLEEP";
            case ESP_RST_BROWNOUT:   return "BROWNOUT";
            case ESP_RST_SDIO:       return "SDIO";
            case ESP_RST_USB:        return "USB";
            case ESP_RST_JTAG:       return "JTAG";
            case ESP_RST_EFUSE:      return "EFUSE";
            case ESP_RST_PWR_GLITCH: return "PWR_GLITCH";
            case ESP_RST_CPU_LOCKUP: return "CPU_LOCKUP";
            default:                 return "?";
        }
    }

    static const char* exceptionCauseName(uint32_t cause) {
        switch (cause) {
            case 0:  return "IllegalInstruction";
            case 5:  return "Alloca";
            case 6:  return "DivideByZero";
            case 9:  return "LoadAlignment";
            case 25: return "Unhandled";
            case 28: return "LoadProhibited";
            case 29: return "StoreProhibited";
            default: return "?";
        }
    }
};

inline const char* const CrashLogger::CRASH_LOG_TAG = "CrashLogger";
