import { QueryClientProvider } from "@tanstack/react-query";
import { queryClient } from "@/lib/query/queryClient";
import { ThemeProvider } from "@/components/theme-provider";
import { AppShell } from "@/components/AppShell";
import { Toaster } from "@/components/ui/sonner";

export default function App() {
  return (
    <ThemeProvider>
      <QueryClientProvider client={queryClient}>
        <AppShell />
        <Toaster richColors={true} />
      </QueryClientProvider>
    </ThemeProvider>
  );
}
