<script lang="ts" setup>
import TemperatureScale from "@/enums/TemperatureScale";
import WebConn from "@/helpers/webConn";
import { IDataPacket } from "@/interfaces/IDataPacket";
import { IExecutionStep } from "@/interfaces/IExecutionStep";
import { IMashSchedule } from "@/interfaces/IMashSchedule";
import { ITempLog } from "@/interfaces/ITempLog";
import { ITempSensor } from "@/interfaces/ITempSensor";
import { useAppStore } from "@/store/app";
import { CategoryScale, Chart as ChartJS, Filler, Legend, LineElement, LinearScale, PointElement, TimeScale, Title, Tooltip } from "chart.js";
import "chartjs-adapter-dayjs-4";
import { INotification } from "@/interfaces/INotification";
import { useClientStore } from "@/store/client";
import annotationPlugin from "chartjs-plugin-annotation";
import debounce from "lodash.debounce";
import { computed, inject, onBeforeUnmount, onMounted, ref, watch } from "vue";
import { Line } from "vue-chartjs";
import { useI18n } from "vue-i18n";
import BoostStatus from "@/enums/BoostStatus";

const { t } = useI18n({ useScope: "global" });

const webConn = inject<WebConn>("webConn");

const appStore = useAppStore();
const clientStore = useClientStore();

const status = ref<string>();
const idleEntered = ref(false);
const stirStatus = ref<string>();
const temperature = ref<number>();
const outputPercent = ref<number>();
const targetTemperature = ref<number>();
const manualOverrideTemperature = ref<number | null>(null);
const manualOverrideOutput = ref<number | null>(null);
const inAdaptationTime = ref<boolean>(false);
const boostStatus = ref<BoostStatus>(BoostStatus.Off);
const powerUsage = ref<number>();
const currentStepName = ref<string>();
const outputSummary = ref<string>('');
const isEditingOutput = ref<boolean>(false);
const isClearing = ref<boolean>(false);
const localOutput = ref<number | null>(null);
const resetManualOutput = ref<boolean>();
const resetManualTemp = ref<boolean>();
const currentScheduleName = ref<string>();
const labelYPos = ref<number>();

const isEditingTime = ref<boolean>(false);
const localRemainingMinutes = ref<number | null>(null);
const timeValueOnFocus = ref<number | null>(null);

const isEditingTemp = ref<boolean>(false);
const isClearingTemp = ref<boolean>(false); 
const localTargetTemp = ref<number | null>(null);
const tempValueOnFocus = ref<number | null>(null);
const backendStepTargetTemp = ref<number>(0); // Stores the step endpoint from ESP

let audio: HTMLAudioElement | null = null;

const intervalId = ref<any>();

const notificationDialog = ref<boolean>(false);
const notificationDialogTitle = ref<string>("");
const notificationDialogText = ref<string>("");

const notificationTimeouts = ref<Array<number>>([]);
const notificationsShown = ref<Array<number>>([]);

const lastCheckedAt = ref<number>(0); // Unix timestamp in milisec, 0 at beginning
const MAX_CHECK_INTERVAL = 1 * 60 * 1000; // 1 minute


const chartInitDone = ref(false);

const lastGoodDataDate = ref<number | null>(null);
const lastRunningVersion = ref<number>(0);

const rawData = ref<Array<IDataPacket>>([]);

const tempSensors = ref<Array<ITempSensor>>([]);
const executionSteps = ref<Array<IExecutionStep>>([]);
const notifications = ref<Array<INotification>>([]);

const selectedMashSchedule = ref<IMashSchedule | null>(null);

const currentTemps = ref<Array<ITempLog>>([]);

const startDateTime = ref<number>();
const speechVoice = ref<SpeechSynthesisVoice | null>(null);

const stirInterval = ref<Array<number>>([0, 3]);
const stirMax = ref<number>(6);

const focussedField = ref<string>("");

const dynamicColor = () => {
  const r = Math.floor(Math.random() * 255);
  const g = Math.floor(Math.random() * 255);
  const b = Math.floor(Math.random() * 255);
  return `rgb(${r},${g},${b})`;
};

const beep = async () => {
  // create web audio api context
  const audioCtx = new AudioContext();

  // create Oscillator node
  const oscillator = audioCtx.createOscillator();

  const gainNode = audioCtx.createGain();
  gainNode.gain.value = clientStore.clientSettings.beepVolume; // volume
  gainNode.connect(audioCtx.destination);

  oscillator.type = "square";
  oscillator.frequency.setValueAtTime(2000, audioCtx.currentTime); // value in hertz

  oscillator.connect(gainNode);

  oscillator.start(audioCtx.currentTime);
  oscillator.stop(audioCtx.currentTime + 0.1); // 100ms beep
};

