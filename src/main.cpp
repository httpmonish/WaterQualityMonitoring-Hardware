/**
 * ============================================================================
 * Project: Smart Water Quality Monitoring System
 * Title  : IoT-Based Smart Water Quality Monitoring and Automated Response System
 * Target : ESP32 DevKit C V4 (Wokwi Simulation Environment)
 * ============================================================================
 * 
 * ----------------------------------------------------------------------------
 * HONESTY RULES & SIMULATION DISCLAIMERS:
 * 1. pH and turbidity are NOT really measured by physical electrochemical
 *    probes in this project. Two analog potentiometers EMULATE pH-sensor and
 *    turbidity-sensor voltage outputs in the Wokwi simulation.
 * 2. Thresholds defined herein are "configured prototype thresholds" for
 *    demonstration and testing purposes, NEVER universal drinking-water standards.
 * 3. The green LED is a "relay-controlled simulated pump actuator", NEVER a
 *    real high-voltage water pump.
 * ----------------------------------------------------------------------------
 */

#include <Arduino.h>
#include <WiFi.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ThingSpeak.h>

// ============================================================================
// SIMULATION & TELEMETRY FEATURE FLAGS (DEFAULT: ALL ENABLED)
// ============================================================================
#define ENABLE_CLOUD       1  // 1: Upload to ThingSpeak | 0: Local-only simulation
#define ENABLE_STATUS_LEDS 1  // 1: Enable 3-state status LEDs (GPIO 25, 27, 32)
#define ENABLE_BUZZER      1  // 1: Enable periodic alarm buzzer on POOR (GPIO 33)

// Actuator Configuration: Active-Low Relay Module
#define RELAY_ACTIVE_LOW true

// ============================================================================
// CLOUD & NETWORK CONFIGURATION
// ============================================================================
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";

// ThingSpeak Configuration (Replace with your actual ThingSpeak credentials)
const unsigned long THINGSPEAK_CHANNEL_ID = 0000000;              // <-- PLACEHOLDER: ENTER YOUR CHANNEL ID HERE
const char* THINGSPEAK_WRITE_API_KEY      = "YOUR_WRITE_API_KEY"; // <-- PLACEHOLDER: ENTER YOUR WRITE API KEY HERE

// Helper macro to verify if credentials are still placeholder defaults
#define IS_PLACEHOLDER_CREDENTIALS() (THINGSPEAK_CHANNEL_ID == 0000000 || strcmp(THINGSPEAK_WRITE_API_KEY, "YOUR_WRITE_API_KEY") == 0)

// ============================================================================
// HARDWARE PIN ASSIGNMENTS
// ============================================================================
// Core Sensor & Actuator Pins
const int PIN_PH_POT    = 34; // Emulated pH Sensor (Analog Potentiometer)
const int PIN_TURB_POT  = 35; // Emulated Turbidity Sensor (Analog Potentiometer)
const int PIN_DS18B20   = 4;  // DS18B20 OneWire Digital Temperature Sensor
const int PIN_RELAY     = 26; // Relay Module Input (Controls Simulated Pump LED)

// Visual Status Indicator Pins (Active-High)
#if ENABLE_STATUS_LEDS
const int PIN_LED_GOOD  = 25; // Green Status LED: Quality GOOD
const int PIN_LED_WARN  = 27; // Yellow Status LED: Quality WARNING
const int PIN_LED_POOR  = 32; // Red Status LED: Quality POOR
#endif

// Acoustic Alarm Pin
#if ENABLE_BUZZER
const int PIN_BUZZER    = 33; // Piezo Buzzer Output
const unsigned int BUZZER_FREQ_HZ = 2000;        // 2 kHz alert tone
const unsigned long BUZZER_BEEP_DURATION_MS = 150; // 150 ms pulse
#endif

// ============================================================================
// CONFIGURED PROTOTYPE THRESHOLDS
// (Configured for prototype demonstration, NOT universal drinking standards)
// ============================================================================
// pH Thresholds (Range: 0.0 - 14.0)
const float PH_GOOD_MIN    = 6.5f;
const float PH_GOOD_MAX    = 8.5f;
const float PH_WARN_LOW    = 6.0f;
const float PH_WARN_HIGH   = 9.0f;

