<script lang="ts" setup>
import NotificationEditor from "@/components/NotificationEditor.vue";
import StepEditor from "@/components/StepEditor.vue";
import WebConn from "@/helpers/webConn";
import { IMashSchedule } from "@/interfaces/IMashSchedule";
import { IMashStep } from "@/interfaces/IMashStep";
import { INotification } from "@/interfaces/INotification";
import { useAppStore } from "@/store/app";
import { mdiDelete, mdiPencil } from "@mdi/js";
import { computed, inject, ref, watch, watchEffect } from "vue";
import { useI18n } from "vue-i18n";

const { t } = useI18n({ useScope: "global" });

const webConn = inject<WebConn>("webConn");

const alert = ref<string>("");
const alertType = ref<"error" | "success" | "warning" | "info">("info");

const appStore = useAppStore();

// copy schedules locally to avoid instant application until saved
const mashSchedules = ref<Array<IMashSchedule>>([...appStore.mashSchedules]);

const currentName = ref<string>("");
const currentBoil = ref<boolean>(false);
const selectedMashSchedule = ref<IMashSchedule | null>(null);

const importFileInput = ref<HTMLInputElement | null>(null);

const getData = async () => {
  await appStore.getMashSchedules();
  mashSchedules.value = [...appStore.mashSchedules];
};

// Steps
const tableStepsData = ref<Array<IMashStep>>([]);
watch(
  () => selectedMashSchedule.value,
  () => {
    if (selectedMashSchedule.value == null) {
      tableStepsData.value = [];
      return;
    }

    tableStepsData.value = [...selectedMashSchedule.value.steps];
  },
);

// Notifications
const tableNotificationsData = ref<Array<INotification>>([]);
watch(
  () => selectedMashSchedule.value,
  () => {
    if (selectedMashSchedule.value == null) {
      tableNotificationsData.value = [];
      return;
    }

    tableNotificationsData.value = [...selectedMashSchedule.value.notifications];
  },
);

// copy name and boil flag when schedule changes
watchEffect(() => {
  if (selectedMashSchedule.value != null) {
    currentName.value = selectedMashSchedule.value.name;
    currentBoil.value = selectedMashSchedule.value.boil;
  }
});

const saveSchedule = async () => {
  // atm schedules are in ram so we can't allow crazy amounts, in the future we will need some kinde of cloud storage
  if (
    selectedMashSchedule.value != null &&
    selectedMashSchedule.value.name !== currentName.value &&
    mashSchedules.value.length >= appStore.maxSchedules.valueOf()
  ) {
    alert.value = t("mashSchedules.max_schedules_reached").replace("{0}", appStore.maxSchedules.toString());
    alertType.value = "warning";
    return;
  }

  const newSchedule: IMashSchedule = {
    name: currentName.value,
    boil: currentBoil.value,
    temporary: false,
    steps: [...tableStepsData.value],
    notifications: [...tableNotificationsData.value],
  };

  const requestData = {
    command: "SaveMashSchedule",
    data: newSchedule,
  };

  const result = await webConn?.doPostRequest(requestData);

  if (result?.message != null) {
    alertType.value = "warning";
    alert.value = result.message;
  }

  // refresh list after save
  getData();
};

const deleteSchedule = async () => {
  if (selectedMashSchedule.value == null) {
    return;
  }

  const requestData = {
    command: "DeleteMashSchedule",
    data: {
      name: selectedMashSchedule.value.name,
    },
  };

  selectedMashSchedule.value = null;

  await webConn?.doPostRequest(requestData);

  getData();
};

