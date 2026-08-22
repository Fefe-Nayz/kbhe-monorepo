import { useCallback, useMemo, useRef, useState } from "react";
import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";

import { AutosaveStatus, useAutosave } from "@/components/AutosaveStatus";
import { ColorPicker, type RGBColor } from "@/components/color-picker";
import { SectionCard, FormRow } from "@/components/shared/SectionCard";
import { Button } from "@/components/ui/button";
import { CommitSlider } from "@/components/ui/commit-slider";
import { Input } from "@/components/ui/input";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import { Switch } from "@/components/ui/switch";
import {
  optimisticMutationScopeId,
  useOptimisticMutation,
} from "@/hooks/use-optimistic-mutation";
import {
  buildRgbGradientFrame,
  buildRgbRainbowFrame,
  isLibhmkRgbEffect,
  libhmkRgbBridge,
  LibhmkRgbEffect,
  rgbBridgeSerialNumber,
  RGBBridgeCapability,
  type LibhmkRgbEffectId,
  type RgbBridgeDeviceInfo,
  type RgbBridgeState,
} from "@/lib/kbhe/rgb-bridge";

interface LibhmkLightingProps {
  device: RgbBridgeDeviceInfo;
}

const LIBHMK_EFFECT_OPTIONS: Array<{ value: LibhmkRgbEffectId; label: string }> = [
  { value: LibhmkRgbEffect.STATIC, label: "Static" },
  { value: LibhmkRgbEffect.BREATHING, label: "Breathing" },
  { value: LibhmkRgbEffect.RAINBOW, label: "Rainbow" },
  { value: LibhmkRgbEffect.RAINBOW_WAVE, label: "Rainbow wave" },
  { value: LibhmkRgbEffect.LIVE, label: "Live frame" },
];

type LiveWrite =
  | { kind: "pixel"; index: number; color: RGBColor }
  | { kind: "frame"; frame: number[] };

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

