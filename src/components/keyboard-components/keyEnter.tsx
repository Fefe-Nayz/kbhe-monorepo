import { Button } from "@/components/ui/button"

interface KeyProps {
  id: string
  label: string | React.ReactNode
  value: string
  width: number
}

function KeyEnter({ id, label, value, width }: KeyProps) {

  const widthPixels = (width * 2.5 * 16 + (width - 1) * 0.25 * 16) * 1.4

  return (
    <div
    
      className="h-26 absolute bg-transparent inset-shadow-2xs border-2 rounded hover:bg-accent transition-colors"
      style={{
        width: `${widthPixels}px`,
        clipPath: "polygon(0% 0%, 100% 0%, 100% 100%, 20% 100%, 20% 46%, 0% 46%)",
      }}
    >
      {/* bg-transparent inset-shadow-2x active:bg-blue-300 text-black rounded px-2 py-1 text-xs font-semibold transition-colors border border-gray-200*/}
      <div className="absolute top-2 right-4 flex flex-col items-end gap-1 pointer-events-none">
        <span className="text-xl font-bold">⏎</span>
        <span className="text-[10px] uppercase font-black opacity-70">{label}</span>
      </div>

      <Button
        id={id}
        value={value}
        variant="ghost"
        className="w-full h-full p-0 bg-transparent hover:bg-transparent active:bg-black/5 rounded-none"
      />
    </div>
  )
}

export default KeyEnter