const speakMessage = async (message: string) => {
  if (clientStore.clientSettings.voiceUri == null) {
    return;
  }

  const synth = window.speechSynthesis;
  // the first time we get the voice and store it in a ref
  if (speechVoice.value == null) {
    const foundVoice = synth.getVoices().find((v) => v.voiceURI === clientStore.clientSettings.voiceUri);
    if (foundVoice !== undefined) {
      speechVoice.value = foundVoice;
    }
  }

  // unable to get a valid voice
  if (speechVoice.value == null) {
    return;
  }

  const ssu = new SpeechSynthesisUtterance(message);
  ssu.voice = speechVoice.value;
  ssu.pitch = 1;
  ssu.rate = clientStore.clientSettings.speechRate;
  ssu.volume = clientStore.clientSettings.speechVolume;
  synth.speak(ssu);
};

const showNotificaton = async (notification: INotification, alert: boolean) => {

  notificationsShown.value.push(notification.timePoint);

  notificationDialogTitle.value = notification.name;
  notificationDialogText.value = notification.message.replaceAll("\n", "<br/>");
  notificationDialog.value = true;

  if (alert && clientStore.clientSettings.beepEnabled) {
    beep();
  }

  if (alert && clientStore.clientSettings.speechEnabled) {
    // when beep and speech add a small pauze
    if (clientStore.clientSettings.beepEnabled) {
      setTimeout(() => {
        speakMessage(notification.message);
      }, 1500);
    } else {
      speakMessage(notification.message);
    }
  }
};

const chartAnnotations = computed(() => {
  // wait for chartjs init
  if (!chartInitDone.value) {
    return null;
  }

  let currentNotifications: Array<INotification> = [];

  // when we are running notifications that come from schedule api call
  if (executionSteps.value != null && executionSteps.value.length > 0) {
    currentNotifications = [...notifications.value];
  } else if (selectedMashSchedule.value !== null && selectedMashSchedule.value.steps !== null && startDateTime.value != null) {
    // when we have no steps and we are idle we can compute notifications from the schedule settings
    if (status.value === "Idle") {
      const scheduleNotifications = [...selectedMashSchedule.value.notifications];

      currentNotifications = scheduleNotifications.map((notification) => {
        let notificationTime = startDateTime.value!;
        notificationTime += notification.timeAbsolute * 60;

        const newNotification = { ...notification };
        newNotification.timePoint = notificationTime;
        return newNotification;
      });
    }
  }

  const annotationData: Array<any> = [];

  currentNotifications.forEach((notification) => {
    const notificationTime = notification.timePoint * 1000;
    const notificationPoint = {
      type: "line",
      xMin: notificationTime,
      xMax: notificationTime,
      yMax: labelYPos.value,     				// Position above 100 C not to overlap the temp line
      borderColor: "rgb(255, 99, 132)",
      borderWidth: 2,
      label: {
        content: notification.name,
        position: "end",
        display: true,
      },
      click(context: any, event: any) {
        showNotificaton(notification, false);
      },
    };
    annotationData.push(notificationPoint);
  });

  return annotationData;
});

