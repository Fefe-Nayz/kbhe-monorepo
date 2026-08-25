import { useNavigate, useLocation } from "react-router-dom";
import { useQuery } from "@tanstack/react-query";
import { useDeviceSession, DeviceSessionManager } from "@/lib/kbhe/session";
import { kbheDevice } from "@/lib/kbhe/device";
import { queryKeys } from "@/lib/query/keys";
import {
  Sidebar,
  SidebarContent,
  SidebarFooter,
  SidebarGroup,
  SidebarGroupContent,
  SidebarGroupLabel,
  SidebarHeader,
  SidebarMenu,
  SidebarMenuButton,
  SidebarMenuItem,
  SidebarRail,
} from "@/components/ui/sidebar";
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuItem,
  DropdownMenuTrigger,
} from "@/components/ui/dropdown-menu";
import { cn } from "@/lib/utils";
import {
  IconLayoutGrid,
  IconKeyboard,
  IconBrandSpeedtest,
  IconArrowBigUpLines,
  IconDeviceGamepad2,
  IconCrosshair,
  IconBulb,
  IconRotateClockwise,
  IconSettings,
  IconActivity,
  IconCode,
  IconUpload,
  IconKeyboardOff,
  IconChevronDown,
  IconPlugConnected,
  IconPlugConnectedX,
  IconDeviceDesktop,
  IconHome,
  IconBraces,
} from "@tabler/icons-react";
import type { Icon } from "@tabler/icons-react";

interface NavItem {
  title: string;
  path: string;
  icon: Icon;
}

const NAV_GROUPS: { label: string; items: NavItem[] }[] = [
  {
    label: "Overview",
    items: [
      { title: "Dashboard", path: "/", icon: IconHome },
      { title: "Profiles", path: "/profiles", icon: IconLayoutGrid },
    ],
  },
  {
    label: "Configure",
    items: [
      { title: "Keymap", path: "/keymap", icon: IconKeyboard },
      { title: "Performance", path: "/performance", icon: IconBrandSpeedtest },
      { title: "Advanced Keys", path: "/advanced-keys", icon: IconArrowBigUpLines },
      { title: "Macros", path: "/macros", icon: IconBraces },
      { title: "Gamepad", path: "/gamepad", icon: IconDeviceGamepad2 },
      { title: "Rotary", path: "/rotary", icon: IconRotateClockwise },
      { title: "Lighting", path: "/lighting", icon: IconBulb },
    ],
  },
  {
    label: "Hardware",
    items: [
      { title: "Calibration", path: "/calibration", icon: IconCrosshair },
      { title: "Device", path: "/device", icon: IconSettings },
      { title: "Firmware", path: "/firmware", icon: IconUpload },
    ],
  },
];

const DEV_ITEMS: NavItem[] = [
  { title: "Diagnostics", path: "/diagnostics", icon: IconActivity },
];

interface AppSidebarProps {
  variant?: "sidebar" | "floating" | "inset";
}

function KeyboardMenu() {
  const { status, deviceInfo } = useDeviceSession();
  const runtimeConnected = status === "connected";
  const detected = status === "connected" || status === "updater" || status === "recovery-only";

  const identityQ = useQuery({
    queryKey: queryKeys.device.identity(),
    queryFn: () => kbheDevice.getDeviceInfo(),
    enabled: runtimeConnected,
    staleTime: 30_000,
  });

  const keyboardName = identityQ.data?.keyboard_name?.trim()
    || deviceInfo?.product?.trim()
    || "KBHE Keyboard";

  const serialNumber = identityQ.data?.serial_number?.trim()
    || deviceInfo?.serialNumber?.trim()
    || null;

  return (
    <SidebarMenu className="gap-1">
      <SidebarMenuItem>
        <DropdownMenu>
          <DropdownMenuTrigger
            render={
              <SidebarMenuButton
                size="lg"
                className="data-[state=open]:bg-sidebar-accent data-[state=open]:text-sidebar-accent-foreground"
              >
                <div
                  className={cn(
                    "relative flex aspect-square size-8 items-center justify-center rounded-lg transition-colors",
                    detected
                      ? "bg-sidebar-primary text-sidebar-primary-foreground"
                      : "bg-sidebar-accent text-muted-foreground",
                  )}
                >
                  <IconKeyboard className="size-4" />
                  <span
                    className={cn(
                      "absolute -bottom-0.5 -right-0.5 size-2.5 rounded-full border-2 border-sidebar",
                      detected ? "bg-success" : "bg-muted-foreground/50",
                    )}
                  />
                </div>
                <div className="grid flex-1 text-left text-sm leading-tight group-data-[collapsible=icon]:hidden">
                  <span className="truncate font-medium">
                    {detected ? keyboardName : "KBHE Configurator"}
                  </span>
                  <span className="truncate font-mono text-[0.7rem] text-muted-foreground">
                    {detected ? (serialNumber ?? "SN unavailable") : "No device"}
                  </span>
                </div>
                <IconChevronDown className="ml-auto size-4 text-muted-foreground group-data-[collapsible=icon]:hidden" />
              </SidebarMenuButton>
            }
          />
          <DropdownMenuContent side="bottom" align="start" className="w-56">
            {detected ? (
              <DropdownMenuItem onClick={() => void DeviceSessionManager.disconnect()}>
                <IconPlugConnectedX className="size-4" />
                Disconnect
              </DropdownMenuItem>
            ) : (
              <DropdownMenuItem onClick={() => void DeviceSessionManager.connect()}>
                <IconKeyboardOff className="size-4" />
                Connect
              </DropdownMenuItem>
            )}
          </DropdownMenuContent>
        </DropdownMenu>
      </SidebarMenuItem>
    </SidebarMenu>
  );
}

