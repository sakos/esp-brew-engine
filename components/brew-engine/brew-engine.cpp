/*
 * esp-brew-engine
 * Copyright (C) Dekien Jeroen 2024
 */
#include "brew-engine.h"

using namespace std;
using json = nlohmann::json;

static const char *TAG = "BrewEngine";

// esp http server only works with static handlers, no other option atm then to save a pointer.
BrewEngine *mainInstance;

BrewEngine::BrewEngine(SettingsManager *settingsManager)
{
	ESP_LOGI(TAG, "BrewEngine Construct");
	this->settingsManager = settingsManager;
	mainInstance = this;
}

void BrewEngine::Init()
{
	// read the post important settings first so when van set outputs asap.
	this->readSystemSettings();

	this->readHeaterSettings();

	// init gpio as soon as possible
	if (this->invertOutputs)
	{
		this->gpioHigh = 0;
		this->gpioLow = 1;
	}

	this->initHeaters();

	if (!this->stir_PIN)
	{
		ESP_LOGW(TAG, "StirPin is not configured!");

		this->stirStatusText = "Disabled";
	}
	else
	{
		gpio_reset_pin(this->stir_PIN);
		gpio_set_direction(this->stir_PIN, GPIO_MODE_OUTPUT);
		gpio_set_level(this->stir_PIN, this->gpioLow);
	}

	if (!this->buzzer_PIN)
	{
		ESP_LOGW(TAG, "Buzzer is not configured!");
	}
	else
	{
		gpio_reset_pin(this->buzzer_PIN);
		gpio_set_direction(this->buzzer_PIN, GPIO_MODE_OUTPUT);
		gpio_set_level(this->buzzer_PIN, this->gpioLow);
	}

	if (!this->speaker1_PIN || !this->speaker2_PIN)
	{
		ESP_LOGW(TAG, "Speaker (both) is not configured!");
	}
	else
	{
		gpio_reset_pin(this->speaker1_PIN);
		gpio_set_direction(this->speaker1_PIN, GPIO_MODE_OUTPUT);
		gpio_set_level(this->speaker1_PIN, 0);
		gpio_reset_pin(this->speaker2_PIN);
		gpio_set_direction(this->speaker2_PIN, GPIO_MODE_OUTPUT);
		gpio_set_level(this->speaker2_PIN, 0);
	}


	// read other settings like maishschedules and pid
	this->readSettings();

	this->readTempSensorSettings();

	this->initOneWire();

	this->detectOnewireTemperatureSensors();

	this->initMqtt();

	this->run = true;

	xTaskCreate(&this->readLoop, "readloop_task", 4096, this, 5, NULL);

	this->server = this->startWebserver();
}

void BrewEngine::initHeaters()
{
	for (auto const &heater : this->heaters)
	{
		ESP_LOGI(TAG, "Heater %s Configured", heater->name.c_str());

		gpio_reset_pin(heater->pinNr);
		gpio_set_direction(heater->pinNr, GPIO_MODE_OUTPUT);
		gpio_set_level(heater->pinNr, this->gpioLow);
	}
	
	// Debug settings for LED output to test improced heater control
	gpio_reset_pin(GPIO_NUM_2);
	gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);
	gpio_set_level(GPIO_NUM_2, 0);

}

void BrewEngine::readSystemSettings()
{
	ESP_LOGI(TAG, "Reading System Settings");

	// io settings
	this->oneWire_PIN = (gpio_num_t)this->settingsManager->Read("onewirePin", (uint16_t)CONFIG_ONEWIRE);
	this->stir_PIN = (gpio_num_t)this->settingsManager->Read("stirPin", (uint16_t)CONFIG_STIR);
	this->buzzer_PIN = (gpio_num_t)this->settingsManager->Read("buzzerPin", (uint16_t)CONFIG_BUZZER);
	this->buzzerTime = this->settingsManager->Read("buzzerTime", (uint8_t)2);
	this->speaker1_PIN = (gpio_num_t)this->settingsManager->Read("speaker1Pin", (uint16_t)CONFIG_SPEAKER1);
	this->speaker2_PIN = (gpio_num_t)this->settingsManager->Read("speaker2Pin", (uint16_t)CONFIG_SPEAKER2);

	bool configInvertOutputs = false;
// is there a cleaner way to do this?, config to bool doesn't seem to work properly
#if defined(CONFIG_InvertOutputs)
	configInvertOutputs = true;
#endif
	this->invertOutputs = this->settingsManager->Read("invertOutputs", configInvertOutputs);

	// mqtt
	this->mqttUri = this->settingsManager->Read("mqttUri", (string)CONFIG_MQTT_URI);

	// temperature scale
	uint8_t defaultConfigScale = 0; // default to Celsius
#if defined(CONFIG_SCALE_FAHRENHEIT)
	defaultConfigScale = Fahrenheit;
#endif

	this->temperatureScale = (TemperatureScale)this->settingsManager->Read("tempScale", defaultConfigScale);

	ESP_LOGI(TAG, "Reading System Settings Done");
}

void BrewEngine::saveSystemSettingsJson(const json &config)
{
	ESP_LOGI(TAG, "Saving System Settings");

	if (!config["onewirePin"].is_null() && config["onewirePin"].is_number())
	{
		this->settingsManager->Write("onewirePin", (uint16_t)config["onewirePin"]);
		this->oneWire_PIN = (gpio_num_t)config["onewirePin"];
	}
	if (!config["stirPin"].is_null() && config["stirPin"].is_number())
	{
		this->settingsManager->Write("stirPin", (uint16_t)config["stirPin"]);
		this->stir_PIN = (gpio_num_t)config["stirPin"];
	}
	if (!config["buzzerPin"].is_null() && config["buzzerPin"].is_number())
	{
		this->settingsManager->Write("buzzerPin", (uint16_t)config["buzzerPin"]);
		this->buzzer_PIN = (gpio_num_t)config["buzzerPin"];
	}
	if (!config["buzzerTime"].is_null() && config["buzzerTime"].is_number())
	{
		this->settingsManager->Write("buzzerTime", (uint8_t)config["buzzerTime"]);
		this->buzzerTime = (uint8_t)config["buzzerTime"];
	}
	if (!config["invertOutputs"].is_null() && config["invertOutputs"].is_boolean())
	{
		this->settingsManager->Write("invertOutputs", (bool)config["invertOutputs"]);
		this->invertOutputs = (bool)config["invertOutputs"];
	}
	if (!config["mqttUri"].is_null() && config["mqttUri"].is_string())
	{
		this->settingsManager->Write("mqttUri", (string)config["mqttUri"]);
		this->mqttUri = config["mqttUri"];
	}
	if (!config["temperatureScale"].is_null() && config["temperatureScale"].is_number())
	{
		uint8_t scale = (uint8_t)config["temperatureScale"];
		this->settingsManager->Write("tempScale", scale); // key is limited to x chars so we shorten it
		this->temperatureScale = (TemperatureScale)config["temperatureScale"];
	}
	if (!config["speaker1Pin"].is_null() && config["speaker1Pin"].is_number())
	{
		this->settingsManager->Write("speaker1Pin", (uint16_t)config["speaker1Pin"]);
		this->speaker1_PIN = (gpio_num_t)config["speaker1Pin"];
	}
	if (!config["speaker2Pin"].is_null() && config["speaker2Pin"].is_number())
	{
		this->settingsManager->Write("speaker2Pin", (uint16_t)config["speaker2Pin"]);
		this->speaker1_PIN = (gpio_num_t)config["speaker2Pin"];
	}


	ESP_LOGI(TAG, "Saving System Settings Done");
}

void BrewEngine::readSettings()
{
	ESP_LOGI(TAG, "Reading Settings");

	vector<uint8_t> empty = json::to_msgpack(json::array({}));
	vector<uint8_t> serialized = this->settingsManager->Read("mashschedules", empty);

	json jSchedules = json::from_msgpack(serialized);

	if (jSchedules.empty())
	{
		ESP_LOGI(TAG, "Adding Default Mash Schedules");
		this->addDefaultMash();
		this->saveMashSchedules();
	}
	else
	{
		for (const auto &el : jSchedules.items())
		{
			json jSchedule = el.value();

			auto schedule = new MashSchedule();
			schedule->from_json(jSchedule);

			this->mashSchedules.insert_or_assign(schedule->name, schedule);
		}
	}

	// we save and load pid doubles as unit16 becease nvs doesnt' have double support, and we are happy with only 1 decimal
	uint16_t pint = this->settingsManager->Read("kP", (uint16_t)(this->mashkP * 10));
	uint16_t iint = this->settingsManager->Read("kI", (uint16_t)(this->mashkI * 10));
	uint16_t dint = this->settingsManager->Read("kD", (uint16_t)(this->mashkD * 10));

	this->mashkP = (double)pint / 10;
	this->mashkI = (double)iint / 10;
	this->mashkD = (double)dint / 10;

	uint16_t bpint = this->settingsManager->Read("boilkP", (uint16_t)(this->boilkP * 10));
	uint16_t biint = this->settingsManager->Read("boilkI", (uint16_t)(this->boilkI * 10));
	uint16_t bdint = this->settingsManager->Read("boilkD", (uint16_t)(this->boilkD * 10));

	this->boilkP = (double)bpint / 10;
	this->boilkI = (double)biint / 10;
	this->boilkD = (double)bdint / 10;

	this->pidLoopTime = this->settingsManager->Read("pidLoopTime", (uint16_t)CONFIG_PID_LOOPTIME);
	this->stepInterval = this->settingsManager->Read("stepInterval", (uint16_t)CONFIG_PID_LOOPTIME); // we use same as pidloop time

	this->boostModeUntil = this->settingsManager->Read("boostModeUntil", (uint8_t)this->boostModeUntil);
	this->heaterLimit = this->settingsManager->Read("heaterLimit", (uint8_t)this->heaterLimit);
	this->heaterCycles = this->settingsManager->Read("heaterCycles", (uint8_t)this->heaterCycles);
	this->relayGuard = this->settingsManager->Read("relayGuard", (uint8_t)this->relayGuard);
}

