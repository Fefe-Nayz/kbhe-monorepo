export function requireDeviceSuccess(result: boolean, action: string): void {
  if (!result) {
    throw new Error(`Device rejected ${action}`);
  }
}
