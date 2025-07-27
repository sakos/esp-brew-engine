<script lang="ts" setup>
import WebConn from "@/helpers/webConn";
import { IWifiSettings } from "@/interfaces/IWifiSettings";
import { mdiEye, mdiEyeOutline } from "@mdi/js";
import { inject, onMounted, ref } from "vue";
import { useI18n } from "vue-i18n";
const { t } = useI18n({ useScope: "global" });

const webConn = inject<WebConn>("webConn");

const wifiSettings = ref<IWifiSettings>({
  enableAP: true,
  apSSID: "",
  apPassword: "",
  apMaxPower: 20,
  staSSID: "",
  staPassword: "",
});

const alert = ref<string>("");
const alertType = ref<"error" | "success" | "warning" | "info">("info");

const hidePwd = ref<boolean>(true); // Controls password visibility toggle

// Retrieve current wifi settings from backend on mount
const getData = async () => {
  try {
    const requestData = {
      command: "GetWifiSettings",
      data: null,
    };
    const apiResult = await webConn?.doPostRequest(requestData);

    if (apiResult === undefined || apiResult.success === false) {
      alert.value = t("wifiSettings.msg_fetch_failed") || "Failed to load WiFi settings";
      alertType.value = "error";
      return;
    }
    // Update local state with retrieved settings
    wifiSettings.value = apiResult.data;
  } catch (e) {
    alert.value = String(e);
    alertType.value = "error";
  }
};

// Save wifi settings (both AP and STA) to backend
const save = async () => {
  alert.value = "";

  // Basic validation: require at least one SSID (AP or STA)
  if (
    (wifiSettings.value.enableAP && wifiSettings.value.apSSID.trim() === "") &&
    wifiSettings.value.staSSID.trim() === ""
  ) {
    alert.value = t("wifiSettings.msg_ssid_empty") || "SSID cannot be empty";
    alertType.value = "warning";
    return;
  }

  const requestData = {
    command: "SaveWifiSettings",
    data: wifiSettings.value,
  };

  try {
    const result = await webConn?.doPostRequest(requestData);
    if (result?.message) {
      alert.value = result.message;
      alertType.value = "warning";
    } else {
      alert.value = t("wifiSettings.msg_save_success") || "WiFi settings saved successfully";
      alertType.value = "success";
    }
  } catch (e) {
    alert.value = String(e);
    alertType.value = "error";
  }
};

onMounted(() => {
  getData();
});
</script>

<template>
  <v-container class="pa-6" fluid>
    <!-- Alert for status messages -->
    <v-alert :type="alertType" v-if="alert" dismissible @click:close="alert = ''">
      {{ alert }}
    </v-alert>

    <v-form @submit.prevent>
      <!-- Display Access Point Mode status (readonly) -->
	  <h3>{{ wifiSettings.enableAP ? t('wifiSettings.access_point_mode_enabled') : t('wifiSettings.client_mode_enabled') }}</h3>

      <!-- AP Settings title -->
	  <div class="text-subtitle-2 mt-4 mb-2">{{ $t('wifiSettings.ap_settings') }}</div>

      <!-- Access Point settings - always visible -->
      <v-row>
        <v-col cols="12" md="6">
          <v-text-field
            v-model="wifiSettings.apSSID"
            :label="t('wifiSettings.access_point_name')"
            maxlength="32"
            hint="The SSID broadcasted by the device"
            persistent-hint
          />
          <v-text-field
            v-model="wifiSettings.apPassword"
            :type="hidePwd ? 'password' : 'text'"
            :label="t('wifiSettings.password')"
            :append-icon="hidePwd ? mdiEye : mdiEyeOutline"
            @click:append="() => (hidePwd = !hidePwd)"
            maxlength="64"
            hint="Password for AP (optional)"
            persistent-hint
          />
          <v-slider
            class="mt-4"
			v-model="wifiSettings.apMaxPower"
            :label="t('wifiSettings.max_wifi_power')"
            step="1"
            thumb-label="always"
            max="20"
            min="10"
          />
        </v-col>
      </v-row>

      <!-- Client settings title -->
	  <div class="text-subtitle-2 mt-4 mb-2">{{ $t('wifiSettings.client_settings') }}</div>

      <!-- Station settings -->
      <v-row>
        <v-col cols="12" md="6">
          <v-text-field
            v-model="wifiSettings.staSSID"
            :label="t('wifiSettings.join_network')"
            maxlength="32"
            clearable
          />
          <v-text-field
            v-model="wifiSettings.staPassword"
            :type="hidePwd ? 'password' : 'text'"
            :label="t('wifiSettings.password')"
            :append-icon="hidePwd ? mdiEye : mdiEyeOutline"
            @click:append="() => (hidePwd = !hidePwd)"
            maxlength="64"
          />
        </v-col>
      </v-row>

      <!-- Save button -->
      <v-row>
        <v-col cols="12" md="3">
          <v-btn color="success" class="mt-4" @click="save">
            {{ t("general.save") }}
          </v-btn>
        </v-col>
      </v-row>
    </v-form>
  </v-container>
</template>