// Turbidity Thresholds (Range: 0.0 - 100.0 NTU)
const float TURB_GOOD_MAX  = 20.0f;
const float TURB_WARN_MAX  = 40.0f;

// Temperature Thresholds (Range: Celsius)
const float TEMP_GOOD_MIN  = 15.0f;
const float TEMP_GOOD_MAX  = 35.0f;
const float TEMP_WARN_LOW  = 5.0f;
const float TEMP_WARN_HIGH = 40.0f;

// Quality Status Enumeration
enum WaterQualityStatus {
    QUALITY_GOOD    = 0,
    QUALITY_WARNING = 1,
    QUALITY_POOR    = 2
};

// ============================================================================
// OBJECT INSTANTIATIONS & GLOBAL STATE
// ============================================================================
OneWire oneWire(PIN_DS18B20);
DallasTemperature tempSensors(&oneWire);
WiFiClient wifiClient;

// Timing intervals (Non-blocking millis)
unsigned long lastSerialPrintTime = 0;
const unsigned long SERIAL_INTERVAL_MS = 2000;   // Print report every 2 seconds

unsigned long lastCloudUploadTime = 0;
const unsigned long CLOUD_INTERVAL_MS  = 20000;  // Upload to ThingSpeak every 20 seconds

// Actuator Hysteresis Configuration
// Pump turns ON immediately when POOR, turns OFF only after 2 consecutive non-POOR readings
const int PUMP_OFF_CONFIRM_COUNT = 2;
int consecutiveNonPoorCount = 0;
bool currentPumpActiveState = false;

// Measurement data structure
struct SensorReadings {
    int   rawPh;
    int   rawTurb;
    float ph;
    float turbidity;
    float temperature;
    bool  tempValid;
    WaterQualityStatus quality;
    bool  pumpActive;
    String reason;
};

SensorReadings currentReadings;

// ============================================================================
// HELPER FUNCTIONS: ACTUATOR & INDICATOR CONTROL
// ============================================================================
/**
 * @brief Sets the physical state of the relay actuator based on polarity.
 * @param turnOn True to activate the pump, False to deactivate.
 */
void setPumpState(bool turnOn) {
    if (RELAY_ACTIVE_LOW) {
        digitalWrite(PIN_RELAY, turnOn ? LOW : HIGH);
    } else {
        digitalWrite(PIN_RELAY, turnOn ? HIGH : LOW);
    }
}

/**
 * @brief Updates the three dedicated status LEDs so exactly one is ON.
 * @param quality Current evaluated water quality status.
 */
void updateStatusLEDs(WaterQualityStatus quality) {
#if ENABLE_STATUS_LEDS
    digitalWrite(PIN_LED_GOOD, (quality == QUALITY_GOOD)    ? HIGH : LOW);
    digitalWrite(PIN_LED_WARN, (quality == QUALITY_WARNING) ? HIGH : LOW);
    digitalWrite(PIN_LED_POOR, (quality == QUALITY_POOR)    ? HIGH : LOW);
#endif
}

/**
 * @brief Triggers acoustic alarm beep when quality is POOR.
 * @param quality Current evaluated water quality status.
 */
void updateBuzzer(WaterQualityStatus quality) {
#if ENABLE_BUZZER
    if (quality == QUALITY_POOR) {
        // Portable 2 kHz beep for about 150 ms (300 cycles). No tone()/LEDC needed.
        for (unsigned long i = 0; i < BUZZER_BEEP_DURATION_MS * 2; i++) {
            digitalWrite(PIN_BUZZER, HIGH);
            delayMicroseconds(250);
            digitalWrite(PIN_BUZZER, LOW);
            delayMicroseconds(250);
        }
    }
#endif
}


// ============================================================================
// HELPER FUNCTIONS: SENSOR ACQUISITION & CONVERSION
// ============================================================================
/**
 * @brief Reads an analog pin multiple times and returns the smoothed average.
 * @param pin The ADC GPIO pin to sample.
 * @param samples Number of consecutive samples to average.
 * @return Smoothed raw ADC value (0 - 4095).
 */
int readAveragedADC(int pin, int samples = 10) {
    long sum = 0;
    for (int i = 0; i < samples; i++) {
        sum += analogRead(pin);
        delayMicroseconds(200); // Brief settling interval
    }
    return (int)(sum / samples);
}

