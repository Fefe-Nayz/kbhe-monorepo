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
          <div className="shrink-0 border-b px-6 py-2.5">{headerContent}</div>
        )}
        {preview && (
          <div className="shrink-0 flex justify-center items-center border-b px-6 py-4">
            {preview}
          </div>
        )}
        <ScrollArea className="flex-1 min-h-0">
          <div className="p-6 flex flex-col gap-5">{children}</div>
        </ScrollArea>
      </div>
    );
  }

  return (
    <div className={cn("flex flex-col h-full overflow-hidden", className)}>
      {headerContent && (
        <div className="shrink-0 border-b px-6 py-2.5">{headerContent}</div>
      )}
      <div className="flex flex-1 min-h-0">
        {preview && (
          <ScrollArea className="shrink-0 w-auto border-r">
            <div className="flex flex-col items-center p-6">{preview}</div>
          </ScrollArea>
        )}
        <ScrollArea className="flex-1 min-h-0">
          <div className="p-6 flex flex-col gap-5 max-w-3xl mx-auto">{children}</div>
        </ScrollArea>
      </div>
    </div>
  );
}

interface PageHeaderProps {
  title: string;
  description?: string;
  actions?: ReactNode;
  /** Optional leading icon rendered in a tinted square. */
  icon?: ReactNode;
}

export function PageHeader({ title, description, actions, icon }: PageHeaderProps) {
  return (
    <div className="flex items-center justify-between gap-4">
      <div className="flex min-w-0 items-center gap-3">
        {icon && (
          <div className="flex size-9 shrink-0 items-center justify-center rounded-lg bg-primary/10 text-primary [&_svg]:size-4.5">
            {icon}
          </div>
        )}
        <div className="min-w-0">
          <h2 className="truncate text-[0.95rem] font-semibold tracking-tight">{title}</h2>
          {description && (
            <p className="mt-0.5 truncate text-xs text-muted-foreground">{description}</p>
          )}
        </div>
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
  icon,
  className,
}: PageHeaderBarProps) {
  return (
    <div className={cn("shrink-0 border-b bg-background/60 px-6 py-3", className)}>
      <PageHeader title={title} description={description} actions={actions} icon={icon} />
    </div>
  );
}

interface PageContentProps {
  children: ReactNode;
  className?: string;
  containerClassName?: string;
}

/**
 * Scrollable page body with a centred, width-capped column.
 * Pages that need the full width pass `containerClassName="max-w-none"`.
 */
export function PageContent({
  children,
  className,
  containerClassName = "max-w-4xl",
}: PageContentProps) {
  return (
    <ScrollArea className={cn("flex-1 min-h-0", className)}>
      <div
        className={cn(
          "mx-auto flex w-full flex-col gap-5 px-6 py-6",
          containerClassName,
        )}
      >
        {children}
      </div>
    </ScrollArea>
  );
}

/**
 * Full-height page scaffold: optional sticky header bar + scrolling body.
 * Replaces the ad-hoc `div.flex.flex-col.h-full` wrappers each page grew its own copy of.
 */
export function Page({
  title,
  description,
  icon,
  actions,
  children,
  containerClassName,
  className,
}: PageHeaderProps & {
  children: ReactNode;
  containerClassName?: string;
  className?: string;
}) {
  return (
    <div className={cn("flex h-full flex-col overflow-hidden", className)}>
      <PageHeaderBar
        title={title}
        description={description}
        icon={icon}
        actions={actions}
      />
      <PageContent containerClassName={containerClassName}>{children}</PageContent>
    </div>
  );
}

/** A titled band inside a page body — groups related cards without nesting them. */
export function PageSection({
  title,
  description,
  actions,
  children,
  className,
}: {
  title?: string;
  description?: string;
  actions?: ReactNode;
  children: ReactNode;
  className?: string;
}) {
  return (
    <section className={cn("flex flex-col gap-3", className)}>
      {(title || actions) && (
        <div className="flex items-end justify-between gap-4">
          <div className="min-w-0">
            {title && (
              <h3 className="text-xs font-semibold uppercase tracking-[0.08em] text-muted-foreground">
                {title}
              </h3>
            )}
            {description && (
              <p className="mt-1 text-xs text-muted-foreground/80">{description}</p>
            )}
          </div>
          {actions && <div className="flex shrink-0 items-center gap-2">{actions}</div>}
        </div>
      )}
      {children}
    </section>
  );
}
