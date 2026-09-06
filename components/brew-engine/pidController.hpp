/*
 * esp-brew-engine
 * Copyright (C) Dekien Jeroen 2024
 *
 */
#ifndef INCLUDE_PIDCONTROLLER_HPP_
#define INCLUDE_PIDCONTROLLER_HPP_

#include <algorithm>
#include <cmath>
#include "esp_log.h"

class PIDController
{
private:
    double previousError;
    double integral;
    double kp; 
    double ki; 
    double kd; 
    double maxOutput;
    double minOutput;
    double maxDelta;
    double iWindow; // Integral tracking window in degrees (e.g., 2.0C)
    bool firstRun = true;

    // --- FIX SCALING FACTORS FOR HUMAN-READABLE TUNING ---
    // This allows the user to input normal numbers (e.g., I=0.5, D=25) 
    // instead of micro-fractions or massive hundreds.
    const double I_SCALE = 1.0 / 1000.0; // User input '1.0' becomes 0.001 internally
    const double D_SCALE = 10.0;         // User input '1.0' becomes 10.0 internally

public:
    bool debug = false;

    PIDController(double p, double i, double d)
    {
        this->kp = p;
        // Apply the internal scaling factor directly during initialization
        this->ki = i * I_SCALE;
        this->kd = d * D_SCALE;
        this->previousError = 0.0;
        this->integral = 0.0;
        this->minOutput = 0.0;
        this->maxOutput = 100.0;
        this->maxDelta = 0.0;
        this->iWindow = 2.0; // Default window: only integrate within 2 degrees of target
    }

    void setMax(double max) { this->maxOutput = max; }
    void setMin(double min) { this->minOutput = min; }
    void setMaxDelta(double delta) { this->maxDelta = delta; }
    void setIWindow(double window) { this->iWindow = window; }

    // dt: elapsed time since last execution in seconds (e.g., 20.0 or 30.0)
    double getOutput(double actualorig, double setpoint, double peaktemp, bool inhold, double dt)
    {
        if (dt <= 0.0) dt = 1.0; // Safety fallback for invalid dt

        double actual = actualorig;
        if (peaktemp > setpoint) 
        {
            double weight = 0;
            if (maxDelta > 0.5) 
            {
                weight = std::min(((peaktemp - setpoint) / maxDelta), 1.0);
            }
            actual = weight * peaktemp + (1.0 - weight) * actual;
        }

        double error = setpoint - actual;
        
        // 1. Proportional term (time-independent)
        double p = kp * error;

        double i = 0.0;
        double d = 0.0;

        if (!this->firstRun)
        {
            // 2. Integral term with conditional integration (iWindow) and true trapezoidal rule
            if (ki > 0.0 && inhold && std::abs(error) <= iWindow)
            {
                // True trapezoidal integration: ((current_error + previous_error) / 2) * dt
                integral += ((error + previousError) / 2.0) * dt;

                // Anti-windup: Clamp the I-term contribution dynamically based on P-term output
                double iMin = minOutput - p;
                double iMax = maxOutput - p;
                
                // Calculate the unconstrained I-term value
                i = ki * integral;
                
                // Clamp integral accumulator to prevent saturation beyond output limits
                if (i < iMin) { i = iMin; integral = iMin / ki; }
                if (i > iMax) { i = iMax; integral = iMax / ki; }
            }
            else
            {
                // Reset integral accumulator outside the window or during ramp phase
                integral = 0.0;
                i = 0.0;
            }

            // 3. Derivative term made time-independent (change in error / elapsed time)
            d = kd * ((error - previousError) / dt);
        }

        previousError = error;
        this->firstRun = false;

        double output = p + i + d;
        output = std::clamp(output, minOutput, maxOutput);

        ESP_LOGI("PID tune", "P: %.2f | I: %.2f | D: %.2f | PID: %.2f (dt: %.1fs)", p, i, d, output, dt);

        return output;
    }
};

#endif // INCLUDE_PIDCONTROLLER_HPP_
