import type { RuntimeSource } from "@/stores/profileStore";

/**
 * Runtime profiles can reuse the same firmware slot. Keep their action-query
 * caches distinct or switching between two temporary app profiles can show
 * and edit the previous profile's macros until a manual refresh.
 */
export function actionProfileCacheScope(
  runtimeSource: RuntimeSource,
  profileIndex: number,
  activeAppProfileName: string | null,
): string {
  return runtimeSource === "app"
    ? `app:${activeAppProfileName ?? "unknown"}`
    : `device:${profileIndex}`;
}
