import { Button } from "@/components/ui/button"

interface KeyProps {
  id: string
  label: string | React.ReactNode
  value: string 
  width: number
  onSelect?: (id: string) => void;
}

function Key({ id, label, value, width, onSelect }: KeyProps) {
  const widthPixels = (12 * width + (width - 1)) * 4

  return (
    <Button
      id={id}
      value={value}
      variant="outline"
      style={{ width: `${widthPixels}px`}}
      className="h-12 bg-transparent shadow-xs active:bg-blue-300 text-black rounded-x px-2 py-1 text-xs font-semibold transition-colors border border-gray-200"
      onClick={() => onSelect?.(id)}

    >
      <span>{label}</span>
    </Button>
  )
}

export default Key