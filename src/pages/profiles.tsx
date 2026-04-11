import { useState, useEffect } from "react";
import { useProfileStore } from "@/stores/profileStore";
import { useKeyboardStore } from "@/stores/keyboard-store";
import { PageLayout, PageHeader } from "@/components/shared/PageLayout";
import { SectionCard } from "@/components/shared/SectionCard";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Badge } from "@/components/ui/badge";
import { Separator } from "@/components/ui/separator";
import {
  AlertDialog,
  AlertDialogAction,
  AlertDialogCancel,
  AlertDialogContent,
  AlertDialogDescription,
  AlertDialogFooter,
  AlertDialogHeader,
  AlertDialogTitle,
  AlertDialogTrigger,
} from "@/components/ui/alert-dialog";
import { IconPlus, IconTrash, IconCopy, IconCheck } from "@tabler/icons-react";

export default function Profiles() {
  const profiles = useProfileStore((s) => s.profiles);
  const selectedProfile = useProfileStore((s) => s.selectedProfile);
  const save = useProfileStore((s) => s.save);
  const remove = useProfileStore((s) => s.remove);
  const duplicate = useProfileStore((s) => s.duplicate);
  const selectProfile = useProfileStore((s) => s.selectProfile);
  const init = useProfileStore((s) => s.init);

  const [newName, setNewName] = useState("");
  const [copyFrom, setCopyFrom] = useState<string | null>(null);
  const [copyName, setCopyName] = useState("");

  useEffect(() => {
    init();
    useKeyboardStore.getState().setSaveEnabled(true);
    return () => {
      useKeyboardStore.getState().setSaveEnabled(false);
    };
  }, [init]);

  const handleCreate = () => {
    const name = newName.trim();
    if (!name || profiles.find((p) => p.name === name)) return;
    save(name);
    setNewName("");
  };

  const handleDuplicate = () => {
    if (!copyFrom) return;
    const name = copyName.trim();
    if (!name || profiles.find((p) => p.name === name)) return;
    duplicate(copyFrom, name);
    setCopyFrom(null);
    setCopyName("");
  };

  return (
    <PageLayout>
      <PageHeader
        title="Profiles"
        description="Manage local keyboard configuration snapshots"
      />

      <SectionCard
        title="Saved Profiles"
        description="Profiles are stored locally. They snapshot your keymap layout."
      >
        <div className="flex flex-col gap-1">
          {profiles.map((profile, i) => {
            const isActive = selectedProfile?.name === profile.name;
            return (
              <div key={profile.name}>
                {i > 0 && <Separator className="my-1" />}
                <div className="flex items-center gap-3 py-1.5">
                  <div className="flex-1 min-w-0">
                    <span className="text-sm font-medium truncate block">
                      {profile.name}
                    </span>
                  </div>
                  {isActive && (
                    <Badge className="shrink-0">
                      <IconCheck className="size-3 mr-1" />
                      Active
                    </Badge>
                  )}
                  <div className="flex items-center gap-1 shrink-0">
                    {!isActive && (
                      <Button
                        variant="ghost"
                        size="sm"
                        className="h-7 px-2 text-xs"
                        onClick={() => selectProfile(profile.name)}
                      >
                        Load
                      </Button>
                    )}
                    <Button
                      variant="ghost"
                      size="sm"
                      className="h-7 px-2"
                      onClick={() => {
                        setCopyFrom(profile.name);
                        setCopyName(`${profile.name} copy`);
                      }}
                    >
                      <IconCopy className="size-3.5" />
                    </Button>
                    {profile.name !== "default" && (
                      <AlertDialog>
                        <AlertDialogTrigger
                          render={
                            <Button
                              variant="ghost"
                              size="sm"
                              className="h-7 px-2 text-destructive hover:text-destructive"
                            >
                              <IconTrash className="size-3.5" />
                            </Button>
                          }
                        />
                        <AlertDialogContent>
                          <AlertDialogHeader>
                            <AlertDialogTitle>Delete profile?</AlertDialogTitle>
                            <AlertDialogDescription>
                              This will permanently delete &ldquo;{profile.name}&rdquo;. This cannot be undone.
                            </AlertDialogDescription>
                          </AlertDialogHeader>
                          <AlertDialogFooter>
                            <AlertDialogCancel>Cancel</AlertDialogCancel>
                            <AlertDialogAction
                              className="bg-destructive text-destructive-foreground"
                              onClick={() => remove(profile.name)}
                            >
                              Delete
                            </AlertDialogAction>
                          </AlertDialogFooter>
                        </AlertDialogContent>
                      </AlertDialog>
                    )}
                  </div>
                </div>
              </div>
            );
          })}
          {profiles.length === 0 && (
            <p className="text-sm text-muted-foreground py-2">
              No profiles yet. Create one below.
            </p>
          )}
        </div>
      </SectionCard>

      <SectionCard
        title="New Profile"
        description="Creates a blank profile with the default layout."
      >
        <div className="flex gap-2">
          <Input
            placeholder="Profile name…"
            value={newName}
            onChange={(e) => setNewName(e.target.value)}
            onKeyDown={(e) => e.key === "Enter" && handleCreate()}
            className="flex-1"
          />
          <Button
            onClick={handleCreate}
            disabled={
              !newName.trim() ||
              !!profiles.find((p) => p.name === newName.trim())
            }
          >
            <IconPlus className="size-4 mr-1" />
            Create
          </Button>
        </div>
        {newName.trim() && profiles.find((p) => p.name === newName.trim()) && (
          <p className="text-xs text-destructive mt-2">
            A profile with this name already exists.
          </p>
        )}
      </SectionCard>

      {copyFrom && (
        <SectionCard
          title={`Duplicate "${copyFrom}"`}
          headerRight={
            <Button
              variant="ghost"
              size="sm"
              className="h-7"
              onClick={() => setCopyFrom(null)}
            >
              Cancel
            </Button>
          }
        >
          <div className="flex gap-2">
            <Input
              placeholder="New profile name…"
              value={copyName}
              onChange={(e) => setCopyName(e.target.value)}
              onKeyDown={(e) => e.key === "Enter" && handleDuplicate()}
              className="flex-1"
            />
            <Button
              onClick={handleDuplicate}
              disabled={
                !copyName.trim() ||
                !!profiles.find((p) => p.name === copyName.trim())
              }
            >
              <IconCopy className="size-4 mr-1" />
              Duplicate
            </Button>
          </div>
        </SectionCard>
      )}
    </PageLayout>
  );
}