function NavButton({
  item,
  active,
  onSelect,
}: {
  item: NavItem;
  active: boolean;
  onSelect: () => void;
}) {
  return (
    <SidebarMenuItem>
      <SidebarMenuButton
        isActive={active}
        tooltip={item.title}
        onClick={onSelect}
        className={cn(
          "relative h-8 gap-2.5 text-[0.82rem] font-medium transition-colors",
          active && "text-sidebar-accent-foreground",
        )}
      >
        {/* Active rail — a solid cue that survives the translucent Mica sidebar. */}
        <span
          className={cn(
            "absolute -left-2 top-1/2 h-4 w-0.75 -translate-y-1/2 rounded-full bg-sidebar-primary transition-opacity group-data-[collapsible=icon]:hidden",
            active ? "opacity-100" : "opacity-0",
          )}
        />
        <item.icon
          className={cn("size-4 shrink-0", active ? "text-sidebar-primary" : "text-muted-foreground")}
        />
        <span className="truncate">{item.title}</span>
      </SidebarMenuButton>
    </SidebarMenuItem>
  );
}

export function AppSidebar({ variant = "inset" }: AppSidebarProps) {
  const navigate = useNavigate();
  const location = useLocation();
  const developerMode = useDeviceSession((s) => s.developerMode);
  const firmwareVersion = useDeviceSession((s) => s.firmwareVersion);
  const status = useDeviceSession((s) => s.status);
  const deviceInfo = useDeviceSession((s) => s.deviceInfo);
  const detected = status === "connected" || status === "updater" || status === "recovery-only";

  const isActive = (path: string) =>
    path === "/" ? location.pathname === "/" : location.pathname.startsWith(path);

  return (
    <Sidebar variant={variant} collapsible="icon" className="app-mica-sidebar">
      <SidebarHeader className="pb-1">
        <KeyboardMenu />
      </SidebarHeader>

      <SidebarContent className="gap-0.5 pt-1">
        {NAV_GROUPS.map((group) => (
          <SidebarGroup key={group.label} className="py-1">
            <SidebarGroupLabel className="h-6 text-[0.68rem] font-semibold uppercase tracking-[0.1em] text-muted-foreground/70">
              {group.label}
            </SidebarGroupLabel>
            <SidebarGroupContent>
              <SidebarMenu className="gap-0.5">
                {group.items.map((item) => (
                  <NavButton
                    key={item.path}
                    item={item}
                    active={isActive(item.path)}
                    onSelect={() => navigate(item.path)}
                  />
                ))}
              </SidebarMenu>
            </SidebarGroupContent>
          </SidebarGroup>
        ))}

        {developerMode && (
          <SidebarGroup className="py-1">
            <SidebarGroupLabel className="h-6 gap-1.5 text-[0.68rem] font-semibold uppercase tracking-[0.1em] text-muted-foreground/70">
              <IconCode className="size-3" />
              Developer
            </SidebarGroupLabel>
            <SidebarGroupContent>
              <SidebarMenu className="gap-0.5">
                {DEV_ITEMS.map((item) => (
                  <NavButton
                    key={item.path}
                    item={item}
                    active={isActive(item.path)}
                    onSelect={() => navigate(item.path)}
                  />
                ))}
              </SidebarMenu>
            </SidebarGroupContent>
          </SidebarGroup>
        )}
      </SidebarContent>

      <SidebarFooter className="gap-1">
        <SidebarMenu className="gap-0.5">
          <NavButton
            item={{ title: "App Settings", path: "/settings", icon: IconDeviceDesktop }}
            active={isActive("/settings")}
            onSelect={() => navigate("/settings")}
          />
        </SidebarMenu>

        <div className="mx-1 rounded-lg border border-sidebar-border/70 bg-sidebar-accent/40 px-2.5 py-2 group-data-[collapsible=icon]:hidden">
          <div className="flex items-center gap-1.5 text-xs">
            <span
              className={cn(
                "size-1.5 shrink-0 rounded-full",
                detected ? "bg-success" : "bg-muted-foreground/50",
              )}
            />
            <span
              className={cn(
                "truncate font-medium",
                detected ? "text-foreground" : "text-muted-foreground",
              )}
            >
              {detected ? (deviceInfo?.product ?? "Connected") : "No device"}
            </span>
          </div>
          <div className="mt-1 flex items-center justify-between gap-2 text-[0.68rem] text-muted-foreground">
            <span className="truncate">KBHE Configurator</span>
            {firmwareVersion && (
              <span className="shrink-0 font-mono">fw {firmwareVersion}</span>
            )}
          </div>
        </div>

        <div className="hidden items-center justify-center py-1.5 group-data-[collapsible=icon]:flex">
          {detected ? (
            <IconPlugConnected className="size-3.5 text-success" />
          ) : (
            <IconPlugConnectedX className="size-3.5 text-muted-foreground" />
          )}
        </div>
      </SidebarFooter>

      <SidebarRail />
    </Sidebar>
  );
}
