import BaseKeyboard from "@/components/baseKeyboard"
import KeyMapper from "@/components/keyMapper"

export default function Home() {
  return (
    <div className="bg-green-700 justify-center items-center p-4 h-[88vh]">
      {/*<h1 className="text-2xl font-bold mb-4">Welcome to the KBHE Configurator</h1>*/}
      <div className="bg-blue-600 flex justify-center mb-1">
        <BaseKeyboard
          mode="multi"
          onButtonClick={(ids) => console.log("All pressed keys:", ids)}
        />
      </div>
      <div className="bg-red-600 flex  overflow-auto">
        <KeyMapper onButtonClick={(id) => console.log("You clicked on ", id)} />
      </div>

    </div>
  )
}