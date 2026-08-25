import { useNavigate } from "react-router-dom";
import { IconAlertTriangle } from "@tabler/icons-react";

import { Button } from "@/components/ui/button";
import { compatibilityPresentation } from "@/lib/kbhe/compatibility";
import { useDeviceSession } from "@/lib/kbhe/session";

export function CompatibilityBanner() {
  const navigate = useNavigate();
  const compatibility = useDeviceSession((state) => state.compatibility);

  if (!compatibility || compatibility.status === "compatible") {
    return null;
  }

  const presentation = compatibilityPresentation(compatibility);
  return (
    <div
      className="flex min-h-12 items-center gap-3 border-b border-warning/35 bg-warning/10 px-4 py-2 text-sm text-warning"
      role="alert"
    >
      <IconAlertTriangle className="size-4 shrink-0" />
      <div className="min-w-0 flex-1">
        <span className="font-semibold">{presentation.title}.</span>{" "}
        <span>{compatibility.reason} Configuration controls are disabled to protect the device.</span>
      </div>
      <div className="flex shrink-0 items-center gap-2">
        {presentation.showFirmwareAction && (
          <Button
            variant="outline"
            size="sm"
            className="h-7 border-warning/40 bg-transparent text-xs text-warning hover:bg-warning/15 dark:text-warning"
            onClick={() => navigate("/firmware")}
          >
            Open Firmware
          </Button>
        )}
        {presentation.showAppUpdateAction && (
          <Button
            variant="outline"
            size="sm"
            className="h-7 border-warning/40 bg-transparent text-xs text-warning hover:bg-warning/15 dark:text-warning"
            onClick={() => navigate("/settings")}
          >
            Update App
          </Button>
        )}
      </div>
    </div>
  );
}
