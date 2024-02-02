#! /usr/bin/env python3
#
# convert_image_to_rgb10.py --- Convert an image to CDI's 10-bit RGB format.
#
# Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
# This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
# License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
#

import sys

try:
    from PIL import Image
    import bitstring
except ImportError:
    sys.stderr.write("Error: Missing dependencies: run 'pip3 install Pillow bitstring'\n")
    sys.exit(1)

def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} INPUT_FILE OUTPUT_FILE")
        print("Converts an image to CDI 10-bit RGB format.")
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2]

    image = Image.open(input_path).convert("RGB")
    image_data = image.load()
    print(f"Input image size is {image.size} bytes.")

    buffer = bitstring.BitArray(bytes())

    with open(output_path, "wb") as output_file:
        for y in range(image.size[1]):
            for x in range(image.size[0]):
                pixel = image_data[x, y]

                # Convert 8-bit color to 10-bit and store in SMPTE 2110-20
                # Linear RGB format (30 bits per pixel, unpadded).
                r = bitstring.Bits(uint=pixel[0] * 4, length=10)
                buffer.append(r)

                g = bitstring.Bits(uint=pixel[1] * 4, length=10)
                buffer.append(g)

                b = bitstring.Bits(uint=pixel[2] * 4, length=10)
                buffer.append(b)

        print(f"Output image size is {len(buffer.bytes)} bytes.")
        output_file.write(buffer.bytes)

if __name__ == "__main__":
    main()
