#include "webui.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <sys/time.h>
#include "config.h"
#include "eventlog.h"
#include "index_html.h"

static const time_t VALID_EPOCH = 1700000000; // 2023-11

bool timeOffsetForBoot(uint16_t boot, int64_t &offset) {
    char key[12];
    snprintf(key, sizeof(key), "o%u", boot);
    Preferences p;
    if (!p.begin("rd04", true)) return false;
    bool found = p.isKey(key);
    if (found) offset = p.getLong64(key, 0);
    p.end();
    return found;
}

void runAccessPoint(uint16_t bootId) {
    WebServer server(80);
    DNSServer dns;
    bool sleepRequested = false;
    unsigned long lastActivity = millis();

    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    dns.start(53, "*", WiFi.softAPIP()); // captive portal: any hostname opens the page

    auto touch = [&]() { lastActivity = millis(); };

    server.on("/", HTTP_GET, [&]() {
        touch();
        server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
    });

    server.on("/api/status", HTTP_GET, [&]() {
        touch();
        time_t now = time(nullptr);
        char json[200];
        snprintf(json, sizeof(json),
                 "{\"now\":%ld,\"timeValid\":%s,\"boot\":%u,\"count\":%u,\"capacity\":%u,\"idleTimeout\":%d}",
                 (long)now, now > VALID_EPOCH ? "true" : "false", bootId,
                 EventLog::count(), EventLog::capacity(), AP_IDLE_TIMEOUT_S);
        server.send(200, "application/json", json);
    });

    server.on("/api/time", HTTP_POST, [&]() {
        touch();
        int64_t ms = atoll(server.arg("ms").c_str());
        if (ms < (int64_t)VALID_EPOCH * 1000) {
            server.send(400, "text/plain", "bad time");
            return;
        }
        time_t now = time(nullptr);
        if (now < VALID_EPOCH) {
            // Events already logged in this session have "seconds since boot" timestamps; remember how to fix them
            char key[12];
            snprintf(key, sizeof(key), "o%u", bootId);
            Preferences p;
            p.begin("rd04", false);
            p.putLong64(key, ms / 1000 - (int64_t)now);
            p.end();
        }
        struct timeval tv = { (time_t)(ms / 1000), (suseconds_t)((ms % 1000) * 1000) };
        settimeofday(&tv, nullptr);
        server.send(200, "text/plain", "ok");
    });

    server.on("/api/events", HTTP_GET, [&]() {
        touch();
        uint32_t limit = server.hasArg("limit") ? strtoul(server.arg("limit").c_str(), nullptr, 10) : 500;
        if (server.hasArg("dl"))
            server.sendHeader("Content-Disposition", "attachment; filename=\"rd04_events.csv\"");
        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.send(200, "text/csv", "ts,time_valid,seq,boot\n");

        String chunk;
        chunk.reserve(1600);
        int cachedBoot = -1;
        bool cachedFound = false;
        int64_t cachedOffset = 0;

        EventLog::forEachNewestFirst(limit, [&](const EventRec &r) {
            int64_t ts = r.ts;
            bool valid = r.flags & EVT_FLAG_TIME_VALID;
            if (!valid) {
                if (r.boot != cachedBoot) {
                    cachedBoot = r.boot;
                    cachedFound = timeOffsetForBoot(r.boot, cachedOffset);
                }
                if (cachedFound) {
                    ts += cachedOffset;
                    valid = true;
                }
            }
            char line[48];
            snprintf(line, sizeof(line), "%lld,%d,%u,%u\n", (long long)ts, valid ? 1 : 0, r.seq, r.boot);
            chunk += line;
            if (chunk.length() > 1400) {
                server.sendContent(chunk);
                chunk = "";
            }
            return true;
        });
        if (chunk.length()) server.sendContent(chunk);
        server.sendContent("");
    });

    server.on("/api/clear", HTTP_POST, [&]() {
        touch();
        bool ok = EventLog::clear();
        server.send(ok ? 200 : 500, "text/plain", ok ? "ok" : "error");
    });

    server.on("/api/sleep", HTTP_POST, [&]() {
        server.send(200, "text/plain", "ok");
        sleepRequested = true;
    });

    server.onNotFound([&]() {
        server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
        server.send(302, "text/plain", "");
    });

    server.begin();
    pinMode(PIN_LED, OUTPUT);
    pinMode(PIN_BOOT, INPUT_PULLUP);

    unsigned long sleepAt = 0;
    unsigned long buttonReleasedSince = 0; // 0 = not yet seen released (ignore the press that opened the AP)
    unsigned long buttonPressedSince = 0;
    while (true) {
        dns.processNextRequest();
        server.handleClient();

        digitalWrite(PIN_LED, (millis() / 500) % 2 ? HIGH : LOW); // slow blink = AP is open

        // Another BOOT press closes the AP
        if (digitalRead(PIN_BOOT) == HIGH) {
            buttonPressedSince = 0;
            if (!buttonReleasedSince) buttonReleasedSince = millis();
        } else if (buttonReleasedSince && millis() - buttonReleasedSince > 300) {
            if (!buttonPressedSince) buttonPressedSince = millis();
            if (millis() - buttonPressedSince > 50) sleepRequested = true;
        }

        if (sleepRequested && !sleepAt) sleepAt = millis() + 500; // let the reply go out
        if (sleepAt && millis() > sleepAt) break;
        if (millis() - lastActivity > (unsigned long)AP_IDLE_TIMEOUT_S * 1000UL) break;
        delay(2);
    }

    server.stop();
    dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    digitalWrite(PIN_LED, HIGH);
    pinMode(PIN_LED, INPUT);
}
