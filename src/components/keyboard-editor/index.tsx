import { type ReactNode } from "react";
import { cn } from "@/lib/utils";
import { ScrollArea } from "@/components/ui/scroll-area";
import {
  ResizableHandle,
  ResizablePanel,
  ResizablePanelGroup,
} from "@/components/ui/resizable";

const PREVIEW_DEFAULT_SIZE = "40%";
const PREVIEW_MIN_SIZE = "16%";
const PREVIEW_MAX_SIZE = "70%";
const SETTINGS_DEFAULT_SIZE = "60%";
const SETTINGS_MIN_SIZE = "30%";

interface KeyboardEditorProps {
  keyboard: ReactNode;
  menubar?: ReactNode;
  children: ReactNode;
  className?: string;
}

export function KeyboardEditor({ keyboard, menubar, children, className }: KeyboardEditorProps) {
  return (
    <div className={cn("flex h-full w-full min-w-0 flex-col overflow-hidden", className)}>
      <ResizablePanelGroup
        id="keyboard-editor-panel-group"
        orientation="vertical"
        resizeTargetMinimumSize={{ fine: 14, coarse: 28 }}
        className="min-h-0"
      >
        <ResizablePanel
          id="keyboard-editor-preview-panel"
          defaultSize={PREVIEW_DEFAULT_SIZE}
          minSize={PREVIEW_MIN_SIZE}
          maxSize={PREVIEW_MAX_SIZE}
          className="min-h-0 overflow-hidden"
        >
          <div className="flex h-full min-h-0 w-full flex-col overflow-hidden overscroll-none">
            <div className="h-full min-h-0 w-full min-w-0 p-4 pb-2">
              {keyboard}
            </div>
            {menubar && (
              <KeyboardEditorMenubar>{menubar}</KeyboardEditorMenubar>
            )}
          </div>
        </ResizablePanel>

        <ResizableHandle
          id="keyboard-editor-resize-handle"
          withHandle
        />

        <ResizablePanel
          id="keyboard-editor-settings-panel"
          defaultSize={SETTINGS_DEFAULT_SIZE}
          minSize={SETTINGS_MIN_SIZE}
          className="min-h-0"
        >
          <div className="flex h-full min-h-0 min-w-0 flex-1 flex-col">
            <ScrollArea className="h-full w-full min-h-0 min-w-0">
              <KeyboardEditorContainer>
                {children}
              </KeyboardEditorContainer>
            </ScrollArea>
          </div>
        </ResizablePanel>
      </ResizablePanelGroup>
    </div>
  );
}

function KeyboardEditorMenubar({ children }: { children: ReactNode }) {
  return (
    <header className="flex h-12 w-full min-w-0 shrink-0 items-center border-b">
      <div className="mx-auto flex w-full min-w-0 max-w-7xl items-center justify-between gap-4 px-4">
        {children}
      </div>
    </header>
  );
}

function KeyboardEditorContainer({ children }: { children: ReactNode }) {
  return (
    <div className="mx-auto w-full min-w-0 max-w-7xl p-4">
      {children}
    </div>
  );
}

export { KeyboardEditorMenubar, KeyboardEditorContainer };
