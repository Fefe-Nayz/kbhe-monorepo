import { SidebarProvider } from "@/components/ui/sidebar"
import { AppSidebar } from "@/components/app-sidebar"
import { Routes, Route } from "react-router-dom"
import { useProfileStore } from "./stores/profileStore"
import { useKeyboardStore } from "./stores/keyboard-store"

import { useEffect, useState } from "react"

import Home from "./pages/Home"
import Remap from "./pages/remap"
import Calibration from "./pages/calibration"
import LED from "./pages/LED"
import Settings from "./pages/settings"
import Gamepad from "./pages/gamepad"
import Performance from "./pages/performance"
import Profiles from "./pages/profiles"

import Nav from "./components/Nav"


export default function App() {
  //For now its works
  const [ready, setReady] = useState(false)
  useEffect(() => {
  // initialize the profile store (load profiles from localStorage)
  useProfileStore.getState().init()


    //  Wait until the profile store is initialized before rendering the app
    const unsub = useProfileStore.subscribe((state) => {
      if (state.selectedProfile) {
        setReady(true)
        unsub()
      }
    })

  // Subscribe to keyboard store changes and update the selected profile in real-time
  const unsubscribe = useKeyboardStore.subscribe((keyboardState) => {
    if (!keyboardState.saveEnabled) return

    const selected = useProfileStore.getState().selectedProfile
    if (!selected) return

    useProfileStore.getState().updateSelectedProfile(keyboardState)
  })

  return () => unsubscribe() // cleanup when component unmounts
}, [])
  // TODO: Use the subscribe method to save the layout in localStorage whenever it changes


if (!ready) {
  return null
}

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
            <Route path="/profiles" element={<Profiles />} />
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