/**
 * @brief Acquires all sensor readings, emulates calibrated units, and handles errors.
 */
void acquireSensors(SensorReadings &readings) {
    // 1. Emulated pH: 12-bit ADC (0..4095) mapped to pH 0.0 - 14.0
    readings.rawPh = readAveragedADC(PIN_PH_POT, 10);
    readings.ph = ((float)readings.rawPh / 4095.0f) * 14.0f;

    // 2. Emulated Turbidity: 12-bit ADC mapped to 0.0 - 100.0 NTU
    readings.rawTurb = readAveragedADC(PIN_TURB_POT, 10);
    readings.turbidity = ((float)readings.rawTurb / 4095.0f) * 100.0f;

    // 3. Digital Temperature: DS18B20 via OneWire
    tempSensors.requestTemperatures();
    float t = tempSensors.getTempCByIndex(0);

    // Validate DS18B20 reading (DEVICE_DISCONNECTED_C is -127.0 C)
    if (t == DEVICE_DISCONNECTED_C || t < -55.0f || t > 125.0f) {
        readings.temperature = -127.0f;
        readings.tempValid = false;
    } else {
        readings.temperature = t;
        readings.tempValid = true;
    }
}

// ============================================================================
// EVALUATION LOGIC: THRESHOLDS & AUTOMATED RESPONSE
// ============================================================================
/**
 * @brief Evaluates water quality against configured prototype thresholds
 *        and determines pump actuator response with de-chatter hysteresis.
 */
void evaluateWaterQuality(SensorReadings &readings) {
    WaterQualityStatus phStatus   = QUALITY_GOOD;
    WaterQualityStatus turbStatus = QUALITY_GOOD;
    WaterQualityStatus tempStatus = QUALITY_GOOD;

    String reasons = "";

    // --- Evaluate pH ---
    if (readings.ph < PH_WARN_LOW || readings.ph > PH_WARN_HIGH) {
        phStatus = QUALITY_POOR;
        reasons += "pH outside configured range; ";
    } else if (readings.ph < PH_GOOD_MIN || readings.ph > PH_GOOD_MAX) {
        phStatus = QUALITY_WARNING;
        reasons += "pH outside configured range; ";
    }

    // --- Evaluate Turbidity ---
    if (readings.turbidity > TURB_WARN_MAX) {
        turbStatus = QUALITY_POOR;
        reasons += "High turbidity; ";
    } else if (readings.turbidity >= TURB_GOOD_MAX) {
        turbStatus = QUALITY_WARNING;
        reasons += "Elevated turbidity; ";
    }

    // --- Evaluate Temperature ---
    if (!readings.tempValid) {
        tempStatus = QUALITY_POOR;
        reasons += "Temperature sensor disconnected; ";
    } else if (readings.temperature < TEMP_WARN_LOW || readings.temperature > TEMP_WARN_HIGH) {
        tempStatus = QUALITY_POOR;
        reasons += "Temperature outside configured range; ";
    } else if (readings.temperature < TEMP_GOOD_MIN || readings.temperature > TEMP_GOOD_MAX) {
        tempStatus = QUALITY_WARNING;
        reasons += "Temperature outside configured range; ";
    }

    // Overall quality is the worst of the three (POOR > WARNING > GOOD)
    if (phStatus == QUALITY_POOR || turbStatus == QUALITY_POOR || tempStatus == QUALITY_POOR) {
        readings.quality = QUALITY_POOR;
    } else if (phStatus == QUALITY_WARNING || turbStatus == QUALITY_WARNING || tempStatus == QUALITY_WARNING) {
        readings.quality = QUALITY_WARNING;
    } else {
        readings.quality = QUALITY_GOOD;
    }

    // Clean up trailing delimiter
    if (reasons.endsWith("; ")) {
        reasons.remove(reasons.length() - 2);
    }
    readings.reason = reasons;

    // --- Actuator Response Logic with Hysteresis ---
    // Immediate activation on POOR.
    // Deactivation occurs only after PUMP_OFF_CONFIRM_COUNT consecutive non-POOR readings.
    if (readings.quality == QUALITY_POOR) {
        currentPumpActiveState = true;
        consecutiveNonPoorCount = 0;
    } else {
        consecutiveNonPoorCount++;
        if (consecutiveNonPoorCount >= PUMP_OFF_CONFIRM_COUNT) {
            currentPumpActiveState = false;
        }
    }
    readings.pumpActive = currentPumpActiveState;

    // Apply hardware outputs
    setPumpState(readings.pumpActive);
    updateStatusLEDs(readings.quality);
    updateBuzzer(readings.quality);
}