void BrewEngine::setMashSchedule(const json &jSchedule)
{
	json newSteps = jSchedule["steps"];

	auto newMash = new MashSchedule();
	newMash->name = jSchedule["name"].get<string>();
	newMash->boil = jSchedule["boil"].get<bool>();

	for (auto const &step : newMash->steps)
	{
		delete step;
	}
	newMash->steps.clear();

	for (auto &el : newSteps.items())
	{
		auto jStep = el.value();

		auto newStep = new MashStep();
		newStep->from_json(jStep);
		newMash->steps.push_back(newStep);
	}

	newMash->sort_steps();

	json newNotifications = jSchedule["notifications"];
	for (auto const &notification : newMash->notifications)
	{
		delete notification;
	}
	newMash->notifications.clear();

	for (auto &el : newNotifications.items())
	{
		auto jNotification = el.value();

		auto newNotification = new Notification();
		newNotification->from_json(jNotification);
		newMash->notifications.push_back(newNotification);
	}

	newMash->sort_notifications();

	this->mashSchedules.insert_or_assign(newMash->name, newMash);
}

void BrewEngine::saveMashSchedules()
{
	ESP_LOGI(TAG, "Saving Mash Schedules");

	json jSchedules = json::array({});
	for (auto const &[key, mashSchedule] : this->mashSchedules)
	{

		if (!mashSchedule->temporary)
		{
			json jSchedule = mashSchedule->to_json();
			jSchedules.push_back(jSchedule);
		}
	}

	// serialize to MessagePack for size
	vector<uint8_t> serialized = json::to_msgpack(jSchedules);

	this->settingsManager->Write("mashschedules", serialized);

	ESP_LOGI(TAG, "Saving Mash Schedules Done, %d bytes", serialized.size());
}

void BrewEngine::savePIDSettings()
{
	ESP_LOGI(TAG, "Saving PID Settings");

	uint16_t pint = static_cast<uint16_t>(this->mashkP * 10);
	uint16_t iint = static_cast<uint16_t>(this->mashkI * 10);
	uint16_t dint = static_cast<uint16_t>(this->mashkD * 10);

	this->settingsManager->Write("kP", pint);
	this->settingsManager->Write("kI", iint);
	this->settingsManager->Write("kD", dint);

	uint16_t bpint = static_cast<uint16_t>(this->boilkP * 10);
	uint16_t biint = static_cast<uint16_t>(this->boilkI * 10);
	uint16_t bdint = static_cast<uint16_t>(this->boilkD * 10);

	this->settingsManager->Write("boilkP", bpint);
	this->settingsManager->Write("boilkI", biint);
	this->settingsManager->Write("boilkD", bdint);

	this->settingsManager->Write("pidLoopTime", this->pidLoopTime);
	this->settingsManager->Write("stepInterval", this->stepInterval);

	this->settingsManager->Write("boostModeUntil", this->boostModeUntil);
	this->settingsManager->Write("heaterLimit", this->heaterLimit);
	this->settingsManager->Write("heaterCycles", this->heaterCycles);
	this->settingsManager->Write("relayGuard", this->relayGuard);

	ESP_LOGI(TAG, "Saving PID Settings Done");
}

void BrewEngine::addDefaultMash()
{
	auto defaultMash = new MashSchedule();
	defaultMash->name = "Default";
	defaultMash->boil = false;

	auto defaultMash_s1 = new MashStep();
	defaultMash_s1->index = 0;
	defaultMash_s1->name = "Beta Amylase";
	defaultMash_s1->temperature = (this->temperatureScale == Celsius) ? 64 : 150;
	defaultMash_s1->stepTime = 5;
	defaultMash_s1->extendStepTimeIfNeeded = true;
	defaultMash_s1->allowBoost = true;
	defaultMash_s1->time = 45;
	defaultMash->steps.push_back(defaultMash_s1);

	auto defaultMash_s2 = new MashStep();
	defaultMash_s2->index = 1;
	defaultMash_s2->name = "Alpha Amylase";
	defaultMash_s2->temperature = (this->temperatureScale == Celsius) ? 72 : 160;
	defaultMash_s2->stepTime = 5;
	defaultMash_s2->extendStepTimeIfNeeded = true;
	defaultMash_s2->allowBoost = false;
	defaultMash_s2->time = 20;
	defaultMash->steps.push_back(defaultMash_s2);

	auto defaultMash_s3 = new MashStep();
	defaultMash_s3->index = 2;
	defaultMash_s3->name = "Mash Out";
	defaultMash_s3->temperature = (this->temperatureScale == Celsius) ? 78 : 170;
	defaultMash_s3->stepTime = 5;
	defaultMash_s3->extendStepTimeIfNeeded = true;
	defaultMash_s3->allowBoost = false;
	defaultMash_s3->time = 5;
	defaultMash->steps.push_back(defaultMash_s3);

	auto defaultMash_n1 = new Notification();
	defaultMash_n1->name = "Add Grains";
	defaultMash_n1->message = "Please add Grains";
	defaultMash_n1->timeFromStart = 5;
	defaultMash_n1->buzzer = true;
	defaultMash->notifications.push_back(defaultMash_n1);

	auto defaultMash_n2 = new Notification();
	defaultMash_n2->name = "Start Lautering";
	defaultMash_n2->message = "Please Start Lautering/Sparging";
	defaultMash_n2->timeFromStart = 85;
	defaultMash_n2->buzzer = true;
	defaultMash->notifications.push_back(defaultMash_n2);

	this->mashSchedules.insert_or_assign(defaultMash->name, defaultMash);

	auto ryeMash = new MashSchedule();
	ryeMash->name = "Rye Mash";
	ryeMash->boil = false;

	auto ryeMash_s1 = new MashStep();
	ryeMash_s1->index = 0;
	ryeMash_s1->name = "Beta Glucanase";
	ryeMash_s1->temperature = (this->temperatureScale == Celsius) ? 43 : 110;
	ryeMash_s1->stepTime = 5;
	ryeMash_s1->extendStepTimeIfNeeded = true;
	ryeMash_s1->allowBoost = true;
	ryeMash_s1->time = 20;
	ryeMash->steps.push_back(ryeMash_s1);

	auto ryeMash_s2 = new MashStep();
	ryeMash_s2->index = 1;
	ryeMash_s2->name = "Beta Amylase";
	ryeMash_s2->temperature = (this->temperatureScale == Celsius) ? 64 : 150;
	ryeMash_s2->stepTime = 5;
	ryeMash_s2->extendStepTimeIfNeeded = true;
	ryeMash_s2->allowBoost = false;
	ryeMash_s2->time = 45;
	ryeMash->steps.push_back(ryeMash_s2);

	auto ryeMash_s3 = new MashStep();
	ryeMash_s3->index = 2;
	ryeMash_s3->name = "Alpha Amylase";
	ryeMash_s3->temperature = (this->temperatureScale == Celsius) ? 72 : 160;
	ryeMash_s3->stepTime = 5;
	ryeMash_s3->extendStepTimeIfNeeded = true;
	ryeMash_s3->allowBoost = false;
	ryeMash_s3->time = 20;
	ryeMash->steps.push_back(ryeMash_s3);

	auto ryeMash_s4 = new MashStep();
	ryeMash_s4->index = 3;
	ryeMash_s4->name = "Mash Out";
	ryeMash_s4->temperature = (this->temperatureScale == Celsius) ? 78 : 170;
	ryeMash_s4->stepTime = 5;
	ryeMash_s4->extendStepTimeIfNeeded = true;
	ryeMash_s4->allowBoost = false;
	ryeMash_s4->time = 5;
	ryeMash->steps.push_back(ryeMash_s4);

	auto ryeMash_n1 = new Notification();
	ryeMash_n1->name = "Add Grains";
	ryeMash_n1->message = "Please add Grains";
	ryeMash_n1->timeFromStart = 5;
	ryeMash_n1->buzzer = true;
	ryeMash->notifications.push_back(ryeMash_n1);

	auto ryeMash_n2 = new Notification();
	ryeMash_n2->name = "Start Lautering";
	ryeMash_n2->message = "Please Start Lautering/Sparging";
	ryeMash_n2->timeFromStart = 110;
	ryeMash_n2->buzzer = true;
	ryeMash->notifications.push_back(ryeMash_n2);

	this->mashSchedules.insert_or_assign(ryeMash->name, ryeMash);

	auto boil = new MashSchedule();
	boil->name = "Boil 70 Min";
	boil->boil = true;

	auto boil_s1 = new MashStep();
	boil_s1->index = 0;
	boil_s1->name = "Boil";
	boil_s1->temperature = (this->temperatureScale == Celsius) ? 101 : 214;
	boil_s1->stepTime = 0;
	boil_s1->extendStepTimeIfNeeded = true;
	boil_s1->time = 70;
	boil->steps.push_back(boil_s1);

	auto boil_n1 = new Notification();
	boil_n1->name = "Bittering Hops";
	boil_n1->message = "Please add Bittering Hops";
	boil_n1->timeFromStart = 0;
	boil_n1->buzzer = true;
	boil->notifications.push_back(boil_n1);

	auto boil_n2 = new Notification();
	boil_n2->name = "Aroma Hops";
	boil_n2->message = "Please add Aroma Hops";
	boil_n2->timeFromStart = 55;
	boil_n2->buzzer = true;
	boil->notifications.push_back(boil_n2);

	this->mashSchedules.insert_or_assign(boil->name, boil);
}

void BrewEngine::addDefaultHeaters()
{
	auto defaultHeater1 = new Heater();
	defaultHeater1->id = 1;
	defaultHeater1->name = "Heater 1";
	defaultHeater1->pinNr = (gpio_num_t)CONFIG_HEAT1;
	defaultHeater1->preference = 1;
	defaultHeater1->watt = 1500;
	defaultHeater1->useForMash = true;
	defaultHeater1->useForBoil = true;

	this->heaters.push_back(defaultHeater1);

	auto defaultHeater2 = new Heater();
	defaultHeater2->id = 2;
	defaultHeater2->name = "Heater 2";
	defaultHeater2->pinNr = (gpio_num_t)CONFIG_HEAT2;
	defaultHeater2->preference = 2;
	defaultHeater2->watt = 1500;
	defaultHeater2->useForMash = true;
	defaultHeater2->useForBoil = true;
	this->heaters.push_back(defaultHeater2);
}

