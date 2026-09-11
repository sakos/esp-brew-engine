export interface IMashStep {
  index: number;
  name: string;
  temperature: number;
  stepTime: number;
  time: number;
  extendStepTimeIfNeeded: boolean;
  allowBoost: boolean;
}

export const defaultMashStep: IMashStep = {
  index: 0,
  name: "New Step",
  temperature: 50,
  stepTime: 5,
  time: 10,
  extendStepTimeIfNeeded: true,
  allowBoost: false,
};
