import { StrictMode } from "react"
import { createRoot } from "react-dom/client"
import { BrowserRouter } from "react-router-dom"
import "./index.css"
import "@/lib/vendor/react-kle-modern/styles.css"
import App from "./App.tsx"

// Dev-only simulated keyboard, so the populated UI can be worked on in a plain
// browser. Tree-shaken out of production builds; see lib/dev/mock-device.ts.
if (import.meta.env.DEV) {
  const { isMockDeviceEnabled, installMockDevice } = await import("@/lib/dev/mock-device")
  if (isMockDeviceEnabled()) installMockDevice()
}

createRoot(document.getElementById("root")!).render(
  <StrictMode>
    <BrowserRouter>
      <App />
    </BrowserRouter>
  </StrictMode>
)
