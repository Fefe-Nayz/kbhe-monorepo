import {
  useMutation,
  useQueryClient,
  type QueryKey,
  type UseMutationOptions,
} from "@tanstack/react-query";

/**
 * TData   = type stored in the query cache (what getQueryData / setQueryData operate on)
 * TVars   = variables passed to mutate()
 * TResult = return type of mutationFn (often boolean / void — unrelated to cache type)
 */
interface UseOptimisticMutationOptions<TData, TVars, TResult = void> {
  queryKey: QueryKey;
  mutationFn: (vars: TVars) => Promise<TResult>;
  /** Build the optimistic cache value from the current cache + new vars. */
  optimisticUpdate: (current: TData | undefined, vars: TVars) => TData;
  onSuccess?: UseMutationOptions<TResult, Error, TVars>["onSuccess"];
  onError?: UseMutationOptions<TResult, Error, TVars, { prev: TData | undefined }>["onError"];
}

export function useOptimisticMutation<TData, TVars, TResult = void>({
  queryKey,
  mutationFn,
  optimisticUpdate,
  onSuccess,
  onError: onErrorProp,
}: UseOptimisticMutationOptions<TData, TVars, TResult>) {
  const qc = useQueryClient();

  return useMutation<TResult, Error, TVars, { prev: TData | undefined }>({
    mutationFn,
    onMutate: async (vars) => {
      await qc.cancelQueries({ queryKey });
      const prev = qc.getQueryData<TData>(queryKey);
      qc.setQueryData<TData>(queryKey, (old) => optimisticUpdate(old, vars));
      return { prev };
    },
    onError: (err, vars, ctx) => {
      if (ctx?.prev !== undefined) qc.setQueryData(queryKey, ctx.prev);
      onErrorProp?.(err, vars, ctx);
    },
    onSettled: () => {
      void qc.invalidateQueries({ queryKey });
    },
    onSuccess,
  });
}
