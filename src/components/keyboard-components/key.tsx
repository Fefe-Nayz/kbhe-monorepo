interface KeyProps {
  id: string
  label: string
  value: string
  width: number
}

function Key({ id, label, value, width }: KeyProps) {
  const widthPixels = (width * 2.5 * 16 + (width - 1) * 0.25 * 16) *  1.4 // conversion en pixels (1rem = 16px)

  return (
    <button
      id={id}
      value={value}
      style={{ width: `${widthPixels}px` }}
      className="h-10 bg-white hover:bg-gray-600 active:bg-gray-500 text-black rounded px-2 py-1 text-xs font-semibold transition-colors border border-gray-600"
    >
      <span>{label}</span>
    </button>
  )
}

export default Key