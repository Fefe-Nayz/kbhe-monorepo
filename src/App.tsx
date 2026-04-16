import { QueryClientProvider } from "@tanstack/react-query";
import { useEffect } from "react";
import { queryClient } from "@/lib/query/queryClient";
import { ThemeProvider } from "@/components/theme-provider";
import { AppShell } from "@/components/AppShell";
import { Toaster } from "@/components/ui/sonner";
import { applyWindowsMicaStyling, signalFrontendReady } from "@/lib/app-startup";

export default function App() {
  useEffect(() => {
    applyWindowsMicaStyling();
    void signalFrontendReady();
  }, []);

  return (
    <ThemeProvider>
      <QueryClientProvider client={queryClient}>
        <AppShell />
        <Toaster richColors={true} />
      </QueryClientProvider>
    </ThemeProvider>
  );
}