const chartData = computed(() => {
  // wait for chartjs init
  if (!chartInitDone.value) {
    return null;
  }
  let scheduleData: Array<any> = [];

  if (executionSteps.value != null && executionSteps.value.length > 0) {
    scheduleData = executionSteps.value.map((step: any) => ({
      x: step.time * 1000,
      y: step.temperature,
    }));
  } else if (selectedMashSchedule.value !== null && selectedMashSchedule.value.steps !== null && startDateTime.value != null) {
    // when we have no steps and we are idle we can show the slected mash schedule
    // if the status is idle we can just project the selectes mash shedule
    if (status.value === "Idle") {
      // sort data on index
      const steps = [...selectedMashSchedule.value.steps];
      steps.sort((a, b) => a.index - b.index);

      let lastTimePoint = startDateTime.value;

      const startPoint = {
        x: lastTimePoint * 1000,
        y: temperature.value,
      };
      scheduleData.push(startPoint);

      steps.forEach((step) => {
        lastTimePoint += step.stepTime * 60;
        const startStepPoint = {
          x: lastTimePoint * 1000,
          y: step.temperature,
        };
        scheduleData.push(startStepPoint);

        lastTimePoint += step.time * 60;
        const endStepPoint = {
          x: lastTimePoint * 1000,
          y: step.temperature,
        };
        scheduleData.push(endStepPoint);
      });
    }
  }

  const realData = rawData.value.map((item) => ({
    x: item.time * 1000,
    y: item.temp,
  }));

  // also add the current value, out controller doesn't send identical temp point for performance reasons
  if (lastGoodDataDate.value != null && temperature.value != null) {
    realData.push({
      x: Date.now(),
      y: temperature.value,
    });
  }

  let datasets = [
    {
      label: `${t("control.avg")} ${appStore.tempUnit}`,
      backgroundColor: "rgba(255, 255, 255, 0.7)",
      borderColor: "rgba(255, 255, 255, 0.9)",
      lineTension: 0,
      fill: false,
      xAxisID: "xAxis",
      yAxisID: "yAxis",
      data: realData,
    },
    {
      label: `${t("control.target")} ${appStore.tempUnit}`,
      backgroundColor: "rgba(0, 158, 85, 0.3)",
      borderColor: "rgba(0, 158, 85, 0.9)",
      lineTension: 0,
      xAxisID: "xAxis",
      yAxisID: "yAxis",
      fill: true,
      data: scheduleData,
    },
  ];

  const extraDataSets = currentTemps.value.map((extraSet) => {
    const setData = extraSet.temps.map((temp) => ({
      x: temp.time * 1000,
      y: temp.temp,
    }));

    // Find the very last temperature value from this sensor's history
    let currentSensorValueText = "";
    if (extraSet.temps.length > 0) {
      // Get the last item in the temps array
      const lastTempRecord = extraSet.temps[extraSet.temps.length - 1];
      // Format to 1 decimal places to match the UI precision
      currentSensorValueText = ` (${lastTempRecord.temp.toFixed(1)}°)`;
    }

    let label = extraSet.sensor;
    let { color } = extraSet;
    const sensor = tempSensors.value.find((s) => s.id === extraSet.sensor);

    if (sensor !== undefined) {
      label = sensor.name;
      color = sensor.color;
    }

    // Append the current live temperature directly to the label text
    const dynamicLabel = `${label}${currentSensorValueText}`;

    const dataset = {
      label: dynamicLabel, // Updated with the live value
      backgroundColor: color,
      borderColor: color,
      lineWidth: 0.2,
      lineTension: 0,
      xAxisID: "xAxis",
      yAxisID: "yAxis",
      fill: false,
      pointRadius: 0,
      data: setData,
    };

    return dataset;
  });

  datasets = [...datasets, ...extraDataSets];

  return {
    labels: [],
    datasets,
  };
});

const clearAllNotificationTimeouts = () => {
  notificationTimeouts.value.forEach((timeOutId) => {
    window.clearTimeout(timeOutId);
  });

  notificationTimeouts.value = [];
};

const setNotifications = (newNotifications: Array<INotification>) => {
  clearAllNotificationTimeouts();

  const timeoutIds: Array<number> = [];

  // Notifications we have not show yet
  newNotifications
    .filter((n) => notificationsShown.value.includes(n.timePoint) === false)
    .forEach((notification) => {
      const timeTill = notification.timePoint * 1000 - Date.now();
      // We do want to loose notification due to overtime, but these are very short in the past! max 10 seconds 
	  // Overtime is gradual, we do not schedule notification popups during overtime. Instead an update will happen after overtime has ended
      if (timeTill > -10000) {
        const timeoutId = window.setTimeout(() => {
          showNotificaton(notification, true);
        }, timeTill);
        timeoutIds.push(timeoutId);
      }
    });


  notificationTimeouts.value = timeoutIds;

  notifications.value = newNotifications;
};

const getRunningSchedule = async () => {

  const requestData = {
    command: "GetRunningSchedule",
    data: null,
  };

  const apiResult = await webConn?.doPostRequest(requestData);

  if (apiResult === undefined || apiResult.success === false) {
    return;
  }

  executionSteps.value = apiResult.data.steps;
  setNotifications(apiResult.data.notifications as Array<INotification>);
 

  lastRunningVersion.value = apiResult.data.version;
};

const resetAll = () => {
  currentTemps.value = [];
  executionSteps.value = [];
  rawData.value = [];
  notificationsShown.value = [];
  setStartDateNow();
};


watch(status, (newStatus, oldStatus) => {
  if (newStatus === "Idle" && oldStatus !== "Idle") {
    idleEntered.value = true;  // Idle mode entered after schedule finished
  }
});

watch(selectedMashSchedule, (newVal, oldVal) => {
  currentTemps.value = [];		// delete temp history
  setStartDateNow();			// reset chart start time
  if (idleEntered.value) {
    // Has effect only once after schedule was finished
    resetAll();						// make chart update working again
    idleEntered.value = false;		// only once
  }
});

watch([outputPercent, manualOverrideOutput], () => {
  if (!isEditingOutput.value && !isClearing.value) {
    localOutput.value = manualOverrideOutput.value !== null ? manualOverrideOutput.value : (outputPercent.value ?? 0);
  }
}, { immediate: true });

// Step time control