export function LibhmkLighting({ device }: LibhmkLightingProps) {
  const serialNumber = rgbBridgeSerialNumber(device);
  const queryKey = useMemo(
    () => ["libhmk-rgb-bridge", serialNumber, device.path, "state"] as const,
    [device.path, serialNumber],
  );
  const operationScope = optimisticMutationScopeId(queryKey);
  const qc = useQueryClient();
  const { saveState, markSaving, markSaved, markError } = useAutosave();
  const [fillColor, setFillColor] = useState<RGBColor>({ r: 32, g: 96, b: 255 });
  const [pixelColor, setPixelColor] = useState<RGBColor>({ r: 255, g: 255, b: 255 });
  const [pixelIndex, setPixelIndex] = useState(0);
  const pendingOperationsRef = useRef(0);
  const operationErrorRef = useRef<Error | null>(null);
  const fillSequenceRef = useRef(0);
  const latestFillSequenceRef = useRef(0);
  const confirmedFillColorRef = useRef(fillColor);

  const beginOperation = useCallback(() => {
    if (pendingOperationsRef.current === 0) {
      operationErrorRef.current = null;
      markSaving();
    }
    pendingOperationsRef.current += 1;
  }, [markSaving]);

  const settleOperation = useCallback((error?: Error) => {
    if (error && operationErrorRef.current === null) {
      operationErrorRef.current = error;
    }
    pendingOperationsRef.current = Math.max(0, pendingOperationsRef.current - 1);
    if (pendingOperationsRef.current !== 0) return;

    const batchError = operationErrorRef.current;
    operationErrorRef.current = null;
    if (batchError) {
      markError(batchError);
    } else {
      markSaved();
    }
  }, [markError, markSaved]);

  const stateQ = useQuery({
    queryKey,
    queryFn: () => libhmkRgbBridge.getState(device),
    retry: 1,
    staleTime: 2_000,
    refetchInterval: 5_000,
  });

  const enabledMut = useOptimisticMutation<RgbBridgeState, boolean>({
    queryKey,
    mutationFn: (enabled) => libhmkRgbBridge.setEnabled(device, enabled),
    optimisticUpdate: (current, enabled) => ({ ...(current ?? stateQ.data!), enabled }),
    onSuccess: () => settleOperation(),
    onError: (error) => {
      settleOperation(new Error(`RGB bridge rejected the enabled state: ${errorMessage(error)}`));
    },
  });

  const brightnessMut = useOptimisticMutation<RgbBridgeState, number>({
    queryKey,
    mutationFn: (brightness) => libhmkRgbBridge.setBrightness(device, brightness),
    optimisticUpdate: (current, brightness) => ({ ...(current ?? stateQ.data!), brightness }),
    onSuccess: () => settleOperation(),
    onError: (error) => {
      settleOperation(new Error(`RGB bridge rejected the brightness: ${errorMessage(error)}`));
    },
  });

  const effectMut = useOptimisticMutation<RgbBridgeState, LibhmkRgbEffectId>({
    queryKey,
    mutationFn: (effect) => libhmkRgbBridge.setEffect(device, effect),
    optimisticUpdate: (current, effect) => ({ ...(current ?? stateQ.data!), effect }),
    onSuccess: () => settleOperation(),
    onError: (error) => {
      settleOperation(new Error(`RGB bridge rejected the effect: ${errorMessage(error)}`));
    },
  });

  const fillMut = useMutation<void, Error, RGBColor, { sequence: number }>({
    scope: { id: operationScope },
    mutationFn: ({ r, g, b }) => libhmkRgbBridge.fill(device, r, g, b),
    onMutate: (color) => {
      const sequence = ++fillSequenceRef.current;
      latestFillSequenceRef.current = sequence;
      setFillColor(color);
      beginOperation();
      return { sequence };
    },
    onSuccess: (_result, color) => {
      confirmedFillColorRef.current = color;
      settleOperation();
    },
    onError: (error, _color, context) => {
      if (context?.sequence === latestFillSequenceRef.current) {
        setFillColor(confirmedFillColorRef.current);
        settleOperation(new Error(`RGB bridge rejected the fill color: ${errorMessage(error)}`));
      } else {
        // A newer queued fill supersedes this failed request. Its eventual
        // result is the only state/error relevant to the visible color.
        settleOperation();
      }
    },
  });

  const commandMut = useMutation<void, Error, "clear" | "restore">({
    scope: { id: operationScope },
    mutationFn: (command) => command === "clear"
      ? libhmkRgbBridge.clear(device)
      : libhmkRgbBridge.restoreEffect(device),
    onMutate: () => beginOperation(),
    onSuccess: async () => {
      await stateQ.refetch();
      settleOperation();
    },
    onError: (error) => {
      settleOperation(new Error(`RGB bridge command failed: ${errorMessage(error)}`));
    },
  });

  const liveWriteMut = useMutation<void, Error, LiveWrite>({
    scope: { id: operationScope },
    mutationFn: (write) => write.kind === "pixel"
      ? libhmkRgbBridge.setPixel(
        device,
        write.index,
        write.color.r,
        write.color.g,
        write.color.b,
      )
      : libhmkRgbBridge.writeFrame(device, write.frame),
    onMutate: () => beginOperation(),
    onSuccess: (_result, write) => {
      if (write.kind === "frame") {
        qc.setQueryData<RgbBridgeState>(queryKey, (current) => current
          ? { ...current, effect: LibhmkRgbEffect.LIVE }
          : current);
      }
      settleOperation();
    },
    onError: (error) => {
      settleOperation(new Error(`RGB live write failed: ${errorMessage(error)}`));
    },
  });

  if (stateQ.isPending) {
    return (
      <div className="flex flex-1 items-center justify-center text-sm text-muted-foreground">
        Negotiating the libhmk RGB bridge…
      </div>
    );
  }

  if (!stateQ.data) {
    return (
      <div className="flex flex-1 items-center justify-center px-6 text-center text-sm text-destructive">
        RGB bridge negotiation failed: {errorMessage(stateQ.error)}
      </div>
    );
  }

  const state = stateQ.data;
  const caps = state.capabilities;
  const supports = (flag: number) => (caps.capabilities & flag) !== 0;
  const canRestoreFromLive = state.effect !== LibhmkRgbEffect.LIVE
    || supports(RGBBridgeCapability.RESTORE_MODE);
  const canStaticWrite = supports(RGBBridgeCapability.FILL) && canRestoreFromLive;
  const canLiveWrite = supports(RGBBridgeCapability.LIVE_MODE)
    && (state.effect === LibhmkRgbEffect.LIVE || supports(RGBBridgeCapability.RESTORE_MODE));

  return (
    <div className="flex-1 overflow-auto p-4 lg:p-6">
      <div className="mx-auto flex max-w-4xl flex-col gap-4">
        <div className="flex items-start justify-between gap-4">
          <div>
            <h2 className="text-lg font-semibold">libhmk RGB bridge</h2>
            <p className="text-sm text-muted-foreground">
              Isolated RGB-only control for PID 0x0004. Native KBHE profile and updater
              commands are never routed to this interface.
            </p>
          </div>
          <AutosaveStatus state={saveState} />
        </div>

        <SectionCard
          title={device.product?.trim() || "KBHE 75HE (libhmk)"}
          description={`Protocol ${caps.protocolMajor}.${caps.protocolMinor} · ${caps.ledCount} LEDs · FFAB:00AB`}
        >
          <FormRow label="Lighting" description="Enable or disable the libhmk RGB engine.">
            <Switch
              checked={state.enabled ?? false}
              disabled={!supports(RGBBridgeCapability.ENABLED) || enabledMut.isPending}
              onCheckedChange={(enabled) => {
                beginOperation();
                enabledMut.mutate(enabled);
              }}
            />
          </FormRow>

          <FormRow label="Brightness" description="Stored by libhmk; live frames remain RAM-only.">
            <CommitSlider
              className="w-64"
              min={0}
              max={255}
              value={state.brightness ?? 0}
              disabled={!supports(RGBBridgeCapability.BRIGHTNESS)}
              onCommit={(brightness) => {
                beginOperation();
                brightnessMut.mutate(brightness);
              }}
            />
          </FormRow>

          <FormRow
            label="Effect"
            description="libhmk effect IDs are explicitly isolated from the native KBHE effect table."
          >
            <Select
              value={String(state.effect)}
              disabled={effectMut.isPending}
              onValueChange={(value) => {
                const effect = Number(value);
                if (!isLibhmkRgbEffect(effect)) return;
                beginOperation();
                effectMut.mutate(effect);
              }}
            >
              <SelectTrigger className="w-48">
                <SelectValue />
              </SelectTrigger>
              <SelectContent>
                {LIBHMK_EFFECT_OPTIONS.map((effect) => (
                  <SelectItem
                    key={effect.value}
                    value={String(effect.value)}
                    disabled={
                      effect.value === LibhmkRgbEffect.LIVE
                      && !supports(RGBBridgeCapability.LIVE_MODE)
                    }
                  >
                    {effect.label}
                  </SelectItem>
                ))}
              </SelectContent>
            </Select>
          </FormRow>
        </SectionCard>

        <SectionCard
          title="Static color"
          description="Fill every LED through the negotiated bridge, without using native firmware commands."
        >
          <FormRow label="Fill color">
            <ColorPicker
              color={fillColor}
              onChange={(color) => {
                if (canStaticWrite) fillMut.mutate(color);
              }}
            />
          </FormRow>
          <div className="flex flex-wrap gap-2 border-t pt-4">
            <Button
              variant="outline"
              disabled={!canStaticWrite || commandMut.isPending}
              onClick={() => commandMut.mutate("clear")}
            >
              Clear LEDs
            </Button>
            <Button
              variant="outline"
              disabled={!supports(RGBBridgeCapability.RESTORE_MODE) || commandMut.isPending}
              onClick={() => commandMut.mutate("restore")}
            >
              Restore previous effect
            </Button>
          </div>
        </SectionCard>

        <SectionCard
          title="Per-LED and live frames"
          description="Individual pixels and complete frames use only the negotiated libhmk bridge. Frame publication is atomic on the final chunk."
        >
          <div className="grid gap-4 md:grid-cols-[1fr_auto] md:items-end">
            <div className="grid gap-3 sm:grid-cols-[8rem_1fr] sm:items-end">
              <div className="space-y-1.5">
                <label htmlFor="libhmk-pixel-index" className="text-sm font-medium">LED index</label>
                <Input
                  id="libhmk-pixel-index"
                  type="number"
                  min={0}
                  max={caps.ledCount - 1}
                  value={pixelIndex}
                  onChange={(event) => {
                    const index = Number(event.target.value);
                    if (Number.isInteger(index)) {
                      setPixelIndex(Math.min(caps.ledCount - 1, Math.max(0, index)));
                    }
                  }}
                />
              </div>
              <FormRow label="Pixel color" className="py-0">
                <ColorPicker color={pixelColor} onChange={setPixelColor} />
              </FormRow>
            </div>
            <Button
              disabled={!supports(RGBBridgeCapability.PIXEL) || !canLiveWrite || liveWriteMut.isPending}
              onClick={() => liveWriteMut.mutate({
                kind: "pixel",
                index: pixelIndex,
                color: pixelColor,
              })}
            >
              Apply LED {pixelIndex}
            </Button>
          </div>

          <div className="mt-4 flex flex-wrap gap-2 border-t pt-4">
            <Button
              variant="outline"
              disabled={
                !supports(RGBBridgeCapability.FRAME_CHUNKS)
                || !canLiveWrite
                || liveWriteMut.isPending
              }
              onClick={() => liveWriteMut.mutate({
                kind: "frame",
                frame: buildRgbGradientFrame(
                  caps.ledCount,
                  [fillColor.r, fillColor.g, fillColor.b],
                  [255 - fillColor.r, 255 - fillColor.g, 255 - fillColor.b],
                ),
              })}
            >
              Live gradient
            </Button>
            <Button
              variant="outline"
              disabled={
                !supports(RGBBridgeCapability.FRAME_CHUNKS)
                || !canLiveWrite
                || liveWriteMut.isPending
              }
              onClick={() => liveWriteMut.mutate({
                kind: "frame",
                frame: buildRgbRainbowFrame(caps.ledCount),
              })}
            >
              Live rainbow frame
            </Button>
          </div>
        </SectionCard>

        <SectionCard title="Negotiated capabilities">
          <p className="font-mono text-xs text-muted-foreground">
            bitmap 0x{caps.capabilities.toString(16).padStart(4, "0")} · chunk {caps.chunkBytes}
            {" bytes · live effect "}{caps.liveEffectId} · current effect {state.effect}
          </p>
        </SectionCard>
      </div>
    </div>
  );
}
