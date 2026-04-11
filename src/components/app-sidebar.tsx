import { useNavigate, useLocation } from "react-router-dom";
import { useDeviceSession } from "@/lib/kbhe/session";
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
  IconLayoutDashboard,
  IconLayoutGrid,
  IconKeyboard,
  IconBrandSpeedtest,
  IconArrowBigUpLines,
  IconDeviceGamepad2,
  IconCrosshair,
  IconBulb,
  IconRotateClockwise,
  IconDeviceDesktop,
  IconActivity,
  IconSettings,
  IconCode,
} from "@tabler/icons-react";
import { cn } from "@/lib/utils";

const NAV_GROUPS = [
  {
    label: "Profiles",
    items: [
      { title: "Profiles", path: "/profiles", icon: IconLayoutGrid },
    ],
  },
  {
    label: "Keyboard",
    items: [
      { title: "Dashboard", path: "/", icon: IconLayoutDashboard },
      { title: "Keymap", path: "/keymap", icon: IconKeyboard },
      { title: "Performance", path: "/performance", icon: IconBrandSpeedtest },
      { title: "Advanced Keys", path: "/advanced-keys", icon: IconArrowBigUpLines },
    ],
  },
  {
    label: "Input",
    items: [
      { title: "Gamepad", path: "/gamepad", icon: IconDeviceGamepad2 },
      { title: "Calibration", path: "/calibration", icon: IconCrosshair },
    ],
  },
  {
    label: "Lighting & Rotary",
    items: [
      { title: "Lighting", path: "/lighting", icon: IconBulb },
      { title: "Rotary", path: "/rotary", icon: IconRotateClockwise },
    ],
  },
  {
    label: "Device",
    items: [
      { title: "Device", path: "/device", icon: IconSettings },
    ],
  },
];

const DEV_ITEMS = [
  { title: "Diagnostics", path: "/diagnostics", icon: IconActivity },
];

export function AppSidebar() {
  const navigate = useNavigate();
  const location = useLocation();
  const developerMode = useDeviceSession((s) => s.developerMode);

  const isActive = (path: string) =>
    path === "/" ? location.pathname === "/" : location.pathname.startsWith(path);

  return (
    <Sidebar collapsible="icon">
      <SidebarHeader>
        <div className="flex items-center gap-2 px-2 py-1 group-data-[collapsible=icon]:justify-center">
          <IconDeviceDesktop className="size-5 shrink-0 text-primary" />
          <span className="font-semibold text-sm group-data-[collapsible=icon]:hidden">
            KBHE Configurator
          </span>
        </div>
      </SidebarHeader>

      <SidebarContent>
        {NAV_GROUPS.map((group) => (
          <SidebarGroup key={group.label}>
            <SidebarGroupLabel>{group.label}</SidebarGroupLabel>
            <SidebarGroupContent>
              <SidebarMenu>
                {group.items.map((item) => (
                  <SidebarMenuItem key={item.path}>
                    <SidebarMenuButton
                      isActive={isActive(item.path)}
                      tooltip={item.title}
                      onClick={() => navigate(item.path)}
                      className={cn(
                        isActive(item.path) && "bg-sidebar-accent text-sidebar-accent-foreground",
                      )}
                    >
                      <item.icon />
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
              <SidebarMenu>
                {DEV_ITEMS.map((item) => (
                  <SidebarMenuItem key={item.path}>
                    <SidebarMenuButton
                      isActive={isActive(item.path)}
                      tooltip={item.title}
                      onClick={() => navigate(item.path)}
                    >
                      <item.icon />
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
        <div className="px-2 py-1 text-xs text-muted-foreground group-data-[collapsible=icon]:hidden">
          TIPE 2026 · KBHE
        </div>
      </SidebarFooter>

      <SidebarRail />
    </Sidebar>
  );
}