const onTimeFocus = () => {
  isEditingTime.value = true;
  // Capture the initial minute value at the moment the field gains focus
  timeValueOnFocus.value = localRemainingMinutes.value;
};

const onTimeBlur = (event: any) => {
  isEditingTime.value = false;

  // If the user left the field entirely empty, abort and let the next getData loop restore it
  if (localRemainingMinutes.value === null || event.target.value === '') {
    return;
  }

  // If no change was made compared to the value when focused, do nothing
  if (localRemainingMinutes.value === timeValueOnFocus.value) {
    return;
  }

  // Value actually changed via step buttons or typing, transmit new target duration to ESP
  changeRemainingTime(localRemainingMinutes.value);
};

const onTimeEnter = (event: any) => {
  event.target.blur(); // Triggers the blur validation sequence above when Enter is pressed
};

const changeRemainingTime = async (minutesValue: number) => {
  // Convert minutes back to raw seconds for the C++ backend
  const targetSeconds = Math.max(0, minutesValue * 60);

  const requestData = {
    command: "SetRemainingTime",
    data: {
      remainingTime: targetSeconds,
    },
  };

  await webConn?.doPostRequest(requestData);
  
  // Wipe the client version registry to force an immediate schedule layout fetch
  lastRunningVersion.value = 0; 
};

// End of step time control

let controller: AbortController | null = null;

const getData = async () => {
  // If there is a pending request, abort it
  controller?.abort();
  controller = new AbortController();

  const requestData = {
    command: "Data",
    data: {
      lastDate: lastGoodDataDate.value,
    },
  };

  const apiResult = await webConn?.doPostRequest(requestData, { signal: controller.signal });

  if (apiResult === undefined || apiResult.success === false) {
    return;
  }

  status.value = apiResult.data.status;
  stirStatus.value = apiResult.data.stirStatus;
  temperature.value = apiResult.data.temp;
  outputPercent.value = apiResult.data.output;
  targetTemperature.value = apiResult.data.targetTemp;
  backendStepTargetTemp.value = apiResult.data.stepTargetTemp || apiResult.data.targetTemp; // Save the static endpoint
  lastGoodDataDate.value = apiResult.data.lastLogDateTime;
  inAdaptationTime.value = apiResult.data.inAdaptationTime;
  boostStatus.value = apiResult.data.boostStatus;
  powerUsage.value = apiResult.data.powerUsage;
  currentStepName.value = apiResult.data.currentStepName;
  outputSummary.value = apiResult.data.outputSummary;
  resetManualOutput.value = apiResult.data.resetManualOutput;
  resetManualTemp.value = apiResult.data.resetManualTemp;
  if (status.value !== 'Idle') {
    const scheduleName = apiResult.data.currentScheduleName;
    selectedMashSchedule.value = appStore.mashSchedules.find(ms => ms.name === scheduleName) || null;
  }

  // Keep manualOverrideOutput safe from delayed backend reset packets while editing
  if (resetManualOutput.value && !isEditingOutput.value) {
	manualOverrideOutput.value = null;
  }
  
  if (resetManualTemp.value && !isEditingTemp.value) {
	manualOverrideTemperature.value = null;
  }
  
  if (!isEditingTemp.value && !isClearingTemp.value) {
    localTargetTemp.value = targetTemperature.value !== undefined ? Math.round(targetTemperature.value * 10) / 10 : 0;
  }
  
  // Update local remaining time field when the user is not actively editing it
  const serverRemainingSeconds = apiResult.data.remainingTime || 0;
  if (!isEditingTime.value) {
    // Convert seconds to display minutes (rounded up to nearest whole minute)
    localRemainingMinutes.value = Math.max(0, Math.ceil(serverRemainingSeconds / 60));
  }  
  
  const serverRunningVersion = apiResult.data.runningVersion;
  const now = Date.now();
  const isTimeoutExpired = (now - lastCheckedAt.value) > MAX_CHECK_INTERVAL;


  if (status.value === "Running" && (lastRunningVersion.value !== serverRunningVersion || isTimeoutExpired)) {
    // the schedule has changed, we need to update
	// Periodic update as well to prevent delay in browser due to sleep.
    getRunningSchedule();
	lastCheckedAt.value = Date.now();
  }


  const tempData = [...rawData.value, ...apiResult.data.tempLog];

  // sort data, chartjs seems todo weird things otherwise
  tempData.sort((a, b) => a.time - b.time);

  rawData.value = tempData;

  // if there are more then 1 sensor we also get the raw data per sensor (whitout history)
  const timestampSeconds = Math.floor(Date.now() / 1000);

  if (apiResult.data.temps !== null) {
    apiResult.data.temps.forEach((te: any) => {
      // find record in templog and add
      const foundRecord = currentTemps.value.find((ct: any) => ct.sensor === te.sensor);
      if (foundRecord === undefined) {
        const newRecord: ITempLog = {
          sensor: te.sensor,
          color: dynamicColor(),
          temps: [
            {
              time: timestampSeconds,
              temp: te.temp,
            },
          ],
        };

        currentTemps.value.push(newRecord);
      } else {
        foundRecord.temps.push({
          time: timestampSeconds,
          temp: te.temp,
        });
      }
    });
  }

  // we only need to get the tempsensort once
  if (tempSensors.value == null || tempSensors.value.length === 0) {
    const requestData3 = {
      command: "GetTempSettings",
      data: null,
    };
    const apiResult3 = await webConn?.doPostRequest(requestData3);

    if (apiResult3 === undefined || apiResult3.success === false) {
      return;
    }

    tempSensors.value = apiResult3.data;
  }
};

