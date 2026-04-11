import { type ReactNode } from "react";
import { cn } from "@/lib/utils";

interface KeyboardEditorProps {
  keyboard: ReactNode;
  menubar?: ReactNode;
  children: ReactNode;
  className?: string;
}

export function KeyboardEditor({ keyboard, menubar, children, className }: KeyboardEditorProps) {
  return (
    <div className={cn("flex h-full flex-col overflow-hidden", className)}>
      <div className="shrink-0 p-4 pb-2">
        {keyboard}
      </div>
      {menubar && (
        <KeyboardEditorMenubar>{menubar}</KeyboardEditorMenubar>
      )}
      <div className="flex-1 min-h-0 overflow-y-auto">
        <KeyboardEditorContainer>
          {children}
        </KeyboardEditorContainer>
      </div>
    </div>
  );
}

function KeyboardEditorMenubar({ children }: { children: ReactNode }) {
  return (
    <header className="flex h-12 shrink-0 items-center border-y">
      <div className="mx-auto flex w-full max-w-7xl items-center justify-between gap-4 px-4">
        {children}
      </div>
    </header>
  );
}

function KeyboardEditorContainer({ children }: { children: ReactNode }) {
  return (
    <div className="mx-auto w-full max-w-7xl p-4">
      {children}
    </div>
  );
}

export { KeyboardEditorMenubar, KeyboardEditorContainer };
