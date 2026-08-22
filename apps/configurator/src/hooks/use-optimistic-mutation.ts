import {
  type QueryClient,
  useMutation,
  useQueryClient,
  type QueryKey,
  type UseMutationOptions,
} from "@tanstack/react-query";
import { useState } from "react";

interface OptimisticMutationContext<TData, TVars> {
  prev: TData | undefined;
  sequence: number;
  queryKey: QueryKey;
  scopeState: OptimisticMutationScopeState<TData>;
  optimisticUpdate: (current: TData | undefined, vars: TVars) => TData;
}

export interface OptimisticMutationScopeState<TData> {
  ledger: OptimisticMutationLedger<TData>;
  applyQueue: { tail: Promise<void> };
}

export class OptimisticMutationLedger<TData> {
  private nextSequence = 0;
  private latestSequence = 0;
  private pending = 0;
  private confirmed: TData | undefined;

  issue(): { sequence: number; startsBatch: boolean } {
    const startsBatch = this.pending === 0;
    const sequence = ++this.nextSequence;
    this.latestSequence = sequence;
    this.pending += 1;
    return { sequence, startsBatch };
  }

  initializeConfirmed(value: TData | undefined): void {
    this.confirmed = value;
  }

  confirm(value: TData): void {
    this.confirmed = value;
  }

  rollbackValue(sequence: number): { shouldRollback: boolean; value: TData | undefined } {
    return {
      shouldRollback: sequence === this.latestSequence,
      value: this.confirmed,
    };
  }

  settle(): boolean {
    this.pending = Math.max(0, this.pending - 1);
    return this.pending === 0;
  }

  confirmedValue(): TData | undefined {
    return this.confirmed;
  }
}

/** Keep rollback bases and optimistic-write queues isolated when a mounted
 * editor changes key/layer/profile while an older mutation is still settling. */
export class OptimisticMutationScopeRegistry<TData> {
  private readonly scopes = new Map<string, OptimisticMutationScopeState<TData>>();

  forQuery(queryKey: QueryKey): OptimisticMutationScopeState<TData> {
    const id = optimisticMutationScopeId(queryKey);
    let state = this.scopes.get(id);
    if (!state) {
      state = {
        ledger: new OptimisticMutationLedger<TData>(),
        applyQueue: { tail: Promise.resolve() },
      };
      this.scopes.set(id, state);
    }
    return state;
  }
}

export function optimisticMutationScopeId(queryKey: QueryKey): string {
  return `kbhe:${JSON.stringify(queryKey)}`;
}

export function rollbackOptimisticQuery<TData>(
  queryClient: QueryClient,
  queryKey: QueryKey,
  previous: TData | undefined,
): void {
  if (previous === undefined) {
    queryClient.removeQueries({ queryKey, exact: true });
    return;
  }
  queryClient.setQueryData(queryKey, previous);
}

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
  onError?: UseMutationOptions<
    TResult,
    Error,
    TVars,
    OptimisticMutationContext<TData, TVars>
  >["onError"];
}

export function useOptimisticMutation<TData, TVars, TResult = void>({
  queryKey,
  mutationFn,
  optimisticUpdate,
  onSuccess,
  onError: onErrorProp,
}: UseOptimisticMutationOptions<TData, TVars, TResult>) {
  const qc = useQueryClient();
  const [scopeRegistry] = useState(() => new OptimisticMutationScopeRegistry<TData>());
  const scopeState = scopeRegistry.forQuery(queryKey);
  const scopeId = optimisticMutationScopeId(queryKey);

  return useMutation<TResult, Error, TVars, OptimisticMutationContext<TData, TVars>>({
    // Mutations targeting the same device field must execute in issue order.
    // This keeps a late failure from rolling the cache behind a newer success.
    scope: { id: scopeId },
    mutationFn: async (vars) => {
      const result = await mutationFn(vars);
      if (result === false) {
        throw new Error("The device rejected the requested change");
      }
      return result;
    },
    onMutate: async (vars) => {
      const mutationQueryKey = queryKey;
      const mutationScopeState = scopeState;
      const mutationOptimisticUpdate = optimisticUpdate;
      const { ledger, applyQueue } = mutationScopeState;
      const { sequence, startsBatch } = ledger.issue();
      let prev: TData | undefined;

      // A TanStack serial scope orders mutationFn calls, but all onMutate
      // callbacks run immediately. Order the optimistic cache writes too and
      // retain one confirmed rollback base for the whole pending batch.
      const apply = applyQueue.tail.then(async () => {
        await qc.cancelQueries({ queryKey: mutationQueryKey });
        if (startsBatch) {
          ledger.initializeConfirmed(qc.getQueryData<TData>(mutationQueryKey));
        }
        prev = qc.getQueryData<TData>(mutationQueryKey);
        qc.setQueryData<TData>(mutationQueryKey, (old) => mutationOptimisticUpdate(old, vars));
      });
      applyQueue.tail = apply.catch(() => undefined);
      try {
        await apply;
      } catch (error) {
        ledger.settle();
        throw error;
      }
      return {
        prev,
        sequence,
        queryKey: mutationQueryKey,
        scopeState: mutationScopeState,
        optimisticUpdate: mutationOptimisticUpdate,
      };
    },
    onError: (err, vars, ctx, mutationContext) => {
      if (ctx) {
        const rollback = ctx.scopeState.ledger.rollbackValue(ctx.sequence);
        if (rollback.shouldRollback) {
          rollbackOptimisticQuery(qc, ctx.queryKey, rollback.value);
        }
      }
      onErrorProp?.(err, vars, ctx, mutationContext);
    },
    onSettled: (_result, _error, _vars, ctx) => {
      if (ctx && ctx.scopeState.ledger.settle()) {
        ctx.scopeState.applyQueue.tail = Promise.resolve();
        void qc.invalidateQueries({ queryKey: ctx.queryKey });
      }
    },
    onSuccess: (result, vars, context, mutationContext) => {
      if (context) {
        const ledger = context.scopeState.ledger;
        ledger.confirm(context.optimisticUpdate(ledger.confirmedValue(), vars));
      }
      return onSuccess?.(result, vars, context, mutationContext);
    },
  });
}