// Temperature override handling
const onTempFocus = () => {
  isEditingTemp.value = true;
  
  // 2. REQUIREMENT: On focus, show the static step final target (or current manual selection)
  if (manualOverrideTemperature.value !== null) {
    localTargetTemp.value = manualOverrideTemperature.value;
  } else {
    localTargetTemp.value = backendStepTargetTemp.value;
  }
  tempValueOnFocus.value = localTargetTemp.value;
};

const onTempBlur = (event: any) => {
  // If the clear button (X) was pressed, bypass blur logic entirely to prevent re-submitting values
  if (isClearingTemp.value) {
    return;
  }

  isEditingTemp.value = false;

  // If the user manually backspaced/cleared the field text completely
  if (localTargetTemp.value === null || event.target.value === '') {
    clearTempOverride();
    return;
  }

  // If user clicked inside but made no modifications, do nothing
  if (localTargetTemp.value === tempValueOnFocus.value) {
    return;
  }

  // 3. REQUIREMENT: Value actually changed, transmit new manual target to ESP
  applyTempOverride(localTargetTemp.value);
};

const onTempEnter = (event: any) => {
  event.target.blur();
};

const applyTempOverride = async (value: number) => {
  manualOverrideTemperature.value = value;
  localTargetTemp.value = value;

  const requestData = {
    command: "SetOverrideTemp",
    data: {
      targetTemp: value,
    },
  };
  await webConn?.doPostRequest(requestData);
};

// 4. REQUIREMENT: Triggered via 'X' button. Instantly forces the default scheduled step target and exits edit mode
const clearTempOverride = async () => {
  isClearingTemp.value = true;
  isEditingTemp.value = false;

  // Clear frontend manual mode indicator
  manualOverrideTemperature.value = null;
  
  // Instantly populate field with the default scheduled step target (not the ramp targetTemp!)
  localTargetTemp.value = backendStepTargetTemp.value !== undefined ? Math.round(backendStepTargetTemp.value * 10) / 10 : 0;

  // FIXED: Explicitly force the HTML input element to lose focus (exit edit mode)
  if (document.activeElement instanceof HTMLElement) {
    document.activeElement.blur();
  }

  const requestData = {
    command: "SetOverrideTemp",
    data: {
      targetTemp: null, // Instructs C++ to return to schedule
    },
  };

  await webConn?.doPostRequest(requestData);

  // Briefly hold the lock to let Vuetify finish its async DOM cycles cleanly before releasing
  setTimeout(() => {
    isClearingTemp.value = false;
  }, 100);
};
// Temperature override handling end



// Dynamic label configuration
//const outputLabel = computed(() => {
//  return manualOverrideOutput.value !== null 
//    ? `${t('control.output')} - ${t('control.manual_override')}` 
//    : `${t('control.output')}`;
//});

const onOutputFocus = () => {
  isEditingOutput.value = true;
};

const onOutputBlur = (event: any) => {
  // If the clear button (X) was pressed, bypass this blur logic entirely
  if (isClearing.value) {
    return;
  }

  isEditingOutput.value = false;
  
  // If the user manually backspaced/cleared the field text
  if (localOutput.value === null || event.target.value === '') {
    clearOutputOverride();
    return;
  }

  // Determine the baseline value before this edit session
  const currentActiveValue = manualOverrideOutput.value !== null ? manualOverrideOutput.value : (outputPercent.value ?? 0);
  
  if (localOutput.value === currentActiveValue) {
    // If no real change was made, enforce proper visual cleanup if we were in auto
    if (manualOverrideOutput.value === null) {
      clearOutputOverride();
    }
  } else {
    // Value actually changed, transmit new override to ESP
    applyOutputOverride(localOutput.value);
  }
};

const onOutputEnter = (event: any) => {
  event.target.blur();
};

