import csv
import re
import os

input_file = 'adc_key_1_teensy.txt'
output_file = 'adc_key_1_teensy.csv'

def parse_and_convert():
    # Check if input file exists
    if not os.path.exists(input_file):
        print(f"Error: Input file '{input_file}' not found in {os.getcwd()}")
        return

    try:
        with open(input_file, 'r') as f:
            lines = f.readlines()
    except Exception as e:
        print(f"Error reading file: {e}")
        return

    data = []
    # Regex to match "[index]: value"
    # Matches [1]: 2100
    pattern = re.compile(r'\[(\d+)\]:\s*(\d+)')

    for line in lines:
        match = pattern.search(line)
        if match:
            index = match.group(1)
            value = match.group(2)
            data.append((index, value))
    
    if not data:
        print("No matching data found in file. Expected format: '[index]: value'")
        return

    try:
        with open(output_file, 'w', newline='') as f:
            writer = csv.writer(f)
            writer.writerow(['Index', 'Value'])
            writer.writerows(data)
            
        print(f"Successfully converted {len(data)} lines to {output_file}")
    except Exception as e:
        print(f"Error writing CSV: {e}")

if __name__ == "__main__":
    parse_and_convert()
