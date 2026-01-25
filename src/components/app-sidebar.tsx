import { useNavigate } from "react-router-dom"

import {
  IconHome,
  IconPencil,
  IconBrandSpeedtest,
  IconSettings,
  IconBulb,
} from "@tabler/icons-react"

import {
  Sidebar,
  SidebarContent,
  SidebarFooter,
  SidebarGroup,
  SidebarGroupContent,
  SidebarGroupLabel,
  SidebarMenu,
  SidebarMenuButton,
  SidebarMenuItem,
} from "@/components/ui/sidebar"


// Menu items.
const items = [
  {
    title: "Home",
    url: "/",
    icon: IconHome,
  },
  {
    title: "Remap",
    url: "/remap",
    icon: IconPencil,
  },
  {
    title: "Calibration",
    url: "/calibration",
    icon: IconBrandSpeedtest,
  },

]

const led_items = [
  {
    title: "LED Effects",
    url: "/led",
    icon: IconBulb,
  },
]

export function AppSidebar() {

  const navigate = useNavigate()
  return (
    <Sidebar collapsible="icon">
      <SidebarContent>
        <SidebarGroup>
          <SidebarGroupLabel>Application</SidebarGroupLabel>
          <SidebarGroupContent>
            <SidebarMenu>
              {items.map((item) => (
                <SidebarMenuItem key={item.title}>
                  <SidebarMenuButton onClick={() => navigate(item.url)}>
                    <item.icon />
                    <span>{item.title}</span>
                  </SidebarMenuButton>
                </SidebarMenuItem>
              ))}
            </SidebarMenu>
          </SidebarGroupContent>
        </SidebarGroup>


        <SidebarGroup>
          <SidebarGroupLabel>LED Configuration</SidebarGroupLabel>
          <SidebarGroupContent>
            <SidebarMenu>
              {led_items.map((item) => (
                <SidebarMenuItem key={item.title}>
                  <SidebarMenuButton onClick={() => navigate(item.url)}>
                    <item.icon />
                    <span>{item.title}</span>
                  </SidebarMenuButton>
                </SidebarMenuItem>
              ))}
            </SidebarMenu>
          </SidebarGroupContent>
        </SidebarGroup>


        <SidebarGroup>
          <SidebarGroupLabel>Settings</SidebarGroupLabel>
          <SidebarGroupContent>
            <SidebarMenu>
              
                <SidebarMenuItem key="settings">
                  <SidebarMenuButton onClick={() => navigate("/settings")}>
                    <IconSettings />
                    <span>Settings</span>
                  </SidebarMenuButton>
                </SidebarMenuItem>
            </SidebarMenu>
          </SidebarGroupContent>
        </SidebarGroup>
      </SidebarContent>
      
    


        <SidebarFooter>

            <div className="px-2 py-2 text-xs text-muted-foreground group-data-[collapsible=icon]:hidden">
                TIPE 2026 - KBHE Configurator
            </div>
              
        </SidebarFooter>
    </Sidebar>
  )
}