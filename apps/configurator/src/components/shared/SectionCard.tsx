import { type ReactNode } from "react";
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card";
import { cn } from "@/lib/utils";

type SectionCardTone = "default" | "muted" | "danger" | "accent";

const TONE_CLASSES: Record<SectionCardTone, string> = {
  default: "bg-card ring-border",
  muted: "bg-surface-sunken ring-border",
  danger: "bg-destructive/5 ring-destructive/25",
  accent: "bg-primary/5 ring-primary/25",
};

const TONE_TITLE_CLASSES: Record<SectionCardTone, string> = {
  default: "",
  muted: "",
  danger: "text-destructive",
  accent: "text-primary",
};

interface SectionCardProps {
  title?: string;
  description?: string;
  /** Small icon shown before the title. */
  icon?: ReactNode;
  headerRight?: ReactNode;
  /** Pinned to the bottom of the card in a tinted footer band. */
  footer?: ReactNode;
  tone?: SectionCardTone;
  children: ReactNode;
  className?: string;
  contentClassName?: string;
  noPadding?: boolean;
}

export function SectionCard({
  title,
  description,
  icon,
  headerRight,
  footer,
  tone = "default",
  children,
  className,
  contentClassName,
  noPadding,
}: SectionCardProps) {
  const hasHeader = Boolean(title || description || headerRight);

  return (
    <Card
      className={cn(
        "gap-0 overflow-visible py-0 shadow-xs ring-1",
        TONE_CLASSES[tone],
        className,
      )}
    >
      {hasHeader && (
        <CardHeader className="flex flex-row items-start justify-between gap-4 border-b px-5 py-3.5">
          <div className="flex min-w-0 flex-1 items-start gap-2.5">
            {icon && (
              <span
                className={cn(
                  "mt-px flex size-6 shrink-0 items-center justify-center rounded-md bg-muted text-muted-foreground [&_svg]:size-3.5",
                  tone === "danger" && "bg-destructive/10 text-destructive",
                  tone === "accent" && "bg-primary/10 text-primary",
                )}
              >
                {icon}
              </span>
            )}
            <div className="min-w-0 flex-1">
              {title && (
                <CardTitle
                  className={cn(
                    "text-sm font-semibold tracking-tight",
                    TONE_TITLE_CLASSES[tone],
                  )}
                >
                  {title}
                </CardTitle>
              )}
              {description && (
                <CardDescription className="mt-0.5 text-xs leading-relaxed">
                  {description}
                </CardDescription>
              )}
            </div>
          </div>
          {headerRight && (
            <div className="flex shrink-0 items-center gap-2">{headerRight}</div>
          )}
        </CardHeader>
      )}
      <CardContent className={cn(noPadding ? "p-0" : "px-5 py-4", contentClassName)}>
        {children}
      </CardContent>
      {footer && (
        <div className="flex items-center justify-end gap-2 border-t bg-muted/40 px-5 py-3">
          {footer}
        </div>
      )}
    </Card>
  );
}

interface FormRowProps {
  label: ReactNode;
  description?: ReactNode;
  children: ReactNode;
  className?: string;
  /** Stack the control beneath the label instead of aligning it to the right. */
  stacked?: boolean;
  /** Indent the row to show it depends on the row above. */
  nested?: boolean;
  htmlFor?: string;
}

/**
 * A labelled form row: label + description on the left, control on the right.
 * Rows inside the same container are separated by hairlines, which keeps dense
 * settings lists readable without wrapping every control in its own card.
 */
export function FormRow({
  label,
  description,
  children,
  className,
  stacked,
  nested,
  htmlFor,
}: FormRowProps) {
  if (stacked) {
    return (
      <div
        className={cn(
          "flex flex-col gap-2 border-t border-border/60 py-3.5 first:border-t-0 first:pt-0 last:pb-0",
          nested && "pl-4",
          className,
        )}
      >
        <div className="min-w-0">
          <label
            htmlFor={htmlFor}
            className="text-sm font-medium leading-none text-foreground"
          >
            {label}
          </label>
          {description && (
            <p className="mt-1 text-xs leading-relaxed text-muted-foreground">{description}</p>
          )}
        </div>
        <div>{children}</div>
      </div>
    );
  }

  return (
    <div
      className={cn(
        "flex items-center justify-between gap-6 border-t border-border/60 py-3.5 first:border-t-0 first:pt-0 last:pb-0",
        nested && "pl-4",
        className,
      )}
    >
      <div className="min-w-0 flex-1">
        <label
          htmlFor={htmlFor}
          className="text-sm font-medium leading-none text-foreground"
        >
          {label}
        </label>
        {description && (
          <p className="mt-1 text-xs leading-relaxed text-muted-foreground">{description}</p>
        )}
      </div>
      <div className="flex shrink-0 items-center gap-2">{children}</div>
    </div>
  );
}

/** Wraps a run of FormRows so hairline separators land consistently. */
export function FormRows({
  children,
  className,
}: {
  children: ReactNode;
  className?: string;
}) {
  return <div className={cn("flex flex-col", className)}>{children}</div>;
}
