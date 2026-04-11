import { type ReactNode } from "react";
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card";
import { cn } from "@/lib/utils";

interface SectionCardProps {
  title?: string;
  description?: string;
  headerRight?: ReactNode;
  children: ReactNode;
  className?: string;
  contentClassName?: string;
  noPadding?: boolean;
}

export function SectionCard({
  title,
  description,
  headerRight,
  children,
  className,
  contentClassName,
  noPadding,
}: SectionCardProps) {
  return (
    <Card className={cn("shadow-none border", className)}>
      {(title || description || headerRight) && (
        <CardHeader className="pb-3">
          <div className="flex items-start justify-between gap-4">
            <div className="flex-1 min-w-0">
              {title && <CardTitle className="text-sm font-medium">{title}</CardTitle>}
              {description && (
                <CardDescription className="text-xs mt-0.5">{description}</CardDescription>
              )}
            </div>
            {headerRight && <div className="shrink-0">{headerRight}</div>}
          </div>
        </CardHeader>
      )}
      <CardContent className={cn(noPadding ? "p-0" : "pt-0", contentClassName)}>
        {children}
      </CardContent>
    </Card>
  );
}

interface FormRowProps {
  label: string;
  description?: string;
  children: ReactNode;
  className?: string;
}

/** A labelled form row: label+description on the left, control on the right */
export function FormRow({ label, description, children, className }: FormRowProps) {
  return (
    <div className={cn("flex items-center justify-between gap-6 py-3 first:pt-0 last:pb-0", className)}>
      <div className="min-w-0 flex-1">
        <p className="text-sm font-medium leading-none">{label}</p>
        {description && (
          <p className="text-xs text-muted-foreground mt-1">{description}</p>
        )}
      </div>
      <div className="shrink-0">{children}</div>
    </div>
  );
}