void BrewEngine::readHeaterSettings()
{
	ESP_LOGI(TAG, "Reading Heater Settings");

	vector<uint8_t> empty = json::to_msgpack(json::array({}));
	vector<uint8_t> serialized = this->settingsManager->Read("heaters", empty);

	json jHeaters = json::from_msgpack(serialized);

	if (jHeaters.empty())
	{
		ESP_LOGI(TAG, "Adding Default Heaters");
		this->addDefaultHeaters();
	}
	else
	{

		for (auto &el : jHeaters.items())
		{
			auto jHeater = el.value();

			auto heater = new Heater();
			heater->from_json(jHeater);

			ESP_LOGI(TAG, "Heater From Settings ID:%d", heater->id);

			this->heaters.push_back(heater);
		}
	}

	// Sort on preference
	sort(this->heaters.begin(), this->heaters.end(), [](Heater *h1, Heater *h2)
		 { return (h1->preference < h2->preference); });
}

void BrewEngine::saveHeaterSettings(const json &jHeaters)
{
	ESP_LOGI(TAG, "Saving Heater Settings");

	if (!jHeaters.is_array())
	{
		ESP_LOGW(TAG, "Heater settings must be an array!");
		return;
	}

	// wait for stop
	vTaskDelay(pdMS_TO_TICKS(1000));

	// clear
	for (auto const &heater : this->heaters)
	{
		delete heater;
	}
	this->heaters.clear();

	uint8_t newId = 0;

	// update running data
	for (auto &el : jHeaters.items())
	{
		newId++;

		if (newId > 10)
		{
			ESP_LOGE(TAG, "Only 10 heaters supported!");
			continue;
		}

		auto jHeater = el.value();
		jHeater["id"] = newId;

		auto heater = new Heater();
		heater->from_json(jHeater);
		heater->id = newId;

		this->heaters.push_back(heater);
	}

	// Sort on preference
	sort(this->heaters.begin(), this->heaters.end(), [](Heater *h1, Heater *h2)
		 { return (h1->preference < h2->preference); });

	// Serialize to MessagePack for size
	vector<uint8_t> serialized = json::to_msgpack(jHeaters);

	this->settingsManager->Write("heaters", serialized);

	// re-init so they can be used
	this->initHeaters();

	ESP_LOGI(TAG, "Saving Heater Settings Done");
}

void BrewEngine::readTempSensorSettings()
{
	vector<uint8_t> empty = json::to_msgpack(json::array({}));
	vector<uint8_t> serialized = this->settingsManager->Read("tempsensors", empty);

	json jTempSensors = json::from_msgpack(serialized);

	for (auto &el : jTempSensors.items())
	{
		auto jSensor = el.value();

		auto sensor = new TemperatureSensor();
		sensor->from_json(jSensor);

		uint64_t sensorId = sensor->id;

		ESP_LOGI(TAG, "Sensor From Settings address: %016llX, ID:%llu", sensorId, sensorId);

		this->sensors.insert_or_assign(sensorId, sensor);
	}
}

void BrewEngine::saveTempSensorSettings(const json &jTempSensors)
{
	ESP_LOGI(TAG, "Saving Temp Sensor Settings");

	if (!jTempSensors.is_array())
	{
		ESP_LOGW(TAG, "Temp settings must be an array!");
		return;
	}

	// we need to temp stop our temp read loop while we change the sensor data
	this->skipTempLoop = true;
	vTaskDelay(pdMS_TO_TICKS(2000));

	// update running data
	for (auto &el : jTempSensors.items())
	{
		auto jSensor = el.value();
		string stringId = jSensor["id"].get<string>();
		uint64_t sensorId = std::stoull(stringId);

		std::map<uint64_t, TemperatureSensor *>::iterator it;
		it = this->sensors.find(sensorId);

		if (it == this->sensors.end())
		{
			// doesn't exist anymore, just ignore
			ESP_LOGI(TAG, "doesn't exist anymore, just ignore %llu", sensorId);
			continue;
		}
		else
		{
			ESP_LOGI(TAG, "Updating Sensor %llu", sensorId);
			// update it
			TemperatureSensor *sensor = it->second;
			sensor->name = jSensor["name"];
			sensor->color = jSensor["color"];

			if (!jSensor["useForControl"].is_null() && jSensor["useForControl"].is_boolean())
			{
				sensor->useForControl = jSensor["useForControl"];
			}

			if (!jSensor["show"].is_null() && jSensor["show"].is_boolean())
			{
				sensor->show = jSensor["show"];

				if (!sensor->show)
				{
					// when show is disabled we also remove it from current, so it doesn't showup anymore
					this->currentTemperatures.erase(sensorId);
				}
			}

			if (!jSensor["compensateAbsolute"].is_null() && jSensor["compensateAbsolute"].is_number())
			{
				sensor->compensateAbsolute = (float)jSensor["compensateAbsolute"];
			}

			if (!jSensor["compensateRelative"].is_null() && jSensor["compensateRelative"].is_number())
			{
				sensor->compensateRelative = (float)jSensor["compensateRelative"];
			}
		}
	}

	// We also need to delete sensors that are no longer in the list
	vector<uint64_t> sensorsToDelete;

	for (auto const &[key, sensor] : this->sensors)
	{
		uint64_t sensorId = sensor->id;
		string stringId = to_string(sensorId); // json doesn't support unit64 so in out json id is string
		auto foundSensor = std::find_if(jTempSensors.begin(), jTempSensors.end(), [&stringId](const json &x)
										{
											auto it = x.find("id");
											return it != x.end() and it.value() == stringId; });

		// remove it
		if (foundSensor == jTempSensors.end())
		{
			ESP_LOGI(TAG, "Erasing Sensor %llu", sensorId);
			sensorsToDelete.push_back(sensorId);
			this->currentTemperatures.erase(sensorId);
		}
	}

	// erase in second loop, we can't mutate wile in auto loop (c++ limitation atm)
	for (auto &sensorId : sensorsToDelete)
	{
		this->sensors.erase(sensorId);
	}

	// // Convert sensors to json and save to nvram
	json jSensors = json::array({});

	for (auto const &[key, val] : this->sensors)
	{
		json jSensor = val->to_json();
		jSensors.push_back(jSensor);
	}

	// Serialize to MessagePack for size
	vector<uint8_t> serialized = json::to_msgpack(jSensors);

	this->settingsManager->Write("tempsensors", serialized);

	// continue our temp loop
	this->skipTempLoop = false;

	ESP_LOGI(TAG, "Saving Temp Sensor Settings Done");
}

void BrewEngine::initMqtt()
{
	// return if no broker is configured
	if (this->mqttUri.find("mqtt://") == std::string::npos)
	{
		return;
	}

	ESP_LOGI(TAG, "initMqtt: Start");

	esp_mqtt_client_config_t mqtt5_cfg = {};
	mqtt5_cfg.broker.address.uri = this->mqttUri.c_str();
	mqtt5_cfg.session.protocol_ver = MQTT_PROTOCOL_V_5;
	mqtt5_cfg.network.disable_auto_reconnect = false;

	this->mqttClient = esp_mqtt_client_init(&mqtt5_cfg);
	// atm we don't need an event
	// esp_mqtt_client_register_event(this->mqttClient, MQTT_EVENT_ANY, this->mqtt_event_handler, this);
	esp_err_t err = esp_mqtt_client_start(this->mqttClient);

	if (err != ESP_OK)
	{
		ESP_LOGW(TAG, "Error Creating MQTT Client");
		return;
	}

	// string iso_datetime = this->to_iso_8601(std::chrono::system_clock::now());
	// string iso_date = iso_datetime.substr(0, 10);

	// we create a topic and just post all out data to runningLog, more complex configuration can follow in the future
	this->mqttTopic = "esp-brew-engine/" + this->Hostname + "/history";
	this->mqttTopicLog = "esp-brew-engine/" + this->Hostname + "/log";
	this->mqttEnabled = true;

	ESP_LOGI(TAG, "initMqtt: Done");
}

void BrewEngine::initOneWire()
{
	ESP_LOGI(TAG, "initOneWire: Start");

	onewire_bus_config_t bus_config;
	bus_config.bus_gpio_num = this->oneWire_PIN;

	onewire_bus_rmt_config_t rmt_config;
	rmt_config.max_rx_bytes = 10; // 1byte ROM command + 8byte ROM number + 1byte device command

	ESP_ERROR_CHECK(onewire_new_bus_rmt(&bus_config, &rmt_config, &this->obh));
	ESP_LOGI(TAG, "1-Wire bus installed on GPIO%d", this->oneWire_PIN);

	ESP_LOGI(TAG, "initOneWire: Done");
}

void BrewEngine::detectOnewireTemperatureSensors()
{

	// we need to temp stop our temp read loop while we change the sensor data
	this->skipTempLoop = true;
	vTaskDelay(pdMS_TO_TICKS(2000));

	// sensors are already loaded via json settings, but we need to add handles and status
	onewire_device_iter_handle_t iter = NULL;
	esp_err_t search_result = ESP_OK;

	// create 1-wire device iterator, which is used for device search
	ESP_ERROR_CHECK(onewire_new_device_iter(this->obh, &iter));
	ESP_LOGI(TAG, "Device iterator created, start searching...");

	int i = 0;
	do
	{

		onewire_device_t next_onewire_device = {};

		search_result = onewire_device_iter_get_next(iter, &next_onewire_device);
		if (search_result == ESP_OK)
		{ // found a new device, let's check if we can upgrade it to a DS18B20
			ds18b20_config_t ds_cfg = {};

			ds18b20_device_handle_t newHandle;

			if (ds18b20_new_device(&next_onewire_device, &ds_cfg, &newHandle) == ESP_OK)
			{
				uint64_t sensorId = next_onewire_device.address;

				ESP_LOGI(TAG, "Found a DS18B20[%d], address: %016llX ID:%llu", i, sensorId, sensorId);
				i++;

				if (this->sensors.size() >= ONEWIRE_MAX_DS18B20)
				{
					ESP_LOGI(TAG, "Max DS18B20 number reached, stop searching...");
					break;
				}

				std::map<uint64_t, TemperatureSensor *>::iterator it;
				it = this->sensors.find(sensorId);

				if (it == this->sensors.end())
				{
					ESP_LOGI(TAG, "New Sensor");

					// doesn't exist yet, we need to add it
					auto sensor = new TemperatureSensor();
					sensor->id = sensorId;
					sensor->name = to_string(sensorId);
					sensor->color = "#ffffff";
					sensor->useForControl = true;
					sensor->show = true;
					sensor->connected = true;
					sensor->compensateAbsolute = 0;
					sensor->compensateRelative = 1;
					sensor->handle = newHandle;
					this->sensors.insert_or_assign(sensor->id, sensor);
				}
				else
				{
					ESP_LOGI(TAG, "Existing Sensor");
					// just set connected and handle
					TemperatureSensor *sensor = it->second;
					sensor->handle = newHandle;
					sensor->connected = true;
				}

				// set resolution for all DS18B20s
				ds18b20_set_resolution(newHandle, DS18B20_RESOLUTION_12B);
			}
			else
			{
				ESP_LOGI(TAG, "Found an unknown device, address: %016llX", next_onewire_device.address);
			}
		}
	} while (search_result != ESP_ERR_NOT_FOUND);

	ESP_ERROR_CHECK(onewire_del_device_iter(iter));
	ESP_LOGI(TAG, "Searching done, %d DS18B20 device(s) found", this->sensors.size());

	this->skipTempLoop = false;
}

