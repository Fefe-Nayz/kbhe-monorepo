import { useEffect } from "react";
import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import BaseKeyboard from "@/components/baseKeyboard";
import { useKeyboardStore } from "@/stores/keyboard-store";
import { useDeviceSession } from "@/lib/kbhe/session";
import { kbheDevice } from "@/lib/kbhe/device";
import { queryKeys } from "@/lib/query/keys";
import { SectionCard, FormRow } from "@/components/shared/SectionCard";
import { PageHeader } from "@/components/shared/PageLayout";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { Button } from "@/components/ui/button";
import { Skeleton } from "@/components/ui/skeleton";
import { Badge } from "@/components/ui/badge";
import {
  IconRefresh,
  IconPlayerPlay,
  IconPlayerStop,
  IconAlertTriangle,
  IconCheck,
} from "@tabler/icons-react";

// Check if shadcn Progress is available, fallback if not
function ProgressBar({ value }: { value: number }) {
  return (
    <div className="h-2 w-full rounded-full bg-muted overflow-hidden">
      <div
        className="h-full bg-primary transition-all"
        style={{ width: `${Math.max(0, Math.min(100, value))}%` }}
      />
    </div>
  );
}

function CalibrationStatusSection({ connected }: { connected: boolean }) {
  const qc = useQueryClient();

  const calQ = useQuery({
    queryKey: queryKeys.calibration.all(),
    queryFn: () => kbheDevice.getCalibration(),
    enabled: connected,
  });

  return (
    <SectionCard
      title="Calibration Status"
      headerRight={
        <Button
          variant="outline" size="sm" className="h-7 gap-1.5"
          disabled={!connected || calQ.isLoading}
          onClick={() => void qc.invalidateQueries({ queryKey: queryKeys.calibration.all() })}
        >
          <IconRefresh className="size-3" />
          Refresh
        </Button>
      }
    >
      {calQ.isLoading ? (
        <div className="space-y-2">{[0,1].map(i=><Skeleton key={i} className="h-7 w-full"/>)}</div>
      ) : !calQ.data ? (
        <p className="text-sm text-muted-foreground">
          {connected ? "Could not load calibration data." : "Connect device to view calibration."}
        </p>
      ) : (
        <div className="flex flex-col divide-y">
          <FormRow label="LUT Zero Value">
            <Badge variant="outline" className="font-mono text-xs">
              {calQ.data.lut_zero_value}
            </Badge>
          </FormRow>
          <FormRow label="Key Zero Values" description="Min/max across all 82 keys">
            <span className="text-xs text-muted-foreground font-mono">
              {Math.min(...calQ.data.key_zero_values)} – {Math.max(...calQ.data.key_zero_values)}
            </span>
          </FormRow>
          <FormRow label="Key Max Values" description="Min/max across all 82 keys">
            <span className="text-xs text-muted-foreground font-mono">
              {Math.min(...calQ.data.key_max_values)} – {Math.max(...calQ.data.key_max_values)}
            </span>
          </FormRow>
        </div>
      )}
    </SectionCard>
  );
}

function GuidedCalibrationSection({ connected }: { connected: boolean }) {
  const qc = useQueryClient();

  const statusQ = useQuery({
    queryKey: queryKeys.calibration.guidedStatus(),
    queryFn: () => kbheDevice.guidedCalibrationStatus(),
    enabled: connected,
    refetchInterval: (query) => (query.state.data?.active ? 1000 : false),
  });

  const startMutation = useMutation({
    mutationFn: () => kbheDevice.guidedCalibrationStart(),
    onSuccess: () => void qc.invalidateQueries({ queryKey: queryKeys.calibration.guidedStatus() }),
  });

  const abortMutation = useMutation({
    mutationFn: () => kbheDevice.guidedCalibrationAbort(),
    onSuccess: () => void qc.invalidateQueries({ queryKey: queryKeys.calibration.guidedStatus() }),
  });

  const guided = statusQ.data;
  const isActive = guided?.active ?? false;
  const progress = guided?.progress_percent ?? 0;

  const PHASE_LABELS: Record<number, string> = {
    0: "Waiting to start",
    1: "Release all keys",
    2: "Press each key slowly",
    3: "Finalizing…",
    4: "Done",
  };

  return (
    <SectionCard title="Guided Calibration" description="Step-by-step wizard to calibrate all keys">
      {!connected ? (
        <p className="text-sm text-muted-foreground">Connect device to use guided calibration.</p>
      ) : (
        <div className="flex flex-col gap-3">
          {isActive && (
            <>
              <div className="flex items-start gap-2 rounded-md border px-3 py-2 text-sm">
                <IconAlertTriangle className="size-4 mt-0.5 shrink-0 text-amber-500" />
                <span>
                  {PHASE_LABELS[guided?.phase ?? 0] ?? `Phase ${guided?.phase}`}
                  {guided?.current_key != null && guided.current_key < 255 && (
                    <span className="ml-2 text-muted-foreground">(Key {guided.current_key})</span>
                  )}
                </span>
              </div>
              <ProgressBar value={progress} />
              <p className="text-xs text-muted-foreground text-right">{progress}%</p>
            </>
          )}
          {!isActive && guided?.phase === 4 && (
            <div className="flex items-center gap-2 rounded-md border border-green-200 bg-green-50 px-3 py-2 text-sm text-green-700 dark:border-green-800 dark:bg-green-950 dark:text-green-400">
              <IconCheck className="size-4 shrink-0" />
              Calibration completed successfully.
            </div>
          )}
          <div className="flex gap-2">
            <Button
              onClick={() => startMutation.mutate()}
              disabled={isActive || startMutation.isPending}
            >
              <IconPlayerPlay className="size-4 mr-1" />
              {isActive ? "Running…" : "Start Guided"}
            </Button>
            {isActive && (
              <Button
                variant="destructive"
                onClick={() => abortMutation.mutate()}
                disabled={abortMutation.isPending}
              >
                <IconPlayerStop className="size-4 mr-1" />
                Abort
              </Button>
            )}
          </div>
        </div>
      )}
    </SectionCard>
  );
}

