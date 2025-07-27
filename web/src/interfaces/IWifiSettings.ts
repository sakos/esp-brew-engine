export interface IWifiSettings {
 
  enableAP: boolean;

  // AP configuration
  apSSID: string;
  apPassword: string;
  apMaxPower: number;

  // STA configuration
  staSSID: string;
  staPassword: string;
}