# @readability.c

@readability.c is a simple C implementation inspired by Mozilla's Readability.js, which is used in Firefox's readability View. This program extracts the main content from web pages, removing clutter and presenting the article in a clean, readable format.

## Features

- Fetches web pages using libcurl
- Parses HTML using libxml2
- Extracts main article content
- Converts HTML to Markdown-like text
- Outputs extracted content to console or as JSON
- Provides basic metadata extraction (title, author, description, etc.)

## Requirements

- GCC
- libxml2
- libcurl

## Installation

1. Clone this repository:
   ```
   git clone https://github.com/l4time/readability-c.git
   cd readability-c
   ```

2. Compile the program:
   ```
   gcc readability.c -o readability `xml2-config --cflags --libs` -lcurl
   ```

## Usage

```
./readability <url> [-json]
```

- `<url>`: The URL of the web page you want to extract content from
- `-json`: (Optional) Output the result in JSON format

## Examples

1. Extract content and display in console:
   ```
   ./readability https://wccftech.com/apple-vision-pro-yet-to-ship-100000-units-in-us-sales-drop-inbound/
   ```

   Output:
   ```
   Title: Apple Vision Pro Has Yet To Ship 100,000 Units In The U.S., With The Mixed-Reality Headset Expected To Witness A 75 Percent Sales Drop

   Description: A report estimates that the Apple Vision Pro is selling incredibly poorly in the U.S., and may not even reach half a million global shipments by the end of 2024

   Site Name: Wccftech

   URL Source: https://wccftech.com/apple-vision-pro-yet-to-ship-100000-units-in-us-sales-drop-inbound/

   Published Time: 2024-07-11T13:58:26+00:00

   Markdown Content:

   The technological superiority of the [Apple Vision Pro](https://wccftech.com/apple-vision-pro-is-the-ar-headset-from-the-company/) was insufficient for the headset to gain traction in the U.S., with the latest statistics revealing that the device has yet to ship 100,000 units in this market. The disappointing reception of the mixed-reality headset also means that it might not ship even half a million units, but its rapid decline could receive some cushion as Apple has expanded the launch in other countries.

   ## Apple Vision Pro might not ship 500,000 units in 2024, but launch in other regions will offset a steeper decline in demand
   As reported by IDC, with the statistics spotted by Bloomberg, the Apple Vision Pro is projected to witness a 75 percent drop in shipments in the current quarter. One of the biggest reasons for this decline is the ludicrous pricing of the head-mounted wearable, with the base model starting from a whopping $3,499. However, analyst Ming-Chi Kuo has pointed out that it is not just the eye-watering price that has discouraged customers from making the headset their daily driver. He previously mentioned that customers who pay full price and use it [often return after a few tries](https://wccftech.com/apple-vision-pro-low-cost-version-affordable-price-is-not-only-hurdle/), hinting at the Apple Vision Pro’s limited utility.

   ...

   Article extracted
   Execution time: 0.065062 seconds
   Memory usage: 15 MB
   ```

2. Extract content and output as JSON:
   ```
   ./readability https://wccftech.com/apple-vision-pro-yet-to-ship-100000-units-in-us-sales-drop-inbound/ -json
   ```

   Output will be in JSON format, containing the title, URL, published time, and content.

## Output

The program will output:
- Article title
- Author (if available)
- Description (if available)
- Site name (if available)
- URL source
- Published time (if available)
- Extracted content in Markdown-like format

When using the `-json` option, the output will be in JSON format, containing the title, URL, published time, and content.

## Performance

The program also outputs execution time and memory usage statistics (when not using the `-json` option). As shown in the example above, the program extracted the article in about 0.065 seconds and used 15 MB of memory.

## Limitations

- The program may not perfectly handle all types of web pages or complex layouts.
- Some websites may block or limit access to their content, which could affect the program's ability to extract articles.
- The quality of the extracted content can vary depending on the structure of the original web page.

## Disclaimer

This is a simple implementation and may not handle all edge cases or complex web page structures as effectively as the original Readability.js. Use it as a starting point for your own projects or for educational purposes.

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request. Areas for improvement could include:
- Better handling of different HTML structures
- Improved metadata extraction
- Support for more output formats
- Enhanced error handling and reporting

## License

This project is licensed under the MIT License.
