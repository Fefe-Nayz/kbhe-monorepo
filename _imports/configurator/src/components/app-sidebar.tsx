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
    items: [{ title: "Dashboard", path: "/", icon: IconHome }],
  },
  {
    label: "Profiles",
    items: [
      { title: "Profiles", path: "/profiles", icon: IconLayoutGrid },
    ],
  },
  {
    label: "Keyboard Configuration",
    items: [
      { title: "Keymap", path: "/keymap", icon: IconKeyboard },
      { title: "Performance", path: "/performance", icon: IconBrandSpeedtest },
      { title: "Advanced Keys", path: "/advanced-keys", icon: IconArrowBigUpLines },
      { title: "Gamepad", path: "/gamepad", icon: IconDeviceGamepad2 },
      { title: "Calibration", path: "/calibration", icon: IconCrosshair },
      { title: "Rotary", path: "/rotary", icon: IconRotateClockwise },
    ],
  },
  {
    label: "Lighting",
    items: [
      { title: "Lighting", path: "/lighting", icon: IconBulb },
    ],
  },
  {
    label: "Device",
    items: [
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
  const connected = status === "connected" || status === "updater";

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
                <div className="flex aspect-square size-8 items-center justify-center rounded-lg bg-sidebar-primary text-sidebar-primary-foreground">
                  <IconKeyboard className="size-4" />
                </div>
                <div className="grid flex-1 text-left text-sm leading-tight group-data-[collapsible=icon]:hidden">
                  <span className="truncate font-medium">
                    {connected ? keyboardName : "KBHE Configurator"}
                  </span>
                  <span className="truncate font-mono text-xs text-muted-foreground">
                    {connected ? (serialNumber ? `${serialNumber}` : "SN unavailable") : "No device"}
                  </span>
                </div>
                <IconChevronDown className="ml-auto size-4 group-data-[collapsible=icon]:hidden" />
              </SidebarMenuButton>
            }
          />
          <DropdownMenuContent side="bottom" align="start" className="w-56">
            {connected ? (
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

export function AppSidebar({ variant = "inset" }: AppSidebarProps) {
  const navigate = useNavigate();
  const location = useLocation();
  const developerMode = useDeviceSession((s) => s.developerMode);
  const firmwareVersion = useDeviceSession((s) => s.firmwareVersion);
  const status = useDeviceSession((s) => s.status);
  const deviceInfo = useDeviceSession((s) => s.deviceInfo);
  const connected = status === "connected" || status === "updater";

  const isActive = (path: string) =>
    path === "/" ? location.pathname === "/" : location.pathname.startsWith(path);

  return (
    <Sidebar variant={variant} collapsible="icon" className="app-mica-sidebar">
      <SidebarHeader className="pb-1">
        <KeyboardMenu />
      </SidebarHeader>

      <SidebarContent className="gap-1 pt-1">
        {NAV_GROUPS.map((group) => (
          <SidebarGroup key={group.label}>
            <SidebarGroupLabel>{group.label}</SidebarGroupLabel>
            <SidebarGroupContent>
              <SidebarMenu className="gap-1">
                {group.items.map((item) => (
                  <SidebarMenuItem key={item.path}>
                    <SidebarMenuButton
                      isActive={isActive(item.path)}
                      tooltip={item.title}
                      onClick={() => navigate(item.path)}
                    >
                      <item.icon className="size-4" />
                      <span>{item.title}</span>
                    </SidebarMenuButton>
                  </SidebarMenuItem>
                ))}
              </SidebarMenu>
            </SidebarGroupContent>
          </SidebarGroup>
        ))}

        {developerMode && (
          <SidebarGroup>
            <SidebarGroupLabel className="flex items-center gap-1">
              <IconCode className="size-3" />
              Developer
            </SidebarGroupLabel>
            <SidebarGroupContent>
              <SidebarMenu className="gap-1">
                {DEV_ITEMS.map((item) => (
                  <SidebarMenuItem key={item.path}>
                    <SidebarMenuButton
                      isActive={isActive(item.path)}
                      tooltip={item.title}
                      onClick={() => navigate(item.path)}
                    >
                      <item.icon className="size-4" />
                      <span>{item.title}</span>
                    </SidebarMenuButton>
                  </SidebarMenuItem>
                ))}
              </SidebarMenu>
            </SidebarGroupContent>
          </SidebarGroup>
        )}
      </SidebarContent>

      <SidebarFooter>
        <SidebarMenu className="mb-1 gap-1">
          <SidebarMenuItem>
            <SidebarMenuButton
              isActive={isActive("/settings")}
              tooltip="App Settings"
              onClick={() => navigate("/settings")}
            >
              <IconDeviceDesktop className="size-4" />
              <span>App Settings</span>
            </SidebarMenuButton>
          </SidebarMenuItem>
        </SidebarMenu>

        <div className="flex flex-col gap-1 px-2 py-1.5 text-xs group-data-[collapsible=icon]:hidden">
          <div className="flex items-center gap-1.5">
            {connected ? (
              <IconPlugConnected className="size-3 text-green-500" />
            ) : (
              <IconPlugConnectedX className="size-3 text-muted-foreground" />
            )}
            <span className={cn("truncate", connected ? "text-foreground" : "text-muted-foreground")}>
              {connected ? (deviceInfo?.product ?? "Connected") : "No device"}
            </span>
          </div>
          <div className="flex items-center justify-between text-muted-foreground">
            <span className="truncate">KBHE Configurator</span>
            {firmwareVersion && (
              <span className="font-mono shrink-0">fw {firmwareVersion}</span>
            )}
          </div>
        </div>
        <div className="hidden items-center justify-center py-1.5 group-data-[collapsible=icon]:flex">
          {connected ? (
            <IconPlugConnected className="size-3.5 text-green-500" />
          ) : (
            <IconPlugConnectedX className="size-3.5 text-muted-foreground" />
          )}
        </div>
      </SidebarFooter>

      <SidebarRail />
    </Sidebar>
  );
}