const applyOutputOverride = async (value: number) => {
  let cleanValue = Math.max(0, Math.min(100, value));
  manualOverrideOutput.value = cleanValue;
  localOutput.value = cleanValue;

  const requestData = {
    command: "SetOverrideOutput",
    data: {
      output: cleanValue,
    },
  };
  await webConn?.doPostRequest(requestData);
};

const clearOutputOverride = async () => {
  isClearing.value = true;
  isEditingOutput.value = false;
  
  // Instantly clear states and force reset to backend automatic value
  manualOverrideOutput.value = null;
  localOutput.value = outputPercent.value ?? 0;
  
  const requestData = {
    command: "SetOverrideOutput",
    data: {
      output: null,
    },
  };
  
  await webConn?.doPostRequest(requestData);
  
  // Wait for Vuetify's internal async operations to finish before releasing the lock
  setTimeout(() => {
    isClearing.value = false;
  }, 100);
};

const setStartDateNow = () => {
  const now = new Date();
  startDateTime.value = Math.floor(now.getTime() / 1000);
};


const start = async () => {
  const requestData = {
    command: "Start",
    data: {
      selectedMashSchedule: null as string | null,
    },
  };

  // reset all our data so we can start over
  resetAll();

  if (selectedMashSchedule.value != null) {
    requestData.data.selectedMashSchedule = selectedMashSchedule.value?.name;
  }

  await webConn?.doPostRequest(requestData);
  // todo capture error
  // reset out running version so we can definitly get the new schedule
  lastRunningVersion.value = 0;
};

const stop = async () => {
  const requestData = {
    command: "Stop",
    data: null,
  };

  clearAllNotificationTimeouts();

  webConn?.doPostRequest(requestData);
  // todo capture error
};

const startStir = async () => {
  const requestData = {
    command: "StartStir",
    data: {
      max: stirMax.value,
      intervalStart: stirInterval.value[0],
      intervalStop: stirInterval.value[1],
    },
  };

  await webConn?.doPostRequest(requestData);
  // todo capture error
};

const stopStir = async () => {
  const requestData = {
    command: "StopStir",
    data: null,
  };

  webConn?.doPostRequest(requestData);
  // todo capture error
};


const initChart = () => {
  ChartJS.register(Title, Tooltip, Legend, PointElement, LineElement, TimeScale, LinearScale, CategoryScale, Filler, annotationPlugin);
  chartInitDone.value = true;
};

const chartOptions = computed<any>(() => {
  let suggestedMin = 60;
  let suggestedMax = 105;
  // ajust min max when farenheit
  if (appStore.temperatureScale === TemperatureScale.Fahrenheit) {
    suggestedMin = 140;
    suggestedMax = 220;
  }

  const options = {
    responsive: true,
    maintainAspectRatio: false,
    animation: false, // Disable all animations, does weird things when adding data
    scales: {
      xAxis: {
        title: {
          display: true,
          text: t("control.time"),
          color: "#ffffff",
        },
        type: "time",
        time: {
          unit: "minute",
          displayFormats: {
            millisecond: "HH:mm",
            second: "HH:mm",
            minute: "HH:mm",
            hour: "HH:mm",
            day: "HH:mm",
            week: "HH:mm",
            month: "HH:mm",
            quarter: "HH:mm",
            year: "HH:mm",
          },
        },
        ticks: {
          color: "#ffffff",
        },
      },
      yAxis: {
        title: {
          display: true,
          text: appStore.tempUnit,
          color: "#ffffff",
        },
        type: "linear",
        suggestedMin,
        suggestedMax,
        ticks: {
          color: "#ffffff",
        },
        grid: { color: "#bdbdbc" },
      },
    },
    plugins: {
      legend: {
        display: true,
        labels: {
          color: '#ffffff', // Explicitly forces the legend text color to white
        }
      },      annotation: {
        annotations: chartAnnotations.value,
      },
    },
  };

  return options;
});

onMounted(() => {
  // atm only used to render te schedule at the current time
  setStartDateNow();
  
  if (appStore.temperatureScale === TemperatureScale.Fahrenheit) {
    labelYPos.value = 240;
  } else {
    labelYPos.value = 115;
  }


  intervalId.value = setInterval(() => {
    getData();
  }, 3000);

  initChart();
  
  // SpeechSynthesis voice load (if they are not yet loaded)
  const synth = window.speechSynthesis;
  if (!synth.getVoices().length) {
    // Start dummy utterance with silent wser to load voices 
    const dummyUtterance = new SpeechSynthesisUtterance(" "); //not text
    dummyUtterance.volume = 0; // to be silent
    synth.speak(dummyUtterance);
  }
  
  // Start silent audio playback loop in the background to keep audio device open while screen gets locked
  audio = new Audio("/silence.mp3");
  audio.loop = true;
  audio.muted = true;
  audio.play().catch(() => {
    const startAudio = () => {
      audio?.play();
      window.removeEventListener("click", startAudio);
      window.removeEventListener("touchstart", startAudio);
    };
    window.addEventListener("click", startAudio, { once: true });
    window.addEventListener("touchstart", startAudio, { once: true });
  });

});

