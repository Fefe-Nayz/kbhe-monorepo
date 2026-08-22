import { describe, expect, test } from "bun:test";
import { QueryClient } from "@tanstack/react-query";

import {
  OptimisticMutationLedger,
  OptimisticMutationScopeRegistry,
  optimisticMutationScopeId,
  rollbackOptimisticQuery,
} from "./use-optimistic-mutation";

describe("optimistic mutation concurrency helpers", () => {
  test("uses one stable serial scope for the same query key", () => {
    expect(optimisticMutationScopeId(["rotary", "settings", 2]))
      .toBe(optimisticMutationScopeId(["rotary", "settings", 2]));
    expect(optimisticMutationScopeId(["rotary", "settings", 2]))
      .not.toBe(optimisticMutationScopeId(["rotary", "settings", 3]));
  });

  test("removes an optimistic value when the query did not previously exist", () => {
    const client = new QueryClient();
    const key = ["led", "brightness"] as const;
    client.setQueryData(key, 200);

    rollbackOptimisticQuery(client, key, undefined);

    expect(client.getQueryData(key)).toBeUndefined();
    expect(client.getQueryState(key)).toBeUndefined();
  });

  test("restores the exact previous cached value", () => {
    const client = new QueryClient();
    const key = ["rotary", "settings"] as const;
    client.setQueryData(key, { sensitivity: 16 });

    rollbackOptimisticQuery(client, key, { sensitivity: 4 });

    expect(client.getQueryData(key)).toEqual({ sensitivity: 4 });
  });

  test("rolls two failed queued mutations back to the last confirmed value", () => {
    const ledger = new OptimisticMutationLedger<number>();
    const first = ledger.issue();
    const second = ledger.issue();
    ledger.initializeConfirmed(10);

    expect(ledger.rollbackValue(first.sequence)).toEqual({
      shouldRollback: false,
      value: 10,
    });
    expect(ledger.rollbackValue(second.sequence)).toEqual({
      shouldRollback: true,
      value: 10,
    });
  });

  test("preserves an earlier confirmed success when the latest queued write fails", () => {
    const ledger = new OptimisticMutationLedger<number>();
    const first = ledger.issue();
    const second = ledger.issue();
    ledger.initializeConfirmed(10);
    ledger.confirm(20);

    expect(ledger.rollbackValue(first.sequence).shouldRollback).toBe(false);
    expect(ledger.rollbackValue(second.sequence)).toEqual({
      shouldRollback: true,
      value: 20,
    });
    expect(ledger.settle()).toBe(false);
    expect(ledger.settle()).toBe(true);
  });

  test("isolates rollback state when one mounted editor changes query keys", () => {
    const registry = new OptimisticMutationScopeRegistry<number>();
    const firstKey = ["effect-params", 1] as const;
    const secondKey = ["effect-params", 2] as const;
    const first = registry.forQuery(firstKey);
    const second = registry.forQuery(secondKey);

    first.ledger.issue();
    first.ledger.initializeConfirmed(10);
    first.ledger.confirm(11);
    const secondMutation = second.ledger.issue();
    second.ledger.initializeConfirmed(20);

    expect(registry.forQuery(firstKey)).toBe(first);
    expect(second.ledger.rollbackValue(secondMutation.sequence)).toEqual({
      shouldRollback: true,
      value: 20,
    });
  });
});
