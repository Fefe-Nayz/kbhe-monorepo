import { Button } from "@/components/ui/button"

interface KeyProps {
  id: string
  label: string | React.ReactNode
  value: string
  width: number
  className?: string
  onSelect?: (id: string) => void;
}

function Key({ id, label, value, width, onSelect, className }: KeyProps) {
  const widthPixels = (12 * width + (width - 1)) * 4

  return (
    <Button
      id={id}
      value={value}
      variant="outline"
      style={{ width: `${widthPixels}px` }}
      className={`h-12 shadow-xs 
                  text-black rounded-x px-2 py-1 text-xs font-semibold 
                  transition-colors border border-gray-200 
                  ${className ?? ""}`}
      onClick={() => onSelect?.(id)}

    >
      <span>{label}</span>
    </Button>
  )
}

export default Key