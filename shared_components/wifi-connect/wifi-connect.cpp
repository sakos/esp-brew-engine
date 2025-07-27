/*
 * esp-brew-engine
 * Copyright (C) Dekien Jeroen 2024
 *
 */
#include "wifi-connect.h"

using namespace std;
using json = nlohmann::json;

static const char *TAG = "WiFiConnect";

WiFiConnect::WiFiConnect(SettingsManager *settingsManager)
{
    this->settingsManager = settingsManager;
}

/*void WiFiConnect::Connect()
{
    this->readSettings();

    if (this->enableAP)
    {
        ESP_LOGI(TAG, "Starting wifi Access Point");
        this->wifi_init_softap();
    }
    else
    {
        ESP_LOGI(TAG, "Starting wifi Station");
        this->wifi_init_sta();
    }
}*/

void WiFiConnect::Connect()
{
    this->readSettings();
	
	ESP_ERROR_CHECK(esp_event_loop_create_default());

    // If no STA config is present start in AP mode directly
    if (this->staSSID.empty()) {
        ESP_LOGW(TAG, "No STA SSID configured, starting Access Point as fallback");
        this->wifi_init_softap();
		this->enableAP = true;		// this flag is for config save backward compatibility only
        return;
    }

    // Try to establish STA client wifi connection
    ESP_LOGI(TAG, "Starting Wifi Station (client) mode");
    bool sta_connected = this->wifi_try_sta_connect();

    if (sta_connected) {
        ESP_LOGI(TAG, "STA connect succeeded, operating in client mode.");
        // Successful, we are in STA mode
		this->enableAP = false;		// this flag is for config save backward compatibility only
        return;
    } else {
        ESP_LOGW(TAG, "STA connect failed, starting Access Point as fallback.");
        // Connection failed fallback to AP mode
        this->wifi_init_softap();
		this->enableAP = true;		// this flag is for config save backward compatibility only
        return;
    }
}


void WiFiConnect::readSettings()
{
    ESP_LOGI(TAG, "Reading Wifi Settings");

    // the logic here is that settings from nvs get preference, but if they don't exist settings from menuconfig are used and also saved to nvs
    // First read the version flag; default 1 (legacy) if not present
    int8_t version = this->settingsManager->Read("wifi_conf_ver", (int8_t)1);

    if (version >= 2)
    {
        // New format reading
		ESP_LOGI(TAG, "Wifi config version:2 found");
        this->enableAP = this->settingsManager->Read("wifi_ap", true);
        this->apSSID = this->settingsManager->Read("wifi_ap_ssid", (string)CONFIG_WIFI_SSID);
        this->apPassword = this->settingsManager->Read("wifi_ap_pass", (string)CONFIG_WIFI_PASS);
        this->apMaxPower = this->settingsManager->Read("wifi_max_power", (int8_t)CONFIG_ESP_PHY_MAX_WIFI_TX_POWER);
        this->staSSID = this->settingsManager->Read("wifi_sta_ssid", (string)"");
        this->staPassword = this->settingsManager->Read("wifi_sta_pass", (string)"");
		ESP_LOGI(TAG, "Read AP SSID: '%s', length: %d", this->apSSID.c_str(), this->apSSID.length());
		ESP_LOGI(TAG, "Read AP password: '%s', length: %d", this->apPassword.c_str(), this->apPassword.length());
		ESP_LOGI(TAG, "Read STA SSID: '%s', length: %d", this->staSSID.c_str(), this->staSSID.length());
		ESP_LOGI(TAG, "Read STA password: '%s', length: %d", this->staPassword.c_str(), this->staPassword.length());
    }
    else
    {
        // Legacy reading
		ESP_LOGI(TAG, "Wifi legacy config version found");
        this->ssid = this->settingsManager->Read("wifi_ssid", (string)"");
        this->password = this->settingsManager->Read("wifi_password", (string)"");
        this->maxWifiPower = this->settingsManager->Read("wifi_max_power", (int8_t)CONFIG_ESP_PHY_MAX_WIFI_TX_POWER);
        this->enableAP = this->settingsManager->Read("wifi_ap", false);

        // Map legacy to current fields for internal use or convert JSON on the fly if needed
        if (this->enableAP)
        {
            this->apSSID = this->ssid;
            this->apPassword = this->password;
            this->staSSID = "";
            this->staPassword = "";
        }
        else
        {
            this->staSSID = this->ssid;
            this->staPassword = this->password;
            this->apSSID = "";
            this->apPassword = "";
        }
		ESP_LOGI(TAG, "Mapped AP SSID: '%s', length: %d", this->apSSID.c_str(), this->apSSID.length());
		ESP_LOGI(TAG, "Mapped AP password: '%s', length: %d", this->apPassword.c_str(), this->apPassword.length());
		ESP_LOGI(TAG, "Mapped STA SSID: '%s', length: %d", this->staSSID.c_str(), this->staSSID.length());
		ESP_LOGI(TAG, "Mapped STA password: '%s', length: %d", this->staPassword.c_str(), this->staPassword.length());

		// Secure that fallback AP config is present in any case.
		if (this->apSSID.empty()) 
		{
			ESP_LOGI(TAG, "No fallback AP SSID was found, set default SSID and password");
			this->apSSID = CONFIG_WIFI_SSID;
		    this->apPassword = CONFIG_WIFI_PASS;
		}
    }
    bool configUseWifiAP = false;
// is there a cleaner way to do this?, config to bool doesn't seem to work properly
#if defined(CONFIG_WIFI_AP)
    configUseWifiAP = true;
#endif

    this->enableAP = this->settingsManager->Read("wifi_ap", (bool)configUseWifiAP);

    ESP_LOGI(TAG, "Reading Wifi Settings Done");
}

