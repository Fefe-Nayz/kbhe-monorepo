// Décalage par rapport au zéro de la LUT
int OFFSET[6] = { -13, -13, -13, -13, -13, -13 };

int getCorrectedValue(int sensor_index, int voltage) {
    int offset = OFFSET[sensor_index];
    return voltage - offset;
}