import { useState, useEffect } from "react";

export function useScreenScale(): number {
    // Reference screen (1280x720) and max screen (3840x2160)
    const minW = 1280;
    const maxW = 3840;

    const minScale = 1;
    const maxScale = 1.4;

    const [scale, setScale] = useState(() => {
        const width = window.innerWidth
        const a = (width - minW) / (maxW - minW);
        return minScale + a * (maxScale - minScale);
    });

    useEffect(() => {
        const handleResize = () => {
        const width = window.innerWidth
        const a = (width - minW) / (maxW - minW);
        setScale(minScale + a * (maxScale - minScale));
        };

        window.addEventListener("resize", handleResize);
        return () => window.removeEventListener("resize", handleResize);
    }, []);

    return scale;
}
