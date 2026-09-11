export interface INotification {
  name: string;
  message: string;
  timeFromStart: number;
  timePoint: number;
  buzzer: boolean;
  done?: boolean;
  refStepIndex: number;
  timeAbsolute: number;
  
}

export const defaultNotification: INotification = {
  name: "New Notification",
  message: "",
  timeFromStart: 0,
  timePoint: 0,
  buzzer: true,
  done: false,
  refStepIndex: 0,
  timeAbsolute: 0,
};
