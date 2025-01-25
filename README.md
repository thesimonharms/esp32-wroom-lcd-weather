# _ESP32 LCD Weather Display_

This is a project display weather information using an ESP32 and a 1602 I2C LCD Display. Note on the first time going through the 3 pages, all the data will be blank. On the second run through you will see information.



## Before using:

This project requires [panigraph/esp32-idf-hd44780](https://github.com/panigrah/ESP32-IDF-HD44780).

You can include that in your project using `idf.py` like so:

```bash
idf.py add-dependency "panigrah/esp32-idf-hd44780^0.0.3"
```

Define these values in main.c, you can get your lattitude and longitude from [latlong.net](https://www.latlong.net/):

```C
#define WIFI_SSID "YOUR SSID"
#define WIFI_PASS "YOUR WIFI PASSWORD"
#define WEATHER_API_KEY "YOUR OPENWEATHER API KEY"
#define LOCATION_LAT 0.0000
#define LOCATION_LON 0.0000
```

And ensure these values are correct for your set up or change them:

```C
#define LCD_ADDR 0x27
#define SDA_PIN  21
#define SCL_PIN  22
#define LCD_COLS 16
#define LCD_ROWS 2
```

## License


Copyright 2025 Simon Harms

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
