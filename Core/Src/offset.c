// Décalage par rapport au zéro de la LUT
int LUT_ZERO_VALUE = 2118;
int KEY_ZERO_VALUES[6] = { 2101, 2110, 2129, 2132, 2117, 2105 };
int OFFSET[6] = {0,0,0,0,0,0};

// Calcule dynamiquement les offsets à partir des valeurs zéro mesurées
void offsetInit() {
    for (int i = 0; i < 6; ++i) {
        OFFSET[i] = KEY_ZERO_VALUES[i] - LUT_ZERO_VALUE;
    }
}

int getCorrectedValue(int sensor_index, int voltage) {
    if (sensor_index < 0 || sensor_index >= 6) return voltage;
    return voltage - OFFSET[sensor_index];
}