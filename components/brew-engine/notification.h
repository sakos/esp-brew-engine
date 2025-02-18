#ifndef _Notification_H_
#define _Notification_H_

#include <chrono>
#include "nlohmann_json.hpp"

using namespace std;
using namespace std::chrono;
using json = nlohmann::json;

class Notification
{
public:
    string name;
    string message;
    int timeFromStart;
    system_clock::time_point timePoint;
    bool buzzer;
    bool done;
	uint refStepIndex;
	uint timeAbsolute;

    json to_json() //Needed for GUI view
    {
        int seconds = 0;
        if (this->timePoint.time_since_epoch() != decltype(this->timePoint)::duration::zero())
        {
            seconds = std::chrono::duration_cast<std::chrono::seconds>(this->timePoint.time_since_epoch()).count();
        }

        json jNotification;
        jNotification["name"] = this->name;
        jNotification["message"] = this->message;
        jNotification["timeFromStart"] = this->timeFromStart;
        jNotification["timePoint"] = seconds;
        jNotification["buzzer"] = this->buzzer;
        jNotification["done"] = this->done;
        jNotification["refStepIndex"] = this->refStepIndex;
        jNotification["timeAbsolute"] = this->timeAbsolute;

        return jNotification;
    }

    void from_json(const json &jsonData)
    {
        this->name = jsonData["name"].get<string>();
        this->timeFromStart = jsonData["timeFromStart"].get<int>();
        this->buzzer = jsonData["buzzer"].get<bool>();
//        this->done = false; // this can never come from json, always from control loop
		if (jsonData.contains("refStepIndex")) 
		{
			this->refStepIndex = jsonData["refStepIndex"].get<int>();
			// refStepIndex has been assigned a value
		} else {
			// refStepIndex was not present in the JSON data
			// Data migration from legacy. Relative time is set to start reference
			this->refStepIndex = 0; 
		}
		
		
        if (!jsonData["message"].is_null() && jsonData["message"].is_string())
        {
            this->message = jsonData["message"];
        }
    }

protected:
private:
};

#endif
