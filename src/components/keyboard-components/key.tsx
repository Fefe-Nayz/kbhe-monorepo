import { Button } from "@/components/ui/button"

interface KeyProps {
  id: string
  label: string | React.ReactNode
  value: string 
  width: number
}

function Key({ id, label, value, width }: KeyProps) {
  const widthPixels = (12 * width + (width - 1)) * 4

  return (
    <Button
      id={id}
      value={value}
      variant="outline"
      style={{ width: `${widthPixels}px` }}
      className="h-12 bg-transparent inset-shadow-2xs active:bg-blue-300 text-black rounded px-2 py-1 text-xs font-semibold transition-colors border border-gray-200"
    >
      <span>{label}</span>
    </Button>
  )
}

export default Key