void WiFiConnect::saveSettings()
{
    ESP_LOGI(TAG, "Saving Wifi Settings");

    // Save new style keys (AP+STA both)
    this->settingsManager->Write("wifi_conf_ver", (int8_t)2);
	
    this->settingsManager->Write("wifi_ap_ssid", this->apSSID);
    this->settingsManager->Write("wifi_ap_pass", this->apPassword);
    this->settingsManager->Write("wifi_max_power", this->apMaxPower);

    this->settingsManager->Write("wifi_sta_ssid", this->staSSID);
    this->settingsManager->Write("wifi_sta_pass", this->staPassword);

    this->settingsManager->Write("wifi_ap", this->enableAP);
    this->settingsManager->Write("Hostname", this->Hostname);

	ESP_LOGI(TAG, "Saved AP SSID: '%s', length: %d", this->apSSID.c_str(), this->apSSID.length());
	ESP_LOGI(TAG, "Saved AP password: '%s', length: %d", this->apPassword.c_str(), this->apPassword.length());
	ESP_LOGI(TAG, "Saved STA SSID: '%s', length: %d", this->staSSID.c_str(), this->staSSID.length());
	ESP_LOGI(TAG, "Saved STA password: '%s', length: %d", this->staPassword.c_str(), this->staPassword.length());

	
    // Backward compatibility: save legacy keys based on enableAP flag
    if (this->enableAP)
    {
        // If AP mode enabled, treat legacy ssid/password/maxPower as AP settings for compatibility
		ESP_LOGI(TAG, "Save legacy wifi settings as AP enabled");
        this->settingsManager->Write("wifi_ssid", this->apSSID);
        this->settingsManager->Write("wifi_password", this->apPassword);
        this->settingsManager->Write("wifi_max_power", this->apMaxPower);
    }
    else
    {
        // If AP mode disabled, legacy keys represent STA settings
 		ESP_LOGI(TAG, "Save legacy wifi settings as AP disabled");
        this->settingsManager->Write("wifi_ssid", this->staSSID);
        this->settingsManager->Write("wifi_password", this->staPassword);
        this->settingsManager->Write("wifi_max_power", this->apMaxPower);
    }

    ESP_LOGI(TAG, "Saving Wifi Settings Done");
}

void WiFiConnect::gotIP(string Ip)
{
    ESP_LOGI(TAG, "Got IP:%s", Ip.c_str());
    if (this->GotIpCallback)
    {
        this->GotIpCallback(Ip);
    }
    this->Ip = Ip;

    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
}

void WiFiConnect::printTime()
{
    char strftime_buf[64];

    // // Set timezone to Eastern Standard Time and print local time
    // setenv("BR", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    // tzset();
    // localtime_r(&now, &timeinfo);

    time_t now = time(0);
    tm *localtm = localtime(&now);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", localtm);
    ESP_LOGI(TAG, "The current date/time is: %s", strftime_buf);
}

void WiFiConnect::obtainTime()
{
    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, this->ntpServer.c_str());
#ifdef CONFIG_SNTP_TIME_SYNC_METHOD_SMOOTH
    sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
#endif
    esp_sntp_init();

    // wait for time to be set
    time_t now = 0;
    struct tm timeinfo;
    int retry = 0;
    const int retry_count = 20;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count)
    {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
    time(&now);
    localtime_r(&now, &timeinfo);
}

