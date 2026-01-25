import BaseKeyboard from "@/components/baseKeyboard"
import KeyMapper from "@/components/keyMapper"

export default function Home() {
  return (
    <div className="p-4">
      <h1 className="text-2xl font-bold mb-4">Welcome to the KBHE Configurator</h1>
      <div>
        <BaseKeyboard />
      </div>
      <div>
        <KeyMapper />

      </div>
      
    </div>
  )
}