#include "thermal_bms.h"
#include <iostream> // For placeholder alerts/logging

// --- Safety Critical Requirement: REQ_BMS_THERMAL_001 ---
// ID: REQ_BMS_THERMAL_001
// Description: The Battery Management System (BMS) shall monitor the battery pack temperature continuously.
// If the temperature exceeds the predefined maximum safe operating temperature (e.g., 60°C),
// the BMS shall initiate a high-priority fault state. In this state, the BMS shall
// (a) immediately open the main contactors to isolate the battery pack,
// (b) activate the vehicle's emergency cooling system (if available), and
// (c) alert the driver and vehicle control unit (VCU) of the critical thermal event.
// The system shall remain in this safe state until the temperature returns to a
// safe operating range and a manual reset is performed by a qualified technician.

// --- Safety Critical Requirement: REQ_BMS_THERMAL_002 ---
// ID: REQ_BMS_THERMAL_002
// Description: The Battery Management System (BMS) shall monitor the battery pack temperature continuously.
// If the temperature falls below the predefined minimum safe operating temperature
// for charging (e.g., 0°C) or discharging (e.g., -20°C), the BMS shall prevent
// or limit charging/discharging operations accordingly. (a) If attempting to charge
// below the minimum charging temperature, the BMS shall inhibit charging and alert
// the VCU. (b) If attempting to discharge below the minimum discharging temperature,
// the BMS shall significantly limit discharge current or inhibit discharging entirely,
// and alert the VCU and driver. The BMS shall only permit full
// charging/discharging operations once the battery temperature is within the
// acceptable range.


ThermalBMS::ThermalBMS() :
    currentBatteryTemperatureC(25.0f), // Assume a nominal starting temperature
    faultState(BmsFaultState::NORMAL),
    alertMessage(""),
    contactorsOpen(false),
    emergencyCoolingActive(false),
    chargingInhibited(false),
    dischargingInhibited(false),
    dischargeCurrentLimit(1000.0f), // Example: Max truck discharge current
    manualResetRequired(false) {}

void ThermalBMS::monitorTemperature(float currentTemperatureCelsius) {
    currentBatteryTemperatureC = currentTemperatureCelsius;

    if (currentBatteryTemperatureC > MAX_SAFE_TEMP_C) {
        handleOverTemperature();
    } else if (currentBatteryTemperatureC < MIN_SAFE_CHARGE_TEMP_C || currentBatteryTemperatureC < MIN_LIMITED_DISCHARGE_TEMP_C) { // Adjusted condition to cover all under temp cases
        if (faultState != BmsFaultState::OVER_TEMPERATURE_CRITICAL) {
            handleUnderTemperature();
        }
    } else {
        if (faultState != BmsFaultState::OVER_TEMPERATURE_CRITICAL || !manualResetRequired) {
            resetToNormalState();
        }
    }
}

void ThermalBMS::handleOverTemperature() {
    if (faultState != BmsFaultState::OVER_TEMPERATURE_CRITICAL) {
        faultState = BmsFaultState::OVER_TEMPERATURE_CRITICAL;
        alertMessage = "CRITICAL: Battery Over-Temperature! Isolating battery.";
        std::cout << "LOG: " << alertMessage << std::endl;

        openMainContactors();
        activateEmergencyCooling();
        alertDriver(alertMessage);
        alertVCU(alertMessage);

        inhibitCharging();
        inhibitDischarging();
        manualResetRequired = true;
    }
}

