import { type ReactNode } from "react";
import { cn } from "@/lib/utils";
import { ScrollArea } from "@/components/ui/scroll-area";

interface PageLayoutProps {
  /** Left/top panel — keyboard preview + selection summary */
  preview?: ReactNode;
  /** Right/main panel — settings cards */
  children: ReactNode;
  /** Extra content for the header bar above both panels */
  headerContent?: ReactNode;
  /** If true, layout is vertical (preview on top, content below) */
  vertical?: boolean;
  className?: string;
}

/**
 * Standard 2-panel page layout used across all config pages.
 * Left: keyboard preview (fixed width).
 * Right: scrollable settings content.
 */
export function PageLayout({
  preview,
  children,
  headerContent,
  vertical = false,
  className,
}: PageLayoutProps) {
  if (vertical) {
    return (
      <div className={cn("flex flex-col h-full overflow-hidden", className)}>
        {headerContent && (
          <div className="shrink-0 border-b px-4 py-2">{headerContent}</div>
        )}
        {preview && (
          <div className="shrink-0 flex justify-center items-center border-b px-4 py-3">
            {preview}
          </div>
        )}
        <ScrollArea className="flex-1 min-h-0">
          <div className="p-4 flex flex-col gap-4">{children}</div>
        </ScrollArea>
      </div>
    );
  }

  return (
    <div className={cn("flex flex-col h-full overflow-hidden", className)}>
      {headerContent && (
        <div className="shrink-0 border-b px-4 py-2">{headerContent}</div>
      )}
      <div className="flex flex-1 min-h-0">
        {preview && (
          <ScrollArea className="shrink-0 w-auto border-r">
            <div className="flex flex-col items-center p-4">{preview}</div>
          </ScrollArea>
        )}
        <ScrollArea className="flex-1 min-h-0">
          <div className="p-4 flex flex-col gap-4 max-w-2xl mx-auto">{children}</div>
        </ScrollArea>
      </div>
    </div>
  );
}

interface PageHeaderProps {
  title: string;
  description?: string;
  actions?: ReactNode;
}

export function PageHeader({ title, description, actions }: PageHeaderProps) {
  return (
    <div className="flex items-center justify-between gap-4">
      <div>
        <h1 className="text-base font-semibold">{title}</h1>
        {description && (
          <p className="text-xs text-muted-foreground mt-0.5">{description}</p>
        )}
      </div>
      {actions && <div className="shrink-0 flex items-center gap-2">{actions}</div>}
    </div>
  );
}

interface PageHeaderBarProps extends PageHeaderProps {
  className?: string;
}

export function PageHeaderBar({
  title,
  description,
  actions,
  className,
}: PageHeaderBarProps) {
  return (
    <div className={cn("shrink-0 border-b px-4 py-2", className)}>
      <PageHeader title={title} description={description} actions={actions} />
    </div>
  );
}

interface PageContentProps {
  children: ReactNode;
  className?: string;
  containerClassName?: string;
}

export function PageContent({
  children,
  className,
  containerClassName = "max-w-3xl",
}: PageContentProps) {
  return (
    <ScrollArea className={cn("flex-1 min-h-0", className)}>
      <div className={cn("mx-auto flex w-full flex-col gap-4 p-4", containerClassName)}>{children}</div>
    </ScrollArea>
  );
}
