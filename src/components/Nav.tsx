import { SidebarTrigger } from "@/components/ui/sidebar"
import { ThemeButton } from "./nav-components/themeButton"



export function Nav() {
  return (
    <nav className="flex justify-between p-4 bg-white border-b">
      <div className="flex items-center gap-4">
        <SidebarTrigger />
        <h1 className="text-xl font-bold">KBHE Configurator</h1>  
      </div>

      <div className="flex-1 flex justify-end">
        <ThemeButton />
      </div>
    </nav>
  )
}

export default Nav