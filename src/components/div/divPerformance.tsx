import { Button } from "../ui/button";

export default function PerformanceButtonDiv() {
    return (
        <div className="bg-white flex flex-row justify-between my-3">
            <div className="flex flex-row">
                <Button className="ml-4" variant="outline" onClick={() => console.log('Select all keys')}>Select All</Button>
                <Button className="ml-4" variant="outline" onClick={() => console.log('Deselect all keys')}>Deselect All Keys</Button>
                <Button className="ml-4" variant="outline" onClick={() => console.log('Base mode toggled')}>Show KeyMap</Button>
            </div>
            <div className="">
                <Button className="ml-4 bg-destructive text-white" variant="destructive" onClick={() => console.log('Settings reset')}>Reset Selected</Button>
            </div>
        </div>
    );
}