onBeforeUnmount(() => {
  clearAllNotificationTimeouts();
  clearInterval(intervalId.value);
  
  // Stop and release silent mp3 playback
  if (audio) {
    audio.pause();
    audio.src = "";
    audio = null;
  }

});

const displayStatus = computed(() => {
  let ds = status.value;

  if (boostStatus.value === BoostStatus.Boost) {
    ds += " (Boost)";
  }

  if (boostStatus.value === BoostStatus.Rest) {
    ds += " (Boost Rest)";
  }

  if (inAdaptationTime.value) {
    ds += " (Adaptive time)";
  }
  
  return ds;
});

</script>

<template>
  <!-- Notification dialog pop-up -->
  <v-dialog v-model="notificationDialog" max-width="500px">
    <v-card>
      <v-toolbar density="compact" color="dialog-header">
        <v-toolbar-title>{{ notificationDialogTitle }}</v-toolbar-title>
      </v-toolbar>

      <v-card-text v-html="notificationDialogText" />

      <v-card-actions>
        <v-spacer />
        <v-btn color="blue-darken-1" variant="text" @click="notificationDialog = false">
          Close
        </v-btn>
      </v-card-actions>
    </v-card>
  </v-dialog>

  <v-container class="spacing-playground pa-6" fluid>
    <v-form fast-fail @submit.prevent>
      <!-- Chart Row -->
      <v-row style="height: 50vh">
        <Line v-if="chartInitDone && chartData" :options="chartOptions" :data="chartData" />
      </v-row>
      
      <!-- First Control Row: Increased top margin (mt-6) to separate from chart, and gap (ga-4) for cell spacing -->
      <v-row no-gutters class="mt-6 ga-4">
        <!-- READ-ONLY LIVE DATA: Main status string -->
        <v-col cols="12" md="3" class="pa-1">
          <v-text-field 
            v-model="displayStatus" 
            readonly 
            variant="plain" 
            base-color="white" 
            color="white" 
            :label="$t('control.status')" 
          />
        </v-col>
        
        <!-- TRICKY SELECT FIELD: Outlined & dark grey when Idle -->
        <v-col cols="12" md="3" class="pa-1">
          <v-select
            v-model="selectedMashSchedule"
            :label="$t('control.mashSchedule')"
            :readonly="status !== 'Idle'"
            :items="appStore.mashSchedules"
            item-title="name"
            :filled="appStore.mashSchedules"
            :clearable="status === 'Idle'"
            return-object
            base-color="white"
            color="white"
            :variant="status === 'Idle' ? 'outlined' : 'plain'"
            :bg-color="status === 'Idle' ? '#1E1E1E' : ''"
            :append-inner-icon="status !== 'Idle' ? 'mdi-lock-outline' : ''"
          />
        </v-col>
        
        <!-- READ-ONLY LIVE DATA: Current step name -->
        <v-col cols="12" md="3" class="pa-1">
          <v-text-field 
            v-model="currentStepName" 
            readonly 
            variant="plain" 
            base-color="white" 
            color="white" 
            :label="$t('control.current_step_name')" 
          />
        </v-col>
        
        <!-- CONFIGURABLE INPUTFIELD: Remaining time in minutes -->
        <v-col cols="12" md="2" class="pa-1">
          <v-text-field
            v-model.number="localRemainingMinutes"
            type="number"
            min="0"
            step="1"
            variant="outlined"
            base-color="white"
            color="white"
            :bg-color="status !== 'Running' ? '#323232' : '#1E1E1E'"
            :disabled="status !== 'Running'"
            :label="$t('control.remaining_time') + ' (perc)'"
            :class="isEditingTime ? 'manual-mode-text' : 'automatic-mode-text'"
            @focus="onTimeFocus"
            @blur="onTimeBlur"
            @keydown.enter="onTimeEnter"
          />
        </v-col>
      </v-row>
      
      <!-- Second Control Row: Added gap (ga-4) to keep consistent horizontal spacing -->
      <v-row no-gutters class="mt-2 ga-4">
        <!-- READ-ONLY LIVE DATA: Current system temperature -->
        <v-col cols="12" md="3" class="pa-1">
          <v-text-field 
            v-model="temperature" 
            readonly 
            variant="plain" 
            base-color="white" 
            color="white" 
            :label="`${$t('control.temperature')} (${appStore.tempUnit})`" 
          />
        </v-col>
        
        <!-- CONFIGURABLE INPUTFIELD: Target temperature override control -->
        <v-col cols="12" md="3" class="pa-1">
          <v-text-field
            v-model.number="localTargetTemp"
            type="number"
            step="1"
            variant="outlined"
            base-color="white"
            color="white"
            :label="`${$t('control.target')} (${appStore.tempUnit})`" 
            :class="manualOverrideTemperature === null ? 'automatic-mode-text' : 'manual-mode-text'"
            :bg-color="manualOverrideTemperature !== null ? 'rgba(255, 112, 67, 0.25)' : '#1E1E1E'"
            :clearable="manualOverrideTemperature !== null"
            @click:clear="clearTempOverride"
            @focus="onTempFocus"
            @blur="onTempBlur"
            @keydown.enter="onTempEnter"
          />
        </v-col>
        
        <!-- READ-ONLY LIVE DATA: Combined PID and Output Override string -->
        <v-col cols="12" md="3" class="pa-1">
          <v-text-field
            v-model="outputSummary"
            variant="plain"
            base-color="white"
            color="white"
            :label="$t('control.output_summary')"
            readonly 
          />
        </v-col>
        
        <!-- CONFIGURABLE INPUTFIELD: Manual PID output percentage -->
        <v-col cols="12" md="2" class="pa-1">
          <v-text-field
            v-model.number="localOutput"
            type="number"
            min="0"
            max="100"
            variant="outlined"
            base-color="white"
            color="white"
            :label="$t('control.output')"
            :class="manualOverrideOutput === null ? 'automatic-mode-text' : 'manual-mode-text'"
            :bg-color="manualOverrideOutput !== null ? 'rgba(255, 112, 67, 0.25)' : '#1E1E1E'"
            :clearable="manualOverrideOutput !== null"
            @click:clear="clearOutputOverride"
            @focus="onOutputFocus"
            @blur="onOutputBlur"
            @keydown.enter="onOutputEnter"
          />
        </v-col>
      </v-row>
      
      <!-- Action Buttons and Power Metrics: tight top margin (mt-1) to reduce dead space below inputs -->
      <v-row no-gutters class="mt-1 ga-4">
        <v-col cols="12" md="6" class="pa-1">
          <v-btn v-if="status === 'Idle'" color="success" class="mt-2" block @click="start"> {{ $t('control.start') }} </v-btn>
          <v-btn v-else color="error" class="mt-2" block @click="stop"> {{ $t('control.stop') }} </v-btn>
        </v-col>
        <v-col cols="12" md="3" class="pa-1">
          <!-- Hidden/Empty spacer maintaining row density -->
        </v-col>
        <!-- READ-ONLY LIVE DATA: Power consumption -->
        <v-col cols="12" md="2" class="pa-1">
          <v-text-field 
            v-model="powerUsage" 
            readonly 
            variant="plain" 
            base-color="white" 
            color="white" 
            :label="$t('control.power_consumption')" 
          />          
        </v-col>
      </v-row>

      <!-- Stir Control Section -->
      <div class="text-subtitle-2 mt-6 mb-2">{{ $t('control.stir_control') }}</div>
      <v-divider :thickness="7" />

      <v-row no-gutters class="mt-2">
        <!-- READ-ONLY LIVE DATA: Motor / Stirrer current status -->
        <v-col cols="12" md="3" class="pa-1">
          <v-text-field 
            v-model="stirStatus" 
            readonly 
            variant="plain" 
            base-color="white" 
            color="white" 
            :label="$t('control.status')" 
          />
        </v-col>
      </v-row>

      <v-row>
        <v-col cols="12" md="">
          <v-range-slider
            v-model="stirInterval"
            :label="$t('control.interval')"
            step="1"
            thumb-label="always"
            :max="stirMax">
            <template v-slot:append>
              <!-- CONFIGURABLE INPUTFIELD: Max range configuration for stirrer loop -->
              <v-text-field
                v-model.number="stirMax"
                hide-details
                single-line
                type="number"
                variant="outlined"
                base-color="white"
                color="white"
                bg-color="#1E1E1E"
                style="width: 70px"
                density="compact"
                :label="$t('control.timespan')" 
              />
            </template>
          </v-range-slider>
        </v-col>
      </v-row>

      <!-- Stir Action Control Trigger Buttons -->
      <v-row>
        <v-col cols="12" md="6">
          <v-btn v-if="stirStatus === 'Idle'" color="success" class="mt-4" block @click="startStir"> {{ $t('control.start') }} </v-btn>
          <v-btn v-else color="error" class="mt-4" block @click="stopStir"> {{ $t('control.stop') }} </v-btn>
        </v-col>
      </v-row>

    </v-form>
  </v-container>
</template>