void BrewEngine::start()
{
	// don't start if we are already running
	if (!this->controlRun)
	{
		this->controlRun = true;
		this->inOverTime = false;
		this->boostStatus = Off;
		this->overrideTargetTemperature = std::nullopt;
		// clear old temp log
		this->tempLog.clear();

		// also clear old steps
		for (auto const &step : this->executionSteps)
		{
			delete step.second;
		}
		this->executionSteps.clear();

		if (this->selectedMashScheduleName.empty() == false)
		{
			this->loadSchedule();
			this->currentMashStep = 1; // 0 is current temp, so we can start at 1
			xTaskCreate(&this->controlLoop, "controlloop_task", 4096, this, 5, NULL);
		}
		else
		{

			// if no schedule is selected, we set the boil flag based on temperature
			if ((this->temperatureScale == Celsius && this->targetTemperature >= 100) || (this->temperatureScale == Fahrenheit && this->targetTemperature >= 212))
			{
				this->boilRun = true;
			}
			else
			{
				this->boilRun = false;
			}
		}

		xTaskCreate(&this->pidLoop, "pidloop_task", 8192, this, 5, NULL);

		xTaskCreate(&this->outputLoop, "outputloop_task", 4096, this, 5, NULL);

		this->statusText = "Running";
	}
}

void BrewEngine::loadSchedule()
{
	auto pos = this->mashSchedules.find(this->selectedMashScheduleName);

	if (pos == this->mashSchedules.end())
	{
		ESP_LOGE(TAG, "Program with name: %s not found!", this->selectedMashScheduleName.c_str());
		return;
	}
	auto schedule = pos->second;

	system_clock::time_point prevTime = std::chrono::system_clock::now();

	for (auto const &step : this->executionSteps)
	{
		delete step.second;
	}
	this->executionSteps.clear();

	this->currentExecutionStep = 0;
	this->boilRun = schedule->boil;
	int stepIndex = 0;

	float prevTemp = this->temperature;
	// insert the current as starting point
	auto execStep0 = new ExecutionStep();
	execStep0->time = prevTime;
	execStep0->temperature = prevTemp;
	execStep0->extendIfNeeded = false;
	this->executionSteps.insert(std::make_pair(stepIndex, execStep0));

	string iso_string = this->to_iso_8601(prevTime);
	ESP_LOGI(TAG, "Time:%s, Temp:%f Extend:%d", iso_string.c_str(), prevTemp, execStep0->extendIfNeeded);

	int extendNotifications = 0;

	stepIndex++;

	for (auto const &step : schedule->steps)
	{
		// a step can actualy be 2 different executions, 1 step time that needs substeps calcualted, and one fixed

		if (step->stepTime > 0 || step->extendStepTimeIfNeeded)
		{

			int stepTime = step->stepTime;

			// when the users request step extended, we need a step so 0 isn't valid we default to 1 min
			if (stepTime == 0)
			{
				stepTime = 1;
				extendNotifications += 60;
			}

			auto stepEndTime = prevTime + minutes(stepTime);

			int subStepsInStep;

			// When boost mode is active we don't want substeps this only complicates things
			if (step->allowBoost && this->boostModeUntil > 0)
			{
				subStepsInStep = 1;
			}
			else
			{
				auto secondsInStep = chrono::duration_cast<chrono::seconds>(stepEndTime - prevTime).count();
				subStepsInStep = (secondsInStep / this->stepInterval);

				// we need atleast one step
				if (subStepsInStep < 1)
				{
					subStepsInStep = 1;
				}
			}

			float tempDiffPerStep = (step->temperature - prevTemp) / (float)subStepsInStep;

			float prevStepTemp = 0;

			for (int j = 0; j < subStepsInStep; j++)
			{
				system_clock::time_point executionStepTime = prevTime;
				executionStepTime += seconds((j + 1) * stepInterval);

				float subStepTemp = prevTemp + (tempDiffPerStep * ((float)j + 1));

				// insert the current as starting point
				auto execStep = new ExecutionStep();
				execStep->time = executionStepTime;
				execStep->temperature = subStepTemp;
				execStep->extendIfNeeded = false;

				if (step->allowBoost && this->boostModeUntil > 0)
				{
					execStep->allowBoost = true;
				}
				else
				{
					execStep->allowBoost = false;
				}

				// set extend if needed on last step if configured
				if (j == (subStepsInStep - 1) && step->extendStepTimeIfNeeded)
				{
					execStep->extendIfNeeded = true;
				}

				float diff = abs(subStepTemp - prevStepTemp);
				// ESP_LOGI(TAG, "Diff:%f, subStepTemp:%f prevStepTemp:%f", diff, subStepTemp, prevStepTemp);

				// only insert if difference or if last step more then 1 degree
				if (diff > 1 || (j == subStepsInStep - 1))
				{
					this->executionSteps.insert(std::make_pair(stepIndex, execStep));
					prevStepTemp = execStep->temperature;
					stepIndex++;

					// Convert the time_point to an ISO 8601 string
					string iso_string = this->to_iso_8601(executionStepTime);

					ESP_LOGI(TAG, "Time:%s, Temp:%f Extend:%d", iso_string.c_str(), subStepTemp, execStep->extendIfNeeded);
				}
			}

			prevTime = stepEndTime;
			prevTemp = prevStepTemp;
		}
		else
		{
			// we start in 10 seconds
			auto stepEndTime = prevTime + seconds(10);

			// go directly to temp
			auto execStep = new ExecutionStep();
			execStep->time = stepEndTime;
			execStep->temperature = (float)step->temperature;
			execStep->extendIfNeeded = step->extendStepTimeIfNeeded;

			this->executionSteps.insert(std::make_pair(stepIndex, execStep));

			stepIndex++;

			// Convert the time_point to an ISO 8601 string
			string iso_string = this->to_iso_8601(prevTime);

			ESP_LOGI(TAG, "Time:%s, Temp:%f Extend:%d", iso_string.c_str(), (float)step->temperature, execStep->extendIfNeeded);

			prevTime = stepEndTime;
			prevTemp = (float)step->temperature;
		}

		// for the hold time we just need add one point
		auto holdEndTime = prevTime + minutes(step->time);

		auto holdStep = new ExecutionStep();
		holdStep->time = holdEndTime;
		holdStep->temperature = (float)step->temperature;
		holdStep->extendIfNeeded = false;

		this->executionSteps.insert(std::make_pair(stepIndex, holdStep));
		stepIndex++;

		prevTime = holdEndTime;
		prevTemp = step->temperature; // is normaly the same but this could change in futrure

		string iso_string2 = this->to_iso_8601(holdEndTime);
		ESP_LOGI(TAG, "Hold Time:%s, Temp:%f ", iso_string2.c_str(), (float)step->temperature);
	}

	// also add notifications
	for (auto const &notification : this->notifications)
	{
		delete notification;
	}
	this->notifications.clear();

	for (auto const &notification : schedule->notifications)
	{
		auto notificationTime = execStep0->time + minutes(notification->timeFromStart) + seconds(extendNotifications);

		// copy notification to new map
		auto newNotification = new Notification();
		newNotification->name = notification->name;
		newNotification->message = notification->message;
		newNotification->timeFromStart = notification->timeFromStart + (extendNotifications / 60); // in minutes
		newNotification->timePoint = notificationTime;

		this->notifications.push_back(newNotification);
	}

	// increate version so client can follow changes
	this->runningVersion++;
}

void BrewEngine::recalculateScheduleAfterOverTime()
{
	ESP_LOGI(TAG, "Recalculate Schedule after OverTime");

	int currentStepIndex = this->currentMashStep;

	auto currentPos = this->executionSteps.find(currentStepIndex);

	if (currentPos == this->executionSteps.end())
	{
		ESP_LOGE(TAG, "Steps not availible anymore");
		this->stop();
		return;
	}

	auto currentStep = currentPos->second;
	system_clock::time_point plannedEnd = currentStep->time;

	system_clock::time_point now = std::chrono::system_clock::now();
	auto extraSeconds = chrono::duration_cast<chrono::seconds>(now - plannedEnd).count();

	for (auto it = currentPos; it != this->executionSteps.end(); ++it)
	{
		auto step = it->second;
		auto newTime = step->time + seconds(extraSeconds);

		string iso_string = this->to_iso_8601(step->time);
		string iso_string2 = this->to_iso_8601(newTime);

		ESP_LOGI(TAG, "Time Changend From: %s, To:%s ", iso_string.c_str(), iso_string2.c_str());

		step->time = newTime;
	}

	// also increase notifications
	for (auto &notification : this->notifications)
	{
		if (!notification->done)
		{
			auto newTime = notification->timePoint + seconds(extraSeconds);
			

			string iso_string = this->to_iso_8601(notification->timePoint);
			string iso_string2 = this->to_iso_8601(newTime);

			ESP_LOGI(TAG, "Notification Time Changend From: %s, To:%s ", iso_string.c_str(), iso_string2.c_str());

			notification->timePoint = newTime;
		}
	}

	// increate version so client can follow changes
	this->runningVersion++;
}

