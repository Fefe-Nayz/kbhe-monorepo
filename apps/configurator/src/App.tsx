import { QueryClientProvider } from "@tanstack/react-query";
import { useEffect, useState } from "react";
import { queryClient } from "@/lib/query/queryClient";
import { ThemeProvider } from "@/components/theme-provider";
import { AppShell } from "@/components/AppShell";
import { Toaster } from "@/components/ui/sonner";
import { applyWindowsMicaStyling, signalFrontendReady } from "@/lib/app-startup";

export default function App() {
  const [contentVisible, setContentVisible] = useState(false);

  useEffect(() => {
    applyWindowsMicaStyling();

    let revealFrame = 0;
    let readyFrame = 0;

    revealFrame = window.requestAnimationFrame(() => {
      setContentVisible(true);
      readyFrame = window.requestAnimationFrame(() => {
        void signalFrontendReady();
      });
    });

    return () => {
      if (revealFrame) {
        window.cancelAnimationFrame(revealFrame);
      }
      if (readyFrame) {
        window.cancelAnimationFrame(readyFrame);
      }
    };
  }, []);

  return (
    <ThemeProvider>
      <QueryClientProvider client={queryClient}>
        <div
          className={[
            "h-full transition-[opacity,transform] duration-500 ease-out motion-reduce:transition-none",
            contentVisible ? "opacity-100 translate-y-0" : "opacity-0 translate-y-1",
          ].join(" ")}
        >
          <AppShell />
        </div>
        <Toaster richColors={true} />
      </QueryClientProvider>
    </ThemeProvider>
  );
}
