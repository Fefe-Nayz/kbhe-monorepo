import BaseKeyboard from "@/components/baseKeyboard"
import PerformanceButtonDiv from "@/components/div/divPerformance"
import KeySettingsMapper from "@/components/div/keySettingsMapper"
import PerformanceZone from "@/components/performance-zone"

export default function Performance() {
  return (
    <main>
      <div className="p-8">
        {/*
        <h1 className="text-3xl font-bold">Performance</h1> 
        <p className="text-gray-600 mt-2">Performance page content</p>
        */}
        <div className="mt-6">
          <BaseKeyboard mode="multi" 
          onButtonClick={(ids) => console.log("All pressed keys:", ids)} />
          <div>
            <PerformanceButtonDiv />
          </div>
        </div>
        <div className="mt-6">
          <PerformanceZone />
        </div>

      </div>
      <div className="flex flex-row border-t border-gray-300 my-6">
              <div className="w-[50%] p-8">
        <h2 className="text-2xl font-bold mb-4">Key Settings</h2>
        <KeySettingsMapper array={[0.5, 1, 1.5, 2]} />
      </div>

      <div>
        <h1>Informations</h1>
      </div>
      </div>




    </main>



  )
}
