import { SidebarProvider } from "@/components/ui/sidebar"
import { AppSidebar } from "@/components/app-sidebar"
import { Routes, Route } from "react-router-dom"

import Home from "./pages/Home"
import Remap from "./pages/remap"
import Calibration from "./pages/calibration"
import LED from "./pages/LED"
import Settings from "./pages/settings"
import Gamepad from "./pages/gamepad"
import Performance from "./pages/performance"

import Nav from "./components/Nav"

export default function App() {
  return (


    <div className="h-screen flex flex-col">

        <SidebarProvider>
        <AppSidebar />

        
        <main className="bg-amber-300 w-screen overflow-hidden">
          {/*
        <div className="bg-amber-500">
            <SidebarTrigger />
        </div>
        
        */}
        
          <Nav  />
          <Routes>
            <Route path="/" element={<Home />} />
            <Route path="/remap" element={<Remap />} />
            <Route path="/performance" element={<Performance />} />
            <Route path="/calibration" element={<Calibration />} />
            <Route path="/gamepad" element={<Gamepad />} />
            <Route path="/led" element={<LED />} />
            <Route path="/settings" element={<Settings />} />
          </Routes>
        </main>
      </SidebarProvider>


    </div>

  )
}