void BrewEngine::stop()
{
	this->controlRun = false;
	this->boostStatus = Off;
	this->inOverTime = false;
	this->statusText = "Idle";
}

void BrewEngine::startStir(const json &stirConfig)
{
	if (!this->stir_PIN)
	{
		ESP_LOGW(TAG, "StirPin is not configured, ignoring startStir!");
		return;
	}

	system_clock::time_point now = std::chrono::system_clock::now();
	this->stirStartCycle = now;

	if (!stirConfig["max"].is_null() && stirConfig["max"].is_number())
	{
		this->stirTimeSpan = stirConfig["max"];
	}

	if (!stirConfig["intervalStart"].is_null() && stirConfig["intervalStart"].is_number())
	{
		this->stirIntervalStart = stirConfig["intervalStart"];
	}

	if (!stirConfig["intervalStop"].is_null() && stirConfig["intervalStop"].is_number())
	{
		this->stirIntervalStop = stirConfig["intervalStop"];
	}

	this->stirRun = true;

	xTaskCreate(&this->stirLoop, "stirloop_task", 4096, this, 10, &this->stirLoopHandle);

	this->stirStatusText = "Running";
}

void BrewEngine::stopStir()
{
	if (!this->stir_PIN)
	{
		ESP_LOGW(TAG, "StirPin is not configured, ignoring stopStir!");
		return;
	}

	this->stirRun = false;

	// stop at once
	gpio_set_level(this->stir_PIN, this->gpioLow);

	this->stirStatusText = "Idle";
}

void BrewEngine::stirLoop(void *arg)
{
	BrewEngine *instance = (BrewEngine *)arg;

	while (instance->run && instance->stirRun)
	{
		if (instance->stirIntervalStart == 0 && instance->stirIntervalStop == instance->stirTimeSpan)
		{
			// always on, just set high and wait for end
			gpio_set_level(instance->stir_PIN, instance->gpioHigh);
		}
		else
		{
			system_clock::time_point now = std::chrono::system_clock::now();

			auto startStirTime = instance->stirStartCycle + minutes(instance->stirIntervalStart);
			auto stopStirTime = instance->stirStartCycle + minutes(instance->stirIntervalStop);

			auto cycleEnd = instance->stirStartCycle + minutes(instance->stirTimeSpan);

			if (now >= startStirTime && now <= stopStirTime)
			{
				gpio_set_level(instance->stir_PIN, instance->gpioHigh);
			}
			else
			{
				gpio_set_level(instance->stir_PIN, instance->gpioLow);
			}

			// string iso_string1 = instance->to_iso_8601(now);
			// string iso_string2 = instance->to_iso_8601(startStirTime);
			// string iso_string3 = instance->to_iso_8601(stopStirTime);
			// string iso_string4 = instance->to_iso_8601(cycleEnd);

			// ESP_LOGI(TAG, "Stir Cycle Now: %s Start:%s Stop:%s CycleEnd: %s", iso_string1.c_str(), iso_string2.c_str(), iso_string3.c_str(), iso_string4.c_str());

			// start next cycle
			if (now >= cycleEnd)
			{
				instance->stirStartCycle = cycleEnd;
			}
		}

		vTaskDelay(pdMS_TO_TICKS(1000));
	}

	vTaskDelete(NULL);
}

void BrewEngine::readLoop(void *arg)
{
	BrewEngine *instance = (BrewEngine *)arg;

	int it = 0;

	while (instance->run)
	{
		vTaskDelay(pdMS_TO_TICKS(1000));

		// When we are changing temp settings we temporarily need to skip our temp loop
		if (instance->skipTempLoop)
		{
			continue;
		}

		int nrOfSensors = 0;
		float sum = 0.0;

		for (auto &[key, sensor] : instance->sensors)
		{
			float temperature;
			ds18b20_device_handle_t handle = sensor->handle;
			string stringId = std::to_string(key);

			// not useForControl or connected, continue
			if (!sensor->handle || !sensor->connected)
			{
				continue;
			}

			esp_err_t err = ds18b20_trigger_temperature_conversion(handle);

			if (err != ESP_OK)
			{
				ESP_LOGW(TAG, "Error Reading from [%s], disabling sensor!", stringId.c_str());
				sensor->connected = false;
				sensor->lastTemp = 0;
				instance->currentTemperatures.erase(key);
				continue;
			};

			err = ds18b20_get_temperature(handle, &temperature);

			if (err != ESP_OK)
			{
				ESP_LOGW(TAG, "Error Reading from [%s], disabling sensor!", stringId.c_str());
				sensor->connected = false;
				sensor->lastTemp = 0;
				instance->currentTemperatures.erase(key);
				continue;
			};

			// conversion needed
			if (instance->temperatureScale == Fahrenheit)
			{
				temperature = (temperature * 1.8) + 32;
			}

			ESP_LOGD(TAG, "temperature read from [%s]: %.2f°", stringId.c_str(), temperature);

			// apply compensation
			if (sensor->compensateAbsolute != 0)
			{
				temperature = temperature + sensor->compensateAbsolute;
			}
			if (sensor->compensateRelative != 0 && sensor->compensateRelative != 1)
			{
				temperature = temperature * sensor->compensateRelative;
			}

			if (sensor->useForControl)
			{
				sum += temperature;
				nrOfSensors++;
			}

			sensor->lastTemp = temperature;

			// we also add our temps to a map individualy, might be nice to see bottom and top temp in gui
			if (sensor->show)
			{
				instance->currentTemperatures.insert_or_assign(key, sensor->lastTemp);
			}
		}

		float avg = sum / nrOfSensors;
/*		float avg = 0;
		if (nrOfSensors > 0) 
		{
			avg = sum / nrOfSensors;
		}*/

		ESP_LOGD(TAG, "Avg Temperature: %.2f°", avg);

		instance->temperature = avg;

		// when controlrun is true we need to keep out data
		if (instance->controlRun)
		{
			// we don't have that much ram so we log only every 5 cycles

			it++;
			if (it > 5)
			{
				it = 0;
				int lastTemp = 0;

				if (!instance->tempLog.empty())
				{
					auto lastValue = instance->tempLog.rbegin();
					lastTemp = lastValue->second;
				}

				if (lastTemp != (int)avg)
				{
					// decided agains chrono just make it a hell lot more complex
					// instance->tempLog.insert(std::make_pair(std::chrono::system_clock::now(), (int)avg));
					time_t current_raw_time = time(0);
					// System time: number of seconds since 00:00,
					instance->tempLog.insert(std::make_pair(current_raw_time, (int)avg));

					ESP_LOGI(TAG, "Logging: %d°", (int)avg);
				}
				else
				{
					ESP_LOGI(TAG, "Skip same");
				}
			}

			if (instance->mqttEnabled)
			{
				string iso_datetime = to_iso_8601(std::chrono::system_clock::now());
				json jPayload;
				jPayload["time"] = iso_datetime;
				jPayload["temp"] = instance->temperature;
				jPayload["target"] = instance->targetTemperature;
				jPayload["output"] = instance->pidOutput;
				string payload = jPayload.dump();

				esp_mqtt_client_publish(instance->mqttClient, instance->mqttTopic.c_str(), payload.c_str(), 0, 1, 1);
			}
		}
	}

	vTaskDelete(NULL);
}

void BrewEngine::pidLoop(void *arg)
{
	BrewEngine *instance = (BrewEngine *)arg;

	double kP, kI, kD;
	if (instance->boilRun)
	{
		kP = instance->boilkP;
		kI = instance->boilkI;
		kD = instance->boilkD;
	}
	else
	{
		kP = instance->mashkP;
		kI = instance->mashkI;
		kD = instance->mashkD;
	}

	PIDController pid(kP, kI, kD);
	pid.setMin(0);
	pid.setMax(100);
	pid.debug = false;

	uint totalWattage = 0;

	// we calculate the total wattage we have availible, depens on heaters and on mash or boil
	for (auto &heater : instance->heaters)
	{

		if (instance->boilRun && heater->useForBoil)
		{
			totalWattage += heater->watt;
			heater->enabled = true;
		}
		else if (!instance->boilRun && heater->useForMash)
		{
			totalWattage += heater->watt;
			heater->enabled = true;
		}
		else
		{
			heater->enabled = false;
		}
	}

	while (instance->run && instance->controlRun)
	{
		// Output is %
		int outputPercent = (int)pid.getOutput((double)instance->temperature, (double)instance->targetTemperature);
		instance->pidOutput = outputPercent;
		ESP_LOGI(TAG, "Pid Output: %d Target: %f", instance->pidOutput, instance->targetTemperature);

		// Manual override and boost
		if (instance->manualOverrideOutput.has_value())
		{
			// Here we don't override the pidOutput display since we want the user to see the pid values even when overriding
			outputPercent = instance->manualOverrideOutput.value();
		}
		else if (instance->boostStatus == Boost)
		{
			outputPercent = 100;
			instance->pidOutput = 100;
		}
		else if (instance->heaterLimit < outputPercent)
		{
			outputPercent = instance->heaterLimit;
			instance->pidOutput = instance->heaterLimit;
		}
		else if (instance->boostStatus == Rest)
		{
			outputPercent = 0;
			instance->pidOutput = 0;
		}

		// set all to 0
		for (auto &heater : instance->heaters)
		{
			heater->burnTime = 0;
		}

		// calc the wattage we need
		int outputWatt = (totalWattage / 100) * outputPercent;

		// we need to calculate our burn time per output
		for (auto &heater : instance->heaters)
		{
			if (!heater->enabled)
			{
				continue;
			}

			if (outputWatt < 0)
			{
				break;
			}

			// we can complete it with this heater
			if (heater->watt > outputWatt)
			{
				heater->burnTime = (int)(((double)outputWatt / (double)heater->watt) * 100);
				
				if (heater->burnTime <= instance->relayGuard/2)
				{
					heater->burnTime=0;
				}
				else if (heater->burnTime <= instance->relayGuard)
				{
					heater->burnTime=instance->relayGuard;
				}

				if (heater->burnTime >= 100 - instance->relayGuard/2)
				{
					heater->burnTime=100;
				}
				else if (heater->burnTime >= 100 - instance->relayGuard)
				{
					heater->burnTime=100 - instance->relayGuard;
				}
				
				ESP_LOGD(TAG, "Pid Calc Heater %s: OutputWatt: %d Burn: %d", heater->name.c_str(), outputWatt, heater->burnTime);
				break;
			}
			else
			{
				// we can't complete it, take out part and continue
				outputWatt -= heater->watt;
				heater->burnTime = 100;
				ESP_LOGD(TAG, "Pid Calc Heater %s: OutputWatt: %d Burn: 100", heater->name.c_str(), outputWatt);
			}
		}

		// Shorter heater cycles for even temperature and prevent hot spots
		int heaterLoopTime = instance->pidLoopTime / instance->heaterCycles;
		
		// we keep going for the desired pidlooptime and set the burn by percent
		for (int i = 0; i < instance->pidLoopTime / instance->heaterCycles; i++)
		{
			if (!instance->run || !instance->controlRun)
			{
				break;
			}

			for (auto &heater : instance->heaters)
			{
				if (!heater->enabled)
				{
					continue;
				}

				int burnUntil = 0;

				if (heater->burnTime > 0)
				{
					burnUntil = ((double)heater->burnTime / 100) * (double)instance->pidLoopTime / (double)instance->heaterCycles; // convert % back to seconds (per heater cycle)
				}

				if (burnUntil > i % heaterLoopTime) // on 
				{
					if (heater->burn != true) // only when not current, we don't want to spam the logs
					{
						heater->burn = true;
						ESP_LOGD(TAG, "Heater %s: On", heater->name.c_str());
					}
				}
				else // off
				{
					if (heater->burn != false) // only when not current, we don't want to spam the logs
					{
						heater->burn = false;
						ESP_LOGD(TAG, "Heater %s: Off", heater->name.c_str());
					}
				}
			}

			// when our target changes we also update our pid target
			if (instance->resetPitTime)
			{
				ESP_LOGI(TAG, "Reset Pid Timer");
				instance->resetPitTime = false;
				break;
			}

			vTaskDelay(pdMS_TO_TICKS(1000));
		}
	}

	instance->pidOutput = 0;

	vTaskDelete(NULL);
}

