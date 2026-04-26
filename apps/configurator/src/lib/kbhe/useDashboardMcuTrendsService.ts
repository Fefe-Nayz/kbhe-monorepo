import { useEffect } from "react";
import { useQuery } from "@tanstack/react-query";
import { kbheDevice } from "@/lib/kbhe/device";
import { useDeviceSession } from "@/lib/kbhe/session";
import { queryKeys } from "@/lib/query/keys";
import { useDashboardMcuTrendStore } from "@/stores/dashboard-mcu-trends-store";

const POLL_INTERVAL = 5000;

/**
 * Global background service: updates dashboard MCU sparkline trends,
 * even when the dashboard page is not currently mounted.
 */
export function useDashboardMcuTrendsService() {
  const connected = useDeviceSession((state) => state.status === "connected");
  const pushMetrics = useDashboardMcuTrendStore((state) => state.pushMetrics);

  const mcuQ = useQuery({
    queryKey: queryKeys.device.mcuMetrics(),
    queryFn: () => kbheDevice.getMcuMetrics(),
    enabled: connected,
    refetchInterval: POLL_INTERVAL,
    refetchIntervalInBackground: true,
  });

  useEffect(() => {
    if (!mcuQ.data) {
      return;
    }

    pushMetrics(mcuQ.data);
  }, [mcuQ.data, pushMetrics]);
}
