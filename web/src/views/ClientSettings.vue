<script setup lang="ts">

import { IClientSettings } from "@/interfaces/IClientSettings";
import { ITitleValue } from "@/interfaces/ITitleValue";
import { useClientStore } from "@/store/client";
import { onBeforeUnmount, onMounted, ref } from "vue";
import { useI18n } from "vue-i18n";

const { t } = useI18n();

const clientStore = useClientStore();

const clientSettings = ref<IClientSettings>({ ...clientStore.clientSettings });
const testMessage = ref<string>("This is a test message.");
const alert = ref<string>("");
const alertType = ref<"error" | "success" | "warning" | "info">("info");

const voices = ref<ITitleValue[]>([]);

const getData = () => {
  const synth = window.speechSynthesis;

  const loadVoices = () => {
    voices.value = synth.getVoices()
      .sort((a, b) => a.name.localeCompare(b.name))
      .map((voice) => ({
        title: `${voice.name} (${voice.lang})`,
        value: voice.voiceURI,
      }));
    voices.value.push({ title: "None", value: null });
  };

  loadVoices();

  synth.onvoiceschanged = () => loadVoices();
};

onMounted(() => {
  getData();
});

const testVoice = async () => {
  if (!clientSettings.value.voiceUri) {
    alertType.value = "warning";
    alert.value = "No voice selected.";
    return;
  }

  const synth = window.speechSynthesis;
  const foundVoice = synth.getVoices().find(v => v.voiceURI === clientSettings.value.voiceUri);

  if (!foundVoice) {
    alertType.value = "error";
    alert.value = "Selected voice not found.";
    return;
  }

  const ssu = new SpeechSynthesisUtterance(testMessage.value);
  ssu.voice = foundVoice;
  ssu.rate = clientSettings.value.speechRate;
  ssu.volume = clientSettings.value.speechVolume;

  synth.cancel();
  synth.speak(ssu);

  alertType.value = "success";
  alert.value = "Started reading test message.";
};

const save = async () => {
  clientStore.clientSettings = { ...clientSettings.value };
  clientStore.saveLocalSettings();
  alertType.value = "success";
  alert.value = "Settings saved.";
};
</script>

<template>
  <v-container class="spacing-playground pa-6 mt-2" fluid>
    <v-alert :type="alertType" v-if="alert" closable @click:close="alert = ''">
      {{ alert }}
    </v-alert>

    <v-form fast-fail @submit.prevent>

      <v-row>
        <v-col cols="12" md="3">
          <v-switch
            v-model="clientSettings.beepEnabled"
            :label="t('clientsettings.beep')"
            color="green"
          />
        </v-col>
        <v-col cols="12" md="9">
          <v-slider
            v-model="clientSettings.beepVolume"
            :label="t('clientsettings.beep_volume')"
            step="0.05" thumb-label="always"
            max="1" min="0.1"
          />
        </v-col>
      </v-row>

      <v-row>
        <v-col cols="12" md="3">
          <v-switch
            v-model="clientSettings.speechEnabled"
            :label="t('clientsettings.speech')"
            color="green"
          />
        </v-col>
        <v-col cols="12" md="9">
          <v-slider
            v-model="clientSettings.speechVolume"
            :label="t('clientsettings.speech_volume')"
            step="0.05" thumb-label="always"
            max="1" min="0.1"
          />
        </v-col>
      </v-row>

      <v-row>
        <v-col cols="12" md="6">
          <v-select
            :label="t('clientsettings.voice')"
            v-model="clientSettings.voiceUri"
            :items="voices"
            filled
            :menu-props="{ maxHeight: '300' }"
          />
        </v-col>
      </v-row>

      <v-row>
        <v-col cols="12" md="6">
          <v-slider
            v-model="clientSettings.speechRate"
            :label="t('clientsettings.speech_rate')"
            step="0.05" thumb-label="always"
            max="2" min="0.1"
          />
        </v-col>
      </v-row>

      <v-row>
        <v-col cols="9" md="6">
          <v-text-field
            :label="t('clientsettings.test_message')"
            v-model="testMessage"
            clearable
          />
        </v-col>
        <v-col cols="3" md="2">
          <v-btn
            variant="outlined"
            color="success"
            class="mt-4 mr-2"
            @click="testVoice"
            :disabled="!clientSettings.speechEnabled"
          >
            {{ t('clientsettings.test') }}
          </v-btn>
        </v-col>
      </v-row>

      <v-row>
        <v-col cols="12" md="3">
          <v-switch
            color="grey"
            v-model="clientSettings.darkMode"
            :label="t('clientsettings.darkMode')"
          />
        </v-col>
      </v-row>

      <v-row>
        <v-col cols="12" md="3">
          <v-btn color="success" class="mt-4 mr-2" @click="save">
            {{ t('general.save') }}
          </v-btn>
        </v-col>
      </v-row>

    </v-form>
  </v-container>
</template>