import { QueryClient } from "@tanstack/react-query";

export const queryClient = new QueryClient({
  defaultOptions: {
    queries: {
      // Don't refetch on window focus — device data is polled on demand
      refetchOnWindowFocus: false,
      // Don't retry automatically — device errors should surface immediately
      retry: 1,
      retryDelay: 500,
      staleTime: 2_000,
    },
    mutations: {
      retry: 1,
      retryDelay: 300,
    },
  },
});
