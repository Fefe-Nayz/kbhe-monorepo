import { useEffect } from "react";
import { useProfileStore } from "@/stores/profileStore";
import {
  Select,
  SelectContent,
  SelectGroup,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import { IconLayoutGrid } from "@tabler/icons-react";

export function ProfileSelect() {
  const profiles = useProfileStore((s) => s.profiles);
  const selectedProfile = useProfileStore((s) => s.selectedProfile);
  const selectProfile = useProfileStore((s) => s.selectProfile);
  const init = useProfileStore((s) => s.init);

  useEffect(() => {
    init();
  }, [init]);

  const items = profiles.map((p) => ({ value: p.name, label: p.name }));

  return (
    <Select
      value={selectedProfile?.name ?? ""}
      items={items}
      onValueChange={(v) => selectProfile(v as string)}
    >
      <SelectTrigger size="sm" className="gap-1.5 text-xs font-medium">
        <IconLayoutGrid className="size-3.5 text-muted-foreground" />
        <SelectValue />
      </SelectTrigger>
      <SelectContent>
        <SelectGroup>
          {profiles.map((p) => (
            <SelectItem key={p.name} value={p.name}>
              {p.name}
            </SelectItem>
          ))}
        </SelectGroup>
      </SelectContent>
    </Select>
  );
}
