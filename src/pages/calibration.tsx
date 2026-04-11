import { useState, useCallback, useMemo } from "react";
import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import BaseKeyboard from "@/components/baseKeyboard";
import { KeyboardEditor } from "@/components/keyboard-editor";
import { KeyTester } from "@/components/key-tester";
import { SectionCard } from "@/components/shared/SectionCard";
import { Button } from "@/components/ui/button";
import { Badge } from "@/components/ui/badge";
import { Progress } from "@/components/ui/progress";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { CommitSlider } from "@/components/ui/slider";
import { Label } from "@/components/ui/label";
import { Skeleton } from "@/components/ui/skeleton";
import { useKeyboardStore } from "@/stores/keyboard-store";
import { useDeviceSession } from "@/lib/kbhe/session";
import { kbheDevice } from "@/lib/kbhe/device";
import { KEY_COUNT } from "@/lib/kbhe/protocol";
import { usePageVisible } from "@/hooks/use-page-visible";
import {
  IconRefresh,
  IconPlayerPlay,
  IconPlayerStop,
} from "@tabler/icons-react";

const MAX_TRAVEL_MM = 4.0;

function heatmapColor(t: number): string {
  const hue = 120 - Math.min(1, Math.max(0, t)) * 120;
  return `hsl(${hue}, 80%, 45%)`;
}

type GuidedState = "idle" | "running" | "success" | "error";

