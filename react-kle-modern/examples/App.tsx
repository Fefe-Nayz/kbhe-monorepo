import { useId, useMemo, useState, type ChangeEvent } from "react";
import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { KeyboardLayout, parseKLE, type KeyboardKey, type KeyboardRenderTheme } from "../src";
import "../src/styles.css";
import defaultLayout from "./default-layout.json";

const DEFAULT_LAYOUT = JSON.stringify(defaultLayout, null, 2);
const DEFAULT_SOURCE_NAME = "keyboard-layout(5).json";

function toMessage(error: unknown): string {
  if (error instanceof Error) return error.message;
  return "Unknown error";
}

export default function App() {
  const inputId = useId();
  const [selected, setSelected] = useState<KeyboardKey | null>(null);
  const [layoutInput, setLayoutInput] = useState(DEFAULT_LAYOUT);
  const [sourceName, setSourceName] = useState(DEFAULT_SOURCE_NAME);
  const [fileReadError, setFileReadError] = useState<string | null>(null);
  const [theme, setTheme] = useState<KeyboardRenderTheme>("modern");

  const parsed = useMemo(() => {
    try {
      return {
        layout: parseKLE(layoutInput),
        error: null as string | null,
      };
    } catch (error) {
      return {
        layout: null,
        error: toMessage(error),
      };
    }
  }, [layoutInput]);

  async function onFileChange(event: ChangeEvent<HTMLInputElement>) {
    const file = event.target.files?.[0];
    event.currentTarget.value = "";
    if (!file) return;

    try {
      const text = await file.text();
      setLayoutInput(text);
      setSourceName(file.name);
      setSelected(null);
      setFileReadError(null);
    } catch (error) {
      setFileReadError(`Could not read file: ${toMessage(error)}`);
    }
  }

  function resetSample() {
    setLayoutInput(DEFAULT_LAYOUT);
    setSourceName(DEFAULT_SOURCE_NAME);
    setSelected(null);
    setFileReadError(null);
  }

  const subtitle = useMemo(() => {
    if (parsed.error) return "Fix JSON parsing error";
    if (!selected) return "Click a key";
    return `${selected.labels[4] || selected.labels[0] || "Key"} · ${selected.width}u × ${selected.height}u`;
  }, [parsed.error, selected]);

  return (
    <div className="min-h-screen bg-[radial-gradient(1000px_420px_at_0%_-10%,oklch(0.98_0_0),transparent),radial-gradient(900px_420px_at_100%_-20%,oklch(0.96_0_0),transparent)] p-4 md:p-8">
      <div className="mx-auto w-full max-w-[1240px]">
        <Card className="overflow-visible border border-border/80">
          <CardHeader className="gap-2 border-b border-border/70">
            <div className="flex flex-wrap items-start justify-between gap-3">
              <div className="space-y-1">
                <CardTitle className="text-xl tracking-tight md:text-2xl">react-kle-modern</CardTitle>
                <p className="text-sm text-muted-foreground">{subtitle}</p>
              </div>
            </div>
          </CardHeader>

          <CardContent className="space-y-4 pt-4">
            <section className="grid gap-4 lg:grid-cols-[minmax(0,1fr)_auto_auto]" aria-label="Layout controls">
              <div className="space-y-2">
                <Label htmlFor={inputId}>KLE JSON</Label>
                <Input
                  id={inputId}
                  type="file"
                  accept=".json,.json5,application/json,text/plain"
                  onChange={onFileChange}
                />
              </div>

              <div className="space-y-2">
                <Label>Theme</Label>
                <div className="flex gap-2">
                  <Button
                    type="button"
                    size="sm"
                    variant={theme === "modern" ? "default" : "outline"}
                    onClick={() => setTheme("modern")}
                  >
                    modern
                  </Button>
                  <Button
                    type="button"
                    size="sm"
                    variant={theme === "kle" ? "default" : "outline"}
                    onClick={() => setTheme("kle")}
                  >
                    kle
                  </Button>
                </div>
              </div>

              <div className="flex items-end gap-2 lg:justify-end">
                <Button type="button" variant="outline" onClick={resetSample}>
                  Reset sample
                </Button>
              </div>
            </section>

            <div className="flex flex-wrap items-center gap-2 text-sm text-muted-foreground">
              <span>Source:</span>
              <Badge variant="outline">{sourceName}</Badge>
            </div>

            {(fileReadError || parsed.error) && (
              <Alert variant="destructive">
                <AlertTitle>Invalid layout</AlertTitle>
                <AlertDescription>{fileReadError ?? `Parse error: ${parsed.error}`}</AlertDescription>
              </Alert>
            )}

            {parsed.layout ? (
              <div className="overflow-auto rounded-xl border border-border/80 bg-muted/35 p-3 md:p-4">
                <KeyboardLayout
                  layout={parsed.layout}
                  theme={theme}
                  interactive
                  selectedKeyId={selected?.id}
                  onKeyClick={setSelected}
                />
              </div>
            ) : (
              <Alert variant="destructive">
                <AlertTitle>Cannot render keyboard</AlertTitle>
                <AlertDescription>
                  Invalid layout JSON. Pick another file or reset to the sample.
                </AlertDescription>
              </Alert>
            )}
          </CardContent>
        </Card>
      </div>
    </div>
  );
}
