import { useNavigate } from "react-router-dom";
import { IconPlugConnectedX, IconRefresh, IconUpload } from "@tabler/icons-react";
import { Button } from "@/components/ui/button";
import { EmptyState } from "@/components/shared/EmptyState";
import { DeviceSessionManager, useDeviceSession } from "@/lib/kbhe/session";

/**
 * The one "no keyboard attached" screen. Every page that cannot do anything
 * useful offline renders this instead of inventing its own sentence, so the
 * disconnected app reads as one product rather than fourteen dead ends.
 */
export function ConnectPrompt({
  feature,
  className,
}: {
  /** What the page would let them do, e.g. "configure the rotary encoder". */
  feature: string;
  className?: string;
}) {
  const status = useDeviceSession((s) => s.status);
  const connecting = status === "connecting";
  const recoveryOnly = status === "recovery-only";
  const navigate = useNavigate();

  return (
    <EmptyState
      className={className}
      size="lg"
      icon={<IconPlugConnectedX />}
      title={recoveryOnly ? "Configuration unavailable" : "No keyboard connected"}
      description={recoveryOnly
        ? `This keyboard is available only for safe recovery, so it cannot ${feature}. Update from the Firmware page.`
        : `Plug in your KBHE keyboard over USB to ${feature}.`}
      action={
        <Button
          variant="outline"
          size="sm"
          disabled={connecting}
          onClick={() => {
            if (recoveryOnly) {
              navigate("/firmware");
            } else {
              void DeviceSessionManager.reconnect();
            }
          }}
        >
          {recoveryOnly
            ? <IconUpload />
            : <IconRefresh className={connecting ? "animate-spin" : undefined} />}
          {recoveryOnly ? "Open Firmware" : connecting ? "Searching…" : "Retry connection"}
        </Button>
      }
    />
  );
}