void BrewEngine::outputLoop(void *arg)
{
	BrewEngine *instance = (BrewEngine *)arg;

	for (auto const &heater : instance->heaters)
	{
		gpio_set_level(heater->pinNr, instance->gpioLow);
		gpio_set_level(GPIO_NUM_2, 0); //Test led
	}

	while (instance->run && instance->controlRun)
	{
		vTaskDelay(pdMS_TO_TICKS(1000));

		for (auto const &heater : instance->heaters)
		{
			if (heater->burn)
			{
				ESP_LOGD(TAG, "Output %s: On", heater->name.c_str());
				gpio_set_level(heater->pinNr, instance->gpioHigh);
				gpio_set_level(GPIO_NUM_2, 1); //Test led

			}
			else
			{
				ESP_LOGD(TAG, "Output %s: Off", heater->name.c_str());
				gpio_set_level(heater->pinNr, instance->gpioLow);
				gpio_set_level(GPIO_NUM_2, 0); //Test led
			}
		}
	}

	// set outputs off and quit thread
	for (auto const &heater : instance->heaters)
	{
		gpio_set_level(heater->pinNr, instance->gpioLow);
		gpio_set_level(GPIO_NUM_2, 0); //Test led

	}

	vTaskDelete(NULL);
}

void BrewEngine::controlLoop(void *arg)
{
	BrewEngine *instance = (BrewEngine *)arg;

	// the pid needs to reset one step later so the next temp is set, oherwise it has a delay
	bool resetPIDNextStep = false;

	// For boost mode to see if temp starts to drop
	float prevTemperature = instance->temperature;
	uint boostUntil = 0;

	while (instance->run && instance->controlRun)
	{

		system_clock::time_point now = std::chrono::system_clock::now();

		if (instance->executionSteps.size() >= instance->currentMashStep)
		{ // there are more steps
			int nextStepIndex = instance->currentMashStep;

			auto nextStep = instance->executionSteps.at(nextStepIndex);

			system_clock::time_point nextAction = nextStep->time;

			bool gotoNextStep = false;

			// set target when not overriden
			if (instance->overrideTargetTemperature.has_value())
			{
				instance->targetTemperature = instance->overrideTargetTemperature.value();
			}
			else
			{
				instance->targetTemperature = nextStep->temperature;
			}

			uint secondsToGo = 0;
			// if its smaller 0 is ok!
			if (nextAction > now)
			{
				secondsToGo = chrono::duration_cast<chrono::seconds>(nextAction - now).count();
			}

			// Boost mode logic
			if (nextStep->allowBoost)
			{
				if (boostUntil == 0)
				{
					boostUntil = (uint)((nextStep->temperature / 100) * (float)instance->boostModeUntil);
				}

				if (instance->boostStatus == Off && instance->temperature < boostUntil)
				{

					ESP_LOGI(TAG, "Boost Start Until: %d", boostUntil);
					instance->logRemote("Boost Start");
					instance->boostStatus = Boost;
				}
				else if (instance->boostStatus == Boost && instance->temperature >= boostUntil)
				{
					// When in boost mode we wait unit boost temp is reched, pid is locked to 100% in boost mode
					ESP_LOGI(TAG, "Boost Rest Start");
					instance->logRemote("Boost Rest Start");
					instance->boostStatus = Rest;
				}
				else if (instance->boostStatus == Rest && instance->temperature < prevTemperature)
				{
					// When in boost rest mode, we wait until temperature drops pid is locked to 0%
					ESP_LOGI(TAG, "Boost Rest End");
					instance->logRemote("Boost Rest End");
					instance->boostStatus = Off;

					// Reset pid
					instance->resetPitTime = true;
				}
			}

			if (secondsToGo < 1)
			{ // change temp and increment Currentstep

				// string iso_string = instance->to_iso_8601(nextStep->time);
				// ESP_LOGI(TAG, "Control Time:%s, TempCur:%f, TempTarget:%d, Extend:%d, Overtime: %d", iso_string.c_str(), instance->temperature, nextStep->temperature, nextStep->extendIfNeeded, instance->inOverTime);

				if (nextStep->extendIfNeeded == true && instance->inOverTime == false && (nextStep->temperature - instance->temperature) >= instance->tempMargin)
				{
					// temp must be reached, we keep going but need to triger a recaluclation event when done
					ESP_LOGI(TAG, "OverTime Start");
					instance->logRemote("OverTime Start");
					instance->inOverTime = true;
				}
				else if (instance->inOverTime == true && (nextStep->temperature - instance->temperature) <= instance->tempMargin)
				{
					// we reached out temp after overtime, we need to recalc the rest and start going again
					ESP_LOGI(TAG, "OverTime Done");
					instance->logRemote("OverTime Done");
					instance->inOverTime = false;
					instance->recalculateScheduleAfterOverTime();
					gotoNextStep = true;
				}
				else if (instance->inOverTime == false)
				{
					ESP_LOGI(TAG, "Going to next Step");
					gotoNextStep = true;
					// also reset override on step change
					instance->overrideTargetTemperature = std::nullopt;
				}

				// else when in overtime just keep going until we reach temp
			}

			// the pid needs to reset one step later so the next temp is set, oherwise it has a delay
			if (resetPIDNextStep)
			{
				resetPIDNextStep = false;
				instance->resetPitTime = true;
			}

			if (gotoNextStep)
			{
				instance->currentMashStep++;

				// Also reset boost
				instance->boostStatus = Off;

				resetPIDNextStep = true;
			}

			// notifications, but only when not in overtime
			if (!instance->inOverTime && !instance->notifications.empty())
			{
				// filter out items that are not done
				auto isNotDone = [](Notification *notification)
				{ return notification->done == false; };

				auto notDone = instance->notifications | views::filter(isNotDone);

				if (!notDone.empty())
				{
					// they are sorted so we just have to check the first one
					auto first = notDone.front();

					if (now > first->timePoint)
					{
						ESP_LOGI(TAG, "Notify %s", first->name.c_str());

						string buzzerName = "buzzer" + first->name;
						xTaskCreate(&instance->buzzer, buzzerName.c_str(), 1024, instance, 10, NULL);
						instance->soundTime = instance->buzzerTime * 1000;
						instance->soundBurst = 300 ; // in milliseconds
						xTaskCreate(&instance->speaker, buzzerName.c_str(), 4096, instance, 10, NULL);

						first->done = true;
					}
				}
			}
		}
		else
		{
			// last step need to stop
			ESP_LOGI(TAG, "Program Finished");
			instance->stop();
		}

		// For boost mode to see if temp starts to drop
		prevTemperature = instance->temperature;

		vTaskDelay(pdMS_TO_TICKS(1000));
	}

	vTaskDelete(NULL);
}

string BrewEngine::bootIntoRecovery()
{
	// recovery is our factory
	esp_partition_subtype_t t = ESP_PARTITION_SUBTYPE_APP_FACTORY;
	const esp_partition_t *factory_partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, t, NULL);

	if (factory_partition == NULL)
	{
		ESP_LOGE(TAG, "Factory Partition not found!");
		return "Error: Factory Partition not found!";
	}

	if (esp_ota_set_boot_partition(factory_partition) == ESP_OK)
	{
		return "Device is booting into recovery hold on!";
	}

	return "Error: Unable to chage boot Partition!";
}

void BrewEngine::reboot(void *arg)
{
	vTaskDelay(2000 / portTICK_PERIOD_MS);
	esp_restart();
}

void BrewEngine::buzzer(void *arg)
{
	BrewEngine *instance = (BrewEngine *)arg;

	if (instance->buzzer_PIN > 0)
	{
		auto buzzerMs = instance->buzzerTime * 1000;
		gpio_set_level(instance->buzzer_PIN, instance->gpioHigh);
		vTaskDelay(buzzerMs / portTICK_PERIOD_MS);
		gpio_set_level(instance->buzzer_PIN, instance->gpioLow);
	}

	vTaskDelete(NULL);
}

