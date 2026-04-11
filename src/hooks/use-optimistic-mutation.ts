import {
  useMutation,
  useQueryClient,
  type QueryKey,
  type UseMutationOptions,
} from "@tanstack/react-query";

interface UseOptimisticMutationOptions<TData, TVars> {
  queryKey: QueryKey;
  mutationFn: (vars: TVars) => Promise<TData>;
  /** Build the optimistic cache value from the current cache + new vars. */
  optimisticUpdate: (current: TData | undefined, vars: TVars) => TData;
  onSuccess?: UseMutationOptions<TData, Error, TVars>["onSuccess"];
}

/**
 * Wraps useMutation with TanStack Query's optimistic-update pattern:
 * instantly patches the query cache, rolls back on error, and
 * revalidates on settle.
 */
export function useOptimisticMutation<TData, TVars>({
  queryKey,
  mutationFn,
  optimisticUpdate,
  onSuccess,
}: UseOptimisticMutationOptions<TData, TVars>) {
  const qc = useQueryClient();

  return useMutation<TData, Error, TVars, { prev: TData | undefined }>({
    mutationFn,
    onMutate: async (vars) => {
      await qc.cancelQueries({ queryKey });
      const prev = qc.getQueryData<TData>(queryKey);
      qc.setQueryData<TData>(queryKey, (old) => optimisticUpdate(old, vars));
      return { prev };
    },
    onError: (_err, _vars, ctx) => {
      if (ctx?.prev !== undefined) qc.setQueryData(queryKey, ctx.prev);
    },
    onSettled: () => {
      void qc.invalidateQueries({ queryKey });
    },
    onSuccess,
  });
}
