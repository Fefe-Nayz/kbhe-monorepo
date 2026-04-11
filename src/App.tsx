import { QueryClientProvider } from "@tanstack/react-query";
import { queryClient } from "@/lib/query/queryClient";
import { AppShell } from "@/components/AppShell";

export default function App() {
  return (
    <QueryClientProvider client={queryClient}>
      <AppShell />
    </QueryClientProvider>
  );
}
