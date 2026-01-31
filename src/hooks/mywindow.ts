import { useState, useEffect } from "react";

export function useScreenScale(): number {
  // Reference screen (1280x720)
  const baseWidth = 1280;
  const baseHeight = 720;
  const baseDiagonal = Math.sqrt(baseWidth ** 2 + baseHeight ** 2);

  const minScale = 1;
  const maxScale = 1.6; 

  const [scale, setScale] = useState(() => {
    const diagonal = Math.sqrt(window.innerWidth ** 2 + window.innerHeight ** 2);
    const currentScale = diagonal / baseDiagonal;
    return Math.min(Math.max(currentScale, minScale), maxScale);
  });

  useEffect(() => {
    const handleResize = () => {
      const diagonal = Math.sqrt(window.innerWidth ** 2 + window.innerHeight ** 2);
      const currentScale = diagonal / baseDiagonal;
      setScale(Math.min(Math.max(currentScale, minScale), maxScale));
    };

    window.addEventListener("resize", handleResize);
    return () => window.removeEventListener("resize", handleResize);
  }, []);

  return scale;
}