void BrewEngine::speaker(void *arg)
{
	BrewEngine *instance = (BrewEngine *)arg; 

	ESP_LOGI(TAG, "Speaker function started"); 


	if ((instance->speaker1_PIN > 0) && (instance->speaker2_PIN > 0))
	{
		ledc_timer_config_t ledc_timer = {};  // Zero-initialize the structure
		ledc_timer.speed_mode = LEDC_LOW_SPEED_MODE;
		ledc_timer.timer_num = LEDC_TIMER_0;
		ledc_timer.duty_resolution = LEDC_TIMER_8_BIT;
		ledc_timer.freq_hz = 1800;
		ledc_timer.clk_cfg = LEDC_AUTO_CLK;	
		ledc_timer_config(&ledc_timer);

		
		ledc_channel_config_t ledc_channel1; 	 // Define 1st PWM channel
		ledc_channel1.speed_mode     = LEDC_LOW_SPEED_MODE;
		ledc_channel1.channel        = LEDC_CHANNEL_0;
		ledc_channel1.timer_sel      = LEDC_TIMER_0;
		ledc_channel1.intr_type      = LEDC_INTR_DISABLE;
		ledc_channel1.gpio_num       = instance->speaker1_PIN;  // First GPIO pin
		ledc_channel1.duty           = 128;			//Relative to timer resolution
		ledc_channel1.hpoint         = 0;
		ledc_channel1.flags.output_invert = 0;


		ledc_channel_config_t ledc_channel2 = ledc_channel1;// Define 2nd PWM channel
		ledc_channel1.channel        = LEDC_CHANNEL_1;
		ledc_channel1.gpio_num       = instance->speaker2_PIN;  // First GPIO pin
		ledc_channel1.hpoint         = 128;			//Shift half period instead of inverting.

		ledc_channel_config(&ledc_channel1);
		ledc_channel_config(&ledc_channel2);
		
		
		for (int i = 0; i < instance->soundTime; i += 2*instance->soundBurst)
		{
		
			//generate sound for burst time
			// Sound volume is proportional to duty. Can be utilized later.
			ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 64);
			ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 64);

			ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
			ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);

			vTaskDelay(instance->soundBurst  / portTICK_PERIOD_MS);
			
			//stop sound for same

			ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
			ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);

			
			vTaskDelay(instance->soundBurst  / portTICK_PERIOD_MS);
			
		}
	}
	vTaskDelete(NULL);
}

string BrewEngine::processCommand(const string &payLoad)
{
	ESP_LOGD(TAG, "payLoad %s", payLoad.c_str());

	json jCommand = json::parse(payLoad);
	string command = jCommand["command"];
	json data = jCommand["data"];

	ESP_LOGD(TAG, "processCommand %s", command.c_str());
	ESP_LOGD(TAG, "data %s", data.dump().c_str());

	json resultData = {};
	string message = "";
	bool success = true;

	if (command == "Data")
	{
		time_t lastLogDateTime = time(0);

		json jTempLog = json::array({});
		if (!this->tempLog.empty())
		{
			auto lastLog = this->tempLog.rend();
			lastLogDateTime = lastLog->first;

			// If we have a last date we only need to send the log increment
			if (!data["lastDate"].is_null() && data["lastDate"].is_number())
			{
				time_t lastClientDate = (time_t)data["lastDate"];
				ESP_LOGD(TAG, "lastClientDate %s", ctime(&lastClientDate));

				// most efficient seems to loop reverse and add until date is reached
				for (auto iter = this->tempLog.rbegin(); iter != this->tempLog.rend(); ++iter)
				{
					if (iter->first > lastClientDate)
					{
						json jTempLogItem;
						jTempLogItem["time"] = iter->first;
						jTempLogItem["temp"] = iter->second;
						jTempLog.push_back(jTempLogItem);
					}
					else
					{
						break;
					}
				}
			}
			else
			{
				for (auto iter = this->tempLog.rbegin(); iter != this->tempLog.rend(); ++iter)
				{
					json jTempLogItem;
					jTempLogItem["time"] = iter->first;
					jTempLogItem["temp"] = iter->second;
					jTempLog.push_back(jTempLogItem);
				}
			}
		}

		// currenttemps is an array of current temps, they are not necessarily all used for control
		json jCurrentTemps = json::array({});
		for (auto const &[key, val] : this->currentTemperatures)
		{
			json jCurrentTemp;
			jCurrentTemp["sensor"] = to_string(key);			   // js doesn't support unint64
			jCurrentTemp["temp"] = (double)((int)(val * 10)) / 10; // round float to 1 digit for display
			jCurrentTemps.push_back(jCurrentTemp);
		}

		resultData = {
			{"temp", (double)((int)(this->temperature * 10)) / 10}, // round float to 1 digit for display
			{"temps", jCurrentTemps},
			{"targetTemp", (double)((int)(this->targetTemperature * 10)) / 10}, // round float to 1 digit for display,
			{"manualOverrideTargetTemp", nullptr},
			{"output", this->pidOutput},
			{"manualOverrideOutput", nullptr},
			{"status", this->statusText},
			{"stirStatus", this->stirStatusText},
			{"lastLogDateTime", lastLogDateTime},
			{"tempLog", jTempLog},
			{"runningVersion", this->runningVersion},
			{"inOverTime", this->inOverTime},
			{"boostStatus", this->boostStatus},
		};

		if (this->manualOverrideOutput.has_value())
		{
			resultData["manualOverrideOutput"] = this->manualOverrideOutput.value();
		}

		if (this->overrideTargetTemperature.has_value())
		{
			resultData["manualOverrideTargetTemp"] = this->overrideTargetTemperature.value();
		}
	}
	else if (command == "GetRunningSchedule")
	{
		json jRunningSchedule;
		jRunningSchedule["version"] = this->runningVersion;

		json jExecutionSteps = json::array({});
		for (auto const &[key, val] : this->executionSteps)
		{
			json jExecutionStep = val->to_json();
			jExecutionSteps.push_back(jExecutionStep);
		}
		jRunningSchedule["steps"] = jExecutionSteps;

		json jNotifications = json::array({});
		for (auto &notification : this->notifications)
		{
			json jNotification = notification->to_json();
			jNotifications.push_back(jNotification);
		}
		jRunningSchedule["notifications"] = jNotifications;

		resultData = jRunningSchedule;
	}
	else if (command == "SetTemp")
	{

		if (data["targetTemp"].is_null())
		{
			this->overrideTargetTemperature = std::nullopt;

			// when not in a program also direclty set targtetemp
			if (this->selectedMashScheduleName.empty() == true)
			{
				this->targetTemperature = 0;
			}
		}
		else if (data["targetTemp"].is_number())
		{

			this->overrideTargetTemperature = (float)data["targetTemp"];

			// when not in a program also direclty set targtetemp
			if (this->selectedMashScheduleName.empty() == true)
			{
				this->targetTemperature = this->overrideTargetTemperature.value();
			}
		}
		else
		{
			this->overrideTargetTemperature = std::nullopt;

			message = "Incorrect data, integer or float expected!";
			success = false;
		}
	}
	else if (command == "SetOverrideOutput")
	{

		if (data["output"].is_null() == false && data["output"].is_number())
		{
			this->manualOverrideOutput = (int)data["output"];
		}
		else
		{
			this->manualOverrideOutput = std::nullopt;
		}

		// reset so effect is immidiate
		this->resetPitTime = true;
	}
	else if (command == "Start")
	{
		if (data["selectedMashSchedule"].is_null())
		{
			this->selectedMashScheduleName.clear();
		}
		else
		{
			this->selectedMashScheduleName = (string)data["selectedMashSchedule"];
		}

		this->start();
	}
	else if (command == "StartStir")
	{
		this->startStir(data);
	}
	else if (command == "Stop")
	{
		this->stop();
	}
	else if (command == "StopStir")
	{
		this->stopStir();
	}
	else if (command == "GetMashSchedules")
	{

		json jSchedules = json::array({});

		for (auto const &[key, val] : this->mashSchedules)
		{
			json jSchedule = val->to_json();
			jSchedules.push_back(jSchedule);
		}

		resultData = jSchedules;
	}
	else if (command == "SaveMashSchedule")
	{
		this->setMashSchedule(data);

		this->saveMashSchedules();
	}
	else if (command == "SetMashSchedule") // used by import function to set but not save
	{
		this->setMashSchedule(data);
	}
	else if (command == "DeleteMashSchedule")
	{
		string deleteName = (string)data["name"];

		auto pos = this->mashSchedules.find(deleteName);

		if (pos == this->mashSchedules.end())
		{
			message = "Schedule with name: " + deleteName + " not found";
			success = false;
		}
		else
		{
			this->mashSchedules.erase(pos);
			this->saveMashSchedules();
		}
	}
	else if (command == "GetPIDSettings")
	{
		resultData = {
			{"kP", this->mashkP},
			{"kI", this->mashkI},
			{"kD", this->mashkD},
			{"boilkP", this->boilkP},
			{"boilkI", this->boilkI},
			{"boilkD", this->boilkD},
			{"pidLoopTime", this->pidLoopTime},
			{"stepInterval", this->stepInterval},
			{"boostModeUntil", this->boostModeUntil},
			{"heaterLimit", this->heaterLimit},
			{"heaterCycles", this->heaterCycles},
			{"relayGuard", this->relayGuard},
		};
	}
	else if (command == "SavePIDSettings")
	{
		this->mashkP = data["kP"].get<double>();
		this->mashkI = data["kI"].get<double>();
		this->mashkD = data["kD"].get<double>();
		this->boilkP = data["boilkP"].get<double>();
		this->boilkI = data["boilkI"].get<double>();
		this->boilkD = data["boilkD"].get<double>();
		this->pidLoopTime = data["pidLoopTime"].get<uint16_t>();
		this->stepInterval = data["stepInterval"].get<uint16_t>();
		this->boostModeUntil = data["boostModeUntil"].get<uint8_t>();
		this->heaterLimit = data["heaterLimit"].get<uint8_t>();
		this->heaterCycles = data["heaterCycles"].get<uint8_t>();
		this->relayGuard = data["relayGuard"].get<uint8_t>();
		this->savePIDSettings();
	}
	else if (command == "GetTempSettings")
	{
		// Convert sensors to json
		json jSensors = json::array({});

		for (auto const &[key, val] : this->sensors)
		{
			json jSensor = val->to_json();
			jSensors.push_back(jSensor);
		}

		resultData = jSensors;
	}
	else if (command == "SaveTempSettings")
	{
		this->saveTempSensorSettings(data);
	}
	else if (command == "DetectTempSensors")
	{
		this->detectOnewireTemperatureSensors();
	}
	else if (command == "GetHeaterSettings")
	{
		// Convert heaters to json
		json jHeaters = json::array({});

		for (auto const &heater : this->heaters)
		{
			json jHeater = heater->to_json();
			jHeaters.push_back(jHeater);
		}

		resultData = jHeaters;
	}
	else if (command == "SaveHeaterSettings")
	{
		if (this->controlRun)
		{
			message = "You cannot save heater settings while running!";
			success = false;
		}
		else
		{
			this->saveHeaterSettings(data);
		}
	}
	else if (command == "GetWifiSettings")
	{
		// get data from wifi-connect
		if (this->GetWifiSettingsJson)
		{
			resultData = this->GetWifiSettingsJson();
		}
	}
	else if (command == "SaveWifiSettings")
	{
		// save via wifi-connect
		if (this->SaveWifiSettingsJson)
		{
			this->SaveWifiSettingsJson(data);
		}
		message = "Please restart device for changes to have effect!";
	}
	else if (command == "ScanWifi")
	{
		// scans for networks
		if (this->ScanWifiJson)
		{
			resultData = this->ScanWifiJson();
		}
	}
	else if (command == "GetSystemSettings")
	{
		resultData = {
			{"onewirePin", this->oneWire_PIN},
			{"stirPin", this->stir_PIN},
			{"buzzerPin", this->buzzer_PIN},
			{"buzzerTime", this->buzzerTime},
			{"invertOutputs", this->invertOutputs},
			{"mqttUri", this->mqttUri},
			{"temperatureScale", this->temperatureScale},
			{"speaker1Pin", this->speaker1_PIN},
			{"speaker2Pin", this->speaker2_PIN},
		};
	}
	else if (command == "SaveSystemSettings")
	{
		this->saveSystemSettingsJson(data);
		message = "Please restart device for changes to have effect!";
	}
	else if (command == "Reboot")
	{
		xTaskCreate(&this->reboot, "reboot_task", 1024, this, 5, NULL);
	}
	else if (command == "FactoryReset")
	{
		this->settingsManager->FactoryReset();
		message = "Device will restart shortly, reconnect to factory wifi settings to continue!";
		xTaskCreate(&this->reboot, "reboot_task", 1024, this, 5, NULL);
	}
	else if (command == "BootIntoRecovery")
	{
		message = this->bootIntoRecovery();

		if (message.find("Error") != std::string::npos)
		{
			success = false;
		}
		else
		{
			xTaskCreate(&this->reboot, "reboot_task", 1024, this, 5, NULL);
		}
	}

	json jResultPayload;
	jResultPayload["data"] = resultData;
	jResultPayload["success"] = success;

	if (message != "")
	{
		jResultPayload["message"] = message;
	}

	string resultPayload = jResultPayload.dump();

	return resultPayload;
}