// must be static limitation of esp-idf or freertos
void WiFiConnect::wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{

    // class instanse is paased as arg, due to static limitation
    WiFiConnect *instance = (WiFiConnect *)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        ESP_LOGI(TAG, "Start Connect - ssid:%s password:%s ", instance->ssid.c_str(), instance->password.c_str());

        esp_wifi_connect();
    }
    // doesn't seem to work yet atm, ipv6 seems imature in esp-idf
    // else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED)
    // {
    //     ESP_LOGI(TAG, "Create Ip6 Linklocal");
    //     // esp_netif_create_ip6_linklocal(instance->sta_netif);

    //     // esp_ip6_addr_t addr;
    // }
    //   else if (event_base == IP_EVENT && event_id == IP_EVENT_GOT_IP6)
    // {
    //     ESP_LOGI(TAG, "Got Ip6");
    // }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGI(TAG, "Disconnected");
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        esp_wifi_connect();
        xEventGroupClearBits(instance->s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

        string ipAddr;
        ipAddr = inet_ntoa(event->ip_info.ip);
        instance->gotIP(ipAddr);

        if (instance->setTime)
        {
            instance->obtainTime();
            instance->printTime();
        }
    }
}


bool WiFiConnect::wifi_try_sta_connect()
{
    this->s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    //ESP_ERROR_CHECK(esp_event_loop_create_default());
    this->sta_netif = esp_netif_create_default_wifi_sta();

    // set Hostname
    ESP_ERROR_CHECK(esp_netif_set_hostname(this->sta_netif, this->Hostname.c_str()));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_wifi_set_max_tx_power(this->maxWifiPower);

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &this->wifi_event_handler, this, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &this->wifi_event_handler, this, &instance_got_ip));

    wifi_config_t wifi_config{};
    strncpy((char *)wifi_config.sta.ssid, this->staSSID.c_str(), sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, this->staPassword.c_str(), sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_try_sta_connect: waiting max 10s for connection...");

    // Wait max 10 seconds for wifi connect.(event_handler will set the bits during this period)
    EventBits_t bits = xEventGroupWaitBits(this->s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(10000));

    // Unregister event handlers, clear event group
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    vEventGroupDelete(this->s_wifi_event_group);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Successfully connected to STA (client) network");
        return true;
    } else {
        ESP_LOGW(TAG, "STA connect failed or timeout.");
        ESP_ERROR_CHECK(esp_wifi_stop());
        // Useful to call wifi deinit to make wifi clean before starting fallback AP 
        esp_wifi_deinit();
        return false;
    }
}

void WiFiConnect::wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    //ESP_ERROR_CHECK(esp_event_loop_create_default());

    this->sta_netif = esp_netif_create_default_wifi_ap();

    // set the Hostname
    ESP_ERROR_CHECK(esp_netif_set_hostname(this->sta_netif, this->Hostname.c_str()));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_wifi_set_max_tx_power(this->apMaxPower);

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &this->wifi_event_handler, this, NULL));

    wifi_config_t wifi_config = {};
    
	strncpy((char *)wifi_config.ap.ssid, this->apSSID.c_str(), sizeof(wifi_config.ap.ssid));
    strncpy((char *)wifi_config.ap.password, this->apPassword.c_str(), sizeof(wifi_config.ap.password));
    wifi_config.ap.channel = this->apChannel;
    wifi_config.ap.max_connection = 10;
    // wifi_config.ap.pmf_cfg.required = true;
    if (this->apPassword.length() >= 8) {
        wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

     // future dns option inspired by?
    // https://github.com/craftmetrics/esp32-dns-server/blob/master/dns_server.c

    // other option is captive portal url via dhcp option
    // or wait unitl esp-idf implements proper dns

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP)); // Ap mode no scan is supported anymore
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wifi Access Point finished. ssid:%s password:%s channel:%d", this->ssid.c_str(), this->password.c_str(), this->apChannel);
}

