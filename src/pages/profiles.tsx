import { useState, useEffect, useRef, useCallback } from "react";
import { useProfileStore } from "@/stores/profileStore";
import { useKeyboardStore } from "@/stores/keyboard-store";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Badge } from "@/components/ui/badge";
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuItem,
  DropdownMenuSeparator,
  DropdownMenuTrigger,
} from "@/components/ui/dropdown-menu";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog";
import {
  IconPlus,
  IconTrash,
  IconCopy,
  IconCheck,
  IconDownload,
  IconUpload,
  IconDotsVertical,
  IconRefresh,
  IconPencil,
} from "@tabler/icons-react";
import { toast } from "sonner";
import { PageContent } from "@/components/shared/PageLayout";

export default function Profiles() {
  const profiles = useProfileStore((s) => s.profiles);
  const selectedProfile = useProfileStore((s) => s.selectedProfile);
  const save = useProfileStore((s) => s.save);
  const remove = useProfileStore((s) => s.remove);
  const rename = useProfileStore((s) => s.rename);
  const duplicate = useProfileStore((s) => s.duplicate);
  const selectProfile = useProfileStore((s) => s.selectProfile);
  const init = useProfileStore((s) => s.init);

  const [deleteTarget, setDeleteTarget] = useState<string | null>(null);
  const [copyFrom, setCopyFrom] = useState<string | null>(null);
  const [copyName, setCopyName] = useState("");
  const [renameTarget, setRenameTarget] = useState<string | null>(null);
  const [renameTo, setRenameTo] = useState("");
  const [createOpen, setCreateOpen] = useState(false);
  const [newName, setNewName] = useState("");

  const importRef = useRef<HTMLInputElement>(null);
  const importTargetRef = useRef<string | null>(null);

  useEffect(() => {
    init();
    useKeyboardStore.getState().setSaveEnabled(true);
    return () => { useKeyboardStore.getState().setSaveEnabled(false); };
  }, [init]);

  const handleCreate = () => {
    const name = newName.trim();
    if (!name || profiles.find((p) => p.name === name)) return;
    save(name);
    setNewName("");
    setCreateOpen(false);
    toast.success(`Profile "${name}" created`);
  };

  const handleExport = (profileName: string) => {
    const profile = profiles.find((p) => p.name === profileName);
    if (!profile) return;
    const blob = new Blob([JSON.stringify(profile, null, 2)], { type: "application/json" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = `${profileName}.json`;
    a.click();
    URL.revokeObjectURL(url);
    toast.success(`Exported "${profileName}"`);
  };

  const handleImportClick = (profileName: string) => {
    importTargetRef.current = profileName;
    importRef.current?.click();
  };

  const handleImportFile = useCallback((e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    const target = importTargetRef.current;
    if (!file || !target) return;

    const reader = new FileReader();
    reader.onload = () => {
      try {
        const json = JSON.parse(reader.result as string);
        if (json.data) {
          localStorage.setItem(`keyboard-profile:${target}`, JSON.stringify(json.data));
        } else {
          localStorage.setItem(`keyboard-profile:${target}`, JSON.stringify(json));
        }
        useProfileStore.getState().refresh();
        toast.success(`Imported into "${target}"`);
      } catch {
        toast.error("Invalid JSON file");
      }
    };
    reader.readAsText(file);
    e.target.value = "";
  }, []);

  return (
    <div className="flex flex-col h-full overflow-hidden">
      <PageContent>
        <div className="grid grid-cols-2 gap-4">
            {profiles.map((profile) => {
              const isActive = selectedProfile?.name === profile.name;
              return (
                <div
                  key={profile.name}
                  className="group rounded-lg border bg-card p-4 shadow-sm flex flex-col gap-3 transition-colors hover:border-primary/30"
                >
                  <div className="flex items-start justify-between">
                    <div className="flex flex-col gap-0.5 min-w-0">
                      <span className="text-sm font-medium truncate">{profile.name}</span>
                      <span className="text-xs text-muted-foreground">
                        {profile.data ? "Custom layout" : "Default layout"}
                      </span>
                    </div>
                    <div className="flex items-center gap-1.5 shrink-0">
                      {isActive && (
                        <Badge className="gap-1 text-[10px]">
                          <IconCheck className="size-3" />
                          Active
                        </Badge>
                      )}
                      <DropdownMenu>
                        <DropdownMenuTrigger render={
                          <Button variant="ghost" size="icon" className="size-7 transition-opacity">
                            <IconDotsVertical className="size-4" />
                            <span className="sr-only">Profile actions</span>
                          </Button>
                        } />
                        <DropdownMenuContent align="end">
                          {!isActive && (
                            <DropdownMenuItem onClick={() => selectProfile(profile.name)}>
                              <IconCheck className="size-4" />
                              Load
                            </DropdownMenuItem>
                          )}
                          <DropdownMenuItem onClick={() => {
                            setCopyFrom(profile.name);
                            setCopyName(`${profile.name} copy`);
                          }}>
                            <IconCopy className="size-4" />
                            Duplicate
                          </DropdownMenuItem>
                          {profile.name !== "default" && (
                            <DropdownMenuItem onClick={() => {
                              setRenameTarget(profile.name);
                              setRenameTo(profile.name);
                            }}>
                              <IconPencil className="size-4" />
                              Rename
                            </DropdownMenuItem>
                          )}
                          <DropdownMenuSeparator />
                          <DropdownMenuItem onClick={() => handleExport(profile.name)}>
                            <IconDownload className="size-4" />
                            Export JSON
                          </DropdownMenuItem>
                          <DropdownMenuItem onClick={() => handleImportClick(profile.name)}>
                            <IconUpload className="size-4" />
                            Import JSON
                          </DropdownMenuItem>
                          {profile.name !== "default" && (
                            <>
                              <DropdownMenuSeparator />
                              <DropdownMenuItem
                                variant="destructive"
                                onClick={() => setDeleteTarget(profile.name)}
                              >
                                <IconTrash className="size-4" />
                                Delete
                              </DropdownMenuItem>
                            </>
                          )}
                        </DropdownMenuContent>
                      </DropdownMenu>
                    </div>
                  </div>
                  {!isActive && (
                    <Button
                      variant="outline"
                      size="sm"
                      className="w-full h-7 text-xs gap-1"
                      onClick={() => selectProfile(profile.name)}
                    >
                      <IconRefresh className="size-3" />
                      Load Profile
                    </Button>
                  )}
                </div>
              );
            })}

            <button
              type="button"
              onClick={() => { setNewName(""); setCreateOpen(true); }}
              className="rounded-lg border-2 border-dashed border-muted-foreground/25 p-4 flex flex-col items-center justify-center gap-2 text-muted-foreground hover:border-primary/40 hover:text-foreground transition-colors min-h-22 cursor-pointer"
            >
              <IconPlus className="size-5" />
              <span className="text-xs font-medium">New Profile</span>
            </button>
          </div>

          {profiles.length === 0 && (
            <div className="text-center py-12 text-muted-foreground text-sm">
              No profiles yet. Create one to get started.
            </div>
          )}
      </PageContent>

      <input
        ref={importRef}
        type="file"
        accept="application/json"
        onChange={handleImportFile}
        className="hidden"
        aria-hidden="true"
      />

      <Dialog open={createOpen} onOpenChange={setCreateOpen}>
        <DialogContent>
          <DialogHeader>
            <DialogTitle>Create Profile</DialogTitle>
            <DialogDescription>
              Give your new profile a name. It will start with the default layout.
            </DialogDescription>
          </DialogHeader>
          <Input
            placeholder="Profile name…"
            value={newName}
            onChange={(e) => setNewName(e.target.value)}
            onKeyDown={(e) => e.key === "Enter" && handleCreate()}
            autoFocus
          />
          <DialogFooter>
            <Button variant="outline" onClick={() => setCreateOpen(false)}>Cancel</Button>
            <Button
              onClick={handleCreate}
              disabled={!newName.trim() || !!profiles.find((p) => p.name === newName.trim())}
            >
              <IconPlus className="size-4 mr-1" />
              Create
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>

      <Dialog open={!!deleteTarget} onOpenChange={() => setDeleteTarget(null)}>
        <DialogContent>
          <DialogHeader>
            <DialogTitle>Delete profile?</DialogTitle>
            <DialogDescription>
              This will permanently delete &ldquo;{deleteTarget}&rdquo;. This cannot be undone.
            </DialogDescription>
          </DialogHeader>
          <DialogFooter>
            <Button variant="outline" onClick={() => setDeleteTarget(null)}>Cancel</Button>
            <Button variant="destructive" onClick={() => {
              if (deleteTarget) {
                remove(deleteTarget);
                toast.success(`Deleted "${deleteTarget}"`);
              }
              setDeleteTarget(null);
            }}>
              Delete
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>

      <Dialog open={!!copyFrom} onOpenChange={() => setCopyFrom(null)}>
        <DialogContent>
          <DialogHeader>
            <DialogTitle>Duplicate &ldquo;{copyFrom}&rdquo;</DialogTitle>
          </DialogHeader>
          <Input
            placeholder="New profile name…"
            value={copyName}
            onChange={(e) => setCopyName(e.target.value)}
            onKeyDown={(e) => {
              if (e.key === "Enter" && copyFrom && copyName.trim()) {
                duplicate(copyFrom, copyName.trim());
                toast.success(`Duplicated to "${copyName.trim()}"`);
                setCopyFrom(null);
              }
            }}
            autoFocus
          />
          <DialogFooter>
            <Button variant="outline" onClick={() => setCopyFrom(null)}>Cancel</Button>
            <Button
              onClick={() => {
                if (copyFrom && copyName.trim()) {
                  duplicate(copyFrom, copyName.trim());
                  toast.success(`Duplicated to "${copyName.trim()}"`);
                  setCopyFrom(null);
                }
              }}
              disabled={!copyName.trim() || !!profiles.find((p) => p.name === copyName.trim())}
            >
              <IconCopy className="size-4 mr-1" />
              Duplicate
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>

      <Dialog open={!!renameTarget} onOpenChange={() => setRenameTarget(null)}>
        <DialogContent>
          <DialogHeader>
            <DialogTitle>Rename &ldquo;{renameTarget}&rdquo;</DialogTitle>
            <DialogDescription>
              Enter a new name for this profile.
            </DialogDescription>
          </DialogHeader>
          <Input
            placeholder="New name…"
            value={renameTo}
            onChange={(e) => setRenameTo(e.target.value)}
            onKeyDown={(e) => {
              if (e.key === "Enter" && renameTarget && renameTo.trim() && renameTo.trim() !== renameTarget && !profiles.find((p) => p.name === renameTo.trim())) {
                rename(renameTarget, renameTo.trim());
                toast.success(`Renamed to "${renameTo.trim()}"`);
                setRenameTarget(null);
              }
            }}
            autoFocus
          />
          <DialogFooter>
            <Button variant="outline" onClick={() => setRenameTarget(null)}>Cancel</Button>
            <Button
              onClick={() => {
                if (renameTarget && renameTo.trim()) {
                  rename(renameTarget, renameTo.trim());
                  toast.success(`Renamed to "${renameTo.trim()}"`);
                  setRenameTarget(null);
                }
              }}
              disabled={
                !renameTo.trim() ||
                renameTo.trim() === renameTarget ||
                !!profiles.find((p) => p.name === renameTo.trim())
              }
            >
              <IconPencil className="size-4 mr-1" />
              Rename
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    </div>
  );
}
