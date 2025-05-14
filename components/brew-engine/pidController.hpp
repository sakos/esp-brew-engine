/*
 * esp-brew-engine
 * Copyright (C) Dekien Jeroen 2024
 *
 */
#ifndef INCLUDE_PIDCONTROLLER_HPP_
#define INCLUDE_PIDCONTROLLER_HPP_

#include <stdexcept>
#include <iostream>
using namespace std;
using std::cout;

class PIDController
{

private:
    double previousError;
    double integral;
    double previousActual;

    double kp; // Proportional
    double ki; // Integral
    double kd; // Derivative
    double max;
    double min = 0;
    double delta;

    bool firstRun = true;

    void addToIntegral(double i)
    {
        double newIntegral = integral + i;

        integral = clamp(newIntegral, min, max);

        if (debug)
        {
            cout << "new integral:" + to_string(integral) + "\n";
        }
    }

public:
    bool debug = false;

    PIDController(double p, double i, double d)
    {
/*        if (p == 0 || i == 0 || d == 0)
        {
            throw std::invalid_argument("K,I or P should not be empty!");
        } */		
		// On one hand zero parameter works, on the other hand, this exemption causes crash.

        this->kp = p;
        this->ki = i;
        this->kd = d;

        previousError = 0.0;
        integral = 0.0;
    }

    void setMax(double max)
    {
        this->max = max;
    }

    void setMin(double min)
    {
        this->min = min;
    }

    void setMaxDelta (double delta)
    {
        this->delta = delta;
    }

    double getOutput(double actualorig, double setpoint, double peaktemp, bool inhold)
    {
        double actual = actualorig;
        if (peaktemp > setpoint)        // Increase average temp closer to highest to avoid overheat
        {
            double weight = 0;
            if (delta > 0.5)            // We need a minimum band to prevent sudden jump
            {
                weight = std::min (((peaktemp - setpoint) / delta), 1.0);
            }
            
            actual = weight * peaktemp + (1-weight) * actual;
        }

        ESP_LOGI("PID tune", "PID actual: %f Target: %f peak %f weighted: %f", actualorig, setpoint, peaktemp, actual);
 
        previousActual = actual;

        // Error
        double error = setpoint - actual;

        // Proportional
        double p = kp * error;

        double i = 0;
        double d = 0;

        // skip i and d on first run
        if (!this->firstRun)
        {
            if (ki > 0)
            {
                // Integral 10
                addToIntegral(error);

                if (inhold)
                {
                    i = ki * (integral / 2); // Trapezoidal integration
                    i = clamp(i, min, max);
                }
                else
                {
                    integral = 0;       // Discard integral during heat step as it causes long overshoot
                    i = 0;
                }
            }

            // Derivative
            d = kd * (error - previousError);
        }
        previousError = error;

        double output = p + i + d;

        if (debug)
        {
            cout << "p:" + to_string(p) + " i:" + to_string(i) + " d:" + to_string(d) + " output:" + to_string(output) + "\n";
        }

        ESP_LOGI("PID tune", "P: %f I: %f D: %f PID: %f", p, i, d, output);

        output = clamp(output, min, max);

        this->firstRun = false;

        return output;
    }
};

#endif // INCLUDE_PIDCONTROLLER_HPP_