import { Button } from "@/components/ui/button";

interface ResetButtonProps {
  text: string;
  onReset: () => void;
}

export default function ResetButton({ text, onReset }: ResetButtonProps) {

  return (
    <>
      <Button className= "bg-destructive text-white text-xs" variant="destructive" onClick={onReset}>
        {text}
      </Button>
    </>

  );
}