httpd_handle_t BrewEngine::startWebserver(void)
{

	httpd_uri_t indexUri;
	indexUri.uri = "/";
	indexUri.method = HTTP_GET;
	indexUri.handler = this->indexGetHandler;

	httpd_uri_t logoUri;
	logoUri.uri = "/logo.svg";
	logoUri.method = HTTP_GET;
	logoUri.handler = this->logoGetHandler;

	httpd_uri_t manifestUri;
	manifestUri.uri = "/manifest.json";
	manifestUri.method = HTTP_GET;
	manifestUri.handler = this->manifestGetHandler;

	httpd_uri_t postUri;
	postUri.uri = "/api";
	postUri.method = HTTP_POST;
	postUri.handler = this->apiPostHandler;

	httpd_uri_t optionsUri;
	optionsUri.uri = "/api";
	optionsUri.method = HTTP_OPTIONS;
	optionsUri.handler = this->apiOptionsHandler;

	httpd_uri_t otherUri;
	otherUri.uri = "/*";
	otherUri.method = HTTP_GET;
	otherUri.handler = this->otherGetHandler;

	httpd_handle_t server = NULL;
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	// whiout this the esp crashed whitout a proper warning
	config.stack_size = 20480;
	config.uri_match_fn = httpd_uri_match_wildcard;

	// Start the httpd server
	ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
	if (httpd_start(&server, &config) == ESP_OK)
	{
		// Set URI handlers
		httpd_register_uri_handler(server, &indexUri);
		httpd_register_uri_handler(server, &logoUri);
		httpd_register_uri_handler(server, &manifestUri);
		httpd_register_uri_handler(server, &otherUri);
		httpd_register_uri_handler(server, &postUri);
		httpd_register_uri_handler(server, &optionsUri);
		return server;
	}

	ESP_LOGI(TAG, "Error starting server!");
	return NULL;
}

void BrewEngine::logRemote(const string &message)
{
	if (this->mqttEnabled)
	{
		string iso_datetime = this->to_iso_8601(std::chrono::system_clock::now());
		json jPayload;
		jPayload["time"] = iso_datetime;
		jPayload["level"] = "Debug";
		jPayload["message"] = message;
		string payload = jPayload.dump();

		esp_mqtt_client_publish(this->mqttClient, this->mqttTopicLog.c_str(), payload.c_str(), 0, 1, 1);
	}
}

void BrewEngine::stopWebserver(httpd_handle_t server)
{
	// Stop the httpd server
	httpd_stop(server);
}

esp_err_t BrewEngine::indexGetHandler(httpd_req_t *req)
{
	// ESP_LOGI(TAG, "index_get_handler");
	extern const unsigned char index_html_start[] asm("_binary_index_html_gz_start");
	extern const unsigned char index_html_end[] asm("_binary_index_html_gz_end");
	const size_t index_html_size = (index_html_end - index_html_start);
	httpd_resp_set_type(req, "text/html");
	httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
	httpd_resp_send(req, (const char *)index_html_start, index_html_size);

	return ESP_OK;
}

esp_err_t BrewEngine::logoGetHandler(httpd_req_t *req)
{
	extern const unsigned char logo_svg_file_start[] asm("_binary_logo_svg_gz_start");
	extern const unsigned char logo_svg_file_end[] asm("_binary_logo_svg_gz_end");
	const size_t logo_svg_file_size = (logo_svg_file_end - logo_svg_file_start);
	httpd_resp_set_type(req, "image/svg+xml");
	httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
	httpd_resp_send(req, (const char *)logo_svg_file_start, logo_svg_file_size);

	return ESP_OK;
}

esp_err_t BrewEngine::manifestGetHandler(httpd_req_t *req)
{
	extern const unsigned char manifest_json_file_start[] asm("_binary_manifest_json_start");
	extern const unsigned char manifest_json_file_end[] asm("_binary_manifest_json_end");
	const size_t manifest_json_file_size = (manifest_json_file_end - manifest_json_file_start);
	httpd_resp_set_type(req, "application/json");
	httpd_resp_send(req, (const char *)manifest_json_file_start, manifest_json_file_size);

	return ESP_OK;
}

esp_err_t BrewEngine::otherGetHandler(httpd_req_t *req)
{
	httpd_resp_set_status(req, "307 Temporary Redirect");
	httpd_resp_set_hdr(req, "Location", "/");
	httpd_resp_send(req, "<html><body>Wrong</body></html>", 0); // Response body can be empty

	return ESP_OK;
}

esp_err_t BrewEngine::apiPostHandler(httpd_req_t *req)
{
	string stringBuffer;
	char buf[256];
	uint32_t ret;
	uint32_t remaining = req->content_len;

	while (remaining > 0)
	{
		// Read the data
		int nBytes = (std::min<uint32_t>)(remaining, sizeof(buf));

		if ((ret = httpd_req_recv(req, buf, nBytes)) <= 0)
		{
			if (ret == HTTPD_SOCK_ERR_TIMEOUT)
			{
				// Timeout, just continue
				continue;
			}

			return ESP_FAIL;
		}

		size_t bytes_read = ret;

		remaining -= bytes_read;

		// append to buffer
		stringBuffer.append((char *)buf, bytes_read);
	}

	string commandResult = mainInstance->processCommand(stringBuffer);

	const char *returnBuf = commandResult.c_str();
	httpd_resp_set_type(req, "text/plain");
	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
	httpd_resp_sendstr(req, returnBuf);

	return ESP_OK;
}

// needed for cors
esp_err_t BrewEngine::apiOptionsHandler(httpd_req_t *req)
{
	string commandResult = "";
	const char *returnBuf = commandResult.c_str();

	httpd_resp_set_type(req, "text/plain");
	httpd_resp_set_hdr(req, "Access-Control-Max-Age", "1728000");
	httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, PATCH, OPTIONS");
	httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Authorization,Content-Type,Accept,Origin,User-Agent,DNT,Cache-Control,X-Mx-ReqToken,Keep-Alive,X-Requested-With,If-Modified-Since");
	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
	httpd_resp_set_status(req, "204");
	httpd_resp_sendstr(req, returnBuf);

	return ESP_OK;
}

string BrewEngine::to_iso_8601(std::chrono::time_point<std::chrono::system_clock> t)
{

	// convert to time_t which will represent the number of
	// seconds since the UNIX epoch, UTC 00:00:00 Thursday, 1st. January 1970
	auto epoch_seconds = std::chrono::system_clock::to_time_t(t);

	// Format this as date time to seconds resolution
	// e.g. 2016-08-30T08:18:51
	std::stringstream stream;
	stream << std::put_time(gmtime(&epoch_seconds), "%FT%T");

	// If we now convert back to a time_point we will get the time truncated
	// to whole seconds
	auto truncated = std::chrono::system_clock::from_time_t(epoch_seconds);

	// Now we subtract this seconds count from the original time to
	// get the number of extra microseconds..
	auto delta_us = std::chrono::duration_cast<std::chrono::microseconds>(t - truncated).count();

	// And append this to the output stream as fractional seconds
	// e.g. 2016-08-30T08:18:51.867479
	stream << "." << std::fixed << std::setw(6) << std::setfill('0') << delta_us;

	return stream.str();
}