// ============================================================================
// SERIAL MONITOR INTERFACE
// ============================================================================
/**
 * @brief Outputs human-readable dashboard to the Serial Monitor at 115200 baud.
 */
void printSerialDashboard(const SensorReadings &readings) {
    Serial.println();
    Serial.println("========================================");
    Serial.println("      SMART WATER QUALITY MONITOR");
    Serial.println("========================================");
    Serial.println();

    // Sensor Readings Display
    Serial.print("pH             : ");
    Serial.println(readings.ph, 2);

    Serial.print("Turbidity      : ");
    Serial.print(readings.turbidity, 2);
    Serial.println(" NTU");

    Serial.print("Temperature    : ");
    if (readings.tempValid) {
        Serial.print(readings.temperature, 2);
        Serial.println(" C");
    } else {
        Serial.println("DISCONNECTED / ERROR");
    }

    // Simulation Explanation Line for Examiner
    Serial.print("SIM STATE      : ADC pH: ");
    Serial.print(readings.rawPh);
    Serial.print(" | ADC Turb: ");
    Serial.println(readings.rawTurb);
    Serial.println();

    // Water Quality Assessment
    Serial.print("Water Quality  : ");
    switch (readings.quality) {
        case QUALITY_GOOD:
            Serial.println("GOOD");
            break;
        case QUALITY_WARNING:
            Serial.println("WARNING");
            break;
        case QUALITY_POOR:
            Serial.println("POOR");
            break;
    }

    // Pump Status
    Serial.print("Pump Status    : ");
    Serial.println(readings.pumpActive ? "ON" : "OFF");

    // Dynamic Context (Reasons & Actions)
    if (readings.quality != QUALITY_GOOD && readings.reason.length() > 0) {
        Serial.print("Reason         : ");
        Serial.println(readings.reason);
    }

    if (readings.quality == QUALITY_POOR) {
        Serial.println("Action         : Automatic response activated");
    }

    Serial.println();

    // System Status Summary
    Serial.print("System Status  : ");
    if (readings.quality == QUALITY_POOR) {
        Serial.println("AUTOMATIC RESPONSE ACTIVE");
    } else {
        Serial.println("MONITORING");
    }

    Serial.println("========================================");
}

// ============================================================================
// WI-FI & CLOUD TELEMETRY (THINGSPEAK)
// ============================================================================
/**
 * @brief Ensures active Wi-Fi connection in Wokwi, reconnecting if disconnected.
 */
void checkWiFiConnection() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.print("[Wi-Fi] Connecting to Wokwi virtual AP: ");
        Serial.println(WIFI_SSID);
        WiFi.begin(WIFI_SSID, WIFI_PASS);
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 15) {
            delay(500);
            Serial.print(".");
            attempts++;
        }
        Serial.println();
        if (WiFi.status() == WL_CONNECTED) {
            Serial.print("[Wi-Fi] Connected! Local IP: ");
            Serial.println(WiFi.localIP());
        } else {
            Serial.println("[Wi-Fi] Connection attempt timed out. Will retry on next cycle.");
        }
    }
}

/**
 * @brief Transmits current telemetry parameters to ThingSpeak IoT Cloud.
 */