export default function Calibration() {
  const selectedKeys = useKeyboardStore((s) => s.selectedKeys);
  const { status }   = useDeviceSession();
  const connected    = status === "connected";
  const visible      = usePageVisible();
  const qc           = useQueryClient();

  const [activeTab, setActiveTab] = useState("status");
  const [guidedState, setGuidedState] = useState<GuidedState>("idle");
  const [guidedProgress, setGuidedProgress] = useState(0);

  const focusedKeyId = selectedKeys[0] ?? null;
  const keyIndex = focusedKeyId?.startsWith("key-")
    ? parseInt(focusedKeyId.replace("key-", ""), 10) : null;

  const calibrationQ = useQuery({
    queryKey: ["calibration", "all"],
    queryFn: () => kbheDevice.getCalibration(),
    enabled: connected,
    staleTime: 30_000,
  });

  const polling = connected && visible && activeTab === "status";

  const keyStatesQ = useQuery({
    queryKey: ["calibration", "keyStates"],
    queryFn: () => kbheDevice.getKeyStates(),
    enabled: polling,
    refetchInterval: polling ? 150 : false,
  });

  const autoCalibrateMutation = useMutation({
    mutationFn: (idx: number) => kbheDevice.autoCalibrate(idx),
    onSuccess: () => void qc.invalidateQueries({ queryKey: ["calibration"] }),
  });

  const guidedStart = useCallback(async () => {
    if (!connected) return;
    setGuidedState("running");
    setGuidedProgress(0);
    try {
      await kbheDevice.guidedCalibrationStart();
      const poll = async () => {
        const st = await kbheDevice.guidedCalibrationStatus();
        if (!st) return;
        if (!st.active) {
          setGuidedState("success");
          setGuidedProgress(100);
          void qc.invalidateQueries({ queryKey: ["calibration"] });
        } else {
          setGuidedProgress(st.progress_percent ?? 50);
          setTimeout(() => void poll(), 200);
        }
      };
      await poll();
    } catch {
      setGuidedState("error");
    }
  }, [connected, qc]);

  const guidedAbort = useCallback(async () => {
    try {
      await kbheDevice.guidedCalibrationAbort();
    } catch { /* ignore */ }
    setGuidedState("idle");
    setGuidedProgress(0);
  }, []);

  const distances = keyStatesQ.data?.distances_mm;

  const keyColorMap = useMemo(() => {
    if (!distances) return undefined;
    const map: Record<string, string> = {};
    for (let i = 0; i < KEY_COUNT; i++) {
      const d = distances[i] ?? 0;
      map[`key-${i}`] = heatmapColor(d / MAX_TRAVEL_MM);
    }
    return map;
  }, [distances]);

  const renderKeyOverlay = useCallback(
    (keyId: string) => {
      if (!distances) return undefined;
      if (!keyId.startsWith("key-")) return undefined;
      const idx = parseInt(keyId.replace("key-", ""), 10);
      const distMm = distances[idx];
      if (distMm === undefined) return undefined;
      return (
        <span className="text-[8px] font-mono text-white drop-shadow-[0_0_2px_rgba(0,0,0,.8)]">
          {distMm.toFixed(1)}
        </span>
      );
    },
    [distances],
  );

  const menubar = (
    <>
      <div className="flex items-center gap-2">
        {polling && (
          <Badge variant="secondary" className="gap-1 text-xs">
            Live
          </Badge>
        )}
      </div>
      <div className="flex items-center gap-2">
        <Button variant="outline" size="sm" className="h-8"
          onClick={() => void qc.invalidateQueries({ queryKey: ["calibration"] })}>
          <IconRefresh className="size-4" />
          Reload
        </Button>
      </div>
    </>
  );

  return (
    <KeyboardEditor
      keyboard={
        <BaseKeyboard
          mode="single"
          onButtonClick={() => {}}
          showLayerSelector={false}
          showRotary={false}
          keyColorMap={keyColorMap}
          renderKeyOverlay={connected ? renderKeyOverlay : undefined}
        />
      }
      menubar={menubar}
    >
      <Tabs value={activeTab} onValueChange={setActiveTab} className="w-full">
        <TabsList>
          <TabsTrigger value="status">Status</TabsTrigger>
          <TabsTrigger value="manual">Manual</TabsTrigger>
          <TabsTrigger value="guided">Guided</TabsTrigger>
        </TabsList>

        <TabsContent value="status" className="mt-4">
          <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
            <SectionCard title="Calibration Data">
              {calibrationQ.isLoading ? (
                <div className="space-y-2">{[0,1,2].map(i => <Skeleton key={i} className="h-6 w-full" />)}</div>
              ) : calibrationQ.data ? (
                <div className="flex flex-col gap-2 text-sm">
                  <div className="flex justify-between">
                    <span className="text-muted-foreground">Keys calibrated</span>
                    <span className="font-mono">{KEY_COUNT}</span>
                  </div>
                  <div className="flex justify-between">
                    <span className="text-muted-foreground">LUT Reference</span>
                    <span className="font-mono">{calibrationQ.data.lut_zero_value}</span>
                  </div>
                  {keyIndex != null && calibrationQ.data.key_zero_values[keyIndex] !== undefined && (
                    <>
                      <div className="flex justify-between border-t pt-2">
                        <span className="text-muted-foreground">Key {keyIndex} Zero</span>
                        <span className="font-mono">{calibrationQ.data.key_zero_values[keyIndex]}</span>
                      </div>
                      <div className="flex justify-between">
                        <span className="text-muted-foreground">Key {keyIndex} Max</span>
                        <span className="font-mono">{calibrationQ.data.key_max_values[keyIndex]}</span>
                      </div>
                    </>
                  )}
                </div>
              ) : (
                <p className="text-sm text-muted-foreground">Connect device to view calibration.</p>
              )}
            </SectionCard>

            <SectionCard title="Key Tester">
              <KeyTester pressHeight="h-16" releaseHeight="h-20" />
            </SectionCard>
          </div>
        </TabsContent>

        <TabsContent value="manual" className="mt-4">
          <SectionCard title="Manual Calibration">
            <div className="flex flex-col gap-4">
              <div className="flex gap-2">
                <Button size="sm" disabled={!connected || keyIndex == null}
                  onClick={() => keyIndex != null && autoCalibrateMutation.mutate(keyIndex)}>
                  Auto Zero Key {keyIndex ?? "—"}
                </Button>
                <Button size="sm" variant="outline" disabled={!connected}
                  onClick={() => autoCalibrateMutation.mutate(0xff)}>
                  Auto Zero All
                </Button>
              </div>

              {keyIndex != null && calibrationQ.data && (
                <div className="flex flex-col gap-4 border-t pt-4">
                  <div className="grid gap-2">
                    <Label className="text-sm">Key {keyIndex} Zero</Label>
                    <CommitSlider
                      min={-32768} max={32767} step={1}
                      value={calibrationQ.data.key_zero_values[keyIndex] ?? 0}
                      onCommit={(v) => {
                        if (!calibrationQ.data) return;
                        const zeros = Array.from(calibrationQ.data.key_zero_values);
                        zeros[keyIndex] = v;
                        void kbheDevice.setCalibration(
                          calibrationQ.data.lut_zero_value,
                          zeros,
                          calibrationQ.data.key_max_values,
                        );
                      }}
                      disabled={!connected}
                    />
                  </div>
                </div>
              )}
            </div>
          </SectionCard>
        </TabsContent>

        <TabsContent value="guided" className="mt-4">
          <SectionCard title="Guided Calibration">
            <div className="flex flex-col gap-4">
              <p className="text-sm text-muted-foreground">
                Guided calibration will systematically calibrate all keys. Follow the on-screen instructions.
                The keyboard LEDs will indicate which key to press.
              </p>
              <div className="flex gap-2">
                {guidedState === "running" ? (
                  <Button variant="destructive" size="sm" onClick={() => void guidedAbort()}>
                    <IconPlayerStop className="size-4" />
                    Abort
                  </Button>
                ) : (
                  <Button size="sm" disabled={!connected} onClick={() => void guidedStart()}>
                    <IconPlayerPlay className="size-4" />
                    Start Guided Calibration
                  </Button>
                )}
              </div>

              {guidedState !== "idle" && (
                <div className="flex flex-col gap-2">
                  <Progress value={guidedProgress} className="h-2" />
                  <span className="text-xs text-muted-foreground">
                    {guidedState === "running" && `Calibrating... ${guidedProgress}%`}
                    {guidedState === "success" && "Calibration complete!"}
                    {guidedState === "error" && "Calibration failed."}
                  </span>
                </div>
              )}
            </div>
          </SectionCard>
        </TabsContent>
      </Tabs>
    </KeyboardEditor>
  );
}
