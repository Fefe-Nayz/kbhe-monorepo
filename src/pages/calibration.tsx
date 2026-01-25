import { invoke } from "@tauri-apps/api/core";
import { useEffect } from "react";

export default function Calibration() {
  useEffect(() => { invoke("list_hid_devices") },
       []);

  return (
    <div className="p-8">
      <h1 className="text-3xl font-bold">Calibration</h1>
    </div>
  )
}