void uploadToThingSpeak(const SensorReadings &readings) {
#if ENABLE_CLOUD == 1
    // Guard against unconfigured placeholder credentials
    if (IS_PLACEHOLDER_CREDENTIALS()) {
        Serial.println("[ThingSpeak] Telemetry skipped: Default placeholder Channel ID or API Key detected.");
        Serial.println("[ThingSpeak] Enter valid ThingSpeak credentials in main.cpp to enable cloud sync.");
        return;
    }

    checkWiFiConnection();
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[ThingSpeak] Telemetry skipped: Wi-Fi offline.");
        return;
    }

    // Populate ThingSpeak telemetry fields
    // Field 1: pH
    // Field 2: Turbidity (NTU)
    // Field 3: Temperature (C)
    // Field 4: Quality Status (0 = GOOD, 1 = WARNING, 2 = POOR)
    // Field 5: Pump State (0 = OFF, 1 = ON)
    ThingSpeak.setField(1, readings.ph);
    ThingSpeak.setField(2, readings.turbidity);
    ThingSpeak.setField(3, readings.tempValid ? readings.temperature : -127.0f);
    ThingSpeak.setField(4, (int)readings.quality);
    ThingSpeak.setField(5, readings.pumpActive ? 1 : 0);

    Serial.println("[ThingSpeak] Transmitting packet to cloud...");
    int responseCode = ThingSpeak.writeFields(THINGSPEAK_CHANNEL_ID, THINGSPEAK_WRITE_API_KEY);

    if (responseCode == 200) {
        Serial.println("[ThingSpeak] Channel update successful (HTTP 200).");
    } else {
        Serial.print("[ThingSpeak] Problem updating channel. HTTP error code: ");
        Serial.println(responseCode);
    }
#else
    // Cloud upload disabled by compiler directive
    static bool loggedNotice = false;
    if (!loggedNotice) {
        Serial.println("[ThingSpeak] Cloud sync disabled (#define ENABLE_CLOUD 0). Local-only mode active.");
        loggedNotice = true;
    }
#endif
}

// ============================================================================
// MAIN SETUP & EXECUTION LOOP
// ============================================================================
void setup() {
    // 1. Initialize Serial communication at 115200 baud
    Serial.begin(115200);
    delay(500);

    // 2. Pre-configure Relay pin to avoid startup actuator glitches
    // Set to inactive state BEFORE switching pinMode to OUTPUT
    if (RELAY_ACTIVE_LOW) {
        digitalWrite(PIN_RELAY, HIGH); // Inactive level for active-low
    } else {
        digitalWrite(PIN_RELAY, LOW);  // Inactive level for active-high
    }
    pinMode(PIN_RELAY, OUTPUT);

    // 3. Initialize Status LEDs
#if ENABLE_STATUS_LEDS
    pinMode(PIN_LED_GOOD, OUTPUT);
    pinMode(PIN_LED_WARN, OUTPUT);
    pinMode(PIN_LED_POOR, OUTPUT);
    digitalWrite(PIN_LED_GOOD, LOW);
    digitalWrite(PIN_LED_WARN, LOW);
    digitalWrite(PIN_LED_POOR, LOW);
#endif

    // 4. Initialize Buzzer
#if ENABLE_BUZZER
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
#endif

    // 5. Configure ADC analog resolution to 12-bit (0..4095)
    analogReadResolution(12);
    pinMode(PIN_PH_POT, INPUT);
    pinMode(PIN_TURB_POT, INPUT);

    // 6. Initialize OneWire DS18B20 digital temperature bus
    tempSensors.begin();

    // 7. Connect to Wokwi virtual Wi-Fi
    Serial.println();
    Serial.println("==================================================");
    Serial.println("Smart Water Quality Monitoring System Initializing");
    Serial.println("Target: ESP32 DevKit C V4 (Wokwi Simulation)");
    Serial.println("==================================================");

#if ENABLE_CLOUD == 1
    WiFi.mode(WIFI_STA);
    checkWiFiConnection();
    ThingSpeak.begin(wifiClient);
#endif

    // Initial sensor warm-up read
    acquireSensors(currentReadings);
    evaluateWaterQuality(currentReadings);

    Serial.println("[System] Initialization complete. Entering main monitoring loop...");
}

void loop() {
    unsigned long currentMillis = millis();

    // 1. Periodic Sensor Acquisition and Serial Reporting (Every 2 seconds)
    if (currentMillis - lastSerialPrintTime >= SERIAL_INTERVAL_MS) {
        lastSerialPrintTime = currentMillis;

        acquireSensors(currentReadings);
        evaluateWaterQuality(currentReadings);
        printSerialDashboard(currentReadings);
    }

    // 2. Periodic Cloud Telemetry Upload (Every 20 seconds)
    if (currentMillis - lastCloudUploadTime >= CLOUD_INTERVAL_MS) {
        lastCloudUploadTime = currentMillis;

        uploadToThingSpeak(currentReadings);
    }
}