void ThermalBMS::handleUnderTemperature() {
    // Charging check (REQ_BMS_THERMAL_002a)
    if (currentBatteryTemperatureC < MIN_SAFE_CHARGE_TEMP_C) {
        if (!chargingInhibited) { // Act if not already inhibited for this reason
            if (faultState != BmsFaultState::OVER_TEMPERATURE_CRITICAL) { // Don't override critical overtemp
                 faultState = BmsFaultState::UNDER_TEMPERATURE_CHARGE_INHIBIT;
                 alertMessage = "NOTICE: Battery too cold for charging. Charging inhibited.";
                 std::cout << "LOG: " << alertMessage << std::endl;
                 inhibitCharging();
                 alertVCU(alertMessage);
            }
        }
    } else {
        // Potential to allow charging if conditions are met and it was previously inhibited ONLY for cold charging.
        if (chargingInhibited && faultState == BmsFaultState::UNDER_TEMPERATURE_CHARGE_INHIBIT) {
            // This state will be fully cleared by resetToNormalState if all temps are fine.
            // No direct action here to prevent conflicting state changes until temp is fully nominal.
        }
    }

    // Discharging check (REQ_BMS_THERMAL_002b)
    if (currentBatteryTemperatureC < MIN_SAFE_DISCHARGE_TEMP_C) {
        if (!dischargingInhibited || faultState != BmsFaultState::UNDER_TEMPERATURE_DISCHARGE_INHIBIT) { // Act if not already inhibited for this reason or state is different
             if (faultState != BmsFaultState::OVER_TEMPERATURE_CRITICAL) {
                faultState = BmsFaultState::UNDER_TEMPERATURE_DISCHARGE_INHIBIT;
                alertMessage = "WARNING: Battery too cold! Discharging inhibited.";
                std::cout << "LOG: " << alertMessage << std::endl;
                inhibitDischarging();
                alertDriver(alertMessage);
                alertVCU(alertMessage);
             }
        }
    } else if (currentBatteryTemperatureC < MIN_LIMITED_DISCHARGE_TEMP_C) {
        if (faultState != BmsFaultState::UNDER_TEMPERATURE_DISCHARGE_LIMITED && faultState != BmsFaultState::UNDER_TEMPERATURE_DISCHARGE_INHIBIT) {
            if (faultState != BmsFaultState::OVER_TEMPERATURE_CRITICAL) {
                faultState = BmsFaultState::UNDER_TEMPERATURE_DISCHARGE_LIMITED;
                alertMessage = "INFO: Battery cold. Discharge current limited.";
                std::cout << "LOG: " << alertMessage << std::endl;
                limitDischargeCurrent(100.0f); // Example limit
                // Ensure discharging is not fully inhibited if it was for a more severe cold condition that has now passed
                if(dischargingInhibited && currentBatteryTemperatureC >= MIN_SAFE_DISCHARGE_TEMP_C) {
                    dischargingInhibited = false; 
                }
                alertDriver(alertMessage);
                alertVCU(alertMessage);
            }
        }
    } else {
        // Temp is above MIN_LIMITED_DISCHARGE_TEMP_C.
        // If it was limited or inhibited due to cold, and not charging-inhibited for cold,
        // this state will be fully cleared by resetToNormalState if all temps are fine.
    }

    // If only charge was inhibited and now temp is fine for charging, but still cold for discharge limiting/inhibit,
    // the discharge logic above will take precedence for faultState. `resetToNormalState` handles full recovery.
}


void ThermalBMS::resetToNormalState() {
    if (manualResetRequired) {
        std::cout << "LOG: Temperature is normal, but manual reset is required for previous OVER_TEMPERATURE_CRITICAL fault." << std::endl;
        return;
    }

    bool wasFaulted = (faultState != BmsFaultState::NORMAL);

    faultState = BmsFaultState::NORMAL;
    alertMessage = "System Normal";
    // contactorsOpen would typically be managed by higher-level logic after fault clear, or by performManualReset
    // emergencyCoolingActive would also be managed elsewhere or turn off based on its own logic
    
    chargingInhibited = false;
    dischargingInhibited = false;
    dischargeCurrentLimit = 1000.0f; // Reset to max capability

    if (wasFaulted) {
        std::cout << "LOG: All thermal conditions nominal. BMS state reset to NORMAL." << std::endl;
        alertDriver("Battery temperature normal. System OK.");
        alertVCU("BMS: Thermal conditions nominal.");
    }
}

BmsFaultState ThermalBMS::getFaultState() const {
    return faultState;
}

std::string ThermalBMS::getAlertMessage() const {
    return alertMessage;
}

bool ThermalBMS::canCharge() const {
    if (faultState == BmsFaultState::OVER_TEMPERATURE_CRITICAL) return false;
    if (currentBatteryTemperatureC < MIN_SAFE_CHARGE_TEMP_C) return false;
    return !chargingInhibited;
}

bool ThermalBMS::canDischarge() const {
    if (faultState == BmsFaultState::OVER_TEMPERATURE_CRITICAL) return false;
    if (currentBatteryTemperatureC < MIN_SAFE_DISCHARGE_TEMP_C) return false;
    return !dischargingInhibited;
}

float ThermalBMS::getAllowedDischargeCurrentLimit() const {
    if (faultState == BmsFaultState::OVER_TEMPERATURE_CRITICAL || dischargingInhibited || currentBatteryTemperatureC < MIN_SAFE_DISCHARGE_TEMP_C) {
        return 0.0f;
    }
    if (faultState == BmsFaultState::UNDER_TEMPERATURE_DISCHARGE_LIMITED || currentBatteryTemperatureC < MIN_LIMITED_DISCHARGE_TEMP_C) {
        return dischargeCurrentLimit;
    }
    return 1000.0f; // Default/max
}


