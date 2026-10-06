# IoT-Based Smart Water Quality Monitoring and Automated Response System

A simulation-only IoT project built for embedded systems and water quality monitoring. The project runs in the **Wokwi Simulator** within **Visual Studio Code**, configured using **PlatformIO** targeting an **ESP32 DevKit C V4**.

📖 **[Read the Full Academic Project Report (REPORT.md)](REPORT.md)**

---

## 📸 Simulation Architecture & Circuit

### Complete Wokwi Circuit Layout
![Wokwi Complete Simulation Circuit Layout](docs/images/wokwi_simulation_diagram_1791266477603.jpg)

### Sensor Subsystem Breakdown
![Wokwi Simulation Sensor Interface Breakdown](docs/images/water_quality_sensors_1791266510826.jpg)

---

## 📌 Project Overview
The system monitors water parameters in real time, classifies overall water quality into three states (**GOOD**, **WARNING**, **POOR**), activates an automated response actuator (simulated pump) with hysteresis, and streams 5-channel telemetry to the **ThingSpeak IoT Cloud**.

### ⚠️ Simulation & Prototyping Notes
* **Sensor Emulation:** pH and turbidity are emulated using two analog rotary potentiometers connected to 12-bit ADC channels (0–14 pH and 0–100 NTU).
* **Configured Thresholds:** All quality thresholds are configured prototype demonstration boundaries, not statutory drinking water standards.
* **Actuator Emulation:** The green LED represents a relay-switched pump actuator.

---

## 🛠️ Tech Stack & Hardware Specification
* **Target Controller:** ESP32 DevKit C V4 (`esp32doit-devkit-v1`)
* **Environment:** PlatformIO IDE / Wokwi for VS Code
* **Framework:** Arduino C++
* **Cloud Platform:** ThingSpeak IoT
* **Virtual Wi-Fi:** `Wokwi-GUEST` (open network)

---

## 🔌 Hardware Pin Mapping

| Peripheral | ESP32 GPIO | Mode / Signal | Function |
| :--- | :--- | :--- | :--- |
| **pH Sensor (Sim)** | GPIO 34 | Analog Input (ADC1) | Emulated pH probe (0.0 – 14.0) |
| **Turbidity Sensor (Sim)** | GPIO 35 | Analog Input (ADC1) | Emulated Turbidity probe (0.0 – 100.0 NTU) |
| **DS18B20 Temp Sensor** | GPIO 4 | OneWire Digital Bus | Water Temperature in °C (4.7 kΩ pull-up to 3.3V) |
| **Status LED - GOOD** | GPIO 25 | Digital Output (220 Ω) | Green indicator for nominal state |
| **Status LED - WARNING** | GPIO 27 | Digital Output (220 Ω) | Yellow indicator for borderline readings |
| **Status LED - POOR** | GPIO 32 | Digital Output (220 Ω) | Red indicator for contamination/hazard |
| **Acoustic Alarm Buzzer** | GPIO 33 | Digital Output | 2 kHz alert pulse (150 ms) on POOR |
| **Relay Input (Pump)** | GPIO 26 | Digital Output | Active-Low relay switching simulated pump LED |

---

## ⚙️ Configured Prototype Thresholds

* **pH:**
  * **GOOD:** $6.5 \le \text{pH} \le 8.5$
  * **WARNING:** $6.0 \le \text{pH} < 6.5$ or $8.5 < \text{pH} \le 9.0$
  * **POOR:** $\text{pH} < 6.0$ or $\text{pH} > 9.0$
* **Turbidity:**
  * **GOOD:** $< 20.0\text{ NTU}$
  * **WARNING:** $20.0 \le \text{Turbidity} \le 40.0\text{ NTU}$
  * **POOR:** $> 40.0\text{ NTU}$
* **Temperature:**
  * **GOOD:** $15.0 \le T \le 35.0\ ^\circ\text{C}$
  * **WARNING:** $5.0 \le T < 15.0\ ^\circ\text{C}$ or $35.0 < T \le 40.0\ ^\circ\text{C}$
  * **POOR:** $T < 5.0\ ^\circ\text{C}$, $T > 40.0\ ^\circ\text{C}$, or Disconnected ($-127\ ^\circ\text{C}$)
* **Overall Quality:** Worst-case evaluation ($\text{POOR} > \text{WARNING} > \text{GOOD}$).

---

## 🔄 Automated Pump Logic & Hysteresis
* **Immediate Response:** When quality drops to `POOR`, the relay activates immediately, turning on the simulated pump and acoustic alarm.
* **Debounced Recovery (Hysteresis):** The pump shuts off only after `PUMP_OFF_CONFIRM_COUNT = 2` consecutive non-POOR cycles (4 seconds), preventing chattering near boundary thresholds.

---

## ☁️ ThingSpeak Telemetry Channels
Data packets are transmitted every 20 seconds:
* **Field 1:** pH
* **Field 2:** Turbidity (NTU)
* **Field 3:** Temperature (°C)
* **Field 4:** Quality Status (`0` = GOOD, `1` = WARNING, `2` = POOR)
* **Field 5:** Pump State (`0` = OFF, `1` = ON)

---

## 🚀 How to Run in VS Code

1. Install **PlatformIO IDE** and **Wokwi for VS Code** extensions.
2. Build the project firmware:
   * Click **PlatformIO sidebar** $\rightarrow$ `esp32doit-devkit-v1` $\rightarrow$ `General` $\rightarrow$ `Build`.
3. Launch simulation:
   * Press `Cmd+Shift+P` (or `F1`) $\rightarrow$ select **`Wokwi: Start Simulator`**.
4. Test scenarios by adjusting the potentiometer sliders and viewing the interactive serial dashboard.
