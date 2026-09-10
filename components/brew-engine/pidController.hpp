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
    }

	
    void setMax(double max) { this->maxOutput = max; }
    void setMin(double min) { this->minOutput = min; }
    void setPID(double p, double i, double d)
    {
        this->kp = p;
        this->ki = i * I_SCALE; 
        this->kd = d * D_SCALE; 
    }	

    // dt: elapsed time since last execution in seconds (e.g., 20.0 or 30.0)
    double getOutput(double actual, double setpoint, bool inhold, double dt)
    {
        if (dt <= 0.0) dt = 1.0; // Safety fallback for invalid dt

        double error = setpoint - actual;
        
        // 1. Proportional term (time-independent)
        double p = kp * error;

        double i = 0.0;
        double d = 0.0;

        if (!this->firstRun)
        {
            // 2. Integral term with conditional integration and true trapezoidal rule
            if (ki > 0.0 && inhold )
            {
                // True trapezoidal integration: ((current_error + previous_error) / 2) * dt
                integral += ((error + previousError) / 2.0) * dt;
				
				i = ki * integral;
				
                // Anti-windup: Clamp the I-term 
                // Tight negative limit to ensure fast recovery when temperature drops
                if (i < -15.0) 
                { 
                    i = -15.0; 
                    integral = -15.0 / ki; 
                }
                // Standard upper limit for maximum physical output
                if (i > maxOutput) 
                { 
                    i = maxOutput; 
                    integral = maxOutput / ki; 
                }   
			}		
            else
            {
                // Reset integral accumulator during ramp phase
                integral = 0.0;
                i = 0.0;
            }

			// 3. Derivative term with dynamic error-dependent scaling
			double raw_d = kd * ((error - previousError) / dt);
			
			// Calculate absolute values for safe magnitude comparison
			double abs_error = std::abs(error);
			double abs_change = std::abs(error - previousError);

			// Default scaling factor is 1.0 (full braking)
			double d_scale = 1.0;

			// If the change rate is smaller than the distance to target, 
			// damp the derivative brake proportionally.
			if (abs_error > 0.0 && abs_change < abs_error)
			{
				d_scale = abs_change / abs_error;
			}

			// Apply the sign-safe scaling factor to the D-term
			d = raw_d * d_scale;

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