// Export current selected schedule as JSON file
// Construct the schedule from the current edit fields, regardless of save state
const exportSchedule = () => {
  if (!currentName.value || currentName.value.trim() === "") {
    alert.value = t("mashSchedules.name_required");
    alertType.value = "warning";
    return;
  }

  const scheduleToExport: IMashSchedule = {
    name: currentName.value.trim(),
    boil: currentBoil.value,
    temporary: false,
    steps: [...tableStepsData.value],
    notifications: [...tableNotificationsData.value],
  };

  const dataStr = JSON.stringify(scheduleToExport, null, 2);
  const blob = new Blob([dataStr], { type: "application/json" });
  const url = URL.createObjectURL(blob);

  const a = document.createElement("a");
  a.href = url;
  a.download = `${scheduleToExport.name}.json`;
  a.click();
  URL.revokeObjectURL(url);
};

// Trigger file input to import a schedule
const importSchedule = () => {
  if (!importFileInput.value) return;
  importFileInput.value.value = ""; // reset to allow re-importing same file
  importFileInput.value.click();
};

// Handle file selection and import schedule JSON
const onFileSelected = (event: Event) => {
  const target = event.target as HTMLInputElement;
  if (!target.files || target.files.length === 0) return;

  const file = target.files[0];
  const reader = new FileReader();

  reader.onload = (e) => {
    try {
      const content = e.target?.result as string;
      const importedSchedule = JSON.parse(content) as IMashSchedule;

      if (!importedSchedule.name) {
        alert.value = t("mashSchedules.invalid_file");
        alertType.value = "error";
        return;
      }

      // Check for existing schedule with same name and replace it; else add new
      const existingIndex = mashSchedules.value.findIndex((s) => s.name === importedSchedule.name);
      if (existingIndex >= 0) {
        mashSchedules.value[existingIndex] = importedSchedule;
      } else {
        mashSchedules.value.push(importedSchedule);
      }

      alert.value = t("mashSchedules.import_success");
      alertType.value = "success";

      selectedMashSchedule.value = importedSchedule;
    } catch {
      alert.value = t("mashSchedules.invalid_file");
      alertType.value = "error";
    }
  };

  reader.readAsText(file);
};
</script>

<template>
  <v-container class="pa-6" fluid>
    <v-alert :type="alertType" v-if="alert" closable @click:close="alert = ''">{{ alert }}</v-alert>
    <v-form fast-fail @submit.prevent>
      <v-row>
        <v-col cols="12" md="3">
          <v-select
            :label="t('mashSchedules.schedule')"
            v-model="selectedMashSchedule"
            :items="mashSchedules"
            item-title="name"
            :filled="mashSchedules"
            clearable
            return-object
          />
        </v-col>
      </v-row>

	  <v-row>
		<v-col cols="12" md="3">
		  <v-text-field v-model="currentName" :label="t('mashSchedules.name')" />
		</v-col>
		<v-col cols="12" md="3">
		  <v-switch v-model="currentBoil" :label="t('mashSchedules.isBoil')" color="red" />
		</v-col>
		<v-col cols="12" md="3" class="d-flex align-center">
		  <v-btn color="success" class="mt-4 mr-2" @click="saveSchedule">{{ t("general.save") }}</v-btn>
		  <v-btn color="error" class="mt-4 mr-2" @click="deleteSchedule">{{ t("general.delete") }}</v-btn>
		  <v-btn color="primary" class="mt-4 mr-2" @click="exportSchedule">{{ t("mashSchedules.export") }}</v-btn>
		  <v-btn color="secondary" class="mt-4" @click="importSchedule">{{ t("mashSchedules.import") }}</v-btn>
		  <input type="file" ref="importFileInput" accept=".json" style="display: none" @change="onFileSelected" />
		</v-col>
	  </v-row>

      <v-row>
        <v-col cols="12">
          <StepEditor
            v-model="tableStepsData"
            :items-per-page="20"
            :allow-new="true"
            :label="t('mashSchedules.steps')"
          />
        </v-col>
      </v-row>

      <v-row>
        <v-col cols="12">
          <NotificationEditor
            v-model="tableNotificationsData"
            :items-per-page="20"
            :allow-new="true"
            :label="t('mashSchedules.notifications')"
          />
        </v-col>
      </v-row>
    </v-form>
  </v-container>
</template>
