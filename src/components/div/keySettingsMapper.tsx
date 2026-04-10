import { Slider } from "@/components/ui/slider"
import { useState } from "react"


interface KeySettingsMapperProps {
  array?: any[];
}

export default function KeySettingsMapper({ array }: KeySettingsMapperProps) {

    const [actuation, setActuation] = useState<number>(array?.[0] || 50);

    return (
        <div className="space-y-4">
            <div >

                <label className="text-sm font-medium">Actuation point : <span>{actuation}</span></label>
                <Slider 
                className="border border-gray-300 rounded-sm h-5 w-5"
                defaultValue={[50]}
                value={[actuation]} 
                min={0} 
                max={500}
                step={10} 
                onValueChange={(value) => {
                    const newValue = Array.isArray(value) ? value[0] : value;
                    setActuation(newValue);
                }}
                />
            </div>
        </div>
    )
}