void ThermalBMS::performManualReset() {
    std::cout << "LOG: Attempting manual reset..." << std::endl;
    if (faultState == BmsFaultState::OVER_TEMPERATURE_CRITICAL) {
        // Check if temperature has returned to a safe range (e.g., below max by a hysteresis margin)
        if (currentBatteryTemperatureC <= MAX_SAFE_TEMP_C - 5.0f) { 
            std::cout << "LOG: Manual reset authorized. Temperature is within safe recovery range." << std::endl;
            manualResetRequired = false;
            contactorsOpen = false; // Assuming technician also verifies and allows contactor closing
            std::cout << "ACTION: Main contactors CLOSED by technician authority after reset." << std::endl;
            // Re-evaluate the overall system state now that reset is done and temp is safe.
            // This might transition to NORMAL or an under-temperature state if applicable.
            resetToNormalState(); // First, try to go to normal
            monitorTemperature(currentBatteryTemperatureC); // Then re-evaluate current conditions fully
        } else {
            alertMessage = "Manual Reset Denied: Battery temperature still too high.";
            std::cout << "LOG: " << alertMessage << std::endl;
            alertDriver(alertMessage);
        }
    } else {
        std::cout << "LOG: Manual reset not applicable or not needed for current state." << std::endl;
    }
}

// --- Mock/Simulated hardware interactions ---
void ThermalBMS::openMainContactors() {
    contactorsOpen = true;
    dischargingInhibited = true; 
    chargingInhibited = true;   
    std::cout << "ACTION: Main contactors OPENED." << std::endl;
}

void ThermalBMS::activateEmergencyCooling() {
    emergencyCoolingActive = true;
    std::cout << "ACTION: Emergency cooling system ACTIVATED." << std::endl;
}

void ThermalBMS::alertDriver(const std::string& message) {
    std::cout << "ALERT (Driver): " << message << std::endl;
}

void ThermalBMS::alertVCU(const std::string& message) {
    std::cout << "ALERT (VCU): " << message << std::endl;
}

void ThermalBMS::inhibitCharging() {
    chargingInhibited = true;
    std::cout << "ACTION: Charging INHIBITED." << std::endl;
}

void ThermalBMS::limitDischargeCurrent(float limit) {
    dischargeCurrentLimit = limit;
    // If it was fully inhibited due to more extreme cold, but now only needs limiting, ensure inhibit flag is cleared.
    if (dischargingInhibited && currentBatteryTemperatureC >= MIN_SAFE_DISCHARGE_TEMP_C) {
        dischargingInhibited = false;
    }
    std::cout << "ACTION: Discharge current LIMITED to " << limit << " A." << std::endl;
}

void ThermalBMS::inhibitDischarging() {
    dischargingInhibited = true;
    dischargeCurrentLimit = 0.0f;
    std::cout << "ACTION: Discharging INHIBITED." << std::endl;
}

void ThermalBMS::allowCharging() {
    if (currentBatteryTemperatureC >= MIN_SAFE_CHARGE_TEMP_C && faultState != BmsFaultState::OVER_TEMPERATURE_CRITICAL) {
        chargingInhibited = false;
        // If state was UNDER_TEMPERATURE_CHARGE_INHIBIT, and now it's clear, reset to normal or other applicable state
        if(faultState == BmsFaultState::UNDER_TEMPERATURE_CHARGE_INHIBIT){
            resetToNormalState(); // Re-evaluate general state
            monitorTemperature(currentBatteryTemperatureC); // Check if other conditions apply
        }
        std::cout << "ACTION: Charging ALLOWED." << std::endl;
    } else {
        std::cout << "ACTION: Conditions not met to allow charging." << std::endl;
    }
}

void ThermalBMS::allowFullDischarge() {
    if (currentBatteryTemperatureC >= MIN_LIMITED_DISCHARGE_TEMP_C && faultState != BmsFaultState::OVER_TEMPERATURE_CRITICAL) {
        dischargingInhibited = false;
        dischargeCurrentLimit = 1000.0f; 
        // If state was related to under temp discharge, and now it's clear, reset to normal or other applicable state
        if(faultState == BmsFaultState::UNDER_TEMPERATURE_DISCHARGE_LIMITED || faultState == BmsFaultState::UNDER_TEMPERATURE_DISCHARGE_INHIBIT){
            resetToNormalState(); // Re-evaluate general state
            monitorTemperature(currentBatteryTemperatureC); // Check if other conditions apply
        }
        std::cout << "ACTION: Full discharge ALLOWED." << std::endl;
    } else {
         std::cout << "ACTION: Conditions not met to allow full discharge." << std::endl;
    }
} 