import ButtonSelectKeys from "@/components/buttons/selectKeys"
import ResetButton from "@/components/buttons/resetButton"
import ToogleBaseMode from "../buttons/baseMode";

export default function PerformanceButtonDiv() {
    return (
        <div className="flex flex-row justify-between my-3">
            <div className="flex flex-row">
                <ButtonSelectKeys />
                <ToogleBaseMode />
            </div>
            <div className="">
                <ResetButton text="Reset Settings" onReset={() => console.log('Settings reset')} />
            </div>
        </div>
    );
}