json WiFiConnect::Scan()
{
    uint16_t number = CONFIG_WIFI_PROV_SCAN_MAX_ENTRIES;
    wifi_ap_record_t ap_info[CONFIG_WIFI_PROV_SCAN_MAX_ENTRIES];
    memset(ap_info, 0, sizeof(ap_info));
    uint16_t ap_count = 0;

	
    wifi_scan_config_t scanConfig = {};
    // run an active scan, a passive scan doesn't find all networks
    scanConfig.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    //scanConfig.show_hidden = true;
	scanConfig.show_hidden = false;
	
	wifi_mode_t mode;
	esp_wifi_get_mode(&mode);
	ESP_LOGI(TAG, "Current WiFi mode before scan: %d", mode);

    //esp_wifi_scan_start(&scanConfig, true);
	esp_err_t err = esp_wifi_scan_start(&scanConfig, true);
	ESP_LOGI(TAG, "esp_wifi_scan_start returned with error %d", err);
   
	ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, ap_info));

    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    ESP_LOGI(TAG, "Total APs scanned = %u", ap_count);

    json jNetworks = json::array({});

    for (int idx = 0; (idx < number) && (idx < ap_count); ++idx)
    {
        //string ssid = (char *)ap_info[idx].ssid;
		std::string ssid = (char*)ap_info[idx].ssid;

        string authMode;

        switch (ap_info[idx].authmode)
        {
        case WIFI_AUTH_OPEN:
            authMode = "Open";
            break;
        case WIFI_AUTH_WPA_PSK:
            authMode = "WPA";
            break;
        case WIFI_AUTH_WPA2_PSK:
            authMode = "WPA2";
            break;
        case WIFI_AUTH_WPA_WPA2_PSK:
            authMode = "WPA/WPA2";
            break;
        case WIFI_AUTH_WPA3_PSK:
            authMode = "WPA3";
            break;
        case WIFI_AUTH_WPA2_WPA3_PSK:
            authMode = "WPA2/WPA3";
            break;
        default:
            authMode = "Unsupported";
            break;
        }

        ESP_LOGI(TAG, "SSID: %s, RSSI: %d, Channel:%d, AuthMode:%s", ssid.c_str(), ap_info[idx].rssi, ap_info[idx].primary, authMode.c_str());

        json jNetwork;
        jNetwork["ssid"] = ssid;
        jNetwork["rssi"] = ap_info[idx].rssi;
        jNetwork["channel"] = ap_info[idx].primary;
        jNetwork["authMode"] = authMode;
        jNetworks.push_back(jNetwork);
    }

    return jNetworks;
}

json WiFiConnect::GetSettingsJson()
{
    json jWifiSettings;

    // Legacy fields
    jWifiSettings["ssid"] = this->ssid;
    jWifiSettings["password"] = this->password;
    jWifiSettings["enableAP"] = this->enableAP;
    jWifiSettings["maxPower"] = this->maxWifiPower;

    // New fields
    jWifiSettings["apSSID"] = this->apSSID;
    jWifiSettings["apPassword"] = this->apPassword;
    jWifiSettings["apMaxPower"] = this->apMaxPower;
    jWifiSettings["staSSID"] = this->staSSID;
    jWifiSettings["staPassword"] = this->staPassword;

    // Only add configVersion if it is defined / valid
    if (this->configVersion > 0)  
    {
        jWifiSettings["configVersion"] = this->configVersion;
    }

    return jWifiSettings;
}

void WiFiConnect::SaveSettingsJson(json config)
{
    // Check and assign AP SSID if present and is string
    if (config.contains("apSSID") && config["apSSID"].is_string())
    {
        this->apSSID = config["apSSID"];
    }

    // Check and assign AP Password
    if (config.contains("apPassword") && config["apPassword"].is_string())
    {
        this->apPassword = config["apPassword"];
    }

    // Check and assign AP Max Power (number expected)
    if (config.contains("apMaxPower") && config["apMaxPower"].is_number())
    {
        this->apMaxPower = static_cast<int8_t>(config["apMaxPower"]);
    }

    // Check and assign STA SSID
    if (config.contains("staSSID") && config["staSSID"].is_string())
    {
        this->staSSID = config["staSSID"];
    }

    // Check and assign STA Password
    if (config.contains("staPassword") && config["staPassword"].is_string())
    {
        this->staPassword = config["staPassword"];
    }

    // Check and assign enableAP flag (boolean expected)
    if (config.contains("enableAP") && config["enableAP"].is_boolean())
    {
        this->enableAP = config["enableAP"];
    }

    // Check and assign legacy single SSID (string)
    if (config.contains("ssid") && config["ssid"].is_string())
    {
        this->ssid = config["ssid"];
    }

    // Check and assign legacy password (string)
    if (config.contains("password") && config["password"].is_string())
    {
        this->password = config["password"];
    }

    // Check and assign legacy maxPower (number)
    if (config.contains("maxPower") && config["maxPower"].is_number())
    {
        this->maxWifiPower = static_cast<int8_t>(config["maxPower"]);
    }

    // Optionally assign configVersion if present
    if (config.contains("configVersion") && config["configVersion"].is_number())
    {
        this->configVersion = static_cast<int>(config["configVersion"]);
    }

    // No need to map legacy fields to new ones here,
    // since WiFiConnect::saveSettings() did it
    
    // Persist to storage
    this->saveSettings();
}
