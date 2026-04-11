/**
 * KeyboardPreviewCard — shadcn Card wrapper around the existing KLE keyboard preview.
 * Used by all pages that show the keyboard layout.
 */

import { type ReactNode } from "react";
import BaseKeyboard from "@/components/baseKeyboard";
import { Card, CardContent } from "@/components/ui/card";
import { cn } from "@/lib/utils";

interface KeyboardPreviewCardProps {
  /** "single" or "multi" selection mode */
  mode?: "single" | "multi";
  /** Called when key(s) are clicked */
  onKeyClick?: (ids: string[] | string) => void;
  /** Extra content below the keyboard (e.g. selection summary) */
  footer?: ReactNode;
  className?: string;
}

export function KeyboardPreviewCard({
  mode = "single",
  onKeyClick,
  footer,
  className,
}: KeyboardPreviewCardProps) {
  return (
    <Card className={cn("shadow-none border overflow-hidden", className)}>
      <CardContent className="p-3">
        <BaseKeyboard
          mode={mode}
          onButtonClick={onKeyClick ?? (() => {})}
        />
        {footer && <div className="mt-3 pt-3 border-t">{footer}</div>}
      </CardContent>
    </Card>
  );
}
