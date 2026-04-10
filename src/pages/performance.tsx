import BaseKeyboard from "@/components/baseKeyboard"
import PerformanceButtonDiv from "@/components/div/divPerformance"
import KeySettingsMapper from "@/components/div/keySettingsMapper"
import PerformanceZone from "@/components/performance-zone"

export default function Performance() {
  return (
    <main className="h-screen flex flex-col">
       
        <h1 className="text-3xl flex items-center border border-gray-300 rounded-md justify-center px-6 mt-4 font-bold">Performance</h1> 
        <p className="text-gray-600 mt-2">Performance page content</p>
      <div className="flex-shrink-0 overflow-x-auto border-b border-gray-300 flex justify-center items-center">
        <div className="p-8 pt-4 pb-4">
          <BaseKeyboard mode="multi" 
          onButtonClick={(ids) => console.log("All pressed keys:", ids)} />
        </div>
      </div>

      <div className="flex-1 overflow-y-auto">
        <div className="p-8 flex flex-col items-center">
         
          
          <div className="mt-6">
            <div>
              <PerformanceButtonDiv />
            </div>
          </div>
          <div className="mt-6 flex justify-center">
            <PerformanceZone />
          </div>
        </div>

        <div className="flex flex-row border-t border-gray-300 my-6">
          <div className="w-[50%] p-8">
            <h2 className="text-2xl font-bold mb-4">Key Settings</h2>
            <KeySettingsMapper array={[0.5, 1, 1.5, 2]} />
          </div>

        </div>
        <div className="h-32"></div>
      </div>

    </main>



  )
}