function ManualCalibrationSection({ connected }: { connected: boolean }) {
  const selectedKeys = useKeyboardStore((s) => s.selectedKeys);
  const qc = useQueryClient();

  const focusedKeyId = selectedKeys[0] ?? null;
  const keyIndex = focusedKeyId?.startsWith("key-")
    ? parseInt(focusedKeyId.replace("key-", ""), 10) : null;

  const autoCalMutation = useMutation({
    mutationFn: (idx: number) => kbheDevice.autoCalibrate(idx),
    onSuccess: () => void qc.invalidateQueries({ queryKey: queryKeys.calibration.all() }),
  });

  return (
    <SectionCard
      title={keyIndex != null ? `Manual Calibration — Key ${keyIndex}` : "Manual Calibration"}
      description={keyIndex == null ? "Select a key on the keyboard to calibrate it" : undefined}
    >
      {keyIndex == null ? (
        <p className="text-sm text-muted-foreground py-2">Click a key above.</p>
      ) : (
        <div className="flex flex-col gap-3">
          <Button
            variant="outline"
            disabled={!connected || autoCalMutation.isPending}
            onClick={() => autoCalMutation.mutate(keyIndex)}
            className="w-fit"
          >
            {autoCalMutation.isPending ? "Calibrating…" : `Auto-calibrate Key ${keyIndex}`}
          </Button>
          {autoCalMutation.isSuccess && (
            <p className="text-xs text-green-600">
              <IconCheck className="inline size-3 mr-1" />
              Key {keyIndex} calibrated.
            </p>
          )}
        </div>
      )}
    </SectionCard>
  );
}

export default function Calibration() {
  const setSaveEnabled = useKeyboardStore((s) => s.setSaveEnabled);
  const { status } = useDeviceSession();
  const connected = status === "connected";

  useEffect(() => { setSaveEnabled(true); return () => setSaveEnabled(false); }, [setSaveEnabled]);

  return (
    <div className="flex flex-col h-full overflow-hidden">
      <div className="shrink-0 border-b px-4 py-2">
        <PageHeader title="Calibration" description="ADC zero/max calibration for all keys" />
      </div>
      <div className="flex-1 overflow-hidden min-h-0">
        <Tabs defaultValue="status" className="flex flex-col h-full">
          <div className="shrink-0 border-b px-4">
            <TabsList className="h-9 mt-1">
              <TabsTrigger value="status">Status</TabsTrigger>
              <TabsTrigger value="manual">Manual</TabsTrigger>
              <TabsTrigger value="guided">Guided</TabsTrigger>
            </TabsList>
          </div>

          <TabsContent value="status" className="flex-1 overflow-y-auto p-4 mt-0">
            <div className="flex flex-col gap-4 max-w-2xl mx-auto">
              <CalibrationStatusSection connected={connected} />
            </div>
          </TabsContent>

          <TabsContent value="manual" className="flex-1 overflow-hidden flex flex-col mt-0 min-h-0">
            <div className="shrink-0 border-b px-4 py-4 overflow-x-auto bg-muted/20">
              <BaseKeyboard mode="single" onButtonClick={() => {}} />
            </div>
            <div className="flex-1 overflow-y-auto p-4">
              <div className="flex flex-col gap-4 max-w-2xl mx-auto">
                <ManualCalibrationSection connected={connected} />
              </div>
            </div>
          </TabsContent>

          <TabsContent value="guided" className="flex-1 overflow-y-auto p-4 mt-0">
            <div className="flex flex-col gap-4 max-w-2xl mx-auto">
              <GuidedCalibrationSection connected={connected} />
            </div>
          </TabsContent>
        </Tabs>
      </div>
    </div>
  );
}
